#ifndef EMULATOR_SRC_LOCAL_DLSS_UPSCALE_H_
#define EMULATOR_SRC_LOCAL_DLSS_UPSCALE_H_

// NVIDIA DLSS (Super Resolution at the render size: DLAA) for the game's TAA pass (KYTY_DLSS=1), through NGX, the
// DLSS SDK's static library (_Build/deps/dlss, never part of the repository; the DLL, nvngx_dlss.dll, beside the
// emulator in deps\dlss). Any GeForce RTX card. The TAA pass's handling is fsr4-upscale.cpp's (it runs after the
// game's TAA, then fsr4-exposure.comp puts the result in the TAA output); this file holds the NGX side.
// The guest's address space then stops below 0xE0'0000'0000 (NGX's CUDA interop needs host address space below 1 TiB at
// device creation). Costs ~9% fps at 1-1 on an RTX 3080 Ti (1440p, 10-09; FSR 4 ~13%).
//   KYTY_DLSS=1               on (wins over KYTY_FSR4)
//   KYTY_DLSS_LOG=2           NGX's verbose log in ours (default: its errors)
//   KYTY_DLSS_PRESET=<letter> the DLAA render preset (default K, the transformer model; J the other one)

#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Libs::Graphics {

struct GraphicContext;

namespace DlssUpscale {

// The switch (KYTY_DLSS) and NGX's presence in this build.
[[nodiscard]] bool Requested();
// Instance and device creation, when requested: the extensions NGX needs, those the system has.
void AddInstanceExtensions(const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions);
void AddDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physical_device,
                         const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions);
// After the device: NGX initialized, DLSS available on this GPU and driver.
void OnDevice(GraphicContext& graphics);
[[nodiscard]] bool Ready();

struct Image {
	vk::Image     image;
	vk::ImageView view;
	vk::Format    format;
};
struct Inputs {
	Image                color, depth, motion, output; // all in VK_IMAGE_LAYOUT_GENERAL, at `extent`
	vk::Extent2D         extent;
	std::array<float, 2> jitter_px {};    // the render jitter in pixels
	std::array<float, 2> motion_scale {}; // the vectors to pixels towards the previous position
	float                frame_ms = 16.7f;
	bool                 reset    = false;
};
// Kyty.Gpu: records DLSS into `command` (the feature made at the first call for its size). With the Vulkan recording
// worker (KYTY_VULKAN_RECORDING) it is queued there, in order with the renderer's commands: a failure then turns DLSS off
// for the frames after. False when DLSS is off or (without the worker) failed.
[[nodiscard]] bool Evaluate(GraphicContext& graphics, vk::CommandBuffer command, const Inputs& inputs);

} // namespace DlssUpscale
} // namespace Libs::Graphics

#endif /* EMULATOR_SRC_LOCAL_DLSS_UPSCALE_H_ */
