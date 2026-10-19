#include "fsr-frame-gen.h"

#include "common/logging/log.h"
#include "frame-gen.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
// FidelityFX SDK 1.1.4 API headers (src/local/ffx-api, MIT).
#include "ffx-api/ffx_framegeneration.h"
#include "ffx-api/vk/ffx_api_vk.h"
#endif

namespace Libs::Graphics::FsrFrameGen {

namespace {

#if defined(_WIN32)
struct Api {
	PfnFfxCreateContext  create  = nullptr;
	PfnFfxDestroyContext destroy = nullptr;
	PfnFfxConfigure      configure = nullptr;
	PfnFfxQuery          query   = nullptr;
	PfnFfxDispatch       dispatch = nullptr;
};

Api                                      g_api;
bool                                     g_requested = false; // the DLL and its entry points
bool                                     g_ready     = false; // the device queues
bool                                     g_async     = true;
bool                                     g_debug     = false;
bool                                     g_debug_view = false; // KYTY_FRAMEGEN_FSR_DEBUG=view: the SDK's debug view (its depth, vectors, masks)
bool                                     g_depth_standard = false; // KYTY_FRAMEGEN_FSR_DEPTH=standard: depth not declared inverted-infinite
float                                    g_mv_sign   = -1.0f;
float                                    g_jitter_sign = 1.0f; // KYTY_FRAMEGEN_FSR_JITTER_SIGN; 0 = no jitter cancellation
bool                                     g_nearfar_plain = false; // KYTY_FRAMEGEN_FSR_NEARFAR=plain: near/far as they are
VkPhysicalDevice                         g_physical_device = VK_NULL_HANDLE;
VkDevice                                 g_device          = VK_NULL_HANDLE;
uint32_t                                 g_queue_family    = 0;
std::array<VkQueue, 3>                   g_queues {}; // async compute, present, image acquire
ffxContext                               g_swapchain_context = nullptr;
ffxContext                               g_generation_context = nullptr;
ffxQueryDescSwapchainReplacementFunctionsVK g_functions {};
VkQueue                                  g_game_queue       = VK_NULL_HANDLE;
Common::Mutex*                           g_game_queue_mutex = nullptr;
thread_local bool                        g_holds_queue_mutex = false; // QueuePresent runs under it
VkSwapchainKHR                           g_swapchain  = VK_NULL_HANDLE;
VkFormat                                 g_format     = VK_FORMAT_UNDEFINED;
VkExtent2D                               g_display {};
VkExtent2D                               g_max_render {2560, 1440};
uint64_t                                 g_frame_id   = 0;
std::chrono::steady_clock::time_point    g_last_prepare {};
bool                                     g_generating = false;
uint64_t                                 g_presents = 0, g_presents_window = 0, g_shown_window = 0;
std::chrono::steady_clock::time_point    g_window_start {};
std::atomic<uint32_t>                    g_presented_frames {1};

const char* ReturnCodeName(ffxReturnCode_t code) {
	switch (code) {
		case FFX_API_RETURN_OK: return "ok";
		case FFX_API_RETURN_ERROR: return "error";
		case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown descriptor type";
		case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "runtime error";
		case FFX_API_RETURN_NO_PROVIDER: return "no provider";
		case FFX_API_RETURN_ERROR_MEMORY: return "memory";
		case FFX_API_RETURN_ERROR_PARAMETER: return "parameter";
		default: return "?";
	}
}

void Say(const char* format, auto... args) {
	LOGF(format, args...);
	std::printf(format, args...);
	std::fflush(stdout);
}

bool LoadDll() {
	std::filesystem::path dir;
	wchar_t               exe[MAX_PATH] {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const auto exe_dir = std::filesystem::path(exe).parent_path();
	if (const char* env = std::getenv("KYTY_FSR_DIR"); env != nullptr && *env != '\0') {
		dir = env;
	} else if (std::filesystem::exists(exe_dir / "deps" / "fsr" / "amd_fidelityfx_vk.dll")) {
		dir = exe_dir / "deps" / "fsr";
	} else {
		dir = exe_dir;
	}
	const auto path   = dir / "amd_fidelityfx_vk.dll";
	HMODULE    module = LoadLibraryW(path.c_str());
	if (module == nullptr) {
		Say("FSR frame generation: cannot load %s (error %lu)\n", path.string().c_str(),
		    static_cast<unsigned long>(GetLastError()));
		return false;
	}
	const auto load = [&](auto& function, const char* name) {
		function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
		    reinterpret_cast<void*>(GetProcAddress(module, name)));
		return function != nullptr;
	};
	if (!load(g_api.create, "ffxCreateContext") || !load(g_api.destroy, "ffxDestroyContext") ||
	    !load(g_api.configure, "ffxConfigure") || !load(g_api.query, "ffxQuery") ||
	    !load(g_api.dispatch, "ffxDispatch")) {
		Say("FSR frame generation: %s lacks an entry point\n", path.string().c_str());
		return false;
	}
	Say("FSR frame generation: loaded %s\n", path.string().c_str());
	return true;
}

// The game's world->clip matrix (row-major, clip = M * world) is P * V: a perspective whose w
// row is the view depth and whose z row is the constant near (reversed, infinite). Split into the
// camera's axes and position and the projection's scales (frame-gen.cpp's Decompose, the same
// math without Streamline's types; FSR 3.1.4 requires the camera's position and axes).
struct Lens {
	float right[3] {}, up[3] {}, forward[3] {}, position[3] {};
	float sx = 1.0f, sy = 1.0f, cx = 0.0f, cy = 0.0f, z_near = 0.0f;
	bool  valid = false;
};

Lens Decompose(const std::array<float, 16>& m) {
	Lens       lens;
	const auto row = [&](uint32_t i) { return std::array<double, 4> {m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]}; };
	const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
	const double flen = std::sqrt(r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2]);
	if (flen < 1e-6) return lens;
	const double f[3] {r3[0] / flen, r3[1] / flen, r3[2] / flen};
	const double tf = r3[3] / flen;
	const auto split = [&](const std::array<double, 4>& r, double& s, double& off, double axis[3], double& t) {
		off = (r[0] * f[0] + r[1] * f[1] + r[2] * f[2]) / flen;
		double v[3] {r[0] / flen - off * f[0], r[1] / flen - off * f[1], r[2] / flen - off * f[2]};
		s = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (s < 1e-9) return false;
		for (int i = 0; i < 3; ++i) axis[i] = v[i] / s;
		t = (r[3] / flen - off * tf) / s;
		return true;
	};
	double sx = 0, cx = 0, sy = 0, cy = 0, x[3] {}, y[3] {}, tx = 0, ty = 0;
	if (!split(r0, sx, cx, x, tx) || !split(r1, sy, cy, y, ty)) return lens;
	// Up = forward x right; a flipped clip y shows as a negative sy.
	const double up[3] {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0]};
	if (up[0] * y[0] + up[1] * y[1] + up[2] * y[2] < 0) {
		sy = -sy;
		ty = -ty;
	}
	// View translation (tx, ty, tf) = -axes . position.
	const double pos[3] {-(tx * x[0] + ty * up[0] + tf * f[0]), -(tx * x[1] + ty * up[1] + tf * f[1]),
	                     -(tx * x[2] + ty * up[2] + tf * f[2])};
	for (int i = 0; i < 3; ++i) {
		lens.right[i]    = static_cast<float>(x[i]);
		lens.up[i]       = static_cast<float>(up[i]);
		lens.forward[i]  = static_cast<float>(f[i]);
		lens.position[i] = static_cast<float>(pos[i]);
	}
	lens.sx     = static_cast<float>(sx);
	lens.sy     = static_cast<float>(sy);
	lens.cx     = static_cast<float>(cx);
	lens.cy     = static_cast<float>(cy);
	lens.z_near = static_cast<float>(r2[3] / flen);
	lens.valid  = lens.z_near > 0.0f;
	return lens;
}

