#include "ui/MainWindow.h"

#include "core/Caches.h"
#include "core/DlssAddon.h"
#include "core/LaunchPlan.h"
#include "core/Precompile.h"
#include "core/ProblemReport.h"
#include "core/ShaderStatus.h"
#include "core/SystemCheck.h"
#include "core/UpdateChecker.h"
#include "core/Version.h"
#include "platform/Platform.h"
#include "ui/Choices.h"
#include "ui/Dialogs.h"
#include "ui/HelpPage.h"
#include "ui/HomePage.h"
#include "ui/SettingsPage.h"
#include "ui/SetupWizard.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QPainter>
#include <QProcess>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr double GB = 1024.0 * 1024.0 * 1024.0;

// The navigation rail's background and the emblem at its top.
class Rail final: public QWidget {
public:
	explicit Rail(QWidget* parent): QWidget(parent) { setFixedWidth(88); }

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter p(this);
		p.fillRect(rect(), QColor(0x17, 0x17, 0x17));
		p.fillRect(QRect(width() - 1, 0, 1, height()), QColor(0x24, 0x24, 0x24));
		Icons::paintEmblem(p, QRectF(width() / 2.0 - 18, 22, 36, 36), true);
	}
};

// The last lines of a log file.
QStringList Tail(const QString& path, int lines) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return {};
	const qint64 size = file.size();
	file.seek(std::max<qint64>(0, size - 64 * 1024));
	QStringList all = QString::fromUtf8(file.readAll()).remove('\r').split('\n');
	while (!all.isEmpty() && all.last().trimmed().isEmpty()) all.removeLast();
	return all.mid(std::max<qsizetype>(0, all.size() - lines));
}

QString Hex(int code) {
	return QStringLiteral("0x%1").arg(QString::number(static_cast<quint32>(code), 16).toUpper().rightJustified(8, QChar(u'0')));
}

} // namespace

