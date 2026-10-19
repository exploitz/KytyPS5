// Where a cheat goes (loader/gamePatchLocate.h): at its offsets, moved to the single place its original code
// is found (another game version), or left out when that cannot be decided safely.
#include "loader/gamePatchLocate.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
using namespace Loader::GamePatch;

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		std::printf("FAILED: %s\n", what);
		failures++;
	}
}

// A 0x1000-byte module whose file bytes are the first 0x800 (one segment), the rest padding.
struct Module {
	std::vector<uint8_t> bytes = std::vector<uint8_t>(0x1000, 0);
	Image                image;
	Module() {
		for (size_t i = 0; i < 0x800; i++) bytes[i] = static_cast<uint8_t>((i * 37 + 11) ^ (i >> 3));
		image.base        = reinterpret_cast<uint64_t>(bytes.data());
		image.mapped_size = bytes.size();
		image.segments    = {{0, 0x800}};
	}
	[[nodiscard]] std::vector<uint8_t> At(size_t offset, size_t size) const {
		return {bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.begin() + static_cast<ptrdiff_t>(offset + size)};
	}
};

Write Line(uint64_t offset, std::vector<uint8_t> off) {
	Write write;
	write.source_address = offset;
	write.on             = std::vector<uint8_t>(off.size(), 0x90);
	write.off            = std::move(off);
	return write;
}
} // namespace

int main() {
	int64_t shift = 0;
	{
		// The file's version: in place, even with little original code.
		Module m;
		std::vector<Write> writes {Line(0x100, m.At(0x100, 2))};
		Check(Locate(writes, m.image, true, &shift) == Found::AtOffset && shift == 0 &&
		          writes[0].target_address == m.image.base + 0x100,
		      "a short cheat at its offset applies on its own version");
	}
	{
		// Another version with the code at the same offset: needs enough original bytes to trust it.
		Module m;
		std::vector<Write> lines {Line(0x100, m.At(0x100, 6)), Line(0x140, m.At(0x140, 4))};
		Check(Locate(lines, m.image, false, &shift) == Found::AtOffset, "10 original bytes at the offset apply");
		std::vector<Write> short_line {Line(0x100, m.At(0x100, 4))};
		Check(Locate(short_line, m.image, false, &shift) == Found::TooShort,
		      "4 original bytes are not trusted on another version, even at the offset");
	}
	{
		// The code moved: the cheat (two lines 0x40 apart) follows it.
		Module m;
		const auto first = m.At(0x300, 6), second = m.At(0x340, 4);
		std::vector<Write> writes {Line(0x2c0, first), Line(0x300, second)};
		Check(Locate(writes, m.image, false, &shift) == Found::Moved && shift == 0x40 &&
		          writes[0].target_address == m.image.base + 0x300 && writes[1].target_address == m.image.base + 0x340,
		      "a cheat whose code moved by 0x40 is placed there, all lines together");
		std::vector<Write> same {Line(0x2c0, first), Line(0x300, second)};
		Check(Locate(same, m.image, true, &shift) == Found::Moved && shift == 0x40,
		      "on its own version too, a cheat whose code is not at its offset is found where it moved");
	}
	{
		// Lines that only match together: the second line breaks the first one's other matches.
		Module m;
		auto       anchor = m.At(0x200, 8);
		const auto other  = m.At(0x220, 4);
		std::copy(anchor.begin(), anchor.end(), m.bytes.begin() + 0x600); // a second copy of the long line
		std::vector<Write> writes {Line(0x100, anchor), Line(0x120, other)};
		Check(Locate(writes, m.image, false, &shift) == Found::Moved && shift == 0x100,
		      "the copy of one line without the other is not a match");
		std::copy(other.begin(), other.end(), m.bytes.begin() + 0x620); // now both places hold both lines
		std::vector<Write> again {Line(0x100, anchor), Line(0x120, other)};
		Check(Locate(again, m.image, false, &shift) == Found::Ambiguous, "two complete matches are left out");
	}
	{
		// Not there at all.
		Module m;
		std::vector<Write> writes {Line(0x100, {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef, 0x01})};
		Check(Locate(writes, m.image, false, &shift) == Found::NotFound, "missing code is not placed");
	}
	{
		// Code caves: inside the module they must still be empty; outside they are allocated later.
		Module m;
		std::vector<Write> writes {Line(0x200, m.At(0x200, 8)), Line(0x900, std::vector<uint8_t>(16, 0)),
		                           Line(0x2000, std::vector<uint8_t>(16, 0))};
		Check(Locate(writes, m.image, false, &shift) == Found::AtOffset && writes[2].target_address == m.image.base + 0x2000,
		      "an empty cave inside the module and one past its end are accepted");
		m.bytes[0x905] = 0xcc;
		std::vector<Write> used {Line(0x200, m.At(0x200, 8)), Line(0x900, std::vector<uint8_t>(16, 0))};
		Check(Locate(used, m.image, true, &shift) != Found::AtOffset, "a cave that holds code is not overwritten");
		std::vector<Write> outside {Line(0x2000, m.At(0x200, 8))};
		Check(Locate(outside, m.image, true, &shift) != Found::AtOffset,
		      "original code expected past the end of the module is not a match");
	}
	if (failures == 0) std::printf("GamePatchLocateTests: ok\n");
	return failures == 0 ? 0 : 1;
}
