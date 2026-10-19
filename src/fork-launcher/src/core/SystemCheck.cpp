#include "core/SystemCheck.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryFile>

namespace {

constexpr double GB = 1024.0 * 1024.0 * 1024.0;

QString Lower(const QString& path) {
	return QDir::fromNativeSeparators(QDir::cleanPath(path)).toLower();
}

bool Inside(const QString& path, const QString& folder) {
	const QString p = Lower(path), f = Lower(folder);
	return !f.isEmpty() && (p == f || p.startsWith(f.endsWith('/') ? f : f + '/'));
}

} // namespace

QString formatGb(double bytes) {
	return QStringLiteral("%1 GB").arg(QLocale(QLocale::English).toString(bytes / GB, 'f', bytes < 10 * GB ? 1 : 0));
}

SystemInfo SystemInfo::gather(const Package& package, bool writeTest) {
	SystemInfo info;
	info.os      = Platform::osName();
	info.cpu     = Platform::cpuName();
	info.threads = Platform::logicalCpuCount();
	info.avx2    = Platform::hasAvx2();
	info.hybrid  = Platform::efficiencyMask() != 0;
	info.gpu     = Platform::gpu();
	info.memory  = Platform::memory();
	info.folder  = package.root;
	const QStorageInfo storage(package.root);
	if (storage.isValid()) {
		info.diskFree   = static_cast<double>(storage.bytesAvailable());
		info.diskTotal  = static_cast<double>(storage.bytesTotal());
		info.fileSystem = QString::fromLatin1(storage.fileSystemType());
	}
	for (const QString& root: Platform::oneDriveRoots()) {
		if (Inside(package.root, root)) info.inOneDrive = true;
	}
	// A zip opened in Explorer runs from a temporary extraction (Temp\Temp1_<name>.zip\, 7-Zip's 7zO..., WinRAR's
	// Rar$EX..., WinZip's wz...), or from the zip's own path.
	const QString path = Lower(package.root);
	if (path.contains(QStringLiteral(".zip/"))) info.inZip = true;
	if (Inside(package.root, QDir::tempPath())) {
		info.inTemp = true;
		const QString relative = Lower(QDir(QDir::tempPath()).relativeFilePath(package.root));
		static const QRegularExpression extraction(QStringLiteral("^(temp\\d+_.*|7zo.*|rar\\$ex.*|wz[0-9a-f].*)$"));
		if (extraction.match(relative.section('/', 0, 0)).hasMatch()) info.inZip = true;
	}
	if (writeTest) {
		QTemporaryFile file(QDir(package.root).filePath(QStringLiteral("kyty-write-test-XXXXXX.tmp")));
		info.writable    = file.open();
		info.writeTested = true;
	} else {
		info.writable = QFileInfo(package.root).isWritable();
	}
	return info;
}

