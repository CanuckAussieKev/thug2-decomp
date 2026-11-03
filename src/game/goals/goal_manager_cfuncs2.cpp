// The goal manager's script commands, 0x00559980-0x0055A6FF (see goal_manager_cfuncs.h): goal names, selection,
// counters, goal points, prosets, the goals' timers and records, their pros' streams, the end of run, the
// temporary special tricks.

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/str.h"
#include "game/skate/skate_module_calls.h" // IWYU pragma: keep - modules::Skate::sole(), which the check doesn't tie to this header

namespace goals {

namespace {
constexpr size_t kNameSize = 128;

using namespace goal_names;

constexpr uint32_t kName = 0xA1DC81F9;            // Name
constexpr uint32_t kGoalType = 0x6D11ED74;        // Goal_Type
constexpr uint32_t kLevel = 0x651533EC;           // level
constexpr uint32_t kIsClassicMode = 0x5A2C589A;   // is_classic
constexpr uint32_t kGoalName = 0xBFECC45B;        // goal_name
constexpr uint32_t kLevelPrefix = 0x15F818D0;     // level_prefix
constexpr uint32_t kTotal = 0x3DD01EA1;           // total
constexpr uint32_t kGoalScoreValue = 0x42C20492;  // goal_points
constexpr uint32_t kProsetNotPrefix = 0xC60CE720; // proset_not_prefix
constexpr uint32_t kType = 0x7321A8D6;            // type
constexpr uint32_t kProStuff = 0x18653764;        // pro_stuff
constexpr uint32_t kShowGoalPoints = 0x888EF05E;  // GoalManager_ShowGoalPoints

// strcpy / strcat (no bounds, as the original).
void dup_from(char* dest, const char* text) { text::copy_unbounded(dest, text); }
void get_append(char* dest, const char* text) { text::append_unbounded(dest, text); }

// The level structure being played (the skate module's +0x380 names it).
script::Struct* level_params_ref() { return script::read_sub_struct(modules::Skate::sole()->level_params()); }

uint32_t get_goal_name(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    return name;
}

// The goal the command names, the manager fetched first (most commands) / after the name.
Objective* manager_then_goal(script::Struct* params)
{
    GoalManager* manager = get_objective_board();
    return manager->goals().get_pull(get_goal_name(params));
}
Objective* name_then_goal(script::Struct* params)
{
    const uint32_t name = get_goal_name(params);
    return get_objective_board()->goals().get_pull(name);
}

} // namespace

// 0x00559980: the goal's id and name made from the level's name, _classic (classic, or a classic_mode_goal), _goal_
// and Goal_Type - into the script's goal_id / goal_name.
bool cfunc_goal_manager_create_goal_name(script::Struct* params, script::Script* script)
{
    const char* type = nullptr;
    params->fetch_text(kGoalType, &type, true);
    const char* level = nullptr;
    level_params_ref()->fetch_text(kLevel, &level, 1);
    std::array<char, kNameSize> name{};
    dup_from(name.data(), level);
    if (game_mode()->is_true(kIsClassicMode) || params->has_bare_flag(kClassicModeGoal))
        get_append(name.data(), "_classic"); // "_classic"
    get_append(name.data(), "_goal_");       // "_goal_"
    get_append(name.data(), type);
    script->args()->put_name_key(checksums::Name{kGoalIdChecksum}, checksums::hash_from_text(name.data()));
    script->args()->append_word(kGoalName, name.data());
    return true;
}

// 0x00559AF0: the level's name into the script's level_prefix.
bool cfunc_goal_manager_get_level_prefix(script::Struct*, script::Script* script)
{
    const char* level = nullptr;
    level_params_ref()->fetch_text(kLevel, &level, 1);
    script->args()->append_word(kLevelPrefix, level);
    return true;
}

// 0x00559B40: the graffiti mode (the unnamed integer; 1 without one).
bool cfunc_goal_manager_set_graffiti_mode(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    int mode = 1;
    params->fetch_int_value(0u, &mode, 0);
    manager->set_tagging_style(mode);
    return true;
}

// 0x00559B80
bool cfunc_goal_manager_has_won_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    return static_cast<uint8_t>(manager->has_beaten_goal(get_goal_name(params))) != 0; // (AL)
}

// 0x00559BC0
bool cfunc_goal_manager_goal_is_selected(script::Struct* params, script::Script*)
{
    return manager_then_goal(params)->is_picked();
}

// 0x00559C10
bool cfunc_goal_manager_toggle_goal_selection(script::Struct* params, script::Script*)
{
    Objective* goal = manager_then_goal(params);
    if (goal->is_picked())
        goal->unpick();
    else
        goal->choose_one();
    return true;
}