MainWindow::MainWindow(const Package& package, Options options)
    : m_package(package), m_options(options), m_store(package.settingsFile, options.screenshots), m_runner(new PlayRunner(this)),
      m_precompile(new Precompile(this)), m_status(new ShaderStatus(this)), m_updates(new UpdateChecker(this)), m_report(new ProblemReport(this)),
      m_dlss(new DlssAddon(this)) {
	setObjectName(QStringLiteral("Root"));
	setWindowTitle(QStringLiteral("KytyPS5 Launcher"));
	setWindowIcon(Icons::appIcon());
	setMinimumSize(880, 560);
	resize(960, 600);
	m_store.load();
	m_config          = LaunchConfig::read(m_package.config);
	m_avx2            = Platform::hasAvx2();
	m_efficiency_mask = Platform::efficiencyMask();
	m_missing         = m_package.missingFiles();

	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_rail            = new Rail(this);
	auto* rail_layout = new QVBoxLayout(m_rail);
	rail_layout->setContentsMargins(0, 80, 0, 16);
	rail_layout->setSpacing(4);
	m_nav_home     = new NavButton(Icons::Icon::Home, tr("Home"), m_rail);
	m_nav_settings = new NavButton(Icons::Icon::Settings, tr("Settings"), m_rail);
	m_nav_help     = new NavButton(Icons::Icon::Help, tr("Help"), m_rail);
	for (NavButton* button: {m_nav_home, m_nav_settings, m_nav_help}) rail_layout->addWidget(button, 0, Qt::AlignHCenter);
	rail_layout->addStretch(1);
	layout->addWidget(m_rail);

	m_pages = new QStackedWidget(this);
	m_pages->setObjectName(QStringLiteral("Pages"));
	m_home     = new HomePage(m_pages);
	m_settings = new SettingsPage(&m_store, m_efficiency_mask != 0, m_pages);
	m_help     = new HelpPage(m_package.logDir, m_pages);
	m_wizard   = new SetupWizard(m_package, &m_store, options.screenshots, m_pages);
	for (QWidget* page: {static_cast<QWidget*>(m_home), static_cast<QWidget*>(m_settings), static_cast<QWidget*>(m_help),
	                     static_cast<QWidget*>(m_wizard)}) {
		m_pages->addWidget(page);
	}
	layout->addWidget(m_pages, 1);

	connect(m_nav_home, &NavButton::clicked, this, [this] { showPage(Page::Home); });
	connect(m_nav_settings, &NavButton::clicked, this, [this] { showPage(Page::Settings); });
	connect(m_nav_help, &NavButton::clicked, this, [this] { showPage(Page::Help); });

	// Home.
	connect(m_home, &HomePage::playClicked, this, &MainWindow::play);
	connect(m_home, &HomePage::shaderChipClicked, this, &MainWindow::startPrecompile);
	connect(m_home, &HomePage::stopPrecompileClicked, this, &MainWindow::stopPrecompile);
	connect(m_home, &HomePage::precompileCardClosed, this, [this] {
		m_home->hidePrecompile();
		refreshShaderChip();
	});
	connect(m_home, &HomePage::reportClicked, this, &MainWindow::reportProblem);
	connect(m_home, &HomePage::settingsClicked, this, [this] { showPage(Page::Settings); });
	connect(m_home, &HomePage::noticeActionClicked, this, [this] {
		if (!m_missing.isEmpty()) QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kReleasesUrl)));
		else showSetup(1);
	});
	// Settings and Help.
	connect(m_settings, &SettingsPage::changed, this, &MainWindow::onSettingChanged);
	connect(m_settings, &SettingsPage::runSetupClicked, this, [this] { showSetup(0); });
	connect(m_help, &HelpPage::openGuide, this, &MainWindow::openGuide);
	connect(m_help, &HelpPage::openLogs, this, &MainWindow::openLogs);
	connect(m_help, &HelpPage::reportProblem, this, &MainWindow::reportProblem);
	connect(m_help, &HelpPage::exportHints, this, &MainWindow::exportHints);
	connect(m_help, &HelpPage::openReleases, this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kReleasesUrl))); });
	// The setup.
	connect(m_wizard, &SetupWizard::finished, this, [this](bool precompileNow) {
		refreshGame();
		m_settings->reload();
		refreshHome();
		showPage(Page::Home);
		if (precompileNow) startPrecompile();
	});
	connect(m_wizard, &SetupWizard::cancelled, this, [this] { showPage(Page::Settings); });

	// Playing.
	connect(m_runner, &PlayRunner::stageChanged, this, [this](PlayRunner::Stage stage, const QString& detail) {
		switch (stage) {
			case PlayRunner::Stage::Idle: refreshHome(); break;
			case PlayRunner::Stage::Starting: m_home->setPlay(PlayButton::Look::Busy, tr("Starting…"), ThinBar::Mode::Busy); break;
			case PlayRunner::Stage::PreparingShaders:
				m_home->setPlay(PlayButton::Look::Busy, tr("Preparing shaders"), ThinBar::Mode::Busy);
				m_home->appendLog(QStringLiteral("launcher: %1").arg(detail));
				break;
			case PlayRunner::Stage::Loading: m_home->setPlay(PlayButton::Look::Busy, tr("Loading"), ThinBar::Mode::Busy); break;
			case PlayRunner::Stage::Running: m_home->setPlay(PlayButton::Look::Running, tr("Running"), ThinBar::Mode::Full, Tone::Ok); break;
		}
	});
	connect(m_runner, &PlayRunner::output, m_home, &HomePage::appendLog);
	connect(m_runner, &PlayRunner::shaderStatus, m_status, &ShaderStatus::setQueried);
	connect(m_runner, &PlayRunner::gameWindowShown, this, [this] {
		if (!m_options.screenshots && isVisible()) showMinimized();
	});
	connect(m_runner, &PlayRunner::gameExited, this, &MainWindow::onGameExited);
	connect(m_runner, &PlayRunner::failed, this, [this](const QString& message) {
		refreshHome();
		m_home->showError(message + tr(" Your antivirus may have blocked it: allow the folder, or download the release again."), {});
	});

	// Precompiling.
	connect(m_precompile, &Precompile::progressChanged, m_home, &HomePage::showPrecompile);
	connect(m_precompile, &Precompile::output, m_home, &HomePage::appendLog);
	connect(m_precompile, &Precompile::finished, this, [this](bool ok, bool stopped, const QString& error) {
		m_status->refresh(m_package, m_game, !m_options.screenshots);
		if (ok) {
			const qint64 minutes = (m_precompile->progress().elapsedMs + 59999) / 60000;
			m_home->showPrecompileResult(true, tr("Shaders ready"),
			                             tr("Precompiled in %n min. New scenes won't stutter the first time they appear.", nullptr, static_cast<int>(minutes)));
		} else if (stopped) {
			m_home->hidePrecompile();
		} else {
			m_home->showPrecompileResult(false, tr("The precompile stopped"), error);
		}
		refreshShaderChip();
	});
	connect(m_status, &ShaderStatus::changed, this, &MainWindow::refreshShaderChip);

	// Updates.
	connect(m_updates, &UpdateChecker::updateAvailable, this, [this](const QString& tag, const QUrl& page) {
		m_home->setUpdate(tag);
		disconnect(m_home, &HomePage::updateClicked, nullptr, nullptr);
		connect(m_home, &HomePage::updateClicked, this, [page] { QDesktopServices::openUrl(page); });
	});

	if (!options.screenshots) Platform::styleTitleBar(this);
}

MainWindow::~MainWindow() = default;

void MainWindow::start() {
	refreshGame();
	refreshHome();
	const bool game_ok = m_game.isGame;
	if (m_options.forceSetup || !m_store.settings().setupDone || !game_ok) {
		showSetup(m_store.settings().setupDone && !m_options.forceSetup ? 1 : 0);
	} else {
		showPage(Page::Home);
	}
	if (m_options.screenshots) return;
	restoreWindowRect();
	m_updates->check();
	if (m_store.wasReset()) {
		QTimer::singleShot(0, this, [this] {
			MessageDialog::tell(this, Tone::Warn, tr("Your settings file was damaged"),
			                    tr("It was renamed to launcher-settings.json.bad and the default settings are used. Check your settings before playing."));
		});
	}
}

