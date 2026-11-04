// PartyMod's start-up and fast exit.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "app/pc_setup.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <SDL2/SDL.h>

#include "engine/gfx/engine_parts.h"
#include "engine/gfx/glyphs.h"
#include "engine/gfx/settings.h"
#include "engine/net/online_hosts.h"
#include "engine/net/online_identity.h"
#include "engine/script/patched.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "platform/sdl_events.h"

namespace sys {

namespace {

// The event pump's first handler: SDL_QUIT asks the game to quit (0x007D6A2C).
void handle_quit(SDL_Event* event)
{
    if (event->type == SDL_QUIT) {
        render::engine_parts().set_quit_requested(true);
    }
}

} // namespace

void init_pc_setup()
{
    using debug_log::StageMap;
    build_info::prepare_all();
    const bool debug = build_info::fetch_flag(build_info::kMiscellaneous, "Debug", false);
    debug_log::configure(debug);
    if (debug) {
        AllocConsole();
        FILE* unused = nullptr;
        freopen_s(&unused, "CONIN$", "r", stdin);
        freopen_s(&unused, "CONOUT$", "w", stderr);
        freopen_s(&unused, "CONOUT$", "w", stdout);
    }
    debug_log::printf(StageMap::kInfo, "PC set-up\n");
    debug_log::printf(StageMap::kInfo, "DIRECTORY: %s\n", build_info::executable_directory());

    script::init_script_patches();
    os_layer::init_events();
    os_layer::register_event_handler(handle_quit);
    // the music shuffle's randomness (audio::shuffle_random)
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    debug_log::printf(StageMap::kInfo, "Patch Initialized\n");

    net::set_up_online_hosts();
    net::set_up_online_identity();
    render::load_glyph_settings();
    render::load_graphics_settings();
}

void fast_exit() { ExitProcess(0); }

} // namespace sys
