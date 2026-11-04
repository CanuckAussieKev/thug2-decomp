// The network game's announcements about a player: fully in, capture the flag, king of the hill (see player_info.h).

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/net/gamenet_handler_util.h"
#include "engine/core/enum_ops.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/player_info.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
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
#include "engine/core/down_cast.h"
#include <gsl/pointers>
#include "platform/crt.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace online {

using namespace detail;

namespace {
constexpr uint32_t kBitMask = 31;
constexpr int kMinStateHistoryFrames = 10;

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

constexpr uint32_t kJoinSettleTime = 1000; // ms
constexpr uint32_t kJustJoinedTime = 5000; // ms

PlayerFlag flag_of(int team) { return carrying_flag_of(static_cast<int>(static_cast<uint32_t>(team) & kBitMask)); }

// "team_<team + 1>_name" (the original's sprintf).
void get_team_key(char* key, int team) { crt::get_sprintf(key, "team_%d_name", team + 1); }
const char* get_team_name(const char* key) { return script::fetch_text(key); }

// Every player carrying `team`'s flag (its lowest) carries none.
void take_flag_from_carriers(Hub* manager, int team, bool stop_at_first)
{
    PlayerSearch search;
    for (auto* player = start_roster_sweep(manager, &search, false); player; player = step_roster_sweep(manager, &search, false)) {
        if (player->get_flag_team() != team) {
            continue;
        }
        if (stop_at_first) {
            player->flag_bits() &= ~PlayerFlag::kCarryingFlags;
            return;
        }
        player->flag_bits() &= ~flag_of(team);
        player->flag_bits() &= ~PlayerFlag::kCarryingFlags;
    }
}

} // namespace

// 0x00543910
bool PlayerRecord::is_invulnerable()
{
    if (((conn_->cur_standing() & net::Conn::kConnReady) == 0U) || !rider()->get_flag_2c_4()) {
        return true;
    }
    if (rider()->state_history()->state_count() < kMinStateHistoryFrames) { // (its state history's frames)
        return true;
    }
    if (frame_clock::get_time() - join_time_ < kJoinSettleTime) {
        return true;
    }
    if (get_game_type() == kNetFirefight) {
        auto* skater = skater_;
        if (core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore))
                ->points_held()
                ->get_total_score() <= 0) {
            return true;
        }
    }
    if (is_eliminated() || (((skater_->composite_flags() >> 2) & 1) != 0U)) {
        return true;
    }
    if (frame_clock::get_time() - join_time_ <= kJustJoinedTime) {
        return core::any(flag_bits_ & PlayerFlag::kJustJoined);
    }
    flag_bits_ &= ~PlayerFlag::kJustJoined;
    return false;
}

// 0x005439D0
void PlayerRecord::set_finished_joining()
{
    Hub* manager = net_game_state().online_hub();
    flag_bits_ |= PlayerFlag::kFullyIn;
    if (!manager->in_net_game() || !manager->get_on_server() || core::any(flag_bits_ & PlayerFlag::kLocal)) {
        return;
    }
    switch (manager->get_hosting_role()) {
    case 1: {
        net::NetMessageInfo desc;
        net::AppHost* server = manager->host(); // (0x005480C0, a getter folded there)
        desc.set_ident(MessageId::kPlayerFullyIn);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(MessageGroup::kGame);
        server->put_on_outbox(conn_ ? conn_->hdl() : 0, &desc);
        break;
    }
    case 2:
        if (!manager->get_host_player()) {
            manager->assign_first_come_first_served(); // (the game starts)
        }
        break;
    default:
        break;
    }
}

// 0x00543A90
void PlayerRecord::score_flag_capture(int team)
{
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    get_team_key(key.data(), team);
    const PlayerRecord* local = find_own_player_record(net_game_state().online_hub());
    const script::StructPtr params = script::make_struct();
    if (core::any(flag_bits_ & PlayerFlag::kLocal)) {
        params->append_word(kString0, script::QbValueKind::kString, get_team_name(key.data()));
        params->put_int(checksums::Name{kTeam}, team);
        get_run(kCapturedFlagYou, params.get());
    } else if (!core::any(local->flag_bits_ & PlayerFlag::kObserver) && local->team_ == team) {
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        params->put_int(checksums::Name{kTeam}, team);
        get_run(kHideCtfArrow);
        get_run(kCapturedYourFlag, params.get());
    } else {
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        params->append_word(kString1, script::QbValueKind::kString, get_team_name(key.data()));
        params->put_int(checksums::Name{kTeam}, team);
        get_run(kCapturedFlagOther, params.get());
    }
    flag_bits_ &= ~PlayerFlag::kCarryingFlags;
}

