#include "fsr4-upscale.h"

#include "dlss-upscale.h"

#include "common/logging/log.h"
#include "frame-gen.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/textureCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"
#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "gpu_tiler_shaders/fsr4_exposure_spv.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

// The provider's C API (3rdparty/fsr4-v07): its Vulkan calls go through kyty_fsr4_vk, filled below from the
// emulator's dispatcher (the macros that route them stay out of this file).
#define KYTY_FSR4_VK_NO_MACROS
#include "ffx_vk_fsr4_v07.h"
#include "ffx_vk_fsr4_v07_assets.h"

extern "C" {
KytyFsr4VkFunctions kyty_fsr4_vk {};
// Live (abba.sh): 0 leaves the TAA pass's output as the game's TAA wrote it (FSR 4 stays loaded), for same-process A/Bs
// and screenshots. 2 and 3 (diagnostics): 0 and 1, with 64 texels of the pass's input and output read back and their
// means logged every 60 frames.
volatile std::atomic_uint32_t kyty_local_fsr4_live {1};
// Live: the jitter's signs (0 as configured, 1 both negated, 2 x negated, 3 y negated, 4 none) and the RCAS sharpening
// in percent (UINT32_MAX: KYTY_FSR4_SHARPNESS), for image comparisons in one session.
volatile std::atomic_uint32_t kyty_local_fsr4_jitter {0};
volatile std::atomic_uint32_t kyty_local_fsr4_sharpness {UINT32_MAX};
// Live: the jitter's scale in percent (100: the clip-space offset times half the size).
volatile std::atomic_uint32_t kyty_local_fsr4_jitter_scale {100};
// Live: 1 negates the motion vectors against the configured sign; 2 zeroes them.
volatile std::atomic_uint32_t kyty_local_fsr4_mv {0};
}

