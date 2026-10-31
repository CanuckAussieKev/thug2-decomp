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

namespace SystemCore {

void InitPcSetup()
{
    using DebugLog::StageMap;
    BuildInfo::PrepareAll();
    bool debug = BuildInfo::FetchFlag(BuildInfo::kMiscellaneous, "Debug", false);
    DebugLog::Configure(debug);
    if (debug) {
        AllocConsole();
        FILE* unused;
        freopen_s(&unused, "CONIN$", "r", stdin);
        freopen_s(&unused, "CONOUT$", "w", stderr);
        freopen_s(&unused, "CONOUT$", "w", stdout);
    }
    DebugLog::Printf(StageMap::kInfo, "PC set-up\n");
    DebugLog::Printf(StageMap::kInfo, "DIRECTORY: %s\n", BuildInfo::ExecutableDirectory());

    Scripts::InitScriptPatches();
    OsLayer::InitEvents();
    // the music shuffle's randomness (AudioStream::ShuffleRandom)
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    DebugLog::Printf(StageMap::kInfo, "Patch Initialized\n");

    NetLink::SetUpOnlineHosts();
    NetLink::SetUpOnlineIdentity();
    Engine3D::LoadGlyphSettings();
    Engine3D::LoadGraphicsSettings();
}

void FastExit()
{
    ExitProcess(0);
}

} // namespace Sys
