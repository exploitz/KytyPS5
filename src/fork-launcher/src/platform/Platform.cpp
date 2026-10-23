#include "platform/Platform.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <cstring>

#if defined(Q_OS_WIN)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <intrin.h>
#include <tlhelp32.h>
#include <cstdio>
#elif defined(Q_OS_LINUX)
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

namespace Platform {

namespace {

constexpr double GB = 1024.0 * 1024.0 * 1024.0;

// --- x86 CPUID (the processor's name; AVX2 where the OS does not say) ---------------------------------------------
#if defined(Q_PROCESSOR_X86)
void Cpuid(unsigned leaf, unsigned sub, unsigned regs[4]) {
#if defined(_MSC_VER)
	int r[4] = {};
	__cpuidex(r, static_cast<int>(leaf), static_cast<int>(sub));
	for (int i = 0; i < 4; i++) regs[i] = static_cast<unsigned>(r[i]);
#else
	__cpuid_count(leaf, sub, regs[0], regs[1], regs[2], regs[3]);
#endif
}

QString CpuidBrand() {
	unsigned r[4] = {};
	Cpuid(0x80000000u, 0, r);
	if (r[0] < 0x80000004u) return {};
	char brand[49] = {};
	for (unsigned i = 0; i < 3; i++) {
		Cpuid(0x80000002u + i, 0, r);
		std::memcpy(brand + 16 * i, r, 16);
	}
	return QString::fromLatin1(brand).simplified();
}

bool CpuidAvx2() {
	unsigned r[4] = {};
	Cpuid(0, 0, r);
	if (r[0] < 7) return false;
	Cpuid(1, 0, r);
	const bool osxsave = (r[2] & (1u << 27)) != 0;
	const bool avx     = (r[2] & (1u << 28)) != 0;
	if (!osxsave || !avx) return false;
#if defined(_MSC_VER)
	const unsigned long long xcr0 = _xgetbv(0);
#else
	unsigned eax = 0, edx = 0;
	__asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
	const unsigned long long xcr0 = (static_cast<unsigned long long>(edx) << 32) | eax;
#endif
	if ((xcr0 & 6) != 6) return false; // the OS saves the YMM registers
	Cpuid(7, 0, r);
	return (r[1] & (1u << 5)) != 0;
}
#else
QString CpuidBrand() { return {}; }
bool    CpuidAvx2() { return false; }
#endif

#if defined(Q_OS_LINUX)
QString ReadText(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
	return QString::fromUtf8(file.readAll());
}

// "0-3,8,10-11" -> {0,1,2,3,8,10,11}
QList<int> ParseCpuList(const QString& text) {
	QList<int> cpus;
	for (const QString& part: text.trimmed().split(',', Qt::SkipEmptyParts)) {
		const auto range = part.split('-');
		bool       ok1 = false, ok2 = false;
		const int  from = range.value(0).toInt(&ok1);
		const int  to   = range.size() > 1 ? range.value(1).toInt(&ok2) : from;
		if (!ok1 || (range.size() > 1 && !ok2)) continue;
		for (int cpu = from; cpu <= to; cpu++) cpus.append(cpu);
	}
	return cpus;
}

// A program's output, or empty when it is missing, fails or takes too long.
QString RunQuiet(const QString& program, const QStringList& arguments, int timeoutMs) {
	const QString path = QStandardPaths::findExecutable(program);
	if (path.isEmpty()) return {};
	QProcess process;
	process.start(path, arguments);
	if (!process.waitForFinished(timeoutMs)) {
		process.kill();
		process.waitForFinished(1000);
		return {};
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
	return QString::fromUtf8(process.readAllStandardOutput());
}
#endif

#if defined(Q_OS_WIN)
QString RegistryString(HKEY root, const wchar_t* key, const wchar_t* value, DWORD type) {
	DWORD size = 0;
	if (RegGetValueW(root, key, value, type, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) return {};
	std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
	if (RegGetValueW(root, key, value, type, nullptr, buffer.data(), &size) != ERROR_SUCCESS) return {};
	QString text = QString::fromWCharArray(buffer.data(), static_cast<int>(size / sizeof(wchar_t)));
	// (The size counts the terminating null.)
	while (text.endsWith(QChar(u'\0'))) text.chop(1);
	return text;
}

HWND FindProcessWindow(qint64 pid) {
	struct Search {
		DWORD pid;
		HWND  window;
	} search {static_cast<DWORD>(pid), nullptr};
	EnumWindows(
	    [](HWND window, LPARAM parameter) -> BOOL {
		    auto* s = reinterpret_cast<Search*>(parameter);
		    DWORD owner_pid = 0;
		    GetWindowThreadProcessId(window, &owner_pid);
		    if (owner_pid != s->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
		    RECT rect {};
		    if (!GetWindowRect(window, &rect) || rect.right - rect.left < 64 || rect.bottom - rect.top < 64) return TRUE;
		    s->window = window;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search));
	return search.window;
}
#endif

} // namespace

QString emulatorExeName() {
#if defined(Q_OS_WIN)
	return QStringLiteral("kyty_emulator.exe");
#else
	return QStringLiteral("kyty_emulator");
#endif
}

QString precompileExeName() {
#if defined(Q_OS_WIN)
	return QStringLiteral("kyty_shader_precompile.exe");
#else
	return QStringLiteral("kyty_shader_precompile");
#endif
}

QStringList requiredRuntimeFiles() {
#if defined(Q_OS_WIN)
	return {QStringLiteral("libwinpthread-1.dll")};
#else
	return {};
#endif
}

int logicalCpuCount() {
#if defined(Q_OS_WIN)
	SYSTEM_INFO info {};
	GetSystemInfo(&info);
	if (info.dwNumberOfProcessors > 0) return static_cast<int>(info.dwNumberOfProcessors);
#elif defined(Q_OS_LINUX)
	const long count = sysconf(_SC_NPROCESSORS_ONLN);
	if (count > 0) return static_cast<int>(count);
#endif
	return std::max(1, QThread::idealThreadCount());
}

bool hasAvx2() {
#if defined(Q_OS_WIN)
	// PF_AVX2_INSTRUCTIONS_AVAILABLE (40); older Windows 10 builds do not know it, so CPUID decides then.
	return IsProcessorFeaturePresent(40) || CpuidAvx2();
#elif defined(Q_OS_LINUX)
	const QString info = ReadText(QStringLiteral("/proc/cpuinfo"));
	if (!info.isEmpty()) {
		static const QRegularExpression flags(QStringLiteral("^flags\\s*:(.*)$"), QRegularExpression::MultilineOption);
		const auto match = flags.match(info);
		if (match.hasMatch()) return match.captured(1).split(' ', Qt::SkipEmptyParts).contains(QStringLiteral("avx2"));
	}
	return CpuidAvx2();
#else
	return CpuidAvx2();
#endif
}

QString cpuName() {
	QString name;
#if defined(Q_OS_WIN)
	name = RegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ);
#elif defined(Q_OS_LINUX)
	static const QRegularExpression model(QStringLiteral("^model name\\s*:\\s*(.*)$"), QRegularExpression::MultilineOption);
	const auto match = model.match(ReadText(QStringLiteral("/proc/cpuinfo")));
	if (match.hasMatch()) name = match.captured(1);
#endif
	if (name.trimmed().isEmpty()) name = CpuidBrand();
	if (name.trimmed().isEmpty()) return QStringLiteral("Unknown processor");
	// "12th Gen Intel(R) Core(TM) i9-12900K" -> "12th Gen Intel Core i9-12900K"
	static const QRegularExpression marks(QStringLiteral("\\((R|TM|tm|r)\\)"));
	return name.remove(marks).remove(QChar(u'\0')).simplified();
}

QList<CpuSet> cpuSets() {
	QList<CpuSet> sets;
#if defined(Q_OS_WIN)
	ULONG length = 0;
	GetSystemCpuSetInformation(nullptr, 0, &length, nullptr, 0);
	if (length == 0) return sets;
	std::vector<char> buffer(length);
	auto*             first = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data());
	if (!GetSystemCpuSetInformation(first, length, &length, nullptr, 0)) return sets;
	for (ULONG offset = 0; offset < length;) {
		const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
		if (info->Size == 0) break;
		// Group 0's logical processors, as run-windows.ps1 reads them.
		if (info->Type == CpuSetInformation && info->CpuSet.Group == 0) {
			sets.append({info->CpuSet.LogicalProcessorIndex, info->CpuSet.CoreIndex, info->CpuSet.EfficiencyClass});
		}
		offset += info->Size;
	}
#elif defined(Q_OS_LINUX)
	// A hybrid Intel CPU lists its kinds of cores (cpu_core: performance, cpu_atom: efficiency).
	const QList<int> atom = ParseCpuList(ReadText(QStringLiteral("/sys/devices/cpu_atom/cpus")));
	const QList<int> core = ParseCpuList(ReadText(QStringLiteral("/sys/devices/cpu_core/cpus")));
	for (int cpu = 0; cpu < logicalCpuCount(); cpu++) {
		bool      ok = false;
		const int core_id =
		    ReadText(QStringLiteral("/sys/devices/system/cpu/cpu%1/topology/core_id").arg(cpu)).trimmed().toInt(&ok);
		const int efficiency = atom.contains(cpu) ? 0 : (core.contains(cpu) || atom.isEmpty() ? 1 : 0);
		sets.append({cpu, ok ? core_id : cpu, efficiency});
	}
#endif
	return sets;
}

QString performanceCpus(quint64 allowed) {
#if defined(Q_OS_WIN)
	const auto sets = cpuSets();
	QSet<int>  classes;
	for (const auto& set: sets) classes.insert(set.efficiencyClass);
	if (classes.size() < 2) return {};
	const int fastest    = *std::max_element(classes.begin(), classes.end());
	int       interrupts = -1;
	for (const auto& set: sets) {
		if (set.cpu == 0) interrupts = set.core;
	}
	QStringList cpus;
	for (const auto& set: sets) {
		if (set.efficiencyClass == fastest && set.core != interrupts && set.cpu < 64 &&
		    (allowed & (quint64(1) << set.cpu)) != 0) {
			cpus.append(QString::number(set.cpu));
		}
	}
	return cpus.join(',');
#else
	// Linux: the recording and render threads are left to the scheduler (the 'auto' switches are removed).
	Q_UNUSED(allowed);
	return {};
#endif
}

quint64 efficiencyMask() {
	const auto sets = cpuSets();
	QSet<int>  classes;
	for (const auto& set: sets) classes.insert(set.efficiencyClass);
	if (classes.size() < 2) return 0;
	const int slowest = *std::min_element(classes.begin(), classes.end());
	quint64   mask    = 0;
	for (const auto& set: sets) {
		if (set.efficiencyClass == slowest && set.cpu < 63) mask |= quint64(1) << set.cpu;
	}
	return mask;
}

Memory memory() {
	Memory memory;
#if defined(Q_OS_WIN)
	MEMORYSTATUSEX status {};
	status.dwLength = sizeof(status);
	if (!GlobalMemoryStatusEx(&status)) return memory;
	memory.ram         = static_cast<double>(status.ullTotalPhys);
	// The installed memory, not what Windows has after the integrated graphics' reservation (16 GB shows as 12-14 there).
	if (ULONGLONG installed_kb = 0; GetPhysicallyInstalledSystemMemory(&installed_kb)) {
		memory.ram = std::max(memory.ram, static_cast<double>(installed_kb) * 1024.0);
	}
	memory.commitFree  = static_cast<double>(status.ullAvailPageFile); // Win32_OperatingSystem.FreeVirtualMemory
	memory.commitKnown = true;
	// Win32_ComputerSystem.AutomaticManagedPagefile: "?:\pagefile.sys" in the paging files.
	const QString paging = RegistryString(HKEY_LOCAL_MACHINE,
	                                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
	                                      L"PagingFiles", RRF_RT_REG_MULTI_SZ);
	for (const QString& entry: paging.split(QChar(u'\0'), Qt::SkipEmptyParts)) {
		if (entry.trimmed().startsWith(QStringLiteral("?:\\"))) memory.systemManagedPageFile = true;
	}
	if (memory.systemManagedPageFile) {
		// A system-managed page file grows to 3 x RAM (at most an eighth of its disk) while the disk has room
		// (run-windows.ps1). The page files' current size: Win32_PageFileUsage.AllocatedBaseSize, from
		// NtQuerySystemInformation(SystemPageFileInformation).
		double paged = 0;
		using Query  = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);
		struct PageFileInformation {
			ULONG  next;
			ULONG  total_pages;
			ULONG  in_use;
			ULONG  peak;
			USHORT name_length;
			USHORT name_capacity;
			PWSTR  name;
		};
		auto* query = reinterpret_cast<Query>(
		    reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
		SYSTEM_INFO system {};
		GetSystemInfo(&system);
		std::vector<char> buffer(64 * 1024);
		ULONG             returned = 0;
		if (query != nullptr && query(18, buffer.data(), static_cast<ULONG>(buffer.size()), &returned) >= 0 && returned > 0) {
			for (size_t offset = 0;;) {
				const auto* info = reinterpret_cast<const PageFileInformation*>(buffer.data() + offset);
				paged += static_cast<double>(info->total_pages) * system.dwPageSize;
				if (info->next == 0) break;
				offset += info->next;
			}
		}
		wchar_t drive[MAX_PATH] = L"C:";
		GetEnvironmentVariableW(L"SystemDrive", drive, MAX_PATH);
		const QStorageInfo disk(QString::fromWCharArray(drive) + QStringLiteral("/"));
		if (disk.isValid()) {
			const double size = static_cast<double>(disk.bytesTotal());
			const double free = static_cast<double>(disk.bytesFree());
			const double room = std::min(std::min(3 * memory.ram, size / 8) - paged, free - 5 * GB);
			memory.commitFree += std::max(0.0, room);
		}
	}
#elif defined(Q_OS_LINUX)
	const QString info = ReadText(QStringLiteral("/proc/meminfo"));
	auto          field = [&](const char* name) {
        const QRegularExpression pattern(QStringLiteral("^%1:\\s*(\\d+) kB").arg(QLatin1String(name)),
		                                          QRegularExpression::MultilineOption);
        const auto               match = pattern.match(info);
        return match.hasMatch() ? match.captured(1).toDouble() * 1024 : 0.0;
	};
	memory.ram = field("MemTotal");
	// Linux overcommits by default: no commit limit to warn about unless strict accounting is on.
	if (ReadText(QStringLiteral("/proc/sys/vm/overcommit_memory")).trimmed() == QStringLiteral("2")) {
		memory.commitKnown = true;
		memory.commitFree  = std::max(0.0, field("CommitLimit") - field("Committed_AS"));
	}
#endif
	return memory;
}

Gpu gpu() {
	Gpu gpu;
#if defined(Q_OS_WIN)
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return gpu;
	static const QRegularExpression ignored(QStringLiteral("Basic Display|Basic Render|Remote|Virtual|Parsec|Mirage"));
	int                             best_score = -1;
	IDXGIAdapter1*                  adapter    = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 desc {};
		if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
			adapter->Release();
			continue;
		}
		const QString name = QString::fromWCharArray(desc.Description).simplified();
		if (name.contains(ignored)) {
			adapter->Release();
			continue;
		}
		QString vendor;
		if (desc.VendorId == 0x10DE) vendor = QStringLiteral("NVIDIA");
		else if (desc.VendorId == 0x1002 || desc.VendorId == 0x1022) vendor = QStringLiteral("AMD");
		else if (desc.VendorId == 0x8086) vendor = QStringLiteral("Intel");
		// Discrete cards first (the PowerShell launcher's order: NVIDIA, AMD, Intel Arc), then the most video memory.
		const bool discrete = vendor == QStringLiteral("NVIDIA") || vendor == QStringLiteral("AMD") ||
		                      name.contains(QStringLiteral("Arc"));
		const int  score    = (discrete ? 1 << 20 : 0) + static_cast<int>(desc.DedicatedVideoMemory >> 24);
		if (score > best_score) {
			best_score      = score;
			gpu.found       = true;
			gpu.name        = name;
			gpu.vendor      = vendor;
			gpu.videoMemory = desc.DedicatedVideoMemory;
			gpu.driver.clear();
			LARGE_INTEGER version {};
			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
				const unsigned a = HIWORD(version.HighPart), b = LOWORD(version.HighPart);
				const unsigned c = HIWORD(version.LowPart), d = LOWORD(version.LowPart);
				gpu.driver       = QStringLiteral("%1.%2.%3.%4").arg(a).arg(b).arg(c).arg(d);
				if (vendor == QStringLiteral("NVIDIA")) {
					// 32.0.15.6094 is NVIDIA's 560.94.
					const unsigned nv = (c % 10) * 10000 + d;
					gpu.driver        = QStringLiteral("%1.%2").arg(nv / 100).arg(nv % 100, 2, 10, QChar(u'0'));
				}
			}
		}
		adapter->Release();
	}
	factory->Release();
