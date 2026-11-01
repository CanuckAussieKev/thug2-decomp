// The horse, network and race goals, 0x005608C0-0x0056123F (see goal_net_types.h).

#include "game/goals/goal_net_types.h"

#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_names.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/victory_conditions.h"
#include "game/rider/end_run.h"
#include "game/rider/rider.h"
#include "engine/net/player_info.h"
#include "game/skate/point_tally.h"
#include "engine/core/down_cast.h"

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kHorseRunDone = 0xA32FAF13;           // (name unknown: the horse goal's run over)
constexpr uint32_t kLetterSpots = 0x92C6E839;            // (name unknown: the horse spots)
constexpr uint32_t kElimiskateInterval = 0xF92E5D89;     // elimiskate_interval
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;      // scavenger_interval
constexpr uint32_t kNetElimiskate = 0x10F84276;          // netelimiskate
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;       // netscavengerhunt
constexpr uint32_t kScavengerHunt = 0x846504FC;          // scavengerhunt
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period
constexpr uint32_t kGameContinues = 0xA4BA22BA;          // game_continues_after_expiration
constexpr uint32_t kRaceTotalLaps = 0x1DAAE95B;          // race_total_laps
constexpr uint32_t kRaceAddTime = 0xA4038EC9;            // (name unknown: a race whose waypoints add time)
constexpr uint32_t kRaceWaypoints = 0xA77B1F07;          // race_waypoints
constexpr uint32_t kStartLap = 0x61FB20D5;               // start_lap
constexpr uint32_t kLastWaypoint = 0x3442E2E0;           // last_waypoint
constexpr uint32_t kFirstWaypoint = 0xF83B9525;          // first_waypoint
constexpr uint32_t kScr = 0xA6D2D890;                    // scr
constexpr uint32_t kCheckpointMessage = 0x1E1B568A;      // goal_race_checkpoint_message
constexpr uint32_t kAddedTime = 0xC906B763;              // added_time
constexpr uint32_t kGoalName = 0xBFECC45B;               // goal_name
constexpr uint32_t kRaceNextWaypoint = 0xD3741449;       // goal_race_next_waypoint

online::Hub* net_session_slot() { return online::g_online_hub; }
uint32_t active_ruleset_label() { return g_game_mode->get_name_key(); }
// Whether every goal is completed (a goal attack's victory condition; the original calls the condition's check with
// whatever ECX holds - it doesn't use its object).
bool goals_completed() { return CompleteGoalsVictoryCondition::all_goals_complete(); }

using PlayerSearch = online::PlayerSearch;

script::Struct* spawn_param_table()
{
    script::Struct* made = script::spawn_param_bag();
    return made ? memory::construct_in<script::Struct>(made) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

} // namespace

// --- LetterGameObjective -----------------------------------------------------------------------------------------------------

// 0x005608C0
LetterGameObjective::LetterGameObjective(script::Struct* params) : Objective(params) {}

// 0x005608E0
LetterGameObjective::~LetterGameObjective() = default;

// 0x00560930
bool LetterGameObjective::wants_to_end()
{
    int done = 0;
    args_->fetch_int_value(kHorseRunDone, &done, 1);
    return done == 1 ? false : Objective::wants_to_end();
}

// 0x005608F0
bool LetterGameObjective::has_run_out()
{
    int done = 0;
    args_->fetch_int_value(kHorseRunDone, &done, 1);
    return done == 1 ? false : Objective::has_run_out();
}

// 0x00560970
void LetterGameObjective::expire() { Objective::expire(); }

// 0x005609B0
void LetterGameObjective::land()
{
    script::Array* spots = nullptr;
    args_->fetch_list(kLetterSpots, &spots, 1);
    const int count = static_cast<int>(spots->extent_);
    int i = 0;
    int score = 0; // (both set before they're read)
    uint32_t flag = 0;
    for (; i < count; ++i) {
        auto* spot = spots->struct_at(static_cast<uint32_t>(i));
        spot->read_hash_value(kFlag, &flag, 1);
        int set = 0;
        goal_flags_->fetch_int_value(flag, &set, 1);
        if (set)
            continue;
        if (!spot->fetch_int_value(kScore, &score, 0))
            args_->fetch_int_value(kScore, &score, 1);
        break;
    }
    if (i == count)
        return;
    const int landed = static_cast<modules::PointTally*>(
                           core::down_cast<objects::Rider>(skate_calls::get_home_rider())->get_points_total())
                           ->pot_score_;
    if (landed > 0) {
        const uint32_t id = get_objective_key();
        GoalManager* manager = get_objective_board();
        if (landed > score) {
            manager->raise_bit(id, flag, 1, 0);
            manager->step_to_next_waypoint(id);
        } else {
            manager->fail_objective(id);
        }
    }
    args_->put_int(kHorseRunDone, 0);
}

// --- OnlineObjective -------------------------------------------------------------------------------------------------------

// 0x00560AC0
OnlineObjective::OnlineObjective(script::Struct* params) : Objective(params) { run_ending_ = 1; }

// 0x00560AF0
OnlineObjective::~OnlineObjective() = default;

// 0x00560B00
bool OnlineObjective::shows_clock() { return !(cur_phase_ & kStateNoTimeLimit); }

// 0x00560B10
bool OnlineObjective::timer_beeps()
{
    return get_objective_key() != kElimiskateInterval && get_objective_key() != kScavengerInterval;
}

// 0x00560BC0
bool OnlineObjective::activate()
{
    net_session_slot()->reset_match_ended();
    return Objective::activate();
}

