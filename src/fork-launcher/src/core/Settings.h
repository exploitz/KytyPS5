#pragma once

#include <QString>

// launcher-settings.json next to the launcher (the PowerShell launcher's file, same keys and values).
struct LauncherSettings {
	QString game;
	bool    setupDone = false;
	QString output    = QStringLiteral("4k");       // 4k | 1080q | 1080f
	QString quality   = QStringLiteral("standard"); // standard | shadows | low | lowest
	QString aa        = QStringLiteral("taa");      // taa | fsr4 | dlss
	QString framegen  = QStringLiteral("off");      // off | fsr
	QString screen    = QStringLiteral("fullscreen"); // fullscreen | <width>x<height>
	bool    aspect    = true;                       // keep 16:9 (fullscreen only)
	int     fps       = 60;                         // 60 | 120
	bool    vsync     = false;
	bool    hdr       = false;
	int     render    = 1440;                       // 3D height with the 4K output: 1440 | 1296 | 1152 | 1080 | 900
	QString ratio     = QStringLiteral("16:9");     // 3D aspect ratio with the 4K output: 16:9 | 21:9 | 32:9
	int     language  = 1;                          // 0-29, the PS5's console languages
	bool    redzone   = true;
	bool    ecores    = false;
	int     hud       = 0;                          // performance overlay: 0 off, 1 frame rate, 2 detailed (KYTY_FPS_HUD)
	// The window's place and size (client area, logical pixels); a width of 0: none saved.
	int     windowX   = 0;
	int     windowY   = 0;
	int     windowW   = 0;
	int     windowH   = 0;

	bool operator==(const LauncherSettings&) const = default;
};

class SettingsStore {
public:
	explicit SettingsStore(QString path, bool readOnly = false);

	// Reads the file; a corrupt one is renamed to <file>.bad and the defaults are used (wasReset() tells).
	void load();
	// Writes the file (not in read-only mode: --screenshots). False when it could not be written.
	bool save();

	[[nodiscard]] LauncherSettings&       settings() { return m_settings; }
	[[nodiscard]] const LauncherSettings& settings() const { return m_settings; }
	[[nodiscard]] bool                    wasReset() const { return m_reset; }
	[[nodiscard]] bool                    readOnly() const { return m_read_only; }
	[[nodiscard]] const QString&          path() const { return m_path; }

	// The values each setting takes (the first is the default).
	static bool valid(const LauncherSettings& settings);

private:
	QString          m_path;
	bool             m_read_only = false;
	bool             m_reset     = false;
	LauncherSettings m_settings;
};
