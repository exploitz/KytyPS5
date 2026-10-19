#pragma once
// Live `shot <path>` (live-control.h): the next frame the presenter prepares, as the guest's display buffer copied for
// presentation, saved as a 32-bit BMP once the GPU finished it (Presenter::PrepareFrame). A test tool: unlike a desktop
// capture it works with the display asleep, and it shows the guest's picture without the window around it.

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace LiveShot {

inline std::mutex        g_mutex;
inline std::string       g_path;
inline uint64_t          g_id = 0;
inline std::atomic<bool> g_pending {false};

inline void Request(uint64_t id, const char* path) {
	std::lock_guard lock(g_mutex);
	g_path = path;
	g_id   = id;
	g_pending.store(true, std::memory_order_release);
}

// Presenter thread: the pending request, taken.
inline bool Take(uint64_t& id, std::string& path) {
	if (!g_pending.load(std::memory_order_acquire)) return false;
	std::lock_guard lock(g_mutex);
	if (!g_pending.exchange(false)) return false;
	id   = g_id;
	path = g_path;
	return true;
}

// `pixels`: rows of 4-byte pixels, top first, in `format` (VkFormat): 37/43 R8G8B8A8, 44/50 B8G8R8A8, 64 A2B10G10R10,
// 58 A2R10G10B10 (other formats are written as B8G8R8A8).
inline void Write(uint64_t id, const std::string& path, const uint8_t* pixels, uint32_t width, uint32_t height,
                  int format) {
	bool ok = false;
	if (FILE* f = std::fopen(path.c_str(), "wb"); f != nullptr) {
		const uint32_t data = width * height * 4u;
		uint8_t        header[54] {};
		const auto     put  = [&](size_t at, uint32_t value) {
            for (int i = 0; i < 4; ++i) header[at + i] = static_cast<uint8_t>(value >> (8 * i));
		};
		header[0] = 'B';
		header[1] = 'M';
		put(2, 54u + data);
		put(10, 54u);
		put(14, 40u);
		put(18, width);
		put(22, static_cast<uint32_t>(-static_cast<int32_t>(height))); // top-down rows
		header[26] = 1;
		header[28] = 32;
		put(34, data);
		ok = std::fwrite(header, 1, sizeof(header), f) == sizeof(header);
		std::string row(width * 4u, '\0');
		for (uint32_t y = 0; ok && y < height; ++y) {
			const uint8_t* src = pixels + static_cast<size_t>(y) * width * 4u;
			for (uint32_t x = 0; x < width; ++x) {
				const uint8_t* p = src + x * 4u;
				const uint32_t v = p[0] | p[1] << 8u | p[2] << 16u | static_cast<uint32_t>(p[3]) << 24u;
				uint32_t       r = p[2], g = p[1], b = p[0];
				if (format == 37 || format == 43) r = p[0], b = p[2];
				if (format == 64) r = (v & 0x3ffu) >> 2u, g = (v >> 12u) & 0xffu, b = (v >> 22u) & 0xffu;
				if (format == 58) b = (v & 0x3ffu) >> 2u, g = (v >> 12u) & 0xffu, r = (v >> 22u) & 0xffu;
				row[x * 4 + 0] = static_cast<char>(b);
				row[x * 4 + 1] = static_cast<char>(g);
				row[x * 4 + 2] = static_cast<char>(r);
				row[x * 4 + 3] = static_cast<char>(0xff);
			}
			ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
		}
		ok = std::fclose(f) == 0 && ok;
	}
	std::printf("LIVE_SHOT id=%" PRIu64 " path=%s width=%u height=%u format=%d ok=%d\n", id, path.c_str(), width, height,
	            format, ok ? 1 : 0);
	std::fflush(stdout);
}

} // namespace LiveShot
