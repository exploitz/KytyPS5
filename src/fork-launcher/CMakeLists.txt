# The player's launcher of this Demon's Souls fork: a Qt 6 Widgets front end to kyty_emulator and
# kyty_shader_precompile in the same folder. It ports run-windows.ps1 (a play launch) and
# precompile-windows.ps1 (the static shader precompile) to C++ (src/core/LaunchPlan.*, src/core/Precompile.*).
#
# A standalone project:
#   cmake -S src/fork-launcher -B _Build/fork-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
# (tools/release/build-launcher.cmd does that and deploys the Qt DLLs), or part of the main build with
# -DKYTY_BUILD_FORK_LAUNCHER=ON.
cmake_minimum_required(VERSION 3.21)
project(KytyPS5Launcher VERSION 1.0.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

# The version shown in Help and --version: git describe of this checkout, else the project version. A release
# build sets KYTY_LAUNCHER_RELEASE_TAG to its GitHub release tag (the update check compares it with the latest).
set(KYTY_LAUNCHER_RELEASE_TAG "" CACHE STRING "GitHub release tag of this build (empty: a development build)")
set(kyty_launcher_version "${PROJECT_VERSION}")
set(kyty_launcher_commit_date "")
find_package(Git QUIET)
if(GIT_FOUND)
	execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE kyty_describe
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE kyty_describe_result)
	if(kyty_describe_result EQUAL 0 AND kyty_describe)
		set(kyty_launcher_version "${kyty_describe}")
	endif()
	execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%cI
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE kyty_commit_date
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE kyty_date_result)
	if(kyty_date_result EQUAL 0)
		set(kyty_launcher_commit_date "${kyty_commit_date}")
	endif()
endif()
if(KYTY_LAUNCHER_RELEASE_TAG)
	set(kyty_launcher_version "${KYTY_LAUNCHER_RELEASE_TAG}")
endif()
message(STATUS "KytyPS5 Launcher version: ${kyty_launcher_version}")

set(kyty_launcher_sources
	src/main.cpp
	src/core/Caches.cpp src/core/Caches.h
	src/core/DlssAddon.cpp src/core/DlssAddon.h
	src/core/DryRun.cpp src/core/DryRun.h
	src/core/Features.h
	src/core/GameInfo.cpp src/core/GameInfo.h
	src/core/LaunchConfig.cpp src/core/LaunchConfig.h
	src/core/LaunchPlan.cpp src/core/LaunchPlan.h
	src/core/OrderedEnvironment.h
	src/core/Package.cpp src/core/Package.h
	src/core/PlayRunner.cpp src/core/PlayRunner.h
	src/core/Precompile.cpp src/core/Precompile.h
	src/core/ProblemReport.cpp src/core/ProblemReport.h
	src/core/Settings.cpp src/core/Settings.h
	src/core/ShaderStatus.cpp src/core/ShaderStatus.h
	src/core/SystemCheck.cpp src/core/SystemCheck.h
	src/core/UpdateChecker.cpp src/core/UpdateChecker.h
	src/core/Version.h
	src/platform/Platform.cpp src/platform/Platform.h
	src/ui/Choices.cpp src/ui/Choices.h
	src/ui/Dialogs.cpp src/ui/Dialogs.h
	src/ui/HelpPage.cpp src/ui/HelpPage.h
	src/ui/HomePage.cpp src/ui/HomePage.h
	src/ui/Icons.cpp src/ui/Icons.h
	src/ui/MainWindow.cpp src/ui/MainWindow.h
	src/ui/Screenshots.cpp src/ui/Screenshots.h
	src/ui/SettingsPage.cpp src/ui/SettingsPage.h
	src/ui/SetupWizard.cpp src/ui/SetupWizard.h
	src/ui/Theme.cpp src/ui/Theme.h
	src/ui/Widgets.cpp src/ui/Widgets.h
)
if(WIN32)
	configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
	list(APPEND kyty_launcher_sources "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

qt_add_executable(kyty_fork_launcher WIN32 ${kyty_launcher_sources})
set_target_properties(kyty_fork_launcher PROPERTIES AUTOMOC ON)
target_compile_features(kyty_fork_launcher PRIVATE cxx_std_20)
target_include_directories(kyty_fork_launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(kyty_fork_launcher PRIVATE
	KYTY_LAUNCHER_VERSION="${kyty_launcher_version}"
	KYTY_LAUNCHER_COMMIT_DATE="${kyty_launcher_commit_date}"
	KYTY_LAUNCHER_RELEASE_TAG="${KYTY_LAUNCHER_RELEASE_TAG}")
target_link_libraries(kyty_fork_launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
if(WIN32)
	target_link_libraries(kyty_fork_launcher PRIVATE dxgi dwmapi advapi32 user32 shell32)
	target_compile_definitions(kyty_fork_launcher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
	set_target_properties(kyty_fork_launcher PROPERTIES OUTPUT_NAME "KytyPS5 Launcher")
else()
	set_target_properties(kyty_fork_launcher PROPERTIES OUTPUT_NAME "kytyps5-launcher")
endif()
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_options(kyty_fork_launcher PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
elseif(MSVC)
	target_compile_options(kyty_fork_launcher PRIVATE /W4 /utf-8)
else()
	target_compile_options(kyty_fork_launcher PRIVATE -Wall -Wextra)
endif()

install(TARGETS kyty_fork_launcher RUNTIME DESTINATION .)
