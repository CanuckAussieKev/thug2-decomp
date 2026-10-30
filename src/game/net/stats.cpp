// Online top stats.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/net/stats.h"
#include "hook/game_code.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/net/gamenet_manager.h"
#include "game/net/online_hosts.h"
#include "game/net/vault_man.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace Net {

namespace {

// A request to the stats server (0x118 bytes, filled on the stack and handed to 0x0054A910).
struct StatsRequest {
    uint32_t timeout; // 0x25800 (ms)
    char url[256];
    uint32_t callback; // 0x0054DB20
    uint32_t unknown108;
    uint32_t unknown10C; // which reply: ratings or scores
    uint32_t unknown110;
    uint32_t unknown114;
};
static_assert(sizeof(StatsRequest) == 0x118, "built in the original's stack frame");

// The top-score files, 32 bytes each (0x0067F2A0): hs_at, bc_at, hs_mo, bc_mo, ratings.
const char* StatsFile(int index)
{
    switch (index) {
    case 0: return GAME_TEXT(0x0067F2A0, "hs_at.txt");
    case 1: return GAME_TEXT(0x0067F2C0, "bc_at.txt");
    case 2: return GAME_TEXT(0x0067F2E0, "hs_mo.txt");
    case 3: return GAME_TEXT(0x0067F300, "bc_mo.txt");
    default: return GAME_TEXT(0x0067F320, "ratings.txt");
    }
}

} // namespace

// 0x0054DCB0
bool __cdecl RetrieveTopStats(Script::CStruct* params, Script::CScript*)
{
    uint8_t* manager = reinterpret_cast<uint8_t*>(GameNet::g_gameNetManager);
    uint8_t* stats = *reinterpret_cast<uint8_t**>(manager + 0x1C);
    int& current = *reinterpret_cast<int*>(stats + 0x90);
    int index = 0;
    if (params->GetInteger(0x7360C9EF, &index, 0))
        current = index;
    else
        ++current;
    StatsRequest request;
    // Originally from PartyMod Fixes: gamestats.<OnlineDomain> in place of gamestats.gamespy.com (0x0064D670; PartyMod's DWord at
    // 0x0054DD14)
    const char* host = GamestatsHost() ? GamestatsHost() : GAME_TEXT(0x0064D670, "gamestats.gamespy.com");
    std::snprintf(request.url, sizeof(request.url), "http://%s/thug2pc/%s", host, StatsFile(current));
    request.unknown114 = 0x1C049404;
    request.unknown108 = 0x1C049404;
    request.unknown110 = 0x74E0CD35;
    request.timeout = 0x25800;
    request.callback = reinterpret_cast<uint32_t>(GAME_CODE(0x0054DB20, GameNet::StatsMan::OnTopStats));
    request.unknown10C = current == 4 ? 0x7346E8E7 : 0xD932B07B;
    // (the vault, +0x14; the request is its DownloadRequest - the same 0x118 bytes)
    static_cast<GameNet::VaultMan*>(*reinterpret_cast<void**>(manager + 0x14))
        ->Download(reinterpret_cast<const GameNet::DownloadRequest*>(&request));
    stats[0x1FC] = 0;
    return true;
}

} // namespace Net

REPLACE(0x0054DCB0, Net::RetrieveTopStats);
