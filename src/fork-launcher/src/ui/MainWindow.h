#pragma once

#include "core/GameInfo.h"
#include "core/LaunchConfig.h"
#include "core/Package.h"
#include "core/PlayRunner.h"
#include "core/Settings.h"

#include <QWidget>

class DlssAddon;
class HelpPage;
class HomePage;
class NavButton;
class Precompile;
class ProblemReport;
class QProcess;
class QStackedWidget;
class SettingsPage;
class SetupWizard;
class ShaderStatus;
class UpdateChecker;

// The launcher's window: a navigation rail and the pages (Home, Settings, Help, the setup), and everything the pages
// ask for: playing, precompiling, reports, the DLSS add-on, the update check.
class MainWindow final: public QWidget {
	Q_OBJECT

public:
	enum class Page { Home, Settings, Help, Setup };
	struct Options {
		bool screenshots = false; // --screenshots: nothing runs, nothing is saved, no dialogs
		bool forceSetup  = false; // --setup
	};

	MainWindow(const Package& package, Options options);
	~MainWindow() override;

	// The first page: the setup on a first run (or --setup), else Home.
	void start();
	// A second launcher was started: this one comes to the front.
	void activate();
	void showPage(Page page);
	void showSetup(int step);

	// --screenshots
	[[nodiscard]] HomePage*     home() const { return m_home; }
	[[nodiscard]] SettingsPage* settingsPage() const { return m_settings; }
	[[nodiscard]] SetupWizard*  wizard() const { return m_wizard; }
	[[nodiscard]] const GameInfo& game() const { return m_game; }

protected:
	void closeEvent(QCloseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	void refreshGame();
	void refreshHome();
	void refreshShaderChip();
	void play();
	bool preflight();
	void startPrecompile();
	void stopPrecompile();
	void reportProblem();
	void exportHints();
	void openGuide();
	void openLogs();
	void onSettingChanged(const QString& key, const QString& previous);
	bool ensureDlss();
	void onGameExited(int exitCode, bool crashed, const QString& outLog, const QString& errLog);
	void restoreWindowRect();
	void saveWindowRect();
	[[nodiscard]] QString shaderStateText() const;
	[[nodiscard]] QString launchText() const;

	Package        m_package;
	Options        m_options;
	SettingsStore  m_store;
	LaunchConfig   m_config;
	GameInfo       m_game;
	bool           m_avx2 = true;
	QStringList    m_missing;
	quint64        m_efficiency_mask = 0;
	bool           m_warned_precompile_play = false;
	bool           m_quit_after_game        = false;
	QString        m_last_launch;

	PlayRunner*    m_runner;
	Precompile*    m_precompile;
	ShaderStatus*  m_status;
	UpdateChecker* m_updates;
	ProblemReport* m_report;
	DlssAddon*     m_dlss;
	QProcess*      m_hints = nullptr;

	QWidget*        m_rail;
	NavButton*      m_nav_home;
	NavButton*      m_nav_settings;
	NavButton*      m_nav_help;
	QStackedWidget* m_pages;
	HomePage*       m_home;
	SettingsPage*   m_settings;
	HelpPage*       m_help;
	SetupWizard*    m_wizard;
};
