#include "core/PlayRunner.h"

#include "core/Caches.h"
#include "platform/Platform.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

PlayRunner::PlayRunner(QObject* parent): QObject(parent) {
	m_poll.setInterval(500);
	connect(&m_poll, &QTimer::timeout, this, &PlayRunner::pollGame);
}

PlayRunner::~PlayRunner() {
	cancelPreparation();
	if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
		// The launcher quits while the game runs (it asked first): the game keeps running. A QProcess would end it when
		// destroyed, so it is left behind as the program exits.
		m_process->disconnect(this);
		m_process->setParent(nullptr);
		m_process = nullptr;
	}
}

qint64 PlayRunner::gamePid() const {
	return m_process != nullptr ? m_process->processId() : 0;
}

void PlayRunner::setStage(Stage stage, const QString& detail) {
	m_stage = stage;
	emit stageChanged(stage, detail);
}

void PlayRunner::start(const LaunchPlan& plan, const GameInfo& game, const Package& package) {
	if (active()) return;
	m_plan         = plan;
	m_game         = game;
	m_package      = package;
	m_shaders      = false;
	m_inputs_ready = false;
	setStage(Stage::Starting);
	emit output(plan.dryRunLines().mid(0, 5).join('\n'));
	for (const QString& line: Caches::renameTitleCaches(package, game)) emit output(line);
	listSeeds();
}

void PlayRunner::cancelPreparation() {
	if (m_tool != nullptr) {
		m_tool->disconnect(this);
		m_tool->kill();
		m_tool->waitForFinished(3000);
		m_tool->deleteLater();
		m_tool = nullptr;
		if (m_stage == Stage::Starting || m_stage == Stage::PreparingShaders) setStage(Stage::Idle);
	}
}

void PlayRunner::runTool(const QStringList& arguments, bool captureOutput, std::function<void(int, const QByteArray&)> done) {
	auto* tool = new QProcess(this);
	m_tool     = tool;
	tool->setProgram(m_plan.tool);
	tool->setArguments(arguments);
	tool->setWorkingDirectory(m_plan.root);
	if (captureOutput) {
		tool->setProcessChannelMode(QProcess::SeparateChannels);
	} else {
		tool->setProcessChannelMode(QProcess::MergedChannels);
		connect(tool, &QProcess::readyReadStandardOutput, this, [this, tool] {
			const QString text = QString::fromUtf8(tool->readAllStandardOutput()).trimmed();
			if (!text.isEmpty()) emit output(text);
		});
	}
	connect(tool, &QProcess::started, this, [this, tool] {
		if (m_plan.mask != m_plan.all) Platform::setProcessAffinity(tool->processId(), m_plan.mask);
	});
	connect(tool, &QProcess::finished, this, [this, tool, captureOutput, done](int code, QProcess::ExitStatus status) {
		const QByteArray out = captureOutput ? tool->readAllStandardOutput() : QByteArray();
		if (!captureOutput) {
			const QString rest = QString::fromUtf8(tool->readAllStandardOutput()).trimmed();
			if (!rest.isEmpty()) emit output(rest);
		}
		m_tool = nullptr;
		tool->deleteLater();
		done(status == QProcess::NormalExit ? code : -1, out);
	});
	connect(tool, &QProcess::errorOccurred, this, [this, tool, done](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		m_tool = nullptr;
		tool->deleteLater();
		done(-1, {});
	});
	tool->start();
}

