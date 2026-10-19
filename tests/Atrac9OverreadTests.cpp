// libatrac9 reading past a superframe (atrac9_padded.h): every superframe of a real ATRAC9 stream (a game's .at9
// file, given on the command line) decoded with its last byte at the end of a readable page and the next page
// no-access. `raw`: the decoder reads the guest bytes in place (an overread faults: the process ends with an
// access violation); `padded`: through DecodeAtrac9FramePadded, as the emulator decodes.
#include "libatrac9.h"
#include "libs/ajm/atrac9_padded.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
[[noreturn]] void Fail(const char* message) {
	std::fprintf(stderr, "Atrac9OverreadTests: %s\n", message);
	std::exit(1);
}

struct Stream {
	unsigned char        config[4] {};
	std::vector<uint8_t> data;
};

// RIFF WAVE with WAVEFORMATEXTENSIBLE: the ATRAC9 config is the 4 bytes after the format's version word
// (fmt + 0x2c), the frames the "data" chunk.
Stream Load(const char* path) {
	std::ifstream file(path, std::ios::binary);
	const std::vector<uint8_t> bytes {std::istreambuf_iterator<char>(file), {}};
	if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
		Fail("not a RIFF WAVE file");
	Stream stream;
	bool   fmt = false;
	for (size_t at = 12; at + 8 <= bytes.size();) {
		uint32_t size = 0;
		std::memcpy(&size, bytes.data() + at + 4, 4);
		if (at + 8 + size > bytes.size()) break;
		if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0 && size >= 0x30) {
			std::memcpy(stream.config, bytes.data() + at + 8 + 0x2c, 4);
			fmt = true;
		} else if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
			stream.data.assign(bytes.begin() + static_cast<ptrdiff_t>(at + 8), bytes.begin() + static_cast<ptrdiff_t>(at + 8 + size));
		}
		at += 8 + size + (size & 1u);
	}
	if (!fmt || stream.data.empty()) Fail("no ATRAC9 fmt or data chunk");
	return stream;
}
} // namespace

int main(int argc, char** argv) {
	if (argc < 3) Fail("usage: atrac9_overread_tests raw|padded <file.at9> [...]");
	const bool padded = std::strcmp(argv[1], "padded") == 0;
	SYSTEM_INFO system {};
	GetSystemInfo(&system);
	const size_t page = system.dwPageSize;
	auto* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	DWORD old = 0;
	if (pages == nullptr || !VirtualProtect(pages + page, page, PAGE_NOACCESS, &old)) Fail("guard page");
	std::vector<uint8_t> scratch;
	size_t superframes = 0, frames = 0;
	for (int f = 2; f < argc; ++f) {
		const auto stream = Load(argv[f]);
		void* handle = Atrac9GetHandle();
		if (handle == nullptr || Atrac9InitDecoder(handle, const_cast<unsigned char*>(stream.config)) != 0) {
			std::fprintf(stderr, "skipped (not an ATRAC9 config): %s\n", argv[f]);
			if (handle != nullptr) Atrac9ReleaseHandle(handle);
			continue;
		}
		Atrac9CodecInfo info {};
		Atrac9GetCodecInfo(handle, &info);
		std::vector<short> pcm(static_cast<size_t>(info.frameSamples) * info.channels);
		const size_t superframe = static_cast<size_t>(info.superframeSize);
		for (size_t at = 0; at + superframe <= stream.data.size(); at += superframe) {
			// The superframe ends where the readable page ends.
			uint8_t* place = pages + page - superframe;
			std::memcpy(place, stream.data.data() + at, superframe);
			size_t offset = 0;
			for (int i = 0; i < info.framesInSuperframe; ++i) {
				int        used  = 0;
				const auto input = place + offset;
				const int  ret   = padded ? Libs::Audio::Ajm::DecodeAtrac9FramePadded(scratch, input, superframe - offset,
				                                                                     [&](const uint8_t* in) {
					                                                                     return Atrac9Decode(handle, in, pcm.data(), &used, 0);
				                                                                     })
				                          : Atrac9Decode(handle, input, pcm.data(), &used, 0);
				if (ret != 0 || used <= 0) break; // an undecodable frame: the emulator conceals it
				offset += static_cast<size_t>(used);
				++frames;
			}
			++superframes;
		}
		Atrac9ReleaseHandle(handle);
	}
	std::printf("Atrac9OverreadTests %s: %zu superframes, %zu frames decoded at a page end without a fault\n",
	            padded ? "padded" : "raw", superframes, frames);
	return 0;
}
