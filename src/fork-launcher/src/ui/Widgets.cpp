#include "ui/Widgets.h"

#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionFocusRect>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace {

QColor Mix(const QColor& a, const QColor& b, qreal t) {
	return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t), static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
	                        static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t), static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

} // namespace

QColor toneColor(Tone tone) {
	switch (tone) {
		case Tone::Accent: return Theme::kAccent;
		case Tone::Ok: return Theme::kOk;
		case Tone::Warn: return Theme::kWarn;
		case Tone::Danger: return Theme::kDanger;
		case Tone::Neutral: break;
	}
	return Theme::kSecondary;
}

QLabel* makeLabel(const QString& text, const char* role, QWidget* parent) {
	auto* label = new QLabel(text, parent);
	if (role != nullptr) label->setProperty("role", QString::fromLatin1(role));
	label->setTextInteractionFlags(Qt::NoTextInteraction);
	return label;
}

QPushButton* makeButton(const QString& text, const char* kind, QWidget* parent) {
	auto* button = new QPushButton(text, parent);
	if (kind != nullptr) button->setProperty("kind", QString::fromLatin1(kind));
	button->setCursor(Qt::PointingHandCursor);
	// (Clicks do not move the focus ring onto buttons; Tab does.)
	button->setFocusPolicy(Qt::TabFocus);
	// (Enter clicks a focused button, as Space does.)
	button->setAutoDefault(true);
	return button;
}

QFrame* makeCard(QWidget* parent, const char* kind) {
	auto* card = new QFrame(parent);
	card->setProperty("card", QString::fromLatin1(kind));
	card->setAttribute(Qt::WA_StyledBackground);
	return card;
}

QFrame* makeSeparator(QWidget* parent) {
	auto* line = new QFrame(parent);
	line->setProperty("separator", true);
	line->setFixedHeight(1);
	return line;
}

// --- ToggleSwitch ---------------------------------------------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget* parent): QAbstractButton(parent) {
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setFixedSize(sizeHint());
	m_animation.setDuration(130);
	m_animation.setEasingCurve(QEasingCurve::OutCubic);
	connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
		m_position = value.toReal();
		update();
	});
}

QSize ToggleSwitch::sizeHint() const {
	return {46, 28};
}

void ToggleSwitch::checkStateSet() {
	// Set from code: no animation (the page is drawn at once).
	if (m_animation.state() == QAbstractAnimation::Running) return;
	m_position = isChecked() ? 1.0 : 0.0;
	update();
}

void ToggleSwitch::nextCheckState() {
	QAbstractButton::nextCheckState();
	m_animation.stop();
	m_animation.setStartValue(m_position);
	m_animation.setEndValue(isChecked() ? 1.0 : 0.0);
	m_animation.start();
}

