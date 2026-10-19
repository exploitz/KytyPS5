#include "core/DlssAddon.h"

#include "core/Version.h"
#include "platform/Platform.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>

DlssAddon::DlssAddon(QObject* parent): QObject(parent), m_network(new QNetworkAccessManager(this)) {}

DlssAddon::~DlssAddon() {
	cancel();
}

void DlssAddon::download(const Package& package) {
	m_package = package;
	m_temp    = std::make_unique<QTemporaryDir>();
	if (!m_temp->isValid()) {
		done(Result::Failed, tr("No temporary folder could be made."));
		return;
	}
	m_file = std::make_unique<QFile>(m_temp->filePath(QStringLiteral("KytyPS5-DLSS-addon.zip")));
	if (!m_file->open(QIODevice::WriteOnly)) {
		done(Result::Failed, tr("The download could not be saved."));
		return;
	}
	QNetworkRequest request(QUrl(QString::fromLatin1(Version::kDlssAddonUrl)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KytyPS5-Launcher"));
	request.setTransferTimeout(60000);
	m_reply = m_network->get(request);
	connect(m_reply, &QNetworkReply::downloadProgress, this, &DlssAddon::progress);
	connect(m_reply, &QNetworkReply::readyRead, this, [this] {
		if (m_reply != nullptr && m_file != nullptr) m_file->write(m_reply->readAll());
	});
	connect(m_reply, &QNetworkReply::finished, this, [this] {
		QNetworkReply* reply = m_reply;
		if (reply == nullptr) return;
		reply->deleteLater();
		m_reply          = nullptr;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::OperationCanceledError) return;
		if (status == 404 || reply->error() == QNetworkReply::ContentNotFoundError) {
			done(Result::NotAvailable, {});
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			done(Result::Failed, reply->errorString());
			return;
		}
		m_file->write(reply->readAll());
		m_file->close();
		extract();
	});
}

void DlssAddon::cancel() {
	if (m_reply != nullptr) {
		m_reply->disconnect(this);
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = nullptr;
		done(Result::Cancelled, {});
	}
	if (m_process != nullptr) {
		m_process->disconnect(this);
		m_process->kill();
		m_process->waitForFinished(2000);
		m_process = nullptr;
		done(Result::Cancelled, {});
	}
}

void DlssAddon::extract() {
	const QString folder = m_temp->filePath(QStringLiteral("unpacked"));
	QDir().mkpath(folder);
	const Platform::Command command = Platform::zipExtractCommand(m_file->fileName(), folder);
	if (!command.found) {
		done(Result::Failed, tr("No unzip tool was found on this system."));
		return;
	}
	auto* process = new QProcess(this);
	m_process     = process;
	process->setProgram(command.program);
	process->setArguments(command.arguments);
	process->setProcessChannelMode(QProcess::MergedChannels);
	connect(process, &QProcess::finished, this, [this, process, folder](int code, QProcess::ExitStatus status) {
		const QString out = QString::fromLocal8Bit(process->readAll()).trimmed();
		process->deleteLater();
		m_process = nullptr;
		if (status != QProcess::NormalExit || code != 0) {
			done(Result::Failed, tr("The download could not be unzipped (%1).").arg(out));
			return;
		}
		// The folder that holds kyty_dlss.dll (the zip's root or a folder in it) goes to plugins/dlss.
		QString source;
		for (QDirIterator it(folder, {QStringLiteral("kyty_dlss.dll")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
			source = QFileInfo(it.next()).absolutePath();
			break;
		}
		if (source.isEmpty() || !QFileInfo::exists(QDir(source).filePath(QStringLiteral("nvngx_dlss.dll")))) {
			done(Result::Failed, tr("The download did not contain kyty_dlss.dll and nvngx_dlss.dll."));
			return;
		}
		const QDir target(m_package.path(QStringLiteral("plugins/dlss")));
		QDir().mkpath(target.path());
		for (const QFileInfo& file: QDir(source).entryInfoList(QDir::Files)) {
			QFile::remove(target.filePath(file.fileName()));
			if (!QFile::copy(file.absoluteFilePath(), target.filePath(file.fileName()))) {
				done(Result::Failed, tr("%1 could not be written into plugins\\dlss.").arg(file.fileName()));
				return;
			}
		}
		if (m_package.hasDlssAddon()) done(Result::Installed, {});
		else done(Result::Failed, tr("The add-on's files are missing after installing it."));
	});
	process->start();
}

void DlssAddon::done(Result result, const QString& detail) {
	if (m_file != nullptr) m_file->close();
	m_file.reset();
	m_temp.reset();
	emit finished(result, detail);
}