namespace Libs::Graphics::Fsr4Upscale {
namespace {

void Say(const char* format, auto... args) {
	LOGF(format, args...);
	std::printf(format, args...);
	std::fflush(stdout);
}

float EnvFloat(const char* name, float fallback) {
	const char* value = std::getenv(name);
	return value != nullptr && *value != '\0' ? static_cast<float>(std::atof(value)) : fallback;
}

struct Settings {
	bool  requested   = false;
	float sharpness   = 0.6f;
	float mv_sign     = -1.0f;
	float jitter_sign = 1.0f;
	bool  hdr         = true;
	bool  exposure    = true;
};

const Settings& Config() {
	static const Settings settings = [] {
		Settings s;
		const char* value = std::getenv("KYTY_FSR4");
		s.requested       = value != nullptr && *value != '\0' && std::string_view(value) != "0";
		// 0.6: as crisp as the game's TAA, which sharpens (10-09, 1-1 still: the game's image has 1.47x the gradient
		// energy of FSR 4's alone; RCAS 0.5 gives 1.21x, 1.0 gives 1.89x).
		s.sharpness       = std::clamp(EnvFloat("KYTY_FSR4_SHARPNESS", 0.6f), 0.0f, 1.0f);
		s.mv_sign         = EnvFloat("KYTY_FSR4_MV_SIGN", -1.0f) > 0.0f ? 1.0f : -1.0f;
		s.jitter_sign     = EnvFloat("KYTY_FSR4_JITTER_SIGN", 1.0f) < 0.0f ? -1.0f : 1.0f;
		s.hdr             = EnvFloat("KYTY_FSR4_HDR", 1.0f) != 0.0f;
		s.exposure        = EnvFloat("KYTY_FSR4_EXPOSURE", 1.0f) != 0.0f;
		return s;
	}();
	return settings;
}

// The device's support (DeviceSupports) and a failure that turned it off for the session.
bool g_device_ready = false;
bool g_failed       = false;

struct OwnImage {
	vk::Image        image;
	vk::DeviceMemory memory;
	vk::ImageView    view;
	bool             initialized = false; // (out of UNDEFINED)
};

// The provider, made at the first TAA pass for its extent.
struct State {
	vk::Device         device;
	vk::PhysicalDevice physical_device;
	FfxInterface       backend {};
	void*              scratch = nullptr;
	ffxContext         context = nullptr;
	bool               backend_made = false;
	vk::Extent2D       extent {};
	// The depth copied into a colour image (the provider samples R32_SFLOAT colour images), through a buffer.
	OwnImage           depth;
	vk::Buffer         depth_buffer;
	vk::DeviceMemory   depth_buffer_memory;
	// FSR 4's output, before the exposure pass writes it into the TAA pass's output.
	OwnImage           color;
	// The exposure pass (fsr4-exposure.comp).
	vk::DescriptorSetLayout set_layout;
	vk::PipelineLayout      pipeline_layout;
	vk::Pipeline            pipeline;
	// Dispatches begun and not yet known complete: (frame id, the tick of the command buffer they went into).
	std::deque<std::pair<uint64_t, uint64_t>> frames;
	uint64_t           next_frame_id = 1;
	bool               reset         = true;
	std::chrono::steady_clock::time_point last_dispatch {};
	uint64_t           dispatches = 0;
	uint64_t           refused    = 0;
};
State g_state;

void Fail(const char* reason) {
	if (!g_failed) Say("FSR 4: %s; the game's TAA alone from now on\n", reason);
	g_failed = true;
}

void FillVulkanFunctions() {
#define KYTY_FSR4_VK(name) kyty_fsr4_vk.name = VULKAN_HPP_DEFAULT_DISPATCHER.name;
#include "kyty-fsr4-vk-list.inc"
#undef KYTY_FSR4_VK
}

uint32_t MemoryType(vk::PhysicalDevice physical_device, uint32_t bits, vk::MemoryPropertyFlags wanted) {
	const auto properties = physical_device.getMemoryProperties();
	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
		if ((bits & (1u << i)) != 0 && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
	return UINT32_MAX;
}

std::filesystem::path AssetDir() {
	if (const char* dir = std::getenv("KYTY_FSR4_DIR"); dir != nullptr && *dir != '\0') return dir;
	return std::filesystem::path("deps") / "fsr4";
}

bool ReadFile(const std::filesystem::path& path, std::vector<char>& bytes) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) return false;
	const auto size = static_cast<size_t>(file.tellg());
	bytes.resize(size);
	file.seekg(0);
	return static_cast<bool>(file.read(bytes.data(), static_cast<std::streamsize>(size)));
}

void DestroyImage(OwnImage& image) {
	auto& s = g_state;
	if (image.view) s.device.destroyImageView(image.view);
	if (image.image) s.device.destroyImage(image.image);
	if (image.memory) s.device.freeMemory(image.memory);
	image = {};
}

void DestroyProvider() {
	auto& s = g_state;
	if (s.context != nullptr) ffxFsr4V07DestroyContext(&s.context, nullptr);
	s.context = nullptr;
	if (s.backend_made) ffxFsr4VkDestroyContext(static_cast<FfxFsr4VkContext*>(s.backend.device));
	s.backend_made = false;
	std::free(s.scratch);
	s.scratch = nullptr;
	DestroyImage(s.depth);
	DestroyImage(s.color);
	if (s.depth_buffer) s.device.destroyBuffer(s.depth_buffer);
	if (s.depth_buffer_memory) s.device.freeMemory(s.depth_buffer_memory);
	s.depth_buffer        = nullptr;
	s.depth_buffer_memory = nullptr;
	s.frames.clear();
}

bool MakeImage(OwnImage& own, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage) {
	auto&               s = g_state;
	vk::ImageCreateInfo image {};
	image.imageType   = vk::ImageType::e2D;
	image.format      = format;
	image.extent      = vk::Extent3D {extent.width, extent.height, 1};
	image.mipLevels   = 1;
	image.arrayLayers = 1;
	image.samples     = vk::SampleCountFlagBits::e1;
	image.tiling      = vk::ImageTiling::eOptimal;
	image.usage       = usage;
	if (s.device.createImage(&image, nullptr, &own.image) != vk::Result::eSuccess) return false;
	const auto             needs = s.device.getImageMemoryRequirements(own.image);
	vk::MemoryAllocateInfo allocate {};
	allocate.allocationSize  = needs.size;
	allocate.memoryTypeIndex = MemoryType(s.physical_device, needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	if (allocate.memoryTypeIndex == UINT32_MAX || s.device.allocateMemory(&allocate, nullptr, &own.memory) != vk::Result::eSuccess ||
	    s.device.bindImageMemory(own.image, own.memory, 0) != vk::Result::eSuccess)
		return false;
	vk::ImageViewCreateInfo view {};
	view.image            = own.image;
	view.viewType         = vk::ImageViewType::e2D;
	view.format           = format;
	view.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	own.initialized       = false;
	return s.device.createImageView(&view, nullptr, &own.view) == vk::Result::eSuccess;
}

bool MakeOwnResources(vk::Extent2D extent) {
	auto& s = g_state;
	if (!MakeImage(s.depth, vk::Format::eR32Sfloat, extent,
	               vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst) ||
	    !MakeImage(s.color, vk::Format::eR16G16B16A16Sfloat, extent,
	               vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc))
		return false;
	vk::BufferCreateInfo buffer {};
	buffer.size  = uint64_t {extent.width} * extent.height * 4u;
	buffer.usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
	if (s.device.createBuffer(&buffer, nullptr, &s.depth_buffer) != vk::Result::eSuccess) return false;
	const auto             needs = s.device.getBufferMemoryRequirements(s.depth_buffer);
	vk::MemoryAllocateInfo allocate {};
	allocate.allocationSize  = needs.size;
	allocate.memoryTypeIndex = MemoryType(s.physical_device, needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	if (allocate.memoryTypeIndex == UINT32_MAX ||
	    s.device.allocateMemory(&allocate, nullptr, &s.depth_buffer_memory) != vk::Result::eSuccess ||
	    s.device.bindBufferMemory(s.depth_buffer, s.depth_buffer_memory, 0) != vk::Result::eSuccess)
		return false;
	if (s.pipeline) return true;
	// The exposure pass: two storage images and the constants, pushed per dispatch.
	std::array<vk::DescriptorSetLayoutBinding, 3> bindings {};
	for (uint32_t i = 0; i < bindings.size(); ++i) {
		bindings[i].binding         = i;
		bindings[i].descriptorType  = i < 2 ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eStorageBuffer;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags      = vk::ShaderStageFlagBits::eCompute;
	}
	vk::DescriptorSetLayoutCreateInfo set {};
	set.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	set.bindingCount = static_cast<uint32_t>(bindings.size());
	set.pBindings    = bindings.data();
	if (s.device.createDescriptorSetLayout(&set, nullptr, &s.set_layout) != vk::Result::eSuccess) return false;
	const vk::PushConstantRange  constants {vk::ShaderStageFlagBits::eCompute, 0, 3 * sizeof(uint32_t)};
	vk::PipelineLayoutCreateInfo layout {};
	layout.setLayoutCount         = 1;
	layout.pSetLayouts            = &s.set_layout;
	layout.pushConstantRangeCount = 1;
	layout.pPushConstantRanges    = &constants;
	if (s.device.createPipelineLayout(&layout, nullptr, &s.pipeline_layout) != vk::Result::eSuccess) return false;
	vk::ShaderModuleCreateInfo shader {};
	shader.codeSize = sizeof(FSR4_EXPOSURE_SPV);
	shader.pCode    = FSR4_EXPOSURE_SPV;
	vk::ShaderModule module;
	if (s.device.createShaderModule(&shader, nullptr, &module) != vk::Result::eSuccess) return false;
	vk::ComputePipelineCreateInfo pipeline {};
	pipeline.stage.stage  = vk::ShaderStageFlagBits::eCompute;
	pipeline.stage.module = module;
	pipeline.stage.pName  = "main";
	pipeline.layout       = s.pipeline_layout;
	const auto made       = s.device.createComputePipelines(nullptr, 1, &pipeline, nullptr, &s.pipeline);
	s.device.destroyShaderModule(module, nullptr);
	return made == vk::Result::eSuccess;
}

// The backend (pipelines from the asset set's SPIR-V) and the upscaler context, at `extent` for both the render and
// the output size (native AA).
bool MakeProvider(GraphicContext& graphics, vk::Extent2D extent) {
	auto& s           = g_state;
	s.device          = graphics.device;
	s.physical_device = graphics.physical_device;
	FfxFsr4V07AssetSet assets {};
	if (!ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_NATIVE_AA, extent.width, extent.height, &assets)) {
		Fail("no model graph for this output size");
		return false;
	}
	const auto dir = AssetDir();
	std::array<std::vector<char>, FFX_FSR4_VK_PASS_COUNT> spirv;
	std::array<const char*, FFX_FSR4_VK_PASS_COUNT>       names {};
	std::array<std::string, FFX_FSR4_VK_PASS_COUNT>       entries;
	names[0]   = assets.pre;
	entries[0] = "main";
	for (uint32_t i = 1; i <= FFX_FSR4_MODEL_PASS_COUNT; ++i) {
		names[i]   = assets.model[i - 1];
		entries[i] = "fsr4_model_v07_i8_pass" + std::to_string(i);
	}
	names[13]   = assets.post;
	names[14]   = assets.rcas;
	names[15]   = assets.spdAutoExposure;
	entries[13] = entries[14] = entries[15] = "main";
	FfxFsr4VkCreateInfo create {};
	for (uint32_t i = 0; i < FFX_FSR4_VK_PASS_COUNT; ++i) {
		if (!ReadFile(dir / names[i], spirv[i]) || spirv[i].empty() || spirv[i].size() % 4 != 0) {
			Say("FSR 4: shader %s missing or invalid in %s\n", names[i], dir.string().c_str());
			Fail("its shader files are not installed");
			return false;
		}
		create.shaders[i].spirv      = reinterpret_cast<const uint32_t*>(spirv[i].data());
		create.shaders[i].sizeBytes  = spirv[i].size();
		create.shaders[i].entryPoint = entries[i].c_str();
	}
	std::vector<char> initializer;
	std::vector<char> weights;
	if (!ReadFile(dir / assets.initializer, initializer) || initializer.size() != FFX_FSR4_V07_INITIALIZER_BYTES ||
	    !ReadFile(dir / assets.prePassWeights, weights) || weights.size() != FFX_FSR4_V07_PRE_PASS_WEIGHTS_BYTES) {
		Say("FSR 4: model files %s / %s missing or of the wrong size in %s\n", assets.initializer, assets.prePassWeights,
		    dir.string().c_str());
		Fail("its model files are not installed");
		return false;
	}
	FillVulkanFunctions();
	create.device               = static_cast<VkDevice>(graphics.device);
	create.physicalDevice       = static_cast<VkPhysicalDevice>(graphics.physical_device);
	create.allocator            = nullptr;
	create.modelInitializer     = initializer.data();
	create.modelInitializerSize = initializer.size();
	create.prePassWeights       = weights.data();
	create.prePassWeightsSize   = weights.size();
	s.scratch                   = std::calloc(1, ffxFsr4VkGetScratchMemorySize());
	create.scratchBuffer        = s.scratch;
	create.scratchBufferSize    = ffxFsr4VkGetScratchMemorySize();
	if (const auto result = ffxFsr4VkCreateContext(&create, &s.backend); result != VK_SUCCESS) {
		Say("FSR 4: the Vulkan backend failed (%d)\n", static_cast<int>(result));
		Fail("its Vulkan backend could not be made");
		return false;
	}
	s.backend_made = true;
	ffxCreateContextDescUpscale upscale {};
	upscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	upscale.flags       = (Config().hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0) |
	                (Config().exposure ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0) | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
	                FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
	upscale.maxRenderSize  = {extent.width, extent.height};
	upscale.maxUpscaleSize = {extent.width, extent.height};
	ffxFsr4V07SetBackendInterface(&s.backend);
	const auto created = ffxFsr4V07CreateContext(&s.context, &upscale.header, nullptr);
	ffxFsr4V07SetBackendInterface(nullptr);
	if (created != FFX_API_RETURN_OK) {
		Say("FSR 4: the upscaler context failed (%u)\n", static_cast<unsigned>(created));
		Fail("its upscaler context could not be made");
		return false;
	}
	if (!MakeOwnResources(extent)) {
		Fail("its images or exposure pass could not be made");
		return false;
	}
	s.extent = extent;
	s.reset  = true;
	Say("FSR 4: ready, native AA %ux%u (model v07 INT8, %s tier, %s)\n", extent.width, extent.height, assets.tier,
	    dir.string().c_str());
	return true;
}

FfxApiResource Texture(vk::ImageView view, uint32_t format, vk::Extent2D extent, uint32_t state) {
	FfxApiResource resource {};
	resource.resource             = static_cast<VkImageView>(view);
	resource.description.type     = FFX_RESOURCE_TYPE_TEXTURE2D;
	resource.description.format   = format;
	resource.description.width    = extent.width;
	resource.description.height   = extent.height;
	resource.description.depth    = 1;
	resource.description.mipCount = 1;
	resource.state                = state;
	return resource;
}

// The external image's layout as the emulator tracks it, kept by the provider (it goes to GENERAL and back).
bool Register(vk::Image image, vk::ImageView view, vk::ImageLayout layout) {
	FfxFsr4VkExternalImageState state {};
	state.structSize        = sizeof(state);
	state.image             = static_cast<VkImage>(image);
	state.view              = static_cast<VkImageView>(view);
	state.layout            = static_cast<VkImageLayout>(layout);
	state.stageMask         = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	state.accessMask        = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	state.restoreLayout     = static_cast<VkImageLayout>(layout);
	state.restoreStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	state.restoreAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	return ffxFsr4VkSetExternalImageState(&g_state.backend, &state) == VK_SUCCESS;
}

// Room for one more dispatch: the provider keeps three unretired; the oldest's command buffer is waited for when full.
bool BeginFrame(CommandScheduler& scheduler, uint64_t& frame_id) {
	auto&      s      = g_state;
	auto&      master = scheduler.GetMasterSemaphore();
	const auto retire = [&] {
		master.Refresh();
		const uint64_t completed = master.KnownGpuTick();
		uint64_t       done      = 0;
		while (!s.frames.empty() && s.frames.front().second <= completed) {
			done = s.frames.front().first;
			s.frames.pop_front();
		}
		if (done != 0) (void)ffxFsr4VkRetireFrame(&s.backend, done);
	};
	retire();
	frame_id    = s.next_frame_id;
	auto result = ffxFsr4VkBeginFrame(&s.backend, frame_id);
	if (result == VK_NOT_READY && !s.frames.empty()) {
		master.Wait(s.frames.front().second);
		retire();
		result = ffxFsr4VkBeginFrame(&s.backend, frame_id);
	}
	if (result != VK_SUCCESS) return false;
	++s.next_frame_id;
	s.frames.emplace_back(frame_id, scheduler.CurrentTick());
	return true;
}

void Barrier(vk::CommandBuffer command, vk::PipelineStageFlags destination) {
	VulkanMemoryBarrier barrier {};
	barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer, destination, {}, 1,
	                        &barrier, 0, nullptr, 0, nullptr);
}

// (Diagnostics, kyty_local_fsr4_live 2 and 3.)
struct Probe {
	vk::Buffer                                   buffer;
	vk::DeviceMemory                             memory;
	void*                                        mapped = nullptr;
	std::array<std::pair<uint64_t, uint32_t>, 4> slots {}; // (tick, live mode) per slot; tick 0 = free
	uint32_t                                     next  = 0;
	uint64_t                                     lines = 0;
};
Probe              g_probe;
constexpr uint32_t ProbePoints    = 64;
constexpr uint64_t ProbeSlotBytes = ProbePoints * 2 * 8;

float Half(uint16_t h) {
	const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 31, mantissa = h & 1023;
	const float    value = exponent == 0    ? std::ldexp(static_cast<float>(mantissa), -24)
	                       : exponent == 31 ? (mantissa != 0 ? NAN : INFINITY)
	                                        : std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25);
	return sign != 0 ? -value : value;
}

