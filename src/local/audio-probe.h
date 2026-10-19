#ifndef EMULATOR_SRC_LOCAL_AUDIO_PROBE_H_
#define EMULATOR_SRC_LOCAL_AUDIO_PROBE_H_
// KYTY_AUDIO_PROBE=<control> (Square: use the equipped item): how long a scripted press of that control
// takes to be heard, measured where the emulator hands audio to the device. The scripted input notes
// each press (NotePress); every buffer the audio output queues is measured (its RMS); the first one
// after a press whose level rises to four times the level before the press (and above a floor) is the
// press's sound. Each press prints "AudioProbe: press N: sound queued +A ms, at the device +B ms" (B
// adds what the device still had queued: the delay before Windows and the speakers take over), or
// that no sound followed within 1.5 s. A game-side audio lag (the sound produced late) shows in A, a
// deep device queue in B - A. For A/B runs of the same actions: the ratios, not the absolute values.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace AudioProbe {

inline const char* Control() {
	static const char* control = std::getenv("KYTY_AUDIO_PROBE");
	return control != nullptr && control[0] != '\0' ? control : nullptr;
}

inline int64_t NowUs() {
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

inline std::atomic<int64_t>  g_press_us {0};
inline std::atomic<uint32_t> g_press_id {0};
inline std::atomic<uint32_t> g_reported {0}; // the last press reported (by whichever port heard it first)

// The scripted input, as it presses `name`.
inline void NotePress(std::string_view name) {
	const char* control = Control();
	if (control == nullptr || name != control) return;
	g_press_us.store(NowUs(), std::memory_order_relaxed);
	g_press_id.fetch_add(1, std::memory_order_release);
}

// A buffer the audio output queues for port `port`: `bytes` of `float_samples` (else 16-bit), the device
// holding `queued_before_us` of audio before it.
inline void NoteOutput(int port, const void* data, uint32_t bytes, bool float_samples, uint64_t queued_before_us) {
	if (Control() == nullptr || port < 0 || port >= 32 || data == nullptr || bytes == 0) return;
	struct State {
		double   level      = 0;  // the running level (exponential average of the buffers' RMS)
		double   before     = -1; // the level when the pending press was first seen
		uint32_t press_seen = 0;
	};
	static std::array<State, 32> states {};
	auto&        state = states[static_cast<size_t>(port)];
	double       sum   = 0;
	size_t       count = 0;
	if (float_samples) {
		count = bytes / sizeof(float);
		for (size_t i = 0; i < count; ++i) {
			float value = 0;
			std::memcpy(&value, static_cast<const uint8_t*>(data) + i * sizeof(float), sizeof(value));
			sum += static_cast<double>(value) * value;
		}
	} else {
		count = bytes / sizeof(int16_t);
		for (size_t i = 0; i < count; ++i) {
			int16_t value = 0;
			std::memcpy(&value, static_cast<const uint8_t*>(data) + i * sizeof(int16_t), sizeof(value));
			const double sample = value / 32768.0;
			sum += sample * sample;
		}
	}
	if (count == 0) return;
	const double   rms   = std::sqrt(sum / static_cast<double>(count));
	const uint32_t press = g_press_id.load(std::memory_order_acquire);
	if (press != 0 && press != g_reported.load(std::memory_order_relaxed)) {
		const int64_t now     = NowUs();
		const int64_t elapsed = now - g_press_us.load(std::memory_order_relaxed);
		if (state.press_seen != press) {
			state.press_seen = press;
			state.before     = state.level;
		}
		if (elapsed > 1500000) {
			uint32_t expected = g_reported.load(std::memory_order_relaxed);
			if (expected != press && g_reported.compare_exchange_strong(expected, press)) {
				std::printf("AudioProbe: press %u: no sound within 1.5 s\n", press);
				std::fflush(stdout);
			}
		} else if (rms > std::max(state.before * 4.0, 0.01)) {
			uint32_t expected = g_reported.load(std::memory_order_relaxed);
			if (expected != press && g_reported.compare_exchange_strong(expected, press)) {
				std::printf("AudioProbe: press %u: sound queued +%.1f ms, at the device +%.1f ms (port %d, level %.4f "
				            "after %.4f)\n",
				            press, static_cast<double>(elapsed) / 1000.0,
				            static_cast<double>(elapsed + static_cast<int64_t>(queued_before_us)) / 1000.0, port, rms,
				            state.before);
				std::fflush(stdout);
			}
		}
		return; // the level before a press stays the level until its sound
	}
	state.level = state.level == 0 ? rms : state.level * 0.9 + rms * 0.1;
}

} // namespace AudioProbe

#endif // EMULATOR_SRC_LOCAL_AUDIO_PROBE_H_
