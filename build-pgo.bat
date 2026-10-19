@echo off
rem build-pgo.bat gen|use [profile]: the des build with profile-guided optimization, in its own build
rem directory (configured like _Build\windows-des). gen: the instrumented emulator (_Build\windows-pgogen;
rem the live command `pgo <file>` writes its profile). use: the release build optimized with the merged
rem profile (_Build\windows-pgouse; default profile _Build\pgo\des\kyty.profdata). The use build also has
rem line tables (-gline-tables-only: the PDB names static and inlined functions for the sampling profiler;
rem debug info does not change the generated code).
rem   KYTY_BUILD_SUFFIX  appended to the build directory (an experiment's own build, e.g. -mi)
rem   KYTY_CMAKE_ARGS    extra arguments for its first configure (e.g. -DKYTY_MIMALLOC=ON)
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=C:\VulkanSDK\1.4.357.0\Bin;%PATH%"
set "LLVM=C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
cd /d C:\Users\Chase\dev\KytyPS5-des
set "MODE=%~1"
set "PROFILE=%~2"
if "%PROFILE%"=="" set "PROFILE=C:/Users/Chase/dev/KytyPS5-des/_Build/pgo/des/kyty.profdata"
if /i "%MODE%"=="gen" (
	set "BUILD=_Build\windows-pgogen%KYTY_BUILD_SUFFIX%"
	set "PGO=-DKYTY_PGO_GENERATE=ON"
) else if /i "%MODE%"=="use" (
	set "BUILD=_Build\windows-pgouse%KYTY_BUILD_SUFFIX%"
	set "PGO=-DKYTY_PGO_USE=%PROFILE% -DCMAKE_CXX_FLAGS_INIT=-gline-tables-only -DCMAKE_C_FLAGS_INIT=-gline-tables-only"
) else (
	echo usage: build-pgo.bat gen^|use [profile]
	exit /b 1
)
if not exist "%BUILD%\build.ninja" (
	cmake -S . -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
		"-DCMAKE_C_COMPILER=%LLVM%/clang-cl.exe" "-DCMAKE_CXX_COMPILER=%LLVM%/clang-cl.exe" ^
		"-DCMAKE_LINKER=%LLVM%/lld-link.exe" "-DCMAKE_AR=%LLVM%/llvm-lib.exe" "-DCMAKE_MT=%LLVM%/llvm-mt.exe" ^
		-DKYTY_BUILD_LAUNCHER=OFF ^
		-DFETCHCONTENT_SOURCE_DIR_XBYAK=C:/Users/Chase/DEV/KytyPS5-des/_Build/deps/xbyak-src ^
		-DFETCHCONTENT_SOURCE_DIR_ZYDIS=C:/Users/Chase/DEV/KytyPS5-des/_Build/deps/zydis-src ^
		-DFETCHCONTENT_SOURCE_DIR_MIMALLOC=C:/Users/Chase/DEV/KytyPS5-des/_Build/deps/mimalloc-src ^
		-DKYTY_STREAMLINE_SDK=C:/Users/Chase/DEV/KytyPS5-des/_Build/deps/streamline/sdk ^
		%PGO% %KYTY_CMAKE_ARGS% || exit /b 1
)
rem An existing build directory takes the profile given (a new file name: Ninja does not see a profile's
rem content change under the same name) and mimalloc from the local copy (directories configured before 10-06).
cmake -B "%BUILD%" %PGO% -DFETCHCONTENT_SOURCE_DIR_MIMALLOC=C:/Users/Chase/DEV/KytyPS5-des/_Build/deps/mimalloc-src >nul || exit /b 1
cmake --build "%BUILD%" --target kyty_emulator --parallel 16
echo EXIT=%ERRORLEVEL%