#elif defined(Q_OS_LINUX)
	// nvidia-smi, else vulkaninfo, else the kernel's DRM devices (best effort: no lspci).
	const QString smi = RunQuiet(QStringLiteral("nvidia-smi"),
	                             {QStringLiteral("--query-gpu=name,driver_version,memory.total"),
	                              QStringLiteral("--format=csv,noheader,nounits")},
	                             3000);
	if (!smi.trimmed().isEmpty()) {
		const auto fields = smi.split('\n').value(0).split(',');
		gpu.found         = true;
		gpu.vendor        = QStringLiteral("NVIDIA");
		gpu.name          = fields.value(0).trimmed();
		gpu.driver        = fields.value(1).trimmed();
		gpu.videoMemory   = static_cast<quint64>(fields.value(2).trimmed().toDouble() * 1024 * 1024);
		return gpu;
	}
	const QString vulkan = RunQuiet(QStringLiteral("vulkaninfo"), {QStringLiteral("--summary")}, 5000);
	if (!vulkan.isEmpty()) {
		static const QRegularExpression name(QStringLiteral("deviceName\\s*=\\s*(.+)"));
		static const QRegularExpression driver(QStringLiteral("driverInfo\\s*=\\s*(.+)"));
		auto                            names   = name.globalMatch(vulkan);
		auto                            drivers = driver.globalMatch(vulkan);
		while (names.hasNext()) {
			const QString device = names.next().captured(1).trimmed();
			const QString info   = drivers.hasNext() ? drivers.next().captured(1).trimmed() : QString();
			if (device.contains(QStringLiteral("llvmpipe"))) continue;
			gpu.found  = true;
			gpu.name   = device;
			gpu.driver = info;
			break;
		}
	}
	const QDir drm(QStringLiteral("/sys/class/drm"));
	for (const QString& card: drm.entryList({QStringLiteral("card?")}, QDir::Dirs | QDir::System)) {
		const QString device = drm.filePath(card) + QStringLiteral("/device/");
		const QString vendor = ReadText(device + QStringLiteral("vendor")).trimmed();
		if (vendor.isEmpty()) continue;
		if (vendor == QStringLiteral("0x10de")) gpu.vendor = QStringLiteral("NVIDIA");
		else if (vendor == QStringLiteral("0x1002")) gpu.vendor = QStringLiteral("AMD");
		else if (vendor == QStringLiteral("0x8086")) gpu.vendor = QStringLiteral("Intel");
		const double vram = ReadText(device + QStringLiteral("mem_info_vram_total")).trimmed().toDouble();
		if (vram > 0) gpu.videoMemory = static_cast<quint64>(vram);
		if (!gpu.found) {
			gpu.found = true;
			gpu.name  = QStringLiteral("%1 graphics card (device %2)")
			               .arg(gpu.vendor.isEmpty() ? QStringLiteral("Unknown") : gpu.vendor,
			                    ReadText(device + QStringLiteral("device")).trimmed());
		}
		if (gpu.vendor == QStringLiteral("NVIDIA") || gpu.vendor == QStringLiteral("AMD")) break;
	}
	if (gpu.found && gpu.vendor.isEmpty()) {
		if (gpu.name.contains(QStringLiteral("NVIDIA"))) gpu.vendor = QStringLiteral("NVIDIA");
		else if (gpu.name.contains(QStringLiteral("AMD")) || gpu.name.contains(QStringLiteral("Radeon")))
			gpu.vendor = QStringLiteral("AMD");
		else if (gpu.name.contains(QStringLiteral("Intel"))) gpu.vendor = QStringLiteral("Intel");
	}