FfxApiResource ImageResource(const VulkanImage& image, bool depth) {
	FfxApiResourceDescription description {};
	description.type     = FFX_API_RESOURCE_TYPE_TEXTURE2D;
	description.format   = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(image.format));
	description.width    = image.extent.width;
	description.height   = image.extent.height;
	description.depth    = 1;
	description.mipCount = 1;
	description.flags    = FFX_API_RESOURCE_FLAGS_NONE;
	// A depth image with stencil (the game's D32S8) needs both bits, or the SDK's view format
	// (D32 alone) mismatches the image and the GPU faults.
	description.usage = depth ? (ffxApiIsStencilFormat(static_cast<VkFormat>(image.format))
	                                ? FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET
	                                : FFX_API_RESOURCE_USAGE_DEPTHTARGET)
	                          : FFX_API_RESOURCE_USAGE_READ_ONLY;
	// The inputs are left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (FrameGen::PrepareInputs).
	return ffxApiGetResourceVK(static_cast<VkImage>(image.image), description, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
}

// A crash inside the SDK is reported with the module and offset it happened in (a null function
// pointer shows as address 0) instead of taking the process down silently.
LONG ReportCrash(EXCEPTION_POINTERS* pointers, const char* what) {
	const auto* record  = pointers->ExceptionRecord;
	const auto  address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
	HMODULE     module  = nullptr;
	char        name[MAX_PATH] {};
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(address), &module) &&
	    module != nullptr) {
		GetModuleFileNameA(module, name, sizeof(name));
	}
	std::printf("FSR frame generation: %s crashed, code 0x%08lx at 0x%llx (%s+0x%llx)%s\n", what,
	            static_cast<unsigned long>(record->ExceptionCode), static_cast<unsigned long long>(address),
	            name[0] != '\0' ? name : "no module", static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(module)),
	            record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2
	                ? (record->ExceptionInformation[0] == 8 ? " (execute)" : record->ExceptionInformation[0] == 1 ? " (write)" : " (read)")
	                : "");
	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
		std::printf("FSR frame generation: faulting address 0x%llx\n", static_cast<unsigned long long>(record->ExceptionInformation[1]));
	std::fflush(stdout);
	return EXCEPTION_EXECUTE_HANDLER;
}

