#include "core/Settings.h"

#include "core/Features.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>

namespace {

const QStringList kOutputs   = {QStringLiteral("4k"), QStringLiteral("1080q"), QStringLiteral("1080f")};
const QStringList kQualities = {QStringLiteral("standard"), QStringLiteral("shadows"), QStringLiteral("low"),
                                QStringLiteral("lowest")};
const QStringList kAa        = {QStringLiteral("taa"), QStringLiteral("fsr4"), QStringLiteral("dlss")};
const QStringList kFramegen  = {QStringLiteral("off"), QStringLiteral("fsr")};
const QStringList kScreens   = {QStringLiteral("fullscreen"), QStringLiteral("1280x720"), QStringLiteral("1920x1080"),
                                QStringLiteral("2560x1440"), QStringLiteral("3840x2160")};
const QList<int>  kRenders   = {1440, 1296, 1152, 1080, 900};
const QStringList kRatios    = {QStringLiteral("16:9"), QStringLiteral("21:9"), QStringLiteral("32:9")};

// The JSON a PowerShell ConvertTo-Json or a hand edit makes: numbers and booleans may come as strings.
bool ToBool(const QJsonValue& value, bool fallback) {
	if (value.isBool()) return value.toBool();
	if (value.isDouble()) return value.toDouble() != 0;
	if (value.isString()) {
		const QString text = value.toString().trimmed().toLower();
		if (text == QStringLiteral("true") || text == QStringLiteral("1")) return true;
		if (text == QStringLiteral("false") || text == QStringLiteral("0")) return false;
	}
	return fallback;
}

int ToInt(const QJsonValue& value, int fallback) {
	if (value.isDouble()) return static_cast<int>(value.toDouble());
	if (value.isString()) {
		bool      ok     = false;
		const int number = value.toString().trimmed().toInt(&ok);
		if (ok) return number;
	}
	return fallback;
}

QString ToChoice(const QJsonValue& value, const QStringList& allowed, const QString& fallback) {
	const QString text = value.isDouble() ? QString::number(value.toDouble()) : value.toString().trimmed().toLower();
	return allowed.contains(text) ? text : fallback;
}

QString JsonString(const QString& text) {
	// A JSON string literal: the compact form of a one-element array without its brackets.
	const QByteArray array = QJsonDocument(QJsonArray {text}).toJson(QJsonDocument::Compact);
	return QString::fromUtf8(array.mid(1, array.size() - 2));
}

} // namespace

SettingsStore::SettingsStore(QString path, bool readOnly): m_path(std::move(path)), m_read_only(readOnly) {}

bool SettingsStore::valid(const LauncherSettings& s) {
	return kOutputs.contains(s.output) && kQualities.contains(s.quality) && kAa.contains(s.aa) &&
	       kFramegen.contains(s.framegen) && kScreens.contains(s.screen) && (s.fps == 60 || s.fps == 120) &&
	       kRenders.contains(s.render) && kRatios.contains(s.ratio) && s.language >= 0 && s.language <= 29 && s.hud >= 0 && s.hud <= 2;
}