#endif
	return gpu;
}

QString osName() {
	return QStringLiteral("%1 (%2 %3, %4)")
	    .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
	         QSysInfo::currentCpuArchitecture());
}

qint64 fileWriteTicks(const QString& path) {
#if defined(Q_OS_WIN)
	WIN32_FILE_ATTRIBUTE_DATA data {};
	if (!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
	                          GetFileExInfoStandard, &data)) {
		return 0;
	}
	ULARGE_INTEGER time {};
	time.LowPart  = data.ftLastWriteTime.dwLowDateTime;
	time.HighPart = data.ftLastWriteTime.dwHighDateTime;
	// FILETIME counts from 1601-01-01, .NET ticks from 0001-01-01.
	return static_cast<qint64>(time.QuadPart) + 504911232000000000LL;
#elif defined(Q_OS_LINUX)
	struct stat info {};
	if (stat(QFile::encodeName(path).constData(), &info) != 0) return 0;
	return 621355968000000000LL + static_cast<qint64>(info.st_mtim.tv_sec) * 10000000LL + info.st_mtim.tv_nsec / 100;
#else
	const QFileInfo info(path);
	return info.exists() ? 621355968000000000LL + info.lastModified().toMSecsSinceEpoch() * 10000LL : 0;
#endif
}

void setProcessAffinity(qint64 pid, quint64 mask) {
	if (pid <= 0 || mask == 0) return;
#if defined(Q_OS_WIN)
	HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr) return;
	SetProcessAffinityMask(process, static_cast<DWORD_PTR>(mask));
	CloseHandle(process);
