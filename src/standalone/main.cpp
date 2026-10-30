// The standalone game's entry point: the game built from source alone, no original exe (CMake target
// thug2_standalone, THUG2_STANDALONE defined). It doesn't link yet - what's missing is listed by
// `node tools/standalone_report.ts`.

#include <windows.h>

#include "game/sys/winmain.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show)
{
    return Sys::WinMain(instance, previous, commandLine, show);
}