ffxReturnCode_t CreateGuarded(ffxContext* context, ffxCreateContextDescHeader* desc, const char* what) {
	__try {
		return g_api.create(context, desc, nullptr);
	} __except (ReportCrash(GetExceptionInformation(), what)) {
		return FFX_API_RETURN_ERROR;
	}
}

// The swapchain's submissions on the game queue, under the renderer's queue lock (a queue is
// externally synchronized; without this the two sides raced and the device was lost).
VkResult VKAPI_PTR GameQueueSubmit(uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
	if (g_game_queue_mutex == nullptr || g_holds_queue_mutex)
		return VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueSubmit(g_game_queue, count, submits, fence);
	Common::LockGuard lock(*g_game_queue_mutex);
	return VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueSubmit(g_game_queue, count, submits, fence);
}

// The SDK's Vulkan backend resolves some functions by their extension names (e.g.
// vkGetBufferMemoryRequirements2KHR, called when the device supports dedicated allocations) and
// calls them unchecked; a device that has them as core functions only, like this one, returns
// null for the KHR name. Resolve the core name then.
PFN_vkVoidFunction VKAPI_PTR DeviceProcAddr(VkDevice device, const char* name) {
	auto* function = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(device, name);
	if (function == nullptr && name != nullptr) {
		std::string core = name;
		if (core.size() > 3 && core.ends_with("KHR")) {
			core.resize(core.size() - 3);
			function = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(device, core.c_str());
		}
	}
	if (function == nullptr && g_debug) Say("FSR frame generation: the device has no %s\n", name);
	return function;
}

// The SDK's own messages (errors, and everything with KYTY_FRAMEGEN_FSR_DEBUG).
void SdkMessage(uint32_t type, const wchar_t* message) {
	char text[1024] {};
	if (message != nullptr) WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text) - 1, nullptr, nullptr);
	Say("FSR frame generation: SDK %s: %s\n", type == 1 ? "error" : type == 2 ? "warning" : "note", text);
}

void ConfigureDebug(ffxContext* context) {
	ffxConfigureDescGlobalDebug1 debug {};
	debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
	debug.fpMessage   = SdkMessage;
	debug.debugLevel  = g_debug ? FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE : FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_ERRORS;
	if (const auto code = g_api.configure(context, &debug.header); code != FFX_API_RETURN_OK && g_debug)
		Say("FSR frame generation: debug messages not accepted (%s)\n", ReturnCodeName(code));
}

// The generation callback the swapchain calls for each generated frame (the SDK sample does
// the same: the dispatch goes to the generation context).
ffxReturnCode_t GenerationCallback(ffxDispatchDescFrameGeneration* params, void* context) {
	return g_api.dispatch(static_cast<ffxContext*>(context), &params->header);
}

