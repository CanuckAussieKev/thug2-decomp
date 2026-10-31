@echo off
rem Build the verification configuration (Debug, out\build\verify, no copy into a game folder): thug2-decomp.exe.
rem (its own PDB server: another build's left-over mspdbsrv otherwise fails it with "Program database manager mismatch")
set _MSPDBSRV_ENDPOINT_=thug2verify
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -no_logo || exit /b 1
cd /d %~dp0..
cmake -S . -B out\build\verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe -DTHUG2_GAME_DIR=NONE || exit /b 1
cmake --build out\build\verify || exit /b 1