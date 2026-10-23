#pragma once

#include <QString>

struct Package;

// --dry-run: the play launch's environment and command line (the lines run-windows.ps1 -DryRun prints, for a line by
// line comparison), the steps a launch takes after them and the precompile's first commands, for the saved settings.
// Nothing is started; only files are read.
namespace DryRun {

QString text(const Package& package);

} // namespace DryRun
