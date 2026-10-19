#ifndef EMULATOR_SRC_LOCAL_FSR4_UPSCALE_H_
#define EMULATOR_SRC_LOCAL_FSR4_UPSCALE_H_

// AMD FSR 4's machine-learning anti-aliasing for the game's TAA pass (KYTY_FSR4=1). The model is the source-v07 INT8
// one, run by FireBurn/FSR-Vulkan's Vulkan provider (3rdparty/fsr4-v07, MIT; not AMD's FSR 4.1.1): on any GPU with
// 8-bit integer dot products, 16-bit floats and compute-shader derivatives (an RTX 30 card has them). After the TAA
// dispatch (FrameGen::TaaShaders), FSR 4 reads the same scene colour, depth and motion vectors at the same size (the
// game's TAA does not upscale: native AA only) and its colour replaces the TAA output's, times the exposure the
// game's TAA applies (fsr4-exposure.comp); the per-pixel flags the game's TAA writes in alpha stay. The camera jitter
// comes from the G-buffer pass (FrameGen's camera). Costs ~13% fps at 1-1 on an RTX 3080 Ti (1440p, 10-09).
// Live switches (kyty_local_fsr4_*, abba.sh): live 0 shows the game's TAA, jitter/jitter_scale/mv/sharpness for
// image comparisons; live 2/3 log the TAA pass's input/output means (the exposure check).
//   KYTY_FSR4=1                  on (the device then enables the features its shaders need)
//   KYTY_FSR4_DIR=<dir>          the model and shader files (default deps\fsr4 beside the emulator)
//   KYTY_FSR4_SHARPNESS=<0..1>   RCAS sharpening after the model (default 0.6: as crisp as the game's own TAA)
//   KYTY_FSR4_MV_SIGN=1          keep the game's motion-vector sign (default: negated, as for frame generation)
//   KYTY_FSR4_JITTER_SIGN=-1     negate the jitter (default: as for frame generation)
//   KYTY_FSR4_HDR=0              the colour declared low dynamic range (default: HDR)
//   KYTY_FSR4_EXPOSURE=0         no auto exposure (default: the provider's SPD auto exposure)

#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>
#include <vector>

namespace Libs::Graphics {

struct GraphicContext;
class TextureCache;
class CommandScheduler;
struct PreparedBindings;

namespace Fsr4Upscale {

// The switch (KYTY_FSR4), read once.
[[nodiscard]] bool Requested();
// Device creation, when requested: the extension its shaders need, if the device has it.
void AddDeviceExtensions(const std::vector<vk::ExtensionProperties>& available, std::vector<const char*>& extensions);
// Device creation: whether the device supports everything (then the caller enables the features, and the
// compute-shader derivatives with `derivatives`). Sets what Active() reports.
[[nodiscard]] bool DeviceSupports(vk::PhysicalDevice physical_device, const std::vector<const char*>& extensions,
                                  const vk::PhysicalDeviceFeatures& features, const vk::PhysicalDeviceVulkan12Features& features12,
                                  const vk::PhysicalDeviceVulkan13Features& features13);
// Requested, and the device has what it needs, and nothing failed so far.
[[nodiscard]] bool Active();
// Kyty.Gpu, right after the game's TAA dispatch (its bindings, `command` the dispatch's command buffer): records FSR 4
// and the pass that puts its colour in the TAA output. False when it did not (the game's TAA result stands).
bool Record(GraphicContext& graphics, TextureCache& cache, CommandScheduler& scheduler, vk::CommandBuffer command,
            const PreparedBindings& bindings);

} // namespace Fsr4Upscale
} // namespace Libs::Graphics

#endif /* EMULATOR_SRC_LOCAL_FSR4_UPSCALE_H_ */
