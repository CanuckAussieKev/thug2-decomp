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
bool retrieve_top_stats(script::Struct* params, script::Script*)
{
    const online::Hub* manager = online::g_online_hub;
    online::RankingsManager* stats = manager->rating_service_.get();
    int& current = stats->top_file_;
    int index = 0;
    if (params->fetch_int_value(0x7360C9EF, &index, 0))
        current = index;
    else
        ++current;
    // (a download by the vault: online::DownloadRequest, filled on the stack and handed to 0x0054A910 - the original
    // built it as a struct of its own)
    online::DownloadRequest request = {};
    // Originally from PartyMod Fixes: gamestats.<OnlineDomain> in place of gamestats.gamespy.com (0x0064D670; PartyMod's DWord at
    // 0x0054DD14)
    const char* host = gamestats_host() ? gamestats_host() : "gamestats.gamespy.com";
    std::snprintf(request.url, sizeof(request.url), "http://%s/thug2pc/%s", host, stats_file(current));
    request.error_script_ = 0x1C049404;
    request.fail_script_ = 0x1C049404;
    request.progress_script_ = 0x74E0CD35;
    request.buffer_size_ = 0x25800;
    request.callback = reinterpret_cast<const void*>(code_address(online::RankingsManager::on_top_stats));
    request.done_script_ = current == 4 ? 0x7346E8E7 : 0xD932B07B; // (which reply: ratings or scores)
    manager->vault_man_->download(&request);                       // (the vault, +0x14)
    // (the original's +0x1FC: need_top_stats_ - this cleared a byte past it, our layout lacking the lists' table words)
    stats->need_top_stats_ = false;
    return true;
}

} // namespace net
