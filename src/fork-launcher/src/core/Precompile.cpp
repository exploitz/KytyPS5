#include "core/Precompile.h"

#include "core/Caches.h"
#include "core/LaunchPlan.h"
#include "platform/Platform.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <bit>
#include <cmath>

namespace {

QString Native(const QString& path) {
	return QDir::toNativeSeparators(path);
}

QString Hms(qint64 ms) {
	const qint64 s = ms / 1000;
	return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QChar(u'0')).arg(s / 60 % 60, 2, 10, QChar(u'0')).arg(s % 60, 2, 10, QChar(u'0'));
}

QString Count(qint64 n) {
	return QLocale(QLocale::English).toString(n);
}

// The switches of the launch config that change what the shader translator makes (pipelineCache.cpp StoreSwitches).
const QStringList kTranslationSwitches = {QStringLiteral("KYTY_EXEC_DEMAND"), QStringLiteral("KYTY_FRONT_FACE_FLOAT"),
                                          QStringLiteral("KYTY_PORTABLE_SHADERS"), QStringLiteral("KYTY_LANE_LOCAL"),
                                          QStringLiteral("KYTY_LANE_LOCAL_TITLE"), QStringLiteral("KYTY_LANE_LOCAL_UNIFORM_LOADS"),
                                          QStringLiteral("KYTY_PS_WAVE_OR")};

// Time the final merge takes after the last shard (a minute or two on 22 CPUs).
constexpr qint64 kFinishMs = 90 * 1000;

} // namespace

int Precompile::estimateMinutes(int cpus) {
	// 10-15, the release package in a new folder on a Core i9-12900K (its 16 P-core threads), RTX 3080 Ti: 98,982
	// pipelines, 61 min, the shaders of recorded play ready after 32 (chen's PC: about 40 min). A rough guide by thread
	// count; the time left once it runs comes from its progress.
	return static_cast<int>(std::ceil(61.0 * 16.0 / std::max(1, cpus) / 10.0)) * 10;
}

quint64 PrecompileSetup::cpuMask(bool ecores, int processorCount) {
	const quint64 efficiency = Platform::efficiencyMask();
	if (efficiency == 0) return 0;
	if (ecores) return efficiency;
	// 10-15, i9-12900K (P-core threads 0-15, E-cores 16-23), the same 24 shards each way: 18.4 pipelines/s on the P-cores
	// (5 at a time; 17.5 with 8 x 2 threads) against 8.0 on all 24 threads (8 x 3; 8.8 with 6). With E-cores in the mask
	// the compilers that land there hold the run up: the full precompile took 154 min, 61 on the P-cores.
	const int     n   = std::max(1, processorCount);
	const quint64 all = n >= 64 ? ~quint64(0) : (quint64(1) << n) - 1;
	const quint64 performance = all & ~efficiency;
	return performance != 0 ? performance : 0;
}

