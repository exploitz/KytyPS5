#pragma once

#include <QString>

struct Package;

// --screenshots DIR: the setup's four pages, Home (idle, precompiling, after a failed run), Settings with a row under
// the mouse, Help and a message dialog, rendered off screen (Qt::WA_DontShowOnScreen + QWidget::grab) into DIR\*.png.
// Nothing is shown, started or saved. At a device pixel ratio other than 1 (QT_SCALE_FACTOR=1.5) the files are named
// <name>@1.5x.png.
namespace Screenshots {

int run(const Package& package, const QString& folder);

} // namespace Screenshots
