// The goal manager's script commands, 0x005591C0-0x0055997F (see goal_manager_cfuncs.h): starting, stopping,
// winning and losing goals, their timers and flags.

#include <cstdint>

#include "game/goals/goal_manager_cfuncs.h"

#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_net_types.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"

namespace goals {

namespace {

constexpr uint32_t kName = 0xA1DC81F9;       // Name
constexpr uint32_t kParams = 0x7031F10C;     // params
constexpr uint32_t kDontAssert = 0x9AC8D54F; // DontAssert
constexpr uint32_t kForceAll = 0xCC365298;   // force_all
constexpr uint32_t kWholeTree = 0x687DE558;  // (name unknown: the goal's whole chain)
constexpr uint32_t kTime = 0x906B67BA;       // time
constexpr uint32_t kSkater = 0x5B8AB877;     // skater
constexpr uint32_t kFlag = 0x2E0B1465;       // flag

// The goal the command's Name names (asserted), and that name.
uint32_t get_goal_name(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    return name;
}

} // namespace

// 0x005591C0
bool cfunc_goal_manager_edit_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    script::Struct* edits = nullptr;
    params->read_sub_struct(kParams, &edits, 1);
    Objective* goal = manager->goals_.get_pull(name);
    if (goal)
        goal->rewrite_args(edits);
    return goal != nullptr;
}

// 0x00559230
bool cfunc_goal_manager_activate_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    const bool dont_assert = params->has_bare_flag(kDontAssert);
    return manager->engage_objective(name, dont_assert);
}

// 0x00559290
bool cfunc_goal_manager_deactivate_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    if (goal)
        goal->disengage(false, true);
    return goal != nullptr;
}

// 0x005592F0
bool cfunc_goal_manager_clear_last_goal(script::Struct*, script::Script*)
{
    get_objective_board()->previous_objective_ = 0;
    return true;
}

// 0x00559310
bool cfunc_goal_manager_win_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    return manager->complete_objective(get_goal_name(params));
}

// 0x00559350
bool cfunc_goal_manager_lose_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    return manager->fail_objective(get_goal_name(params));
}

// 0x00559390
bool cfunc_goal_manager_deactivate_all_goals(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    manager->disengage_every_objective(params->has_entry_named(kForceAll));
    return true;
}

// 0x005593D0
bool cfunc_goal_manager_activate_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->activate_all_goals();
    return true;
}

// 0x005593F0
bool cfunc_goal_manager_uninitialize_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->release_every_objective();
    return true;
}

// 0x00559410: with the flag 0x687DE558 the goal's whole chain, else the goal (one not in the level is read through
// null, as the original).
bool cfunc_goal_manager_uninitialize_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    if (params->has_bare_flag(kWholeTree)) {
        manager->release_objective_tree(name);
        return true;
    }
    manager->goals_.get_pull(name)->uninitialize(false, 0);
    return true;
}

// 0x00559490
bool cfunc_goal_manager_initialize_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->prepare_every_objective();
    return true;
}

// 0x005594B0
bool cfunc_goal_manager_initialize_all_selected_goals(script::Struct*, script::Script*)
{
    get_objective_board()->prepare_picked_objectives();
    return true;
}

// 0x005594D0
bool cfunc_goal_manager_activate_all_selected_goals(script::Struct*, script::Script*)
{
    get_objective_board()->activate_all_selected_goals();
    return true;
}

// 0x005594F0
bool cfunc_goal_manager_goal_is_active(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    return goal ? goal->is_operative() : false;
}

// 0x00559540: the timer set to `time` seconds - without one, started again from the goal's own time.
bool cfunc_goal_manager_set_goal_timer(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    int time = -1;
    params->fetch_int_value(kTime, &time, 0);
    Objective* goal = manager->goals_.get_pull(name);
    if (goal) {
        if (time == -1)
            goal->reset_timer();
        else
            goal->set_seconds_remaining(time);
    }
    return goal != nullptr;
}

// 0x005595D0
bool cfunc_goal_manager_zero_goal_timer(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    if (goal)
        goal->set_seconds_remaining(0);
    return goal != nullptr;
}

// 0x00559630
bool cfunc_goal_manager_pause_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->freeze_every_objective();
    return true;
}

// 0x00559650
bool cfunc_goal_manager_un_pause_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->thaw_every_objective();
    return true;
}

// 0x00559670: the goal's flag (the unnamed checksum) set to the unnamed integer - of `skater`'s flags, if given.
bool cfunc_goal_manager_set_goal_flag(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    uint32_t flag = 0;
    params->read_hash_value(0u, &flag, 1);
    int value = 0;
    params->fetch_int_value(0u, &value, 1);
    uint32_t skater = 0;
    params->read_hash_value(kSkater, &skater, 0);
    Objective* goal = manager->goals_.get_pull(name);
    if (!goal)
        return false;
    return goal->set_flag(flag, value, skater);
}

// 0x00559720
bool cfunc_goal_manager_has_seen_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    return goal ? goal->has_seen() : false;
}

// 0x00559770 (a goal not in the level is read through null, as the original)
bool cfunc_goal_manager_all_flags_set(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    return manager->goals_.get_pull(get_goal_name(params))->has_every_bit();
}

// 0x005597C0: the goal's `flag` is set (of `skater`'s flags, if given).
bool cfunc_goal_manager_goal_flag_set(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    uint32_t flag = 0;
    params->read_hash_value(kFlag, &flag, 1);
    uint32_t skater = 0;
    params->read_hash_value(kSkater, &skater, 0);
    return manager->goals_.get_pull(name)->bit_is_on(flag, skater);
}

// 0x00559850: there is a last goal to retry.
bool cfunc_goal_manager_can_retry_goal(script::Struct*, script::Script*)
{
    return get_objective_board()->previous_objective_ != 0;
}

// 0x00559870: the goal's parameters added to the script's.
bool cfunc_goal_manager_get_goal_params(script::Struct* params, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    script->args()->append_structure_if_any(goal->fetch_args());
    return true;
}

// 0x005598D0
bool cfunc_goal_manager_can_start_goal(script::Struct*, script::Script*)
{
    return get_objective_board()->start_allowed_;
}

// 0x005598E0: the race goal's next waypoint (the goal taken to be a race).
bool cfunc_goal_manager_next_race_waypoint(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const uint32_t name = get_goal_name(params);
    Objective* goal = manager->goals_.get_pull(name);
    return core::down_cast<RaceObjective>(goal)->advance_race_gate(name);
}

// 0x00559930
bool cfunc_goal_manager_next_waypoint(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    return manager->goals_.get_pull(get_goal_name(params))->step_to_next_waypoint();
}

} // namespace goals