void ProbeRead(CommandScheduler& scheduler) {
	auto& p = g_probe;
	if (p.mapped == nullptr) return;
	auto& master = scheduler.GetMasterSemaphore();
	master.Refresh();
	for (uint32_t i = 0; i < p.slots.size(); ++i) {
		auto& [tick, mode] = p.slots[i];
		if (tick == 0 || tick > master.KnownGpuTick()) continue;
		const auto* words = reinterpret_cast<const uint16_t*>(static_cast<const char*>(p.mapped) + i * ProbeSlotBytes);
		std::array<double, 8> sum {};
		uint32_t              count = 0;
		for (uint32_t k = 0; k < ProbePoints; ++k) {
			std::array<float, 8> v {};
			for (uint32_t c = 0; c < 4; ++c) v[c] = Half(words[(k * 2) * 4 + c]), v[4 + c] = Half(words[(k * 2 + 1) * 4 + c]);
			if (!std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); })) continue;
			for (uint32_t c = 0; c < 8; ++c) sum[c] += v[c];
			++count;
		}
		if (count != 0 && p.lines++ % 60 == 0)
			Say("FSR4 PROBE %s: in %.4f %.4f %.4f a %.4f | out %.4f %.4f %.4f a %.4f | out/in %.3f (%u texels)\n",
			    mode == 3 ? "fsr4" : "game", sum[0] / count, sum[1] / count, sum[2] / count, sum[3] / count, sum[4] / count,
			    sum[5] / count, sum[6] / count, sum[7] / count, (sum[4] + sum[5] + sum[6]) / std::max(1e-9, sum[0] + sum[1] + sum[2]),
			    count);
		tick = 0;
	}
}

