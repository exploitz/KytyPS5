#include "core/LaunchPlan.h"

#include "core/Caches.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>

#include <bit>

namespace {

QString Native(const QString& path) {
	return QDir::toNativeSeparators(path);
}

// "$_ -match '\s'": quoted.
QString QuoteArgument(const QString& argument) {
	static const QRegularExpression space(QStringLiteral("\\s"));
	return argument.contains(space) ? QStringLiteral("\"%1\"").arg(argument) : argument;
}

QString FormatGb(double bytes) {
	return QLocale(QLocale::English).toString(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 0);
}

} // namespace

QString quoteCommand(const QString& program, const QStringList& arguments) {
	QStringList quoted;
	for (const QString& argument: arguments) quoted.append(QuoteArgument(argument));
	return QStringLiteral("%1 %2").arg(QuoteArgument(Native(program)), quoted.join(' '));
}

QStringList LaunchPlan::arguments() const {
	return QStringList {QStringLiteral("--game"), game} + options;
}

QString LaunchPlan::commandLine() const {
	// $quoted = @('--game', "`"$Game`"") + ($options | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } })
	QStringList quoted {QStringLiteral("--game"), QStringLiteral("\"%1\"").arg(game)};
	for (const QString& option: options) quoted.append(QuoteArgument(option));
	return QStringLiteral("%1 %2").arg(Native(exe), quoted.join(' '));
}

QStringList LaunchPlan::dryRunLines() const {
	QStringList lines;
	QString     game_line = QStringLiteral("game:     ");
	if (!titleName.isEmpty()) game_line += titleName + QStringLiteral(", ");
	game_line += version;
	if (!tested) {
		game_line += QStringLiteral(" (untested: the emulator supports %1)").arg(GameInfo::testedVersions().join(QStringLiteral(" and ")));
	}
	lines << game_line;
	lines << QStringLiteral("config:   %1").arg(Native(configPath));
	QString affinity = QStringLiteral("affinity: 0x%1 (%2 CPUs)").arg(QString::number(mask, 16).toUpper()).arg(cpus);
	if (!environment.value(QStringLiteral("KYTY_RENDER_CPUS")).isEmpty()) {
		affinity += QStringLiteral("; render threads on CPUs %1").arg(environment.value(QStringLiteral("KYTY_RENDER_CPUS")));
	}
	lines << affinity;
	lines << QStringLiteral("switches: %1").arg(environment.count());
	lines << QStringLiteral("command:  %1").arg(commandLine());
	for (const auto& entry: environment.entries()) lines << QStringLiteral("  %1=%2").arg(entry.first, entry.second);
	return lines;
}

QProcessEnvironment LaunchPlan::processEnvironment(const QProcessEnvironment& base) const {
	QProcessEnvironment result = base;
	for (const QString& name: removeFromEnvironment) result.remove(name);
	for (const auto& entry: environment.entries()) {
		// (Set-Item env:X '' removes X.)
		if (entry.second.isEmpty()) result.remove(entry.first);
		else result.insert(entry.first, entry.second);
	}
	return result;
}

QStringList LaunchPlan::toolArguments(const QStringList& extra) const {
	return QStringList {QStringLiteral("--game"), game, QStringLiteral("--seeds"), Native(seeds)} + extra;
}

QString LaunchPlan::outLog() const {
	return QDir(logDir).filePath(stamp + QStringLiteral(".out.log"));
}

QString LaunchPlan::errLog() const {
	return QDir(logDir).filePath(stamp + QStringLiteral(".err.log"));
}

