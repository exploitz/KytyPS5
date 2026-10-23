#pragma once

#include <QString>
#include <QStringList>

// A game: its folder (eboot.bin and sce_sys) or the folder packed into a ZArchive (a .zar file, read by the emulator
// without extracting it). Its title and version come from sce_sys/param.json (a .zar's is copied out by
// kyty_shader_precompile --param).
struct GameInfo {
	QString path;
	bool    isGame     = false; // run-windows.ps1 Test-Game: eboot.bin there, or an existing .zar file
	bool    isZar      = false;
	bool    paramRead  = false;
	QString titleId;
	QString contentVersion; // "01.007.000"
	QString titleName;
	QString iconPath; // sce_sys/icon0.png, when the folder has one
	QString picPath;  // sce_sys/pic0.png

	// "PPSA01341 01.007.000", or "unknown" (run-windows.ps1's $version).
	[[nodiscard]] QString versionKey() const;
	// "PPSA01341_01.007.000": the name of this version's caches; empty when unknown.
	[[nodiscard]] QString cacheId() const;
	// "v1.07"
	[[nodiscard]] QString displayVersion() const;
	[[nodiscard]] bool    tested() const;
	// A tested version this fork did not test itself: 1.05, which chen's branch supports (its precompile hints ship);
	// this fork's additions were tested on 1.07. Shown as "Supported" instead of "Tested".
	[[nodiscard]] bool    supportedOnly() const;
	// Another region's Demon's Souls (PPSA01340, PPSA01342) in a tested version: likely to run, not tested.
	[[nodiscard]] bool    otherRegion() const;

	static const QStringList& testedVersions();
	static bool               isGamePath(const QString& path);
	// Reads the game at `path`. `tool` (kyty_shader_precompile) reads a .zar's param.json; empty: not for .zar.
	static GameInfo read(const QString& path, const QString& tool);
};
