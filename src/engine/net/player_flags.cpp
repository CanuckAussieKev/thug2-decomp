// The network game's announcements about a player: fully in, capture the flag, king of the hill (see player_info.h).

#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/player_info.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
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
#include "game/rider/skater_score.h"
#include "game/rider/state_history.h"
#include "platform/compiler.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's sprintf, kept)

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kSkaterScore = 0xD262802F;        // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600;       // netfirefight
constexpr uint32_t kNetKing = 0x6EF8FDA0;            // netking
constexpr uint32_t kKing = 0x5D32129C;               // king
constexpr uint32_t kString0 = 0xA4B08520;            // String0
constexpr uint32_t kString1 = 0xD3B7B5B6;            // String1
constexpr uint32_t kTeam = 0x3B1F59E0;               // team
constexpr uint32_t kPlayer1 = 0x7A813378;            // player_1
constexpr uint32_t kPlayer2 = 0xE38862C2;            // player_2
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;       // hide_ctf_arrow
constexpr uint32_t kShowCtfArrow = 0x6F25888C;       // show_ctf_arrow
constexpr uint32_t kCapturedFlagYou = 0xF77B7EF8;    // captured_flag_you
constexpr uint32_t kCapturedYourFlag = 0x79A97AD8;   // captured_your_flag
constexpr uint32_t kCapturedFlagOther = 0xB11FCD98;  // captured_flag_other
constexpr uint32_t kStoleFlagYou = 0xE046F65E;       // stole_flag_you
constexpr uint32_t kStoleFlagFromYou = 0x647EC837;   // stole_flag_from_you
constexpr uint32_t kStoleFlagOther = 0xAA21CAD8;     // stole_flag_other
constexpr uint32_t kTookFlagYou = 0x2FAC0937;        // took_flag_you
constexpr uint32_t kTookFlagYours = 0x3DDE0CCE;      // took_flag_yours
constexpr uint32_t kTookFlagOther = 0x331C3A51;      // took_flag_other
constexpr uint32_t kRetrievedFlagYou = 0x60B68B40;   // retrieved_flag_you
constexpr uint32_t kRetrievedFlagOther = 0x50D47876; // retrieved_flag_other
constexpr uint32_t kShowCrownArrow = 0x1DFD5982;     // show_crown_arrow
constexpr uint32_t kHideCrownArrow = 0x8399BF35;     // hide_crown_arrow
constexpr uint32_t kNewKingYou = 0xDCA91E37;         // NewKingYou
constexpr uint32_t kNewKingOther = 0xB0CF4C93;       // NewKingOther

constexpr uint8_t kMsgPlayerFullyIn = 0x5E;
constexpr uint32_t kJoinSettleTime = 1000; // ms
constexpr uint32_t kJustJoinedTime = 5000; // ms

Hub* the_manager() { return g_online_hub; }
PlayerRecord* get_local(Hub* manager) { return find_own_player_record(manager); }
uint32_t flag_of(int team) { return kPlayerCarryingFlag0 << (static_cast<uint32_t>(team) & 31); }

// "team_<team + 1>_name" (the original's sprintf).
void get_team_key(char* key, int team) { std::sprintf(key, "team_%d_name", team + 1); }
const char* get_team_name(const char* key) { return script::fetch_text(key); }

// Every player carrying `team`'s flag (its lowest) carries none.
void take_flag_from_carriers(Hub* manager, int team, bool stop_at_first)
{
    PlayerSearch search;
    for (auto* p = start_roster_sweep(manager, &search, false); p; p = step_roster_sweep(manager, &search, false)) {
        if (p->get_flag_team() != team)
            continue;
        if (stop_at_first) {
            p->flag_bits_ &= ~kPlayerCarryingFlags;
            return;
        }
        p->flag_bits_ &= ~flag_of(team);
        p->flag_bits_ &= ~kPlayerCarryingFlags;
    }
}

} // namespace

