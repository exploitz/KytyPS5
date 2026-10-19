#include "ui/HomePage.h"

#include "ui/Theme.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString Duration(qint64 ms) {
	const qint64 minutes = (ms + 59999) / 60000;
	if (minutes < 60) return QCoreApplication::translate("HomePage", "%n min", nullptr, static_cast<int>(minutes));
	return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

QString Remaining(qint64 ms) {
	if (ms < 0) return QCoreApplication::translate("HomePage", "Estimating the time left…");
	if (ms < 60000) return QCoreApplication::translate("HomePage", "Less than a minute left");
	return QCoreApplication::translate("HomePage", "About %1 left").arg(Duration(ms));
}

// The key art scaled for the banner, the icon for its tile (both off the UI thread: pic0.png is a 4K PNG).
QImage LoadArt(const QString& path) {
	if (path.isEmpty()) return {};
	QImageReader reader(path);
	QImage       art = reader.read();
	if (!art.isNull() && art.width() > 2560) art = art.scaledToWidth(2560, Qt::SmoothTransformation);
	return art.convertToFormat(QImage::Format_RGB32);
}

QImage LoadIcon(const QString& path) {
	if (path.isEmpty()) return {};
	QImage icon(path);
	if (!icon.isNull() && icon.width() > 256) icon = icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	return icon;
}

} // namespace

