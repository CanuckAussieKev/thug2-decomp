@echo off
rem Builds the retail-limits DLL for players (src/retail_limits, docs/retail-limits.md): an x86 MinSizeRel build of just
rem thug2_retail_limits_dll (dinput8.dll) and its offline check (thug2-limits-check.exe) in out\build\retail-limits-x86
rem - the DLL is 32-bit only, as the original game is, whatever the main build's machine. tools/editor_tools.ts runs this
rem when the editor's tools folder has no DLL yet; the retail export installs it (--raised-limits).
rem   tools\build_retail_limits.cmd            -> out\build\retail-limits-x86\retail_limits\dinput8.dll
rem (its own PDB server, as verify_build.cmd's)
setlocal
set _MSPDBSRV_ENDPOINT_=thug2retaillimits
if "%CMAKE_BUILD_PARALLEL_LEVEL%"=="" set CMAKE_BUILD_PARALLEL_LEVEL=3
set FOLDER=out\build\retail-limits-x86
set VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat
if not exist "%VSDEVCMD%" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat
)
call "%VSDEVCMD%" -arch=x86 -host_arch=x86 -no_logo || exit /b 1
cd /d %~dp0..
cmake -S . -B %FOLDER% -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_CXX_COMPILER=cl.exe -DTHUG2_GAME_DIR=NONE || exit /b 1
cmake --build %FOLDER% --target thug2_retail_limits_dll thug2_retail_limits_check || exit /b 1
echo built %CD%\%FOLDER%\retail_limits\dinput8.dll