void MainWindow::activate() {
	// (Hidden while the game runs after a close: shown again, and it stays.)
	m_quit_after_game = false;
	Platform::forceForeground(this);
}

void MainWindow::showPage(Page page) {
	m_rail->setVisible(page != Page::Setup);
	switch (page) {
		case Page::Home:
			m_pages->setCurrentWidget(m_home);
			m_nav_home->setChecked(true);
			break;
		case Page::Settings:
			m_settings->reload();
			m_pages->setCurrentWidget(m_settings);
			m_nav_settings->setChecked(true);
			break;
		case Page::Help:
			m_pages->setCurrentWidget(m_help);
			m_nav_help->setChecked(true);
			break;
		case Page::Setup: m_pages->setCurrentWidget(m_wizard); break;
	}
}

void MainWindow::showSetup(int step) {
	const bool cancellable = m_store.settings().setupDone && m_game.isGame;
	m_wizard->begin(cancellable, step);
	showPage(Page::Setup);
}

void MainWindow::refreshGame() {
	m_game = GameInfo::read(m_store.settings().game, m_options.screenshots ? QString() : m_package.tool);
	m_home->setGame(m_game, m_game.isGame, !m_options.screenshots);
	m_status->refresh(m_package, m_game, !m_options.screenshots);
}

void MainWindow::refreshHome() {
	const LauncherSettings& s = m_store.settings();
	m_home->setSummary(Choices::summary(s));
	// What keeps PLAY from working, most important first.
	QString reason;
	if (!m_missing.isEmpty()) {
		m_home->showNotice(Tone::Danger,
		                   tr("Some of the emulator's files are missing (%1). Your antivirus may have removed them, or the zip was only partly "
		                      "extracted: download the release again and unzip the whole folder.")
		                       .arg(m_missing.join(QStringLiteral(", "))),
		                   tr("Releases page"));
		reason = tr("Files are missing.");
	} else if (s.game.isEmpty()) {
		m_home->showNotice(Tone::Warn, tr("No game chosen yet. Choose your Demon's Souls folder to play."), tr("Choose game"));
		reason = tr("Choose your game first.");
	} else if (!m_game.isGame) {
		m_home->showNotice(Tone::Danger,
		                   tr("The game can't be found at %1. Choose the folder with eboot.bin and sce_sys again.").arg(QDir::toNativeSeparators(s.game)),
		                   tr("Choose game"));
		reason = tr("The game folder is missing.");
	} else {
		m_home->hideNotice();
	}
	if (!m_avx2) reason = tr("This processor lacks AVX2, which the emulator needs.");
	if (!m_runner->active()) {
		m_home->setPlay(reason.isEmpty() ? PlayButton::Look::Play : PlayButton::Look::Disabled, tr("PLAY"), ThinBar::Mode::Hidden);
	}
	m_home->setPlayReason(m_avx2 ? QString() : reason);
	refreshShaderChip();
}

void MainWindow::refreshShaderChip() {
	const ShaderStatus::State& state = m_status->state();
	const int minutes = Precompile::estimateMinutes(Platform::logicalCpuCount());
	if (!m_game.isGame) {
		m_home->setShaderState(Tone::Neutral, tr("No game chosen"), {}, {});
	} else if (m_precompile->running()) {
		m_home->setShaderState(Tone::Accent, tr("Precompiling"), {}, {});
	} else if (state.driverChanged()) {
		m_home->setShaderState(Tone::Warn, tr("Graphics driver changed"), tr("Precompile again"),
		                       tr("Your graphics driver changed since the last precompile: run it again so new scenes don't stutter."));
	} else if (state.outdated()) {
		m_home->setShaderState(Tone::Warn, tr("New shaders to precompile"), tr("Precompile"),
		                       tr("This emulator version lists more of the game's shaders; precompiling again compiles only the new ones."));
	} else if (state.files == Caches::StaticState::Done) {
		m_home->setShaderState(Tone::Ok, tr("Shaders ready"), {}, {});
	} else if (state.files == Caches::StaticState::Partial) {
		m_home->setShaderState(Tone::Warn, tr("Precompile not finished"), tr("Continue"), tr("It picks up where it stopped."));
	} else {
		m_home->setShaderState(Tone::Warn, tr("Shaders not precompiled"), tr("Precompile"),
		                       tr("Until then, new scenes stutter the first time they appear. About %1 min on this PC.").arg(minutes));
	}
}

QString MainWindow::shaderStateText() const {
	const ShaderStatus::State& state = m_status->state();
	QString text = state.files == Caches::StaticState::Done ? QStringLiteral("static cache present")
	               : state.files == Caches::StaticState::Partial ? QStringLiteral("static cache partial (an interrupted precompile)")
	                                                             : QStringLiteral("not precompiled");
	if (state.queried) text += QStringLiteral("; --status: inputs %1, static cache %2").arg(state.inputsCurrent ? QStringLiteral("current") : QStringLiteral("stale"), state.staticCache);
	if (m_precompile->running()) text += QStringLiteral("; a precompile is running");
	return text;
}