void ToggleSwitch::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	if (!isEnabled()) p.setOpacity(0.35);
	const QRectF track(3, (height() - 22) / 2.0, 40, 22);
	const QColor off_color = m_hover ? QColor(0x3A, 0x3A, 0x3A) : QColor(0x30, 0x30, 0x30);
	const QColor on_color  = m_hover ? Theme::kAccentHover : Theme::kAccent;
	p.setPen(QPen(Mix(QColor(0x44, 0x44, 0x44), on_color, m_position), 1));
	p.setBrush(Mix(off_color, on_color, m_position));
	p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), 11, 11);
	const qreal  knob = 16;
	const qreal  x    = track.left() + 3 + (track.width() - 6 - knob) * m_position;
	p.setPen(Qt::NoPen);
	p.setBrush(Mix(QColor(0xA8, 0xA8, 0xA8), QColor(0x14, 0x14, 0x14), m_position));
	p.drawEllipse(QRectF(x, track.top() + 3, knob, knob));
	if (Theme::keyboardFocus(this)) {
		p.setPen(QPen(Theme::withAlpha(Theme::kAccentHover, 170), 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(track.adjusted(-2.5, -2.5, 2.5, 2.5), 13, 13);
	}
}

// --- DarkCombo --------------------------------------------------------------------------------------------------------

DarkCombo::DarkCombo(QWidget* parent): QComboBox(parent) {
	setItemDelegate(new QStyledItemDelegate(this));
	setCursor(Qt::PointingHandCursor);
	// (No value changes from scrolling the page over it.)
	setFocusPolicy(Qt::StrongFocus);
	setMinimumWidth(220);
	view()->setMouseTracking(true);
}

void DarkCombo::paintEvent(QPaintEvent* event) {
	QComboBox::paintEvent(event);
	QPainter     p(this);
	const bool   open  = view()->isVisible();
	const QColor color = !isEnabled() ? QColor(0x4A, 0x4A, 0x4A) : (underMouse() || open) ? Theme::kText : Theme::kSecondary;
	Icons::paint(p, open ? Icons::Icon::ChevronUp : Icons::Icon::ChevronDown, QRectF(width() - 28, (height() - 16) / 2.0, 16, 16), color, 1.6);
}

void DarkCombo::showPopup() {
	QComboBox::showPopup();
	// A small gap between the box and the list under it.
	QWidget* popup = view()->window();
	if (popup->y() >= mapToGlobal(QPoint(0, height())).y() - 2) popup->move(popup->x(), popup->y() + 4);
}

// --- NavButton --------------------------------------------------------------------------------------------------------

NavButton::NavButton(Icons::Icon icon, const QString& text, QWidget* parent): QAbstractButton(parent), m_icon(icon) {
	setText(text);
	setCheckable(true);
	setAutoExclusive(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setAccessibleName(text);
}

QSize NavButton::sizeHint() const {
	return {80, 64};
}

void NavButton::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void NavButton::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void NavButton::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF body = QRectF(rect()).adjusted(8, 2, -8, -2);
	if (isChecked()) {
		p.setPen(Qt::NoPen);
		p.setBrush(Theme::withAlpha(Theme::kAccent, 26));
		p.drawRoundedRect(body, Theme::kRadius, Theme::kRadius);
		p.setBrush(Theme::kAccent);
		p.drawRoundedRect(QRectF(0, height() / 2.0 - 12, 3, 24), 1.5, 1.5);
	} else if (m_hover) {
		p.setPen(Qt::NoPen);
		p.setBrush(Theme::kHover);
		p.drawRoundedRect(body, Theme::kRadius, Theme::kRadius);
	}
	if (Theme::keyboardFocus(this)) {
		p.setPen(QPen(Theme::withAlpha(Theme::kAccentHover, 160), 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(body, Theme::kRadius, Theme::kRadius);
	}
	const QColor color = isChecked() ? Theme::kAccent : m_hover ? Theme::kText : Theme::kSecondary;
	Icons::paint(p, m_icon, QRectF(width() / 2.0 - 11, 11, 22, 22), color, 1.7);
	p.setFont(Theme::font(8.5, isChecked() ? QFont::DemiBold : QFont::Normal));
	p.setPen(isChecked() ? Theme::kText : color);
	p.drawText(QRectF(0, 38, width(), 18), Qt::AlignHCenter | Qt::AlignTop, text());
}

// --- Chip -------------------------------------------------------------------------------------------------------------

Chip::Chip(QWidget* parent): QAbstractButton(parent) {
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Chip::setState(Tone tone, const QString& text, const QString& action) {
	m_tone   = tone;
	m_action = action;
	setText(text);
	setCursor(action.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
	setFocusPolicy(action.isEmpty() ? Qt::NoFocus : Qt::TabFocus);
	setEnabled(true);
	setAccessibleName(action.isEmpty() ? text : text + QStringLiteral(", ") + action);
	updateGeometry();
	update();
}

QSize Chip::sizeHint() const {
	const QFontMetrics text_metrics(Theme::font(9.5));
	const QFontMetrics action_metrics(Theme::font(9.5, QFont::DemiBold));
	int width = 12 + 8 + 8 + text_metrics.horizontalAdvance(text()) + 14;
	if (!m_action.isEmpty()) width += 18 + action_metrics.horizontalAdvance(m_action);
	return {width, 30};
}

void Chip::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void Chip::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void Chip::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QColor color     = toneColor(m_tone);
	const bool   clickable = !m_action.isEmpty();
	p.setPen(QPen(Theme::withAlpha(color, clickable && m_hover ? 120 : 72), 1));
	p.setBrush(Theme::withAlpha(color, clickable && m_hover ? 40 : 24));
	p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::kRadius, Theme::kRadius);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawEllipse(QRectF(12, height() / 2.0 - 4, 8, 8));
	const QFont text_font = Theme::font(9.5);
	p.setFont(text_font);
	p.setPen(Theme::kText);
	const int text_width = QFontMetrics(text_font).horizontalAdvance(text());
	p.drawText(QRect(28, 0, text_width + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
	if (clickable) {
		const int x = 28 + text_width + 8;
		p.setPen(Theme::kSecondary);
		p.drawText(QRect(x, 0, 10, height()), Qt::AlignCenter, QStringLiteral("·"));
		QFont action_font = Theme::font(9.5, QFont::DemiBold);
		action_font.setUnderline(m_hover);
		p.setFont(action_font);
		p.setPen(m_hover ? Theme::kAccentHover : Theme::kAccent);
		p.drawText(QRect(x + 14, 0, width() - x - 14, height()), Qt::AlignVCenter | Qt::AlignLeft, m_action);
	}
	if (Theme::keyboardFocus(this)) {
		p.setPen(QPen(Theme::withAlpha(Theme::kAccentHover, 170), 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), Theme::kRadius, Theme::kRadius);
	}
}

// --- Tag ------------------------------------------------------------------------------------------------------------

QFont tagFont() {
	return Theme::font(8, QFont::DemiBold);
}

QSizeF tagSize(const QString& text) {
	const QFontMetricsF metrics(tagFont());
	return {std::ceil(metrics.horizontalAdvance(text)) + 16, std::ceil(metrics.height()) + 6};
}

void paintTag(QPainter& painter, const QRectF& rect, const QString& text, Tone tone, bool hover) {
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	const QColor color = toneColor(tone);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Theme::withAlpha(color, hover ? 64 : 38));
	painter.drawRoundedRect(rect, 4, 4);
	painter.setFont(tagFont());
	painter.setPen(hover && tone == Tone::Accent ? Theme::kAccentHover : color);
	painter.drawText(rect, Qt::AlignCenter, text);
	painter.restore();
}

Tag::Tag(const QString& text, Tone tone, QWidget* parent): QAbstractButton(parent), m_tone(tone) {
	setText(text);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setFocusPolicy(Qt::NoFocus);
}

void Tag::setClickable(bool clickable) {
	m_clickable = clickable;
	setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
	setFocusPolicy(clickable ? Qt::TabFocus : Qt::NoFocus);
	update();
}

QSize Tag::sizeHint() const {
	return tagSize(text()).toSize();
}

void Tag::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void Tag::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void Tag::paintEvent(QPaintEvent*) {
	QPainter p(this);
	paintTag(p, QRectF(rect()), text(), m_tone, m_clickable && (m_hover || isDown()));
	if (Theme::keyboardFocus(this)) {
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(Theme::withAlpha(Theme::kAccentHover, 170), 1.2));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6), 4, 4);
	}
}

// --- ThinBar ----------------------------------------------------------------------------------------------------------

ThinBar::ThinBar(QWidget* parent): QWidget(parent), m_timer(new QTimer(this)) {
	setFixedHeight(3);
	m_timer->setInterval(16);
	connect(m_timer, &QTimer::timeout, this, [this] {
		m_phase = std::fmod(m_phase + 0.012, 1.0);
		update();
	});
}

void ThinBar::setMode(Mode mode, Tone tone) {
	m_mode = mode;
	m_tone = tone;
	if (mode == Mode::Busy) m_timer->start();
	else m_timer->stop();
	update();
}

void ThinBar::paintEvent(QPaintEvent*) {
	if (m_mode == Mode::Hidden) return;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect());
	const QColor color = toneColor(m_tone);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x26, 0x26, 0x26));
	p.drawRoundedRect(r, 1.5, 1.5);
	if (m_mode == Mode::Full) {
		p.setBrush(color);
		p.drawRoundedRect(r, 1.5, 1.5);
		return;
	}
	const qreal segment = r.width() * 0.35;
	const qreal x       = -segment + (r.width() + segment) * m_phase;
	QLinearGradient gradient(QPointF(x, 0), QPointF(x + segment, 0));
	gradient.setColorAt(0, Theme::withAlpha(color, 0));
	gradient.setColorAt(0.5, color);
	gradient.setColorAt(1, Theme::withAlpha(color, 0));
	p.setClipRect(r);
	p.setBrush(gradient);
	p.drawRect(QRectF(x, 0, segment, r.height()));
}

