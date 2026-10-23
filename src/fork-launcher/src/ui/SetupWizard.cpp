#include "ui/SetupWizard.h"

#include "core/Caches.h"
#include "core/Precompile.h"
#include "platform/Platform.h"
#include "ui/Choices.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

Tone ToneOf(CheckItem::State state) {
	return state == CheckItem::State::Ok ? Tone::Ok : state == CheckItem::State::Warn ? Tone::Warn : Tone::Danger;
}

QPixmap RoundedIcon(const QString& path, int size, qreal dpr) {
	const QImage image(path);
	if (image.isNull()) return {};
	QPixmap pixmap(QSize(size, size) * dpr);
	pixmap.setDevicePixelRatio(dpr);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	QPainterPath clip;
	clip.addRoundedRect(QRectF(0, 0, size, size), 8, 8);
	p.setClipPath(clip);
	p.drawImage(QRectF(0, 0, size, size), image);
	return pixmap;
}

QString TimeText(int minutes) {
	if (minutes < 90) return QCoreApplication::translate("SetupWizard", "about %1 minutes").arg(minutes);
	return QCoreApplication::translate("SetupWizard", "about %1 hours").arg(QLocale(QLocale::English).toString(minutes / 60.0, 'f', 1));
}

// The primary screen in physical pixels (a 4K screen at 150% is still 2160 lines).
QSize PrimaryScreen() {
	const QScreen* screen = QGuiApplication::primaryScreen();
	return screen != nullptr ? (QSizeF(screen->size()) * screen->devicePixelRatio()).toSize() : QSize(1920, 1080);
}

} // namespace

