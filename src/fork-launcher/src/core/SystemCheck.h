#pragma once

#include "core/Package.h"
#include "platform/Platform.h"

#include <QList>
#include <QString>

// This PC as the setup's first page and a problem report describe it.
struct SystemInfo {
	QString          os;
	QString          cpu;
	int              threads = 0;
	bool             avx2    = false;
	bool             hybrid  = false; // efficiency cores
	Platform::Gpu    gpu;
	Platform::Memory memory;
	QString          folder;
	double           diskFree  = -1;
	double           diskTotal = -1;
	QString          fileSystem;
	bool             inOneDrive   = false;
	bool             inZip        = false; // a zip opened in Explorer, or a temporary extraction of one
	bool             inTemp       = false; // somewhere in the Temp folder
	bool             writable     = true;
	bool             writeTested  = false;

	// `writeTest`: try to create a file in the folder (not in --screenshots, where nothing may be saved).
	static SystemInfo gather(const Package& package, bool writeTest);
};

struct CheckItem {
	enum class State { Ok, Warn, Bad };
	State   state = State::Ok;
	QString line; // one line
	QString hint; // what to do (shown when not ok)
};

QList<CheckItem> systemChecks(const SystemInfo& info);
QString          formatGb(double bytes);