PrecompileSetup PrecompileSetup::make(const Package& package, const GameInfo& game, const LaunchConfig& config, quint64 affinity,
                                      int processorCount) {
	PrecompileSetup s;
	s.root       = package.root;
	s.tool       = package.tool;
	s.game       = Native(game.path);
	s.passStates = package.passStates;
	if (!QFileInfo::exists(package.tool)) {
		s.error = QStringLiteral("missing %1").arg(Native(package.tool));
		return s;
	}
	if (!game.paramRead || game.cacheId().isEmpty()) {
		s.error = QStringLiteral("no sce_sys\\param.json in %1").arg(Native(game.path));
		return s;
	}
	s.cacheId = game.cacheId();
	// seeds-<title>_<version>.seeds next to the launcher (a release) or in _Build\static-precompile.
	const QString name = QStringLiteral("seeds-%1.seeds").arg(s.cacheId);
	s.seeds = QFileInfo::exists(package.path(name)) || !package.buildTree ? package.path(name)
	                                                                       : package.path(QStringLiteral("_Build/static-precompile/") + name);
	// One another program made (<file>.program) is made again, and replaced where it lists something else.
	s.programStamp = Caches::programStamp(package.tool);
	s.remakeSeeds  = QFileInfo::exists(s.seeds) && Caches::readProgramStamp(s.seeds) != s.programStamp;
	s.makeSeeds    = !QFileInfo::exists(s.seeds) || s.remakeSeeds;
	// The specializations of recorded play, all of it (precompile-windows.ps1 since chen's dc3bca16): the release's hints
	// completed with the seed file's code, this PC's warmup recordings and a recorded seed file next to the seed file.
	// One of them alone left the others out (a laptop's recorded seed file kept the hints of a pipeline its play then met).
	const QDir seeds_dir = QFileInfo(s.seeds).absoluteDir();
	s.recorded           = seeds_dir.filePath(QStringLiteral("recorded-%1.seeds").arg(s.cacheId));
	s.released           = seeds_dir.filePath(QStringLiteral("released-%1.seeds").arg(s.cacheId));
	s.together           = seeds_dir.filePath(QStringLiteral("recorded-play-%1.hints").arg(s.cacheId));
	s.hinted             = seeds_dir.filePath(QStringLiteral("hinted-%1.seeds").arg(s.cacheId));
	const QDir warmup(package.path(QStringLiteral("_PipelineCache/warmup-v2")));
	for (const QString& dir: warmup.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		const QString file = warmup.filePath(dir + QStringLiteral("/%1.shaders").arg(s.cacheId));
		if (QFileInfo::exists(file)) s.recordings << file;
	}
	s.hints = package.path(QStringLiteral("tools/local/static-precompile/hints-%1.hints").arg(s.cacheId));
	// Another region's Demon's Souls of the same version shares its shaders: its hints (PPSA01341's first).
	static const QRegularExpression demons_souls(QStringLiteral("^PPSA0134[0-2]$"));
	if (!QFileInfo::exists(s.hints) && demons_souls.match(game.titleId).hasMatch()) {
		const QDir  hints_dir(package.path(QStringLiteral("tools/local/static-precompile")));
		QStringList region = hints_dir.entryList({QStringLiteral("hints-PPSA0134?_%1.hints").arg(game.contentVersion)}, QDir::Files, QDir::Name);
		std::stable_sort(region.begin(), region.end(), [](const QString& a, const QString& b) {
			return a.startsWith(QStringLiteral("hints-PPSA01341_")) && !b.startsWith(QStringLiteral("hints-PPSA01341_"));
		});
		if (!region.isEmpty()) s.hints = hints_dir.filePath(region.first());
	}
	s.hasHints = QFileInfo::exists(s.hints);
	// The CPUs: -Affinity, else the launch config's list, else all.
	quint64 mask = affinity;
	if (mask == 0) {
		for (int cpu: config.cpuAffinity) mask |= quint64(1) << (static_cast<unsigned>(cpu) & 63u);
	}
	const int n = std::max(1, processorCount);
	s.all       = n >= 64 ? ~quint64(0) : (quint64(1) << n) - 1;
	s.mask      = mask & s.all;
	if (s.mask == 0) s.mask = s.all;
	s.cpus = std::popcount(s.mask);
	// A process scales to a few threads; each translates the programs its share needs.
	s.jobs = std::max(1, static_cast<int>(std::ceil(static_cast<double>(s.cpus) / s.threads)));
	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
	s.logs              = QDir(package.logDir).filePath(stamp + QStringLiteral("-precompile"));
	s.nvidiaCaches      = QDir(QDir::tempPath()).filePath(QStringLiteral("kyty-precompile-") + stamp);
	s.plan              = QDir(s.logs).filePath(QStringLiteral("plan.bin"));
	for (const QString& key: kTranslationSwitches) {
		const QString value = config.value(key);
		if (!value.isEmpty() && !qEnvironmentVariableIsSet(key.toLatin1().constData())) s.translationSwitches << key + '=' + value;
	}
	return s;
}

