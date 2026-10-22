#include "loader/demonsSoulsRenderSize.h"
#include "loader/demonsSoulsHdOutput.h"
#include "loader/demonsSoulsSuperResolution.h"

#include "graphics/host_gpu/hostMemory.h"
#include "graphics/presentation/window.h"
#include "graphics/host_gpu/renderer/demonsSouls.h"
#include "loader/elf.h"
#include "loader/guestCode.h"
#include "loader/runtimeLinker.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace Loader::DemonsSoulsRenderSize {
namespace {

bool Readable(uint64_t address, uint64_t size) {
	return Libs::Graphics::HostMemoryRangeIsReadable(address, size);
}

bool HoldsPair(uint64_t address, uint32_t width, uint32_t height) {
	uint32_t pair[2] {};
	if (!Readable(address, sizeof(pair))) return false;
	std::memcpy(pair, reinterpret_cast<const void*>(address), sizeof(pair));
	return pair[0] == width && pair[1] == height;
}

// The one address of a mode's render size the mode cases (`pattern`) load (both cases load the same constant), after
// their output (`output_width` x `output_height`: the 4K modes' unless given).
std::optional<uint64_t> FindRenderConstant(const Program& program, std::string_view pattern, uint32_t width,
                                           uint32_t height, uint32_t output_width = OutputWidth,
                                           uint32_t output_height = OutputHeight) {
	std::vector<uint64_t> constants;
	for (const auto site: GuestCode::Find(program, GuestCode::Pattern(pattern), 64)) {
		int32_t output_displacement = 0, render_displacement = 0;
		std::memcpy(&output_displacement, reinterpret_cast<const void*>(site + 4), sizeof(int32_t));
		std::memcpy(&render_displacement, reinterpret_cast<const void*>(site + 12), sizeof(int32_t));
		const uint64_t output = site + 8 + static_cast<int64_t>(output_displacement);
		const uint64_t render = site + 16 + static_cast<int64_t>(render_displacement);
		if (HoldsPair(output, output_width, output_height) && HoldsPair(render, width, height) &&
		    std::find(constants.begin(), constants.end(), render) == constants.end())
			constants.push_back(render);
	}
	return constants.size() == 1 ? std::optional(constants[0]) : std::nullopt;
}

// The one place `bytes` is in the program's loaded segments.
std::optional<uint64_t> FindUniqueData(const Program& program, std::span<const uint8_t> bytes) {
	const auto* elf = program.elf.get();
	if (elf == nullptr || elf->GetEhdr() == nullptr || elf->GetPhdr() == nullptr) return std::nullopt;
	std::vector<uint64_t> found;
	const auto*           phdr = elf->GetPhdr();
	for (size_t i = 0; i < elf->GetEhdr()->e_phnum && found.size() < 2; i++) {
		if (phdr[i].p_type != PT_LOAD || phdr[i].p_filesz < bytes.size()) continue;
		const auto address = program.base_vaddr + phdr[i].p_vaddr;
		if (!Readable(address, phdr[i].p_filesz)) continue;
		const auto* begin = reinterpret_cast<const uint8_t*>(address);
		const auto* end   = begin + phdr[i].p_filesz;
		for (const auto* at = std::search(begin, end, bytes.begin(), bytes.end()); at != end && found.size() < 2;
		     at             = std::search(at + 1, end, bytes.begin(), bytes.end()))
			found.push_back(reinterpret_cast<uint64_t>(at));
	}
	return found.size() == 1 ? std::optional(found[0]) : std::nullopt;
}

double   g_wide_aspect = 0.0; // the aspect KYTY_RENDER_ASPECT installed (0: 16:9)
uint32_t g_wide_output = 0;   // the output's width InstallWideOutput set (0: the game's 3840)

// The RIP-relative target of the disp32 at `site` + `at`, the instruction `length` bytes long.
uint64_t RipTarget(uint64_t site, uint32_t at, uint32_t length) {
	int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void*>(site + at), sizeof(displacement));
	return site + length + static_cast<int64_t>(displacement);
}

