#pragma once

#include <QString>
#include <QStringList>

// The release package the launcher sits in (or a build tree): where the emulator, the precompile program, the
// launch config, the logs and the settings are.
struct Package {
	QString root;          // the launcher's folder ($PSScriptRoot of the scripts)
	bool    buildTree = false; // root\_Build exists: the logs go to _Build\run-logs, the seeds to _Build\static-precompile
	QString emulator;      // kyty_emulator.exe next to the launcher, else _Build\windows\kyty_emulator.exe
	QString tool;          // kyty_shader_precompile.exe next to the emulator
	QString config;        // launch.json, else run-windows.json (empty: none)
	QString passStates;    // tools\local\static-precompile\pass-states.json
	QString logDir;        // _Build\run-logs in a build tree, else logs
	QString settingsFile;  // launcher-settings.json
	QString dryRunFile;    // dry-run.txt

	[[nodiscard]] QString path(const QString& relative) const;
	// The files a release needs that are missing (relative paths): an antivirus or a partial unzip removed them.
	[[nodiscard]] QStringList missingFiles() const;
	// The DLSS add-on's files (plugins/dlss).
	[[nodiscard]] bool hasDlssAddon() const;
	[[nodiscard]] bool hasFsr4() const;
	[[nodiscard]] bool hasFsrFrameGen() const;
	// README.md (a release), else docs\PORTABLE-README.md (a build tree); empty: none.
	[[nodiscard]] QString readme() const;

	static Package detect(const QString& root);
};
