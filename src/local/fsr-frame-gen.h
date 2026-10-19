#ifndef EMULATOR_SRC_LOCAL_FSR_FRAME_GEN_H_
#define EMULATOR_SRC_LOCAL_FSR_FRAME_GEN_H_

// AMD FSR 3.1 frame generation (FidelityFX SDK 1.1.4, the prebuilt amd_fidelityfx_vk.dll loaded
// at run time) behind KYTY_FRAMEGEN_FSR=1, for any Vulkan GPU. The game's inputs are the ones
// FrameGen gathers for DLSS-G (frame-gen.h: the TAA pass's depth and motion vectors, the G-buffer
// camera and jitter, copied beside the presented frame at the guest flip). The presenter's
// swapchain is FidelityFX's frame-interpolation swapchain (its replacement functions stand in for
// vkCreateSwapchainKHR, vkAcquireNextImageKHR and vkQueuePresentKHR), and every presented frame
// records the generation's prepare pass and configures the next present: generation on while
// the frame carries inputs, off for menus, movies and loading screens.
//   KYTY_FSR_DIR=<dir>              where amd_fidelityfx_vk.dll is (default: <exe dir>\deps\fsr, then <exe dir>)
//   KYTY_FRAMEGEN_FSR_ASYNC=0       interpolation on the game queue instead of the async compute queue
//   KYTY_FRAMEGEN_FSR_DEBUG=1       the SDK's debug checker and its tear/pacing lines (=view: its debug view)
//   KYTY_FRAMEGEN_FSR_DEPTH=standard depth not declared inverted-infinite (default: inverted, infinite far plane)
//   KYTY_FRAMEGEN_FSR_MV_SIGN=1     keep the game's motion-vector sign (default: negated, as DLSS-G needed)
//   KYTY_FRAMEGEN_FSR_JITTER_SIGN=1 keep the jitter sign (default: negated like the FSR sample)

#include "common/threads.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>

namespace Libs::Graphics {

namespace FrameGen {
struct FrameInputs;
}

namespace FsrFrameGen {

// Process start, before the device: the environment and the DLL. Turns FrameGen's input gathering on.
void Initialize();
// The DLL is loaded and the entry points found.
[[nodiscard]] bool Requested();
// Device creation: graphics-family queues to request beyond the renderer's own (the frame
// interpolation swapchain takes an async compute, a present and an image-acquire queue).
[[nodiscard]] uint32_t ExtraQueueCount();
// After the device exists: the queues, starting at `first_extra_queue` of `queue_family`.
void OnDevice(vk::PhysicalDevice physical_device, vk::Device device, uint32_t queue_family,
              uint32_t first_extra_queue);
// The swapchain can be replaced (OnDevice found the queues).
[[nodiscard]] bool Enabled();

// The frame-interpolation swapchain. Create destroys the previous one when `*swapchain` names it
// and leaves the new one there; its images come from GetSwapchainImages. Destroy drops the
// contexts (the swapchain with them).
// `game_queue_mutex` guards the renderer's submissions on `game_queue`: the swapchain submits
// its own work there too, through a function that takes it.
[[nodiscard]] vk::Result CreateSwapchain(const vk::SwapchainCreateInfoKHR& info, vk::Queue game_queue,
                                         Common::Mutex& game_queue_mutex, vk::SwapchainKHR* swapchain);
void                     DestroySwapchain();
[[nodiscard]] vk::Result GetSwapchainImages(vk::SwapchainKHR swapchain, uint32_t* count, vk::Image* images);
[[nodiscard]] vk::Result AcquireNextImage(vk::SwapchainKHR swapchain, uint64_t timeout, vk::Semaphore semaphore,
                                          uint32_t* index);
// On the game queue the swapchain was created with.
[[nodiscard]] vk::Result QueuePresent(vk::Queue queue, const vk::PresentInfoKHR& info);

// Present thread, in the frame's command buffer before the blit into the swapchain image: the
// prepare pass (depth, motion vectors, camera) when `inputs` are valid, then the configuration
// of the coming present. `region` is the part of the backbuffer the game image covers.
void PrepareFrame(vk::CommandBuffer command, const FrameGen::FrameInputs* inputs, vk::Extent2D backbuffer,
                  vk::Rect2D region, vk::SwapchainKHR swapchain);

// Frames shown per presented frame as last measured (1 without generation), for the FPS panel.
[[nodiscard]] uint32_t PresentedFrames();

} // namespace FsrFrameGen
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_LOCAL_FSR_FRAME_GEN_H_