void PlayRunner::listSeeds() {
	if (!m_plan.listSeeds) {
		queryStatus();
		return;
	}
	// No seed file yet (a release has none: it holds the game's shader code), or one another program made.
	setStage(Stage::PreparingShaders, tr("Listing the game's shaders"));
	QDir().mkpath(QFileInfo(m_plan.seedsMade).absolutePath());
	emit output(QStringLiteral("shaders:  listing the game's shaders from its files %1")
	                .arg(m_plan.staleSeeds ? QStringLiteral("again (another precompile program)") : QStringLiteral("(once)")));
	const QStringList arguments {QStringLiteral("--game"), m_plan.game, QStringLiteral("--make-seeds"),
	                             QDir::toNativeSeparators(m_plan.seedsMade), QStringLiteral("--states"),
	                             QDir::toNativeSeparators(m_plan.passStates)};
	// (& $tool ... | Out-Null: no affinity, the output discarded; its errors are shown.)
	auto* tool = new QProcess(this);
	m_tool     = tool;
	tool->setProgram(m_plan.tool);
	tool->setArguments(arguments);
	tool->setWorkingDirectory(m_plan.root);
	tool->setStandardOutputFile(QProcess::nullDevice());
	connect(tool, &QProcess::readyReadStandardError, this, [this, tool] {
		const QString text = QString::fromUtf8(tool->readAllStandardError()).trimmed();
		if (!text.isEmpty()) emit output(text);
	});
	auto finish = [this, tool](int code) {
		m_tool = nullptr;
		tool->deleteLater();
		if (code == 0 && QFileInfo::exists(m_plan.seedsMade)) {
			if (m_plan.staleSeeds && Caches::sameContent(m_plan.seedsMade, m_plan.seedsTarget)) {
				QFile::remove(m_plan.seedsMade);
			} else if (m_plan.staleSeeds) {
				QFile::remove(m_plan.seedsTarget);
				QFile::rename(m_plan.seedsMade, m_plan.seedsTarget);
			}
			Caches::writeProgramStamp(m_plan.seedsTarget, m_plan.programStamp);
			m_plan.seeds = m_plan.seedsTarget;
		} else if (!m_plan.staleSeeds) {
			emit output(QStringLiteral("shaders:  listing failed: the game compiles its shaders when they first appear"));
		}
		queryStatus();
	};
	connect(tool, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
		finish(status == QProcess::NormalExit ? code : -1);
	});
	connect(tool, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) finish(-1);
	});
	tool->start();
}

void PlayRunner::queryStatus() {
	m_shaders = !m_plan.seeds.isEmpty() && QFileInfo::exists(m_plan.tool);
	if (!m_shaders) {
		makeInputs();
		return;
	}
	setStage(Stage::PreparingShaders, tr("Checking the shaders"));
	runTool(m_plan.toolArguments({QStringLiteral("--status")}), true, [this](int code, const QByteArray& out) {
		m_shaders              = code == 0;
		const QString status   = QString::fromUtf8(out);
		m_inputs_ready         = status.contains(QStringLiteral("inputs current"));
		const bool outdated    = status.contains(QStringLiteral("static cache outdated"));
		const bool current     = status.contains(QStringLiteral("static cache current"));
		const bool cache_ready = current && !Caches::hasShardCheckpoints(m_package);
		if (m_shaders) {
			emit shaderStatus(m_inputs_ready, current ? QStringLiteral("current") : outdated ? QStringLiteral("outdated") : QStringLiteral("stale"));
			emit output(QStringLiteral("shaders:  prefetch inputs %1, static pipeline cache %2")
			                .arg(m_inputs_ready ? QStringLiteral("ready") : QStringLiteral("to be made"),
			                     cache_ready ? QStringLiteral("ready")
			                     : outdated  ? QStringLiteral("out of date (the precompile compiles what is new)")
			                                 : QStringLiteral("not made (precompile)")));
		}
		makeInputs();
	});
}

void PlayRunner::makeInputs() {
	if (!m_shaders || m_inputs_ready) {
		launchGame();
		return;
	}
	setStage(Stage::PreparingShaders, tr("Preparing shaders for your graphics card"));
	emit output(QStringLiteral("shaders:  making the background shader preparation inputs for this graphics card "
	                           "(first run or after a driver update: about a minute)"));
	runTool(m_plan.toolArguments({QStringLiteral("--no-pipelines"), QStringLiteral("--static-inputs"), QStringLiteral("--threads"),
	                              QString::number(m_plan.cpus)}),
	        false, [this](int code, const QByteArray&) {
		        if (code != 0) emit output(QStringLiteral("shaders:  making them failed; no background shader preparation this time"));
		        launchGame();
	        });
}

