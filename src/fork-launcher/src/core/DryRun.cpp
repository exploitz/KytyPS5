#include "core/DryRun.h"

#include "core/Caches.h"
#include "core/LaunchPlan.h"
#include "core/Precompile.h"
#include "core/SystemCheck.h"
#include "core/Version.h"
#include "platform/Platform.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace DryRun {

QString text(const Package& package) {
	SettingsStore store(package.settingsFile, true);
	store.load();
	const LauncherSettings& s = store.settings();

	LaunchInputs inputs;
	inputs.package         = package;
	inputs.settings        = s;
	inputs.config          = LaunchConfig::read(package.config);
	inputs.game            = GameInfo::read(s.game, package.tool);
	inputs.processorCount  = Platform::logicalCpuCount();
	inputs.performanceCpus = &Platform::performanceCpus;

	QStringList lines;
	lines << QStringLiteral("# KytyPS5 Launcher %1 --dry-run, %2").arg(Version::text(), QDateTime::currentDateTime().toString(Qt::ISODate));
	lines << QStringLiteral("# settings: %1%2").arg(QDir::toNativeSeparators(package.settingsFile),
	                                                 store.wasReset() ? QStringLiteral(" (unreadable: the defaults)") : QString());
	lines << QStringLiteral("#   output=%1 quality=%2 aa=%3 framegen=%4 screen=%5 aspect=%6 fps=%7 vsync=%8 hdr=%9")
	             .arg(s.output, s.quality, s.aa, s.framegen, s.screen, s.aspect ? QStringLiteral("true") : QStringLiteral("false"))
	             .arg(s.fps)
	             .arg(s.vsync ? QStringLiteral("true") : QStringLiteral("false"), s.hdr ? QStringLiteral("true") : QStringLiteral("false"));
	lines << QStringLiteral("#   render=%1 ratio=%5 language=%2 redzone=%3 ecores=%4")
	             .arg(s.render)
	             .arg(s.language)
	             .arg(s.redzone ? QStringLiteral("true") : QStringLiteral("false"), s.ecores ? QStringLiteral("true") : QStringLiteral("false"))
	             .arg(s.ratio);
	if (!inputs.config.ok()) {
		lines << QStringLiteral("# cannot launch: %1").arg(inputs.config.error);
		return lines.join('\n') + '\n';
	}
	if (!inputs.game.isGame) lines << QStringLiteral("# cannot launch: no eboot.bin in %1 (nor a .zar archive)").arg(QDir::toNativeSeparators(s.game));
	lines << QString();

	const LaunchPlan plan = LaunchPlan::build(inputs);
	lines << plan.dryRunLines();

	lines << QString();
	lines << QStringLiteral("# What a launch does after the lines above (where run-windows.ps1 -DryRun stops):");
	lines << QStringLiteral("working directory: %1").arg(QDir::toNativeSeparators(plan.root));
	lines << QStringLiteral("environment: the launcher's, without %1, with the switches above").arg(plan.removeFromEnvironment.join(QStringLiteral(", ")));
	lines << QStringLiteral("cpu affinity: %1").arg(plan.mask != plan.all ? QStringLiteral("0x%1 on the emulator and the precompile program").arg(QString::number(plan.mask, 16).toUpper())
	                                                                      : QStringLiteral("not set (every CPU)"));
	lines << QStringLiteral("logs: %1, %2").arg(QDir::toNativeSeparators(plan.outLog()), QDir::toNativeSeparators(plan.errLog()));
	if (!inputs.game.cacheId().isEmpty()) {
		lines << QStringLiteral("caches: title-only caches (seeds.seeds, _PipelineCache\\static\\%1.*) become %2's first (Rename-TitleCaches)")
		             .arg(inputs.game.titleId, inputs.game.cacheId());
	}
	lines << QStringLiteral("shaders: seed file %1%2")
	             .arg(plan.seeds.isEmpty() ? QStringLiteral("(none yet: %1)").arg(QDir::toNativeSeparators(plan.seedsTarget)) : QDir::toNativeSeparators(plan.seeds),
	                  plan.staleSeeds ? QStringLiteral(" (made by another precompile program)") : QString());
	if (plan.listSeeds) {
		lines << QStringLiteral("  list:   %1")
		             .arg(quoteCommand(plan.tool, {QStringLiteral("--game"), plan.game, QStringLiteral("--make-seeds"), QDir::toNativeSeparators(plan.seedsMade),
		                                           QStringLiteral("--states"), QDir::toNativeSeparators(plan.passStates)}));
	}
	LaunchPlan with_seeds = plan;
	if (with_seeds.seeds.isEmpty()) with_seeds.seeds = plan.seedsTarget;
	const bool shaders = (!plan.seeds.isEmpty() || plan.listSeeds) && QFileInfo::exists(plan.tool);
	if (shaders) {
		lines << QStringLiteral("  status: %1").arg(quoteCommand(plan.tool, with_seeds.toolArguments({QStringLiteral("--status")})));
		lines << QStringLiteral("  inputs: %1   (when --status says the inputs are not current)")
		             .arg(quoteCommand(plan.tool, with_seeds.toolArguments({QStringLiteral("--no-pipelines"), QStringLiteral("--static-inputs"),
		                                                                    QStringLiteral("--threads"), QString::number(plan.cpus)})));
	} else {
		lines << QStringLiteral("  (no seed file and no way to list one: the game compiles its shaders when they first appear)");
	}
	const Platform::Memory memory = Platform::memory();
	lines << QStringLiteral("memory: %1 of RAM, %2").arg(formatGb(memory.ram), memory.commitKnown ? formatGb(memory.commitFree) + QStringLiteral(" more can be committed")
	                                                                                             : QStringLiteral("no commit limit"));
	for (const QString& message: memoryMessages(memory.ram, memory.commitFree, memory.commitKnown)) lines << QStringLiteral("memory:   ") + message;
	lines << QStringLiteral("start: %1").arg(plan.commandLine());

	lines << QString();
	const quint64 precompile_mask = PrecompileSetup::cpuMask(s.ecores, Platform::logicalCpuCount());
	lines << QStringLiteral("# The precompile (precompile-windows.ps1%1):")
	             .arg(precompile_mask == 0 ? QString()
	                                       : QStringLiteral(" -Affinity 0x%1: %2")
	                                             .arg(QString::number(precompile_mask, 16).toUpper(),
	                                                  s.ecores ? QStringLiteral("Precompile on E-cores") : QStringLiteral("the performance cores")));
	const PrecompileSetup setup = PrecompileSetup::make(package, inputs.game, inputs.config, precompile_mask, Platform::logicalCpuCount());
	lines << setup.describe();
	return lines.join('\n') + '\n';
}

} // namespace DryRun