QStringList PrecompileSetup::describe() const {
	QStringList lines;
	if (!ok()) {
		lines << QStringLiteral("precompile: cannot start: %1").arg(error);
		return lines;
	}
	auto command = [&](const QStringList& arguments) {
		return quoteCommand(tool, QStringList {QStringLiteral("--game"), game} + arguments);
	};
	const QString log_name = Native(QDir(root).relativeFilePath(logs));
	lines << QStringLiteral("precompile: %1, %2 shards, %3 at a time, affinity 0x%4; logs %5")
	             .arg(Native(seeds))
	             .arg(shards)
	             .arg(jobs)
	             .arg(QString::number(mask, 16).toUpper(), log_name);
	lines << QStringLiteral("  working directory: %1").arg(Native(root));
	lines << QStringLiteral("  environment: %1").arg(translationSwitches.isEmpty() ? QStringLiteral("(no translation switches)")
	                                                                               : translationSwitches.join(QStringLiteral(", ")));
	lines << QStringLiteral("  each process: __GL_SHADER_DISK_CACHE_PATH=%1\\<name>, normal priority, power throttling off, affinity 0x%2")
	             .arg(Native(nvidiaCaches), QString::number(mask, 16).toUpper());
	if (makeSeeds) {
		lines << QStringLiteral("  seeds%1: %2")
		             .arg(remakeSeeds ? QStringLiteral(" (again: another program made them)") : QString(),
		                  command({QStringLiteral("--make-seeds"), Native(remakeSeeds ? seeds + QStringLiteral(".new") : seeds),
		                        QStringLiteral("--states"), Native(passStates)}));
	}
	// Recorded play: the release's hints alone, else everything gathered (the release's hints applied, the recordings, the
	// recorded seed file).
	QStringList sources;
	if (hasHints) {
		lines << QStringLiteral("  released: %1").arg(command({QStringLiteral("--seeds"), Native(seeds), QStringLiteral("--apply-hints"),
		                                                    Native(hints), QStringLiteral("--out"), Native(released)}));
		sources << released;
	}
	sources << recordings;
	if (QFileInfo::exists(recorded)) sources << recorded;
	QString recorded_file;
	if (sources.size() == 1 && hasHints) {
		recorded_file = released;
	} else if (!sources.isEmpty()) {
		QStringList gather {QStringLiteral("--seeds"), Native(seeds), QStringLiteral("--make-hints"), Native(together)};
		for (const QString& source: sources) gather << QStringLiteral("--recorded") << Native(source);
		lines << QStringLiteral("  gather: %1").arg(command(gather));
		lines << QStringLiteral("  apply: %1").arg(command({QStringLiteral("--seeds"), Native(seeds), QStringLiteral("--apply-hints"),
		                                                 Native(together), QStringLiteral("--out"), Native(hinted)}));
		recorded_file = hinted;
	}
	lines << QStringLiteral("  merge-before: %1").arg(command({QStringLiteral("--merge")}));
	const bool has_recorded = !recorded_file.isEmpty();
	if (has_recorded) {
		lines << QStringLiteral("  plan-recorded: %1")
		             .arg(command({QStringLiteral("--seeds"), Native(recorded_file), QStringLiteral("--dry-run"), QStringLiteral("--plan"),
		                        Native(plan), QStringLiteral("--threads"), QString::number(cpus)}));
	}
	QStringList plan_seeds {QStringLiteral("--seeds"), Native(seeds), QStringLiteral("--dry-run"), QStringLiteral("--plan"), Native(plan),
	                        QStringLiteral("--threads"), QString::number(cpus), QStringLiteral("--static-inputs")};
	if (has_recorded) plan_seeds << QStringLiteral("--covered-by") << Native(recorded_file);
	lines << QStringLiteral("  plan-seeds: %1   (also the shader prefetch inputs)").arg(command(plan_seeds));
	lines << QStringLiteral("  shards (with KYTY_PRECOMPILE_CLAIMS=%1\\claims):").arg(Native(nvidiaCaches));
	if (has_recorded) {
		const qint64  pipelines = QFileInfo::exists(recorded_file) ? Caches::seedPipelines(recorded_file) : -1;
		const QString count     = pipelines >= 0 ? QString::number(std::max<qint64>(1, (pipelines + 99) / 100)) : QStringLiteral("<n>");
		lines << QStringLiteral("    shard0of%1-%2: %3")
		             .arg(count, QFileInfo(recorded_file).completeBaseName(),
		                  command({QStringLiteral("--seeds"), Native(recorded_file), QStringLiteral("--shard"), QStringLiteral("0/") + count,
		                        QStringLiteral("--threads"), QString::number(threads), QStringLiteral("--plan"), Native(plan)}));
		if (pipelines < 0) lines << QStringLiteral("      (<n>: the recipes of the hinted seed file / 100, once --apply-hints made it)");
		lines << QStringLiteral("    then: merge-recorded: %1 (without the claims)").arg(command({QStringLiteral("--merge"), QStringLiteral("--keep-shards")}));
	}
	lines << QStringLiteral("    shard0of%1-%2: %3")
	             .arg(QString::number(shards), QFileInfo(seeds).completeBaseName(),
	                  command({QStringLiteral("--seeds"), Native(seeds), QStringLiteral("--shard"), QStringLiteral("0/%1").arg(shards),
	                        QStringLiteral("--threads"), QString::number(threads), QStringLiteral("--plan"), Native(plan)}));
	lines << QStringLiteral("  merge: %1").arg(command({QStringLiteral("--merge"), QStringLiteral("--prune")}));
	return lines;
}

