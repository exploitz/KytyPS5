#include "core/Package.h"

#include "platform/Platform.h"

#include <QDir>
#include <QFileInfo>

QString Package::path(const QString& relative) const {
	return QDir::cleanPath(QDir(root).filePath(relative));
}

Package Package::detect(const QString& root) {
	Package package;
	package.root      = QDir::cleanPath(QDir(root).absolutePath());
	package.buildTree = QFileInfo(package.path(QStringLiteral("_Build"))).isDir();
	const QString exe = Platform::emulatorExeName();
	package.emulator  = QFileInfo::exists(package.path(exe)) ? package.path(exe)
	                                                         : package.path(QStringLiteral("_Build/windows/") + exe);
	package.tool      = QDir(QFileInfo(package.emulator).absolutePath()).filePath(Platform::precompileExeName());
	for (const char* name: {"launch.json", "run-windows.json"}) {
		if (QFileInfo::exists(package.path(QString::fromLatin1(name)))) {
			package.config = package.path(QString::fromLatin1(name));
			break;
		}
	}
	package.passStates   = package.path(QStringLiteral("tools/local/static-precompile/pass-states.json"));
	package.logDir       = package.buildTree ? package.path(QStringLiteral("_Build/run-logs")) : package.path(QStringLiteral("logs"));
	package.settingsFile = package.path(QStringLiteral("launcher-settings.json"));
	package.dryRunFile   = package.path(QStringLiteral("dry-run.txt"));
	return package;
}

QStringList Package::missingFiles() const {
	QStringList missing;
	auto        relative = [&](const QString& file) { return QDir::toNativeSeparators(QDir(root).relativeFilePath(file)); };
	if (!QFileInfo::exists(emulator)) missing << Platform::emulatorExeName();
	if (!QFileInfo::exists(tool)) missing << Platform::precompileExeName();
	if (config.isEmpty()) missing << QStringLiteral("launch.json");
	if (!QFileInfo::exists(passStates)) missing << relative(passStates);
	const QString emulator_dir = QFileInfo(emulator).absolutePath();
	for (const QString& file: Platform::requiredRuntimeFiles()) {
		if (!QFileInfo::exists(QDir(emulator_dir).filePath(file))) missing << file;
	}
	return missing;
}

bool Package::hasDlssAddon() const {
	return QFileInfo::exists(path(QStringLiteral("plugins/dlss/kyty_dlss.dll"))) &&
	       QFileInfo::exists(path(QStringLiteral("plugins/dlss/nvngx_dlss.dll")));
}

bool Package::hasFsr4() const {
	// (The emulator reads deps\fsr4 relative to its working directory, the launcher's folder.)
	const QDir fsr4(path(QStringLiteral("deps/fsr4")));
	return fsr4.exists() && !fsr4.isEmpty(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
}

bool Package::hasFsrFrameGen() const {
	return QFileInfo::exists(path(QStringLiteral("deps/fsr/amd_fidelityfx_vk.dll"))) ||
	       QFileInfo::exists(QDir(QFileInfo(emulator).absolutePath()).filePath(QStringLiteral("deps/fsr/amd_fidelityfx_vk.dll")));
}

QString Package::readme() const {
	for (const char* name: {"README.md", "docs/PORTABLE-README.md"}) {
		if (QFileInfo::exists(path(QString::fromLatin1(name)))) return path(QString::fromLatin1(name));
	}
	return {};
}