SetupWizard::SetupWizard(const Package& package, SettingsStore* store, bool screenshotMode, QWidget* parent)
    : QWidget(parent), m_package(package), m_store(store), m_screenshots(screenshotMode) {
	setObjectName(QStringLiteral("Page"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(48, 28, 48, 24);
	layout->setSpacing(0);
	m_steps = new StepBar({tr("PC"), tr("Game"), tr("Display"), tr("Shaders")}, this);
	layout->addWidget(m_steps);
	layout->addSpacing(28);
	m_title = makeLabel({}, "title", this);
	layout->addWidget(m_title);
	layout->addSpacing(6);
	m_intro = makeLabel({}, "secondary", this);
	m_intro->setWordWrap(true);
	layout->addWidget(m_intro);
	layout->addSpacing(20);
	m_pages = new QStackedWidget(this);
	m_pages->addWidget(makePcPage());
	m_pages->addWidget(makeGamePage());
	m_pages->addWidget(makeDisplayPage());
	m_pages->addWidget(makeShadersPage());
	layout->addWidget(m_pages, 1);
	layout->addSpacing(16);
	auto* nav = new QHBoxLayout;
	nav->setSpacing(8);
	m_cancel = makeButton(tr("Cancel"), "text", this);
	connect(m_cancel, &QPushButton::clicked, this, &SetupWizard::cancelled);
	nav->addWidget(m_cancel);
	nav->addStretch(1);
	m_back = makeButton(tr("Back"), nullptr, this);
	m_back->setMinimumWidth(96);
	connect(m_back, &QPushButton::clicked, this, [this] { showStep(m_step - 1); });
	nav->addWidget(m_back);
	m_next = makeButton(tr("Next"), "primary", this);
	m_next->setMinimumWidth(110);
	connect(m_next, &QPushButton::clicked, this, [this] {
		if (m_step < 3) showStep(m_step + 1);
		else finish();
	});
	nav->addWidget(m_next);
	layout->addLayout(nav);
}

// --- 1. PC ------------------------------------------------------------------------------------------------------------

QWidget* SetupWizard::makePcPage() {
	auto* page   = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);
	auto* card = makeCard(page);
	m_pc_list  = new QVBoxLayout(card);
	m_pc_list->setContentsMargins(16, 8, 16, 8);
	m_pc_list->setSpacing(0);
	layout->addWidget(card);
	m_pc_note = makeLabel({}, "danger", page);
	m_pc_note->setWordWrap(true);
	m_pc_note->hide();
	layout->addWidget(m_pc_note);
	layout->addStretch(1);
	return page;
}

void SetupWizard::refreshPc() {
	while (QLayoutItem* item = m_pc_list->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	const SystemInfo       info  = SystemInfo::gather(m_package, !m_screenshots);
	const QList<CheckItem> items = systemChecks(info);
	bool                   bad   = false;
	for (int i = 0; i < items.size(); i++) {
		const CheckItem& item = items.at(i);
		if (i > 0) m_pc_list->addWidget(makeSeparator());
		auto* row    = new QWidget;
		auto* layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 9, 0, 9);
		layout->setSpacing(12);
		layout->addWidget(new StateIcon(ToneOf(item.state), 18), 0, Qt::AlignTop);
		auto* text = new QVBoxLayout;
		text->setSpacing(2);
		auto* line = makeLabel(item.line);
		line->setWordWrap(true);
		text->addWidget(line);
		if (item.state != CheckItem::State::Ok && !item.hint.isEmpty()) {
			auto* hint = makeLabel(item.hint, "secondary");
			hint->setFont(Theme::font(9));
			hint->setWordWrap(true);
			text->addWidget(hint);
		}
		layout->addLayout(text, 1);
		m_pc_list->addWidget(row);
		bad = bad || item.state == CheckItem::State::Bad;
	}
	m_pc_note->setText(tr("The items marked with a cross keep the game from running. You can finish the setup now and fix them later."));
	m_pc_note->setVisible(bad);
}

// --- 2. Game ----------------------------------------------------------------------------------------------------------

QWidget* SetupWizard::makeGamePage() {
	auto* page   = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);
	auto* row = new QHBoxLayout;
	row->setSpacing(8);
	m_game_path = new QLineEdit(page);
	m_game_path->setPlaceholderText(tr("The game folder, for example C:\\Games\\PPSA01341-app0"));
	connect(m_game_path, &QLineEdit::editingFinished, this, [this] { setGame(m_game_path->text().trimmed()); });
	row->addWidget(m_game_path, 1);
	auto* browse = makeButton(tr("Browse folder…"), nullptr, page);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString start = QFileInfo(m_game_path->text()).isDir() ? m_game_path->text() : QDir::homePath();
		const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the game folder (with eboot.bin and sce_sys)"), start);
		if (!folder.isEmpty()) setGame(QDir::toNativeSeparators(folder));
	});
	row->addWidget(browse);
	auto* zar = makeButton(tr("Choose .zar file…"), nullptr, page);
	connect(zar, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getOpenFileName(this, tr("Choose the game's .zar file"), QFileInfo(m_game_path->text()).absolutePath(),
		                                                  tr("ZArchive (*.zar)"));
		if (!file.isEmpty()) setGame(QDir::toNativeSeparators(file));
	});
	row->addWidget(zar);
	layout->addLayout(row);

	m_game_card       = makeCard(page);
	auto* card_layout = new QHBoxLayout(m_game_card);
	card_layout->setContentsMargins(14, 14, 16, 14);
	card_layout->setSpacing(16);
	m_game_icon = new QLabel(m_game_card);
	m_game_icon->setFixedSize(64, 64);
	card_layout->addWidget(m_game_icon);
	auto* text = new QVBoxLayout;
	text->setSpacing(4);
	text->addStretch(1);
	m_game_title = makeLabel({}, nullptr, m_game_card);
	m_game_title->setFont(Theme::font(13, QFont::DemiBold));
	text->addWidget(m_game_title);
	m_game_line = makeLabel({}, "secondary", m_game_card);
	m_game_line->setTextFormat(Qt::RichText);
	text->addWidget(m_game_line);
	text->addStretch(1);
	card_layout->addLayout(text, 1);
	m_game_card->hide();
	layout->addWidget(m_game_card);

	m_game_message      = new QWidget(page);
	auto* message       = new QHBoxLayout(m_game_message);
	message->setContentsMargins(2, 0, 0, 0);
	message->setSpacing(10);
	m_game_message_icon = new StateIcon(Tone::Danger, 18, m_game_message);
	message->addWidget(m_game_message_icon, 0, Qt::AlignTop);
	m_game_message_text = makeLabel({}, nullptr, m_game_message);
	m_game_message_text->setWordWrap(true);
	message->addWidget(m_game_message_text, 1);
	m_game_message->hide();
	layout->addWidget(m_game_message);

	auto* note = makeLabel(tr("Use only a dump of a game you own. Tested: PPSA01341 version 1.07; 1.05 is supported."), "secondary", page);
	note->setFont(Theme::font(9));
	layout->addStretch(1);
	layout->addWidget(note);
	return page;
}

