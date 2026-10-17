// PartyMod's start-up and fast exit.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/sys/pc_setup.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "game/gfx/glyphs.h"
#include "game/gfx/settings.h"
#include "game/net/online_hosts.h"
#include "game/script/patched.h"
#include "game/sys/config.h"
#include "game/sys/debug_log.h"
#include "game/sys/sdl_events.h"

// The hook DLL's last words (src/proxy/dinput8.cpp; tools/check has its own).
extern "C" void thug2_decomp_before_exit();

namespace Sys {

void InitPcSetup()
{
    using DebugLog::Level;
    Config::Init();
    bool debug = Config::GetBool(Config::kMiscellaneous, "Debug", false);
    DebugLog::Configure(debug);
    if (debug) {
        AllocConsole();
        FILE* unused;
        freopen_s(&unused, "CONIN$", "r", stdin);
        freopen_s(&unused, "CONOUT$", "w", stderr);
        freopen_s(&unused, "CONOUT$", "w", stdout);
    }
    DebugLog::Printf(Level::kInfo, "PC set-up\n");
    DebugLog::Printf(Level::kInfo, "DIRECTORY: %s\n", Config::ExecutableDirectory());

    Script::InitScriptPatches();
    Platform::InitEvents();
    // the music shuffle's randomness (Pcm::ShuffleRandom)
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    DebugLog::Printf(Level::kInfo, "Patch Initialized\n");

    Net::SetUpOnlineHosts();
    Nx::LoadGlyphSettings();
    Nx::LoadGraphicsSettings();
}

void FastExit()
{
    thug2_decomp_before_exit();
    ExitProcess(0);
}

} // namespace Sys