Precompile::Precompile(QObject* parent): QObject(parent) {
	qRegisterMetaType<PrecompileProgress>();
	m_ticker.setInterval(1000);
	connect(&m_ticker, &QTimer::timeout, this, &Precompile::tick);
}

Precompile::~Precompile() {
	if (m_running) stop();
}

void Precompile::start(const PrecompileSetup& setup) {
	if (m_running) return;
	m_setup        = setup;
	m_running      = true;
	m_stopping     = false;
	m_tier_merged  = false;
	m_has_recorded = false;
	m_recorded.clear();
	m_first.clear();
	m_second.clear();
	m_jobs.clear();
	m_job_info.clear();
	m_progress             = {};
	m_smoothed_remaining   = -1;
	m_compile_done_at_start = 0;
	m_last_line             = 0;
	m_clock.start();
	m_ticker.start();
	// The translation switches of the launch config (set already: kept).
	m_base = QProcessEnvironment::systemEnvironment();
	for (const QString& entry: setup.translationSwitches) {
		const qsizetype eq = entry.indexOf('=');
		m_base.insert(entry.left(eq), entry.mid(eq + 1));
	}
	QDir().mkpath(setup.logs);
	emit output(setup.describe().value(0));
	if (setup.makeSeeds) runStep(Step::Seeds);
	else beginRecorded();
}

// Recorded play's specializations: the release's hints applied first (when there are any), then everything gathered.
void Precompile::beginRecorded() {
	m_sources.clear();
	m_recorded.clear();
	if (m_setup.hasHints) runStep(Step::Released);
	else gatherRecorded();
}

void Precompile::gatherRecorded() {
	QStringList sources = m_sources;
	sources << m_setup.recordings;
	if (QFileInfo::exists(m_setup.recorded)) sources << m_setup.recorded;
	if (sources.size() == 1 && sources.first() == m_setup.released) {
		m_recorded = m_setup.released;
		runStep(Step::MergeBefore);
	} else if (!sources.isEmpty()) {
		m_sources = sources;
		runStep(Step::Gather);
	} else {
		runStep(Step::MergeBefore);
	}
}

QProcess* Precompile::makeTool(const QString& name, const QStringList& arguments, bool claims, bool allCpus) {
	auto*               process     = new QProcess(this);
	QProcessEnvironment environment = m_base;
	// Each process gets an empty NVIDIA disk cache of its own, removed when it is done.
	const QString cache = QDir(m_setup.nvidiaCaches).filePath(name);
	QDir().mkpath(cache);
	environment.insert(QStringLiteral("__GL_SHADER_DISK_CACHE_PATH"), Native(cache));
	if (claims) environment.insert(QStringLiteral("KYTY_PRECOMPILE_CLAIMS"), Native(QDir(m_setup.nvidiaCaches).filePath(QStringLiteral("claims"))));
	process->setProcessEnvironment(environment);
	process->setProgram(m_setup.tool);
	process->setArguments(QStringList {QStringLiteral("--game"), m_setup.game} + arguments);
	process->setWorkingDirectory(m_setup.root);
	process->setStandardOutputFile(QDir(m_setup.logs).filePath(name + QStringLiteral(".out.log")));
	process->setStandardErrorFile(QDir(m_setup.logs).filePath(name + QStringLiteral(".err.log")));
	process->setObjectName(name);
	connect(process, &QProcess::started, this, [this, process, allCpus] {
		Platform::setPrecompilePriority(process->processId());
		Platform::setProcessAffinity(process->processId(), allCpus ? m_setup.all : m_setup.mask);
	});
	return process;
}