#elif defined(Q_OS_LINUX)
	cpu_set_t set;
	CPU_ZERO(&set);
	for (int cpu = 0; cpu < 64; cpu++) {
		if ((mask & (quint64(1) << cpu)) != 0) CPU_SET(cpu, &set);
	}
	sched_setaffinity(static_cast<pid_t>(pid), sizeof(set), &set);
#endif
}

void setPrecompilePriority(qint64 pid) {
#if defined(Q_OS_WIN)
	HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr) return;
	// Normal priority: Windows parks below-normal processes on the E-cores of a hybrid CPU.
	SetPriorityClass(process, NORMAL_PRIORITY_CLASS);
	PROCESS_POWER_THROTTLING_STATE state {};
	state.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask   = 0;
	SetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state));
	CloseHandle(process);
#else
	Q_UNUSED(pid);
#endif
}

std::optional<bool> processHasWindow(qint64 pid) {
#if defined(Q_OS_WIN)
	return FindProcessWindow(pid) != nullptr;
#else
	Q_UNUSED(pid);
	return std::nullopt;
#endif
}

bool bringProcessToFront(qint64 pid) {
#if defined(Q_OS_WIN)
	HWND window = FindProcessWindow(pid);
	if (window == nullptr) return false;
	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
	return SetForegroundWindow(window) != FALSE;
#else
	Q_UNUSED(pid);
	return false;
#endif
}

