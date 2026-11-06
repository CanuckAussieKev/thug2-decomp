@echo off
rem Build the verification configuration (Debug, out\build\verify, no copy into a game folder): thug2-decomp.exe.
rem `verify_build x64`: the x64 configuration instead (out\build\verify-x64: the x64-ready libraries only,
rem docs/design/x64-port.md).
rem (its own PDB server: another build's left-over mspdbsrv otherwise fails it with "Program database manager mismatch")
setlocal
set _MSPDBSRV_ENDPOINT_=thug2verify
set ARCH=x86
set HOST=x86
set FOLDER=out\build\verify
if /i "%~1"=="x64" (
    set ARCH=x64
    set HOST=x64
    set FOLDER=out\build\verify-x64
    set _MSPDBSRV_ENDPOINT_=thug2verify64
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=%HOST% -no_logo || exit /b 1
cd /d %~dp0..
cmake -S . -B %FOLDER% -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe -DTHUG2_GAME_DIR=NONE || exit /b 1
cmake --build %FOLDER% || exit /b 1
