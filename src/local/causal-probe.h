#pragma once
// Causal probe ("prove causality before optimizing", .claude/skills/ps5-emu-optimization): a known delay injected
// at one kind of work, so the frame-time slope shows whether that work is on the critical path. A slope near 1 means
// time saved there is frame time saved; near 0, saving it gains nothing (it overlaps other waits).
// KYTY_PROBE_SITE picks the site (CausalProbe::Site, 0 = off, the default). kyty_local_probe_ns is the delay per hit
// (KYTY_PROBE_NS, or poked live: abba.sh kyty_local_probe_ns 0 N). kyty_local_probe_hits counts the site's hits,
// so the delay per frame is ns x hits per frame. Off, a site costs one relaxed load.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <immintrin.h>

extern "C" {
extern volatile std::atomic_uint32_t kyty_local_probe_site;
extern volatile std::atomic_uint32_t kyty_local_probe_ns;
extern volatile std::atomic_uint64_t kyty_local_probe_hits;
}

namespace CausalProbe {

enum Site : uint32_t {
	GraphicsSlice    = 1, // each slice of a graphics-ring command buffer the GPU thread translates
	ComputeSlice     = 2, // each slice of an async compute queue's command buffer
	GraphicsDispatch = 3, // each dispatch on the graphics ring
	ComputeDispatch  = 4, // each dispatch on an async compute queue
	Draw             = 5, // each draw, on every draw path (a run of n draws counts n)
	Flip             = 6, // once a frame, at the flip the graphics ring reached
	GuestSubmit      = 7, // each graphics submission, on the game's thread that submits it
	SiteCount
};

inline void At(Site site, uint32_t count = 1) {
	if (kyty_local_probe_site.load(std::memory_order_relaxed) != site) [[likely]]
		return;
	kyty_local_probe_hits.fetch_add(count, std::memory_order_relaxed);
	const auto ns = uint64_t {kyty_local_probe_ns.load(std::memory_order_relaxed)} * count;
	if (ns == 0) return;
	const auto end = std::chrono::steady_clock::now() + std::chrono::nanoseconds(ns);
	while (std::chrono::steady_clock::now() < end) _mm_pause();
}

} // namespace CausalProbe
