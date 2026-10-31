// The standalone game's entry point: the game built from source alone, no original exe (CMake target
// thug2_standalone, THUG2_STANDALONE defined).
//
// What runs before this is the C runtime's start-up, as the original's entry point (0x0062583D) did before its WinMain
// (0x004E2680): heap, threads, stdio, command line and environment, then the C and C++ initialiser tables - the
// compiler's dynamic initialisers of our own globals; its start-up code (WinMainCRTStartup) then calls this with what the
// original passes: the module handle, null, the command line after the program's name, and STARTUPINFO's wShowWindow
// when STARTF_USESHOWWINDOW is set, else SW_SHOWDEFAULT. The original's C runtime also set the x87 precision to 53 bits
// (_fpmath -> __setdefaultprecision), which is how Windows starts a process anyway, and started in the "C" locale, as
// ours does. Its game initialisers - the other half of that start-up - run first thing here.

#include <windows.h>

#include <cstring>

#include "game/sys/startup_init.h"
#include "game/sys/winmain.h"

namespace {

// NOT ORIGINAL: the hook DLL of the hooked build is a proxy called dinput8.dll and may sit in the game folder, which
// Windows searches first for a DLL loaded by name. Nothing here imports DirectInput by name (SDL makes it through COM,
// whose registration names the system's DLL by its path), but anything that did - an import, a delay-load, a
// LoadLibrary("dinput8.dll") - would get the proxy. Loaded from the system directory by its full path first, the
// system's is the dinput8.dll of this process: a later load by name finds it already loaded.
void LoadSystemDirectInput()
{
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    if (length == 0 || length + sizeof("\\dinput8.dll") > MAX_PATH)
        return;
    std::strcat(path, "\\dinput8.dll");
    LoadLibraryExA(path, nullptr, 0);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show)
{
    LoadSystemDirectInput();
    Sys::RunStartupInitialisers();
    return Sys::WinMain(instance, previous, commandLine, show);
}