QList<qint64> processesNamed(const QString& exeName, bool othersOnly) {
	QList<qint64> pids;
#if defined(Q_OS_WIN)
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return pids;
	PROCESSENTRY32W entry {};
	entry.dwSize = sizeof(entry);
	const DWORD self = GetCurrentProcessId();
	for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
		if (othersOnly && entry.th32ParentProcessID == self) continue;
		if (QString::fromWCharArray(entry.szExeFile).compare(exeName, Qt::CaseInsensitive) == 0) {
			pids.append(entry.th32ProcessID);
		}
	}
	CloseHandle(snapshot);
#elif defined(Q_OS_LINUX)
	const QDir proc(QStringLiteral("/proc"));
	for (const QString& entry: proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		bool         ok  = false;
		const qint64 pid = entry.toLongLong(&ok);
		if (!ok) continue;
		QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
		if (exe.isEmpty()) {
			QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
			if (cmdline.open(QIODevice::ReadOnly)) exe = QString::fromLocal8Bit(cmdline.readAll().split('\0').value(0));
		}
		if (QFileInfo(exe).fileName() != exeName) continue;
		if (othersOnly) {
			QFile status(QStringLiteral("/proc/%1/status").arg(pid));
			if (status.open(QIODevice::ReadOnly) &&
			    QString::fromLatin1(status.readAll()).contains(QStringLiteral("\nPPid:\t%1\n").arg(QCoreApplication::applicationPid())))
				continue;
		}
		pids.append(pid);
	}
