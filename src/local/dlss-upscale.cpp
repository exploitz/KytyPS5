#include "dlss-upscale.h"

#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"
#ifdef KYTY_LOCAL_VULKAN_RECORDING
#include "vulkan-recording.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(KYTY_DLSS_SDK)
#include <filesystem>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
// NVIDIA DLSS SDK headers (_Build/deps/dlss/include, see CMakeLists.txt).
#include <nvsdk_ngx_helpers_vk.h>
#endif

namespace Libs::Graphics::DlssUpscale {
namespace {

void Say(const char* format, auto... args) {
	LOGF(format, args...);
	std::printf(format, args...);
	std::fflush(stdout);
}

bool Switched() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_DLSS");
		return value != nullptr && *value != '\0' && std::string_view(value) != "0";
	}();
	return on;
}

#if defined(KYTY_DLSS_SDK)
// A project id of its own (NVIDIA's guidance for an application without an id from them: the project id with a custom
// engine).
constexpr const char* ProjectId     = "9586cc42-08ac-4f41-8416-d98ebe836009";
constexpr const char* EngineVersion = "1.0";

// The emulator's folder, and deps\dlss in it (nvngx_dlss.dll): absolute, NGX's signed-library loader refuses a
// relative path ("One or more arguments are not correct").
const std::wstring& ExeDir() {
	static const std::wstring dir = [] {
		wchar_t exe[MAX_PATH] {};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		return std::filesystem::path(exe).parent_path().wstring();
	}();
	return dir;
}
const std::wstring& DllDir() {
	static const std::wstring dir = (std::filesystem::path(ExeDir()) / "deps" / "dlss").wstring();
	return dir;
}
const wchar_t* g_dll_paths[1] {};

// NGX's own log lines, in ours (KYTY_DLSS_LOG=2: verbose).
void NVSDK_CONV Log(const char* message, NVSDK_NGX_Logging_Level /*level*/, NVSDK_NGX_Feature /*component*/) {
	Say("NGX: %s%s", message, message[0] != '\0' && message[std::strlen(message) - 1] == '\n' ? "" : "\n");
}

NVSDK_NGX_FeatureCommonInfo CommonInfo() {
	const char*                 verbose = std::getenv("KYTY_DLSS_LOG");
	NVSDK_NGX_FeatureCommonInfo info {};
	g_dll_paths[0]                            = DllDir().c_str();
	info.PathListInfo.Path                    = g_dll_paths;
	info.PathListInfo.Length                  = 1;
	info.LoggingInfo.LoggingCallback          = Log;
	info.LoggingInfo.MinimumLoggingLevel      = verbose != nullptr && std::string_view(verbose) == "2" ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE
	                                                                                                  : NVSDK_NGX_LOGGING_LEVEL_ON;
	info.LoggingInfo.DisableOtherLoggingSinks = true;
	return info;
}

// The names handed to vkCreateInstance / vkCreateDevice (kept: the lists point into these).
std::deque<std::string> g_names;
std::atomic<bool>       g_ready   = false; // (the evaluation may turn it off on the recording worker)
NVSDK_NGX_Parameter*    g_params  = nullptr;
NVSDK_NGX_Handle*       g_feature = nullptr;
vk::Extent2D            g_extent {};

void Add(const char* const* required, uint32_t count, const std::vector<vk::ExtensionProperties>& available,
         std::vector<const char*>& extensions, const char* what) {
	for (uint32_t i = 0; i < count; ++i) {
		const char* name = required[i];
		if (name == nullptr) continue;
		if (std::any_of(extensions.begin(), extensions.end(), [&](const char* e) { return std::strcmp(e, name) == 0; })) continue;
		// Buffer device addresses are Vulkan 1.2's (the device enables the core feature): the EXT one must not be
		// enabled with it, the KHR one adds nothing.
		if (std::string_view(name) == "VK_EXT_buffer_device_address" || std::string_view(name) == "VK_KHR_buffer_device_address") {
			Say("DLSS: %s left out (core in Vulkan 1.2)\n", name);
			continue;
		}
		if (!std::any_of(available.begin(), available.end(),
		                 [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; })) {
			Say("DLSS: the %s extension %s is not available\n", what, name);
			continue;
		}
		extensions.push_back(g_names.emplace_back(name).c_str());
		Say("DLSS: %s extension %s\n", what, name);
	}
}

uint32_t Preset() {
	const char* value = std::getenv("KYTY_DLSS_PRESET");
	if (value == nullptr || value[0] < 'A' || value[0] > 'Z') return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
	return static_cast<uint32_t>(value[0] - 'A' + 1);
}