// --- ProgressBar ------------------------------------------------------------------------------------------------------

ProgressBar::ProgressBar(QWidget* parent): QWidget(parent), m_timer(new QTimer(this)) {
	setFixedHeight(6);
	m_timer->setInterval(33);
	connect(m_timer, &QTimer::timeout, this, [this] {
		m_shine = std::fmod(m_shine + 0.008, 1.4);
		update();
	});
}

void ProgressBar::setValue(double fraction) {
	m_value = std::clamp(fraction, 0.0, 1.0);
	if (m_value > 0 && m_value < 1 && isVisible()) m_timer->start();
	else m_timer->stop();
	update();
}

void ProgressBar::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect());
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x2A, 0x2A, 0x2A));
	p.drawRoundedRect(r, 3, 3);
	if (m_value <= 0) return;
	const QRectF fill(r.left(), r.top(), std::max(r.height(), r.width() * m_value), r.height());
	QLinearGradient gradient(fill.topLeft(), fill.topRight());
	gradient.setColorAt(0, Theme::kAccentPressed);
	gradient.setColorAt(1, Theme::kAccentHover);
	p.setBrush(gradient);
	p.drawRoundedRect(fill, 3, 3);
	if (m_timer->isActive()) {
		const qreal x = fill.left() - 80 + (fill.width() + 80) * std::min(1.0, m_shine);
		QLinearGradient shine(QPointF(x, 0), QPointF(x + 80, 0));
		shine.setColorAt(0, QColor(255, 255, 255, 0));
		shine.setColorAt(0.5, QColor(255, 255, 255, 60));
		shine.setColorAt(1, QColor(255, 255, 255, 0));
		QPainterPath clip;
		clip.addRoundedRect(fill, 3, 3);
		p.setClipPath(clip);
		p.setBrush(shine);
		p.drawRect(QRectF(x, fill.top(), 80, fill.height()));
	}
}