void DestroyContexts() {
	// The swapchain's threads may still be presenting: wait for its presents and its queues
	// before the contexts go (the SDK sample flushes everything first; without it the device
	// was lost on a recreate).
	if (g_swapchain_context != nullptr) {
		ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait {};
		wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
		(void)g_api.dispatch(&g_swapchain_context, &wait.header);
		for (const auto queue: g_queues)
			if (queue != VK_NULL_HANDLE) (void)VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueWaitIdle(queue);
		if (g_game_queue != VK_NULL_HANDLE && g_game_queue_mutex != nullptr) {
			Common::LockGuard lock(*g_game_queue_mutex);
			(void)VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueWaitIdle(g_game_queue);
		}
	}
	if (g_generation_context != nullptr) {
		g_api.destroy(&g_generation_context, nullptr);
		g_generation_context = nullptr;
	}
	if (g_swapchain_context != nullptr) {
		g_api.destroy(&g_swapchain_context, nullptr);
		g_swapchain_context = nullptr;
	}
	g_swapchain = VK_NULL_HANDLE;
	g_functions = {};
}
#endif

} // namespace

void Initialize() {
#if defined(_WIN32)
	const char* value = std::getenv("KYTY_FRAMEGEN_FSR");
	if (value == nullptr || value[0] == '\0' || value[0] == '0') return;
	if (const char* async = std::getenv("KYTY_FRAMEGEN_FSR_ASYNC"); async != nullptr) g_async = async[0] != '0';
	g_debug = std::getenv("KYTY_FRAMEGEN_FSR_DEBUG") != nullptr;
	g_debug_view = g_debug && std::string(std::getenv("KYTY_FRAMEGEN_FSR_DEBUG")) == "view";
	if (const char* depth = std::getenv("KYTY_FRAMEGEN_FSR_DEPTH"); depth != nullptr) g_depth_standard = std::string(depth) == "standard";
	if (const char* sign = std::getenv("KYTY_FRAMEGEN_FSR_MV_SIGN"); sign != nullptr) g_mv_sign = std::atof(sign) > 0 ? 1.0f : -1.0f;
	if (const char* sign = std::getenv("KYTY_FRAMEGEN_FSR_JITTER_SIGN"); sign != nullptr)
		g_jitter_sign = static_cast<float>(std::atof(sign));
	if (const char* nf = std::getenv("KYTY_FRAMEGEN_FSR_NEARFAR"); nf != nullptr) g_nearfar_plain = std::string(nf) == "plain";
	if (const char* size = std::getenv("KYTY_FRAMEGEN_FSR_MAX_RENDER"); size != nullptr) {
		unsigned w = 0, h = 0;
		if (std::sscanf(size, "%ux%u", &w, &h) == 2 && w != 0 && h != 0) g_max_render = {w, h};
	}
	g_requested = LoadDll();
	if (g_requested) FrameGen::EnableInputs();
#endif
}

bool Requested() {
#if defined(_WIN32)
	return g_requested;
#else
	return false;
#endif
}

uint32_t ExtraQueueCount() {
	return Requested() ? 3u : 0u;
}

void OnDevice(vk::PhysicalDevice physical_device, vk::Device device, uint32_t queue_family,
              uint32_t first_extra_queue) {
#if defined(_WIN32)
	if (!g_requested) return;
	g_physical_device = static_cast<VkPhysicalDevice>(physical_device);
	g_device          = static_cast<VkDevice>(device);
	g_queue_family    = queue_family;
	for (uint32_t i = 0; i < g_queues.size(); ++i) {
		vk::Queue queue;
		device.getQueue(queue_family, first_extra_queue + i, &queue);
		g_queues[i] = static_cast<VkQueue>(queue);
		if (g_queues[i] == VK_NULL_HANDLE) {
			Say("FSR frame generation: queue %u of family %u is missing\n", first_extra_queue + i, queue_family);
			return;
		}
	}
	g_ready = true;
	Say("FSR frame generation: ready (queues %u..%u of family %u, %s)\n", first_extra_queue,
	    first_extra_queue + 2, queue_family, g_async ? "async interpolation" : "interpolation on the game queue");
#else
	(void)physical_device;
	(void)device;
	(void)queue_family;
	(void)first_extra_queue;
#endif
}

bool Enabled() {
#if defined(_WIN32)
	return g_ready;
#else
	return false;
#endif
}

