#pragma once

#include "core/Package.h"

#include <QObject>
#include <QPointer>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;

// The DLSS add-on (plugins/dlss: kyty_dlss.dll and NVIDIA's nvngx_dlss.dll, a download of its own under NVIDIA's
// license): downloaded from this fork's latest release and unzipped with the system's tar (Windows) or bsdtar/unzip.
class DlssAddon final: public QObject {
	Q_OBJECT

public:
	enum class Result { Installed, NotAvailable, Failed, Cancelled };
	Q_ENUM(Result)

	explicit DlssAddon(QObject* parent = nullptr);
	~DlssAddon() override;

	void download(const Package& package);
	void cancel();

signals:
	void progress(qint64 received, qint64 total);
	void finished(DlssAddon::Result result, const QString& detail);

private:
	void extract();
	void done(Result result, const QString& detail);

	Package                        m_package;
	QNetworkAccessManager*         m_network = nullptr;
	QPointer<QNetworkReply>        m_reply;
	QPointer<QProcess>             m_process;
	std::unique_ptr<QTemporaryDir> m_temp;
	std::unique_ptr<QFile>         m_file;
};