// --- StateIcon --------------------------------------------------------------------------------------------------------

StateIcon::StateIcon(Tone tone, int size, QWidget* parent): QWidget(parent), m_tone(tone), m_size(size) {
	setFixedSize(size, size);
}

void StateIcon::setTone(Tone tone) {
	m_tone = tone;
	update();
}

void StateIcon::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(toneColor(m_tone));
	p.drawEllipse(QRectF(0.5, 0.5, m_size - 1, m_size - 1));
	const Icons::Icon glyph = m_tone == Tone::Ok       ? Icons::Icon::Check
	                          : m_tone == Tone::Warn   ? Icons::Icon::Exclamation
	                          : m_tone == Tone::Danger ? Icons::Icon::Cross
	                                                   : Icons::Icon::Exclamation;
	const qreal inset = m_size * 0.12;
	Icons::paint(p, glyph, QRectF(inset, inset, m_size - 2 * inset, m_size - 2 * inset), QColor(0x14, 0x14, 0x14), m_size / 8.0);
}

// --- HeroBanner -------------------------------------------------------------------------------------------------------

HeroBanner::HeroBanner(QWidget* parent): QWidget(parent) {
	setMinimumHeight(200);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void HeroBanner::setArt(const QImage& art) {
	m_art    = art;
	m_scaled = {};
	update();
}

void HeroBanner::resizeEvent(QResizeEvent* event) {
	m_scaled = {};
	QWidget::resizeEvent(event);
}

void HeroBanner::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const QRectF r(rect());
	const qreal  dpr = devicePixelRatioF();
	if (!m_art.isNull()) {
		const QSize target = (QSizeF(size()) * dpr).toSize();
		if (m_scaled.isNull() || m_scaled.size() != target) {
			// Cover the banner; the crop a little above the middle (where key art keeps faces and titles).
			const QImage cover = m_art.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const int    x     = (cover.width() - target.width()) / 2;
			const int    y     = static_cast<int>((cover.height() - target.height()) * 0.3);
			m_scaled           = QPixmap::fromImage(cover.copy(x, y, target.width(), target.height()));
			m_scaled.setDevicePixelRatio(dpr);
		}
		p.drawPixmap(0, 0, m_scaled);
	} else {
		// No key art (a .zar game, or none chosen): a dark ember glow.
		QLinearGradient base(r.topLeft(), r.bottomLeft());
		base.setColorAt(0, QColor(0x1E, 0x1A, 0x15));
		base.setColorAt(1, Theme::kWindow);
		p.fillRect(r, base);
		QRadialGradient glow(QPointF(r.width() * 0.78, r.height() * 0.08), r.width() * 0.55);
		glow.setColorAt(0, QColor(0xC9, 0xA2, 0x5E, 46));
		glow.setColorAt(1, QColor(0xC9, 0xA2, 0x5E, 0));
		p.fillRect(r, glow);
	}
	// The art fades into the page behind the header and PLAY, and darkens on the left under the title.
	QLinearGradient down(r.topLeft(), r.bottomLeft());
	down.setColorAt(0, QColor(18, 18, 18, 25));
	down.setColorAt(0.35, QColor(18, 18, 18, 70));
	down.setColorAt(0.58, QColor(18, 18, 18, 170));
	down.setColorAt(0.8, QColor(18, 18, 18, 236));
	down.setColorAt(1, QColor(18, 18, 18, 255));
	p.fillRect(r, down);
	QLinearGradient left(r.topLeft(), QPointF(r.width() * 0.62, 0));
	left.setColorAt(0, QColor(18, 18, 18, 150));
	left.setColorAt(1, QColor(18, 18, 18, 0));
	p.fillRect(r, left);
}

