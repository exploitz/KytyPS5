#pragma once

#include "ui/Icons.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVariantAnimation>
#include <QWidget>

class QTimer;

// The launcher's own widgets: painted where a stylesheet cannot draw them well.

enum class Tone { Neutral, Accent, Ok, Warn, Danger };
QColor toneColor(Tone tone);

QLabel*      makeLabel(const QString& text, const char* role = nullptr, QWidget* parent = nullptr);
QPushButton* makeButton(const QString& text, const char* kind = nullptr, QWidget* parent = nullptr);
QFrame*      makeCard(QWidget* parent = nullptr, const char* kind = "true");
QFrame*      makeSeparator(QWidget* parent = nullptr);

// An On/Off switch.
class ToggleSwitch final: public QAbstractButton {
	Q_OBJECT
public:
	explicit ToggleSwitch(QWidget* parent = nullptr);
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void checkStateSet() override;
	void nextCheckState() override;

private:
	QVariantAnimation m_animation;
	qreal             m_position = 0; // 0 off .. 1 on
	bool              m_hover    = false;
};

// A combo box with a painted chevron and stylesheet-able items; `highlighted` tells which item the mouse is over.
class DarkCombo final: public QComboBox {
	Q_OBJECT
public:
	explicit DarkCombo(QWidget* parent = nullptr);

protected:
	void paintEvent(QPaintEvent* event) override;
	void showPopup() override;
};

// A page button of the navigation rail: an icon over a label.
class NavButton final: public QAbstractButton {
	Q_OBJECT
public:
	NavButton(Icons::Icon icon, const QString& text, QWidget* parent = nullptr);
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Icons::Icon m_icon;
	bool        m_hover = false;
};

// A status chip: a colored dot and a text, optionally an action ("Precompile") that makes it clickable.
class Chip final: public QAbstractButton {
	Q_OBJECT
public:
	explicit Chip(QWidget* parent = nullptr);
	void setState(Tone tone, const QString& text, const QString& action = {});
	[[nodiscard]] bool hasAction() const { return !m_action.isEmpty(); }
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Tone    m_tone = Tone::Neutral;
	QString m_action;
	bool    m_hover = false;
};

// A small tag such as "Recommended"; clickable, it reads "Recommended: 1080p Quality" and applies that. One look for
// every tag (paintTag: the choice cards' too).
class Tag final: public QAbstractButton {
	Q_OBJECT
public:
	explicit Tag(const QString& text, Tone tone = Tone::Accent, QWidget* parent = nullptr);
	void                setClickable(bool clickable);
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Tone m_tone;
	bool m_clickable = false;
	bool m_hover     = false;
};
QFont  tagFont();
QSizeF tagSize(const QString& text);
void   paintTag(QPainter& painter, const QRectF& rect, const QString& text, Tone tone, bool hover);

// A thin activity bar (under the Play button): hidden, sweeping (busy) or full.
class ThinBar final: public QWidget {
	Q_OBJECT
public:
	enum class Mode { Hidden, Busy, Full };
	explicit ThinBar(QWidget* parent = nullptr);
	void setMode(Mode mode, Tone tone = Tone::Accent);
	[[nodiscard]] QSize sizeHint() const override { return {200, 3}; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	Mode    m_mode = Mode::Hidden;
	Tone    m_tone = Tone::Accent;
	QTimer* m_timer;
	qreal   m_phase = 0;
};

// A rounded progress bar.
class ProgressBar final: public QWidget {
	Q_OBJECT
public:
	explicit ProgressBar(QWidget* parent = nullptr);
	void setValue(double fraction);
	[[nodiscard]] QSize sizeHint() const override { return {200, 6}; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	double  m_value = 0;
	QTimer* m_timer;
	qreal   m_shine = 0;
};

// ok (check), warn (!) or bad (x) in a filled circle.
class StateIcon final: public QWidget {
	Q_OBJECT
public:
	explicit StateIcon(Tone tone = Tone::Ok, int size = 18, QWidget* parent = nullptr);
	void setTone(Tone tone);
	[[nodiscard]] QSize sizeHint() const override { return {m_size, m_size}; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	Tone m_tone;
	int  m_size;
};

// The game's key art across the top of Home, fading into the page toward its bottom, where the game's header and
// PLAY sit on it (children of the banner).
class HeroBanner final: public QWidget {
	Q_OBJECT
public:
	explicit HeroBanner(QWidget* parent = nullptr);
	void setArt(const QImage& art);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	QImage  m_art;
	QPixmap m_scaled; // the art at this size and pixel ratio
};

// The big PLAY button; while a launch runs it shows its stage ("Starting…", "Loading", "Running").
class PlayButton final: public QAbstractButton {
	Q_OBJECT
public:
	enum class Look { Play, Busy, Running, Disabled };
	explicit PlayButton(QWidget* parent = nullptr);
	void setLook(Look look, const QString& text);
	[[nodiscard]] Look look() const { return m_look; }
	[[nodiscard]] QSize sizeHint() const override { return {240, 56}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Look    m_look = Look::Play;
	bool    m_hover = false;
	QTimer* m_spin;
	qreal   m_angle = 0;
};

// The setup's steps: numbered circles joined by lines.
class StepBar final: public QWidget {
	Q_OBJECT
public:
	explicit StepBar(const QStringList& steps, QWidget* parent = nullptr);
	void setCurrent(int index);
	[[nodiscard]] QSize sizeHint() const override { return {640, 36}; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QStringList m_steps;
	int         m_current = 0;
};

// A selectable card with a radio mark, a title, a line under it and an optional tag.
class ChoiceCard final: public QAbstractButton {
	Q_OBJECT
public:
	ChoiceCard(const QString& title, const QString& line, const QString& tag = {}, QWidget* parent = nullptr);
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QString m_line, m_tag;
	bool    m_hover = false;
};

// An icon-only flat button (the error card's close).
class IconButton final: public QAbstractButton {
	Q_OBJECT
public:
	IconButton(Icons::Icon icon, int size, QWidget* parent = nullptr);
	[[nodiscard]] QSize sizeHint() const override { return {m_size, m_size}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Icons::Icon m_icon;
	int         m_size;
	bool        m_hover = false;
};