LaunchPlan LaunchPlan::build(const LaunchInputs& in) {
	LaunchPlan              plan;
	const LauncherSettings& s       = in.settings;
	const Package&          package = in.package;
	plan.root                       = package.root;
	plan.exe                        = Native(package.emulator);
	plan.tool                       = Native(package.tool);
	plan.game                       = Native(s.game);
	plan.configPath                 = Native(in.config.path);
	plan.titleId                    = in.game.paramRead ? in.game.titleId : QString();
	plan.titleName                  = in.game.paramRead ? in.game.titleName : QString();
	plan.version                    = in.game.versionKey();
	plan.tested                     = in.game.tested();

	// --- The settings as run-windows.ps1 parameters (launcher.ps1 Get-PlayCommand) ---------------------------------
	int  width = 0, height = 0;
	bool fullscreen = false, aspect_fit = false;
	if (s.screen == QStringLiteral("fullscreen")) {
		fullscreen = true;
		aspect_fit = s.aspect;
	} else {
		const QStringList size = s.screen.split('x');
		width                  = size.value(0).toInt();
		height                 = size.value(1).toInt();
	}
	const QString present_mode  = s.vsync ? QStringLiteral("Fifo") : QString();
	const int     language      = s.language;
	const bool    fps120        = s.fps == 120;
	// (Passed as chosen, 1440 too: without it the emulator takes the height from the window, run-windows.ps1.)
	const int     render_height = s.output == QStringLiteral("4k") ? s.render : 0;
	const QString aspect_ratio  = s.output == QStringLiteral("4k") && s.ratio != QStringLiteral("16:9") ? s.ratio : QString();
	const QString quality       = s.quality != QStringLiteral("standard") ? s.quality : QString();
	const QString output        = s.output;
	const QString upscaler      = s.aa != QStringLiteral("taa") ? s.aa : QString();
	const bool    fsr_frame_gen = s.framegen == QStringLiteral("fsr");
	const bool    no_red_zone   = !s.redzone;

	// --- Emulator options: everything after the binary in the Linux command, minus --game ---------------------------
	const QStringList& command = in.config.command;
	const int          start   = static_cast<int>(command.indexOf(QStringLiteral("--"))) + 2;
	for (int i = start; i < command.size(); i++) {
		if (command.at(i).compare(QStringLiteral("--game"), Qt::CaseInsensitive) == 0) {
			i++;
			continue;
		}
		plan.options.append(command.at(i));
	}
	auto set_option = [&](const QString& name, const QString& value) {
		const qsizetype at = plan.options.indexOf(name);
		if (at >= 0 && at + 1 < plan.options.size()) plan.options[at + 1] = value;
		else if (at >= 0) plan.options.append(value);
		else plan.options << name << value;
	};
	if (width > 0) set_option(QStringLiteral("--screen-width"), QString::number(width));
	if (height > 0) set_option(QStringLiteral("--screen-height"), QString::number(height));
	if (fullscreen) plan.options.append(QStringLiteral("--fullscreen"));
	if (s.hdr) plan.options.append(QStringLiteral("--hdr"));
	if (!present_mode.isEmpty()) set_option(QStringLiteral("--present-mode"), present_mode);
	if (language >= 0) set_option(QStringLiteral("--console-language"), QString::number(language));
	// The flip queue at the console's 60 Hz caps the game at 60 fps; -Fps120: a 120 Hz vblank.
	set_option(QStringLiteral("--vblank-frequency"), fps120 ? QStringLiteral("120") : QStringLiteral("60"));
	// Windows dispatches exceptions over the guest's SysV red zone: --redzone rewrites its accesses at load time.
	if (!no_red_zone) plan.options.append(QStringLiteral("--redzone"));

	// --- Environment switches: the config's (its SRT AOT library left out, as with -NoAot) --------------------------
	OrderedEnvironment& env = plan.environment;
	for (const auto& entry: in.config.environment) {
		if (entry.first.compare(QStringLiteral("KYTY_SRT_AOT_LIBRARY"), Qt::CaseInsensitive) == 0) continue;
		env.set(entry.first, entry.second);
	}

	// --- CPU affinity: the config's list, else every CPU; only CPUs this PC has --------------------------------------
	const int n = std::max(1, in.processorCount);
	plan.all    = n >= 64 ? ~quint64(0) : (quint64(1) << n) - 1;
	quint64 mask = 0;
	for (int cpu: in.config.cpuAffinity) mask |= quint64(1) << (static_cast<unsigned>(cpu) & 63u);
	mask &= plan.all;
	if (mask == 0) mask = plan.all;
	plan.mask = mask;
	plan.cpus = std::popcount(mask);

	auto is = [&](const QString& key, const QString& value) {
		return env.contains(key) && env.value(key).compare(value, Qt::CaseInsensitive) == 0;
	};
	// The emulator's own defaults are the release settings (run-windows.ps1 sets no performance switches any more:
	// its -Set KEY=VALUE is the way to change one). The recording worker's CPUs: 'auto' picks this PC's performance
	// cores (below); the render thread on the same ones.
	env.setIfMissing(QStringLiteral("KYTY_RECORDING_CPUS"), QStringLiteral("auto"));
	if (!env.contains(QStringLiteral("KYTY_RENDER_CPUS"))) env.set(QStringLiteral("KYTY_RENDER_CPUS"), env.value(QStringLiteral("KYTY_RECORDING_CPUS")));
	// Demon's Souls' own engine settings: the emulator adds its defaults (no XPR index-buffer culling, the 4 ms least
	// game step above 60 Hz) to KYTY_GAME_CONVARS itself (src/loader/gameArgs.cpp); every region (PPSA01340-PPSA01342).
	static const QRegularExpression demons_souls(QStringLiteral("^PPSA0134[0-2]$"), QRegularExpression::CaseInsensitiveOption);
	const bool                      is_demons_souls = demons_souls.match(plan.titleId).hasMatch();
	// -RenderHeight: the height Demon's Souls renders its 3D at (its 4K output and UI stay).
	if (render_height > 0) env.set(QStringLiteral("KYTY_RENDER_HEIGHT"), QString::number(render_height));
	// -Aspect: the 3D wider (21:9, 32:9), the game's projection widened to match.
	if (!aspect_ratio.isEmpty()) env.set(QStringLiteral("KYTY_RENDER_ASPECT"), aspect_ratio);
	// -Quality: engine settings that cut what a frame draws.
	if (!quality.isEmpty()) {
		// (No r_forceShadowLOD: it draws objects with their shadow meshes and objects without one vanish: 1-1's dead
		// trees and cart, the Nexus's runes, the Gates' barricades.)
		static const QHash<QString, QString> convars = {
		    {QStringLiteral("shadows"), QStringLiteral("sunShadowMaxDistance=60;r_enableAmbientShadowing=false;r_enableSpotShadows=false")},
		    {QStringLiteral("low"), QStringLiteral("r_forceViewportHeightForPixelSizeCalc=720;sunShadowMaxDistance=125")},
		    {QStringLiteral("lowest"), QStringLiteral("r_forceViewportHeightForPixelSizeCalc=480;sunShadowMaxDistance=60;"
		                                              "r_enableAmbientShadowing=false;r_enableSpotShadows=false")}};
		if (is_demons_souls && convars.contains(quality)) {
			QStringList parts;
			if (!env.value(QStringLiteral("KYTY_GAME_CONVARS")).isEmpty()) parts << env.value(QStringLiteral("KYTY_GAME_CONVARS"));
			parts << convars.value(quality);
			env.set(QStringLiteral("KYTY_GAME_CONVARS"), parts.join(';'));
		}
	}
	// Decals off: the game's projected decals (blood, moss, grime: ~1450 of ~9600 draws a frame at Boletaria 1-1,
	// +8% there, +1% at Latria) are not drawn.
	if (!s.decals && is_demons_souls) {
		QStringList parts;
		if (!env.value(QStringLiteral("KYTY_GAME_CONVARS")).isEmpty()) parts << env.value(QStringLiteral("KYTY_GAME_CONVARS"));
		parts << QStringLiteral("r_projectedDecalEnable=0");
		env.set(QStringLiteral("KYTY_GAME_CONVARS"), parts.join(';'));
	}
	// -TextureReduce: the launcher's Low / Lowest pass 1 / 2 (large textures without their largest 1 / 2 levels; without
	// it the emulator takes 1 below 12 GB of video memory, else 0).
	if (quality == QStringLiteral("low")) env.set(QStringLiteral("KYTY_TEXTURE_REDUCE"), QStringLiteral("1"));
	else if (quality == QStringLiteral("lowest")) env.set(QStringLiteral("KYTY_TEXTURE_REDUCE"), QStringLiteral("2"));
	// -HdOutput: Demon's Souls' own 1080p mode (its post-processing and HUD at 1920x1080, its 3D at most 1080 high;
	// src/loader/demonsSoulsHdOutput.h). The two 1080p outputs of earlier releases both take it.
	if (output == QStringLiteral("1080q") || output == QStringLiteral("1080f")) env.set(QStringLiteral("KYTY_HD_OUTPUT"), QStringLiteral("1"));
	// -Upscaler dlss: DLSS in place of Sony's upscaler on the game's PS5 Pro path (src/loader/demonsSoulsSuperResolution.h;
	// NVIDIA's Streamline runtime next to the emulator or KYTY_STREAMLINE_DIR). (FSR 4 of earlier releases: not in this
	// emulator yet; the game's TAA then.)
	if (upscaler == QStringLiteral("dlss")) env.set(QStringLiteral("KYTY_UPSCALER"), QStringLiteral("dlss"));
	// -Fsr: AMD FSR 3.1 frame generation (one generated frame, any GPU; amd_fidelityfx_vk.dll next to the emulator).
	if (fsr_frame_gen) {
		env.set(QStringLiteral("KYTY_FRAMEGEN"), QStringLiteral("1"));
		env.set(QStringLiteral("KYTY_FRAMEGEN_BACKEND"), QStringLiteral("fsr"));
	}
	// Not a precompile: the game must not exit after its warmup.
	plan.removeFromEnvironment << QStringLiteral("KYTY_SHADER_WARMUP_ONLY");
	// The start-up warmup's threads: 'auto' leaves two of the launch's CPUs to the game and the system, at most 16.
	if (is(QStringLiteral("KYTY_SHADER_WARMUP_THREADS"), QStringLiteral("auto"))) {
		env.set(QStringLiteral("KYTY_SHADER_WARMUP_THREADS"), QString::number(std::max(1, std::min(16, plan.cpus - 2))));
	}
	if (aspect_fit) env.set(QStringLiteral("KYTY_PRESENT_ASPECT"), QStringLiteral("fit"));
	// The performance overlay (the launcher's own setting; run-windows.ps1 has no parameter for it).
	if (s.hud == 1 || s.hud == 2) env.set(QStringLiteral("KYTY_FPS_HUD"), QString::number(s.hud));
	// (Above 60 Hz the emulator itself adds gameMinDeltaTime=0.004, the 4 ms least game step that keeps Demon's Souls
	// in real time: src/loader/gameArgs.cpp.)
	// 'auto' recording and render CPUs: this PC's performance cores without CPU 0's core (none: left to the OS).
	const QString recording = QStringLiteral("KYTY_RECORDING_CPUS"), render = QStringLiteral("KYTY_RENDER_CPUS");
	if (is(recording, QStringLiteral("auto")) || is(render, QStringLiteral("auto"))) {
		const QString performance = in.performanceCpus ? in.performanceCpus(mask) : QString();
		for (const QString& key: {recording, render}) {
			if (!is(key, QStringLiteral("auto"))) continue;
			if (!performance.isEmpty()) env.set(key, performance);
			else env.remove(key);
		}
	}

	// --- Logs ----------------------------------------------------------------------------------------------------------
	plan.logDir = package.logDir;
	plan.stamp  = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));

	// --- Shaders: the seed file of this game version, listed from the game's files when there is none ------------------
	const QString id = in.game.cacheId();
	plan.seedName    = id.isEmpty() ? QStringLiteral("seeds.seeds") : QStringLiteral("seeds-%1.seeds").arg(id);
	for (const QString& candidate: {package.path(plan.seedName), package.path(QStringLiteral("_Build/static-precompile/") + plan.seedName)}) {
		if (QFileInfo::exists(candidate)) {
			plan.seeds = candidate;
			break;
		}
	}
	plan.passStates   = package.passStates;
	const bool tool   = QFileInfo::exists(package.tool);
	plan.programStamp = tool ? Caches::programStamp(package.tool) : QString();
	plan.staleSeeds   = !plan.seeds.isEmpty() && !plan.programStamp.isEmpty() && Caches::readProgramStamp(plan.seeds) != plan.programStamp;
	plan.listSeeds    = (plan.seeds.isEmpty() || plan.staleSeeds) && tool && QFileInfo::exists(plan.passStates);
	plan.seedsTarget  = !plan.seeds.isEmpty() ? plan.seeds
	                    : package.buildTree  ? package.path(QStringLiteral("_Build/static-precompile/") + plan.seedName)
	                                         : package.path(plan.seedName);
	plan.seedsMade    = plan.staleSeeds ? plan.seedsTarget + QStringLiteral(".new") : plan.seedsTarget;
	return plan;
}

QStringList memoryMessages(double ram, double commitFree, bool commitKnown) {
	constexpr double GB = 1024.0 * 1024.0 * 1024.0;
	QStringList      messages;
	if (ram < 24 * GB) {
		messages << QStringLiteral("This PC has %1 GB of RAM: the game runs, but loading takes longer (the emulator takes about 14 GB "
		                           "while a save loads and keeps using a few GB in play; 32 GB recommended).")
		                .arg(FormatGb(ram));
	}
	if (commitKnown && commitFree < 34 * GB) {
		messages << QStringLiteral("Windows can give programs only %1 GB more memory (RAM plus page file) and the emulator needs about 34 GB: "
		                           "it may close in the middle of the game. Close other programs, or set the page file to 'System managed size' "
		                           "(Settings > System > About > Advanced system settings > Performance > Settings > Advanced > Virtual memory).")
		                .arg(FormatGb(commitFree));
	}
	return messages;
}