void Precompile::runStep(Step step) {
	QString     name;
	QStringList arguments;
	bool        claims = false;
	switch (step) {
		case Step::Seeds:
			update(PrecompileProgress::Stage::Listing, tr("Listing the game's shaders"));
			QDir().mkpath(QFileInfo(m_setup.seeds).absolutePath());
			name      = QStringLiteral("seeds");
			arguments = {QStringLiteral("--make-seeds"), Native(m_setup.remakeSeeds ? m_setup.seeds + QStringLiteral(".new") : m_setup.seeds),
			             QStringLiteral("--states"), Native(m_setup.passStates)};
			break;
		case Step::Released:
			update(PrecompileProgress::Stage::Preparing, tr("Getting ready"));
			name      = QStringLiteral("released");
			arguments = {QStringLiteral("--seeds"), Native(m_setup.seeds), QStringLiteral("--apply-hints"), Native(m_setup.hints),
			             QStringLiteral("--out"), Native(m_setup.released)};
			break;
		case Step::Gather:
			update(PrecompileProgress::Stage::Preparing, tr("Getting ready"));
			name      = QStringLiteral("gather");
			arguments = {QStringLiteral("--seeds"), Native(m_setup.seeds), QStringLiteral("--make-hints"), Native(m_setup.together)};
			for (const QString& source: m_sources) arguments << QStringLiteral("--recorded") << Native(source);
			break;
		case Step::Apply:
			update(PrecompileProgress::Stage::Preparing, tr("Getting ready"));
			name      = QStringLiteral("apply");
			arguments = {QStringLiteral("--seeds"), Native(m_setup.seeds), QStringLiteral("--apply-hints"), Native(m_setup.together),
			             QStringLiteral("--out"), Native(m_setup.hinted)};
			break;
		case Step::MergeBefore:
			update(PrecompileProgress::Stage::Preparing, tr("Getting ready"));
			name      = QStringLiteral("merge-before");
			arguments = {QStringLiteral("--merge")};
			break;
		case Step::PlanRecorded:
			update(PrecompileProgress::Stage::Planning, tr("Planning the work"));
			name      = QStringLiteral("plan-recorded");
			arguments = {QStringLiteral("--seeds"), Native(m_recorded), QStringLiteral("--dry-run"), QStringLiteral("--plan"),
			             Native(m_setup.plan), QStringLiteral("--threads"), QString::number(m_setup.cpus)};
			break;
		case Step::PlanSeeds:
			update(PrecompileProgress::Stage::Planning, tr("Planning the work"));
			name      = QStringLiteral("plan-seeds");
			// (With the compiled inputs: the game's shader prefetch has them from the start.) The seeds' guessed specializations
			// of shaders recorded play has are left out (chen's dc3bca16: two 1-1 walks met 1 of 2,227 such pipelines).
			arguments = {QStringLiteral("--seeds"), Native(m_setup.seeds), QStringLiteral("--dry-run"), QStringLiteral("--plan"),
			             Native(m_setup.plan), QStringLiteral("--threads"), QString::number(m_setup.cpus), QStringLiteral("--static-inputs")};
			if (m_has_recorded) arguments << QStringLiteral("--covered-by") << Native(m_recorded);
			break;
		case Step::MergeRecorded:
			// Without the claims: a merge copying the store would claim every pipeline in it.
			name      = QStringLiteral("merge-recorded");
			arguments = {QStringLiteral("--merge"), QStringLiteral("--keep-shards")};
			break;
		case Step::Merge:
			m_merge_clock.start();
			update(PrecompileProgress::Stage::Merging, tr("Merging"));
			name      = QStringLiteral("merge");
			arguments = {QStringLiteral("--merge"), QStringLiteral("--prune")};
			break;
		case Step::Shards: scheduleShards(); return;
	}
	// (The seed and hint steps run as the script's own commands: on all CPUs.)
	QProcess* process = makeTool(name, arguments, claims,
	                             step == Step::Seeds || step == Step::Released || step == Step::Gather || step == Step::Apply);
	m_jobs.insert(name, process);
	connect(process, &QProcess::finished, this, [this, step, name, process](int code, QProcess::ExitStatus status) {
		m_jobs.remove(name);
		process->deleteLater();
		QDir(QDir(m_setup.nvidiaCaches).filePath(name)).removeRecursively();
		if (!m_stopping) afterStep(step, status == QProcess::NormalExit ? code : -1, name);
	});
	// (Queued: a start failure is reported inside start(), before this function has returned.)
	connect(
	    process, &QProcess::errorOccurred, this,
	    [this, step, name, process](QProcess::ProcessError error) {
		    if (error != QProcess::FailedToStart || m_stopping) return;
		    m_jobs.remove(name);
		    process->deleteLater();
		    afterStep(step, -1, name);
	    },
	    Qt::QueuedConnection);
	process->start();
}