QString MainWindow::launchText() const {
	if (!m_last_launch.isEmpty()) return m_last_launch;
	LaunchInputs inputs;
	inputs.package         = m_package;
	inputs.settings        = m_store.settings();
	inputs.config          = m_config;
	inputs.game            = m_game;
	inputs.processorCount  = Platform::logicalCpuCount();
	inputs.performanceCpus = &Platform::performanceCpus;
	return QStringLiteral("(not launched in this session; the launch the current settings make)\n") + LaunchPlan::build(inputs).dryRunLines().join('\n') + '\n';
}

// --- Playing ----------------------------------------------------------------------------------------------------------

bool MainWindow::preflight() {
	const LauncherSettings& s = m_store.settings();
	if (!m_missing.isEmpty()) {
		MessageDialog::tell(this, Tone::Danger, tr("Some of the emulator's files are missing"),
		                    tr("%1 could not be found. Your antivirus may have removed them, or the zip was only partly extracted: download the "
		                       "release again and unzip the whole folder.")
		                        .arg(m_missing.join(QStringLiteral(", "))));
		return false;
	}
	if (!m_config.ok()) {
		MessageDialog::tell(this, Tone::Danger, tr("The launch settings can't be read"),
		                    tr("%1 is damaged. Download the release again and unzip the whole folder.").arg(QDir::toNativeSeparators(m_config.path)));
		return false;
	}
	const SystemInfo info = SystemInfo::gather(m_package, true);
	if (info.inZip || !info.writable) {
		MessageDialog::tell(this, Tone::Danger,
		                    info.inZip ? tr("The launcher is running from inside a zip file") : tr("The launcher's folder is read-only"),
		                    tr("The emulator needs to write its caches next to it. Unzip the whole release into a normal folder, such as "
		                       "C:\\Games\\KytyPS5, and start the launcher from there."));
		return false;
	}
	if (!m_game.isGame) {
		if (MessageDialog::ask(this, Tone::Danger, tr("The game can't be found"),
		                       tr("Choose the folder that holds eboot.bin and the sce_sys folder, or a .zar file of it."),
		                       {tr("Cancel"), tr("Choose game")}) == 1) {
			showSetup(1);
		}
		return false;
	}
	if (m_game.isZar && !m_game.paramRead) {
		if (MessageDialog::ask(this, Tone::Danger, tr("The game's .zar file can't be read"),
		                       tr("Make sure it is a complete ZArchive of the game folder, or choose the extracted folder instead."),
		                       {tr("Cancel"), tr("Choose game")}) == 1) {
			showSetup(1);
		}
		return false;
	}
	// The game or a precompile already running.
	const QList<qint64> games = Platform::processesNamed(QFileInfo(m_package.emulator).fileName());
	if (!games.isEmpty()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("The game is already running"), tr("Switch to it, or close it before starting it again."),
		                       {tr("Cancel"), tr("Show the game")}) == 1) {
			Platform::bringProcessToFront(games.first());
		}
		return false;
	}
	if (m_precompile->running()) {
		if (!m_warned_precompile_play) {
			if (MessageDialog::ask(this, Tone::Warn, tr("The precompile is still running"),
			                       tr("New scenes may stutter until it is done. It keeps running while you play."),
			                       {tr("Cancel"), tr("Play anyway")}) != 1) {
				return false;
			}
			m_warned_precompile_play = true;
		}
	} else if (!Platform::processesNamed(QFileInfo(m_package.tool).fileName(), true).isEmpty()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("A shader precompile is running in another window"),
		                       tr("Playing now is possible, but new scenes may stutter until it is done."), {tr("Cancel"), tr("Play anyway")}) != 1) {
			return false;
		}
	}
	// The game version.
	if (m_game.otherRegion()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("This region's release is untested"),
		                       tr("%1 %2 should run like the tested PPSA01341 %2, but it has not been tested.").arg(m_game.titleId, m_game.displayVersion()),
		                       {tr("Cancel"), tr("Play anyway")}) != 1) {
			return false;
		}
	} else if (!m_game.tested()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("This game version is untested"),
		                       tr("%1 may not start or may crash; version 1.07 is tested, 1.05 supported.").arg(m_game.versionKey()),
		                       {tr("Cancel"), tr("Play anyway")}) != 1) {
			return false;
		}
	}
	if ((s.output == QStringLiteral("1080q") || s.output == QStringLiteral("1080f")) && m_game.paramRead &&
	    m_game.contentVersion != QStringLiteral("01.007.000")) {
		if (MessageDialog::ask(this, Tone::Warn, tr("The 1080p modes are made for version 1.07"),
		                       tr("With %1 the HUD may have the wrong size. If it looks wrong, choose 4K (PS5) in Settings.").arg(m_game.displayVersion()),
		                       {tr("Cancel"), tr("Play anyway")}) != 1) {
			return false;
		}
	}
	// Optional files the settings need.
	if (s.aa == QStringLiteral("fsr4") && !m_package.hasFsr4()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("FSR 4's files are missing"),
		                       tr("The deps\\fsr4 folder is not in the launcher's folder. Download the release again, or use the game's TAA."),
		                       {tr("Cancel"), tr("Use Game TAA")}) != 1) {
			return false;
		}
		m_store.settings().aa = QStringLiteral("taa");
		m_store.save();
		m_settings->reload();
	}
	if (s.framegen == QStringLiteral("fsr") && !m_package.hasFsrFrameGen()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("FSR 3.1 frame generation's file is missing"),
		                       tr("deps\\fsr\\amd_fidelityfx_vk.dll is not in the launcher's folder. Download the release again, or turn frame "
		                          "generation off."),
		                       {tr("Cancel"), tr("Turn it off")}) != 1) {
			return false;
		}
		m_store.settings().framegen = QStringLiteral("off");
		m_store.save();
		m_settings->reload();
	}
	if (s.aa == QStringLiteral("dlss") && !m_package.hasDlssAddon() && !ensureDlss()) return false;
	// Memory and disk: Windows ends a program that asks for more than RAM and the page file can hold.
	const Platform::Memory memory = info.memory;
	QStringList            short_of;
	if (memory.ram < 24 * GB) {
		short_of << tr("This PC has %1 of RAM: the game runs, but loading takes longer (32 GB is recommended).").arg(formatGb(memory.ram));
	}
	if (memory.commitKnown && memory.commitFree < 34 * GB) {
		short_of << tr("Windows can give programs only %1 more memory (RAM plus page file) and the emulator needs about 34 GB: it may close "
		               "mid-game. Close other programs, or set the page file to System managed size.")
		                .arg(formatGb(memory.commitFree));
	}
	if (info.diskFree >= 0 && info.diskFree < 10 * GB) {
		short_of << tr("Only %1 is free on this drive; the game's caches need room to grow.").arg(formatGb(info.diskFree));
	}
	if (!short_of.isEmpty()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("This PC may run short"), short_of.join(QStringLiteral("\n\n")), {tr("Cancel"), tr("Start anyway")}) != 1) {
			return false;
		}
	}
	return true;
}

