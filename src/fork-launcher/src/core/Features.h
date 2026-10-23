#pragma once

// Switches for what this release of the launcher offers.

// The DLSS add-on (plugins/dlss): the emulator of this release cannot load it yet, so "DLSS (add-on)" is not offered and
// a saved "dlss" reads as the game's TAA. The download and install code (DlssAddon, MainWindow::ensureDlss) stays.
inline constexpr bool kDlssAddonSupported = false;
