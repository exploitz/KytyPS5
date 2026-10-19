@echo off
rem build-target.bat <target>...: targets in an existing build directory (KYTY_BUILD_DIR, default _Build\windows-pgouse).
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=C:\VulkanSDK\1.4.357.0\Bin;%PATH%"
cd /d C:\Users\Chase\dev\KytyPS5-des
set "BUILD=%KYTY_BUILD_DIR%"
if "%BUILD%"=="" set "BUILD=_Build\windows-pgouse"
cmake --build "%BUILD%" --target %* --parallel 16
echo EXIT=%ERRORLEVEL%
