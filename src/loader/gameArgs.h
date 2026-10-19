#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

// Engine console variables for the game's own command-line file: KYTY_GAME_CONVARS="name=value;name=value"
// gives a read-only open of the PackageCmdLineArgs.txt the game ships a copy with a "+name=value" line per
// entry added (Demon's Souls passes every line of that file to its console at boot), so engine settings
// change without touching the game's files. KYTY_RENDER_CONVARS does the same for the graphics-mode preset files
// (misc/renderconfig_ps5_*.txt), which the game runs again when its options load: r_Wants4K=WantsBase there renders
// and outputs 1080p instead of Performance mode's 1440p scene and 4K output.
namespace Loader::GameArgs {

// The host file a read-only open of `real` should open instead (empty: `real` itself).
std::filesystem::path RedirectRead(const std::filesystem::path& real);
// A stat of `real`: the size of the copy a read would get (nothing: its own).
std::optional<uint64_t> RedirectedSize(const std::filesystem::path& real);

} // namespace Loader::GameArgs
