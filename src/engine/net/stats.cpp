// Online top stats.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/net/stats.h"
#include "engine/core/code_address.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/net/gamenet_manager.h"
#include "engine/net/online_hosts.h"
#include "engine/net/vault_man.h"
#include "engine/script/param_struct.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

namespace net {

namespace {

// A request to the stats server (0x118 bytes, filled on the stack and handed to 0x0054A910).
struct StatsRequest {
    uint32_t timeout; // 0x25800 (ms)
    char url[256];
    uint32_t callback; // 0x0054DB20
    uint32_t unknown108;
    uint32_t unknown10c_; // which reply: ratings or scores
    uint32_t unknown110;
    uint32_t unknown114;
};
static_assert(sizeof(StatsRequest) == 0x118, "built in the original's stack frame");

// The top-score files, 32 bytes each (0x0067F2A0): hs_at, bc_at, hs_mo, bc_mo, ratings.
const char* stats_file(int index)
{
    switch (index) {
    case 0:
        return "hs_at.txt";
    case 1:
        return "bc_at.txt";
    case 2:
        return "hs_mo.txt";
    case 3:
        return "bc_mo.txt";
    default:
        return "ratings.txt";
    }
}

} // namespace

// 0x0054DCB0
bool __cdecl retrieve_top_stats(script::Struct* params, script::Script*)
{
    online::Hub* manager = online::g_online_hub;
    online::RankingsManager* stats = manager->rating_service_;
    int& current = stats->top_file_;
    int index = 0;
    if (params->fetch_int_value(0x7360C9EF, &index, 0))
        current = index;
    else
        ++current;
    StatsRequest request;
    // Originally from PartyMod Fixes: gamestats.<OnlineDomain> in place of gamestats.gamespy.com (0x0064D670; PartyMod's DWord at
    // 0x0054DD14)
    const char* host = gamestats_host() ? gamestats_host() : "gamestats.gamespy.com";
    std::snprintf(request.url, sizeof(request.url), "http://%s/thug2pc/%s", host, stats_file(current));
    request.unknown114 = 0x1C049404;
    request.unknown108 = 0x1C049404;
    request.unknown110 = 0x74E0CD35;
    request.timeout = 0x25800;
    request.callback = reinterpret_cast<uint32_t>(code_address(online::RankingsManager::on_top_stats));
    request.unknown10c_ = current == 4 ? 0x7346E8E7 : 0xD932B07B;
    // (the vault, +0x14; the request is its DownloadRequest - the same 0x118 bytes)
    manager->vault_man_->download(reinterpret_cast<const online::DownloadRequest*>(&request));
    // (the original's +0x1FC: need_top_stats_ - this cleared a byte past it, our layout lacking the lists' table words)
    stats->need_top_stats_ = false;
    return true;
}

} // namespace net
