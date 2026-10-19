#pragma once

// Everything the launcher asks of the operating system beyond Qt: one implementation per platform in Platform.cpp
// (Windows, Linux, and a safe fallback elsewhere).

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class QWidget;

namespace Platform {

// kyty_emulator.exe / kyty_emulator, kyty_shader_precompile.exe / kyty_shader_precompile.
QString emulatorExeName();
QString precompileExeName();
// The DLLs a release ships next to the emulator that it cannot start without (Windows: libwinpthread-1.dll).
QStringList requiredRuntimeFiles();

// Logical processors (run-windows.ps1's [Environment]::ProcessorCount).
int logicalCpuCount();
bool hasAvx2();
QString cpuName();

// A logical processor as Windows' CPU sets describe it (group 0).
struct CpuSet {
	int cpu             = 0;
	int core            = 0;
	int efficiencyClass = 0;
};
QList<CpuSet> cpuSets();
// run-windows.ps1 Get-PerformanceCpus: the performance cores of a hybrid CPU without the core of CPU 0, within
// `allowed`, as "1,2,3"; empty when every core is alike (and on Linux, where the threads are left to the OS).
QString performanceCpus(quint64 allowed);
// launcher.ps1 Get-EfficiencyMask: the efficiency cores of a hybrid CPU as an affinity mask, 0 when every core is alike.
quint64 efficiencyMask();

struct Memory {
	double ram        = 0; // bytes of RAM
	double commitFree = 0; // bytes programs can still commit (RAM plus page file), with a system-managed page file's room
	bool   commitKnown = false;
	bool   systemManagedPageFile = false;
};
// run-windows.ps1's memory check.
Memory memory();

struct Gpu {
	bool    found = false;
	QString name;
	QString vendor; // "NVIDIA", "AMD", "Intel" or ""
	QString driver;
	quint64 videoMemory = 0; // bytes, 0 when unknown
};
Gpu gpu();

QString osName();

// The last write time of a file in .NET ticks (100 ns since 0001-01-01, UTC): the program stamp run-windows.ps1 and
// precompile-windows.ps1 write next to a seed file ("<size> <ticks>"), so the launcher and the scripts agree on it.
qint64 fileWriteTicks(const QString& path);

void setProcessAffinity(qint64 pid, quint64 mask);
// precompile-windows.ps1: normal priority and power throttling (EcoQoS) off, so a windowless batch process is not
// kept on the efficiency cores. No-op elsewhere.
void setPrecompilePriority(qint64 pid);
// Whether the process has a visible top-level window; std::nullopt where this cannot be told.
std::optional<bool> processHasWindow(qint64 pid);
bool bringProcessToFront(qint64 pid);
// Running processes whose executable is named `exeName` (case-insensitive on Windows).
// `othersOnly`: not the launcher's own child processes (its shader status query, its play preparation).
QList<qint64> processesNamed(const QString& exeName, bool othersOnly = false);

// Single instance: the second launcher lets the first take the foreground.
void allowAnyProcessToSetForeground();
void forceForeground(QWidget* window);
// A dark title bar (and on Windows 11 the window colors) for a top-level window.
void styleTitleBar(QWidget* window);

// OneDrive's synced folders (empty elsewhere).
QStringList oneDriveRoots();

// Zip files through the system's own tools (Windows: tar.exe; Linux: bsdtar, unzip/zip). `found` is false when there
// is none.
struct Command {
	bool        found = false;
	QString     program;
	QStringList arguments;
	QString     workingDirectory;
};
// Extracts `zip` into `folder`.
Command zipExtractCommand(const QString& zip, const QString& folder);
// Makes `zip` of `files` (paths relative to `folder`).
Command zipCreateCommand(const QString& zip, const QString& folder, const QStringList& files);

// Shows the file selected in the file manager (Explorer), or opens its folder.
void revealInFileManager(const QString& path);
// --version and --dry-run print to the console that started a GUI program (Windows: attach to it).
void attachParentConsole();

} // namespace Platform
