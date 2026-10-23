#include "ui/Screenshots.h"

#include "core/Package.h"
#include "core/Precompile.h"
#include "ui/Dialogs.h"
#include "ui/HomePage.h"
#include "ui/MainWindow.h"
#include "ui/SettingsPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QPixmap>
#include <QThread>
#include <cstdio>

namespace Screenshots {

namespace {

void Settle() {
	for (int i = 0; i < 6; i++) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(15);
	}
}

} // namespace

int run(const Package& package, const QString& folder) {
	QDir().mkpath(folder);
	MessageDialog::setScreenshotMode(true);
	MainWindow::Options options;
	options.screenshots = true;
	MainWindow window(package, options);
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.resize(960, 600);
	window.start();
	window.show();
	Settle();
	const qreal   dpr    = window.devicePixelRatioF();
	const QString suffix = qFuzzyCompare(dpr, 1.0) ? QString() : QStringLiteral("@%1x").arg(dpr);
	QStringList   written;
	auto          save = [&](QWidget& widget, const QString& name) {
        Settle();
        const QString path = QDir(folder).filePath(name + suffix + QStringLiteral(".png"));
        if (widget.grab().save(path)) written << path;
	};

	// The setup's pages.
	for (int step = 0; step < 4; step++) {
		window.showSetup(step);
		static const char* names[] = {"setup-1-pc", "setup-2-game", "setup-3-display", "setup-4-shaders"};
		save(window, QString::fromLatin1(names[step]));
	}
	// Home: idle, starting, precompiling, after a failed run.
	window.showPage(MainWindow::Page::Home);
	save(window, QStringLiteral("home-idle"));
	// The smallest window.
	window.resize(window.minimumSize());
	save(window, QStringLiteral("home-min"));
	window.resize(960, 600);
	window.home()->setPlay(PlayButton::Look::Busy, QCoreApplication::translate("MainWindow", "Preparing shaders"), ThinBar::Mode::Busy);
	window.home()->setLogShown(true);
	window.home()->appendLog(QStringLiteral("game:     Demon's Souls, PPSA01341 01.007.000\nshaders:  prefetch inputs to be made, static pipeline cache not made\n"
	                                        "shaders:  making the background shader preparation inputs for this graphics card"));
	save(window, QStringLiteral("home-starting"));
	window.home()->setLogShown(false);
	window.home()->setPlay(PlayButton::Look::Play, QCoreApplication::translate("MainWindow", "PLAY"), ThinBar::Mode::Hidden);
	// (A sample of the precompile card: these numbers are made up for the picture.)
	PrecompileProgress progress;
	progress.stage       = PrecompileProgress::Stage::Compiling;
	progress.done        = 23840;
	progress.total       = 63179;
	progress.text        = QStringLiteral("Compiling shaders 23,840 / 63,179");
	progress.fraction    = 0.05 + 0.90 * progress.done / progress.total;
	progress.elapsedMs   = 18 * 60 * 1000;
	progress.remainingMs = 31 * 60 * 1000;
	window.home()->showPrecompile(progress);
	save(window, QStringLiteral("home-precompile"));
	window.home()->hidePrecompile();
	window.home()->showError(QCoreApplication::translate("MainWindow", "The game closed unexpectedly (error %1).").arg(QStringLiteral("0xC0000005")),
	                         QStringLiteral("— 20261014-221530-412.err.log —\n(sample) The last lines of the game's error log appear here.\n"
	                                        "— 20261014-221530-412.out.log —\n(sample) The last lines of the game's output appear here."));
	window.home()->setErrorDetailsShown(true);
	save(window, QStringLiteral("home-error"));
	window.home()->hideError();
	// Settings with a row under the mouse, Help.
	window.showPage(MainWindow::Page::Settings);
	window.settingsPage()->simulateHover(QStringLiteral("screen"));
	save(window, QStringLiteral("settings"));
	window.showPage(MainWindow::Page::Help);
	save(window, QStringLiteral("help"));
	// A message dialog.
	MessageDialog dialog(&window, Tone::Warn, QCoreApplication::translate("MainWindow", "This PC may run short"),
	                     QCoreApplication::translate("MainWindow", "Windows can give programs only %1 more memory (RAM plus page file) and the emulator "
	                                                               "needs about 34 GB: it may close mid-game. Close other programs, or set the page file "
	                                                               "to System managed size.")
	                         .arg(QStringLiteral("21 GB")),
	                     {QCoreApplication::translate("MainWindow", "Cancel"), QCoreApplication::translate("MainWindow", "Start anyway")});
	dialog.setAttribute(Qt::WA_DontShowOnScreen);
	dialog.adjustSize();
	dialog.show();
	save(dialog, QStringLiteral("dialog"));
	dialog.hide();
	window.hide();
	for (const QString& path: written) std::printf("%s\n", QDir::toNativeSeparators(path).toLocal8Bit().constData());
	std::fflush(stdout);
	return written.size() == 12 ? 0 : 1;
}

} // namespace Screenshots
