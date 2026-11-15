// NOT ORIGINAL - the macOS (arm64) port's entry point. Windows starts an .exe at its WinMain; a Mach-O program starts at
// main, so this is what the linker enters: the program's command line handed to the Win32 shim (GetCommandLineA is what
// the game parses), then WinMain, whose parameters are Win32's instance and show-command handles (there are none here).
//
// app/winmain.cpp's WinMain and everything under it are the game's own: the same start-up, the same game.

#include <cstddef>

#include <unistd.h>

#include "platform/run_log.h"
#include "app/macos_layout.h"
#include "platform/posix/windows.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char* command_line, int show);

int main(int argc, char** argv)
{
    port_set_command_line(argc, argv);
    // NOT ORIGINAL - the working directory the original had: Windows starts a game in its own folder, and the game's
    // paths are relative to it ("Data\pre\...", "scripts\..."). A file manager starts this program in "/", so the
    // program puts itself where its data is before anything opens a file.
    if (chdir(port_game_directory()) != 0) {
        // (nowhere better to go: the absolute paths still work, and the log says where it ran)
    }
    return WinMain(nullptr, nullptr, GetCommandLineA(), 1 /* SW_SHOWNORMAL */);
}