void SettingsStore::load() {
	m_settings = {};
	m_reset    = false;
	QFile file(m_path);
	if (file.exists()) {
		QByteArray data;
		if (file.open(QIODevice::ReadOnly)) data = file.readAll();
		file.close();
		// PowerShell's Set-Content -Encoding UTF8 writes a byte order mark.
		if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
		QJsonParseError    error {};
		const QJsonDocument document = QJsonDocument::fromJson(data, &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			m_reset = true;
			if (!m_read_only) {
				const QString bad = m_path + QStringLiteral(".bad");
				QFile::remove(bad);
				QFile::rename(m_path, bad);
			}
		} else {
			const QJsonObject json = document.object();
			LauncherSettings& s    = m_settings;
			s.game                 = json.value(QStringLiteral("game")).toString().trimmed();
			s.setupDone            = ToBool(json.value(QStringLiteral("setupDone")), s.setupDone);
			s.output               = ToChoice(json.value(QStringLiteral("output")), kOutputs, s.output);
			s.quality              = ToChoice(json.value(QStringLiteral("quality")), kQualities, s.quality);
			s.aa                   = ToChoice(json.value(QStringLiteral("aa")), kAa, s.aa);
			s.framegen             = ToChoice(json.value(QStringLiteral("framegen")), kFramegen, s.framegen);
			s.screen               = ToChoice(json.value(QStringLiteral("screen")), kScreens, s.screen);
			s.aspect               = ToBool(json.value(QStringLiteral("aspect")), s.aspect);
			const int fps          = ToInt(json.value(QStringLiteral("fps")), s.fps);
			s.fps                  = fps == 120 ? 120 : 60;
			s.vsync                = ToBool(json.value(QStringLiteral("vsync")), s.vsync);
			s.hdr                  = ToBool(json.value(QStringLiteral("hdr")), s.hdr);
			const int render       = ToInt(json.value(QStringLiteral("render")), s.render);
			s.render               = kRenders.contains(render) ? render : 1440;
			s.ratio                = ToChoice(json.value(QStringLiteral("ratio")), kRatios, s.ratio);
			const int language     = ToInt(json.value(QStringLiteral("language")), s.language);
			s.language             = language >= 0 && language <= 29 ? language : 1;
			s.redzone              = ToBool(json.value(QStringLiteral("redzone")), s.redzone);
			s.ecores               = ToBool(json.value(QStringLiteral("ecores")), s.ecores);
			const int hud          = ToInt(json.value(QStringLiteral("hud")), s.hud);
			s.hud                  = hud >= 0 && hud <= 2 ? hud : 0;
			s.windowX              = ToInt(json.value(QStringLiteral("windowX")), 0);
			s.windowY              = ToInt(json.value(QStringLiteral("windowY")), 0);
			s.windowW              = std::max(0, ToInt(json.value(QStringLiteral("windowW")), 0));
			s.windowH              = std::max(0, ToInt(json.value(QStringLiteral("windowH")), 0));
			// (No DLSS add-on in this release: the game's TAA.)
			if (!kDlssAddonSupported && s.aa == QStringLiteral("dlss")) s.aa = QStringLiteral("taa");
		}
	}
	// The game a run-windows.ps1 launch remembered (run.cmd), when the launcher has none (launcher.ps1).
	if (m_settings.game.isEmpty()) {
		QFile remembered(QFileInfo(m_path).dir().filePath(QStringLiteral("game-path.txt")));
		if (remembered.open(QIODevice::ReadOnly)) {
			QByteArray line = remembered.readLine();
			if (line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);
			m_settings.game = QString::fromUtf8(line).trimmed();
		}
	}
}

bool SettingsStore::save() {
	if (m_read_only) return true;
	const LauncherSettings& s = m_settings;
	auto                    b = [](bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };
	QStringList             lines;
	lines << QStringLiteral("  \"game\": %1").arg(JsonString(s.game)) << QStringLiteral("  \"setupDone\": %1").arg(b(s.setupDone))
	      << QStringLiteral("  \"output\": %1").arg(JsonString(s.output))
	      << QStringLiteral("  \"quality\": %1").arg(JsonString(s.quality))
	      << QStringLiteral("  \"aa\": %1").arg(JsonString(s.aa)) << QStringLiteral("  \"framegen\": %1").arg(JsonString(s.framegen))
	      << QStringLiteral("  \"screen\": %1").arg(JsonString(s.screen)) << QStringLiteral("  \"aspect\": %1").arg(b(s.aspect))
	      << QStringLiteral("  \"fps\": %1").arg(s.fps) << QStringLiteral("  \"vsync\": %1").arg(b(s.vsync))
	      << QStringLiteral("  \"hdr\": %1").arg(b(s.hdr)) << QStringLiteral("  \"render\": %1").arg(s.render)
	      << QStringLiteral("  \"ratio\": %1").arg(JsonString(s.ratio))
	      << QStringLiteral("  \"language\": %1").arg(s.language) << QStringLiteral("  \"redzone\": %1").arg(b(s.redzone))
	      << QStringLiteral("  \"ecores\": %1").arg(b(s.ecores)) << QStringLiteral("  \"hud\": %1").arg(s.hud)
	      << QStringLiteral("  \"windowX\": %1").arg(s.windowX) << QStringLiteral("  \"windowY\": %1").arg(s.windowY)
	      << QStringLiteral("  \"windowW\": %1").arg(s.windowW) << QStringLiteral("  \"windowH\": %1").arg(s.windowH);
	QSaveFile file(m_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
	file.write((QStringLiteral("{\n") + lines.join(QStringLiteral(",\n")) + QStringLiteral("\n}\n")).toUtf8());
	return file.commit();
}
