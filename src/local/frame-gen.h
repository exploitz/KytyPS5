#ifndef EMULATOR_SRC_LOCAL_FRAME_GEN_H_
#define EMULATOR_SRC_LOCAL_FRAME_GEN_H_
// DLSS Frame Generation through NVIDIA Streamline (KYTY_FRAMEGEN=<generated frames, 1..5>), for
// Demon's Souls PPSA01341 01.007.000 and 01.005.000. The inputs come from the game's own passes, found by frame
// capture (docs/RE-DEMONS-SOULS.md): the G-buffer vertex shaders' constants hold the camera
// (world->clip matrix, the previous frame's, and the TAA jitter), the TAA dispatch reads the depth
// and motion-vector images. The renderer thread gathers them per guest frame; at the guest flip
// they are copied next to the presenter's frame; the present thread hands them to Streamline and
// presents through it (sl.interposer.dll is the process' Vulkan loader, see Initialize()).
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/image/image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace Libs::Graphics {
class CommandBuffer;
class TextureCache;
namespace FrameGen {

// The TAA compute shader, and the G-buffer vertex shaders sharing the constant layout below (flattened SRT
// words), of 01.007.000 and of 01.005.000: the same shaders recompiled (KYTY_SHADER_DUMP: the same resources,
// the vertex shaders the same translated program; the TAA images are what is read from its dispatch).
constexpr std::array<uint64_t, 2> TaaShaders {0xa44e580f4f0cfb2bULL, 0x934a6e1197b99148ULL};
constexpr std::array<uint64_t, 6> GbufferVertexShaders {0xf4d2e78cd1769fc7ULL, 0x08e16923875aa8e9ULL, 0x0ffcbf08be26e74eULL,
                                                        0x1d69b5d27198446aULL, 0x5931e753b757f3bcULL, 0xa97f610824942162ULL};
// Draws of the G-buffer vertex shaders since start: 3D gameplay is on screen (the harness and the
// scripted input use it to tell the level from menus, movies and loading screens).
inline std::atomic<uint64_t> g_gameplay_draws {0};
inline void NoteGameplayDraw(uint64_t vs_hash) {
	if (std::find(GbufferVertexShaders.begin(), GbufferVertexShaders.end(), vs_hash) != GbufferVertexShaders.end())
		g_gameplay_draws.fetch_add(1, std::memory_order_relaxed);
}
inline bool IsTaaShader(uint64_t hash) {
	return std::find(TaaShaders.begin(), TaaShaders.end(), hash) != TaaShaders.end();
}
inline bool IsGbufferVertexShader(uint64_t hash) {
	return std::find(GbufferVertexShaders.begin(), GbufferVertexShaders.end(), hash) != GbufferVertexShaders.end();
}
constexpr uint32_t SrtViewProj     = 29; // 16 floats, row-major, clip = M * world
constexpr uint32_t SrtPrevViewProj = 45; // 12 floats: the previous frame's rows x, y and w
constexpr uint32_t SrtJitter       = 57; // 2 floats: clip-space offset (NDC units)
// The frame's last pass (frame capture, 2026-10-05): one full-screen draw reads the HUD layer (a
// target as large as the output, RGBA16F, that ~60 HUD quads drew into) and the scene, and writes
// the presented image. Its program depends on the output: 4K (and the menus at any output), and 1080p
// (KYTY_RENDER_CONVARS r_Wants4K=WantsPerf: a 1920x1080 HUD layer and A2B10G10R10 image; 10-09 trace of
// a gameplay frame: ~130 HUD quads of 0x36a4fcb0fc83b8f7, then this draw).
constexpr std::array<uint64_t, 2> FinalCompositeShaders {0x1ad8b5870fe4d3ecULL, 0x37db6bd367380e9cULL};
inline bool IsFinalCompositeShader(uint64_t hash) {
	return std::find(FinalCompositeShaders.begin(), FinalCompositeShaders.end(), hash) != FinalCompositeShaders.end();
}

struct Camera {
	std::array<float, 16> view_proj {};
	std::array<float, 16> prev_view_proj {};
	std::array<float, 2>  jitter_ndc {};
	bool                  valid = false;
};

// One presented frame's inputs, owned by the presenter's frame.
struct FrameInputs {
	VulkanImage   depth, motion;
	vk::ImageView depth_view = nullptr, motion_view = nullptr;
	vk::Extent2D  render_extent {};
	Camera        camera;
	uint64_t      guest_frame = 0;
	bool          motion_attachment = false; // the motion copy is a color attachment too
	bool          valid       = false;
	// The frame without its HUD (HudlessWanted): at the game's size, and blitted to the backbuffer's
	// size and format like the presented image (FSR's HUDLessColor).
	VulkanImage   hudless, hudless_backbuffer;
	vk::ImageView hudless_view = nullptr, hudless_backbuffer_view = nullptr;
	bool          hudless_valid = false, hudless_backbuffer_valid = false;
};

// Process start, before the window: loads Streamline when KYTY_FRAMEGEN is set (it then becomes
// SDL's Vulkan loader, and so the proxy of instance, device and swapchain calls).
void Initialize();
// After Initialize: Streamline is loaded (device creation then adds what it relies on but does
// not add itself, VK_KHR_present_id for its VK_NV_low_latency2).
[[nodiscard]] bool Requested();
// After the device exists: checks that DLSS-G is available on it.
void OnDevice(vk::Instance instance, vk::PhysicalDevice physical_device, vk::Device device);
// Right after the dispatcher is loaded for the device (before the recording layer copies it):
// the command-buffer calls Streamline intercepts only for its command-list state tracking
// (disabled here) go straight to the driver.
void BypassCommandHooks(vk::Device device);
[[nodiscard]] bool Enabled();
// Another frame generator (fsr-frame-gen.h) wants the inputs: Enabled() then holds without
// Streamline, whose own calls stay off.
void EnableInputs();
// FSR 4 upscaling (fsr4-upscale.h) wants only the camera: Enabled() holds (the G-buffer draw that has it and the TAA
// dispatch take the native paths) and PrepareInputs copies nothing.
void EnableCamera();
// Renderer thread: this frame's camera, once its G-buffer pass gave it (null before).
[[nodiscard]] const Camera* CurrentCamera();

// Present thread, around vkQueuePresentKHR of the image showing `inputs` (null: a frame
// without inputs, e.g. a menu). `backbuffer` is the swapchain extent, `region` the part the
// game image covers.
void BeforePresent(const FrameInputs* inputs, vk::Extent2D backbuffer, vk::Rect2D region,
                   vk::Format format, uint32_t image_count);
void AfterPresent();
// Present thread: the frames DLSS-G showed per presented frame (1 without generation), as last
// queried (about once a second), for the KYTY_FPS_HUD panel.
[[nodiscard]] uint32_t PresentedFrames();
// KYTY_FPS_HUD: the panel's rectangle, normalized to the game image. Its motion vectors are
// zeroed so generated frames keep the panel in place.
void SetStaticRect(float x0, float y0, float x1, float y1);

// Renderer thread (Kyty.Gpu).
// The guest frame ran the TAA pass: the game's 3D view (play, or a cutscene in the engine), not a movie, a menu or a
// loading screen; with or without frame generation, by the normal or the table way. The guest flip takes it
// (Presenter::PrepareFrame): above 60 Hz the other frames keep 60 (videoOut.cpp).
void NoteScene();
[[nodiscard]] bool TakeScene();
// The current guest frame ran the TAA pass so far, or the last one flipped did (HudAnchor: the game queues a frame's
// interface before the flip of the one before).
[[nodiscard]] bool SceneNoted();
// The guest flips taken so far (TakeScene).
[[nodiscard]] uint64_t GuestFlips();
// The current guest frame has no camera yet: its next G-buffer draw goes the normal way (the table way's draws have
// no constants on the CPU), whose hook reads it.
[[nodiscard]] bool WantsCamera();
void OnDraw(uint64_t vs_hash, std::span<const uint32_t> flattened_srt);
// A draw of this vertex shader would give OnDraw this frame's camera (a G-buffer shader, no camera yet):
// the table path (KYTY_TABLE_XPR), which materializes no SRT on the CPU, leaves it to the native path.
[[nodiscard]] bool WantsDraw(uint64_t vs_hash);
void OnDispatch(uint64_t cs_hash, ImageId depth, uint64_t depth_address, ImageId motion,
                uint64_t motion_address);
// The final composite drawn a second time with its HUD layer cleared gives the frame without its
// HUD, which FSR compares with the presented one to keep the HUD out of the generated frames
// (without it the HUD smears). FSR frame generation only; KYTY_FRAMEGEN_FSR_HUDLESS=0 turns it off.
[[nodiscard]] bool HudlessWanted();
// Around that draw, outside rendering: SaveHud copies the HUD layer aside and clears it; after the
// draw, CaptureHudless copies the result and puts the HUD layer back. Each leaves both images in
// the layout and access it found them in.
void SaveHud(GraphicContext& graphics, vk::CommandBuffer command, Image& hud);
void CaptureHudless(GraphicContext& graphics, vk::CommandBuffer command, Image& target, Image& hud);
// Present thread, ahead of FSR's prepare: the HUD-less image scaled into `region` of a
// backbuffer-sized image of the swapchain's format, as the presented image is.
void BlitHudless(GraphicContext& graphics, vk::CommandBuffer command, FrameInputs& inputs, vk::Extent2D backbuffer,
                 vk::Format format, vk::Rect2D region);
// At the guest flip, in the frame's command buffer: copy the gathered images into `inputs`.
void PrepareInputs(GraphicContext& graphics, TextureCache& cache, CommandBuffer& command,
                   FrameInputs& inputs);
void DestroyInputs(GraphicContext& graphics, FrameInputs& inputs);

} // namespace FrameGen
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_LOCAL_FRAME_GEN_H_
