#pragma once

#include <QString>

// The launcher's version (git describe at configure time, or the release tag; CMakeLists.txt).
namespace Version {

inline QString text() {
	return QStringLiteral(KYTY_LAUNCHER_VERSION);
}
inline QString releaseTag() {
	return QStringLiteral(KYTY_LAUNCHER_RELEASE_TAG);
}
inline QString commitDate() {
	return QStringLiteral(KYTY_LAUNCHER_COMMIT_DATE);
}

inline constexpr char kRepository[]   = "exploitz/KytyPS5";
inline constexpr char kReleasesUrl[]  = "https://github.com/exploitz/KytyPS5/releases";
inline constexpr char kIssuesUrl[]    = "https://github.com/exploitz/KytyPS5/issues/new";
// The releases, newest first, pre-releases too (/releases/latest leaves those out, and the betas are pre-releases).
inline constexpr char kReleasesApiUrl[] = "https://api.github.com/repos/exploitz/KytyPS5/releases?per_page=30";
inline constexpr char kDlssAddonUrl[] = "https://github.com/exploitz/KytyPS5/releases/latest/download/KytyPS5-DLSS-addon.zip";
inline constexpr char kReadmeUrl[]    = "https://github.com/exploitz/KytyPS5#readme";

} // namespace Version
