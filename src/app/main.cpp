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
#include "platform/mute_audio.h"
#include "platform/run_log.h"
#include "engine/sys/startup_init.h"
#include "app/command_table.h"
#include "app/extensions.h"
#include "app/winmain.h"
#include "audio_dsound/dsound_device.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/muted_device.h"
#include "engine/media/movie.h"
#include "media_bink/bink_movie.h"
#include "render_d3d9/d3d9_device.h"
#include "render_d3d9/d3d9_ui_backend.h"
#include "engine/gfx/ui_backend.h"
#include "engine/gfx/loading_console.h"
#include "render_d3d9/loading_console_panel.h"
#include "engine/gfx/render_device.h"
#include "game/commands/node_hooks.h"
#include "game/front_end/pad_hooks.h"
#include "game/online/transport_hooks.h"
#include "game/rider/debug_collision.h"
#include "game/skate/render_hooks.h"
#include "game/skate/sys_hooks.h"
#include "game/world/object_hooks.h"

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
    // NOT ORIGINAL: the script command table, composed in app/command_table.cpp, given to the script engine.
    app::install_command_tables();
    // NOT ORIGINAL: the extensions (the editor, the new front end, the dev tools) registered before any of the
    // original's code runs (app/extensions.h).
    app::register_extensions();
    // NOT ORIGINAL: the online session's hooks into the network transport (game/online/transport_hooks.h).
    online::install_transport_hooks();
    // NOT ORIGINAL: the node commands the engine's time of day and colour cycles call (game/commands/node_hooks.h).
    commands::install_node_hooks();
    // NOT ORIGINAL: what the engine's objects ask of the game (game/world/object_hooks.h).
    objects::install_game_hooks();
    // NOT ORIGINAL: what the engine's graphics ask of the game (game/skate/render_hooks.h).
    render::install_game_hooks();
    // NOT ORIGINAL: what the engine's input asks of the game (game/front_end/pad_hooks.h).
    sio::install_game_hooks();
    // NOT ORIGINAL: what the engine's main loop and debug mode ask of the game (game/skate/sys_hooks.h).
    sys::install_game_hooks();
    // NOT ORIGINAL: the graphics backend the engine draws with (engine/gfx/render_device.h): Direct3D 9, the only one.
    render::install_device(render::d3d9::device());
    // NOT ORIGINAL: Dear ImGui's platform and renderer backends for our UI layers - the level editor's, the new front
    // end's (engine/gfx/ui_backend.h): Direct3D 9's.
    render::install_ui_backend(render::d3d9::ui_backend());
    // NOT ORIGINAL: the sound output the engine plays on (engine/audio/audio_device.h): DirectSound's - or, with
    // [features] mute_audio=1, the muted output over it (engine/audio/muted_device.h).
    audio::install_device(sound::audio_muted() ? audio::muted_device(audio::dsound::device())
                                               : audio::dsound::device());
    // NOT ORIGINAL: the movie codec the movies and streams play with (engine/media/movie.h): Bink's (the player's
    // binkw32.dll).
    media::install_movie(media::bink::movie());
    // NOT ORIGINAL: the loading console's panel, which that backend draws (engine/gfx/loading_console.h).
    gfx::loading_console::install_panel(gfx::loading_console::panel_hooks());
    // NOT ORIGINAL: [features] debug_collision's log after each feeler query (game/rider/debug_collision.h).
    collision_overlay::install();
    sys::run_startup_initialisers();
    return sys::win_main(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}