void Precompile::afterStep(Step step, int code, const QString& name) {
	const QString log_name = Native(QDir(m_setup.root).relativeFilePath(m_setup.logs));
	switch (step) {
		case Step::Seeds: {
			if (code != 0) {
				fail(tr("Listing the game's shaders failed (kyty_shader_precompile --make-seeds); logs: %1").arg(log_name));
				return;
			}
			const QString made = m_setup.remakeSeeds ? m_setup.seeds + QStringLiteral(".new") : m_setup.seeds;
			if (m_setup.remakeSeeds && Caches::sameContent(made, m_setup.seeds)) {
				QFile::remove(made);
			} else if (m_setup.remakeSeeds) {
				QFile::remove(m_setup.seeds);
				QFile::rename(made, m_setup.seeds);
			}
			Caches::writeProgramStamp(m_setup.seeds, m_setup.programStamp);
			beginRecorded();
			return;
		}
		case Step::Released:
			if (code == 0 && QFileInfo::exists(m_setup.released)) m_sources << m_setup.released;
			else emit output(QStringLiteral("the release's specialization hints could not be applied (kyty_shader_precompile --apply-hints)"));
			gatherRecorded();
			return;
		case Step::Gather:
			if (code == 0) {
				runStep(Step::Apply);
				return;
			}
			emit output(QStringLiteral("the specializations of recorded play could not be gathered (kyty_shader_precompile --make-hints): "
			                           "the seeds alone"));
			runStep(Step::MergeBefore);
			return;
		case Step::Apply:
			if (code == 0 && QFileInfo::exists(m_setup.hinted)) m_recorded = m_setup.hinted;
			else emit output(QStringLiteral("the specializations of recorded play could not be applied (kyty_shader_precompile --apply-hints): "
			                                "the seeds alone"));
			runStep(Step::MergeBefore);
			return;
		case Step::MergeBefore:
			if (code != 0) {
				fail(tr("Merging the last run's shards failed; logs: %1").arg(log_name));
				return;
			}
			m_has_recorded = !m_recorded.isEmpty() && QFileInfo::exists(m_recorded);
			runStep(m_has_recorded ? Step::PlanRecorded : Step::PlanSeeds);
			return;
		case Step::PlanRecorded:
		case Step::PlanSeeds: {
			if (code != 0) {
				fail(tr("Planning the precompile failed; logs: %1").arg(log_name));
				return;
			}
			// "plan saved to <file> (63179 pipelines, ...)": the run's pipelines so far.
			const qint64 planned =
			    readNumber(QDir(m_setup.logs).filePath(name + QStringLiteral(".out.log")), QStringLiteral("plan saved to .*\\((\\d+) pipelines"));
			if (planned > 0) m_progress.total = planned;
			if (step == Step::PlanRecorded) {
				runStep(Step::PlanSeeds);
				return;
			}
			if (m_has_recorded) {
				// About as many pipelines a shard as the seeds' (100).
				const qint64 pipelines = std::max<qint64>(0, Caches::seedPipelines(m_recorded));
				const int    count     = static_cast<int>(std::max<qint64>(1, (pipelines + 99) / 100));
				for (int i = 0; i < count; i++) m_first.enqueue({{}, m_recorded, QStringLiteral("%1/%2").arg(i).arg(count)});
			}
			for (int i = 0; i < m_setup.shards; i++) m_second.enqueue({{}, m_setup.seeds, QStringLiteral("%1/%2").arg(i).arg(m_setup.shards)});
			m_tier_merged = m_first.isEmpty();
			m_compile_clock.start();
			m_compile_done_at_start = 0;
			update(PrecompileProgress::Stage::Compiling, tr("Compiling shaders"));
			scheduleShards();
			return;
		}
		case Step::MergeRecorded:
			if (code != 0) {
				fail(tr("Merging the first shaders failed; logs: %1").arg(log_name));
				return;
			}
			m_progress.readyToPlay = true;
			emit output(QStringLiteral("  %1 ready to play: the shaders of recorded play are in the static cache; the game's other shaders follow")
			                .arg(Hms(m_clock.elapsed())));
			scheduleShards();
			return;
		case Step::Merge:
			if (code != 0) {
				fail(tr("The final merge failed; logs: %1").arg(log_name));
				return;
			}
			complete();
			return;
		case Step::Shards: return;
	}
}

