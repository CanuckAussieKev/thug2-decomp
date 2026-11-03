#include <cstdint>
// Online top stats.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/net/stats.h"
#include "platform/code_address.h"

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
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace {
constexpr uint32_t kStatsBufferBytes = 0x25800;

// Names (the checksums of their text).
constexpr uint32_t kDoNothing = 0x74E0CD35U;              // do_nothing
constexpr uint32_t kDownloadMoreStats = 0xD932B07BU;      // download_more_stats
constexpr uint32_t kFILE = 0x7360C9EFU;                   // FILE
constexpr uint32_t kStatsRetrievalComplete = 0x7346E8E7U; // stats_retrieval_complete
constexpr uint32_t kStatsRetrievalFailed = 0x1C049404U;   // StatsRetrievalFailed

} // namespace

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
bool retrieve_top_stats(script::Struct* params, script::Script* /*script*/)
{
    const online::Hub* manager = online::net_game_state().online_hub();
    online::RankingsManager* stats = manager->rating_service().get();
    int& current = stats->top_file();
    int index = 0;
    if (params->fetch_int_value(kFILE, &index, 0))
        current = index;
    else
        ++current;
    // (a download by the vault: online::DownloadRequest, filled on the stack and handed to 0x0054A910 - the original
    // built it as a struct of its own)
    online::DownloadRequest request = {};
    // Originally from PartyMod Fixes: gamestats.<OnlineDomain> in place of gamestats.gamespy.com (0x0064D670; PartyMod's DWord at
    // 0x0054DD14)
    const char* host = gamestats_host() ? gamestats_host() : "gamestats.gamespy.com";
    text::format_to(request.url, "http://%s/thug2pc/%s", host, stats_file(current));
    request.error_script_ = kStatsRetrievalFailed;
    request.fail_script_ = kStatsRetrievalFailed;
    request.progress_script_ = kDoNothing;
    request.buffer_size_ = kStatsBufferBytes;
    request.callback = code_address(online::RankingsManager::on_top_stats);
    request.done_script_ = current == 4 ? kStatsRetrievalComplete : kDownloadMoreStats; // (which reply: ratings or scores)
    online::VaultMan::download(&request);                                               // (the vault, +0x14)
    // (the original's +0x1FC: need_top_stats_ - this cleared a byte past it, our layout lacking the lists' table words)
    stats->set_need_top_stats(false);
    return true;
}

} // namespace net
