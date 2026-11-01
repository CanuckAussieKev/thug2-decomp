// PartyMod's start-up and fast exit.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/pc_setup.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "engine/gfx/glyphs.h"
#include "engine/gfx/settings.h"
#include "engine/net/online_hosts.h"
#include "engine/net/online_identity.h"
#include "engine/script/patched.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "platform/sdl_events.h"

namespace sys {

void init_pc_setup()
{
    using debug_log::StageMap;
    build_info::prepare_all();
    bool debug = build_info::fetch_flag(build_info::kMiscellaneous, "Debug", false);
    debug_log::configure(debug);
    if (debug) {
        AllocConsole();
        FILE* unused;
        freopen_s(&unused, "CONIN$", "r", stdin);
        freopen_s(&unused, "CONOUT$", "w", stderr);
        freopen_s(&unused, "CONOUT$", "w", stdout);
    }
    debug_log::printf(StageMap::kInfo, "PC set-up\n");
    debug_log::printf(StageMap::kInfo, "DIRECTORY: %s\n", build_info::executable_directory());

    script::init_script_patches();
    os_layer::init_events();
    // the music shuffle's randomness (audio::shuffle_random)
    // NOLINTNEXTLINE(bugprone-random-generator-seed): the original's seed / a test's fixed sequence
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    debug_log::printf(StageMap::kInfo, "Patch Initialized\n");

    net::set_up_online_hosts();
    net::set_up_online_identity();
    render::load_glyph_settings();
    render::load_graphics_settings();
}

void fast_exit() { ExitProcess(0); }

} // namespace sys
