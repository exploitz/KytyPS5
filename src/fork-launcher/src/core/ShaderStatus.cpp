#include "core/ShaderStatus.h"

#include <QDir>
#include <QFileInfo>

ShaderStatus::ShaderStatus(QObject* parent): QObject(parent) {}

ShaderStatus::~ShaderStatus() {
	if (m_process != nullptr) {
		m_process->disconnect(this);
		m_process->kill();
		m_process->waitForFinished(2000);
	}
}

void ShaderStatus::refresh(const Package& package, const GameInfo& game, bool query) {
	if (m_process != nullptr) {
		m_process->disconnect(this);
		m_process->kill();
		m_process->deleteLater();
		m_process = nullptr;
	}
	m_state       = {};
	m_state.files = Caches::staticState(package, game.cacheId());
	emit changed();
	if (!query || game.cacheId().isEmpty() || !QFileInfo::exists(package.tool)) return;
	const QString name = QStringLiteral("seeds-%1.seeds").arg(game.cacheId());
	QString       seeds;
	for (const QString& candidate: {package.path(name), package.path(QStringLiteral("_Build/static-precompile/") + name)}) {
		if (QFileInfo::exists(candidate)) {
			seeds = candidate;
			break;
		}
	}
	if (seeds.isEmpty()) return;
	auto* process = new QProcess(this);
	m_process     = process;
	process->setProgram(package.tool);
	process->setArguments({QStringLiteral("--game"), QDir::toNativeSeparators(game.path), QStringLiteral("--seeds"),
	                       QDir::toNativeSeparators(seeds), QStringLiteral("--status")});
	process->setWorkingDirectory(package.root);
	process->setStandardErrorFile(QProcess::nullDevice());
	connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
		const QString out = QString::fromUtf8(process->readAllStandardOutput());
		process->deleteLater();
		m_process = nullptr;
		if (status != QProcess::NormalExit || code != 0) return;
		setQueried(out.contains(QStringLiteral("inputs current")),
		           out.contains(QStringLiteral("static cache current"))    ? QStringLiteral("current")
		           : out.contains(QStringLiteral("static cache outdated")) ? QStringLiteral("outdated")
		                                                                   : QStringLiteral("stale"));
	});
	process->start();
}

void ShaderStatus::setQueried(bool inputsCurrent, const QString& staticCache) {
	m_state.queried       = true;
	m_state.inputsCurrent = inputsCurrent;
	m_state.staticCache   = staticCache;
	emit changed();
}
