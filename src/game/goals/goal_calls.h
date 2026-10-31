#pragma once

// The goal manager's functions the goals call (goal_manager.h), as the goals' code names them.

#include <cstdint>

#include "game/goals/goal_manager.h"

namespace goals {

class Objective;

namespace goal_calls {

inline GoalManager* as_manager(void* manager) { return static_cast<GoalManager*>(manager); }

// 0x005591B0: the skate module's goal manager.
inline void* objective_board() { return get_objective_board(); }
// 0x00557F90: the goal with id `id`, or null.
inline Objective* get_goal(void* manager, uint32_t id, int assert) { return as_manager(manager)->get_goal(id, assert); }
// 0x00559110: group `group` of level `level` (-1: the career's) is locked; group -1: never.
inline bool is_group_locked(void* manager, int group, int level)
{
    return as_manager(manager)->is_group_locked(group, level);
}
// 0x00557E40: the goal points counted up and shown.
inline void show_goal_points(void* manager) { as_manager(manager)->show_goal_points(); }
// 0x005588D0: goal `id` unlocked.
inline void open_up_objective(void* manager, uint32_t id) { as_manager(manager)->open_up_objective(id); }
// 0x00559050: the difficulty level (career: the career's; else the game mode's) - 0, 1 or 2.
inline int get_skill_tier(void* manager, bool career) { return as_manager(manager)->get_skill_tier(career); }
// 0x00558030 / 0x00558180: goal `id` won / lost (whether there was such a goal).
inline bool complete_objective(void* manager, uint32_t id) { return as_manager(manager)->complete_objective(id); }
inline bool fail_objective(void* manager, uint32_t id) { return as_manager(manager)->fail_objective(id); }
// 0x00557FC0: goal `id` started.
inline bool engage_objective(void* manager, uint32_t id, bool unused)
{
    return as_manager(manager)->engage_objective(id, unused);
}
// 0x0055DBE0: `goal` added to the active goals' list.
inline void add_active_goal(void* manager, Objective* goal) { as_manager(manager)->add_active_goal(goal); }

} // namespace goal_calls
} // namespace goals