void SetupWizard::setGame(const QString& requested) {
	QString path = requested;
	m_game_path->setText(path);
	m_game_ok = false;
	m_game_card->hide();
	m_game_message->hide();
	auto message = [&](Tone tone, const QString& text) {
		m_game_message_icon->setTone(tone);
		m_game_message_text->setText(text);
		m_game_message->show();
	};
	if (!path.isEmpty() && !GameInfo::isGamePath(path) && QFileInfo(path).isDir()) {
		// The folder above the game's: its one game folder.
		QStringList games;
		for (const QFileInfo& sub: QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			if (GameInfo::isGamePath(sub.absoluteFilePath())) games << QDir::toNativeSeparators(sub.absoluteFilePath());
		}
		if (games.size() == 1) {
			path = games.first();
			m_game_path->setText(path);
		}
	}
	if (path.isEmpty()) {
		m_next->setEnabled(m_step != 1);
		return;
	}
	if (!QFileInfo::exists(path)) {
		message(Tone::Danger, tr("That folder doesn't exist. Choose the folder that holds eboot.bin and the sce_sys folder."));
	} else if (!GameInfo::isGamePath(path)) {
		message(Tone::Danger, tr("This folder has no eboot.bin. Choose the folder that holds eboot.bin and the sce_sys folder."));
	} else {
		if (path.endsWith(QStringLiteral(".zar"), Qt::CaseInsensitive) && !m_screenshots) QApplication::setOverrideCursor(Qt::WaitCursor);
		m_game = GameInfo::read(path, m_screenshots ? QString() : m_package.tool);
		if (QApplication::overrideCursor() != nullptr) QApplication::restoreOverrideCursor();
		if (m_game.isZar && !m_game.paramRead) {
			message(Tone::Danger, tr("This .zar file could not be read. Make sure it is a complete ZArchive of the game folder, or choose the "
			                         "extracted folder instead."));
		} else {
			m_game_ok = true;
			m_game_icon->setPixmap(RoundedIcon(m_game.iconPath, 64, devicePixelRatioF()));
			m_game_icon->setVisible(!m_game.iconPath.isEmpty());
			m_game_title->setText(m_game.titleName.isEmpty() ? tr("Demon's Souls") : m_game.titleName);
			const QString state = m_game.tested()        ? QStringLiteral("<span style='color:#5CB85C'>%1</span>").arg(m_game.supportedOnly() ? tr("Supported") : tr("Tested"))
			                      : m_game.otherRegion() ? QStringLiteral("<span style='color:#E0A030'>%1</span>").arg(tr("Untested region"))
			                                             : QStringLiteral("<span style='color:#E0A030'>%1</span>").arg(tr("Untested version"));
			m_game_line->setText(m_game.paramRead ? QStringLiteral("%1 · %2 · %3").arg(m_game.titleId, m_game.displayVersion(), state)
			                                      : tr("Version unknown (no sce_sys\\param.json)"));
			m_game_card->show();
			if (!m_game.paramRead) {
				message(Tone::Warn, tr("The game's version could not be read. It may still run; the shader precompile needs sce_sys\\param.json."));
			} else if (m_game.otherRegion()) {
				message(Tone::Warn, tr("%1 is another region's release: it should run like the tested PPSA01341 %2, but it is untested.")
				                        .arg(m_game.titleId, m_game.displayVersion()));
			} else if (!m_game.tested()) {
				message(Tone::Warn, tr("Version %1 is untested: it may not start or may crash. Version 1.07 is tested, 1.05 supported.")
				                        .arg(m_game.contentVersion));
			}
		}
	}
	if (m_step == 1) m_next->setEnabled(m_game_ok);
}

// --- 3. Display -------------------------------------------------------------------------------------------------------