NVSDK_NGX_Resource_VK Resource(const Image& image, vk::Extent2D extent, bool written) {
	const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	return NVSDK_NGX_Create_ImageView_Resource_VK(static_cast<VkImageView>(image.view), static_cast<VkImage>(image.image), range,
	                                              static_cast<VkFormat>(image.format), extent.width, extent.height, written);
}
#endif

} // namespace

bool Requested() {
#if defined(KYTY_DLSS_SDK)
	return Switched();
#else
	static const bool said = Switched() && (Say("DLSS: this build has no DLSS SDK (_Build/deps/dlss): off\n"), true);
	(void)said;
	return false;
#endif
}

void AddInstanceExtensions(const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions) {
#if defined(KYTY_DLSS_SDK)
	if (!Requested()) return;
	// The static lists (NVSDK_NGX_VULKAN_RequiredExtensions: no Vulkan calls of their own before the instance exists).
	// VK_NVX_binary_import fails the device unless the guest leaves the driver address space (memoryAddressSpace.inc's
	// reserve ceiling, on with KYTY_DLSS).
	unsigned int instance_count = 0, device_count = 0;
	const char** instance_names = nullptr;
	const char** device_names   = nullptr;
	if (const auto result = NVSDK_NGX_VULKAN_RequiredExtensions(&instance_count, &instance_names, &device_count, &device_names);
	    NVSDK_NGX_FAILED(result)) {
		Say("DLSS: no extension list (0x%x)\n", static_cast<unsigned>(result));
		return;
	}
	Add(instance_names, instance_count, available, extensions, "instance");
	Say("DLSS: %u instance extensions asked for\n", instance_count);
#else
	(void)available;
	(void)extensions;
#endif
}

void AddDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physical_device,
                         const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions) {
	(void)instance;
	(void)physical_device;
#if defined(KYTY_DLSS_SDK)
	if (!Requested()) return;
	unsigned int instance_count = 0, device_count = 0;
	const char** instance_names = nullptr;
	const char** device_names   = nullptr;
	if (const auto result = NVSDK_NGX_VULKAN_RequiredExtensions(&instance_count, &instance_names, &device_count, &device_names);
	    NVSDK_NGX_FAILED(result)) {
		Say("DLSS: no extension list (0x%x)\n", static_cast<unsigned>(result));
		return;
	}
	Add(device_names, device_count, available, extensions, "device");
	Say("DLSS: %u device extensions asked for\n", device_count);
#else
	(void)available;
	(void)extensions;
#endif
}

void OnDevice(GraphicContext& graphics) {
#if defined(KYTY_DLSS_SDK)
	if (!Requested()) return;
	const auto common = CommonInfo();
	Say("DLSS: starting NGX\n");
	auto       result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion, ExeDir().c_str(), static_cast<VkInstance>(graphics.instance),
        static_cast<VkPhysicalDevice>(graphics.physical_device), static_cast<VkDevice>(graphics.device),
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr, VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr, &common,
        NVSDK_NGX_Version_API);
	if (NVSDK_NGX_FAILED(result)) {
		Say("DLSS: NGX did not start (0x%x)\n", static_cast<unsigned>(result));
		return;
	}
	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&g_params);
	int available = 0, needs_driver = 0, init_result = 0;
	if (NVSDK_NGX_SUCCEED(result)) {
		(void)NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
		(void)NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
		(void)NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
	}
	g_ready = NVSDK_NGX_SUCCEED(result) && available != 0;
	Say("DLSS: %s%s (capabilities 0x%x, init 0x%x)\n", g_ready ? "ready (DLAA in place of the TAA's resolve)" : "not available",
	    needs_driver != 0 ? ", it needs a newer driver" : "", static_cast<unsigned>(result), static_cast<unsigned>(init_result));
#else
	(void)graphics;
#endif
}

bool Ready() {
#if defined(KYTY_DLSS_SDK)
	return g_ready;
#else
	return false;
#endif
}

