#include "ui/Theme.h"

#include <QApplication>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace Theme {

namespace {

QString UiFamily() {
#if defined(Q_OS_WIN)
	return QStringLiteral("Segoe UI");
#else
	return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#endif
}

// Remembers how each widget got the focus (the property keyboardFocus): by Tab or a shortcut, or by a click.
class FocusTracker final: public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::FocusIn && watched->isWidgetType()) {
			const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
			watched->setProperty("keyboardFocus", reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason);
		}
		return QObject::eventFilter(watched, event);
	}
};

} // namespace

bool keyboardFocus(const QWidget* widget) {
	return widget->hasFocus() && widget->property("keyboardFocus").toBool();
}

QFont font(qreal pointSize, QFont::Weight weight) {
	QFont f(UiFamily());
	f.setPointSizeF(pointSize);
	f.setWeight(weight);
	f.setHintingPreference(QFont::PreferNoHinting);
	return f;
}

QFont monoFont(qreal pointSize) {
	QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#if defined(Q_OS_WIN)
	f.setFamily(QStringLiteral("Consolas"));
#endif
	f.setPointSizeF(pointSize);
	return f;
}

QColor withAlpha(QColor color, int alpha) {
	color.setAlpha(alpha);
	return color;
}

void setRole(QWidget* widget, const char* name, const QString& value) {
	if (widget->property(name).toString() == value) return;
	widget->setProperty(name, value);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

void apply(QApplication& application) {
	// Fusion under the stylesheet: the same look on every platform, nothing taken from a native light theme.
	application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	application.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
	QPalette palette;
	palette.setColor(QPalette::Window, kWindow);
	palette.setColor(QPalette::WindowText, kText);
	palette.setColor(QPalette::Base, QColor(0x14, 0x14, 0x14));
	palette.setColor(QPalette::AlternateBase, kPanel);
	palette.setColor(QPalette::Text, kText);
	palette.setColor(QPalette::Button, kHover);
	palette.setColor(QPalette::ButtonText, kText);
	palette.setColor(QPalette::BrightText, Qt::white);
	palette.setColor(QPalette::Highlight, QColor(0x6B, 0x58, 0x38));
	palette.setColor(QPalette::HighlightedText, kText);
	palette.setColor(QPalette::ToolTipBase, kHover);
	palette.setColor(QPalette::ToolTipText, kText);
	palette.setColor(QPalette::PlaceholderText, QColor(0x6A, 0x6A, 0x6A));
	palette.setColor(QPalette::Link, kAccent);
	palette.setColor(QPalette::Light, QColor(0x3A, 0x3A, 0x3A));
	palette.setColor(QPalette::Midlight, QColor(0x30, 0x30, 0x30));
	palette.setColor(QPalette::Mid, kBorder);
	palette.setColor(QPalette::Dark, QColor(0x0E, 0x0E, 0x0E));
	palette.setColor(QPalette::Shadow, Qt::black);
	for (const auto role: {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) palette.setColor(QPalette::Disabled, role, kMuted);
	application.setPalette(palette);
	application.setFont(font(10));
	application.setStyleSheet(styleSheet());
	application.installEventFilter(new FocusTracker(&application));
}

QString styleSheet() {
	return QStringLiteral(R"QSS(
QWidget { color: #EDEDED; }
QWidget#Root, QWidget#Page, QStackedWidget#Pages { background: #121212; }
QToolTip { background: #262626; color: #EDEDED; border: 1px solid #3A3A3A; padding: 5px 8px; }
QLabel { background: transparent; }
QLabel[role="secondary"] { color: #9A9A9A; }
QLabel[role="muted"] { color: #6E6E6E; }
QLabel[role="title"] { font-size: 17pt; font-weight: 600; }
QLabel[role="heading"] { font-size: 11pt; font-weight: 600; }
QLabel[role="ok"] { color: #5CB85C; }
QLabel[role="warn"] { color: #E0A030; }
QLabel[role="danger"] { color: #D9534F; }
QLabel[role="accent"] { color: #C9A25E; }
QFrame[card="true"] { background: #1C1C1C; border: 1px solid #2E2E2E; border-radius: 6px; }
QFrame[card="danger"] { background: #211919; border: 1px solid #4A2A28; border-radius: 6px; }
QFrame[card="warn"] { background: #211D15; border: 1px solid #4A3B22; border-radius: 6px; }
QFrame[separator="true"] { background: #262626; border: none; min-height: 1px; max-height: 1px; }

QPushButton { background: #262626; color: #EDEDED; border: 1px solid #343434; border-radius: 6px; padding: 7px 16px; min-height: 18px; }
QPushButton:hover { background: #2E2E2E; border-color: #404040; }
QPushButton:pressed { background: #202020; }
QPushButton:focus { border-color: #C9A25E; }
QPushButton:disabled { color: #5C5C5C; background: #1A1A1A; border-color: #262626; }
QPushButton[kind="primary"] { background: #C9A25E; color: #121212; border: 1px solid #C9A25E; font-weight: 600; }
QPushButton[kind="primary"]:hover { background: #DDB774; border-color: #DDB774; }
QPushButton[kind="primary"]:pressed { background: #A8853F; border-color: #A8853F; }
QPushButton[kind="primary"]:focus { border-color: #F2DDAE; }
QPushButton[kind="primary"]:disabled { background: #3A3427; color: #7A7262; border-color: #3A3427; }
QPushButton[kind="danger"] { background: #3A1F1E; color: #F2C4C2; border: 1px solid #5A2C2A; }
QPushButton[kind="danger"]:hover { background: #4A2523; }
QPushButton[kind="text"] { background: transparent; border: 1px solid transparent; color: #9A9A9A; padding: 4px 8px; }
QPushButton[kind="text"]:hover { color: #EDEDED; background: #1C1C1C; }
QPushButton[kind="text"]:pressed { color: #C9A25E; }
QPushButton[kind="text"]:focus { color: #DDB774; border-color: transparent; }
QPushButton[kind="text"]:disabled { color: #4A4A4A; background: transparent; }
QPushButton[kind="link"] { background: transparent; border: none; color: #C9A25E; padding: 0px; font-weight: 600; }
QPushButton[kind="link"]:hover { color: #DDB774; }

QComboBox { background: #141414; color: #EDEDED; border: 1px solid #343434; border-radius: 6px; padding: 5px 34px 5px 11px; min-height: 22px; combobox-popup: 0; }
QComboBox:hover { border-color: #4A4A4A; background: #171717; }
QComboBox:focus { border-color: #C9A25E; }
QComboBox:on { border-color: #C9A25E; }
QComboBox:disabled { color: #555555; border-color: #262626; background: #161616; }
QComboBox::drop-down { width: 30px; border: none; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; border: none; }
QComboBoxPrivateContainer { background: #1C1C1C; border: 1px solid #383838; }
QComboBox QAbstractItemView { background: #1C1C1C; color: #EDEDED; border: 1px solid #383838; padding: 4px; outline: 0; selection-background-color: #2A2A2A; selection-color: #EDEDED; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0px 10px; border-radius: 4px; }
QComboBox QAbstractItemView::item:selected { background: #2C2C2C; color: #FFFFFF; }

QLineEdit { background: #141414; color: #EDEDED; border: 1px solid #343434; border-radius: 6px; padding: 7px 10px; selection-background-color: #6B5838; }
QLineEdit:focus { border-color: #C9A25E; }
QLineEdit:read-only { color: #CFCFCF; }

QPlainTextEdit { background: #151515; color: #BDBDBD; border: 1px solid #2A2A2A; border-radius: 6px; padding: 6px; selection-background-color: #6B5838; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: #2E2E2E; border-radius: 4px; min-height: 32px; margin: 0px 2px; }
QScrollBar::handle:vertical:hover { background: #3E3E3E; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2E2E2E; border-radius: 4px; min-width: 32px; margin: 2px 0px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

QDialog { background: #181818; }
)QSS");
}

} // namespace Theme