QWidget* SetupWizard::makeDisplayPage() {
	auto* page   = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);
	auto* card        = makeCard(page);
	auto* card_layout = new QVBoxLayout(card);
	card_layout->setContentsMargins(16, 6, 16, 6);
	card_layout->setSpacing(0);
	auto add = [&](const QString& name, const QList<Choice>& choices, DarkCombo*& box, Tag*& tag, Tag*& use, QLabel*& note) {
		if (card_layout->count() > 0) card_layout->addWidget(makeSeparator(card));
		auto* row    = new QWidget(card);
		auto* rows   = new QVBoxLayout(row);
		rows->setContentsMargins(0, 10, 0, 10);
		rows->setSpacing(4);
		auto* top = new QHBoxLayout;
		top->setSpacing(12);
		auto* label = makeLabel(name, nullptr, row);
		label->setFixedWidth(150);
		top->addWidget(label);
		box = new DarkCombo(row);
		for (const Choice& choice: choices) box->addItem(choice.label, choice.value);
		box->setFixedWidth(250);
		top->addWidget(box);
		tag = new Tag(tr("Recommended"), Tone::Accent, row);
		top->addWidget(tag);
		// (When another choice is made: the recommended one, a click away.)
		use = new Tag({}, Tone::Accent, row);
		use->setClickable(true);
		top->addWidget(use);
		top->addStretch(1);
		rows->addLayout(top);
		note = makeLabel({}, "secondary", row);
		note->setFont(Theme::font(9));
		note->setWordWrap(true);
		note->setContentsMargins(162, 0, 0, 0);
		rows->addWidget(note);
		card_layout->addWidget(row);
		connect(box, &QComboBox::currentIndexChanged, this, &SetupWizard::refreshDisplay);
	};
	add(tr("Output"), Choices::output(), m_output, m_output_tag, m_output_use, m_output_note);
	add(tr("Graphics detail"), Choices::quality(), m_quality, m_quality_tag, m_quality_use, m_quality_note);
	add(tr("Display mode"), Choices::screen(), m_screen, m_screen_tag, m_screen_use, m_screen_note);
	connect(m_output_use, &Tag::clicked, this, [this] { m_output->setCurrentIndex(Choices::indexOf(Choices::output(), m_recommended_output)); });
	connect(m_quality_use, &Tag::clicked, this, [this] { m_quality->setCurrentIndex(Choices::indexOf(Choices::quality(), m_recommended_quality)); });
	connect(m_screen_use, &Tag::clicked, this, [this] { m_screen->setCurrentIndex(0); });
	layout->addWidget(card);
	m_display_context = makeLabel({}, "secondary", page);
	m_display_context->setFont(Theme::font(9));
	m_display_context->setWordWrap(true);
	layout->addWidget(m_display_context);
	layout->addStretch(1);
	return page;
}

void SetupWizard::refreshDisplay() {
	const QString output  = m_output->currentData().toString();
	const QString quality = m_quality->currentData().toString();
	const QString screen  = m_screen->currentData().toString();
	m_output_tag->setVisible(output == m_recommended_output);
	m_quality_tag->setVisible(quality == m_recommended_quality);
	m_screen_tag->setVisible(screen == QStringLiteral("fullscreen"));
	m_output_use->setText(tr("Recommended: %1").arg(Choices::label(Choices::output(), m_recommended_output)));
	m_quality_use->setText(tr("Recommended: %1").arg(Choices::label(Choices::quality(), m_recommended_quality)));
	m_screen_use->setText(tr("Recommended: %1").arg(Choices::label(Choices::screen(), QStringLiteral("fullscreen"))));
	m_output_use->setVisible(output != m_recommended_output);
	m_quality_use->setVisible(quality != m_recommended_quality);
	m_screen_use->setVisible(screen != QStringLiteral("fullscreen"));
	for (Tag* use: {m_output_use, m_quality_use, m_screen_use}) use->updateGeometry();
	m_output_note->setText(Choices::output().value(Choices::indexOf(Choices::output(), output)).description);
	m_quality_note->setText(Choices::quality().value(Choices::indexOf(Choices::quality(), quality)).description);
	m_screen_note->setText(Choices::screenDescription());
}

// --- 4. Shaders -------------------------------------------------------------------------------------------------------

QWidget* SetupWizard::makeShadersPage() {
	auto* page   = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);
	m_shader_text = makeLabel({}, nullptr, page);
	m_shader_text->setWordWrap(true);
	layout->addWidget(m_shader_text);
	layout->addSpacing(6);
	m_now   = new ChoiceCard(tr("Precompile now"), tr("Starts on the Home page when you finish. You can play meanwhile."), tr("Recommended"), page);
	m_later = new ChoiceCard(tr("Later"), tr("Start it any time from the Home page. Until then, new scenes may stutter."), {}, page);
	m_now->setChecked(true);
	layout->addWidget(m_now);
	layout->addWidget(m_later);
	m_shader_done     = new QWidget(page);
	auto* done_layout = new QHBoxLayout(m_shader_done);
	done_layout->setContentsMargins(0, 0, 0, 0);
	done_layout->setSpacing(10);
	done_layout->addWidget(new StateIcon(Tone::Ok, 18, m_shader_done), 0, Qt::AlignTop);
	auto* done_text = makeLabel(tr("The shaders are already precompiled for this graphics card. Nothing to do here."), nullptr, m_shader_done);
	done_text->setWordWrap(true);
	done_layout->addWidget(done_text, 1);
	m_shader_done->hide();
	layout->addWidget(m_shader_done);
	layout->addStretch(1);
	return page;
}

