#pragma once

#include "core/GameInfo.h"
#include "core/LaunchPlan.h"
#include "core/Package.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <functional>

// Runs a LaunchPlan as run-windows.ps1 does after its dry run: the seed file (listed once from the game's files), the
// static cache's --status, the shader prefetch inputs when they are stale, then the emulator with its switches, its
// output in logs\<stamp>.out.log / .err.log. The memory checks and the dialogs are the window's (MainWindow).
class PlayRunner final: public QObject {
	Q_OBJECT

public:
	enum class Stage { Idle, Starting, PreparingShaders, Loading, Running };
	Q_ENUM(Stage)

	explicit PlayRunner(QObject* parent = nullptr);
	~PlayRunner() override;

	void start(const LaunchPlan& plan, const GameInfo& game, const Package& package);
	// Stops the steps before the game starts (the game itself is never stopped by the launcher).
	void cancelPreparation();

	[[nodiscard]] bool  active() const { return m_stage != Stage::Idle; }
	[[nodiscard]] Stage stage() const { return m_stage; }
	[[nodiscard]] qint64 gamePid() const;

signals:
	void stageChanged(PlayRunner::Stage stage, const QString& detail);
	// Lines for the live log panel.
	void output(const QString& text);
	// The shader state --status reported (inputs current, static cache current/outdated/stale).
	void shaderStatus(bool inputsCurrent, const QString& staticCache);
	void gameWindowShown();
	void gameExited(int exitCode, bool crashed, const QString& outLog, const QString& errLog);
	// The game could not start (one sentence for the user).
	void failed(const QString& message);

private:
	void setStage(Stage stage, const QString& detail = {});
	void listSeeds();
	void queryStatus();
	void makeInputs();
	void launchGame();
	void pollGame();
	void readLog();
	void finishGame(int exitCode, QProcess::ExitStatus status);
	// The precompile program on the launch's CPUs (Invoke-Tool); `done` gets its exit code and standard output.
	void runTool(const QStringList& arguments, bool captureOutput, std::function<void(int, const QByteArray&)> done);

	LaunchPlan         m_plan;
	GameInfo           m_game;
	Package            m_package;
	Stage              m_stage = Stage::Idle;
	bool               m_shaders       = false;
	bool               m_inputs_ready  = false;
	QPointer<QProcess> m_tool;
	QProcess*          m_process = nullptr;
	QTimer             m_poll;
	QElapsedTimer      m_since_start;
	qint64             m_log_offset = 0;
	QByteArray         m_log_pending;
};
