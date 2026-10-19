#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;
class QWidget;

// The launcher's look: a dark palette with an ember gold accent, a QSS stylesheet for the stock widgets and fonts for
// the painted ones. 8 px spacing grid, 6 px corners.
namespace Theme {

inline const QColor kWindow {0x12, 0x12, 0x12};
inline const QColor kPanel {0x1C, 0x1C, 0x1C};
inline const QColor kHover {0x26, 0x26, 0x26};
inline const QColor kBorder {0x2E, 0x2E, 0x2E};
inline const QColor kText {0xED, 0xED, 0xED};
inline const QColor kSecondary {0x9A, 0x9A, 0x9A};
inline const QColor kMuted {0x5C, 0x5C, 0x5C};
inline const QColor kAccent {0xC9, 0xA2, 0x5E};
inline const QColor kAccentHover {0xDD, 0xB7, 0x74};
inline const QColor kAccentPressed {0xA8, 0x85, 0x3F};
inline const QColor kOk {0x5C, 0xB8, 0x5C};
inline const QColor kWarn {0xE0, 0xA0, 0x30};
inline const QColor kDanger {0xD9, 0x53, 0x4F};

constexpr int kRadius = 6;
constexpr int kGrid   = 8;

// Segoe UI on Windows, the system's UI font elsewhere.
QFont font(qreal pointSize, QFont::Weight weight = QFont::Normal);
QFont monoFont(qreal pointSize);
QColor withAlpha(QColor color, int alpha);

void    apply(QApplication& application);
QString styleSheet();

// Whether the widget has the focus from the keyboard (Tab): the painted widgets draw their focus ring only then.
bool keyboardFocus(const QWidget* widget);

// A dynamic property (for the stylesheet's [role=...] selectors) with a re-polish.
void setRole(QWidget* widget, const char* name, const QString& value);

} // namespace Theme