void ProbeCopy(GraphicContext& graphics, CommandScheduler& scheduler, vk::CommandBuffer command, Image& input, Image& output,
               uint32_t mode) {
	auto& p = g_probe;
	if (p.mapped == nullptr) {
		vk::BufferCreateInfo info {};
		info.size  = ProbeSlotBytes * p.slots.size();
		info.usage = vk::BufferUsageFlagBits::eTransferDst;
		if (graphics.device.createBuffer(&info, nullptr, &p.buffer) != vk::Result::eSuccess) return;
		const auto             needs = graphics.device.getBufferMemoryRequirements(p.buffer);
		vk::MemoryAllocateInfo allocate {};
		allocate.allocationSize  = needs.size;
		allocate.memoryTypeIndex = MemoryType(graphics.physical_device, needs.memoryTypeBits,
		                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		if (allocate.memoryTypeIndex == UINT32_MAX ||
		    graphics.device.allocateMemory(&allocate, nullptr, &p.memory) != vk::Result::eSuccess ||
		    graphics.device.bindBufferMemory(p.buffer, p.memory, 0) != vk::Result::eSuccess ||
		    graphics.device.mapMemory(p.memory, 0, VK_WHOLE_SIZE, {}, &p.mapped) != vk::Result::eSuccess)
			return;
	}
	ProbeRead(scheduler);
	const uint32_t slot = p.next++ % p.slots.size();
	if (p.slots[slot].first != 0) return; // (still in flight)
	input.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, ImageSubresourceRange {}, command);
	output.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, ImageSubresourceRange {}, command);
	for (uint32_t which = 0; which < 2; ++which) {
		auto&                                        image = which == 0 ? input : output;
		std::array<vk::BufferImageCopy, ProbePoints> regions {};
		for (uint32_t k = 0; k < ProbePoints; ++k) {
			auto& r            = regions[k];
			r.bufferOffset     = slot * ProbeSlotBytes + (k * 2 + which) * 8;
			r.imageSubresource = vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
			r.imageOffset      = vk::Offset3D {static_cast<int32_t>((k % 8) * image.backing.extent.width / 8 + image.backing.extent.width / 16),
                                          static_cast<int32_t>((k / 8) * image.backing.extent.height / 8 +
                                                               image.backing.extent.height / 16),
                                          0};
			r.imageExtent      = vk::Extent3D {1, 1, 1};
		}
		command.copyImageToBuffer(image.backing.image, vk::ImageLayout::eTransferSrcOptimal, p.buffer, ProbePoints, regions.data());
	}
	p.slots[slot] = {scheduler.CurrentTick(), mode};
}

} // namespace