// 0x00543910
bool PlayerRecord::is_invulnerable()
{
    if (!(conn_->cur_standing_ & net::Conn::kConnReady) || !skater_->get_flag_2c_4())
        return true;
    if (skater_->state_history_->state_count_ < 10) // (its state history's frames)
        return true;
    if (frame_clock::get_time() - join_time_ < kJoinSettleTime)
        return true;
    if (get_game_type() == kNetFirefight) {
        auto* skater = raw::as<objects::GameEntity>(skater_);
        if (static_cast<modules::PointTally*>(
                core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore))->points_held_)
                ->get_total_score() <= 0)
            return true;
    }
    if (is_eliminated() || (skater_->composite_flags_ >> 2) & 1)
        return true;
    if (frame_clock::get_time() - join_time_ <= kJustJoinedTime)
        return (flag_bits_ >> 1) & 1;
    flag_bits_ &= ~kPlayerJustJoined;
    return false;
}

// 0x005439D0
void PlayerRecord::set_finished_joining()
{
    Hub* manager = the_manager();
    flag_bits_ |= kPlayerFullyIn;
    if (!manager->in_net_game() || !manager->get_on_server() || (flag_bits_ & kPlayerLocal))
        return;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (manager->get_hosting_role()) {
    case 1: {
        net::NetMessageInfo desc;
        net::AppHost* server = manager->host_; // (0x005480C0, a getter folded there)
        desc.ident_ = kMsgPlayerFullyIn;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(conn_ ? conn_->hdl_ : 0, &desc);
        break;
    }
    case 2:
        if (!manager->get_host_player())
            manager->assign_first_come_first_served(); // (the game starts)
        break;
    }
}

// 0x00543A90
void PlayerRecord::score_flag_capture(int team)
{
    std::array<char, 64> key{};
    get_team_key(key.data(), team);
    const PlayerRecord* local = get_local(the_manager());
    script::Struct* params = new_params();
    if (flag_bits_ & kPlayerLocal) {
        params->append_word(kString0, script::kQbvalString, get_team_name(key.data()));
        params->put_int(kTeam, team);
        get_run(kCapturedFlagYou, params);
    } else if (!(local->flag_bits_ & kPlayerObserver) && local->team_ == team) {
        params->append_word(kString0, script::kQbvalString, label_.data());
        params->put_int(kTeam, team);
        get_run(kHideCtfArrow);
        get_run(kCapturedYourFlag, params);
    } else {
        params->append_word(kString0, script::kQbvalString, label_.data());
        params->append_word(kString1, script::kQbvalString, get_team_name(key.data()));
        params->put_int(kTeam, team);
        get_run(kCapturedFlagOther, params);
    }
    flag_bits_ &= ~kPlayerCarryingFlags;
    delete_params(params);
}

// 0x00543C70
void PlayerRecord::note_flag_steal(int team)
{
    Hub* manager = the_manager();
    std::array<char, 64> key{};
    get_team_key(key.data(), team);
    script::Struct* params = new_params();
    PlayerRecord* local = get_local(manager);
    if (flag_bits_ & kPlayerLocal) {
        params->append_word(kString0, script::kQbvalString, get_team_name(key.data()));
        get_run(kStoleFlagYou, params);
    } else {
        params->append_word(kString0, script::kQbvalString, label_.data());
        params->append_word(kString1, script::kQbvalString, get_team_name(key.data()));
        const bool from_local = !(local->flag_bits_ & kPlayerObserver) && local->get_flag_team() == team;
        get_run(from_local ? kStoleFlagFromYou : kStoleFlagOther, params);
    }
    delete_params(params);
    take_flag_from_carriers(manager, team, false);
    flag_bits_ |= flag_of(team);
}

