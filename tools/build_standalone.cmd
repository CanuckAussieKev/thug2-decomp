@echo off
rem Build the thug2_standalone target (the game from source alone, no original exe) in the verification folder.
rem It doesn't link yet: tools/standalone_report.ts runs this and sorts the unresolved symbols.
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -no_logo || exit /b 1
cd /d %~dp0..
cmake -S . -B out\build\verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe -DTHUG2_GAME_DIR=NONE || exit /b 1
cmake --build out\build\verify --target thug2_standalone -- -k 0