QList<CheckItem> systemChecks(const SystemInfo& info) {
	using S = CheckItem::State;
	QList<CheckItem> items;
	// The processor: the release is built for AVX2 (x86-64-v3).
	if (info.avx2) {
		items.append({S::Ok, QStringLiteral("%1 · %2 threads").arg(info.cpu).arg(info.threads), {}});
	} else {
		items.append({S::Bad, QStringLiteral("%1 · no AVX2").arg(info.cpu),
		              QStringLiteral("The emulator needs a processor with AVX2 (Intel Core 4th generation, AMD Ryzen or newer). It won't start on this one.")});
	}
	// The graphics card.
	if (!info.gpu.found) {
		items.append({S::Bad, QStringLiteral("No graphics card found"), QStringLiteral("A graphics card with Vulkan 1.3 is needed. Install its latest driver.")});
	} else {
		const bool    nvidia = info.gpu.vendor == QStringLiteral("NVIDIA");
		const bool    enough = info.gpu.videoMemory == 0 || info.gpu.videoMemory >= 11 * GB;
		const QString memory = info.gpu.videoMemory > 0 ? QStringLiteral(" · %1").arg(formatGb(static_cast<double>(info.gpu.videoMemory))) : QString();
		QString       hint;
		if (!nvidia) hint = QStringLiteral("Tested on NVIDIA GeForce RTX cards; other cards are untested and may show problems.");
		else if (!enough) hint = QStringLiteral("12 GB of video memory is recommended; with less, busy areas run slower.");
		items.append({nvidia && enough ? S::Ok : S::Warn, info.gpu.name + memory, hint});
	}
	// RAM.
	const QString ram = QStringLiteral("%1 of RAM").arg(formatGb(info.memory.ram));
	if (info.memory.ram >= 30 * GB) items.append({S::Ok, ram, {}});
	else if (info.memory.ram >= 15 * GB)
		items.append({S::Warn, ram, QStringLiteral("The game runs, but loading takes longer. 32 GB is recommended.")});
	else items.append({S::Bad, ram, QStringLiteral("At least 16 GB is needed to run the game; 32 GB is recommended.")});
	// Commit: the emulator reserves about 34 GB of RAM plus page file.
	if (info.memory.commitKnown) {
		const QString line = QStringLiteral("Page file: %1 available for programs").arg(formatGb(info.memory.commitFree));
		if (info.memory.commitFree >= 34 * GB) items.append({S::Ok, line, {}});
		else
			items.append({S::Warn, line,
			              QStringLiteral("The emulator reserves about 34 GB. Set the page file to System managed size (System > About > "
			                             "Advanced system settings > Performance > Advanced > Virtual memory).")});
	}
	// Disk space in the launcher's folder: the caches take 15-20 GB.
	if (info.diskFree >= 0) {
		const QString line = QStringLiteral("%1 free on this drive").arg(formatGb(info.diskFree));
		if (info.diskFree >= 25 * GB) items.append({S::Ok, line, {}});
		else
			items.append({S::Warn, line, QStringLiteral("The shader caches take 15-20 GB in this folder. Free some space or move the folder to a bigger drive.")});
	}
	// The file system: the shader cache is one file of about 5.5 GB (FAT32: 4 GB at most).
	if (!info.fileSystem.isEmpty()) {
		const QString fs   = info.fileSystem.toUpper();
		const QString line = QStringLiteral("File system: %1").arg(info.fileSystem);
		if (fs.contains(QStringLiteral("FAT32")) || fs == QStringLiteral("VFAT") || fs == QStringLiteral("FAT")) {
			items.append({S::Warn, line, QStringLiteral("FAT32 can't hold the shader cache, a single file of about 5.5 GB. Move the folder to an NTFS drive.")});
		} else if (fs.contains(QStringLiteral("EXFAT"))) {
			items.append({S::Warn, line, QStringLiteral("exFAT is slow and easily damaged with the 5.5 GB shader cache file. An NTFS drive is better.")});
		} else {
			items.append({S::Ok, line, {}});
		}
	}
	// The folder itself.
	const QString folder = QStringLiteral("Folder: %1").arg(QDir::toNativeSeparators(info.folder));
	if (info.inZip) {
		items.append({S::Bad, QStringLiteral("Running from inside a zip file"),
		              QStringLiteral("Unzip the whole release first (right-click it > Extract All), then start the launcher from the new folder.")});
	} else if (!info.writable) {
		items.append({S::Bad, QStringLiteral("This folder is read-only"),
		              QStringLiteral("The emulator can't save its caches here. Move the folder somewhere you can write, such as C:\\Games\\KytyPS5.")});
	} else if (info.inOneDrive) {
		items.append({S::Warn, QStringLiteral("This folder is inside OneDrive"),
		              QStringLiteral("OneDrive would upload 15-20 GB of caches and can lock files while you play. Move the folder outside OneDrive.")});
	} else if (info.inTemp) {
		items.append({S::Warn, QStringLiteral("This folder is inside the Temp folder"),
		              QStringLiteral("Windows may clean up files here. Move the folder somewhere permanent, such as C:\\Games\\KytyPS5.")});
	} else {
		items.append({S::Ok, folder, {}});
	}
	return items;
}
