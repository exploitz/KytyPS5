# KytyPS5 for Demon's Souls

A Windows build of the KytyPS5 PlayStation 5 emulator, tuned for one game: **Demon's Souls (PS5, PPSA01341)**.

[chenxiao07's Demon's Souls branch of KytyPS5](https://github.com/chenxiao07/KytyPS5) is the foundation of this
fork: the table path (GPU-driven draws), native XPR, the shader precompile, the graphics presets, the DualSense
support and much more are his work. This fork builds on his v20261016b (commit `efc1efc7`) and adds **10-22%
more speed** ([measured](#performance) against chen's own build), its own launcher, 1080p
output modes, more graphics options and fixes, listed [below](#what-this-fork-adds).

chen's branch is based on [KytyPS5](https://github.com/KytyPS5/KytyPS5), which is itself based on
[Kyty](https://github.com/InoriRus/Kyty) by InoriRus.

<!-- screenshot: the launcher's Home page -->
<!-- screenshot: Demon's Souls in game (Boletaria 1-1, looking at the castle) -->

## Download and play

1. Download the zip from **[the Releases page](https://github.com/exploitz/KytyPS5/releases)**.
2. Unzip it into a folder of its own with about 20 GB free, for example `C:\Games\KytyPS5` (not Program Files,
   Downloads, Temp or OneDrive: the shader caches, logs and saves are kept inside it).
3. Double-click **KytyPS5 Launcher.exe**. Windows may say it protected your PC (the programs are not signed):
   click More info, then Run anyway.
4. The first-run setup checks your PC, asks for **your own dump of the game** (the folder with `eboot.bin` and
   `sce_sys`, usually `PPSA01341-app0`), picks settings for your screen and processor, and offers the shader
   precompile (recommended once).
5. Click **Play**.

The guide inside the zip (also [docs/PORTABLE-README.md](docs/PORTABLE-README.md)) explains every setting, the
first launch, performance tips and troubleshooting.

### Game version

| Game | Title ID | Versions |
| --- | --- | --- |
| Demon's Souls | PPSA01341 | 1.07 (01.007.000): tested with this fork. 1.05 (01.005.000): supported by chen's branch; this fork's additions were tested on 1.07 only (the lighting rule matches 1.07's shaders and does not apply to 1.05; the 1080p modes are untested there). Update to 1.07 if you can. |
| Demon's Souls (US) | PPSA01342 | Same game patches and settings; untested. |

The launcher reads the game's version and warns when it is not one of these.

## Requirements

| Part | Recommended | Notes |
| --- | --- | --- |
| Windows | Windows 10 (22H2) or 11, 64-bit | **Windows only** for now. Linux is planned; macOS is not supported. |
| Processor (CPU) | With AVX2: Intel Core 4th generation, AMD Ryzen, or newer | Single-thread speed matters most (the emulator's render thread runs on one core). More threads make the shader precompile faster. |
| Graphics card (GPU) | Vulkan 1.3, **12 GB of video memory** | 8 GB works, with lower frame rates in heavy areas. Tested on NVIDIA RTX; **AMD and Intel GPUs are untested**. |
| Memory (RAM) | **32 GB** | 16 GB works, with slower loading. The emulator commits about 34 GB of RAM plus page file: leave the page file on "System managed size". |
| Disk space | About 20 GB free, NTFS | Static precompile about 5.5 GB (one file: not FAT32/exFAT), translation store about 5 GB, driver cache about 1 GB. An SSD is recommended. |

## What this fork adds

Everything below sits on top of chen's work, which remains the foundation.

1. **Faster GPU shaders.** The shader translator drops redundant per-lane masking work (EXEC-mask selects) that
   most of the game's shaders carried: the largest single gain, +6-7% at Boletaria 1-1 and the Tower of Latria.
2. **Translation store.** Translated shaders are kept on disk between launches, so later launches load them
   instead of translating about 20,000 shaders again: loading takes about a minute.
3. **Faster lighting.** A Demon's Souls-specific rule for four tile-lighting and fog compute shaders: the
   lighting's GPU time at Boletaria 1-1 drops from 4.34 ms to 3.42 ms (+1.6% to +5.5% fps).
4. **Its own texture-memory collector.** It decides which textures to drop when video memory runs short, keeps
   textures that come back soon after being dropped, and sets its levels from your card's video memory budget:
   about 4-5% faster than chen's collector in our Stonefang test.
5. **A launcher.** First-run setup with a PC check and recommended settings, every setting explained, the shader
   precompile and the game start with their progress inside the launcher, and a one-click problem report. On
   Intel processors with efficiency cores it runs the precompile on the performance cores, about 2.5 times as fast
   (154 to 61 minutes on the test PC).
   It is a Qt application, ready for Linux.
6. **1080p output modes with the HUD at its normal size.** Small patches to the game's code, applied at start
   and located by the game's original bytes: "1080p Quality" renders the 1440p scene and scales it down,
   "1080p Performance" renders at 1080p.
7. **"Fewer shadows" graphics preset.** No ambient shadowing, no shadows from torches and other small lights,
   sun shadows only up to 60 m; the scenery is left as it is. +12% (Boletaria 1-1), +15% (Tower of Latria),
   +17% (Nexus). chen's Low and Lowest go further but swap some foreground objects for simpler ones.
8. **FSR 4 anti-aliasing (experimental).** Replaces the game's TAA at the same resolution: cleaner edges, about
   13% slower. Built from an older community-hosted FSR 4 "v07" source; **not AMD's official FSR 4**.
9. **FSR 3.1 frame generation** (AMD FidelityFX, any GPU), with the HUD kept out of generated frames.
10. **A faster memory allocator** (mimalloc) behind the emulator's own allocations: +1.4-2.5%.
11. **Video memory kept under the card's budget.** A 12 GB card was treated as a small one (its usable budget reads
    under 12 GB), which parked textures outside the counted memory and left the card paging in a new game and in
    boss fights; textures the GPU made, which no collector could evict, are now swapped out to RAM under pressure
    and restored when the game uses them again (RAM, not video memory, pays).
12. **Keyboard and mouse play.** The mouse turns the camera in proportion to its speed (sensitivity in the
    launcher), the first click in the window takes it and Esc gives it back; the mouse buttons attack, block and
    lock on, the wheel switches spells and items; a PC key layout (Space rolls, E interacts, R uses the item, F
    two-hands). See docs/PORTABLE-README.md, Controls.
13. **Fixes.** A crash from the pause menu (a render target found again before use); a crash in the ATRAC9 audio decoder at the Shrine of Storms; 7.1 sound folded down to stereo
    when your output is not 7.1, and the game's ambisonic sound objects decoded into the 7.1 mix; the game's
    file reads on its own threads (a movie froze with the background reader); a translation store that grew at
    every launch.

**DLSS is not included.** NVIDIA's SDK license is not compatible with shipping it inside a GPL program. Using
DLSS through NVIDIA's own Streamline download, which you would add yourself, is planned for a later release.
**Trophies** are not recorded yet.

<!-- screenshot: Standard vs Fewer shadows, same spot (Tower of Latria) -->

## Performance

chen's own build of his commit `6bae2db5` against this fork's exe built on the same commit. (The release adds his
next two commits, `4254fb56` and `30d2d2cb`, which open the game's files ahead while it loads: they change loading,
not the frame rate.)

- Settings: PS5 Performance mode (1440p scene, 4K output), frame rate uncapped for measuring.
- Test PC: Intel Core i9-12900K, NVIDIA GeForce RTX 3080 Ti, 64 GB RAM, Windows 11.
- Builds: chen's with his PGO profile for this CPU (`-march=native`); this fork's release exe (`-march=x86-64-v3`,
  any AVX2 CPU) with the same profile.
- Method: both builds' shader caches warm first, then rounds in mirrored order (chen's build first and last).
  Each figure averages the two rounds of that build (2026-10-15).

| Area | chen's build | This fork | Change |
| --- | ---: | ---: | ---: |
| Boletaria 1-1, standing | 42.5 | 48.2 | +13% |
| Boletaria 1-2, standing | 43.6 | 53.0 | +22% |
| Tower of Latria, standing | 45.6 | 50.3 | +10% |
| Shrine of Storms beach, standing | 48.1 | 56.0 | +16% |
| Nexus, standing | 56.5 | 66.4 | +17% |
| Stonefang 2-1, camera turning | 65.6 | 73.9 | +13% |
| Shrine of Storms beach, camera turning | 61.9 | 69.3 | +12% |
| Boletaria 1-1, camera turning | 56.3 | 64.3 | +14% |
| Boletaria 1-2, camera turning | 60.7 | 69.5 | +15% |
| Nexus, camera turning | 52.2 | 58.7 | +12% |

- The 1% lows (the slowest frames) are better in every area.
- At the normal 60 fps limit most areas hold 60 fps while moving on this PC; the heaviest views, such as
  Boletaria 1-1 looking at the castle, run in the high 40s.

Results vary by PC: the CPU's single-thread speed matters most, then the graphics card.

## Settings

| Setting | Choices | In short |
| --- | --- | --- |
| Output | 4K (PS5) / 1080p Quality / 1080p Performance | The picture the game makes: 4K as on PS5, or 1080p with the HUD at its normal size. |
| 3D resolution | 1440p (PS5) down to 900p | With the 4K output: a smaller 3D scene for weaker graphics cards. |
| Aspect ratio | 16:9 (PS5) / 21:9 / 32:9 | With the 4K output: the 3D rendered wider for ultrawide screens (chen's). |
| Graphics detail | Standard (PS5) / Fewer shadows / Low / Lowest | Less to draw for slower CPUs; Fewer shadows keeps the scenery. |
| Anti-aliasing | Game TAA / FSR 4 (experimental) | FSR 4: cleaner edges, about 13% slower. |
| Frame generation | Off / FSR 3.1 | Smoother motion, a little more input lag. |
| Display mode | Fullscreen / window sizes | F11 or Alt+Enter switches in game. |
| Keep 16:9 | On / Off | Black bars on ultrawide and 16:10 screens. |
| Frame rate limit | 60 (PS5) / 120 | 120: above 60 where your PC can; movies and menus stay at 60. |
| VSync | Off / On | On: no tearing; G-SYNC/FreeSync keep adapting. |
| HDR | Off / On | Needs Windows HDR on for the display. |
| Performance overlay | Off / Frame rate / Detailed | The emulator's own frame rate and frame-time panel. |
| Language | the PS5 console languages | The game's text and voices. |

## Testing and reporting problems

This release is tested on one PC. Reports from other hardware help most, AMD and Intel graphics cards, AMD
processors, laptops and Windows 10 above all. If something goes wrong, click **Report a problem** in the
launcher: it saves a zip with the logs and a summary of your PC to your Desktop and opens a
[new issue](https://github.com/exploitz/KytyPS5/issues/new/choose); attach the zip. The zip leaves your Windows
user name out of folder paths (`C:\Users\<user>`). Please report problems with this fork here, not to chen or to the
KytyPS5 project.

## Building from source

The release is built for Windows x64 with the scripts in `tools/release`:

1. Install once: Git; Visual Studio 2022 or newer with the **Desktop development with C++** workload (its
   **C++ CMake tools** and **C++ Clang tools** components); the Vulkan SDK (`winget install
   KhronosGroup.VulkanSDK`); Python 3; and for the launcher, Qt 6.8 for MSVC x64 (for example
   `pip install aqtinstall`, then `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt`).
2. Clone with submodules: `git clone --recurse-submodules https://github.com/exploitz/KytyPS5.git`.
3. Build:
   - `tools\release\build-release.cmd`: the emulator and the precompile program for any AVX2 CPU
     (`-march=x86-64-v3`), without NVIDIA's SDKs, with the repository's PGO profile, in `_Build\windows-release`.
   - `tools\release\build-launcher.cmd`: the launcher and its Qt files, in `_Build\fork-launcher\deploy`
     (`KYTY_QT_DIR` if Qt is not in `C:\Qt\6.8.3\msvc2022_64`).
   - `powershell -File tools\release\package-release.ps1 -Version <version>`: the release folder and zip in
     `_Build\package`, with every license. It needs the FidelityFX and FSR 4 run-time files in
     `_Build\windows\deps\fsr` and `deps\fsr4` (not part of the repository).
4. For development, `build-windows.cmd` builds `_Build\windows\kyty_emulator.exe` for the CPU it is built on
   (`.\build-windows.cmd all` for every target), and `run-windows.ps1 -Game <folder>` starts it.
   Regression tests: `cmake --build _Build/windows --target kyty_tests`, then
   `ctest --test-dir _Build/windows --output-on-failure`.

## How this fork was made

This fork was developed with heavy use of AI assistance (Anthropic's Claude), directed and play-tested by its
maintainer. Every performance change was measured on real hardware with alternating A/B runs before it was
kept, and changes that did not measurably help were reverted; the numbers above come from those runs. Reviews,
bug reports and pull requests are welcome.

## Credits

- **[chenxiao07](https://github.com/chenxiao07/KytyPS5)**: the Demon's Souls branch this fork builds on, and
  nearly all of the emulator's Demon's Souls performance work.
- **[The KytyPS5 developers](https://github.com/KytyPS5/KytyPS5)**: the emulator itself.
- **[InoriRus](https://github.com/InoriRus/Kyty)**: the original Kyty, which KytyPS5 is based on.
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)**: credited by KytyPS5 as a reference for the memory
  model and the AVPlayer implementation; its FFmpeg build is used here.
- **AMD FidelityFX SDK** (MIT): FSR 3.1 frame generation. **FSR-Vulkan** (MIT): the Vulkan provider for the
  experimental FSR 4 option. **Qt** (LGPL-3.0): the launcher.
- The other third-party components and their licenses are in `3rdparty` and, in a release, `LICENSES`.

## License

This fork, like KytyPS5, is licensed under the [GNU General Public License version 2](LICENSE)
(`GPL-2.0-only`). The source code for every release is in this repository.

KytyPS5 is based on the original [Kyty](https://github.com/InoriRus/Kyty), released under the MIT License; its
copyright and license notice are preserved in [`LICENSES/Kyty-MIT.txt`](LICENSES/Kyty-MIT.txt). Third-party
components remain subject to their own licenses.

## Legal

- This project includes **no game files**, no console firmware and no keys. Use only a dump made from
  **your own copy** of the game.
- The shader list files the launcher makes from your game (`seeds-*.seeds`, `hinted-*.seeds`) contain the
  game's shader code. Do not share them.
- This project is not affiliated with or endorsed by Sony Interactive Entertainment, Bluepoint Games, NVIDIA or
  AMD. PlayStation, Demon's Souls, FSR, FidelityFX, FreeSync, DLSS and G-SYNC are trademarks of their
  respective owners.
