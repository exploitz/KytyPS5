@echo off
rem The fork's player launcher (src\fork-launcher, Qt 6 Widgets): configures and builds it (Release, Ninja, the Visual
rem Studio x64 environment), then deploys the Qt files it needs into _Build\fork-launcher\deploy (windeployqt, a
rem minimal set: Qt Core, Gui, Widgets, Network, the Windows platform plugin and the Schannel TLS backend), ready to
rem be copied next to kyty_emulator.exe in the release package.
rem   KYTY_QT_DIR        Qt 6.8 for MSVC x64 (default C:\Qt\6.8.3\msvc2022_64)
rem   KYTY_LAUNCHER_TAG  the GitHub release tag of this build (the update check compares it); empty: a development build
setlocal
for %%i in ("%~dp0..\..") do set "ROOT=%%~fi"
set "SRC=%ROOT%\src\fork-launcher"
set "BUILD=%ROOT%\_Build\fork-launcher"
set "DEPLOY=%BUILD%\deploy"
if not defined KYTY_QT_DIR set "KYTY_QT_DIR=C:\Qt\6.8.3\msvc2022_64"
if not exist "%KYTY_QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
	echo Qt not found in "%KYTY_QT_DIR%": install Qt 6.8 for MSVC 2022 x64, for example
	echo   aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
	echo or set KYTY_QT_DIR to its folder.
	exit /b 1
)

rem Visual Studio 2022 or newer, or its Build Tools, with the "Desktop development with C++" workload.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
	echo Visual Studio with the "Desktop development with C++" workload not found.
	exit /b 1
)
rem (vcvars64.bat runs vswhere by name.)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
where cmake >nul 2>nul || (
	echo cmake not found: add the VS component "C++ CMake tools for Windows" or winget install Kitware.CMake.
	exit /b 1
)
where ninja >nul 2>nul || (
	echo ninja not found: add the VS component "C++ CMake tools for Windows" or winget install Ninja-build.Ninja.
	exit /b 1
)

cmake -S "%SRC%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%KYTY_QT_DIR%" "-DKYTY_LAUNCHER_RELEASE_TAG=%KYTY_LAUNCHER_TAG%" || exit /b 1
cmake --build "%BUILD%" || exit /b 1

rem The deployed set: the exe and the Qt files windeployqt finds it needs, without translations, image format,
rem icon engine, style and network information plugins, and only Windows' own TLS backend (Schannel).
if exist "%DEPLOY%" rmdir /s /q "%DEPLOY%"
mkdir "%DEPLOY%" || exit /b 1
copy /y "%BUILD%\KytyPS5 Launcher.exe" "%DEPLOY%\" >nul || exit /b 1
"%KYTY_QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler ^
	--no-opengl-sw --no-compiler-runtime --no-ffmpeg --skip-plugin-types imageformats,iconengines,styles,generic,networkinformation ^
	--exclude-plugins qcertonlybackend,qopensslbackend "%DEPLOY%\KytyPS5 Launcher.exe" || exit /b 1
rem (Qt's software OpenGL and the Direct2D platform plugin are not used.)
if exist "%DEPLOY%\opengl32sw.dll" del /q "%DEPLOY%\opengl32sw.dll"
if exist "%DEPLOY%\platforms\qdirect2d.dll" del /q "%DEPLOY%\platforms\qdirect2d.dll"
echo.
echo launcher: %DEPLOY%
dir /b /s "%DEPLOY%"