// 0x00543C70
void PlayerRecord::note_flag_steal(int team)
{
    Hub* manager = net_game_state().online_hub();
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    get_team_key(key.data(), team);
    script::StructPtr params = script::make_struct();
    PlayerRecord* local = find_own_player_record(manager);
    if (core::any(flag_bits_ & PlayerFlag::kLocal)) {
        params->append_word(kString0, script::QbValueKind::kString, get_team_name(key.data()));
        get_run(kStoleFlagYou, params.get());
    } else {
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        params->append_word(kString1, script::QbValueKind::kString, get_team_name(key.data()));
        const bool from_local = !core::any(local->flag_bits_ & PlayerFlag::kObserver) && local->get_flag_team() == team;
        get_run(from_local ? kStoleFlagFromYou : kStoleFlagOther, params.get());
    }
    params.reset();
    take_flag_from_carriers(manager, team, false);
    flag_bits_ |= flag_of(team);
}

// 0x00543F00
void PlayerRecord::note_flag_pickup(int team)
{
    Hub* manager = net_game_state().online_hub();
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    get_team_key(key.data(), team);
    script::StructPtr params = script::make_struct();
    const PlayerRecord* local = find_own_player_record(manager);
    if (core::any(flag_bits_ & PlayerFlag::kLocal)) {
        params->append_word(kString0, script::QbValueKind::kString, get_team_name(key.data()));
        get_run(kTookFlagYou, params.get());
    } else if (!core::any(local->flag_bits_ & PlayerFlag::kObserver) && team == local->team_) {
        script::StructPtr arrow = script::make_struct();
        arrow->put_int(checksums::Name{kTeam}, local->team_);
        get_run(kShowCtfArrow, arrow.get());
        arrow.reset();
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        get_run(kTookFlagYours, params.get());
    } else {
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        params->append_word(kString1, script::QbValueKind::kString, get_team_name(key.data()));
        get_run(kTookFlagOther, params.get());
    }
    params.reset();
    flag_bits_ |= flag_of(team);
}

// 0x00544110
void PlayerRecord::note_flag_returned()
{
    Hub* manager = net_game_state().online_hub();
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    get_team_key(key.data(), team_);
    gsl::owner<script::Struct*> params = script::new_struct();
    if (core::any(flag_bits_ & PlayerFlag::kLocal)) {
        params->append_word(kString0, script::QbValueKind::kString, get_team_name(key.data()));
        params->put_int(checksums::Name{kTeam}, team_);
        get_run(kHideCtfArrow);
        get_run(kRetrievedFlagYou, params);
    } else {
        const PlayerRecord* local = find_own_player_record(manager);
        if (local && !core::any(local->flag_bits_ & PlayerFlag::kObserver) && local->team_ == team_) {
            get_run(kHideCtfArrow);
        }
        params->append_word(kString0, script::QbValueKind::kString, label_.data());
        params->append_word(kString1, script::QbValueKind::kString, get_team_name(key.data()));
        params->put_int(checksums::Name{kTeam}, team_);
        get_run(kRetrievedFlagOther, params);
    }
    // (the structure is never deleted here: the original leaks it)
    take_flag_from_carriers(manager, team_, true);
}

// 0x00544340
void PlayerRecord::give_crown(bool king)
{
    Hub* manager = net_game_state().online_hub();
    if (!king) {
        if (!core::any(flag_bits_ & PlayerFlag::kKing)) {
            return;
        }
        if (core::any(flag_bits_ & PlayerFlag::kLocal) && (get_game_type() == kNetKing || get_game_type() == kKing)) {
            const script::StructPtr params = script::make_struct();
            params->put_name_key(checksums::Name{kPlayer1}, kPlayer1);
            params->put_name_key(checksums::Name{kPlayer2}, kPlayer2);
            get_run(kShowCrownArrow, params.get());
        }
        flag_bits_ &= ~PlayerFlag::kKing;
        return;
    }
    if (auto* previous = is_crown_rule_active(manager)) {
        previous->give_crown(false);
    }
    if (!core::any(flag_bits_ & PlayerFlag::kKing)) {
        flag_bits_ |= PlayerFlag::kKing;
        if (core::any(flag_bits_ & PlayerFlag::kLocal)) {
            script::StructPtr params = script::make_struct();
            const uint32_t player = rider()->get_skater_number() == 0 ? kPlayer1 : kPlayer2; // (its skater number)
            params->put_name_key(checksums::Name{player}, player);
            get_run(kHideCrownArrow, params.get());
            params.reset();
            get_run(kNewKingYou);
        } else {
            const script::StructPtr params = script::make_struct();
            params->put_name_key(checksums::Name{kPlayer1}, kPlayer1);
            get_run(kShowCrownArrow, params.get());
            params->append_word(kString0, label_.data());
            get_run(kNewKingOther, params.get());
        }
        // Split screen: the other screen's player is told too.
        if (!manager->in_net_game() && manager->find_player_slot_by_obj(get_skater_id(skater_) == 0 ? 1 : 0)) {
            const script::StructPtr params = script::make_struct();
            params->append_word(kString0, rider()->fetch_shown_name()); // (its display name)
            get_run(kNewKingOther, params.get());
        }
        flag_bits_ |= PlayerFlag::kKing;
    }
    if (void* crown = manager->get_crown_holder()) {
        static_cast<objects::CrownObject*>(crown)->put_on(skater_); // (worn by the skater)
    }
}

} // namespace online