// 0x00560BE0
bool OnlineObjective::disengage(bool force, bool reset_tree)
{
    if (!force && active_ruleset_label() == kNetGoalAttack)
        return false;
    return Objective::disengage(force, reset_tree);
}

// 0x00560B70
bool OnlineObjective::tick_frame()
{
    if (!is_prepared_) {
        prepare_all();
        is_prepared_ = true;
    }
    if (active_ruleset_label() == kNetGoalAttack && goals_completed() && is_operative()) {
        expire();
        return true;
    }
    return Objective::tick_frame();
}

// 0x00560C20
bool OnlineObjective::wants_to_end()
{
    bool timed = false;
    bool scavenger = false;
    if (active_ruleset_label() == kNetElimiskate)
        timed = true;
    if (active_ruleset_label() == kNetScavengerHunt || active_ruleset_label() == kScavengerHunt) {
        scavenger = true;
        if (get_objective_key() != kScavengerCollectPeriod)
            timed = true;
    }
    if (active_ruleset_label() == kNetGoalAttack)
        return goals_completed() && all_skaters_run_done();
    if (timed)
        return seconds_remaining_ <= 0;
    online::Hub* net = net_session_slot();
    PlayerSearch search;
    if (!net->count_players())
        return true;
    if (cur_phase_ & kStateNoTimeLimit)
        return false;
    if (!net->final_scores_received() && net->in_net_game() && !scavenger)
        return false;
    if (run_ending_ == 2)
        return seconds_remaining_ <= 0;
    if (all_skaters_run_done() && seconds_remaining_ <= 0)
        return true;
    for (void* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        objects::Rider* skater = static_cast<online::PlayerRecord*>(player)->skater_;
        auto* end_run = core::down_cast<objects::RiderSessionEnd>(skater->fetch_module(kSkaterEndRun));
        if (!end_run->run_done() && !skater->is_halted_ && run_ending_ != 2 && seconds_remaining_ <= 0) {
            flag_bits_ |= kFlagEndOfRunStarted;
            end_run->end_run(false);
        }
    }
    return net->has_match_ended();
}

// 0x00560E20
void OnlineObjective::expire()
{
    online::Hub* net = net_session_slot();
    if (!args_->has_bare_flag(kGameContinues))
        net->mark_match_ended();
    set_seconds_remaining(0);
    Objective::expire();
}

// --- RaceObjective ------------------------------------------------------------------------------------------------------

// 0x00560E60
RaceObjective::RaceObjective(script::Struct* params) : Objective(params)
{
    params->fetch_int_value(kRaceTotalLaps, &laps_, 0);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    add_time_ = params->has_entry_named(kRaceAddTime);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00560EE0
RaceObjective::~RaceObjective() = default;

// 0x00560EF0
bool RaceObjective::activate()
{
    lap_ = 1;
    return Objective::activate();
}

// 0x00560F00
bool RaceObjective::has_run_out() { return !(cur_phase_ & kStateNoTimeLimit) && seconds_remaining_ <= 0; }

// 0x00560F20
bool RaceObjective::wants_to_end()
{
    if ((cur_phase_ & kStateNoTimeLimit) || seconds_remaining_ > 0)
        return false;
    Objective::wants_to_end();
    return true;
}

// 0x00560F40
void RaceObjective::expire()
{
    invoke_hook_script(kExpireScript);
    disengage(false, true);
}

// 0x00560F90
bool RaceObjective::advance_race_gate(uint32_t id)
{
    if (wants_to_end()) {
        debug::printf("expired!\n"); // "expired!\n"
        return false;
    }
    script::Array* waypoints = nullptr;
    args_->fetch_list(kRaceWaypoints, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->extent_; ++i) {
        auto* waypoint = waypoints->struct_at(i);
        uint32_t flag = 0;
        waypoint->read_hash_value(kFlag, &flag, 1);
        if (bit_matches(flag, 1, 0)) {
            if (i != waypoints->extent_ - 1)
                continue;
            if (lap_++ >= laps_)
                return true;
            zero_objective_bits();
            return advance_race_gate(id);
        }
        script::Struct* params = spawn_param_table();
        params->put_name_key(kGoalIdChecksum, id);
        if (i == 0 && laps_ > 1)
            params->put_int(kStartLap, lap_);
        if (i == waypoints->extent_ - 1 && lap_ == laps_)
            params->put_name_key(kLastWaypoint, 1);
        if (args_->has_bare_flag(kQuickStart))
            params->put_name_key(0u, kQuickStart);
        else
            params->remove_flag(kQuickStart);
        if (lap_ == 1 && i == 0) {
            params->put_int(kFirstWaypoint, 1);
        } else {
            params->put_name_key(kScr, kCheckpointMessage);
            if (!(cur_phase_ & kStateNoTimeLimit) && add_time_) {
                int time = 0;
                waypoint->fetch_int_value(kTime, &time, 0);
                advance_clock(time);
                params->put_int(kAddedTime, time);
            }
        }
        params->append_structure_if_any(waypoint);
        const char* goal_name = nullptr;
        args_->fetch_text(kGoalName, &goal_name, 1);
        std::array<char, 0x100> script_name{};
        std::snprintf(script_name.data(), sizeof(script_name), "%s_next_waypoint_script_id",
                      goal_name); // "%s_next_waypoint_script_id"
        script::spawn_script(kRaceNextWaypoint, params, 0, nullptr, 0, checksums::hash_from_text(script_name.data()), false,
                             false, false, false);
        delete_structure(params);
        return true;
    }
    return true;
}

} // namespace goals
