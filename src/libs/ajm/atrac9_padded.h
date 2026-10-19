#pragma once
// libatrac9's bit reader loads whole 32-bit words, so decoding a frame can read up to a few bytes past the
// frame's last byte. When the game's superframe ends where its mapped memory ends (Shrine of Storms, 10-07: a
// host access violation in PeekInt right after the region) that read faults. The decoder is given a copy of
// the superframe's remaining bytes followed by zeros instead.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Libs::Audio::Ajm {

// Zeros after the copied bytes: well past any word the bit reader can load beyond the end.
inline constexpr size_t Atrac9InputPadding = 64;

// decode(const uint8_t* input) -> int: the libatrac9 call, given the padded copy of `available` bytes.
template <typename Decode>
int DecodeAtrac9FramePadded(std::vector<uint8_t>& scratch, const uint8_t* input, size_t available, Decode&& decode) {
	if (scratch.size() < available + Atrac9InputPadding) scratch.resize(available + Atrac9InputPadding);
	if (available != 0) std::memcpy(scratch.data(), input, available);
	std::memset(scratch.data() + available, 0, Atrac9InputPadding);
	return decode(scratch.data());
}

} // namespace Libs::Audio::Ajm
