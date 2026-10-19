#include "core/GameInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {

bool ParseParam(const QByteArray& raw, GameInfo& info) {
	QByteArray data = raw;
	if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
	const QJsonDocument document = QJsonDocument::fromJson(data);
	if (!document.isObject()) return false;
	const QJsonObject json = document.object();
	info.titleId           = json.value(QStringLiteral("titleId")).toString();
	info.contentVersion    = json.value(QStringLiteral("contentVersion")).toString();
	// localizedParameters.(localizedParameters.defaultLanguage).titleName
	const QJsonObject localized = json.value(QStringLiteral("localizedParameters")).toObject();
	const QString     language  = localized.value(QStringLiteral("defaultLanguage")).toString();
	info.titleName              = localized.value(language).toObject().value(QStringLiteral("titleName")).toString();
	return true;
}

} // namespace

const QStringList& GameInfo::testedVersions() {
	static const QStringList versions = {QStringLiteral("PPSA01341 01.007.000"), QStringLiteral("PPSA01341 01.005.000")};
	return versions;
}

QString GameInfo::versionKey() const {
	return paramRead ? QStringLiteral("%1 %2").arg(titleId, contentVersion) : QStringLiteral("unknown");
}

QString GameInfo::cacheId() const {
	return paramRead && !titleId.isEmpty() && !contentVersion.isEmpty() ? QStringLiteral("%1_%2").arg(titleId, contentVersion)
	                                                                    : QString();
}

QString GameInfo::displayVersion() const {
	const QStringList parts = contentVersion.split('.');
	bool              ok1 = false, ok2 = false;
	const int         major = parts.value(0).toInt(&ok1);
	const int         minor = parts.value(1).toInt(&ok2);
	if (!ok1 || !ok2) return contentVersion.isEmpty() ? QStringLiteral("unknown version") : contentVersion;
	return QStringLiteral("v%1.%2").arg(major).arg(minor, 2, 10, QChar(u'0'));
}

bool GameInfo::tested() const {
	// (-contains compares case-insensitively.)
	return testedVersions().contains(versionKey(), Qt::CaseInsensitive);
}

bool GameInfo::supportedOnly() const {
	return tested() && versionKey().compare(testedVersions().first(), Qt::CaseInsensitive) != 0;
}

bool GameInfo::otherRegion() const {
	static const QRegularExpression demons_souls(QStringLiteral("^PPSA0134[02]$"), QRegularExpression::CaseInsensitiveOption);
	return paramRead && demons_souls.match(titleId).hasMatch() &&
	       (contentVersion == QStringLiteral("01.007.000") || contentVersion == QStringLiteral("01.005.000"));
}

bool GameInfo::isGamePath(const QString& path) {
	if (path.isEmpty()) return false;
	if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("eboot.bin")))) return true;
	const QFileInfo file(path);
	return path.endsWith(QStringLiteral(".zar"), Qt::CaseInsensitive) && file.isFile();
}

GameInfo GameInfo::read(const QString& path, const QString& tool) {
	GameInfo info;
	info.path   = path;
	info.isGame = isGamePath(path);
	info.isZar  = path.endsWith(QStringLiteral(".zar"), Qt::CaseInsensitive) && QFileInfo(path).isFile();
	if (path.isEmpty()) return info;
	const QDir    folder(path);
	const QString param = folder.filePath(QStringLiteral("sce_sys/param.json"));
	if (QFileInfo::exists(param)) {
		QFile file(param);
		if (file.open(QIODevice::ReadOnly)) info.paramRead = ParseParam(file.readAll(), info);
		const QString icon = folder.filePath(QStringLiteral("sce_sys/icon0.png"));
		const QString pic  = folder.filePath(QStringLiteral("sce_sys/pic0.png"));
		if (QFileInfo::exists(icon)) info.iconPath = icon;
		if (QFileInfo::exists(pic)) info.picPath = pic;
		return info;
	}
	if (!info.isZar || tool.isEmpty() || !QFileInfo::exists(tool)) return info;
	// kyty_shader_precompile --game <file.zar> --param <copy>
	QTemporaryDir temporary;
	if (!temporary.isValid()) return info;
	const QString copy = temporary.filePath(QStringLiteral("param.json"));
	QProcess      process;
	process.setProgram(tool);
	process.setArguments({QStringLiteral("--game"), QDir::toNativeSeparators(path), QStringLiteral("--param"),
	                      QDir::toNativeSeparators(copy)});
	process.setWorkingDirectory(QFileInfo(tool).absolutePath());
	process.start();
	if (!process.waitForFinished(30000)) {
		process.kill();
		process.waitForFinished(2000);
		return info;
	}
	if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
		QFile file(copy);
		if (file.open(QIODevice::ReadOnly)) info.paramRead = ParseParam(file.readAll(), info);
	}
	return info;
}