HomePage::HomePage(QWidget* parent): QWidget(parent) {
	setObjectName(QStringLiteral("Page"));
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	// The key art over the top of the page; the game's header and PLAY sit on its lower edge, where it fades into the page.
	m_hero = new HeroBanner(this);
	root->addWidget(m_hero);
	m_update = new Chip(m_hero);
	m_update->hide();
	connect(m_update, &Chip::clicked, this, &HomePage::updateClicked);
	auto* stage = new QVBoxLayout(m_hero);
	stage->setContentsMargins(32, 20, 32, 16);
	stage->setSpacing(18);
	stage->addStretch(1);
	auto* header = new QHBoxLayout;
	header->setSpacing(18);
	m_icon = new QLabel(m_hero);
	m_icon->setFixedSize(72, 72);
	header->addWidget(m_icon, 0, Qt::AlignVCenter);
	auto* titles = new QVBoxLayout;
	titles->setSpacing(2);
	titles->addStretch(1);
	m_title = makeLabel({}, nullptr, m_hero);
	m_title->setFont(Theme::font(22, QFont::DemiBold));
	titles->addWidget(m_title);
	m_line = makeLabel({}, nullptr, m_hero);
	m_line->setTextFormat(Qt::RichText);
	m_line->setStyleSheet(QStringLiteral("color: #C4C4C4;"));
	titles->addWidget(m_line);
	titles->addStretch(1);
	header->addLayout(titles, 1);
	stage->addLayout(header);

	// PLAY and the status beside it.
	auto* row = new QHBoxLayout;
	row->setSpacing(24);
	auto* play_column = new QVBoxLayout;
	play_column->setSpacing(6);
	m_play = new PlayButton(m_hero);
	m_play->setFixedWidth(240);
	connect(m_play, &PlayButton::clicked, this, &HomePage::playClicked);
	play_column->addWidget(m_play);
	m_play_bar = new ThinBar(m_hero);
	m_play_bar->setFixedWidth(240);
	play_column->addWidget(m_play_bar);
	m_play_reason = makeLabel({}, "danger", m_hero);
	m_play_reason->setWordWrap(true);
	m_play_reason->setFixedWidth(240);
	m_play_reason->setFont(Theme::font(9));
	m_play_reason->hide();
	play_column->addWidget(m_play_reason);
	play_column->addStretch(1);
	row->addLayout(play_column);

	// Beside PLAY: the shader chip and the summary, or the precompile's card (the one shown takes the room).
	m_status            = new QWidget(m_hero);
	auto* status_layout = new QVBoxLayout(m_status);
	status_layout->setContentsMargins(0, 0, 0, 0);
	status_layout->setSpacing(0);
	m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto* chips        = new QWidget(m_status);
	m_chips            = chips;
	auto* chips_layout = new QVBoxLayout(chips);
	chips_layout->setContentsMargins(0, 0, 0, 0);
	chips_layout->setSpacing(8);
	auto* chip_row = new QHBoxLayout;
	chip_row->setSpacing(8);
	m_shader_chip = new Chip(chips);
	connect(m_shader_chip, &Chip::clicked, this, [this] {
		if (m_shader_chip->hasAction()) emit shaderChipClicked();
	});
	chip_row->addWidget(m_shader_chip);
	chip_row->addStretch(1);
	chips_layout->addLayout(chip_row);
	m_shader_hint = makeLabel({}, "secondary", chips);
	m_shader_hint->setWordWrap(true);
	m_shader_hint->setFont(Theme::font(9.5));
	m_shader_hint->hide();
	chips_layout->addWidget(m_shader_hint);
	auto* summary_row = new QHBoxLayout;
	summary_row->setSpacing(10);
	m_summary = makeLabel({}, "secondary", chips);
	summary_row->addWidget(m_summary);
	auto* change = makeButton(tr("Change"), "link", chips);
	connect(change, &QPushButton::clicked, this, &HomePage::settingsClicked);
	summary_row->addWidget(change);
	summary_row->addStretch(1);
	chips_layout->addLayout(summary_row);
	status_layout->addWidget(chips);

	// The precompile's card.
	auto* card        = makeCard(m_status);
	m_card            = card;
	auto* card_layout = new QVBoxLayout(card);
	card_layout->setContentsMargins(16, 12, 14, 12);
	card_layout->setSpacing(8);
	auto* title_row = new QHBoxLayout;
	title_row->setSpacing(8);
	m_pc_icon = new StateIcon(Tone::Ok, 18, card);
	m_pc_icon->hide();
	title_row->addWidget(m_pc_icon);
	m_pc_title = makeLabel(tr("Precompiling shaders"), nullptr, card);
	m_pc_title->setFont(Theme::font(10.5, QFont::DemiBold));
	title_row->addWidget(m_pc_title);
	title_row->addStretch(1);
	m_pc_percent = makeLabel({}, "accent", card);
	m_pc_percent->setFont(Theme::font(10.5, QFont::DemiBold));
	title_row->addWidget(m_pc_percent);
	card_layout->addLayout(title_row);
	m_pc_bar = new ProgressBar(card);
	card_layout->addWidget(m_pc_bar);
	auto* step_row = new QHBoxLayout;
	m_pc_step      = makeLabel({}, nullptr, card);
	m_pc_step->setWordWrap(true);
	step_row->addWidget(m_pc_step, 1);
	m_pc_time = makeLabel({}, "secondary", card);
	step_row->addWidget(m_pc_time, 0, Qt::AlignTop);
	card_layout->addLayout(step_row);
	auto* stop_row = new QHBoxLayout;
	stop_row->setSpacing(10);
	m_pc_hint = makeLabel({}, "secondary", card);
	m_pc_hint->setFont(Theme::font(9));
	m_pc_hint->setWordWrap(true);
	stop_row->addWidget(m_pc_hint, 1);
	m_pc_report = makeButton(tr("Report a problem"), nullptr, card);
	m_pc_report->hide();
	connect(m_pc_report, &QPushButton::clicked, this, &HomePage::reportClicked);
	stop_row->addWidget(m_pc_report);
	m_pc_stop = makeButton(tr("Stop"), nullptr, card);
	m_pc_stop->setMinimumWidth(84);
	connect(m_pc_stop, &QPushButton::clicked, this, [this] {
		if (m_pc_finished) emit precompileCardClosed();
		else emit stopPrecompileClicked();
	});
	stop_row->addWidget(m_pc_stop);
	card_layout->addLayout(stop_row);
	status_layout->addWidget(card);
	row->addWidget(m_status, 1, Qt::AlignTop);
	stage->addLayout(row);

	auto* body   = new QWidget(this);
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(32, 8, 32, 12);
	layout->setSpacing(12);
	root->addWidget(body, 1);

	// A notice (missing files, no AVX2, no game).
	m_notice             = makeCard(body, "warn");
	auto* notice_layout  = new QHBoxLayout(m_notice);
	notice_layout->setContentsMargins(14, 10, 12, 10);
	notice_layout->setSpacing(12);
	m_notice_icon = new StateIcon(Tone::Warn, 20, m_notice);
	notice_layout->addWidget(m_notice_icon, 0, Qt::AlignTop);
	m_notice_text = makeLabel({}, nullptr, m_notice);
	m_notice_text->setWordWrap(true);
	notice_layout->addWidget(m_notice_text, 1);
	m_notice_action = makeButton({}, nullptr, m_notice);
	connect(m_notice_action, &QPushButton::clicked, this, &HomePage::noticeActionClicked);
	notice_layout->addWidget(m_notice_action, 0, Qt::AlignVCenter);
	m_notice->hide();
	layout->addWidget(m_notice);

	// The error card after a failed run: one sentence, its actions on the same line, the log's last lines under it.
	m_error            = makeCard(body, "danger");
	auto* error_layout = new QVBoxLayout(m_error);
	error_layout->setContentsMargins(14, 8, 8, 8);
	error_layout->setSpacing(8);
	auto* error_top = new QHBoxLayout;
	error_top->setSpacing(10);
	error_top->addWidget(new StateIcon(Tone::Danger, 20, m_error), 0, Qt::AlignVCenter);
	error_top->addSpacing(2);
	m_error_text = makeLabel({}, nullptr, m_error);
	m_error_text->setWordWrap(true);
	error_top->addWidget(m_error_text, 1);
	m_error_details = makeButton(tr("Show details"), "text", m_error);
	connect(m_error_details, &QPushButton::clicked, this, [this] { setErrorDetailsShown(!m_error_log->isVisible()); });
	error_top->addWidget(m_error_details, 0, Qt::AlignVCenter);
	auto* report = makeButton(tr("Report a problem"), nullptr, m_error);
	connect(report, &QPushButton::clicked, this, &HomePage::reportClicked);
	error_top->addWidget(report, 0, Qt::AlignVCenter);
	auto* close = new IconButton(Icons::Icon::Close, 26, m_error);
	close->setToolTip(tr("Dismiss"));
	close->setAccessibleName(tr("Dismiss"));
	connect(close, &IconButton::clicked, this, &HomePage::hideError);
	error_top->addWidget(close, 0, Qt::AlignVCenter);
	error_layout->addLayout(error_top);
	m_error_log = new QPlainTextEdit(m_error);
	m_error_log->setReadOnly(true);
	m_error_log->setFont(Theme::monoFont(8.5));
	m_error_log->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_error_log->setFixedHeight(104);
	m_error_log->hide();
	error_layout->addWidget(m_error_log);
	m_error->hide();
	layout->addWidget(m_error);

	// The live log (collapsed), else space.
	m_log = new QPlainTextEdit(body);
	m_log->setReadOnly(true);
	m_log->setFont(Theme::monoFont(8.5));
	m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_log->setMaximumBlockCount(4000);
	m_log->setPlaceholderText(tr("The game's output appears here while it runs."));
	m_log->hide();
	layout->addWidget(m_log, 1);
	m_log_spacer = new QWidget(body);
	layout->addWidget(m_log_spacer, 1);

	// Quick links.
	auto* footer = new QHBoxLayout;
	footer->setSpacing(4);
	m_log_toggle = makeButton(tr("Show log"), "text", body);
	connect(m_log_toggle, &QPushButton::clicked, this, [this] { setLogShown(!m_log->isVisible()); });
	footer->addWidget(m_log_toggle);
	footer->addStretch(1);
	auto* report_link = makeButton(tr("Report a problem"), "text", body);
	connect(report_link, &QPushButton::clicked, this, &HomePage::reportClicked);
	footer->addWidget(report_link);
	layout->addLayout(footer);

	setPlay(PlayButton::Look::Play, tr("PLAY"), ThinBar::Mode::Hidden);
	setLogShown(false);
	setStatusPage(0);
}

