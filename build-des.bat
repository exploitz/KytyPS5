@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=C:\VulkanSDK\1.4.357.0\Bin;%PATH%"
cd /d C:\Users\Chase\dev\KytyPS5-des
cmake --build _Build\windows-des --target kyty_emulator --parallel 16
echo EXIT=%ERRORLEVEL%