#else
	Q_UNUSED(exeName);
#endif
	return pids;
}

void allowAnyProcessToSetForeground() {
#if defined(Q_OS_WIN)
	AllowSetForegroundWindow(ASFW_ANY);
#endif
}

void forceForeground(QWidget* window) {
	if (window == nullptr) return;
	window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	window->show();
	window->raise();
	window->activateWindow();
#if defined(Q_OS_WIN)
	HWND handle = reinterpret_cast<HWND>(window->winId());
	if (IsIconic(handle)) ShowWindow(handle, SW_RESTORE);
	SetForegroundWindow(handle);
#endif
}

void styleTitleBar(QWidget* window) {
#if defined(Q_OS_WIN)
	if (window == nullptr) return;
	HWND handle = reinterpret_cast<HWND>(window->winId());
	BOOL dark   = TRUE;
	// DWMWA_USE_IMMERSIVE_DARK_MODE (20; 19 before Windows 10 20H1).
	if (FAILED(DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark)))) DwmSetWindowAttribute(handle, 19, &dark, sizeof(dark));
	// Windows 11: the caption in the window's own color (DWMWA_BORDER_COLOR, _CAPTION_COLOR, _TEXT_COLOR).
	const COLORREF border = RGB(0x2E, 0x2E, 0x2E), caption = RGB(0x12, 0x12, 0x12), text = RGB(0xED, 0xED, 0xED);
	DwmSetWindowAttribute(handle, 34, &border, sizeof(border));
	DwmSetWindowAttribute(handle, 35, &caption, sizeof(caption));
	DwmSetWindowAttribute(handle, 36, &text, sizeof(text));