#if defined(KYTY_DLSS_SDK)
namespace {

// Records DLSS into `command` (the feature made at the first call for its size, or again at a new one).
bool EvaluateNow(VkDevice device, VkCommandBuffer command, const Inputs& inputs, PFN_vkDeviceWaitIdle wait_idle) {
	if (!g_ready.load(std::memory_order_relaxed)) return false;
	if (g_feature != nullptr && g_extent != inputs.extent) {
		(void)wait_idle(device); // (the old feature's work may still run)
		(void)NVSDK_NGX_VULKAN_ReleaseFeature(g_feature);
		g_feature = nullptr;
	}
	if (g_feature == nullptr) {
		NVSDK_NGX_DLSS_Create_Params create {};
		create.Feature.InWidth            = inputs.extent.width;
		create.Feature.InHeight           = inputs.extent.height;
		create.Feature.InTargetWidth      = inputs.extent.width;
		create.Feature.InTargetHeight     = inputs.extent.height;
		create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
		// The scene colour is linear HDR (pre-exposed: the game's TAA applies its exposure after), the depth reversed,
		// the vectors at the render size.
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
		                              NVSDK_NGX_DLSS_Feature_Flags_AutoExposure | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
		NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, Preset());
		const auto result = NGX_VULKAN_CREATE_DLSS_EXT1(device, command, 1, 1, &g_feature, g_params, &create);
		if (NVSDK_NGX_FAILED(result) || g_feature == nullptr) {
			Say("DLSS: the feature could not be made at %ux%u (0x%x)\n", inputs.extent.width, inputs.extent.height,
			    static_cast<unsigned>(result));
			g_ready = false;
			return false;
		}
		g_extent = inputs.extent;
		Say("DLSS: DLAA at %ux%u, preset %c\n", inputs.extent.width, inputs.extent.height, static_cast<char>('A' + Preset() - 1));
	}
	auto                          color  = Resource(inputs.color, inputs.extent, false);
	auto                          depth  = Resource(inputs.depth, inputs.extent, false);
	auto                          motion = Resource(inputs.motion, inputs.extent, false);
	auto                          output = Resource(inputs.output, inputs.extent, true);
	NVSDK_NGX_VK_DLSS_Eval_Params eval {};
	eval.Feature.pInColor                 = &color;
	eval.Feature.pInOutput                = &output;
	eval.pInDepth                         = &depth;
	eval.pInMotionVectors                 = &motion;
	eval.InJitterOffsetX                  = inputs.jitter_px[0];
	eval.InJitterOffsetY                  = inputs.jitter_px[1];
	eval.InRenderSubrectDimensions.Width  = inputs.extent.width;
	eval.InRenderSubrectDimensions.Height = inputs.extent.height;
	eval.InReset                          = inputs.reset ? 1 : 0;
	eval.InMVScaleX                       = inputs.motion_scale[0];
	eval.InMVScaleY                       = inputs.motion_scale[1];
	eval.InPreExposure                    = 1.0f;
	eval.InExposureScale                  = 1.0f;
	eval.InFrameTimeDeltaInMsec           = inputs.frame_ms;
	if (const auto result = NGX_VULKAN_EVALUATE_DLSS_EXT(command, g_feature, g_params, &eval); NVSDK_NGX_FAILED(result)) {
		Say("DLSS: evaluation failed (0x%x)\n", static_cast<unsigned>(result));
		g_ready = false;
		return false;
	}
	return true;
}

#ifdef KYTY_LOCAL_VULKAN_RECORDING
// NGX records into the command buffer with Vulkan calls of its own, past the recording layer: with the renderer's
// commands recorded on its worker (KYTY_VULKAN_RECORDING, KYTY_DEFERRED_SUBMIT), the evaluation is a packet there, in
// order with them (recorded straight away, it wrote into a command buffer the worker had not begun yet, and the driver
// crashed on the worker).
struct Packet {
	VkDevice        device;
	VkCommandBuffer command;
	Inputs          inputs;
};
static_assert(std::is_trivially_copyable_v<Packet>);

void Replay(std::span<const LocalVulkanRecording::Segment> segments, const vk::detail::DispatchLoaderDynamic& dispatch) {
	Packet packet {};
	std::memcpy(&packet, segments[0].data, sizeof(packet));
	(void)EvaluateNow(packet.device, packet.command, packet.inputs, dispatch.vkDeviceWaitIdle);
}
#endif

} // namespace
#endif

bool Evaluate(GraphicContext& graphics, vk::CommandBuffer command, const Inputs& inputs) {
#if defined(KYTY_DLSS_SDK)
	if (!g_ready.load(std::memory_order_relaxed)) return false;
#ifdef KYTY_LOCAL_VULKAN_RECORDING
	const Packet packet {static_cast<VkDevice>(graphics.device), static_cast<VkCommandBuffer>(command), inputs};
	const LocalVulkanRecording::Segment                  segment {&packet, sizeof(packet)};
	const std::span<const LocalVulkanRecording::Segment> segments {&segment, 1};
	if (!LocalVulkanRecording::EnqueuePacket(Replay, segments)) LocalVulkanRecording::ReplayInline(Replay, segments);
	return true; // (a failure on the worker turns DLSS off for the frames after)
#else
	return EvaluateNow(static_cast<VkDevice>(graphics.device), static_cast<VkCommandBuffer>(command), inputs,
	                   VULKAN_HPP_DEFAULT_DISPATCHER.vkDeviceWaitIdle);
#endif
#else
	(void)graphics;
	(void)command;
	(void)inputs;
	return false;
#endif
}

} // namespace Libs::Graphics::DlssUpscale
