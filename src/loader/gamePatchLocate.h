#ifndef KYTY_LOADER_GAME_PATCH_LOCATE_H_
#define KYTY_LOADER_GAME_PATCH_LOCATE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Where a cheat's writes go in a loaded module (gamePatch.cpp): at their own offsets, or, when the code
// moved (another game version), at the one place where all of the cheat's original bytes are found.
namespace Loader::GamePatch {

struct Write {
	uint64_t             source_address = 0;
	uint64_t             target_address = 0;
	std::vector<uint8_t> off;
	std::vector<uint8_t> on;
};

// The module as the locator reads it: its mapped range, and the segments that hold file bytes.
struct Image {
	struct Segment {
		uint64_t vaddr  = 0;
		uint64_t filesz = 0;
	};
	uint64_t             base        = 0;
	uint64_t             mapped_size = 0;
	std::vector<Segment> segments;
};

// A cheat placed by searching (not at its own offset, or a file for another game version) must check at
// least this many original bytes: shorter code turns up in too many places by chance.
constexpr size_t kMinSearchedBytes = 8;

enum class Found { AtOffset, Moved, NotFound, TooShort, Ambiguous };

inline bool IsZero(const std::vector<uint8_t>& bytes) {
	return std::all_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte == 0; });
}

inline bool IsInside(const Image& image, uint64_t address, size_t size) {
	return size != 0 && address >= image.base && address + size <= image.base + image.mapped_size;
}

// The writes at their offsets moved by `shift`: every original byte must be in place, a code cave inside
// the module must still be empty (zero); one outside it is allocated when applied.
inline bool Place(std::vector<Write>& writes, const Image& image, int64_t shift) {
	for (auto& write: writes) {
		write.target_address = image.base + write.source_address + static_cast<uint64_t>(shift);
		if (!IsInside(image, write.target_address, write.off.size())) {
			if (!IsZero(write.off)) return false;
			continue;
		}
		if (std::memcmp(reinterpret_cast<const void*>(write.target_address), write.off.data(), write.off.size()) != 0) {
			return false;
		}
	}
	return true;
}

inline size_t CheckedBytes(const std::vector<Write>& writes) {
	size_t bytes = 0;
	for (const auto& write: writes) {
		if (!IsZero(write.off)) bytes += write.off.size();
	}
	return bytes;
}

inline Found Locate(std::vector<Write>& writes, const Image& image, bool same_version, int64_t* shift) {
	const size_t checked = CheckedBytes(writes);
	if ((same_version || checked >= kMinSearchedBytes) && Place(writes, image, 0)) {
		*shift = 0;
		return Found::AtOffset;
	}
	if (checked < kMinSearchedBytes) {
		return Found::TooShort;
	}
	const auto anchor = std::max_element(writes.begin(), writes.end(), [](const Write& a, const Write& b) {
		return (IsZero(a.off) ? 0 : a.off.size()) < (IsZero(b.off) ? 0 : b.off.size());
	});
	std::vector<int64_t> shifts;
	for (const auto& segment: image.segments) {
		if (segment.filesz < anchor->off.size()) continue;
		const auto* begin = reinterpret_cast<const uint8_t*>(image.base + segment.vaddr);
		const auto* end   = begin + segment.filesz;
		for (const auto* current = begin; current < end && shifts.size() < 2;) {
			const auto* found = std::search(current, end, anchor->off.begin(), anchor->off.end());
			if (found == end) break;
			const auto candidate = static_cast<int64_t>(static_cast<uint64_t>(found - begin) + segment.vaddr) -
			                       static_cast<int64_t>(anchor->source_address);
			if (std::find(shifts.begin(), shifts.end(), candidate) == shifts.end() && Place(writes, image, candidate)) {
				shifts.push_back(candidate);
			}
			current = found + 1;
		}
		if (shifts.size() >= 2) break;
	}
	if (shifts.size() != 1) {
		return shifts.empty() ? Found::NotFound : Found::Ambiguous;
	}
	*shift = shifts.front();
	(void)Place(writes, image, *shift);
	return Found::Moved;
}

} // namespace Loader::GamePatch

#endif // KYTY_LOADER_GAME_PATCH_LOCATE_H_