void Precompile::scheduleShards() {
	if (m_stopping) return;
	// The first tier done: merged into the static cache, so the game can use it while the rest compiles.
	if (!m_tier_merged && m_first.isEmpty() && m_jobs.isEmpty()) {
		m_tier_merged = true;
		if (!m_second.isEmpty()) {
			runStep(Step::MergeRecorded);
			return;
		}
	}
	QQueue<Job>* pending = !m_first.isEmpty() ? &m_first : m_tier_merged ? &m_second : nullptr;
	while (pending != nullptr && !pending->isEmpty() && m_jobs.size() < m_setup.jobs) {
		Job job = pending->dequeue();
		job.name = QStringLiteral("shard") + QString(job.shard).replace('/', QStringLiteral("of")) + '-' + QFileInfo(job.seedFile).completeBaseName();
		QProcess* process = makeTool(job.name,
		                              {QStringLiteral("--seeds"), Native(job.seedFile), QStringLiteral("--shard"), job.shard, QStringLiteral("--threads"),
		                               QString::number(m_setup.threads), QStringLiteral("--plan"), Native(m_setup.plan)},
		                              true);
		m_jobs.insert(job.name, process);
		m_job_info.insert(job.name, job);
		const QString name = job.name;
		connect(process, &QProcess::finished, this, [this, name, process](int code, QProcess::ExitStatus status) {
			process->deleteLater();
			if (!m_stopping) shardFinished(name, status == QProcess::NormalExit ? code : -1);
		});
		connect(
		    process, &QProcess::errorOccurred, this,
		    [this, name, process](QProcess::ProcessError error) {
			    if (error != QProcess::FailedToStart || m_stopping) return;
			    process->deleteLater();
			    shardFinished(name, -1);
		    },
		    Qt::QueuedConnection);
		process->start();
	}
	m_progress.shardsRunning = static_cast<int>(m_jobs.size());
	m_progress.shardsWaiting = static_cast<int>(m_first.size() + m_second.size());
	if (m_first.isEmpty() && m_second.isEmpty() && m_jobs.isEmpty() && m_tier_merged) runStep(Step::Merge);
}

void Precompile::shardFinished(const QString& name, int code) {
	m_jobs.remove(name);
	const Job job = m_job_info.take(name);
	QDir(QDir(m_setup.nvidiaCaches).filePath(name)).removeRecursively();
	const QString log_name = Native(QDir(m_setup.root).relativeFilePath(m_setup.logs));
	if (code == 3) {
		// Binaries were left out: the shard runs again as two.
		const QStringList parts = job.shard.split('/');
		const int         i = parts.value(0).toInt(), n = parts.value(1).toInt();
		if (n >= 65536) {
			fail(tr("A part of the precompile (%1) cannot be split further; logs: %2").arg(name, log_name));
			return;
		}
		QQueue<Job>& queue = job.seedFile == m_recorded ? m_first : m_second;
		queue.enqueue({{}, job.seedFile, QStringLiteral("%1/%2").arg(i).arg(2 * n)});
		queue.enqueue({{}, job.seedFile, QStringLiteral("%1/%2").arg(i + n).arg(2 * n)});
		m_progress.splits++;
	} else if (code != 0) {
		fail(tr("A part of the precompile (%1) failed; logs: %2").arg(name, log_name));
		return;
	} else {
		m_progress.shardsDone++;
		// "...; 237 pipelines in 242555 ms": what this shard made (their sum is the plan's count).
		const qint64 made = readNumber(QDir(m_setup.logs).filePath(name + QStringLiteral(".out.log")), QStringLiteral("; (\\d+) pipelines in \\d+ ms"));
		if (made > 0) m_progress.done += made;
	}
	if (m_progress.total > 0) {
		update(PrecompileProgress::Stage::Compiling,
		       tr("Compiling shaders %1 / %2").arg(Count(std::min(m_progress.done, m_progress.total)), Count(m_progress.total)));
	} else {
		update(PrecompileProgress::Stage::Compiling, tr("Compiling shaders"));
	}
	scheduleShards();
}

qint64 Precompile::readNumber(const QString& log, const QString& pattern) const {
	QFile file(log);
	if (!file.open(QIODevice::ReadOnly)) return -1;
	const QString                  text = QString::fromUtf8(file.readAll());
	const QRegularExpression        expression(pattern);
	QRegularExpressionMatchIterator it   = expression.globalMatch(text);
	qint64                          last = -1;
	while (it.hasNext()) last = it.next().captured(1).toLongLong();
	return last;
}

void Precompile::update(PrecompileProgress::Stage stage, const QString& text) {
	m_progress.stage = stage;
	m_progress.text  = text;
	tick();
}