void HomePage::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	updateHeroHeight();
	placeUpdateChip();
}

void HomePage::updateHeroHeight() {
	// About 58% of the page is art; never less than its header and PLAY row need, with some art above them.
	const int needed = m_hero->layout()->minimumSize().height() + 64;
	m_hero->setFixedHeight(std::max(needed, qRound(height() * 0.58)));
}

void HomePage::setStatusPage(int index) {
	// (Only the one shown takes room, so PLAY's row stays as low as its content: on the art's edge.)
	m_status_page = index;
	m_chips->setVisible(index == 0);
	m_card->setVisible(index == 1);
	updateHeroHeight();
	// (Again once the layouts took the change.)
	QTimer::singleShot(0, this, &HomePage::updateHeroHeight);
}

void HomePage::placeUpdateChip() {
	m_update->resize(m_update->sizeHint());
	m_update->move(m_hero->width() - m_update->width() - 24, 20);
}

void HomePage::setGame(const GameInfo& game, bool hasGame, bool async) {
	const int generation = ++m_image_generation;
	if (!hasGame) {
		m_hero->setArt({});
		m_icon->hide();
		m_title->setText(tr("Choose your game"));
		m_line->setText(tr("Point the launcher at your Demon's Souls folder to play."));
		return;
	}
	m_title->setText(game.titleName.isEmpty() ? tr("Demon's Souls") : game.titleName);
	const QString state = game.tested()        ? QStringLiteral("<span style='color:#5CB85C'>%1</span>").arg(game.supportedOnly() ? tr("Supported") : tr("Tested"))
	                      : game.otherRegion() ? QStringLiteral("<span style='color:#E0A030'>%1</span>").arg(tr("Untested region"))
	                                           : QStringLiteral("<span style='color:#E0A030'>%1</span>").arg(tr("Untested version"));
	m_line->setText(game.paramRead ? QStringLiteral("%1 · %2 · %3").arg(game.titleId, game.displayVersion(), state)
	                               : tr("Unknown version · %1").arg(state));
	const QImage icon = LoadIcon(game.iconPath);
	if (!icon.isNull()) {
		// A rounded tile with a hairline edge.
		const qreal dpr = devicePixelRatioF();
		QPixmap     tile(QSize(72, 72) * dpr);
		tile.setDevicePixelRatio(dpr);
		tile.fill(Qt::transparent);
		QPainter p(&tile);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		QPainterPath clip;
		clip.addRoundedRect(QRectF(0, 0, 72, 72), 8, 8);
		p.setClipPath(clip);
		p.drawImage(QRectF(0, 0, 72, 72), icon);
		p.setClipping(false);
		p.setPen(QPen(QColor(255, 255, 255, 30), 1));
		p.drawRoundedRect(QRectF(0.5, 0.5, 71, 71), 8, 8);
		p.end();
		m_icon->setPixmap(tile);
	}
	m_icon->setVisible(!icon.isNull());
	const QString pic = game.picPath;
	if (!async) {
		m_hero->setArt(LoadArt(pic));
		return;
	}
	m_hero->setArt({});
	QPointer<HomePage> self(this);
	QThreadPool::globalInstance()->start([self, pic, generation] {
		const QImage art = LoadArt(pic);
		QMetaObject::invokeMethod(
		    qApp,
		    [self, art, generation] {
			    if (self != nullptr && self->m_image_generation == generation) self->m_hero->setArt(art);
		    },
		    Qt::QueuedConnection);
	});
}