// The wide output (WideOutputWidth) for display mode 5 (`mode`), where 01.007.000 has its places (false, nothing
// changed, when one is not found):
// - the output pair its cases load, `width` x 2160. Display mode 4's (Cinematic: its 3D and output, natively) is the same
//   constant: its two loads are moved to the same 3840x2160 in the mode 3 preset (CinematicSource), Cinematic kept as
//   it is (rendered at 5120x2160 natively the game ran out of its memory pools and stopped);
// - the upscaler's context (SuperResolution::InitContext's 3840, 2160);
// - the fixed 3840x2160 the video settings' three record builders store, which sizes the upscaler's image and the
//   processing after it: each mode's own output instead (rcx = xmm1 in the setter and the constructor, rdx = r10 << 32
//   | r9 in the size query);
// - the mode's entries of the output width and height tables.
// (`height`: 2160, or the HD output's 1080 with the same places: DemonsSoulsSuperResolution::HdOutput.)
bool InstallWideOutput(const Program& program, int mode, uint32_t width, uint32_t height = OutputHeight) {
	std::vector<uint64_t> outputs; // (the setter's and the constructor's mode 5 cases load the same pair)
	for (const auto site: GuestCode::Find(program, GuestCode::Pattern(TrinityCase), 8)) {
		const uint64_t output = RipTarget(site, 4, 8);
		if (HoldsPair(output, OutputWidth, OutputHeight) && std::find(outputs.begin(), outputs.end(), output) == outputs.end())
			outputs.push_back(output);
	}
	if (outputs.size() != 1) return false;
	std::vector<uint64_t> cinematic;
	for (const auto site: GuestCode::Find(program, GuestCode::Pattern(CinematicCase), 8))
		if (RipTarget(site, 4, 8) == outputs[0]) cinematic.push_back(site);
	const auto preset   = FindUniqueData(program, std::span(reinterpret_cast<const uint8_t*>(CinematicSource), sizeof(CinematicSource)));
	const auto contexts = GuestCode::Find(program, GuestCode::Pattern(UpscalerContext), 2);
	const auto setter   = GuestCode::Find(program, GuestCode::Pattern(SetterFixedPair), 2);
	const auto builder  = GuestCode::Find(program, GuestCode::Pattern(ConstructorFixedPair), 2);
	const auto query    = GuestCode::Find(program, GuestCode::Pattern(QueryFixedPair), 2);
	const auto widths   = FindUniqueData(program, std::span(reinterpret_cast<const uint8_t*>(OutputWidths), sizeof(OutputWidths)));
	const auto heights  = FindUniqueData(program, std::span(reinterpret_cast<const uint8_t*>(OutputHeights), sizeof(OutputHeights)));
	if (cinematic.size() != 2 || !preset || contexts.size() != 1 || setter.size() != 1 || builder.size() != 1 ||
	    query.size() != 1 || !widths || !heights)
		return false;
	// (The 3840x2160 after the preset's 0x80ffffff and 2560x1440; nothing writes the preset.)
	const uint64_t source = *preset + 12;
	for (const auto site: cinematic) {
		const auto displacement = static_cast<int32_t>(static_cast<int64_t>(source) - static_cast<int64_t>(site + 8));
		std::memcpy(reinterpret_cast<void*>(site + 4), &displacement, sizeof(displacement));
	}
	const uint32_t pair[2] {width, height};
	std::memcpy(reinterpret_cast<void*>(outputs[0]), pair, sizeof(pair));
	std::memcpy(reinterpret_cast<void*>(contexts[0] + 1), &width, sizeof(width));
	std::memcpy(reinterpret_cast<void*>(contexts[0] + 6), &height, sizeof(height));
	// vmovq rcx, xmm1; nop dword [rax + rax] (10 bytes, as the mov rcx, imm64 they replace).
	constexpr uint8_t from_xmm1[10] {0xc4, 0xe1, 0xf9, 0x7e, 0xc9, 0x0f, 0x1f, 0x44, 0x00, 0x00};
	std::memcpy(reinterpret_cast<void*>(setter[0] + 4), from_xmm1, sizeof(from_xmm1));
	std::memcpy(reinterpret_cast<void*>(builder[0] + 7), from_xmm1, sizeof(from_xmm1));
	// mov edx, r10d; shl rdx, 32; or rdx, r9 (10 bytes, as the mov rdx, imm64).
	constexpr uint8_t from_query[10] {0x44, 0x89, 0xd2, 0x48, 0xc1, 0xe2, 0x20, 0x4c, 0x09, 0xca};
	std::memcpy(reinterpret_cast<void*>(query[0] + 3), from_query, sizeof(from_query));
	std::memcpy(reinterpret_cast<void*>(*widths + mode * sizeof(uint32_t)), &width, sizeof(width));
	std::memcpy(reinterpret_cast<void*>(*heights + mode * sizeof(uint32_t)), &height, sizeof(height));
	return true;
}

} // namespace