void Precompile::tick() {
	if (!m_running) return;
	PrecompileProgress& p = m_progress;
	p.elapsedMs           = m_clock.elapsed();
	using S               = PrecompileProgress::Stage;
	const double compile  = p.total > 0 ? std::min(1.0, static_cast<double>(p.done) / p.total) : 0.0;
	switch (p.stage) {
		case S::Listing: p.fraction = 0.01; break;
		case S::Preparing: p.fraction = 0.02; break;
		case S::Planning: p.fraction = 0.03; break;
		case S::Compiling: p.fraction = 0.05 + 0.90 * compile; break;
		case S::Merging: p.fraction = 0.97; break;
		case S::Done: p.fraction = 1.0; break;
	}
	// Time left: the compile rate once a few minutes of shards have finished, else the static estimate.
	double remaining = -1;
	const qint64 compile_ms = m_compile_clock.isValid() ? m_compile_clock.elapsed() : 0;
	if (p.stage == S::Compiling && p.total > 0 && p.done > m_compile_done_at_start && compile_ms > 60 * 1000) {
		const double rate = static_cast<double>(p.done - m_compile_done_at_start) / compile_ms;
		remaining         = (p.total - p.done) / rate + kFinishMs;
	} else if (p.stage == S::Merging) {
		remaining = std::max<double>(15 * 1000, static_cast<double>(kFinishMs - m_merge_clock.elapsed()));
	} else if (p.stage != S::Done) {
		remaining = std::max<double>(60 * 1000, estimateMinutes(m_setup.cpus) * 60.0 * 1000.0 - p.elapsedMs);
	}
	if (remaining >= 0) {
		m_smoothed_remaining = m_smoothed_remaining < 0 ? remaining : 0.85 * (m_smoothed_remaining - 1000) + 0.15 * remaining;
		m_smoothed_remaining = std::max(0.0, m_smoothed_remaining);
		p.remainingMs        = static_cast<qint64>(m_smoothed_remaining);
	} else {
		p.remainingMs = p.stage == S::Done ? 0 : -1;
	}
	p.shardsRunning = p.stage == S::Compiling ? static_cast<int>(m_jobs.size()) : 0;
	p.shardsWaiting = static_cast<int>(m_first.size() + m_second.size());
	// Every minute, the script's line.
	if (p.stage == S::Compiling && p.elapsedMs - m_last_line >= 60 * 1000) {
		m_last_line = p.elapsedMs;
		emit output(QStringLiteral("  %1 shards: %2 done, %3 running, %4 waiting (%5 split)")
		                .arg(Hms(p.elapsedMs))
		                .arg(p.shardsDone)
		                .arg(p.shardsRunning)
		                .arg(p.shardsWaiting)
		                .arg(p.splits));
	}
	emit progressChanged(p);
}

void Precompile::complete() {
	const QDir static_dir(QDir(m_setup.root).filePath(QStringLiteral("_PipelineCache/static")));
	QFileInfo  newest;
	for (const char* extension: {".bin", ".binaries", ".blobs"}) {
		const QFileInfo file(static_dir.filePath(m_setup.cacheId + QString::fromLatin1(extension)));
		if (file.exists() && (!newest.exists() || file.lastModified() > newest.lastModified())) newest = file;
	}
	if (newest.exists()) {
		emit output(QStringLiteral("done in %1: %2 (%3 MB; %4 shards split)")
		                .arg(Hms(m_clock.elapsed()), Native(newest.absoluteFilePath()))
		                .arg(QLocale(QLocale::English).toString(newest.size() / (1024.0 * 1024.0), 'f', 0))
		                .arg(m_progress.splits));
	} else {
		emit output(QStringLiteral("done in %1 (%2 shards split)").arg(Hms(m_clock.elapsed())).arg(m_progress.splits));
	}
	update(PrecompileProgress::Stage::Done, tr("Done"));
	cleanup();
	emit finished(true, false, {});
}

void Precompile::fail(const QString& error) {
	m_stopping = true;
	for (QProcess* process: m_jobs) {
		process->disconnect(this);
		process->kill();
		process->waitForFinished(5000);
		process->deleteLater();
	}
	m_jobs.clear();
	emit output(error);
	cleanup();
	emit finished(false, false, error);
}

void Precompile::stop() {
	if (!m_running) return;
	m_stopping = true;
	for (QProcess* process: m_jobs) {
		process->disconnect(this);
		process->kill();
		process->waitForFinished(5000);
		process->deleteLater();
	}
	m_jobs.clear();
	emit output(QStringLiteral("precompile stopped: the next run continues where this one stopped"));
	cleanup();
	emit finished(false, true, {});
}

void Precompile::cleanup() {
	m_ticker.stop();
	m_running = false;
	QDir(m_setup.nvidiaCaches).removeRecursively();
}
