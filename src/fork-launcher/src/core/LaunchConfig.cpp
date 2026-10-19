#include "core/LaunchConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// [string]$value in PowerShell: numbers as written, booleans as True/False.
QString ToText(const QJsonValue& value) {
	if (value.isString()) return value.toString();
	if (value.isBool()) return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
	if (value.isDouble()) return QString::number(value.toDouble(), 'g', 17);
	return {};
}

} // namespace

QString LaunchConfig::value(const QString& key) const {
	for (const auto& entry: environment) {
		if (entry.first.compare(key, Qt::CaseInsensitive) == 0) return entry.second;
	}
	return {};
}

LaunchConfig LaunchConfig::read(const QString& path) {
	LaunchConfig config;
	config.path = path;
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		config.error = QStringLiteral("cannot read the launch config %1").arg(path);
		return config;
	}
	QByteArray data = file.readAll();
	// A UTF-8 byte order mark (PowerShell's Set-Content -Encoding UTF8).
	if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
	QJsonParseError    error {};
	const QJsonDocument document = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		config.error = QStringLiteral("the launch config %1 is not valid JSON (%2)").arg(path, error.errorString());
		return config;
	}
	const QJsonObject json = document.object();
	// (QJsonObject keeps its keys sorted; the switches' order is the file's, so they are read from the text.)
	const QJsonObject environment = json.value(QStringLiteral("environment")).toObject();
	QStringList       order;
	{
		const int start = data.indexOf("\"environment\"");
		if (start >= 0) {
			const int open = data.indexOf('{', start);
			int       depth = 0;
			bool      in_string = false;
			QByteArray key;
			bool       reading_key = false;
			bool       expect_key  = true;
			for (int i = open; i >= 0 && i < data.size(); i++) {
				const char c = data.at(i);
				if (in_string) {
					if (c == '\\' && i + 1 < data.size()) {
						if (reading_key) key.append(data.mid(i, 2));
						i++;
					} else if (c == '"') {
						in_string = false;
						if (reading_key) {
							const QJsonDocument parsed = QJsonDocument::fromJson("[\"" + key + "\"]");
							order.append(parsed.array().at(0).toString());
							reading_key = false;
							expect_key  = false;
						}
					} else if (reading_key) {
						key.append(c);
					}
					continue;
				}
				if (c == '"') {
					in_string = true;
					if (depth == 1 && expect_key) {
						reading_key = true;
						key.clear();
					}
				} else if (c == '{' || c == '[') {
					depth++;
				} else if (c == '}' || c == ']') {
					depth--;
					if (depth == 0) break;
				} else if (c == ',' && depth == 1) {
					expect_key = true;
				}
			}
		}
	}
	for (const QString& key: order) {
		if (environment.contains(key)) config.environment.append({key, ToText(environment.value(key))});
	}
	for (auto it = environment.begin(); it != environment.end(); ++it) {
		if (!order.contains(it.key())) config.environment.append({it.key(), ToText(it.value())});
	}
	const QJsonValue command = json.value(QStringLiteral("command"));
	if (command.isArray()) {
		for (const QJsonValue& item: command.toArray()) config.command.append(ToText(item));
	} else if (!command.isUndefined() && !command.isNull()) {
		config.command.append(ToText(command));
	}
	const QJsonValue affinity = json.value(QStringLiteral("cpu_affinity"));
	if (affinity.isArray()) {
		for (const QJsonValue& item: affinity.toArray()) {
			bool      ok  = item.isDouble();
			const int cpu = ok ? item.toInt() : item.toString().toInt(&ok);
			if (ok) config.cpuAffinity.append(cpu);
		}
	} else if (affinity.isDouble()) {
		config.cpuAffinity.append(affinity.toInt());
	}
	return config;
}
