#pragma once

// A play launch as run-windows.ps1 makes it, in C++: the emulator's options and switches for the launcher's settings,
// the CPUs, the shader steps before the start and the logs. Pure computation (files are only read); PlayRunner runs it.
//
// The launcher's settings map to run-windows.ps1's parameters as the PowerShell launcher passed them
// (launcher.ps1 Get-PlayCommand): -Language, -Output, -Fullscreen [-AspectFit] or -Width/-Height, -Quality,
// -Upscaler, -FsrFrameGen, -Fps120, -PresentMode Fifo (VSync), -Hdr, -RenderHeight and -Aspect (4K output), -NoRedZone; the
// launcher's own: DLSS (KYTY_DLSS) and the performance overlay (KYTY_FPS_HUD). Not ported: -Baseline, -NoAot and the SRT
// AOT library (a config's KYTY_SRT_AOT_LIBRARY is dropped, as with -NoAot), -Prompt (the launcher's own dialogs),
// -DryRun (the launcher's --dry-run), -Set, -Patch, -Affinity, -FrameGen, -StreamlineDir, -Precompile.

#include "core/GameInfo.h"
#include "core/LaunchConfig.h"
#include "core/OrderedEnvironment.h"
#include "core/Package.h"
#include "core/Settings.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

struct LaunchInputs {
	Package          package;
	LauncherSettings settings;
	LaunchConfig     config;
	GameInfo         game;
	int              processorCount = 1;
	// Get-PerformanceCpus (Platform::performanceCpus; replaceable in tests).
	std::function<QString(quint64)> performanceCpus;
};

struct LaunchPlan {
	QString            root;
	QString            exe;
	QString            game;
	QString            configPath;
	QStringList        options;
	OrderedEnvironment environment;
	QStringList        removeFromEnvironment; // variables taken out of the inherited environment
	quint64            mask = 0;
	quint64            all  = 0;
	int                cpus = 0;
	QString            titleId;
	QString            titleName;
	QString            version; // "PPSA01341 01.007.000" or "unknown"
	bool               tested = false;

	QString logDir;
	QString stamp; // yyyyMMdd-HHmmss-fff: the logs <stamp>.out.log and <stamp>.err.log

	// The shader steps before the start (the static precompile's seed file and its --status).
	QString tool;
	QString seedName;
	QString seeds;        // the seed file found (empty: none yet)
	QString passStates;
	QString programStamp; // of the precompile program (empty: no program)
	bool    staleSeeds = false;
	bool    listSeeds  = false; // run --make-seeds first
	QString seedsTarget;
	QString seedsMade;          // the target, or <target>.new when a stale file is listed again

	// --game <game> <options>
	[[nodiscard]] QStringList arguments() const;
	// The command line as run-windows.ps1 prints it (quoted where needed).
	[[nodiscard]] QString     commandLine() const;
	// run-windows.ps1 -DryRun's output: game, config, affinity, switches, command, then each switch.
	[[nodiscard]] QStringList dryRunLines() const;
	// The emulator's environment: `base` without removeFromEnvironment, with the switches.
	[[nodiscard]] QProcessEnvironment processEnvironment(const QProcessEnvironment& base) const;
	// --game <game> --seeds <seeds> <extra>: the precompile program's arguments (Invoke-Tool).
	[[nodiscard]] QStringList toolArguments(const QStringList& extra) const;
	[[nodiscard]] QString     outLog() const;
	[[nodiscard]] QString     errLog() const;

	static LaunchPlan build(const LaunchInputs& inputs);
};

// run-windows.ps1's memory messages ("memory:" lines) for this PC's RAM and commit limit.
QStringList memoryMessages(double ram, double commitFree, bool commitKnown);

// The command line of a program and its arguments as PowerShell would show it (arguments with spaces quoted).
QString quoteCommand(const QString& program, const QStringList& arguments);
