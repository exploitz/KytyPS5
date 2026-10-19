// KytyPS5 Launcher: the player's front end of this Demon's Souls fork (the emulator in the same folder).
//   (no options)         the launcher; the setup on its first run
//   --setup              the setup again
//   --dry-run            writes dry-run.txt next to the launcher: the play launch's switches and command line (as
//                        run-windows.ps1 -DryRun prints them), the steps around it and the precompile's first commands
//   --screenshots DIR    renders the pages into DIR\*.png off screen (nothing is shown, started or saved)
//   --version            prints the version

#include "core/DryRun.h"
#include "core/Package.h"
#include "core/Version.h"
#include "platform/Platform.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/Screenshots.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>

#include <cstdio>

namespace {

void Print(const QString& text) {
	std::fputs(text.toLocal8Bit().constData(), stdout);
	std::fflush(stdout);
}

QString Usage() {
	return QStringLiteral("KytyPS5 Launcher %1\n"
	                      "  --setup             the setup again\n"
	                      "  --dry-run           write dry-run.txt next to the launcher (the launch and the precompile, nothing started)\n"
	                      "  --screenshots DIR   render the pages into DIR\\*.png off screen\n"
	                      "  --version           print the version\n")
	    .arg(Version::text());
}

// One launcher per folder and user: a second one brings the first to the front.
QString InstanceName(const Package& package) {
	const QByteArray key = (QDir::cleanPath(package.root).toLower() + '|' + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"))).toUtf8();
	return QStringLiteral("KytyPS5Launcher-") + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16));
}

} // namespace

int main(int argc, char* argv[]) {
	QStringList arguments;
	for (int i = 1; i < argc; i++) arguments << QString::fromLocal8Bit(argv[i]);

	// The console modes need no window system.
	if (arguments.contains(QStringLiteral("--version")) || arguments.contains(QStringLiteral("--dry-run")) ||
	    arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h")) || arguments.contains(QStringLiteral("/?"))) {
		QCoreApplication application(argc, argv);
		Platform::attachParentConsole();
		if (arguments.contains(QStringLiteral("--version"))) {
			Print(QStringLiteral("KytyPS5 Launcher %1\n").arg(Version::text()));
			return 0;
		}
		if (!arguments.contains(QStringLiteral("--dry-run"))) {
			Print(Usage());
			return 0;
		}
		const Package package = Package::detect(QCoreApplication::applicationDirPath());
		const QString text    = DryRun::text(package);
		QSaveFile     file(package.dryRunFile);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 || !file.commit()) {
			Print(QStringLiteral("cannot write %1\n").arg(QDir::toNativeSeparators(package.dryRunFile)));
			return 1;
		}
		Print(QStringLiteral("dry run: %1\n").arg(QDir::toNativeSeparators(package.dryRunFile)));
		return 0;
	}

	QApplication application(argc, argv);
	QApplication::setApplicationName(QStringLiteral("KytyPS5 Launcher"));
	QApplication::setApplicationVersion(Version::text());
	QApplication::setWindowIcon(Icons::appIcon());
	// (The window decides when to quit: it hides while the game runs after a close.)
	QApplication::setQuitOnLastWindowClosed(false);
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("KytyPS5 Launcher"));
	const QCommandLineOption setup(QStringLiteral("setup"), QStringLiteral("Run the setup again."));
	const QCommandLineOption screenshots(QStringLiteral("screenshots"), QStringLiteral("Render the pages into <dir>."), QStringLiteral("dir"));
	parser.addOption(setup);
	parser.addOption(screenshots);
	parser.process(application);
	Theme::apply(application);
	const Package package = Package::detect(QCoreApplication::applicationDirPath());

	if (parser.isSet(screenshots)) {
		Platform::attachParentConsole();
		return Screenshots::run(package, parser.value(screenshots));
	}

	const QString name = InstanceName(package);
	{
		QLocalSocket other;
		other.connectToServer(name);
		if (other.waitForConnected(500)) {
			Platform::allowAnyProcessToSetForeground();
			other.write("activate\n");
			other.waitForBytesWritten(1000);
			other.disconnectFromServer();
			return 0;
		}
	}
	QLocalServer server;
	server.setSocketOptions(QLocalServer::UserAccessOption);
	if (!server.listen(name)) {
		// A stale socket of a launcher that crashed (Linux): removed, then listened to again.
		QLocalServer::removeServer(name);
		server.listen(name);
	}

	MainWindow::Options options;
	options.forceSetup = parser.isSet(setup);
	MainWindow window(package, options);
	QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
		while (QLocalSocket* connection = server.nextPendingConnection()) {
			QObject::connect(connection, &QLocalSocket::disconnected, connection, &QObject::deleteLater);
			window.activate();
		}
	});
	window.start();
	window.show();
	return QApplication::exec();
}