// 0x00543F00
void PlayerRecord::note_flag_pickup(int team)
{
    Hub* manager = the_manager();
    std::array<char, 64> key{};
    get_team_key(key.data(), team);
    script::Struct* params = new_params();
    const PlayerRecord* local = get_local(manager);
    if (flag_bits_ & kPlayerLocal) {
        params->append_word(kString0, script::kQbvalString, get_team_name(key.data()));
        get_run(kTookFlagYou, params);
    } else if (!(local->flag_bits_ & kPlayerObserver) && team == local->team_) {
        script::Struct* arrow = new_params();
        arrow->put_int(kTeam, local->team_);
        get_run(kShowCtfArrow, arrow);
        delete_params(arrow);
        params->append_word(kString0, script::kQbvalString, label_.data());
        get_run(kTookFlagYours, params);
    } else {
        params->append_word(kString0, script::kQbvalString, label_.data());
        params->append_word(kString1, script::kQbvalString, get_team_name(key.data()));
        get_run(kTookFlagOther, params);
    }
    delete_params(params);
    flag_bits_ |= flag_of(team);
}

// 0x00544110
void PlayerRecord::note_flag_returned()
{
    Hub* manager = the_manager();
    std::array<char, 64> key{};
    get_team_key(key.data(), team_);
    script::Struct* params = new_params();
    if (flag_bits_ & kPlayerLocal) {
        params->append_word(kString0, script::kQbvalString, get_team_name(key.data()));
        params->put_int(kTeam, team_);
        get_run(kHideCtfArrow);
        get_run(kRetrievedFlagYou, params);
    } else {
        const PlayerRecord* local = get_local(manager);
        if (local && !(local->flag_bits_ & kPlayerObserver) && local->team_ == team_)
            get_run(kHideCtfArrow);
        params->append_word(kString0, script::kQbvalString, label_.data());
        params->append_word(kString1, script::kQbvalString, get_team_name(key.data()));
        params->put_int(kTeam, team_);
        get_run(kRetrievedFlagOther, params);
    }
    // (the structure is never deleted here: the original leaks it)
    take_flag_from_carriers(manager, team_, true);
}

// 0x00544340
void PlayerRecord::give_crown(bool king)
{
    Hub* manager = the_manager();
    if (!king) {
        if (!(flag_bits_ & kPlayerKing))
            return;
        if ((flag_bits_ & kPlayerLocal) && (get_game_type() == kNetKing || get_game_type() == kKing)) {
            script::Struct* params = new_params();
            params->put_name_key(kPlayer1, kPlayer1);
            params->put_name_key(kPlayer2, kPlayer2);
            get_run(kShowCrownArrow, params);
            delete_params(params);
        }
        flag_bits_ &= ~kPlayerKing;
        return;
    }
    if (auto* previous = is_crown_rule_active(manager))
        previous->give_crown(false);
    if (!(flag_bits_ & kPlayerKing)) {
        flag_bits_ |= kPlayerKing;
        if (flag_bits_ & kPlayerLocal) {
            script::Struct* params = new_params();
            const uint32_t player = skater_->get_skater_number() == 0 ? kPlayer1 : kPlayer2; // (its skater number)
            params->put_name_key(player, player);
            get_run(kHideCrownArrow, params);
            delete_params(params);
            get_run(kNewKingYou);
        } else {
            script::Struct* params = new_params();
            params->put_name_key(kPlayer1, kPlayer1);
            get_run(kShowCrownArrow, params);
            params->append_word(kString0, label_.data());
            get_run(kNewKingOther, params);
            delete_params(params);
        }
        // Split screen: the other screen's player is told too.
        if (!manager->in_net_game() && manager->find_player_slot_by_obj(get_skater_id(skater_) == 0 ? 1 : 0)) {
            script::Struct* params = new_params();
            params->append_word(kString0, skater_->fetch_shown_name()); // (its display name)
            get_run(kNewKingOther, params);
            delete_params(params);
        }
        flag_bits_ |= kPlayerKing;
    }
    if (void* crown = manager->get_crown_holder())
        static_cast<objects::CrownObject*>(crown)->put_on(skater_); // (worn by the skater)
}

} // namespace online