void MainWindow::play() {
	if (m_runner->active()) {
		if (m_runner->stage() == PlayRunner::Stage::Running) Platform::bringProcessToFront(m_runner->gamePid());
		return;
	}
	if (m_home->playLook() == PlayButton::Look::Disabled) {
		if (!m_game.isGame && m_missing.isEmpty()) showSetup(1);
		return;
	}
	if (!preflight()) return;
	refreshGame();
	LaunchInputs inputs;
	inputs.package         = m_package;
	inputs.settings        = m_store.settings();
	inputs.config          = m_config;
	inputs.game            = m_game;
	inputs.processorCount  = Platform::logicalCpuCount();
	inputs.performanceCpus = &Platform::performanceCpus;
	const LaunchPlan plan  = LaunchPlan::build(inputs);
	m_last_launch          = plan.dryRunLines().join('\n') + '\n';
	m_home->hideError();
	m_home->appendLog(QString());
	m_runner->start(plan, m_game, m_package);
}

void MainWindow::onGameExited(int exitCode, bool crashed, const QString& outLog, const QString& errLog) {
	if (!m_options.screenshots) {
		if (m_quit_after_game) {
			QApplication::quit();
			return;
		}
		showNormal();
		Platform::forceForeground(this);
	}
	refreshHome();
	m_status->refresh(m_package, m_game, !m_options.screenshots);
	if (exitCode == 0 && !crashed) return;
	QStringList details;
	const QStringList err = Tail(errLog, 25);
	if (!err.isEmpty()) details << QStringLiteral("— %1 —").arg(QFileInfo(errLog).fileName()) << err;
	const QStringList out = Tail(outLog, 25);
	if (!out.isEmpty()) details << QStringLiteral("— %1 —").arg(QFileInfo(outLog).fileName()) << out;
	const QString sentence = crashed || static_cast<quint32>(exitCode) >= 0xC0000000u
	                             ? tr("The game closed unexpectedly (error %1).").arg(Hex(exitCode))
	                             : tr("The game stopped with an error (exit code %1).").arg(exitCode);
	m_home->showError(sentence, details.join('\n'));
}

// --- Precompiling -----------------------------------------------------------------------------------------------------

