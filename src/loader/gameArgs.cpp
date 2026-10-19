#include "loader/gameArgs.h"

#include "common/file.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

namespace Loader::GameArgs {
namespace {

// One "+name=value" line per entry of a list (';' or ',' between entries).
std::string LinesOf(const char* variable) {
	std::string out;
	const char* value = std::getenv(variable);
	const std::string list = value != nullptr ? value : "";
	for (size_t at = 0; at < list.size();) {
		auto end = list.find_first_of(";,", at);
		if (end == std::string::npos) end = list.size();
		const auto first = list.find_first_not_of(' ', at);
		const auto last  = list.find_last_not_of(' ', end - 1);
		if (first != std::string::npos && first < end && last >= first) out += "+" + list.substr(first, last - first + 1) + "\n";
		at = end + 1;
	}
	return out;
}

// KYTY_GAME_CONVARS: the game's command-line file, read once at boot. KYTY_RENDER_CONVARS: the graphics-mode
// preset files (misc/renderconfig_ps5_*.txt), which the game runs again whenever its options load or the
// Graphics Mode changes, after the command line; a setting those files also set (r_Wants4K, the output
// resolution tier) only holds from there.
const std::string& CommandLineLines() {
	static const std::string lines = LinesOf("KYTY_GAME_CONVARS");
	return lines;
}
const std::string& RenderConfigLines() {
	static const std::string lines = LinesOf("KYTY_RENDER_CONVARS");
	return lines;
}

// The lines a read of `real` gets after its own (none: not redirected). The engine opens its files in lower case
// ("$/packagecmdlineargs.txt"): names are compared without case.
const std::string& ExtraLines(const std::filesystem::path& real) {
	static const std::string none;
	auto name = real.filename().string();
	for (auto& c: name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (name == "packagecmdlineargs.txt") return CommandLineLines();
	if (name.starts_with("renderconfig_") && name.ends_with(".txt")) return RenderConfigLines();
	return none;
}

std::string Contents(const std::filesystem::path& real) {
	std::string  text;
	Common::File in;
	if (in.Open(real, Common::File::Mode::Read)) { // (also a game archive's member)
		text.resize(static_cast<size_t>(in.Size()));
		uint32_t read = 0;
		in.Read(text.data(), static_cast<uint32_t>(text.size()), &read);
		text.resize(read);
	}
	if (!text.empty() && text.back() != '\n') text += '\n';
	return text + ExtraLines(real);
}

} // namespace

std::filesystem::path RedirectRead(const std::filesystem::path& real) {
	const auto& extra = ExtraLines(real);
	if (extra.empty()) return {};
	static std::mutex mutex;
	std::lock_guard   lock(mutex);
	// The copy goes next to the temporary files; one that cannot be written leaves the file as it is.
	std::error_code error;
	const auto      copy = std::filesystem::temp_directory_path(error) / ("kyty-" + real.filename().string());
	if (error) return {};
	const auto    text = Contents(real);
	std::ofstream out(copy, std::ios::binary | std::ios::trunc);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	out.close();
	if (!out) return {};
	std::printf("Game args: %s read with\n%s", real.string().c_str(), extra.c_str());
	std::fflush(stdout);
	return copy;
}

std::optional<uint64_t> RedirectedSize(const std::filesystem::path& real) {
	if (ExtraLines(real).empty()) return std::nullopt;
	return Contents(real).size();
}

} // namespace Loader::GameArgs