#else
	Q_UNUSED(window);
#endif
}

QStringList oneDriveRoots() {
	QStringList roots;
#if defined(Q_OS_WIN)
	for (const char* name: {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
		const QString value = QDir::fromNativeSeparators(qEnvironmentVariable(name)).trimmed();
		if (!value.isEmpty() && !roots.contains(value, Qt::CaseInsensitive)) roots.append(value);
	}
#endif
	return roots;
}

Command zipExtractCommand(const QString& zip, const QString& folder) {
	Command command;
#if defined(Q_OS_WIN)
	const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))).filePath(QStringLiteral("System32/tar.exe"));
	if (QFileInfo::exists(tar)) {
		command = {true, tar, {QStringLiteral("-xf"), QDir::toNativeSeparators(zip), QStringLiteral("-C"), QDir::toNativeSeparators(folder)}, {}};
	}
#else
	if (const QString bsdtar = QStandardPaths::findExecutable(QStringLiteral("bsdtar")); !bsdtar.isEmpty()) {
		command = {true, bsdtar, {QStringLiteral("-xf"), zip, QStringLiteral("-C"), folder}, {}};
	} else if (const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip")); !unzip.isEmpty()) {
		command = {true, unzip, {QStringLiteral("-o"), QStringLiteral("-q"), zip, QStringLiteral("-d"), folder}, {}};
	}
#endif
	return command;
}

Command zipCreateCommand(const QString& zip, const QString& folder, const QStringList& files) {
	Command command;
#if defined(Q_OS_WIN)
	const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))).filePath(QStringLiteral("System32/tar.exe"));
	if (QFileInfo::exists(tar)) {
		// (-a: the format from the .zip extension.)
		command = {true, tar, {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"), QDir::toNativeSeparators(zip)}, folder};
		command.arguments += files;
	}
#else
	if (const QString bsdtar = QStandardPaths::findExecutable(QStringLiteral("bsdtar")); !bsdtar.isEmpty()) {
		command = {true, bsdtar, {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"), zip}, folder};
		command.arguments += files;
	} else if (const QString zipper = QStandardPaths::findExecutable(QStringLiteral("zip")); !zipper.isEmpty()) {
		command = {true, zipper, {QStringLiteral("-q"), QStringLiteral("-r"), zip}, folder};
		command.arguments += files;
	}
#endif
	return command;
}

void revealInFileManager(const QString& path) {
#if defined(Q_OS_WIN)
	QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
#else
	const QFileInfo info(path);
	QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
#endif
}

void attachParentConsole() {
#if defined(Q_OS_WIN)
	// Output redirected to a file or a pipe already has its handle; a console needs attaching.
	const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		FILE* stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
	}
#endif
}

} // namespace Platform