vk::Result CreateSwapchain(const vk::SwapchainCreateInfoKHR& info, vk::Queue game_queue,
                           Common::Mutex& game_queue_mutex, vk::SwapchainKHR* swapchain) {
#if defined(_WIN32)
	if (!g_ready || swapchain == nullptr) return vk::Result::eErrorInitializationFailed;
	g_game_queue       = static_cast<VkQueue>(game_queue);
	g_game_queue_mutex = &game_queue_mutex;
	// Every resize: the contexts are sized to the display and the swapchain is theirs.
	DestroyContexts();
	VkSwapchainKHR raw = VK_NULL_HANDLE; // *swapchain was destroyed with the previous context
	ffxCreateContextDescFrameGenerationSwapChainVK desc {};
	desc.header.type          = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
	desc.physicalDevice       = g_physical_device;
	desc.device               = g_device;
	desc.swapchain            = &raw;
	desc.allocator            = nullptr;
	desc.createInfo           = static_cast<VkSwapchainCreateInfoKHR>(info);
	desc.createInfo.oldSwapchain = VK_NULL_HANDLE;
	desc.gameQueue            = {static_cast<VkQueue>(game_queue), g_queue_family, GameQueueSubmit};
	desc.asyncComputeQueue    = {g_queues[0], g_queue_family, nullptr};
	desc.presentQueue         = {g_queues[1], g_queue_family, nullptr};
	desc.imageAcquireQueue    = {g_queues[2], g_queue_family, nullptr};
	ffxCreateContextDescFrameGenerationSwapChainModeVK mode {};
	mode.header.type           = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK;
	mode.composeOnPresentQueue = true;
	desc.header.pNext          = &mode.header;
	if (g_debug)
		Say("FSR frame generation: creating the swapchain %ux%u format %u images %u present mode %u\n",
		    desc.createInfo.imageExtent.width, desc.createInfo.imageExtent.height,
		    static_cast<unsigned>(desc.createInfo.imageFormat), desc.createInfo.minImageCount,
		    static_cast<unsigned>(desc.createInfo.presentMode));
	if (const auto code = CreateGuarded(&g_swapchain_context, &desc.header, "the swapchain context"); code != FFX_API_RETURN_OK) {
		Say("FSR frame generation: the frame-interpolation swapchain failed (%s)\n", ReturnCodeName(code));
		g_swapchain_context = nullptr;
		return vk::Result::eErrorInitializationFailed;
	}
	if (g_debug) Say("FSR frame generation: swapchain context created\n");
	ConfigureDebug(&g_swapchain_context);
	g_functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
	if (const auto code = g_api.query(&g_swapchain_context, &g_functions.header);
	    code != FFX_API_RETURN_OK || g_functions.pOutAcquireNextImageKHR == nullptr ||
	    g_functions.pOutQueuePresentKHR == nullptr || g_functions.pOutGetSwapchainImagesKHR == nullptr) {
		Say("FSR frame generation: no swapchain replacement functions (%s)\n", ReturnCodeName(code));
		DestroyContexts();
		return vk::Result::eErrorInitializationFailed;
	}

	ffxCreateBackendVKDesc backend {};
	backend.header.type       = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
	backend.vkDevice          = g_device;
	backend.vkPhysicalDevice  = g_physical_device;
	backend.vkDeviceProcAddr  = DeviceProcAddr;
	ffxCreateContextDescFrameGeneration generation {};
	generation.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	generation.header.pNext = &backend.header;
	generation.flags        = g_depth_standard ? 0u : FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
	if (g_async) generation.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	if (g_debug) generation.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
	generation.displaySize      = {info.imageExtent.width, info.imageExtent.height};
	generation.maxRenderSize    = {std::max(g_max_render.width, info.imageExtent.width),
	                               std::max(g_max_render.height, info.imageExtent.height)};
	generation.backBufferFormat = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(info.imageFormat));
	if (g_debug) Say("FSR frame generation: creating the generation context\n");
	if (const auto code = CreateGuarded(&g_generation_context, &generation.header, "the generation context"); code != FFX_API_RETURN_OK) {
		Say("FSR frame generation: the frame-generation context failed (%s)\n", ReturnCodeName(code));
		g_generation_context = nullptr;
		DestroyContexts();
		return vk::Result::eErrorInitializationFailed;
	}
	ConfigureDebug(&g_generation_context);
	{
		// What the two contexts hold in video memory (the caches' budget leaves KYTY_VRAM_RESERVE_MB for it).
		FfxApiEffectMemoryUsage generation_memory {}, swapchain_memory {};
		ffxQueryDescFrameGenerationGetGPUMemoryUsage query_generation {};
		query_generation.header.type                   = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE;
		query_generation.gpuMemoryUsageFrameGeneration = &generation_memory;
		ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageVK query_swapchain {};
		query_swapchain.header.type                            = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_VK;
		query_swapchain.gpuMemoryUsageFrameGenerationSwapchain = &swapchain_memory;
		(void)g_api.query(&g_generation_context, &query_generation.header);
		(void)g_api.query(&g_swapchain_context, &query_swapchain.header);
		Say("FSR frame generation: video memory: generation %llu MiB, swapchain %llu MiB\n",
		    static_cast<unsigned long long>(generation_memory.totalUsageInBytes >> 20u),
		    static_cast<unsigned long long>(swapchain_memory.totalUsageInBytes >> 20u));
	}
	g_swapchain  = raw;
	g_format     = static_cast<VkFormat>(info.imageFormat);
	g_display    = {info.imageExtent.width, info.imageExtent.height};
	g_generating = false;
	g_presents_window = g_shown_window = 0;
	g_window_start    = std::chrono::steady_clock::now();
	*swapchain        = vk::SwapchainKHR(raw);
	Say("FSR frame generation: swapchain %ux%u, generation up to %ux%u inputs\n", g_display.width, g_display.height,
	    generation.maxRenderSize.width, generation.maxRenderSize.height);
	return vk::Result::eSuccess;
