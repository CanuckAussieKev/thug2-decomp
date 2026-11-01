// The skate-the-line goal (SkateLineGoal, 0x005691F0-0x0056952F; see goal_skate_line.h).

#include <cstdint>

#include "game/goals/goal_skate_line.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/goals/goal_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace goals {

namespace {

constexpr uint32_t kRequiresLine = 0x7100155D;     // (name unknown: the line must be landed)
constexpr uint32_t kGoalObjects = 0x3CCE31F0;      // goal_objects
constexpr uint32_t kFlag = 0x2E0B1465;             // flag
constexpr uint32_t kTime = 0x906B67BA;             // time
constexpr uint32_t kGoalObjectsIndex = 0x89B927D0; // goal_objects_index
constexpr uint32_t kFirstSpotParams = 0xDB5515AA;  // first_spot_params
constexpr uint32_t kNextCluster = 0x918C00F2;      // goal_skate_the_line_next_cluster

script::Struct* spawn_param_table()
{
    script::Struct* made = script::spawn_param_bag();
    return made ? new (made) script::Struct : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

} // namespace

// 0x005691F0
SkateLineGoal::SkateLineGoal(script::Struct* params) : Objective(params) {}

// 0x00569210
SkateLineGoal::~SkateLineGoal() = default;

// 0x00569220
bool SkateLineGoal::land()
{
    if (!requires_combo())
        return false;
    if (!args_->has_bare_flag(kRequiresLine))
        return false;
    GoalManager* manager = get_objective_board();
    if (has_every_bit())
        return manager->complete_objective(get_objective_key());
    debug::printf("all flags not set\n"); // "all flags not set\n"
    return manager->fail_objective(get_objective_key());
}

// 0x005692C0
void SkateLineGoal::next_cluster()
{
    script::Array* objects = nullptr;
    args_->fetch_list(kGoalObjects, &objects, 1);
    auto* cluster = reinterpret_cast<script::Struct*>(objects->fetch_part(static_cast<uint32_t>(num_flags_set_)));
    int time;
    if (cluster->fetch_int_value(kTime, &time, 1))
        set_seconds_remaining(time);
    script::Struct* params = spawn_param_table();
    params->append_structure_if_any(args_);
    params->append_structure_if_any(cluster);
    params->put_int(kGoalObjectsIndex, num_flags_set_);
    if (num_flags_set_ == 1)
        params->put_sub_struct(kFirstSpotParams, reinterpret_cast<script::Struct*>(objects->fetch_part(0)));
    script::launch_script(kNextCluster, params, nullptr, false, nullptr);
    delete_structure(params);
}

// 0x00569410 (vtable +0x4C)
bool SkateLineGoal::set_flag(uint32_t flag, int value, uint32_t sub)
{
    if (has_won() || is_latched())
        return false;
    if (has_every_bit() && requires_combo())
        return false;
    script::Array* objects = nullptr;
    args_->fetch_list(kGoalObjects, &objects, 1);
    uint32_t cluster_flag;
    reinterpret_cast<script::Struct*>(objects->fetch_part(static_cast<uint32_t>(num_flags_set_)))
        ->read_hash_value(kFlag, &cluster_flag, 1);
    bool set = false;
    if (cluster_flag != flag)
        return set; // (only the line's next cluster)
    if (!is_operative())
        get_objective_board()->engage_objective(get_objective_key(), false);
    set = Objective::set_flag(flag, value, sub);
    if (!is_operative())
        return set;
    if (!has_every_bit()) {
        next_cluster();
        return set;
    }
    if (!requires_combo())
        get_objective_board()->complete_objective(get_objective_key());
    return set;
}

} // namespace goals