bool Requested() {
	return Config().requested;
}

void AddDeviceExtensions(const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions) {
	if (!Requested()) return;
	const bool has = std::any_of(available.begin(), available.end(), [](const auto& extension) {
		return std::strcmp(extension.extensionName, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) == 0;
	});
	if (has) extensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
}

bool DeviceSupports(vk::PhysicalDevice physical_device, const std::vector<const char*>& extensions,
                    const vk::PhysicalDeviceFeatures& features, const vk::PhysicalDeviceVulkan12Features& features12,
                    const vk::PhysicalDeviceVulkan13Features& features13) {
	g_device_ready = false;
	if (!Requested()) return false;
	const bool extension = std::any_of(extensions.begin(), extensions.end(), [](const char* name) {
		return std::strcmp(name, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) == 0;
	});
	vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives {};
	vk::PhysicalDeviceFeatures2                            features2 {};
	features2.pNext = &derivatives;
	if (extension) physical_device.getFeatures2(&features2);
	vk::PhysicalDeviceSubgroupProperties subgroup {};
	vk::PhysicalDeviceProperties2        properties {};
	properties.pNext = &subgroup;
	physical_device.getProperties2(&properties);
	const bool quads   = (subgroup.supportedOperations & vk::SubgroupFeatureFlagBits::eQuad) &&
	                   (subgroup.supportedStages & vk::ShaderStageFlagBits::eCompute);
	const bool formats = [&] {
		for (const auto format: {vk::Format::eR8G8B8A8Unorm, vk::Format::eR16G16Sfloat, vk::Format::eR16G16B16A16Sfloat,
		                         vk::Format::eR32Sfloat}) {
			const auto format_properties = physical_device.getFormatProperties(format);
			const auto wanted            = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage;
			if ((format_properties.optimalTilingFeatures & wanted) != wanted) return false;
		}
		return true;
	}();
	g_device_ready = extension && derivatives.computeDerivativeGroupLinear && features.shaderInt16 &&
	                 features.shaderStorageImageExtendedFormats && features12.shaderInt8 && features12.shaderFloat16 &&
	                 features13.shaderIntegerDotProduct && quads && formats;
	Say("FSR 4: %s (compute derivatives %d, int8 %d, float16 %d, int16 %d, dot product %d, quad ops %d, formats %d)\n",
	    g_device_ready ? "the device has what its shaders need" : "the device lacks what its shaders need: off",
	    extension && derivatives.computeDerivativeGroupLinear, features12.shaderInt8, features12.shaderFloat16, features.shaderInt16,
	    features13.shaderIntegerDotProduct, quads ? 1 : 0, formats ? 1 : 0);
	return g_device_ready;
}

