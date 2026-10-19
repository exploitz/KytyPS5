@echo off
rem The release build of this fork: build-windows.cmd in its own directory (_Build\windows-release) with what a
rem release needs to differ in:
rem   -march=x86-64-v3   any CPU with AVX2 (Intel Core 4th generation, AMD Ryzen), not only the building PC's
rem                      (the default, native, makes code that stops other CPUs with an illegal instruction)
rem   no DLSS SDK        NVIDIA's NGX library cannot be part of a GPL program: DLSS is a separate add-on
rem   no Streamline SDK  DLSS frame generation, NVIDIA's plugins likewise
rem   KYTY_RELEASE_PATHS no path of the building PC in the programs (source paths relative, the PDB by its name)
rem The PGO profile is the repository's (tools\pgo\kyty.profdata), as build-windows.cmd picks it. Then
rem tools\release\package-release.ps1 makes the zip.
rem   KYTY_RELEASE_CMAKE_ARGS   more arguments for the first configure (e.g. local FetchContent sources)
setlocal
set "ROOT=%~dp0..\..\"
set "KYTY_BUILD_DIR=%ROOT%_Build\windows-release"
set "KYTY_CMAKE_ARGS=-DKYTY_MARCH=x86-64-v3 -DKYTY_DLSS_SDK=%ROOT%_Build\no-dlss -DKYTY_STREAMLINE_SDK=%ROOT%_Build\no-streamline -DKYTY_BUILD_LAUNCHER=OFF -DKYTY_RELEASE_PATHS=ON %KYTY_RELEASE_CMAKE_ARGS%"
call "%ROOT%build-windows.cmd" kyty_emulator || exit /b 1
call "%ROOT%build-windows.cmd" kyty_shader_precompile || exit /b 1
echo release build: %KYTY_BUILD_DIR%
