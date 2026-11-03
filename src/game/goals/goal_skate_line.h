#pragma once

// The skate-the-line goal (SkateLineGoal, vtable 0x0064E790, 0x70 bytes): the goal_objects clusters in order (the
// one at num_flags_set_ is next - its time, goal_skate_the_line_next_cluster), won when every flag is set (with a combo
// required: on landing it).

#include <cstdint>

#include "game/goals/objective.h"
#include "engine/core/checksums.h"

namespace goals {

class SkateLineGoal final : public Objective {
public:
    SkateLineGoal(const SkateLineGoal&) = delete;
    SkateLineGoal& operator=(const SkateLineGoal&) = delete;
    // 0x005691F0: the constructor (Objective's). 0x00569210 / 0x00569290 (vtable +0x00): the destructor
    // body / the scalar deleting destructor.
    explicit SkateLineGoal(script::Struct* params);
    ~SkateLineGoal() override;
    // 0x00569220: the skater landed: with a combo required (and 0x7100155D), won when every flag is set, else lost.
    // The answer is the win's / loss's (false when it doesn't apply).
    bool land();
    // 0x005692C0: the next cluster started: its time; goal_skate_the_line_next_cluster with the goal's and the
    // cluster's parameters, its index (and the first spot's from the second on).
    void next_cluster();
    // 0x00569410 (vtable +0x4C): a flag set only when it is the next cluster's (the goal started first if it
    // isn't active): won when they're all set (unless a combo is required), else the next cluster. Not while won,
    // locked, or done waiting for the combo.
    bool set_flag(checksums::Name flag_name, int value, checksums::Name sub_goal) override;
};
static_assert(sizeof(SkateLineGoal) == 0x70, "the size the goal manager allocated");

} // namespace goals