void MainWindow::startPrecompile() {
	if (m_precompile->running()) return;
	if (!m_missing.isEmpty()) {
		MessageDialog::tell(this, Tone::Danger, tr("Some of the emulator's files are missing"),
		                    tr("%1 could not be found. Download the release again and unzip the whole folder.").arg(m_missing.join(QStringLiteral(", "))));
		return;
	}
	if (!m_game.isGame || !m_game.paramRead) {
		if (MessageDialog::ask(this, Tone::Warn, tr("Choose the game first"),
		                       tr("The precompile needs the game's folder with its sce_sys\\param.json (or a readable .zar file)."),
		                       {tr("Cancel"), tr("Choose game")}) == 1) {
			showSetup(1);
		}
		return;
	}
	if (!Platform::processesNamed(QFileInfo(m_package.tool).fileName(), true).isEmpty()) {
		MessageDialog::tell(this, Tone::Warn, tr("A shader precompile is already running"),
		                    tr("Another window is precompiling. Wait for it to finish, or close it first; this one continues where it stopped."));
		return;
	}
	if (m_runner->active() || !Platform::processesNamed(QFileInfo(m_package.emulator).fileName()).isEmpty()) {
		if (MessageDialog::ask(this, Tone::Warn, tr("The game is running"),
		                       tr("The precompile uses the whole processor and would slow the game down. Close the game first, or precompile anyway."),
		                       {tr("Cancel"), tr("Precompile anyway")}) != 1) {
			return;
		}
	}
	if (const SystemInfo info = SystemInfo::gather(m_package, true); info.inZip || !info.writable) {
		MessageDialog::tell(this, Tone::Danger,
		                    info.inZip ? tr("The launcher is running from inside a zip file") : tr("The launcher's folder is read-only"),
		                    tr("The precompile writes its caches next to the launcher. Unzip the whole release into a normal folder, such as "
		                       "C:\\Games\\KytyPS5, and start the launcher from there."));
		return;
	}
	const LauncherSettings& s     = m_store.settings();
	const quint64           mask  = PrecompileSetup::cpuMask(s.ecores, Platform::logicalCpuCount());
	const PrecompileSetup   setup = PrecompileSetup::make(m_package, m_game, m_config, mask, Platform::logicalCpuCount());
	if (!setup.ok()) {
		MessageDialog::tell(this, Tone::Danger, tr("The precompile can't start"), setup.error);
		return;
	}
	for (const QString& line: Caches::renameTitleCaches(m_package, m_game)) m_home->appendLog(line);
	m_home->hideError();
	PrecompileProgress first;
	first.text = tr("Getting ready");
	m_home->showPrecompile(first);
	m_precompile->start(setup);
	refreshShaderChip();
}

void MainWindow::stopPrecompile() {
	if (!m_precompile->running()) return;
	if (MessageDialog::ask(this, Tone::Neutral, tr("Stop the precompile?"),
	                       tr("Stopping is safe: next time it continues where it stopped."), {tr("Keep going"), tr("Stop")}) != 1) {
		return;
	}
	m_precompile->stop();
}

// --- Settings ---------------------------------------------------------------------------------------------------------

void MainWindow::onSettingChanged(const QString& key, const QString& previous) {
	LauncherSettings& s = m_store.settings();
	if (key == QStringLiteral("aa") && s.aa == QStringLiteral("dlss") && !m_package.hasDlssAddon()) {
		if (!ensureDlss()) {
			s.aa = previous == QStringLiteral("dlss") ? QStringLiteral("taa") : previous;
			m_store.save();
			m_settings->reload();
		}
	} else if (key == QStringLiteral("aa") && s.aa == QStringLiteral("fsr4") && !m_package.hasFsr4()) {
		MessageDialog::tell(this, Tone::Warn, tr("FSR 4's files are missing"),
		                    tr("The deps\\fsr4 folder is not in the launcher's folder. Download the release again to use FSR 4."));
		s.aa = previous;
		m_store.save();
		m_settings->reload();
	} else if (key == QStringLiteral("framegen") && s.framegen == QStringLiteral("fsr") && !m_package.hasFsrFrameGen()) {
		MessageDialog::tell(this, Tone::Warn, tr("FSR 3.1 frame generation's file is missing"),
		                    tr("deps\\fsr\\amd_fidelityfx_vk.dll is not in the launcher's folder. Download the release again to use it."));
		s.framegen = previous;
		m_store.save();
		m_settings->reload();
	} else if (key == QStringLiteral("output") && s.output != QStringLiteral("4k") && m_game.paramRead &&
	           m_game.contentVersion != QStringLiteral("01.007.000")) {
		MessageDialog::tell(this, Tone::Warn, tr("The 1080p modes are made for version 1.07"),
		                    tr("Your game is %1: its HUD may have the wrong size in this mode. If it looks wrong, choose 4K (PS5).").arg(m_game.displayVersion()));
	}
	refreshHome();
}