void HomePage::setPlay(PlayButton::Look look, const QString& text, ThinBar::Mode bar, Tone barTone) {
	m_play->setLook(look, text);
	m_play_bar->setMode(bar, barTone);
}

PlayButton::Look HomePage::playLook() const {
	return m_play->look();
}

void HomePage::setPlayReason(const QString& reason) {
	m_play_reason->setText(reason);
	m_play_reason->setVisible(!reason.isEmpty());
}

void HomePage::setShaderState(Tone tone, const QString& text, const QString& action, const QString& hint) {
	m_shader_chip->setState(tone, text, action);
	m_shader_hint->setText(hint);
	m_shader_hint->setVisible(!hint.isEmpty());
}

void HomePage::setSummary(const QString& summary) {
	m_summary->setText(summary);
}

void HomePage::setUpdate(const QString& tag) {
	if (tag.isEmpty()) {
		m_update->hide();
		return;
	}
	m_update->setState(Tone::Accent, tr("Update available"), tag);
	placeUpdateChip();
	m_update->show();
	m_update->raise();
}

void HomePage::showPrecompile(const PrecompileProgress& progress) {
	m_pc_finished = false;
	m_pc_icon->hide();
	m_pc_report->hide();
	m_pc_stop->setText(tr("Stop"));
	m_pc_stop->show();
	m_pc_bar->show();
	m_pc_title->setText(tr("Precompiling shaders"));
	m_pc_percent->setText(QStringLiteral("%1%").arg(qRound(progress.fraction * 100)));
	m_pc_percent->show();
	m_pc_bar->setValue(progress.fraction);
	m_pc_step->setText(progress.readyToPlay && progress.stage == PrecompileProgress::Stage::Compiling
	                       ? progress.text + tr(" · ready to play")
	                       : progress.text);
	m_pc_time->setText(Remaining(progress.remainingMs));
	m_pc_hint->setText(tr("You can continue later"));
	setStatusPage(1);
}

