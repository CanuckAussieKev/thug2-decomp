// The game's entry point (thug2-decomp.exe).
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
#include <array>
#include <span>

#include "platform/output_sinks.h"
#include "platform/run_log.h"
#include "engine/sys/startup_init.h"
#include "platform/winmain.h"

namespace {

// NOT ORIGINAL: a game folder may hold a dinput8.dll of its own (mod loaders and proxies are often called that), which
// Windows would find first for a DLL loaded by name. Nothing here imports DirectInput by name (SDL makes it through COM,
// whose registration names the system's DLL by its path), but anything that did - an import, a delay-load, a
// LoadLibrary("dinput8.dll") - would get that one. Loaded from the system directory by its full path first, the
// system's is the dinput8.dll of this process: a later load by name finds it already loaded.
void load_system_direct_input()
{
    std::array<char, MAX_PATH> path{};
    const UINT length = GetSystemDirectoryA(path.data(), MAX_PATH);
    if (length == 0 || length + sizeof("\\dinput8.dll") > MAX_PATH) {
        return;
    }
    std::memcpy(std::span<char>(path).subspan(length).data(), "\\dinput8.dll", sizeof("\\dinput8.dll"));
    LoadLibraryExA(path.data(), nullptr, 0);
}

} // namespace

// (its parameters named as the Windows SDK declares them)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, char* lpCmdLine, int nShowCmd)
{
    // NOT ORIGINAL: the debugger output and the run log's error lines behind the base layer's sinks; thug2-decomp.log
    // next to the exe and the crash handling (a crash logged with its call stack, never a dialog).
    platform::install_output_sinks();
    log_open();
    get_log("thug2-decomp standalone");
    load_system_direct_input();
    sys::run_startup_initialisers();
    return sys::win_main(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}