// 0x00559C80
bool cfunc_goal_manager_deselect_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->unpick_every_objective();
    return true;
}

// 0x00559CA0
bool cfunc_goal_manager_goals_are_selected(script::Struct*, script::Script*)
{
    return get_objective_board()->count_picked_objectives() > 0;
}

// 0x00559CC0: an object of the goal's counter got (if it's active). False without Name.
bool cfunc_goal_manager_got_counter_object(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    if (!params->read_hash_value(kName, &name, 0))
        return false;
    Objective* goal = get_objective_board()->goals().get_pull(name);
    if (goal->is_operative())
        goal->picked_up_countable();
    return true;
}

// 0x00559D20
bool cfunc_goal_manager_counter_goal_done(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->tally_objective_finished();
}

// 0x00559D70
bool cfunc_goal_manager_add_goal_point(script::Struct*, script::Script*)
{
    GoalManager* manager = get_objective_board();
    ++manager->objective_score();
    ++manager->total_goal_points();
    script::launch_script(kShowGoalPoints, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x00559DB0: the unnamed integer's goal points spent.
bool cfunc_goal_manager_spend_goal_points(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    int points = 0;
    params->fetch_int_value(0u, &points, 1);
    manager->objective_score() -= points;
    return true;
}

// 0x00559DF0: at least the unnamed integer's goal points.
bool cfunc_goal_manager_has_goal_points(script::Struct* params, script::Script*)
{
    const GoalManager* manager = get_objective_board();
    int points = 0;
    params->fetch_int_value(0u, &points, 1);
    return manager->objective_score() >= points;
}

// 0x00559E30
bool cfunc_goal_manager_clear_goal_points(script::Struct*, script::Script*)
{
    GoalManager* manager = get_objective_board();
    manager->objective_score() = 0;
    manager->total_goal_points() = 0;
    return true;
}

// 0x00559E50: the goal points (with total: all ever got) into the script's goal_points.
bool cfunc_goal_manager_get_number_of_goal_points(script::Struct* params, script::Script* script)
{
    const GoalManager* manager = get_objective_board();
    const int points = params->has_bare_flag(kTotal) ? manager->total_goal_points() : manager->objective_score();
    script->args()->put_int(checksums::Name{kGoalScoreValue}, points);
    return true;
}

// 0x00559E90: a goal of the proset named by the unnamed string is beaten.
bool cfunc_goal_manager_has_beaten_goal_with_proset(script::Struct* params, script::Script*)
{
    const char* prefix = nullptr;
    params->fetch_text(0u, &prefix, 1);
    return get_objective_board()->has_won_proset_goal(prefix);
}

// 0x00559EC0: the unnamed string without its last character, then NOT_ - into the script's proset_not_prefix.
// (An empty string reads the byte after its end, as the original.)
bool cfunc_goal_manager_get_proset_not_prefix(script::Struct* params, script::Script* script)
{
    const char* prefix = nullptr;
    params->fetch_text(0u, &prefix, 1);
    std::array<char, kNameSize> text{};
    core::at(text, 0) = 0;
    int length = 0;
    const core::CStringCursor at(prefix); // (an empty one reads 0 after its end where the original read what follows)
    if (at[1]) {
        for (bool again = true; again; again = at[static_cast<std::size_t>(length) + 1]) {
            core::at(text, length) = at[static_cast<std::size_t>(length)];
            ++length;
        }
    }
    core::at(text, length) = 0;
    get_append(text.data(), "NOT_"); // "NOT_"
    script->args()->append_word(kProsetNotPrefix, text.data());
    return true;
}

// 0x00559F70: the saved state given to the level's goals.
bool cfunc_goal_manager_level_load(script::Struct*, script::Script*)
{
    get_objective_board()->restore_goal_data();
    return true;
}

// 0x00559F90
bool cfunc_goal_manager_unlock_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->open_up_every_objective();
    return true;
}

// 0x00559FB0: stubbed in the game (a message only).
bool cfunc_goal_manager_deactivate_current_goal(script::Struct*, script::Script*)
{
    get_objective_board();
    debug::printf("CGoalManager::DeactivateCurrentGoal stubbed\n"); // "CGoalManager::DeactivateCurrentGoal stubbed\n"
    return true;
}

// 0x00559FD0
bool cfunc_goal_manager_set_start_time(script::Struct* params, script::Script*)
{
    name_then_goal(params)->reset_current_time();
    return true;
}

// 0x0055A020
bool cfunc_goal_manager_update_combo_timer(script::Struct* params, script::Script*)
{
    name_then_goal(params)->update_current_time();
    return true;
}

// 0x0055A070
bool cfunc_goal_manager_set_start_height(script::Struct* params, script::Script*)
{
    name_then_goal(params)->set_launch_altitude();
    return true;
}

// 0x0055A0C0
bool cfunc_goal_manager_check_height_record(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->test_highest_air();
}

// 0x0055A110
bool cfunc_goal_manager_check_distance_record(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->test_longest_jump();
}

// 0x0055A160: the goal's pro's start stream (with the command's parameters).
bool cfunc_goal_manager_play_goal_start_stream(script::Struct* params, script::Script*)
{
    manager_then_goal(params)->ped()->play_opening_voice_line(params);
    return true;
}

// 0x0055A1B0: the goal's pro's wait stream.
bool cfunc_goal_manager_play_goal_wait_stream(script::Struct* params, script::Script*)
{
    name_then_goal(params)->ped()->play_idle_voice_line();
    return true;
}

// 0x0055A200: the goal's pro's win stream (with the command's parameters).
bool cfunc_goal_manager_play_goal_win_stream(script::Struct* params, script::Script*)
{
    manager_then_goal(params)->ped()->play_victory_voice_line(params);
    return true;
}

// 0x0055A250: the goal's pro's stream stopped.
bool cfunc_goal_manager_stop_current_stream(script::Struct* params, script::Script*)
{
    name_then_goal(params)->ped()->cut_voice_line();
    return true;
}

// 0x0055A2A0: every locked pro goal unlocked.
bool cfunc_goal_manager_turn_pro(script::Struct*, script::Script*)
{
    get_objective_board()->unlock_all_pro_goals();
    return true;
}

// 0x0055A2C0
bool cfunc_goal_manager_pause_goal(script::Struct* params, script::Script*)
{
    name_then_goal(params)->get_freeze();
    return true;
}

// 0x0055A310
bool cfunc_goal_manager_un_pause_goal(script::Struct* params, script::Script*)
{
    name_then_goal(params)->un_pause();
    return true;
}

// 0x0055A360 (the goal taken to be a competition)
bool cfunc_goal_manager_pause_competition(script::Struct* params, script::Script*)
{
    core::down_cast<ContestObjective>(name_then_goal(params))->freeze_contest();
    return true;
}

// 0x0055A3B0
bool cfunc_goal_manager_un_pause_competition(script::Struct* params, script::Script*)
{
    core::down_cast<ContestObjective>(name_then_goal(params))->thaw_contest();
    return true;
}

// 0x0055A400
bool cfunc_goal_manager_goal_is_locked(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    return static_cast<uint8_t>(get_objective_board()->is_goal_blocked(name)) != 0; // (AL)
}

// 0x0055A430: an active competition's goal_id into the script's. False if none.
bool cfunc_goal_manager_is_in_competition(script::Struct*, script::Script* script)
{
    Objective* competition = get_objective_board()->get_active_competition();
    if (!competition)
        return false;
    script->args()->put_name_key(checksums::Name{kGoalIdChecksum}, competition->get_objective_key());
    return true;
}

// 0x0055A470: the goal's pro's animations of `type` as the script's pro_stuff (none: removed).
bool cfunc_goal_manager_get_goal_animations(script::Struct* params, script::Script* script)
{
    const uint32_t name = get_goal_name(params);
    const char* type = nullptr;
    params->fetch_text(kType, &type, 0);
    const Objective* goal = get_objective_board()->goals().get_pull(name);
    const uint32_t animations = goal->ped()->get_objective_anim_sets(type);
    script->args()->erase_module(kProStuff);
    if (animations)
        script->args()->put_name_key(checksums::Name{kProStuff}, animations);
    return true;
}

// 0x0055A500
bool cfunc_goal_manager_play_goal_stream(script::Struct* params, script::Script*)
{
    get_objective_board()->play_objective_voice_line(params);
    return true;
}

// 0x0055A520
bool cfunc_goal_manager_end_run_called(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->end_of_run_started();
}

// 0x0055A570
bool cfunc_goal_manager_clear_end_run(script::Struct* params, script::Script* script)
{
    name_then_goal(params)->end_of_run_done(script);
    return true;
}

// 0x0055A5C0
bool cfunc_goal_manager_finished_end_of_run(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->run_done();
}

// 0x0055A610
bool cfunc_goal_manager_started_end_of_run(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->run_started();
}

// 0x0055A660
bool cfunc_goal_manager_add_temp_special_trick(script::Struct* params, script::Script*)
{
    return name_then_goal(params)->award_special_trick();
}

// 0x0055A6B0
bool cfunc_goal_manager_remove_temp_special_trick(script::Struct* params, script::Script*)
{
    name_then_goal(params)->remove_reward_trick();
    return true;
}

} // namespace goals
