#include "core/UpdateChecker.h"

#include "core/Version.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

UpdateChecker::UpdateChecker(QObject* parent): QObject(parent), m_network(this) {}

UpdateChecker::Release UpdateChecker::parse(const QByteArray& data, const QString& ownTag) {
	const QJsonDocument document = QJsonDocument::fromJson(data);
	Release             newest;
	if (!document.isArray()) {
		newest.error = QStringLiteral("Invalid update feed");
		return newest;
	}
	QDateTime newest_date;
	for (const QJsonValue& value: document.array()) {
		const QJsonObject item = value.toObject();
		if (item.value(QStringLiteral("draft")).toBool()) continue;
		const QString   tag       = item.value(QStringLiteral("tag_name")).toString();
		const QString   published = item.value(QStringLiteral("published_at")).toString();
		const QDateTime date      = QDateTime::fromString(published, Qt::ISODate);
		if (tag.isEmpty() || !date.isValid()) continue;
		if (!ownTag.isEmpty() && tag == ownTag) newest.ownPublished = published;
		if (!newest_date.isValid() || date > newest_date) {
			newest_date       = date;
			newest.tag        = tag;
			newest.page       = QUrl(item.value(QStringLiteral("html_url")).toString());
			newest.published  = published;
		}
	}
	if (newest.tag.isEmpty() || !newest.page.isValid() || newest.page.scheme() != QStringLiteral("https")) {
		newest.error = QStringLiteral("No release in the update feed");
	}
	return newest;
}

bool UpdateChecker::isNewer(const Release& release, const QString& ownTag, const QString& ownCommitDate) {
	if (!release.error.isEmpty()) return false;
	if (!ownTag.isEmpty() && release.tag == ownTag) return false;
	// This build's own release in the list: one published after it.
	if (!ownTag.isEmpty() && !release.ownPublished.isEmpty()) {
		return QDateTime::fromString(release.published, Qt::ISODate) > QDateTime::fromString(release.ownPublished, Qt::ISODate);
	}
	// A test build (its tag not in the list) or a development build: a release published more than a day after its code.
	const QDateTime published = QDateTime::fromString(release.published, Qt::ISODate);
	const QDateTime own       = QDateTime::fromString(ownCommitDate, Qt::ISODate);
	return published.isValid() && own.isValid() && published > own.addDays(1);
}

void UpdateChecker::check() {
	if (m_checking) return;
	m_checking = true;
	QNetworkRequest request(QUrl(QString::fromLatin1(Version::kReleasesApiUrl)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "KytyPS5-Launcher");
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(15000);
	QNetworkReply* reply = m_network.get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		m_checking = false;
		// (404: the repository has no release yet.)
		if (reply->error() != QNetworkReply::NoError) return;
		const Release release = parse(reply->readAll(), Version::releaseTag());
		if (isNewer(release, Version::releaseTag(), Version::commitDate())) emit updateAvailable(release.tag, release.page);
	});
}