#else
	(void)info;
	(void)game_queue;
	(void)game_queue_mutex;
	(void)swapchain;
	return vk::Result::eErrorInitializationFailed;
#endif
}

void DestroySwapchain() {
#if defined(_WIN32)
	DestroyContexts();
#endif
}

vk::Result GetSwapchainImages(vk::SwapchainKHR swapchain, uint32_t* count, vk::Image* images) {
#if defined(_WIN32)
	if (g_functions.pOutGetSwapchainImagesKHR == nullptr) return vk::Result::eErrorInitializationFailed;
	return static_cast<vk::Result>(g_functions.pOutGetSwapchainImagesKHR(
	    g_device, static_cast<VkSwapchainKHR>(swapchain), count, reinterpret_cast<VkImage*>(images)));
#else
	(void)swapchain;
	(void)count;
	(void)images;
	return vk::Result::eErrorInitializationFailed;
#endif
}

vk::Result AcquireNextImage(vk::SwapchainKHR swapchain, uint64_t timeout, vk::Semaphore semaphore, uint32_t* index) {
#if defined(_WIN32)
	if (g_functions.pOutAcquireNextImageKHR == nullptr) return vk::Result::eErrorInitializationFailed;
	return static_cast<vk::Result>(g_functions.pOutAcquireNextImageKHR(
	    g_device, static_cast<VkSwapchainKHR>(swapchain), timeout, static_cast<VkSemaphore>(semaphore), VK_NULL_HANDLE, index));
#else
	(void)swapchain;
	(void)timeout;
	(void)semaphore;
	(void)index;
	return vk::Result::eErrorInitializationFailed;
#endif
}

vk::Result QueuePresent(vk::Queue queue, const vk::PresentInfoKHR& info) {
#if defined(_WIN32)
	if (g_functions.pOutQueuePresentKHR == nullptr) return vk::Result::eErrorInitializationFailed;
	const auto present = static_cast<VkPresentInfoKHR>(info);
	// The replacement may submit on the game queue inside this call; the renderer's recording
	// thread submits there too, so the call runs under the queue lock (GameQueueSubmit then
	// submits without taking it again).
	vk::Result result;
	{
		Common::LockGuard lock(*g_game_queue_mutex);
		g_holds_queue_mutex = true;
		result = static_cast<vk::Result>(g_functions.pOutQueuePresentKHR(static_cast<VkQueue>(queue), &present));
		g_holds_queue_mutex = false;
	}
	++g_presents;
	++g_presents_window;
	// About once a second: the frames the swapchain actually showed per present.
	const auto now = std::chrono::steady_clock::now();
	if (now - g_window_start >= std::chrono::seconds(1) && g_functions.pOutGetLastPresentCountFFXAPI != nullptr) {
		const auto shown = g_functions.pOutGetLastPresentCountFFXAPI(g_swapchain);
		const auto delta = shown - g_shown_window;
		g_presented_frames.store(g_presents_window != 0 ? static_cast<uint32_t>(std::clamp<uint64_t>((delta + g_presents_window / 2) / g_presents_window, 1, 4)) : 1,
		                         std::memory_order_relaxed);
		if (g_presents % 300 < g_presents_window) {
			LOGF("FSR frame generation: %llu presents, %llu shown in the last second (%s)\n",
			     static_cast<unsigned long long>(g_presents_window), static_cast<unsigned long long>(delta),
			     g_generating ? "generating" : "passing through");
		}
		g_shown_window    = shown;
		g_presents_window = 0;
		g_window_start    = now;
	}
	return result;
#else
	(void)queue;
	(void)info;
	return vk::Result::eErrorInitializationFailed;
#endif
}

