#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QByteArray;

// Whether this fork has a newer release (adapted from src/launcher's UpdateChecker: the GitHub API's releases of
// exploitz/KytyPS5, pre-releases included, without its dialogs and its dependency on the emulator's git version
// header). A release build reports a release published after its own; a build whose tag is not in the list (a test
// build) and a development build, releases published more than a day after their commit. No release yet: nothing.
class UpdateChecker final: public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject* parent = nullptr);

	void check();

	struct Release {
		QString tag;
		QUrl    page;
		QString published;    // ISO 8601
		QString ownPublished; // this build's own release (its tag in the list), else empty
		QString error;
	};
	// The newest release that is not a draft.
	static Release parse(const QByteArray& data, const QString& ownTag);
	static bool    isNewer(const Release& release, const QString& ownTag, const QString& ownCommitDate);

signals:
	void updateAvailable(const QString& tag, const QUrl& page);

private:
	QNetworkAccessManager m_network;
	bool                  m_checking = false;
};
