#include "ui/Choices.h"

#include "core/Features.h"
#include "core/Settings.h"

#include <QCoreApplication>

namespace Choices {

const QList<Choice>& output() {
	static const QList<Choice> choices = {
	    {QStringLiteral("4k"), QStringLiteral("4K (PS5)"),
	     QStringLiteral("The PS5's Performance mode: 1440p 3D, 4K picture. Best for 1440p and 4K screens.")},
	    {QStringLiteral("1080q"), QStringLiteral("1080p Quality"),
	     QStringLiteral("The 1440p 3D scaled down to 1080p: the sharpest image on a 1080p screen. About 3% slower than 1080p Performance.")},
	    {QStringLiteral("1080f"), QStringLiteral("1080p Performance"), QStringLiteral("1080p 3D. Faster on weaker graphics cards, slightly softer.")},
	};
	return choices;
}

const QList<Choice>& quality() {
	static const QList<Choice> choices = {
	    {QStringLiteral("standard"), QStringLiteral("Standard (PS5)"), QStringLiteral("Everything as the PS5 draws it.")},
	    {QStringLiteral("shadows"), QStringLiteral("Fewer shadows"),
	     QStringLiteral("Drops ambient and small-light shadows and shortens sun shadows. 12-17% faster, scenery unchanged. "
	                    "The best first step on a slower CPU.")},
	    {QStringLiteral("low"), QStringLiteral("Low"), QStringLiteral("Simpler shadows and distant detail. 9-15% faster; some objects get simpler.")},
	    {QStringLiteral("lowest"), QStringLiteral("Lowest"), QStringLiteral("The most simplified. 23-30% faster; some objects disappear.")},
	};
	return choices;
}

const QList<Choice>& antiAliasing() {
	static const QList<Choice> choices = [] {
		QList<Choice> list = {
		    {QStringLiteral("taa"), QStringLiteral("Game TAA"), QStringLiteral("The game's own anti-aliasing.")},
		    {QStringLiteral("fsr4"), QStringLiteral("FSR 4 (experimental)"),
		     QStringLiteral("Cleaner edges than the game's TAA, about 13% slower. Built from a community-hosted FSR 4 source.")},
		};
		// (Not in this release: the emulator cannot load the add-on yet.)
		if (kDlssAddonSupported) {
			list.append({QStringLiteral("dlss"), QStringLiteral("DLSS (add-on)"),
			             QStringLiteral("NVIDIA DLAA: cleaner edges, about 9% slower. Needs an RTX card and the free DLSS add-on.")});
		}
		return list;
	}();
	return choices;
}

const QList<Choice>& frameGeneration() {
	static const QList<Choice> choices = {
	    {QStringLiteral("off"), QStringLiteral("Off"), QStringLiteral("Only the game's own frames.")},
	    {QStringLiteral("fsr"), QStringLiteral("FSR 3.1"),
	     QStringLiteral("A generated frame between each pair of real ones for smoother motion. Slightly more input lag; best near 60 fps.")},
	};
	return choices;
}

const QList<Choice>& render() {
	static const QList<Choice> choices = {
	    {QStringLiteral("1440"), QStringLiteral("1440p (PS5)"), {}}, {QStringLiteral("1296"), QStringLiteral("1296p"), {}},
	    {QStringLiteral("1152"), QStringLiteral("1152p"), {}},       {QStringLiteral("1080"), QStringLiteral("1080p"), {}},
	    {QStringLiteral("900"), QStringLiteral("900p"), {}},
	};
	return choices;
}

const QList<Choice>& ratio() {
	static const QList<Choice> choices = {
	    {QStringLiteral("16:9"), QStringLiteral("16:9 (PS5)"), QStringLiteral("The game's own shape.")},
	    {QStringLiteral("21:9"), QStringLiteral("21:9"),
	     QStringLiteral("For ultrawide screens: the 3D rendered wider, seeing more to the sides. The HUD keeps its shape; menus "
	                    "are stretched, movies stay 16:9. Only with 4K output.")},
	    {QStringLiteral("32:9"), QStringLiteral("32:9"),
	     QStringLiteral("For super-ultrawide screens, as 21:9 but wider still. Only with 4K output.")},
	};
	return choices;
}

const QList<Choice>& screen() {
	static const QList<Choice> choices = {
	    {QStringLiteral("fullscreen"), QStringLiteral("Fullscreen"), {}},
	    {QStringLiteral("1280x720"), QStringLiteral("Window 1280×720"), {}},
	    {QStringLiteral("1920x1080"), QStringLiteral("Window 1920×1080"), {}},
	    {QStringLiteral("2560x1440"), QStringLiteral("Window 2560×1440"), {}},
	    {QStringLiteral("3840x2160"), QStringLiteral("Window 3840×2160"), {}},
	};
	return choices;
}

const QList<Choice>& fps() {
	static const QList<Choice> choices = {
	    {QStringLiteral("60"), QStringLiteral("60 (PS5)"), QStringLiteral("The game's own limit.")},
	    {QStringLiteral("120"), QStringLiteral("120"),
	     QStringLiteral("Lets the game run above 60 where your PC can. The game keeps its speed; movies, menus and loading screens "
	                    "stay at 60.")},
	};
	return choices;
}

const QList<Choice>& overlay() {
	static const QList<Choice> choices = {
	    {QStringLiteral("0"), QStringLiteral("Off"), QStringLiteral("No overlay.")},
	    {QStringLiteral("1"), QStringLiteral("Frame rate"), QStringLiteral("The frame rate and a frame-time graph at the top right of the game.")},
	    {QStringLiteral("2"), QStringLiteral("Detailed"), QStringLiteral("Adds the render thread's time and GPU, CPU and memory use.")},
	};
	return choices;
}

const QStringList& languages() {
	static const QStringList names = {
	    QStringLiteral("Japanese"),           QStringLiteral("English (United States)"), QStringLiteral("French (France)"),
	    QStringLiteral("Spanish (Spain)"),    QStringLiteral("German"),                  QStringLiteral("Italian"),
	    QStringLiteral("Dutch"),              QStringLiteral("Portuguese (Portugal)"),   QStringLiteral("Russian"),
	    QStringLiteral("Korean"),             QStringLiteral("Chinese (Traditional)"),   QStringLiteral("Chinese (Simplified)"),
	    QStringLiteral("Finnish"),            QStringLiteral("Swedish"),                 QStringLiteral("Danish"),
	    QStringLiteral("Norwegian"),          QStringLiteral("Polish"),                  QStringLiteral("Portuguese (Brazil)"),
	    QStringLiteral("English (United Kingdom)"), QStringLiteral("Turkish"),           QStringLiteral("Spanish (Latin America)"),
	    QStringLiteral("Arabic"),             QStringLiteral("French (Canada)"),         QStringLiteral("Czech"),
	    QStringLiteral("Hungarian"),          QStringLiteral("Greek"),                   QStringLiteral("Romanian"),
	    QStringLiteral("Thai"),               QStringLiteral("Vietnamese"),              QStringLiteral("Indonesian"),
	};
	return names;
}

int indexOf(const QList<Choice>& choices, const QString& value) {
	for (int i = 0; i < choices.size(); i++) {
		if (choices.at(i).value == value) return i;
	}
	return 0;
}

QString label(const QList<Choice>& choices, const QString& value) {
	return choices.value(indexOf(choices, value)).label;
}

QString renderDescription() {
	return QStringLiteral("Only with 4K output. Lower values render the 3D smaller for weaker graphics cards; the HUD stays sharp.");
}

QString screenDescription() {
	return QStringLiteral("Fullscreen at your screen's resolution, or a window. F11 or Alt+Enter switches in game.");
}

QString aspectDescription() {
	return QStringLiteral("Black bars on ultrawide or 16:10 screens instead of stretching.");
}

QString vsyncDescription() {
	return QStringLiteral("Prevents tearing. Off gives the lowest input lag.");
}

QString hdrDescription() {
#if defined(Q_OS_WIN)
	return QStringLiteral("For HDR screens. Turn on HDR in Windows display settings first.");
#else
	return QStringLiteral("For HDR screens. Turn on HDR in your system's display settings first.");
#endif
}

QString languageDescription() {
	return QStringLiteral("The console language the game uses for text and voice.");
}

QString redzoneDescription() {
#if defined(Q_OS_WIN)
	return QStringLiteral("Keep on. Prevents random crashes from a Windows quirk.");
#else
	return QStringLiteral("Keep on. Prevents random crashes from how exceptions reach the game.");
#endif
}

QString ecoresDescription() {
	return QStringLiteral("Runs the precompile on the efficiency cores only, so the PC stays responsive. Much slower: by default it "
	                      "runs on the performance cores, where it is fastest.");
}

QString summary(const LauncherSettings& s) {
	QStringList parts;
	parts << label(output(), s.output);
	if (s.quality == QStringLiteral("standard")) parts << QStringLiteral("Standard detail");
	else if (s.quality == QStringLiteral("shadows")) parts << QStringLiteral("Fewer shadows");
	else parts << label(quality(), s.quality) + QStringLiteral(" detail");
	if (s.output == QStringLiteral("4k") && s.ratio != QStringLiteral("16:9")) parts << s.ratio;
	parts << label(screen(), s.screen);
	if (s.aa == QStringLiteral("fsr4")) parts << QStringLiteral("FSR 4");
	else if (s.aa == QStringLiteral("dlss")) parts << QStringLiteral("DLSS");
	if (s.framegen == QStringLiteral("fsr")) parts << QStringLiteral("Frame generation");
	if (s.fps == 120) parts << QStringLiteral("120 fps");
	if (s.hdr) parts << QStringLiteral("HDR");
	return parts.join(QStringLiteral(" · "));
}

} // namespace Choices
