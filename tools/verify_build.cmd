@echo off
rem Build the verification configuration (Debug, no copy into the game folder) used by thug2_check.exe.
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -no_logo || exit /b 1
cd /d %~dp0..
cmake -S . -B out\build\verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe -DTHUG2_GAME_DIR=NONE || exit /b 1
cmake --build out\build\verify || exit /b 1
