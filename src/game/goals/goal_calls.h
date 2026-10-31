#pragma once

// The goal manager's functions the goals call (goal_manager.h), as the goals' code names them.

#include <cstdint>

#include "game/goals/goal_manager.h"

namespace GoalSystem {

class CObjective;

namespace GoalCalls {

inline CGoalManager* AsManager(void* manager) { return static_cast<CGoalManager*>(manager); }

// 0x005591B0: the skate module's goal manager.
inline void* ObjectiveBoard() { return GetObjectiveBoard(); }
// 0x00557F90: the goal with id `id`, or null.
inline CObjective* GetGoal(void* manager, uint32_t id, int assert) { return AsManager(manager)->GetGoal(id, assert); }
// 0x00559110: group `group` of level `level` (-1: the career's) is locked; group -1: never.
inline bool IsGroupLocked(void* manager, int group, int level) { return AsManager(manager)->IsGroupLocked(group, level); }
// 0x00557E40: the goal points counted up and shown.
inline void ShowGoalPoints(void* manager) { AsManager(manager)->ShowGoalPoints(); }
// 0x005588D0: goal `id` unlocked.
inline void OpenUpObjective(void* manager, uint32_t id) { AsManager(manager)->OpenUpObjective(id); }
// 0x00559050: the difficulty level (career: the career's; else the game mode's) - 0, 1 or 2.
inline int GetSkillTier(void* manager, bool career) { return AsManager(manager)->GetSkillTier(career); }
// 0x00558030 / 0x00558180: goal `id` won / lost (whether there was such a goal).
inline bool CompleteObjective(void* manager, uint32_t id) { return AsManager(manager)->CompleteObjective(id); }
inline bool FailObjective(void* manager, uint32_t id) { return AsManager(manager)->FailObjective(id); }
// 0x00557FC0: goal `id` started.
inline bool EngageObjective(void* manager, uint32_t id, bool unused) { return AsManager(manager)->EngageObjective(id, unused); }
// 0x0055DBE0: `goal` added to the active goals' list.
inline void AddActiveGoal(void* manager, CObjective* goal) { AsManager(manager)->AddActiveGoal(goal); }

} // namespace GoalCalls
} // namespace Game
