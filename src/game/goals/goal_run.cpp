// goals::Objective while it runs: its scripts, links, update, expiry, the skater's end of run, gaps, waypoints, clusters,
// tricks, records (0x00554A80-0x00555A5F, 0x00556230; see objective.h).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/end_run.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/objects/compositemgr.h"
#include "engine/gfx/tod_manager.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include "engine/core/down_cast.h"

namespace goals {

namespace {
float g_goal_run_minus_milliseconds{-1000.0f};
float g_goal_run_inches_to_feet{0.083333336f};

using namespace goal_calls;
using namespace goal_names;

// The x87 code's float to int conversion (_ftol): truncation.
int truncate(double value) { return static_cast<int>(value); }

// A new link (the manager's Allocate, as the original's inlined new).
GoalEdge* alloc_chain_link()
{
    auto* link = static_cast<GoalEdge*>(memory::alloc(sizeof(GoalEdge)));
    if (link) {
        link->objective_key_ = 0;
        link->succ_ = nullptr;
        link->won_ = false;
    }
    return link;
}

online::Hub* net_session_slot() { return online::g_online_hub; }
objects::GameEntity* local_skater() { return skate_calls::get_home_rider(); }
// The game mode's name (0x005532A0) and whether it beeps the timer (0x00553150).
uint32_t active_ruleset_label()
{
    return static_cast<goals::MatchRules*>(skate_calls::get_game_mode())->get_name_key();
}
bool game_mode_timer_beeps()
{
    return static_cast<goals::MatchRules*>(skate_calls::get_game_mode())->clock_beeps_near_end();
}
// How often the score (modules::PointTally) has seen trick `trick` with no spin, not perfect, one tap (0x005624C0).
int get_trick_count(void* score, uint32_t trick)
{
    return static_cast<modules::PointTally*>(score)->count_trick_after_taps(trick, 0, false, 1);
}
// An object's member function by name.
bool call_object_member(objects::GameEntity* object, uint32_t name, script::Struct* params, script::Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}
objects::RiderSessionEnd* end_run_of(objects::GameEntity* skater)
{
    return core::down_cast<objects::RiderSessionEnd>(skater->fetch_module(kSkaterEndRun));
}

} // namespace

// 0x00554A80
bool Objective::invoke_hook_script(uint32_t name)
{
    uint32_t script = name;
    if (!args_->read_hash_value(name, &script, 0))
        return false;
    script::launch_script(script, args_.get(), nullptr, false, nullptr);
    return true;
}

// 0x00554AC0
void Objective::attach_upper_link(uint32_t id)
{
    if (!parents_->objective_key_) {
        parents_->objective_key_ = id;
        return;
    }
    GoalEdge* link = alloc_chain_link();
    link->objective_key_ = id;
    link->succ_ = parents_;
    parents_ = link;
}

// 0x00554B40
void Objective::attach_child(uint32_t id)
{
    if (!sub_nodes_->objective_key_) {
        sub_nodes_->objective_key_ = id;
        return;
    }
    for (const GoalEdge* link = sub_nodes_; link && link->objective_key_; link = link->succ_) {
        if (link->objective_key_ == id)
            return;
    }
    GoalEdge* link = alloc_chain_link();
    link->objective_key_ = id;
    link->succ_ = sub_nodes_;
    sub_nodes_ = link;
}

// 0x00554BD0
void Objective::uninitialize(bool children, int depth)
{
    if (is_operative())
        disengage(true, true);
    uint32_t script = 0;
    if (args_->read_hash_value(kUninit, &script, 0))
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    flag_bits_ &= ~kFlagInitialized;
    if (!children)
        return;
    void* manager = objective_board();
    for (const GoalEdge* link = sub_nodes_; link && link->objective_key_; link = link->succ_)
        get_goal(manager, link->objective_key_, 1)->uninitialize(children, depth + 1);
}

// 0x00554C70
bool Objective::lose()
{
    if (!is_operative())
        return false;
    uint32_t script = 0;
    if (args_->read_hash_value(kFail, &script, 0))
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    disengage(true, true);
    return true;
}

// 0x00554CD0
bool Objective::has_run_out()
{
    online::Hub* net = net_session_slot();
    if (!local_skater() || !net->count_players())
        return true;
    if (cur_phase_ & kStateNoTimeLimit)
        return false;
    return seconds_remaining_ <= 0;
}

// 0x00554D10
bool Objective::is_expired_at_end_of_run(bool force)
{
    online::Hub* net = net_session_slot();
    objects::GameEntity* skater = local_skater();
    if (!skater)
        return true;
    objects::RiderSessionEnd* end_run = end_run_of(skater);
    if (!net->count_players())
        return true;
    if (cur_phase_ & kStateNoTimeLimit)
        return false;
    if (run_ending_ == 0 || run_ending_ == 1) {
        const bool done = run_ending_ == 1 ? end_run->run_done() : end_run->goal_run_done();
        if (done)
            return seconds_remaining_ <= 0;
    }
    if (seconds_remaining_ > 0)
        return false;
    // Out of time: the run is ended, the goal expires once it's done.
    if (run_ending_ == 0) {
        flag_bits_ |= kFlagEndOfRunStarted;
        end_run->finish_objective_run(); // (the original passed `force`: not read)
    } else if (run_ending_ == 1) {
        flag_bits_ |= kFlagEndOfRunStarted;
        end_run->end_run(force);
    }
    return false;
}

// 0x00556230
bool Objective::wants_to_end() { return is_expired_at_end_of_run(false); }

// 0x00554DD0
bool Objective::tick_frame()
{
    if (is_operative() && !is_frozen())
        invoke_hook_script(kActiveScript);
    if (!is_operative() || is_frozen())
        return true;
    if (has_time_limit() && seconds_remaining_ >= 1) {
        const int before = seconds_remaining_;
        seconds_remaining_ +=
            truncate(frame_clock::get_slomo_frame_seconds() * g_goal_run_minus_milliseconds); // (-1000)
        elapsed_ += truncate(frame_clock::get_slomo_frame_seconds() * gfx::g_thousand);       // (1000)
        // The last ten seconds beep each second.
        if (seconds_remaining_ < 10000 && before / 1000 > seconds_remaining_ / 1000 && timer_beeps() &&
            game_mode_timer_beeps())
            script::launch_script(kTimerRunoutBeep, nullptr, nullptr, false, nullptr);
    }
    if (args_->has_bare_flag(kRequireCombo) && !args_->has_bare_flag(kChainActiveFlag)) {
        objects::GameEntity* skater = local_skater();
        if (skater) {
            const auto* score =
                core::down_cast<objects::Rider>(skater)->get_points_total();
            // (the combo's points: its base score times its multiplier)
            if (score && truncate(score->pot_ * static_cast<double>(score->multiplier_)) > 0)
                args_->put_name_key(kNoName, kChainActiveFlag);
        }
    }
    if (has_time_limit() && wants_to_end())
        expire();
    return true;
}

// 0x00554F40
void Objective::expire()
{
    uint32_t script = 0;
    if (args_->read_hash_value(kExpireScript, &script, 0))
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    if (flag_bits_ & kFlagDeactivateOnExpire)
        disengage(false, true);
}

// 0x00554F90
bool Objective::register_gap_hit(uint32_t gap)
{
    script::Array* gaps = nullptr;
    if (!args_->fetch_list(kNeededGapList, &gaps, 0))
        return false;
    const char* trick = kEmptyString;
    args_->fetch_text(kDemandedTrick, &trick, 0);
    if (*trick) {
        objects::GameEntity* skater = local_skater();
        if (!skater)
            return false;
        void* score = core::down_cast<objects::Rider>(skater)->get_points_total();
        const uint32_t name = checksums::hash_from_text(trick);
        const uint32_t backside = checksums::fold_text_into_checksum(kBsPrefix, trick);
        const uint32_t frontside = checksums::fold_text_into_checksum(kFsPrefix, trick);
        if (!get_trick_count(score, name) && !get_trick_count(score, backside) && !get_trick_count(score, frontside))
            return false;
    }
    for (uint32_t i = 0; i < gaps->extent_; ++i) {
        if (gaps->fetch_part(i) == gap) {
            std::array<char, 32> flag{};
            std::snprintf(flag.data(), sizeof(flag), kGotFormat, static_cast<int>(i + 1));
            set_flag(checksums::hash_from_text(flag.data()), 1, 0u);
            return true;
        }
    }
    return false;
}

// 0x00555100
bool Objective::step_to_next_waypoint()
{
    script::Array* waypoints = nullptr;
    if (!args_->fetch_list(kWaypoints, &waypoints, 0))
        args_->fetch_list(kWaypoints2, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->extent_; ++i) {
        auto* waypoint = waypoints->struct_at(i);
        uint32_t flag = 0;
        waypoint->read_hash_value(kFlag, &flag, 1);
        if (goal_flags_->has_entry_named(flag)) {
            int set = 0;
            goal_flags_->fetch_int_value(flag, &set, 1);
            if (set == 1)
                continue;
        }
        cur_phase_ |= kStatePaused;
        int time = 0;
        waypoint->fetch_int_value(kTime, &time, 1);
        if (args_->has_bare_flag(kAddTime)) {
            seconds_remaining_ += time * 1000 + 20;
        } else {
            args_->put_int(kTime, time);
            reset_timer();
        }
        uint32_t id = 0;
        args_->read_hash_value(kGoalIdName, &id, 1);
        waypoint->put_name_key(kGoalIdChecksum, id);
        if (!num_flags_set_)
            waypoint->put_name_key(kNoName, kFirstSpot);
        if (args_->has_bare_flag(kQuickStart))
            waypoint->put_name_key(kNoName, kQuickStart);
        else
            waypoint->remove_flag(kQuickStart);
        script::spawn_script(args_->has_bare_flag(kHorse) ? kHorseSpotScript : kRaceSpotScript, waypoint, 0, nullptr, 0,
                             0, false, false, false, false);
        waypoint->remove_flag(kQuickStart);
        return true;
    }
    return true;
}

// 0x005552D0
void Objective::kill_cluster(uint32_t cluster, int score)
{
    script::Array* clusters = nullptr;
    args_->fetch_list(kKillClusters, &clusters, 1);
    for (uint32_t i = 0; i < clusters->extent_; ++i) {
        auto* entry = clusters->struct_at(i);
        uint32_t id = 0;
        entry->read_hash_value(kIdName, &id, 1);
        int needed = 0;
        if (!entry->fetch_int_value(kScore, &needed, 0))
            args_->fetch_int_value(kScore, &needed, 1);
        if (id == cluster && score > needed) {
            if (!objects::g_trick_spot_hub->set_object_graffiti_color(cluster, 0))
                debug::printf("ModulateTrickObjectColor didn't work\n"); // "ModulateTrickObjectColor didn't work\n"
            uint32_t flag = 0;
            entry->read_hash_value(kFlag, &flag, 1);
            set_flag(flag, 1, 0u);
            return;
        }
    }
}

// 0x005553C0
bool Objective::did_trick_string()
{
    void* score = core::down_cast<objects::RiderPointKeeper>(local_skater()->fetch_module(kSkaterScore))->points_held_;
    const char* trick = nullptr;
    args_->fetch_text(kTrickString, &trick, 1);
    uint32_t type = 0;
    args_->read_hash_value(kTrickType, &type, 1);
    if (type == kGrind) {
        // (the original copies into its 128 bytes unbounded)
        std::array<char, 128> name{};
        std::snprintf(name.data(), sizeof(name), "FS %s", trick);
        if (get_trick_count(score, checksums::hash_from_text(name.data())))
            return true;
        std::snprintf(name.data(), sizeof(name), "BS %s", trick);
        if (get_trick_count(score, checksums::hash_from_text(name.data())))
            return true;
    }
    // (create_a_trick is looked at, but either way the trick string itself is counted)
    args_->has_bare_flag(kCreateATrick);
    return get_trick_count(score, checksums::hash_from_text(trick)) > 0;
}

// 0x00555540
uint32_t Objective::get_top_objective_key()
{
    void* manager = objective_board();
    const Objective* goal = this;
    while (goal->parents_ && goal->parents_->objective_key_)
        goal = get_goal(manager, goal->parents_->objective_key_, 1);
    uint32_t id = 0;
    goal->args_->read_hash_value(kGoalIdName, &id, 1);
    return id;
}

// 0x00555590
bool Objective::has_won()
{
    // (NetLobby counts whether or not a network game is on)
    if ((net_session_slot()->in_net_game() && active_ruleset_label() == kNetGoalAttack) ||
        active_ruleset_label() == kNetLobby)
        return (flag_bits_ & kFlagWonInNetGame) != 0;
    return (flag_bits_ & kFlagWon) != 0;
}

// 0x005555E0
void Objective::mark_won()
{
    if (net_session_slot()->in_net_game() && active_ruleset_label() == kNetGoalAttack)
        flag_bits_ |= kFlagWonInNetGame;
    else
        flag_bits_ |= kFlagWon;
    void* manager = objective_board();
    for (const GoalEdge* link = parents_; link && link->objective_key_; link = link->succ_)
        get_goal(manager, link->objective_key_, 1)->mark_won();
}

// 0x00555650
void Objective::set_stage_flag(int bit) { stage_flags_ |= 1u << (bit & 31); }

// 0x00555670
void Objective::set_net_stage_flag(int bit) { net_stage_flags_ |= 1u << (bit & 31); }

// 0x00555690
int Objective::stage_flag_set(int bit) { return (stage_flags_ & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556B0
int Objective::net_stage_flag_set(int bit) { return (net_stage_flags_ & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556D0
void Objective::set_launch_altitude()
{
    auto* state = core::down_cast<objects::RiderPoseState>(local_skater()->fetch_module(kSkaterState));
    args_->put_int(kLaunchAltitude, truncate(state->height_above_ground_));
}

// 0x00555710
bool Objective::test_highest_air()
{
    if (is_frozen())
        return false;
    int record = 0;
    args_->fetch_int_value(kRecord, &record, 0);
    objects::GameEntity* skater = local_skater();
    int start = 10000;
    args_->fetch_int_value(kLaunchAltitude, &start, 0);
    auto* state = core::down_cast<objects::RiderPoseState>(skater->fetch_module(kSkaterState));
    int height = truncate(state->height_above_ground_) - start;
    if (height < 0)
        height = 0;
    const int feet = height / 12;
    const int inches = height % 12;
    args_->put_int(kCurrentHeightFeet, feet);
    args_->put_int(kCurrentHeightInches, inches);
    if (height <= record)
        return false;
    args_->put_int(kRecord, height);
    args_->put_int(kRecordFeet, feet);
    args_->put_int(kRecordInches, inches);
    return true;
}

// 0x00555800
bool Objective::test_longest_jump()
{
    uint32_t id = 0;
    args_->read_hash_value(kDistanceObject, &id, 1);
    objects::Entity* object =
        static_cast<objects::ObjectHubBase*>(objects::g_composite_registry.get())->look_up_entity(id);
    const objects::GameEntity* skater = local_skater();
    if (!skater || !object)
        return false;
    // The distance across the ground: the height's square added and taken away again, as the original.
    const float* from = &core::down_cast<objects::GameEntity>(object)->at_point_.x;
    const float* to = &skater->at_point_.x;
    const double dx = static_cast<double>(from[0]) - to[0];
    const double dy = static_cast<double>(from[1]) - to[1];
    const double dz = static_cast<double>(from[2]) - to[2];
    const double down = static_cast<double>(to[1]) - from[1];
    const double distance = std::sqrt(dz * dz + dx * dx + dy * dy - down * down);
    auto rounded = static_cast<float>(distance);
    const int feet = truncate(distance * g_goal_run_inches_to_feet); // (1/12)
    const int whole = truncate(rounded);
    const int inches = whole % 12;
    args_->put_int(kCurrentDistanceFeet, feet);
    args_->put_int(kCurrentDistanceInches, inches);
    int record = 0;
    args_->fetch_int_value(kRecord, &record, 0);
    if (!(static_cast<double>(record) < rounded))
        return false;
    args_->put_int(kRecord, whole);
    args_->put_int(kRecordFeet, feet);
    args_->put_int(kRecordInches, inches);
    return true;
}

// 0x00555930
bool Objective::un_beat()
{
    if (net_session_slot()->in_net_game()) {
        stage_flags_ = 0;
        net_stage_flags_ = 0;
        if (flag_bits_ & kFlagWonInNetGame) {
            flag_bits_ &= ~kFlagWonInNetGame;
            return true;
        }
    } else if (flag_bits_ & kFlagWon) {
        flag_bits_ &= ~kFlagWon;
        return true;
    }
    return false;
}

// 0x00555970
void Objective::end_of_run_done(script::Script* script)
{
    flag_bits_ &= ~kFlagEndOfRunStarted;
    objects::GameEntity* skater = local_skater();
    objects::RiderSessionEnd* end_run = end_run_of(skater);
    if (run_ending_ == 0) {
        call_object_member(skater, kGoalEndOfRunDone, nullptr, script);
        end_run->clear_goal_end_of_run_started();
    } else if (run_ending_ == 1) {
        call_object_member(skater, kEndofRunDone, nullptr, script);
        end_run->clear_end_of_run_started();
    }
}

// 0x005559E0
bool Objective::run_done()
{
    objects::RiderSessionEnd* end_run = end_run_of(local_skater());
    if (run_ending_ == 0)
        return end_run->goal_run_done();
    if (run_ending_ == 1)
        return end_run->run_done();
    return false;
}

// 0x00555A20
bool Objective::run_started()
{
    objects::RiderSessionEnd* end_run = end_run_of(local_skater());
    if (run_ending_ == 0)
        return (flag_bits_ & kFlagEndOfRunStarted) != 0;
    if (run_ending_ == 1)
        return end_run->end_of_run_started();
    return false;
}

} // namespace goals