void PrepareFrame(vk::CommandBuffer command, const FrameGen::FrameInputs* inputs, vk::Extent2D backbuffer,
                  vk::Rect2D region, vk::SwapchainKHR swapchain) {
#if defined(_WIN32)
	if (g_generation_context == nullptr || static_cast<VkSwapchainKHR>(swapchain) != g_swapchain) return;
	++g_frame_id;
	const auto now   = std::chrono::steady_clock::now();
	float      delta = 16.6f;
	if (g_last_prepare != std::chrono::steady_clock::time_point {})
		delta = std::clamp(std::chrono::duration<float, std::milli>(now - g_last_prepare).count(), 1.0f, 200.0f);
	g_last_prepare = now;

	// KYTY_FRAMEGEN_FSR_GENERATE=0: the interpolation swapchain alone, generation never on (a
	// bisection aid: does the level start survive without the prepare pass and the interpolation?).
	static const bool generate = [] {
		const char* value = std::getenv("KYTY_FRAMEGEN_FSR_GENERATE");
		return value == nullptr || value[0] != '0';
	}();
	bool usable = generate && inputs != nullptr && inputs->valid && inputs->render_extent.width != 0 &&
	              inputs->render_extent.width <= std::max(g_max_render.width, g_display.width) &&
	              inputs->render_extent.height <= std::max(g_max_render.height, g_display.height);
	Lens lens;
	if (usable) {
		lens   = Decompose(inputs->camera.view_proj);
		usable = lens.valid;
	}
	if (usable) {
		const float width  = static_cast<float>(inputs->render_extent.width);
		const float height = static_cast<float>(inputs->render_extent.height);
		ffxDispatchDescFrameGenerationPrepare prepare {};
		prepare.header.type   = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
		prepare.frameID       = g_frame_id;
		prepare.flags         = 0;
		prepare.commandList   = static_cast<VkCommandBuffer>(command);
		prepare.renderSize    = {inputs->render_extent.width, inputs->render_extent.height};
		// The TAA jitter in pixels (frame-gen.cpp: the clip-space offset scaled by half the size).
		prepare.jitterOffset  = {g_jitter_sign * inputs->camera.jitter_ndc[0] * width * 0.5f,
		                         g_jitter_sign * -inputs->camera.jitter_ndc[1] * height * 0.5f};
		// The game's vectors are in UV units along the motion; FSR wants pixels towards the
		// previous position, as DLSS-G did (negated).
		prepare.motionVectorScale      = {g_mv_sign * width, g_mv_sign * height};
		prepare.frameTimeDelta         = delta;
		prepare.unused_reset           = false;
		// Inverted, infinite depth: the SDK sample swaps the planes (KYTY_FRAMEGEN_FSR_NEARFAR=plain: not swapped).
		const bool plain               = g_nearfar_plain || g_depth_standard;
		prepare.cameraFar              = plain ? std::numeric_limits<float>::max() : lens.z_near;
		prepare.cameraNear             = plain ? lens.z_near : std::numeric_limits<float>::max();
		prepare.cameraFovAngleVertical = 2.0f * std::atan(1.0f / std::fabs(lens.sy));
		prepare.viewSpaceToMetersFactor = 1.0f;
		prepare.depth                  = ImageResource(inputs->depth, true);
		prepare.motionVectors          = ImageResource(inputs->motion, false);
		ffxDispatchDescFrameGenerationPrepareCameraInfo camera {};
		camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
		std::copy_n(lens.position, 3, camera.cameraPosition);
		std::copy_n(lens.up, 3, camera.cameraUp);
		std::copy_n(lens.right, 3, camera.cameraRight);
		std::copy_n(lens.forward, 3, camera.cameraForward);
		prepare.header.pNext = &camera.header;
		if (!g_generating) {
			const auto& m = inputs->camera.view_proj;
			Say("FSR frame generation: inputs depth fmt %u %ux%u, motion fmt %u %ux%u, render %ux%u, jitter %.3f %.3f, "
			    "fov %.1f deg, near %.3f, delta %.1f ms\n",
			    static_cast<unsigned>(inputs->depth.format), inputs->depth.extent.width, inputs->depth.extent.height,
			    static_cast<unsigned>(inputs->motion.format), inputs->motion.extent.width, inputs->motion.extent.height,
			    inputs->render_extent.width, inputs->render_extent.height, prepare.jitterOffset.x, prepare.jitterOffset.y,
			    prepare.cameraFovAngleVertical * 57.2958f, lens.z_near, delta);
			// The depth convention, from the matrix itself: a reversed infinite projection has a clip z row
			// of (0 0 0 near) against the w row (the view direction); anything else is not what we declare.
			Say("FSR frame generation: view_proj clip z row %.5f %.5f %.5f %.5f, w row %.5f %.5f %.5f %.5f, "
			    "position %.2f %.2f %.2f, declared %s\n",
			    m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], lens.position[0], lens.position[1], lens.position[2],
			    g_depth_standard ? "standard" : "inverted-infinite");
		}
		if (const auto code = g_api.dispatch(&g_generation_context, &prepare.header); code != FFX_API_RETURN_OK) {
			if (g_frame_id % 300 == 1) Say("FSR frame generation: prepare failed (%s)\n", ReturnCodeName(code));
			usable = false;
		}
	}

	ffxConfigureDescFrameGeneration configure {};
	configure.header.type                        = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
	configure.swapChain                          = g_swapchain;
	configure.presentCallback                    = nullptr;
	configure.presentCallbackUserContext         = nullptr;
	configure.frameGenerationCallback            = GenerationCallback;
	configure.frameGenerationCallbackUserContext = &g_generation_context;
	configure.frameGenerationEnabled             = usable;
	configure.allowAsyncWorkloads                = g_async;
	// The frame without its HUD (frame-gen.h): FSR keeps what differs from it, the HUD, out of the
	// generated frames instead of moving it with the scene.
	configure.HUDLessColor = usable && inputs != nullptr && inputs->hudless_backbuffer_valid
	                             ? ImageResource(inputs->hudless_backbuffer, false)
	                             : FfxApiResource {};
	if (usable && !g_generating) {
		Say("FSR frame generation: HUD-less colour %s\n",
		    inputs != nullptr && inputs->hudless_backbuffer_valid ? "given (the HUD stays out of generated frames)" : "not available");
	}
	configure.flags = g_debug_view ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW
	                  : g_debug    ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES | FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES
	                               : 0u;
	configure.onlyPresentGenerated = false;
	configure.generationRect       = {region.offset.x, region.offset.y, static_cast<int32_t>(region.extent.width),
	                                  static_cast<int32_t>(region.extent.height)};
	if (configure.generationRect.width <= 0 || configure.generationRect.height <= 0)
		configure.generationRect = {0, 0, static_cast<int32_t>(backbuffer.width), static_cast<int32_t>(backbuffer.height)};
	configure.frameID = g_frame_id;
	if (const auto code = g_api.configure(&g_generation_context, &configure.header); code != FFX_API_RETURN_OK) {
		if (g_frame_id % 300 == 1) Say("FSR frame generation: configure failed (%s)\n", ReturnCodeName(code));
	}
	if (usable != g_generating) {
		g_generating = usable;
		LOGF("FSR frame generation: %s at frame %llu\n", usable ? "on" : "off", static_cast<unsigned long long>(g_frame_id));
	}
#else
	(void)command;
	(void)inputs;
	(void)backbuffer;
	(void)region;
	(void)swapchain;
#endif
}

uint32_t PresentedFrames() {
#if defined(_WIN32)
	return g_presented_frames.load(std::memory_order_relaxed);
#else
	return 1;
#endif
}

} // namespace Libs::Graphics::FsrFrameGen