bool MainWindow::ensureDlss() {
	if (m_package.hasDlssAddon()) return true;
	const Platform::Gpu gpu = Platform::gpu();
	if (!(gpu.vendor == QStringLiteral("NVIDIA") && gpu.name.contains(QStringLiteral("RTX"), Qt::CaseInsensitive))) {
		MessageDialog::tell(this, Tone::Warn, tr("DLSS needs an NVIDIA RTX graphics card"),
		                    tr("This PC has %1. Use Game TAA or FSR 4 instead.").arg(gpu.found ? gpu.name : tr("no NVIDIA RTX card")));
		return false;
	}
	if (MessageDialog::ask(this, Tone::Accent, tr("DLSS comes as a free add-on"),
	                       tr("NVIDIA's DLSS library can't ship inside the emulator, so it is a separate download from this fork's releases "
	                          "(into plugins\\dlss here). Download and install it now?"),
	                       {tr("Not now"), tr("Download")}) != 1) {
		return false;
	}
	MessageDialog dialog(this, Tone::Accent, tr("Downloading the DLSS add-on"), tr("From github.com/exploitz/KytyPS5/releases."), {tr("Cancel")});
	dialog.showProgress(true);
	dialog.setProgress(0, tr("Connecting…"));
	DlssAddon::Result result = DlssAddon::Result::Cancelled;
	QString           detail;
	const auto        progress = connect(m_dlss, &DlssAddon::progress, &dialog, [&dialog](qint64 received, qint64 total) {
        dialog.setProgress(total > 0 ? static_cast<double>(received) / total : 0,
		                          total > 0 ? tr("%1 of %2 MB").arg(received / (1024 * 1024)).arg(total / (1024 * 1024)) : tr("%1 MB").arg(received / (1024 * 1024)));
	});
	const auto        done     = connect(m_dlss, &DlssAddon::finished, &dialog, [&](DlssAddon::Result r, const QString& d) {
        result = r;
        detail = d;
        dialog.done(100);
	});
	m_dlss->download(m_package);
	if (dialog.exec() != 100) m_dlss->cancel();
	disconnect(progress);
	disconnect(done);
	switch (result) {
		case DlssAddon::Result::Installed:
			MessageDialog::tell(this, Tone::Ok, tr("The DLSS add-on is installed"), tr("DLSS is used the next time you play."));
			return true;
		case DlssAddon::Result::NotAvailable:
			if (MessageDialog::ask(this, Tone::Warn, tr("The DLSS add-on is not available yet"),
			                       tr("Use Game TAA or FSR 4 for now; the add-on will be on the releases page when it's ready."),
			                       {tr("Open releases page"), tr("OK")}) == 0) {
				QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kReleasesUrl)));
			}
			return false;
		case DlssAddon::Result::Failed:
			if (MessageDialog::ask(this, Tone::Danger, tr("The DLSS add-on could not be installed"),
			                       tr("%1 You can download KytyPS5-DLSS-addon.zip from the releases page and unzip it into the plugins\\dlss folder here.")
			                           .arg(detail.endsWith('.') || detail.isEmpty() ? detail : detail + '.'),
			                       {tr("Open releases page"), tr("OK")}) == 0) {
				QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kReleasesUrl)));
			}
			return false;
		case DlssAddon::Result::Cancelled: return false;
	}
	return false;
}

// --- Help -------------------------------------------------------------------------------------------------------------

void MainWindow::reportProblem() {
	if (m_report->busy()) return;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const QString system = ProblemReport::systemText(m_package, m_game, shaderStateText());
	connect(
	    m_report, &ProblemReport::finished, this,
	    [this](bool ok, const QString& zip, const QString& error) {
		    QApplication::restoreOverrideCursor();
		    if (!ok) {
			    MessageDialog::tell(this, Tone::Danger, tr("The report could not be made"),
			                        tr("%1 You can attach the newest files of the logs folder to an issue by hand.").arg(error.isEmpty() ? QString() : error + '.'));
			    return;
		    }
		    QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kIssuesUrl)));
		    Platform::revealInFileManager(zip);
		    MessageDialog::tell(this, Tone::Ok, tr("Your report is on the Desktop"),
		                        tr("Attach %1 to the GitHub issue that just opened, and describe what happened.").arg(QFileInfo(zip).fileName()));
	    },
	    Qt::SingleShotConnection);
	m_report->create(m_package, system, launchText(), m_home->logText());
}

// The specializations this PC's play recorded, as hints without the game's code (precompile-windows.ps1 -ExportHints):
// attached to an issue, they go into the next releases' hints.
void MainWindow::exportHints() {
	if (m_hints != nullptr) return;
	const PrecompileSetup s = PrecompileSetup::make(m_package, m_game, m_config, 0, Platform::logicalCpuCount());
	if (!s.error.isEmpty()) {
		MessageDialog::tell(this, Tone::Danger, tr("The shader hints could not be made"), s.error + '.');
		return;
	}
	QStringList recorded;
	const QDir  warmup(m_package.path(QStringLiteral("_PipelineCache/warmup-v2")));
	for (const QString& dir: warmup.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		const QString file = warmup.filePath(dir + QStringLiteral("/%1.shaders").arg(s.cacheId));
		if (QFileInfo::exists(file)) recorded << QDir::toNativeSeparators(file);
	}
	if (recorded.isEmpty() || !QFileInfo::exists(s.seeds)) {
		MessageDialog::tell(this, Tone::Neutral, tr("Nothing recorded yet"),
		                    tr("Play for a while first: the emulator records which shaders your play uses, and the hints are made from that."));
		return;
	}
	const QString out = m_package.path(QStringLiteral("my-hints-%1.hints").arg(s.cacheId));
	QStringList   arguments {QStringLiteral("--game"),  s.game, QStringLiteral("--seeds"), QDir::toNativeSeparators(s.seeds),
                            QStringLiteral("--make-hints"), QDir::toNativeSeparators(out)};
	for (const QString& file: recorded) arguments << QStringLiteral("--recorded") << file;
	m_hints = new QProcess(this);
	m_hints->setProcessChannelMode(QProcess::MergedChannels);
	QApplication::setOverrideCursor(Qt::WaitCursor);
	connect(m_hints, &QProcess::finished, this, [this, out](int code, QProcess::ExitStatus status) {
		QApplication::restoreOverrideCursor();
		m_hints->deleteLater();
		m_hints = nullptr;
		if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(out)) {
			MessageDialog::tell(this, Tone::Danger, tr("The shader hints could not be made"),
			                    tr("kyty_shader_precompile --make-hints ended with code %1.").arg(code));
			return;
		}
		Platform::revealInFileManager(out);
		MessageDialog::tell(this, Tone::Ok, tr("Shader hints saved"),
		                    tr("%1 (%2 KB) lists which shaders your play used, without the game's code. Attach it to an issue on "
		                       "GitHub: the next releases precompile them for everyone.")
		                        .arg(QFileInfo(out).fileName())
		                        .arg((QFileInfo(out).size() + 1023) / 1024));
	});
	m_hints->start(m_package.tool, arguments);
	if (!m_hints->waitForStarted(10000)) {
		QApplication::restoreOverrideCursor();
		m_hints->deleteLater();
		m_hints = nullptr;
		MessageDialog::tell(this, Tone::Danger, tr("The shader hints could not be made"),
		                    tr("%1 did not start.").arg(QDir::toNativeSeparators(m_package.tool)));
	}
}