double WideAspect() {
	return g_wide_aspect;
}

uint32_t WideOutputWidth() {
	return g_wide_output;
}

uint32_t AutoHeight(bool upscaler) {
	const char* setting = std::getenv("KYTY_RENDER_HEIGHT");
	if (setting != nullptr && *setting != 0 && std::strcmp(setting, "auto") != 0) return 0;
	uint32_t width = 0, height = 0;
	Libs::Graphics::WindowOutputSize(&width, &height);
	if (height == 0) return 0;
	if (upscaler) height = std::max(SuperResolutionLeastHeight, static_cast<uint32_t>(std::lround(height / 1.5)));
	// Without an upscaler at most the game's own 1440: a 4K window would render 2.25 times the default's pixels, a GPU
	// cost nobody chose. 2160 stays a choice (KYTY_RENDER_HEIGHT=2160).
	return std::clamp(height, 720u, upscaler ? OutputHeight : RenderHeight);
}

void Install(Program* program) {
	const char* setting = std::getenv("KYTY_RENDER_HEIGHT");
	const char* wide    = std::getenv("KYTY_RENDER_ASPECT");
	const bool  numeric   = setting != nullptr && *setting != 0 && std::strcmp(setting, "auto") != 0;
	const auto  automatic = AutoHeight(DemonsSoulsSuperResolution::Active());
	// (The PS5 Pro path with the HD output always: its 1080p output is made here.)
	const bool  hd_pro    = DemonsSoulsSuperResolution::Active() && DemonsSoulsSuperResolution::HdOutput();
	if ((!numeric && automatic == 0 && !hd_pro && (wide == nullptr || *wide == 0)) || program == nullptr ||
	    !Libs::Graphics::DemonsSouls::IsSupportedGame() || program->file_name.filename() != "eboot.bin")
		return;
	// (The game's super resolution path: display mode 5 is the performance mode, loader/demonsSoulsSuperResolution.h.
	// Its HD path, loader/demonsSoulsHdOutput.h: display mode 2, 1920x1080; another size is display mode 1's, whose
	// 1600x900 the game upscales to the same 1080p output. Both: display mode 5 with a 1920x1080 output, hd_pro.)
	const bool     trinity     = DemonsSoulsSuperResolution::Active();
	const bool     hd          = DemonsSoulsHdOutput::Active() && !hd_pro;
	const uint32_t game_width  = hd ? DemonsSoulsHdOutput::HdWidth : trinity ? TrinityWidth : RenderWidth;
	const uint32_t game_height = hd ? DemonsSoulsHdOutput::HdHeight : trinity ? TrinityHeight : RenderHeight;
	const int      mode        = hd ? HdUpscaledMode : trinity ? TrinityMode : PerformanceMode;
	const uint32_t out_width   = hd || hd_pro ? DemonsSoulsHdOutput::HdWidth : OutputWidth;
	const uint32_t out_height  = hd || hd_pro ? DemonsSoulsHdOutput::HdHeight : OutputHeight;
	auto requested = automatic != 0 ? automatic
	                 : numeric      ? static_cast<uint32_t>(std::strtoul(setting, nullptr, 10))
	                 : hd_pro       ? DemonsSoulsHdOutput::HdHeight
	                                : game_height;
	if (hd_pro && requested > DemonsSoulsHdOutput::HdHeight) {
		std::printf("Demon's Souls render size: %u is above the 1080p output's 1080 (the HD output with the upscaler); "
		            "1080 instead\n",
		            requested);
		requested = DemonsSoulsHdOutput::HdHeight;
	}
	if (automatic != 0) {
		uint32_t window_width = 0, window_height = 0;
		Libs::Graphics::WindowOutputSize(&window_width, &window_height);
		std::printf("Demon's Souls render size: automatic for the %ux%u window: %s 3D at height %u%s\n", window_width,
		            window_height, hd ? "HD output (1920x1080)," : trinity ? "PS5 Pro output (3840x2160)," : "4K output,",
		            std::min(requested, hd ? DemonsSoulsHdOutput::HdHeight : requested),
		            hd && window_height == DemonsSoulsHdOutput::HdHeight ? " (pixel-exact)"
		            : !hd && !trinity && window_height == OutputHeight ? " (pixel-exact)"
		                                                               : "");
		if (hd && requested > DemonsSoulsHdOutput::HdHeight) requested = DemonsSoulsHdOutput::HdHeight;
	}
	if (hd && requested > DemonsSoulsHdOutput::HdHeight && requested <= OutputHeight) {
		std::printf("Demon's Souls render size: KYTY_RENDER_HEIGHT=%u is above the HD output's 1080; 1080 instead\n",
		            requested);
		requested = DemonsSoulsHdOutput::HdHeight;
	}
	auto aspect = wide != nullptr && *wide != 0 ? ParseAspect(wide) : 0.0;
	if (wide != nullptr && *wide != 0 && aspect == 0.0)
		std::printf("Demon's Souls render size: KYTY_RENDER_ASPECT=%s is not wider than 16:9 and at most 32:9; 16:9 kept\n",
		            wide);
	if (hd_pro && aspect != 0.0) {
		std::printf("Demon's Souls render size: a wider aspect is not with the 1080p output (the HD output with the "
		            "upscaler); 16:9\n");
		aspect = 0.0;
	}
	if (trinity && !hd_pro && requested >= 720 && requested < SuperResolutionLeastHeight) {
		std::printf("Demon's Souls render size: KYTY_RENDER_HEIGHT=%u is below the game's super resolution (1080 at "
		            "least); 1080 instead\n",
		            requested);
		requested = SuperResolutionLeastHeight;
	}
	if (requested == game_height && aspect == 0.0) return;
	auto size = RenderSizeFor(requested, aspect != 0.0 ? aspect : GameAspect);
	if (const auto most = MaxRenderHeight(aspect != 0.0 ? aspect : GameAspect); size.height > most) {
		const auto lower = RenderSizeFor(most, aspect != 0.0 ? aspect : GameAspect);
		std::printf("Demon's Souls render size: %ux%u does not fit the game's render target memory (made for 3840x2160: "
		            "a red or broken picture, or a crash); %ux%u instead\n",
		            size.width, size.height, lower.width, lower.height);
		size = lower;
	}
	if (size.height == 0) {
		std::printf("Demon's Souls render size: KYTY_RENDER_HEIGHT=%s is not 720-2160; the game's %ux%u kept\n", setting,
		            game_width, game_height);
		return;
	}
	auto constant = hd ? FindRenderConstant(*program, ModeCase, ModeWidths[mode], ModeHeights[mode], out_width, out_height)
	                   : FindRenderConstant(*program, trinity ? TrinityCase : ModeCase, game_width, game_height);
	if (!constant && !trinity && !hd) constant = FindRenderConstant(*program, ModeCaseDuplicated, game_width, game_height);
	const auto widths   = FindUniqueData(*program, std::span(reinterpret_cast<const uint8_t*>(ModeWidths), sizeof(ModeWidths)));
	const auto heights  = FindUniqueData(*program, std::span(reinterpret_cast<const uint8_t*>(ModeHeights), sizeof(ModeHeights)));
	// (The render setup's 16:9: the one such store in the code.)
	std::optional<uint64_t> aspect_store;
	if (aspect != 0.0) {
		const auto sites = GuestCode::Find(*program, GuestCode::Pattern(AspectStore), 2);
		if (sites.size() == 1) aspect_store = sites[0];
	}
	if (!constant || !widths || !heights || (aspect != 0.0 && !aspect_store)) {
		std::printf("Demon's Souls render size: the video settings of a known build not found; the game's %ux%u kept\n",
		            game_width, game_height);
		return;
	}
	// Installation occurs before module initializers or guest threads run; program memory is writable to the host.
	const uint32_t pair[2] {size.width, size.height};
	std::memcpy(reinterpret_cast<void*>(*constant), pair, sizeof(pair));
	std::memcpy(reinterpret_cast<void*>(*widths + mode * sizeof(uint32_t)), &size.width, sizeof(uint32_t));
	std::memcpy(reinterpret_cast<void*>(*heights + mode * sizeof(uint32_t)), &size.height, sizeof(uint32_t));
	if (hd) DemonsSoulsHdOutput::SetMode(HdUpscaledMode + 1);
	if (aspect_store) {
		// The projection's aspect: the render size's own, as the game's 2560x1440 has 16/9.
		const float value = static_cast<float>(static_cast<double>(size.width) / size.height);
		std::memcpy(reinterpret_cast<void*>(*aspect_store + 3), &value, sizeof(value));
		g_wide_aspect = value;
	}
	// The super resolution path with a wider aspect: the upscaler makes the wide output (WideOutputWidth).
	const char* wide_output = std::getenv("KYTY_WIDE_OUTPUT");
	if (!trinity && aspect != 0.0 && wide_output != nullptr && std::strcmp(wide_output, "1") == 0) {
		std::printf("Demon's Souls render size: the wide output (KYTY_WIDE_OUTPUT=1) is the PS5 Pro path's only (KYTY_UPSCALER)%s; "
		            "the 3D squeezed into the %ux%u output\n",
		            hd ? ", not the HD output's" : "", out_width, out_height);
	}
	if (trinity && aspect != 0.0 && (wide_output == nullptr || std::strcmp(wide_output, "0") != 0)) {
		const auto output = RenderSizeFor(OutputHeight, aspect);
		if (InstallWideOutput(*program, mode, output.width)) {
			g_wide_output = output.width;
		} else {
			std::printf("Demon's Souls render size: the wide output's settings of a known build not found; the 3D squeezed "
			            "into the 3840x2160 output\n");
		}
	}
	if (hd_pro) {
		// The PS5 Pro path's 1080p output (the wide output's places at 1920x1080), and its super resolution from this
		// render height on (the game's own 1080 at least otherwise).
		if (!InstallWideOutput(*program, mode, DemonsSoulsHdOutput::HdWidth, DemonsSoulsHdOutput::HdHeight))
			std::printf("Demon's Souls render size: the PS5 Pro path's output settings of a known build not found; its "
			            "4K output kept\n");
		if (!DemonsSoulsSuperResolution::SetLeastHeight(program, size.height))
			std::printf("Demon's Souls render size: the super resolution's least height of a known build not found; it "
			            "runs from 1080 only\n");
	}
	std::printf("Demon's Souls render size: %s mode renders %ux%u instead of %ux%u (output %ux%u)%s\n",
	            hd ? "HD" : "performance", size.width, size.height, game_width, game_height,
	            g_wide_output != 0 ? g_wide_output : out_width, out_height,
	            g_wide_output != 0 ? ", its projection and the output as wide" : aspect_store ? ", its projection as wide" : "");
}

} // namespace Loader::DemonsSoulsRenderSize
