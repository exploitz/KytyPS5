#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// A launch config: launch.json next to the launcher (a release), else run-windows.json (a build tree). Its switches
// ("environment"), the Linux command line it was made from ("command": "--", the binary, then the options) and the
// CPUs to run on ("cpu_affinity", optional).
struct LaunchConfig {
	QString                        path;
	QList<QPair<QString, QString>> environment;
	QStringList                    command;
	QList<int>                     cpuAffinity;
	QString                        error; // empty when it was read

	[[nodiscard]] bool ok() const { return error.isEmpty(); }
	[[nodiscard]] QString value(const QString& key) const;

	static LaunchConfig read(const QString& path);
};
