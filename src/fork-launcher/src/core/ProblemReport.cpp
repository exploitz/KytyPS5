#include "core/ProblemReport.h"

#include "core/SystemCheck.h"
#include "core/Version.h"
#include "platform/Platform.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>

#include <algorithm>

namespace {

// A log copied whole, or its last 16 MB (a long session's output), without the user name (withoutUserName).
bool CopyTail(const QString& from, const QString& to) {
	constexpr qint64 limit = 16 * 1024 * 1024;
	QFile            in(from);
	if (!in.open(QIODevice::ReadOnly)) return false;
	QFile out(to);
	if (!out.open(QIODevice::WriteOnly)) return false;
	if (in.size() > limit) {
		in.seek(in.size() - limit);
		out.write("[... the beginning of this log is left out ...]\n");
	}
	out.write(ProblemReport::withoutUserName(QString::fromUtf8(in.readAll())).toUtf8());
	return true;
}

} // namespace

ProblemReport::ProblemReport(QObject* parent): QObject(parent) {}

QString ProblemReport::withoutUserName(const QString& text) {
	const QString user = QFileInfo(QDir::homePath()).fileName();
	if (user.isEmpty()) return text;
	// \Users\<name>, /Users/<name>, \\Users\\<name> (JSON) or /home/<name>; the name ends at a separator, a quote,
	// a space or the end.
	const QRegularExpression paths(QStringLiteral(R"(((?:\\{1,2}|/)(?:Users|home)(?:\\{1,2}|/))%1(?=[\\/"'\s]|$))")
	                                   .arg(QRegularExpression::escape(user)),
	                               QRegularExpression::CaseInsensitiveOption);
	QString result = text;
	return result.replace(paths, QStringLiteral("\\1<user>"));
}

ProblemReport::~ProblemReport() {
	if (m_process != nullptr) {
		m_process->disconnect(this);
		m_process->kill();
		m_process->waitForFinished(3000);
	}
}

QString ProblemReport::systemText(const Package& package, const GameInfo& game, const QString& shaderState) {
	const SystemInfo info = SystemInfo::gather(package, false);
	QStringList      lines;
	lines << QStringLiteral("KytyPS5 Launcher %1").arg(Version::text());
	lines << QStringLiteral("Report made: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
	lines << QString();
	lines << QStringLiteral("OS:           %1").arg(info.os);
	lines << QStringLiteral("CPU:          %1").arg(info.cpu);
	lines << QStringLiteral("Threads:      %1%2").arg(info.threads).arg(info.hybrid ? QStringLiteral(" (with efficiency cores)") : QString());
	lines << QStringLiteral("AVX2:         %1").arg(info.avx2 ? QStringLiteral("yes") : QStringLiteral("no"));
	lines << QStringLiteral("GPU:          %1").arg(info.gpu.found ? info.gpu.name : QStringLiteral("not found"));
	lines << QStringLiteral("GPU driver:   %1").arg(info.gpu.driver.isEmpty() ? QStringLiteral("unknown") : info.gpu.driver);
	lines << QStringLiteral("Video memory: %1").arg(info.gpu.videoMemory > 0 ? formatGb(static_cast<double>(info.gpu.videoMemory)) : QStringLiteral("unknown"));
	lines << QStringLiteral("RAM:          %1").arg(formatGb(info.memory.ram));
	lines << QStringLiteral("Commit free:  %1").arg(info.memory.commitKnown ? formatGb(info.memory.commitFree) +
	                                                                              (info.memory.systemManagedPageFile ? QStringLiteral(" (system-managed page file)")
	                                                                                                                 : QStringLiteral(" (fixed page file)"))
	                                                                        : QStringLiteral("not limited"));
	lines << QStringLiteral("Folder:       %1").arg(QDir::toNativeSeparators(info.folder));
	lines << QStringLiteral("Disk free:    %1 of %2 (%3)").arg(formatGb(info.diskFree), formatGb(info.diskTotal), info.fileSystem);
	QStringList folder_state;
	if (info.inZip) folder_state << QStringLiteral("inside a zip");
	if (info.inTemp) folder_state << QStringLiteral("in Temp");
	if (info.inOneDrive) folder_state << QStringLiteral("in OneDrive");
	if (!info.writable) folder_state << QStringLiteral("read-only");
	lines << QStringLiteral("Folder state: %1").arg(folder_state.isEmpty() ? QStringLiteral("normal") : folder_state.join(QStringLiteral(", ")));
	for (const QScreen* screen: QGuiApplication::screens()) {
		const QSize size = screen->size() * screen->devicePixelRatio();
		lines << QStringLiteral("Screen:       %1x%2 at %3%, %4 Hz%5")
		             .arg(size.width())
		             .arg(size.height())
		             .arg(qRound(screen->devicePixelRatio() * 100))
		             .arg(qRound(screen->refreshRate()))
		             .arg(screen == QGuiApplication::primaryScreen() ? QStringLiteral(" (primary)") : QString());
	}
	lines << QString();
	lines << QStringLiteral("Game:         %1").arg(QDir::toNativeSeparators(game.path));
	lines << QStringLiteral("Game id:      %1%2").arg(game.versionKey(), !game.tested() ? QStringLiteral(" (untested)") : game.supportedOnly() ? QStringLiteral(" (supported)") : QStringLiteral(" (tested)"));
	lines << QStringLiteral("Title:        %1").arg(game.titleName);
	lines << QStringLiteral("Precompile:   %1").arg(shaderState);
	lines << QString();
	lines << QStringLiteral("Files:");
	const QStringList optional = {Platform::emulatorExeName(),
	                              Platform::precompileExeName(),
	                              QStringLiteral("launch.json"),
	                              QStringLiteral("run-windows.json"),
	                              QStringLiteral("tools/local/static-precompile/pass-states.json"),
	                              QStringLiteral("tools/local/static-precompile/hints-%1.hints").arg(game.cacheId()),
	                              QStringLiteral("seeds-%1.seeds").arg(game.cacheId()),
	                              QStringLiteral("recorded-%1.seeds").arg(game.cacheId()),
	                              QStringLiteral("hinted-%1.seeds").arg(game.cacheId()),
	                              QStringLiteral("_PipelineCache/static/%1.binaries").arg(game.cacheId()),
	                              QStringLiteral("_PipelineCache/static/%1.bin").arg(game.cacheId()),
	                              QStringLiteral("_PipelineCache/static/%1.blobs").arg(game.cacheId()),
	                              QStringLiteral("_PipelineCache/static/%1.shaders").arg(game.cacheId()),
	                              QStringLiteral("patches/1080p-quality.json"),
	                              QStringLiteral("patches/1080p-fast.json"),
	                              QStringLiteral("deps/fsr4"),
	                              QStringLiteral("deps/fsr/amd_fidelityfx_vk.dll"),
	                              QStringLiteral("plugins/dlss/kyty_dlss.dll"),
	                              QStringLiteral("plugins/dlss/nvngx_dlss.dll")};
	for (const QString& file: optional) {
		const QFileInfo f(package.path(file));
		lines << QStringLiteral("  %1 %2%3")
		             .arg(f.exists() ? QStringLiteral("yes") : QStringLiteral("no "), QDir::toNativeSeparators(file),
		                  f.isFile() ? QStringLiteral(" (%1 bytes)").arg(f.size()) : QString());
	}
	return lines.join('\n') + '\n';
}

void ProblemReport::create(const Package& package, const QString& system, const QString& launch, const QString& launcherLog) {
	if (busy()) return;
	m_staging = std::make_unique<QTemporaryDir>();
	if (!m_staging->isValid()) {
		emit finished(false, {}, tr("No temporary folder could be made."));
		return;
	}
	const QDir  staging(m_staging->path());
	QStringList files;
	auto        write = [&](const QString& name, const QString& text) {
        QFile file(staging.filePath(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(withoutUserName(text).toUtf8());
            files << name;
        }
	};
	write(QStringLiteral("system.txt"), system);
	if (!launch.isEmpty()) write(QStringLiteral("launch.txt"), launch);
	if (!launcherLog.isEmpty()) write(QStringLiteral("launcher-log.txt"), launcherLog);
	if (QFile settings(package.settingsFile); settings.open(QIODevice::ReadOnly)) {
		write(QStringLiteral("launcher-settings.json"), QString::fromUtf8(settings.readAll()));
	}
	// The newest play runs: <stamp>.out.log and <stamp>.err.log, at most five.
	const QDir logs(package.logDir);
	QMap<QString, QStringList> runs; // stamp -> files
	for (const QFileInfo& file: logs.entryInfoList({QStringLiteral("*.out.log"), QStringLiteral("*.err.log")}, QDir::Files)) {
		runs[file.fileName().section('.', 0, 0)].append(file.absoluteFilePath());
	}
	QStringList stamps = runs.keys();
	std::sort(stamps.begin(), stamps.end(), std::greater<>());
	if (stamps.size() > 5) stamps = stamps.mid(0, 5);
	if (!stamps.isEmpty()) {
		staging.mkpath(QStringLiteral("logs"));
		files << QStringLiteral("logs");
	}
	for (const QString& stamp: stamps) {
		for (const QString& file: runs.value(stamp)) CopyTail(file, staging.filePath(QStringLiteral("logs/") + QFileInfo(file).fileName()));
	}
	// The last precompile: its own steps' logs and the errors of its shards.
	QStringList precompiles = logs.entryList({QStringLiteral("*-precompile")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	if (!precompiles.isEmpty()) {
		const QString name = precompiles.last();
		const QDir    run(logs.filePath(name));
		staging.mkpath(QStringLiteral("logs/") + name);
		if (!files.contains(QStringLiteral("logs"))) files << QStringLiteral("logs");
		int errors = 0;
		for (const QFileInfo& file: run.entryInfoList({QStringLiteral("*.log")}, QDir::Files)) {
			const bool shard = file.fileName().startsWith(QStringLiteral("shard"));
			if (shard && (file.size() == 0 || !file.fileName().endsWith(QStringLiteral(".err.log")) || ++errors > 20)) continue;
			CopyTail(file.absoluteFilePath(), staging.filePath(QStringLiteral("logs/%1/%2").arg(name, file.fileName())));
		}
	}
	// The zip on the Desktop.
	const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	QDir().mkpath(desktop);
	const QString base = QStringLiteral("KytyPS5-report-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
	m_zip              = QDir(desktop).filePath(base + QStringLiteral(".zip"));
	for (int i = 2; QFileInfo::exists(m_zip); i++) m_zip = QDir(desktop).filePath(QStringLiteral("%1-%2.zip").arg(base).arg(i));
	// Made in the staging folder under a plain name, then copied: tar.exe cannot open an archive path with letters
	// outside the system's code page (a Desktop under C:\Users\<a Polish or Cyrillic name> on an English Windows).
	const QString           made    = staging.filePath(QStringLiteral("KytyPS5-report.zip"));
	const Platform::Command command = Platform::zipCreateCommand(QStringLiteral("KytyPS5-report.zip"), staging.path(), files);
	if (!command.found) {
		emit finished(false, {}, tr("No zip tool was found on this system."));
		return;
	}
	auto* process = new QProcess(this);
	m_process     = process;
	process->setProgram(command.program);
	process->setArguments(command.arguments);
	process->setWorkingDirectory(command.workingDirectory);
	process->setProcessChannelMode(QProcess::MergedChannels);
	connect(process, &QProcess::finished, this, [this, process, made](int code, QProcess::ExitStatus status) {
		QString out = QString::fromLocal8Bit(process->readAll()).trimmed();
		process->deleteLater();
		m_process     = nullptr;
		const bool zipped = status == QProcess::NormalExit && code == 0 && QFileInfo::exists(made);
		const bool ok     = zipped && QFile::copy(made, m_zip);
		if (zipped && !ok) out = tr("It could not be saved to %1").arg(QDir::toNativeSeparators(m_zip));
		m_staging.reset();
		emit finished(ok, m_zip, ok ? QString() : out);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		process->deleteLater();
		m_process = nullptr;
		m_staging.reset();
		emit finished(false, {}, process->errorString());
	});
	process->start();
}
