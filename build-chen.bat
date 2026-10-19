@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=C:\VulkanSDK\1.4.357.0\Bin;%PATH%"
cd /d C:\Users\Chase\dev\KytyPS5-chen
if not exist _Build\windows\build.ninja cmake -S . -B _Build\windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON "-DCMAKE_CXX_FLAGS=/arch:AVX2" "-DCMAKE_C_FLAGS=/arch:AVX2" -DKYTY_BUILD_LAUNCHER=OFF
cmake --build _Build\windows --target kyty_emulator --parallel 16
echo EXIT=%ERRORLEVEL%
