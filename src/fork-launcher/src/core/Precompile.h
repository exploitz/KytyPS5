#pragma once

// The static shader and pipeline precompile as precompile-windows.ps1 runs it, in C++: the seed file, recorded play's
// specializations (the release's hints, another region's for a Demon's Souls without its own, this PC's warmup
// recordings and a recorded seed file, gathered into one hinted seed file), the plan (its seeds' half also writes the
// shader prefetch inputs, and leaves out the guessed specializations of shaders recorded play has), the shards
// (kyty_shader_precompile --shard i/n, Jobs at a time on the allowed CPUs, split in two on exit code 3) and the merges.
// An interrupted run resumes: its finished
// shards are merged first and what the static cache holds is not compiled again. Progress events for the window.
//
// Not ported: -Coverage, -Quick, -Full, -InputsOnly, -ExportHints, -NoPrune, -Jobs/-Threads/-Shards/-Seeds/-Recorded overrides
// (the script's defaults are used: 3 threads a process, 512 shards of the seed file).

#include "core/GameInfo.h"
#include "core/LaunchConfig.h"
#include "core/Package.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcessEnvironment>
#include <QQueue>
#include <QStringList>
#include <QTimer>

class QProcess;

struct PrecompileProgress {
	enum class Stage { Listing, Preparing, Planning, Compiling, Merging, Done };
	Stage   stage = Stage::Listing;
	QString text;            // the current step in plain words
	qint64  done  = 0;       // pipelines compiled (Compiling)
	qint64  total = 0;       // pipelines planned (0: not known yet)
	double  fraction  = 0;   // of the whole run, 0..1
	qint64  elapsedMs = 0;
	qint64  remainingMs = -1; // -1: no estimate yet
	bool    readyToPlay = false; // the shaders of recorded play are in the static cache
	int     shardsDone = 0, shardsRunning = 0, shardsWaiting = 0, splits = 0;
};
Q_DECLARE_METATYPE(PrecompileProgress)

// What a precompile run is made of, computed before anything runs (also the dry run's listing).
struct PrecompileSetup {
	QString      root, tool, game, cacheId, seeds, passStates, programStamp, logs, nvidiaCaches, plan;
	// Recorded play: the release's hints (`hints`, `released` once completed with the seed file's code), this PC's warmup
	// recordings, a recorded seed file next to the seed file (`recorded`); gathered into `together`, then `hinted`.
	QString      hints, released, recorded, together, hinted;
	QStringList  recordings;
	bool         makeSeeds = false, remakeSeeds = false, hasHints = false;
	quint64      mask = 0, all = 0;
	int          cpus = 0, jobs = 1, threads = 3, shards = 512;
	QStringList  translationSwitches; // KEY=VALUE copied from the launch config
	QString      error;

	[[nodiscard]] bool ok() const { return error.isEmpty(); }
	// The first commands of a run, for --dry-run.
	[[nodiscard]] QStringList describe() const;

	// `affinity`: 0 = the launch config's CPUs, else all (precompile-windows.ps1 -Affinity).
	static PrecompileSetup make(const Package& package, const GameInfo& game, const LaunchConfig& config, quint64 affinity,
	                            int processorCount);
	// The CPUs a precompile runs on (make's `affinity`): on a hybrid CPU its performance cores, or its efficiency cores
	// alone with `ecores` (the setting "Precompile on E-cores"); 0 elsewhere (the launch config's list, else all).
	static quint64 cpuMask(bool ecores, int processorCount);
};

class Precompile final: public QObject {
	Q_OBJECT

public:
	explicit Precompile(QObject* parent = nullptr);
	~Precompile() override;

	void start(const PrecompileSetup& setup);
	// Stops every process (safe: the next run resumes from the shards that finished).
	void stop();
	[[nodiscard]] bool running() const { return m_running; }
	[[nodiscard]] const PrecompileProgress& progress() const { return m_progress; }
	// The static estimate: about 55 minutes on 22 CPUs (run-windows.ps1), in minutes rounded up to ten.
	static int estimateMinutes(int cpus);

signals:
	void progressChanged(const PrecompileProgress& progress);
	void output(const QString& text);
	// ok: the static cache is complete; stopped: the user stopped it; else `error` says what failed.
	void finished(bool ok, bool stopped, const QString& error);

private:
	struct Job {
		QString name;
		QString seedFile;
		QString shard; // "i/n" (empty: not a shard)
	};
	enum class Step { Seeds, Released, Gather, Apply, MergeBefore, PlanRecorded, PlanSeeds, Shards, MergeRecorded, Merge };

	// The process of one step or shard, not started yet: the caller connects to it, then starts it (on Windows a process
	// that cannot start reports it inside start()). `allCpus`: on every CPU, as the script's own commands.
	QProcess* makeTool(const QString& name, const QStringList& arguments, bool claims, bool allCpus = false);
	void      runStep(Step step);
	void      beginRecorded();
	void      gatherRecorded();
	void      afterStep(Step step, int code, const QString& name);
	void      scheduleShards();
	void      shardFinished(const QString& name, int code);
	void      fail(const QString& error);
	void      complete();
	void      cleanup();
	void      update(PrecompileProgress::Stage stage, const QString& text);
	void      tick();
	qint64    readNumber(const QString& log, const QString& pattern) const;

	PrecompileSetup         m_setup;
	bool                    m_running = false;
	bool                    m_stopping = false;
	bool                    m_has_recorded = false;
	bool                    m_tier_merged = false;
	QString                 m_recorded; // recorded play's seed file of this run (empty: none)
	QStringList             m_sources;  // what Gather puts together
	QQueue<Job>             m_first, m_second;
	QHash<QString, QProcess*> m_jobs; // running processes by name
	QHash<QString, Job>     m_job_info;
	QProcessEnvironment     m_base;
	PrecompileProgress      m_progress;
	QElapsedTimer           m_clock;
	QElapsedTimer           m_compile_clock;
	QElapsedTimer           m_merge_clock;
	qint64                  m_compile_done_at_start = 0;
	double                  m_smoothed_remaining = -1;
	qint64                  m_last_line = 0;
	QTimer                  m_ticker;
};