void HomePage::showPrecompileResult(bool ok, const QString& title, const QString& detail) {
	m_pc_finished = true;
	m_pc_icon->setTone(ok ? Tone::Ok : Tone::Danger);
	m_pc_icon->show();
	m_pc_title->setText(title);
	m_pc_percent->setVisible(ok);
	m_pc_percent->setText(QStringLiteral("100%"));
	if (ok) m_pc_bar->setValue(1.0);
	m_pc_bar->setVisible(ok);
	m_pc_step->setText(detail);
	m_pc_time->clear();
	m_pc_hint->clear();
	m_pc_report->setVisible(!ok);
	m_pc_stop->setText(tr("Close"));
	setStatusPage(1);
}

void HomePage::hidePrecompile() {
	setStatusPage(0);
}

bool HomePage::precompileShown() const {
	return m_status_page == 1;
}

void HomePage::showError(const QString& sentence, const QString& details) {
	m_error_text->setText(sentence);
	m_error_log->setPlainText(details);
	m_error_log->verticalScrollBar()->setValue(m_error_log->verticalScrollBar()->maximum());
	setErrorDetailsShown(false);
	m_error->show();
}

void HomePage::hideError() {
	m_error->hide();
}

void HomePage::setErrorDetailsShown(bool shown) {
	m_error_log->setVisible(shown);
	m_error_details->setText(shown ? tr("Hide details") : tr("Show details"));
	if (shown) m_error_log->verticalScrollBar()->setValue(m_error_log->verticalScrollBar()->maximum());
}

void HomePage::showNotice(Tone tone, const QString& text, const QString& action) {
	m_notice->setProperty("card", tone == Tone::Danger ? QStringLiteral("danger") : QStringLiteral("warn"));
	m_notice->style()->unpolish(m_notice);
	m_notice->style()->polish(m_notice);
	m_notice_icon->setTone(tone);
	m_notice_text->setText(text);
	m_notice_action->setText(action);
	m_notice_action->setVisible(!action.isEmpty());
	m_notice->show();
}

void HomePage::hideNotice() {
	m_notice->hide();
}

void HomePage::appendLog(const QString& text) {
	m_log->appendPlainText(text);
}

QString HomePage::logText() const {
	return m_log->toPlainText();
}

bool HomePage::logShown() const {
	return m_log->isVisible();
}

void HomePage::setLogShown(bool shown) {
	m_log->setVisible(shown);
	m_log_spacer->setVisible(!shown);
	m_log_toggle->setText(shown ? tr("Hide log") : tr("Show log"));
	if (shown) m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}