// --- PlayButton -------------------------------------------------------------------------------------------------------

PlayButton::PlayButton(QWidget* parent): QAbstractButton(parent), m_spin(new QTimer(this)) {
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setFixedHeight(56);
	setMinimumWidth(220);
	setText(tr("PLAY"));
	setAccessibleName(tr("Play"));
	m_spin->setInterval(16);
	connect(m_spin, &QTimer::timeout, this, [this] {
		m_angle = std::fmod(m_angle + 6.0, 360.0);
		update();
	});
}

void PlayButton::setLook(Look look, const QString& text) {
	m_look = look;
	setText(text);
	setAccessibleName(text);
	if (look == Look::Busy) m_spin->start();
	else m_spin->stop();
	setCursor(look == Look::Play || look == Look::Running ? Qt::PointingHandCursor : Qt::ArrowCursor);
	update();
}

void PlayButton::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void PlayButton::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void PlayButton::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	QColor       fill, border, ink;
	switch (m_look) {
		case Look::Play:
			fill   = isDown() ? Theme::kAccentPressed : m_hover ? Theme::kAccentHover : Theme::kAccent;
			border = fill;
			ink    = QColor(0x12, 0x12, 0x12);
			break;
		case Look::Busy:
			fill   = QColor(0x22, 0x1E, 0x17);
			border = Theme::withAlpha(Theme::kAccent, 110);
			ink    = Theme::kAccentHover;
			break;
		case Look::Running:
			fill   = m_hover ? QColor(0x1B, 0x26, 0x1C) : QColor(0x17, 0x20, 0x18);
			border = Theme::withAlpha(Theme::kOk, 110);
			ink    = Theme::kText;
			break;
		case Look::Disabled:
			fill   = QColor(0x24, 0x24, 0x24);
			border = QColor(0x2E, 0x2E, 0x2E);
			ink    = QColor(0x66, 0x66, 0x66);
			break;
	}
	if (m_look == Look::Play) {
		// A soft glow under the gold.
		QLinearGradient sheen(r.topLeft(), r.bottomLeft());
		sheen.setColorAt(0, fill.lighter(108));
		sheen.setColorAt(1, fill.darker(104));
		p.setPen(QPen(border, 1));
		p.setBrush(sheen);
	} else {
		p.setPen(QPen(border, 1));
		p.setBrush(fill);
	}
	p.drawRoundedRect(r, Theme::kRadius, Theme::kRadius);
	if (Theme::keyboardFocus(this)) {
		p.setPen(QPen(m_look == Look::Play ? QColor(0x12, 0x12, 0x12, 140) : Theme::withAlpha(Theme::kAccentHover, 170), 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(r.adjusted(3, 3, -3, -3), 4, 4);
	}
	QFont font = Theme::font(m_look == Look::Play || m_look == Look::Disabled ? 14 : 12, QFont::DemiBold);
	if (m_look == Look::Play || m_look == Look::Disabled) font.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
	p.setFont(font);
	const QFontMetricsF metrics(font);
	const qreal         text_width = metrics.horizontalAdvance(text());
	const qreal         glyph      = 18;
	const qreal         gap        = 10;
	const qreal         total      = glyph + gap + text_width;
	const qreal         x          = (width() - total) / 2;
	const QRectF        glyph_rect(x, (height() - glyph) / 2, glyph, glyph);
	switch (m_look) {
		case Look::Play:
		case Look::Disabled: Icons::paint(p, Icons::Icon::Play, glyph_rect, ink, 1.5); break;
		case Look::Busy: {
			p.setPen(QPen(Theme::withAlpha(Theme::kAccent, 60), 2.2));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(glyph_rect.adjusted(2, 2, -2, -2));
			p.setPen(QPen(Theme::kAccentHover, 2.2, Qt::SolidLine, Qt::RoundCap));
			p.drawArc(glyph_rect.adjusted(2, 2, -2, -2), static_cast<int>(-m_angle * 16), 100 * 16);
			break;
		}
		case Look::Running:
			p.setPen(Qt::NoPen);
			p.setBrush(Theme::kOk);
			p.drawEllipse(QRectF(glyph_rect.center().x() - 5, glyph_rect.center().y() - 5, 10, 10));
			p.setBrush(Theme::withAlpha(Theme::kOk, 50));
			p.drawEllipse(glyph_rect.center(), 9, 9);
			break;
	}
	p.setPen(ink);
	p.drawText(QRectF(x + glyph + gap, 0, text_width + 4, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// --- StepBar ----------------------------------------------------------------------------------------------------------

StepBar::StepBar(const QStringList& steps, QWidget* parent): QWidget(parent), m_steps(steps) {
	setFixedHeight(36);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StepBar::setCurrent(int index) {
	m_current = index;
	update();
}

void StepBar::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const int   n        = static_cast<int>(m_steps.size());
	const qreal circle   = 26;
	const QFont number   = Theme::font(9.5, QFont::DemiBold);
	const QFont label    = Theme::font(10);
	const QFont current  = Theme::font(10, QFont::DemiBold);
	const qreal y        = height() / 2.0;
	// Each step: its circle and label; lines fill the space between them.
	QList<qreal> starts, ends;
	qreal        used = 0;
	for (int i = 0; i < n; i++) used += circle + 10 + QFontMetricsF(i == m_current ? current : label).horizontalAdvance(m_steps.at(i));
	const qreal gap = n > 1 ? (width() - used) / (n - 1) : 0;
	qreal       x   = 0;
	for (int i = 0; i < n; i++) {
		const bool  done = i < m_current;
		const bool  now  = i == m_current;
		const QRectF c(x, y - circle / 2, circle, circle);
		if (done) {
			p.setPen(Qt::NoPen);
			p.setBrush(Theme::kAccent);
			p.drawEllipse(c);
			Icons::paint(p, Icons::Icon::Check, c.adjusted(5, 5, -5, -5), QColor(0x14, 0x14, 0x14), 2.2);
		} else {
			p.setPen(QPen(now ? Theme::kAccent : QColor(0x3C, 0x3C, 0x3C), now ? 2 : 1.5));
			p.setBrush(now ? Theme::withAlpha(Theme::kAccent, 30) : Qt::NoBrush);
			p.drawEllipse(c.adjusted(1, 1, -1, -1));
			p.setFont(number);
			p.setPen(now ? Theme::kAccentHover : Theme::kSecondary);
			p.drawText(c, Qt::AlignCenter, QString::number(i + 1));
		}
		const QFont& f = now ? current : label;
		p.setFont(f);
		p.setPen(now ? Theme::kText : done ? QColor(0xC8, 0xC8, 0xC8) : Theme::kSecondary);
		const qreal text_width = QFontMetricsF(f).horizontalAdvance(m_steps.at(i));
		p.drawText(QRectF(x + circle + 10, 0, text_width + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, m_steps.at(i));
		const qreal end = x + circle + 10 + text_width;
		if (i + 1 < n) {
			p.setPen(QPen(done ? Theme::withAlpha(Theme::kAccent, 150) : QColor(0x30, 0x30, 0x30), 1.5));
			p.drawLine(QPointF(end + 16, y), QPointF(end + gap - 16, y));
		}
		x = end + gap;
	}
}

// --- ChoiceCard -------------------------------------------------------------------------------------------------------

ChoiceCard::ChoiceCard(const QString& title, const QString& line, const QString& tag, QWidget* parent)
    : QAbstractButton(parent), m_line(line), m_tag(tag) {
	setText(title);
	setCheckable(true);
	setAutoExclusive(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(title);
	setAccessibleDescription(line);
}

QSize ChoiceCard::sizeHint() const {
	return {420, 66};
}

void ChoiceCard::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void ChoiceCard::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void ChoiceCard::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r       = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const bool   checked = isChecked();
	p.setPen(QPen(checked ? Theme::kAccent : m_hover ? QColor(0x46, 0x46, 0x46) : Theme::kBorder, checked ? 1.5 : 1));
	p.setBrush(checked ? QColor(0x22, 0x1F, 0x19) : m_hover ? QColor(0x20, 0x20, 0x20) : Theme::kPanel);
	p.drawRoundedRect(r, Theme::kRadius, Theme::kRadius);
	const QRectF radio(18, height() / 2.0 - 9, 18, 18);
	p.setPen(QPen(checked ? Theme::kAccent : QColor(0x55, 0x55, 0x55), 1.6));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(radio.adjusted(1, 1, -1, -1));
	if (checked) {
		p.setPen(Qt::NoPen);
		p.setBrush(Theme::kAccent);
		p.drawEllipse(radio.adjusted(5, 5, -5, -5));
	}
	const QFont title = Theme::font(10.5, QFont::DemiBold);
	const QFont line  = Theme::font(9.5);
	p.setFont(title);
	p.setPen(Theme::kText);
	const qreal x     = 52;
	const qreal title_width = QFontMetricsF(title).horizontalAdvance(text());
	p.drawText(QRectF(x, 12, title_width + 2, 22), Qt::AlignVCenter | Qt::AlignLeft, text());
	if (!m_tag.isEmpty()) {
		const QSizeF size = tagSize(m_tag);
		paintTag(p, QRectF(QPointF(x + title_width + 10, 23 - size.height() / 2), size), m_tag, Tone::Accent, false);
	}
	p.setFont(line);
	p.setPen(Theme::kSecondary);
	p.drawText(QRectF(x, 34, width() - x - 16, 20), Qt::AlignVCenter | Qt::AlignLeft, m_line);
	if (Theme::keyboardFocus(this)) {
		p.setPen(QPen(Theme::withAlpha(Theme::kAccentHover, 170), 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 4, 4);
	}
}

// --- IconButton -------------------------------------------------------------------------------------------------------

IconButton::IconButton(Icons::Icon icon, int size, QWidget* parent): QAbstractButton(parent), m_icon(icon), m_size(size) {
	setFixedSize(size, size);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
}

void IconButton::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void IconButton::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void IconButton::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	if (m_hover || Theme::keyboardFocus(this)) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 18));
		p.drawRoundedRect(QRectF(rect()), 4, 4);
	}
	const qreal inset = m_size * 0.22;
	Icons::paint(p, m_icon, QRectF(inset, inset, m_size - 2 * inset, m_size - 2 * inset), m_hover ? Theme::kText : Theme::kSecondary, 1.6);
}