bool Active() {
	return (g_device_ready || DlssUpscale::Ready()) && !g_failed;
}

bool Record(GraphicContext& graphics, TextureCache& cache, CommandScheduler& scheduler, vk::CommandBuffer command,
            const PreparedBindings& bindings) {
	if (!Active()) return false;
	auto&      s    = g_state;
	const auto live = kyty_local_fsr4_live.load(std::memory_order_relaxed);
	// The TAA pass (frame capture, 10-09): 0 the scene colour (RGBA16F), 1 depth (D32S8), 3 motion vectors (RG16F),
	// 6 its output (RGBA16F storage), all at the render size; buffer 0 its constants (the exposure first).
	if (bindings.images.size() < 7 || bindings.buffers.empty() || bindings.buffer_sources.empty()) return ++s.refused, false;
	const auto& color_binding  = bindings.images[0];
	const auto& depth_binding  = bindings.images[1];
	const auto& motion_binding = bindings.images[3];
	const auto& output_binding = bindings.images[6];
	if (!color_binding.image_id || !depth_binding.image_id || !motion_binding.image_id || !output_binding.image_id ||
	    !color_binding.image_view || !motion_binding.image_view || !output_binding.image_view)
		return ++s.refused, false;
	auto&      color  = cache.GetImage(color_binding.image_id);
	auto&      depth  = cache.GetImage(depth_binding.image_id);
	auto&      motion = cache.GetImage(motion_binding.image_id);
	auto&      output = cache.GetImage(output_binding.image_id);
	const auto extent = vk::Extent2D {color.backing.extent.width, color.backing.extent.height};
	const auto same   = [&](const Image& image) {
        return image.backing.extent.width == extent.width && image.backing.extent.height == extent.height;
	};
	if (color.backing.format != vk::Format::eR16G16B16A16Sfloat || motion.backing.format != vk::Format::eR16G16Sfloat ||
	    output.backing.format != vk::Format::eR16G16B16A16Sfloat ||
	    (depth.backing.format != vk::Format::eD32SfloatS8Uint && depth.backing.format != vk::Format::eD32Sfloat) ||
	    !same(depth) || !same(motion) || !same(output) || bindings.buffer_sources[0].size < 4 ||
	    bindings.buffers[0].range < bindings.buffer_sources[0].size)
		return ++s.refused, false;
	if (live == 0 || live == 2) {
		s.reset = true; // (FSR 4's history is stale when it comes back)
		if (live == 2) ProbeCopy(graphics, scheduler, command, color, output, live);
		return false;
	}
	const auto* camera = FrameGen::CurrentCamera();
	if (camera == nullptr) return ++s.refused, false; // (no G-buffer pass seen this frame: no jitter to give)
	// DLSS (KYTY_DLSS) when NGX has it, otherwise FSR 4.
	const bool dlss = DlssUpscale::Ready();
	if (s.extent != extent) {
		if (s.context != nullptr || s.color.image) {
			// A new size: the old work may still run.
			(void)graphics.device.waitIdle();
			DestroyProvider();
		}
		if (dlss) {
			s.device          = graphics.device;
			s.physical_device = graphics.physical_device;
			if (!MakeOwnResources(extent)) {
				Fail("its images or exposure pass could not be made");
				return false;
			}
			s.extent = extent;
			s.reset  = true;
		} else if (!MakeProvider(graphics, extent)) {
			return false;
		}
	}
	uint64_t frame_id = 0;
	if (!dlss && !BeginFrame(scheduler, frame_id)) {
		Fail("no free frame for its descriptors");
		return false;
	}
	// The game's TAA wrote the output; FSR 4 reads the same inputs.
	Barrier(command, vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer);

	// The depth into the colour image the provider samples, through a buffer (a depth aspect copies only so).
	depth.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, ImageSubresourceRange {}, command);
	vk::BufferImageCopy region {};
	region.imageSubresource = vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
	region.imageExtent      = vk::Extent3D {extent.width, extent.height, 1};
	command.copyImageToBuffer(depth.backing.image, vk::ImageLayout::eTransferSrcOptimal, s.depth_buffer, 1, &region);
	const auto to_layout = [&](OwnImage& own, vk::ImageLayout from, vk::ImageLayout to, vk::AccessFlags source,
	                           vk::AccessFlags destination, vk::PipelineStageFlags source_stage,
	                           vk::PipelineStageFlags destination_stage) {
		vk::ImageMemoryBarrier barrier {};
		barrier.srcAccessMask       = source;
		barrier.dstAccessMask       = destination;
		barrier.oldLayout           = own.initialized ? from : vk::ImageLayout::eUndefined;
		barrier.newLayout           = to;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image               = own.image;
		barrier.subresourceRange    = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		command.pipelineBarrier(source_stage, destination_stage, {}, 0, nullptr, 0, nullptr, 1, &barrier);
		own.initialized = true;
	};
	vk::BufferMemoryBarrier copied {};
	copied.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
	copied.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
	copied.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	copied.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	copied.buffer              = s.depth_buffer;
	copied.size                = VK_WHOLE_SIZE;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 1, &copied,
	                        0, nullptr);
	to_layout(s.depth, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eShaderRead,
	          vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer);
	region.imageSubresource = vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
	command.copyBufferToImage(s.depth_buffer, s.depth.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
	to_layout(s.depth, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eTransferWrite,
	          vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader);
	to_layout(s.color, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead,
	          vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader);

	const auto now   = std::chrono::steady_clock::now();
	const auto delta = s.last_dispatch == std::chrono::steady_clock::time_point {}
	                       ? 16.7f
	                       : std::chrono::duration<float, std::milli>(now - s.last_dispatch).count();
	// A gap (a loading screen, a menu) leaves nothing of the old history worth keeping.
	if (delta > 500.0f) s.reset = true;
	s.last_dispatch = now;

	// As frame generation takes them (fsr-frame-gen.cpp): the jitter in pixels from the clip-space offset, the vectors
	// (UV units along the motion) scaled to pixels towards the previous position.
	const float     width            = static_cast<float>(extent.width);
	const float     height           = static_cast<float>(extent.height);
	const auto      jitter_mode      = kyty_local_fsr4_jitter.load(std::memory_order_relaxed);
	static uint32_t last_jitter_mode = 0;
	if (std::exchange(last_jitter_mode, jitter_mode) != jitter_mode) s.reset = true;
	const float jitter_scale = static_cast<float>(kyty_local_fsr4_jitter_scale.load(std::memory_order_relaxed)) / 100.0f;
	const float jitter_x     = jitter_scale * (jitter_mode == 4 ? 0.0f : (jitter_mode == 1 || jitter_mode == 2 ? -1.0f : 1.0f));
	const float jitter_y     = jitter_scale * (jitter_mode == 4 ? 0.0f : (jitter_mode == 1 || jitter_mode == 3 ? -1.0f : 1.0f));
	// (Mode 5: the previous frame's jitter, in case the G-buffer pass's SRT holds the one the TAA does not use.)
	static std::array<float, 2> previous_ndc {};
	const auto                  ndc = jitter_mode == 5 ? previous_ndc : camera->jitter_ndc;
	previous_ndc                    = camera->jitter_ndc;
	const std::array<float, 2> jitter {jitter_x * Config().jitter_sign * ndc[0] * width * 0.5f,
	                                   jitter_y * Config().jitter_sign * -ndc[1] * height * 0.5f};
	const auto                 mv_mode = kyty_local_fsr4_mv.load(std::memory_order_relaxed);
	const float                mv_sign = mv_mode == 2 ? 0.0f : (mv_mode == 1 ? -Config().mv_sign : Config().mv_sign);
	const std::array<float, 2> motion_scale {mv_sign * width, mv_sign * height};

	if (dlss) {
		// NGX transitions nothing: its inputs and output in GENERAL.
		color.Transit(vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderRead, ImageSubresourceRange {}, command);
		motion.Transit(vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderRead, ImageSubresourceRange {}, command);
		DlssUpscale::Inputs inputs;
		inputs.color        = {color.backing.image, color_binding.image_view, color.backing.format};
		inputs.motion       = {motion.backing.image, motion_binding.image_view, motion.backing.format};
		inputs.depth        = {s.depth.image, s.depth.view, vk::Format::eR32Sfloat};
		inputs.output       = {s.color.image, s.color.view, vk::Format::eR16G16B16A16Sfloat};
		inputs.extent       = extent;
		inputs.jitter_px    = jitter;
		inputs.motion_scale = motion_scale;
		inputs.frame_ms     = std::clamp(delta, 1.0f, 100.0f);
		inputs.reset        = s.reset;
		if (!DlssUpscale::Evaluate(graphics, command, inputs)) {
			Fail("DLSS's evaluation failed");
			return false;
		}
	} else {
		// The inputs stay in the layouts the dispatch's bindings put them in; the provider returns them there.
		if (!Register(color.backing.image, color_binding.image_view, color.CurrentState().layout) ||
		    !Register(motion.backing.image, motion_binding.image_view, motion.CurrentState().layout) ||
		    !Register(s.depth.image, s.depth.view, vk::ImageLayout::eGeneral) ||
		    !Register(s.color.image, s.color.view, vk::ImageLayout::eGeneral)) {
			Fail("an image's state was refused");
			return false;
		}
		ffxDispatchDescUpscale dispatch {};
		dispatch.header.type   = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
		dispatch.commandList   = static_cast<VkCommandBuffer>(command);
		dispatch.color         = Texture(color_binding.image_view, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT, extent,
		                                 FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.depth         = Texture(s.depth.view, FFX_SURFACE_FORMAT_R32_FLOAT, extent, FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.motionVectors = Texture(motion_binding.image_view, FFX_SURFACE_FORMAT_R16G16_FLOAT, extent,
		                                 FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.output = Texture(s.color.view, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT, extent, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
		dispatch.jitterOffset            = {jitter[0], jitter[1]};
		dispatch.motionVectorScale       = {motion_scale[0], motion_scale[1]};
		dispatch.renderSize              = {extent.width, extent.height};
		dispatch.upscaleSize             = {extent.width, extent.height};
		const auto  sharpness_percent    = kyty_local_fsr4_sharpness.load(std::memory_order_relaxed);
		const float sharpness            = sharpness_percent == UINT32_MAX ? Config().sharpness
		                                                                   : std::min(sharpness_percent, 100u) / 100.0f;
		dispatch.enableSharpening        = sharpness > 0.0f;
		dispatch.sharpness               = sharpness;
		dispatch.enableAutoExposure      = Config().exposure;
		dispatch.frameTimeDelta          = std::clamp(delta, 1.0f, 100.0f);
		dispatch.preExposure             = 1.0f;
		dispatch.reset                   = s.reset;
		dispatch.cameraNear              = 0.1f;
		dispatch.cameraFar               = 10000.0f;
		dispatch.cameraFovAngleVertical  = 1.0f;
		dispatch.viewSpaceToMetersFactor = 1.0f;
		if (const auto result = ffxFsr4V07Dispatch(&s.context, &dispatch.header); result != FFX_API_RETURN_OK) {
			Say("FSR 4: dispatch failed (%u)\n", static_cast<unsigned>(result));
			Fail("its dispatch failed");
			return false;
		}
	}
	s.reset = false;

	// FSR 4's colour into the TAA output, times the exposure the game's TAA applies; the game's alpha stays.
	Barrier(command, vk::PipelineStageFlagBits::eComputeShader);
	const auto& constants  = bindings.buffers[0];
	const auto  misaligned = constants.range - bindings.buffer_sources[0].size; // (NativeStorageBuffer's adjustment)
	const vk::DescriptorImageInfo  fsr4_image {nullptr, s.color.view, vk::ImageLayout::eGeneral};
	const vk::DescriptorImageInfo  taa_image {nullptr, output_binding.image_view, vk::ImageLayout::eGeneral};
	std::array<vk::WriteDescriptorSet, 3> writes {};
	for (uint32_t i = 0; i < writes.size(); ++i) {
		writes[i].dstBinding      = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType  = i < 2 ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eStorageBuffer;
	}
	writes[0].pImageInfo  = &fsr4_image;
	writes[1].pImageInfo  = &taa_image;
	writes[2].pBufferInfo = &constants;
	output.Transit(vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
	               ImageSubresourceRange {}, command);
	const std::array<uint32_t, 3> push {static_cast<uint32_t>(misaligned / 4), extent.width, extent.height};
	command.bindPipeline(vk::PipelineBindPoint::eCompute, s.pipeline);
	command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, s.pipeline_layout, 0, writes);
	command.pushConstants(s.pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), push.data());
	command.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
	Barrier(command, vk::PipelineStageFlagBits::eAllCommands);
	if (live == 3) ProbeCopy(graphics, scheduler, command, color, output, live);

	if (++s.dispatches == 1 || s.dispatches % 3600 == 0)
		Say("%s: %llu frames (%llu TAA passes left to the game alone), jitter %.3f %.3f px\n", dlss ? "DLSS" : "FSR 4",
		    static_cast<unsigned long long>(s.dispatches), static_cast<unsigned long long>(s.refused), jitter[0], jitter[1]);
	return true;
}

} // namespace Libs::Graphics::Fsr4Upscale