void PlayRunner::launchGame() {
	if (!QDir().mkpath(m_plan.logDir)) {
		setStage(Stage::Idle);
		emit failed(tr("The logs folder could not be created (%1).").arg(QDir::toNativeSeparators(m_plan.logDir)));
		return;
	}
	setStage(Stage::Loading, tr("Loading"));
	m_process = new QProcess(this);
	m_process->setProgram(m_plan.exe);
	m_process->setArguments(m_plan.arguments());
	m_process->setWorkingDirectory(m_plan.root);
	m_process->setProcessEnvironment(m_plan.processEnvironment(QProcessEnvironment::systemEnvironment()));
	m_process->setStandardOutputFile(m_plan.outLog());
	m_process->setStandardErrorFile(m_plan.errLog());
	connect(m_process, &QProcess::started, this, [this] {
		if (m_plan.mask != m_plan.all) Platform::setProcessAffinity(m_process->processId(), m_plan.mask);
		emit output(QStringLiteral("pid %1; logs: %2\\%3.*.log")
		                .arg(m_process->processId())
		                .arg(QDir::toNativeSeparators(QDir(m_plan.root).relativeFilePath(m_plan.logDir)), m_plan.stamp));
		m_since_start.start();
		m_log_offset = 0;
		m_log_pending.clear();
		m_poll.start();
	});
	connect(m_process, &QProcess::finished, this, &PlayRunner::finishGame);
	connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		const QString reason = m_process->errorString();
		m_process->deleteLater();
		m_process = nullptr;
		setStage(Stage::Idle);
		emit failed(tr("The emulator could not be started (%1).").arg(reason));
	});
	m_process->start();
}

void PlayRunner::pollGame() {
	readLog();
	if (m_stage != Stage::Loading || m_process == nullptr) return;
	const std::optional<bool> window = Platform::processHasWindow(m_process->processId());
	// (Where the platform cannot tell: a few seconds after the start.)
	if ((window.has_value() && *window) || (!window.has_value() && m_since_start.elapsed() > 4000)) {
		setStage(Stage::Running, tr("Running"));
		emit gameWindowShown();
	}
}

void PlayRunner::readLog() {
	QFile file(m_plan.outLog());
	if (!file.open(QIODevice::ReadOnly)) return;
	if (file.size() < m_log_offset) m_log_offset = 0;
	if (!file.seek(m_log_offset)) return;
	const QByteArray data = file.read(4 * 1024 * 1024);
	m_log_offset += data.size();
	m_log_pending += data;
	const int last = m_log_pending.lastIndexOf('\n');
	if (last < 0) return;
	const QString text = QString::fromUtf8(m_log_pending.left(last)).remove('\r');
	m_log_pending.remove(0, last + 1);
	if (!text.isEmpty()) emit output(text);
}

void PlayRunner::finishGame(int exitCode, QProcess::ExitStatus status) {
	m_poll.stop();
	readLog();
	if (!m_log_pending.isEmpty()) emit output(QString::fromUtf8(m_log_pending));
	m_log_pending.clear();
	const bool crashed = status == QProcess::CrashExit;
	emit       output(QStringLiteral("The game exited with code %1 (0x%2)")
	                .arg(exitCode)
	                .arg(QString::number(static_cast<quint32>(exitCode), 16).toUpper().rightJustified(8, QChar(u'0'))));
	m_process->deleteLater();
	m_process = nullptr;
	setStage(Stage::Idle);
	emit gameExited(exitCode, crashed, m_plan.outLog(), m_plan.errLog());
}