void MainWindow::openGuide() {
	const QString readme = m_package.readme();
	if (readme.isEmpty()) {
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(Version::kReadmeUrl)));
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(readme))) {
#if defined(Q_OS_WIN)
		QProcess::startDetached(QStringLiteral("notepad.exe"), {QDir::toNativeSeparators(readme)});
#endif
	}
}

void MainWindow::openLogs() {
	QDir().mkpath(m_package.logDir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(m_package.logDir));
}

// --- Closing ----------------------------------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event) {
	if (m_options.screenshots) {
		event->accept();
		return;
	}
	// Both questions first, then what they decide (a cancel of the second leaves the precompile running).
	const bool precompiling = m_precompile->running();
	const bool playing      = m_runner->stage() == PlayRunner::Stage::Loading || m_runner->stage() == PlayRunner::Stage::Running;
	if (precompiling && MessageDialog::ask(this, Tone::Warn, tr("The precompile is still running"),
	                                       tr("Stopping it is safe: next time it continues where it left off."),
	                                       {tr("Keep running"), tr("Stop and close")}) != 1) {
		event->ignore();
		return;
	}
	if (playing && MessageDialog::ask(this, Tone::Neutral, tr("The game is still running"), tr("The launcher can close; the game keeps running."),
	                                  {tr("Cancel"), tr("Close launcher")}) != 1) {
		event->ignore();
		return;
	}
	if (precompiling) m_precompile->stop();
	if (playing) {
		// The launcher stays in the background until the game ends (its logs are complete then), out of sight.
		saveWindowRect();
		m_quit_after_game = true;
		hide();
		event->ignore();
		return;
	}
	m_runner->cancelPreparation();
	saveWindowRect();
	event->accept();
	QApplication::quit();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
	const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
	// (Keys no child took: a focused painted button takes Enter as a click; on Home, Enter plays and Esc closes the log.)
	if (enter) {
		if (auto* button = qobject_cast<QAbstractButton*>(QApplication::focusWidget()); button != nullptr && isAncestorOf(button) &&
		                                                                               button->isVisible() && button->isEnabled()) {
			button->click();
			return;
		}
		// (Not from the log: a read-only text view leaves Enter to the window.)
		const QWidget* focus = QApplication::focusWidget();
		if (m_pages->currentWidget() == m_home && (focus == nullptr || !focus->inherits("QAbstractScrollArea"))) {
			play();
			return;
		}
	}
	if (event->key() == Qt::Key_Escape && m_pages->currentWidget() == m_home && m_home->logShown()) {
		m_home->setLogShown(false);
		return;
	}
	QWidget::keyPressEvent(event);
}

void MainWindow::restoreWindowRect() {
	// The saved place and size, unless they are off every screen now (a monitor unplugged).
	const LauncherSettings& s = m_store.settings();
	if (s.windowW < minimumWidth() || s.windowH < minimumHeight()) return;
	const QRect saved(s.windowX, s.windowY, s.windowW, s.windowH);
	for (const QScreen* screen: QGuiApplication::screens()) {
		const QRect visible = screen->availableGeometry().intersected(saved);
		if (visible.width() >= 160 && visible.height() >= 120) {
			setGeometry(saved);
			return;
		}
	}
}

void MainWindow::saveWindowRect() {
	if (m_options.screenshots) return;
	const QRect rect = windowState() & (Qt::WindowMaximized | Qt::WindowMinimized | Qt::WindowFullScreen) ? normalGeometry() : geometry();
	if (rect.width() <= 0 || rect.height() <= 0) return;
	LauncherSettings& s = m_store.settings();
	s.windowX           = rect.x();
	s.windowY           = rect.y();
	s.windowW           = rect.width();
	s.windowH           = rect.height();
	m_store.save();
}