void SetupWizard::setShadersComplete(bool complete) {
	m_shaders_complete = complete;
	refreshShaders();
}

void SetupWizard::refreshShaders() {
	const int minutes = Precompile::estimateMinutes(Platform::logicalCpuCount());
	m_shader_text->setText(tr("Precompiling builds all of the game's shaders for your graphics card ahead of time, so new scenes don't "
	                          "stutter. On this PC it takes %1; you can play meanwhile, and stop and continue at any time.")
	                           .arg(TimeText(minutes)));
	m_shader_text->setVisible(!m_shaders_complete);
	m_now->setVisible(!m_shaders_complete);
	m_later->setVisible(!m_shaders_complete);
	m_shader_done->setVisible(m_shaders_complete);
}

// --- Navigation -------------------------------------------------------------------------------------------------------

void SetupWizard::begin(bool cancellable, int step) {
	m_cancellable = cancellable;
	m_cancel->setVisible(cancellable);
	const LauncherSettings& s = m_store->settings();
	// The recommendations: 1080p Quality on a screen of 1080 lines or fewer, Fewer shadows below 12 threads.
	const QSize screen   = PrimaryScreen();
	const int   threads  = Platform::logicalCpuCount();
	m_recommended_output  = screen.height() <= 1080 ? QStringLiteral("1080q") : QStringLiteral("4k");
	m_recommended_quality = threads < 12 ? QStringLiteral("shadows") : QStringLiteral("standard");
	m_display_context->setText(tr("Recommended for your %1×%2 main screen and your processor's %3 threads.")
	                               .arg(screen.width())
	                               .arg(screen.height())
	                               .arg(threads));
	const bool first = !s.setupDone;
	m_output->setCurrentIndex(Choices::indexOf(Choices::output(), first ? m_recommended_output : s.output));
	m_quality->setCurrentIndex(Choices::indexOf(Choices::quality(), first ? m_recommended_quality : s.quality));
	m_screen->setCurrentIndex(Choices::indexOf(Choices::screen(), first ? QStringLiteral("fullscreen") : s.screen));
	refreshDisplay();
	refreshPc();
	setGame(s.game);
	const GameInfo game = GameInfo::read(s.game, {});
	m_shaders_complete  = Caches::staticState(m_package, game.cacheId()) == Caches::StaticState::Done;
	refreshShaders();
	showStep(step);
}

void SetupWizard::showStep(int step) {
	m_step = std::clamp(step, 0, 3);
	m_steps->setCurrent(m_step);
	m_pages->setCurrentIndex(m_step);
	static const char* titles[] = {QT_TR_NOOP("Your PC"), QT_TR_NOOP("Your game"), QT_TR_NOOP("Display"), QT_TR_NOOP("Shaders")};
	static const char* intros[] = {
	    QT_TR_NOOP("A quick check of what this PC brings for the emulator."),
	    QT_TR_NOOP("Choose your Demon's Souls dump: the folder with eboot.bin and sce_sys, or a .zar file of it."),
	    QT_TR_NOOP("Settings picked for this PC. You can change them any time in Settings."),
	    QT_TR_NOOP("Smooth play from the first minute."),
	};
	m_title->setText(tr(titles[m_step]));
	m_intro->setText(tr(intros[m_step]));
	m_back->setVisible(m_step > 0);
	m_next->setText(m_step == 3 ? tr("Finish") : tr("Next"));
	m_next->setEnabled(m_step != 1 || m_game_ok);
	if (m_step == 3) {
		// The shaders of the game chosen on step 2.
		m_shaders_complete = m_game_ok && Caches::staticState(m_package, m_game.cacheId()) == Caches::StaticState::Done;
		refreshShaders();
	}
}

void SetupWizard::finish() {
	LauncherSettings& s = m_store->settings();
	s.game              = m_game_path->text().trimmed();
	s.output            = m_output->currentData().toString();
	s.quality           = m_quality->currentData().toString();
	s.screen            = m_screen->currentData().toString();
	s.setupDone         = true;
	m_store->save();
	emit finished(!m_shaders_complete && m_now->isChecked());
}
