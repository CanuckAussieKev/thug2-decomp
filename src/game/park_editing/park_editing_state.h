#pragma once

#include <array>
#include <cstdint>
#include "game/park_editing/park.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/park_editing/park_editor.h"

// NOT ORIGINAL - the park editor's state its files share: the copied rails' origin and pools, the edited goal's and
// created goal's names, the collision and rail editor switches and the park editor's owner (the original's globals).
namespace park_editing {

// The park editor's state its files share: the copied rails' origin and pools, the edited goal's and created goal's
// names, the collision and rail editor switches and the park editor's owner. Made on first use, never destroyed.
class ParkEditingState {
public:
    maths::Vec4& get_zero() { return rail_origin_; }
    memory::PoolPair& rail_pools() { return rail_pools_; }
    std::array<char, 104>& name_buffer() { return goal_name_buffer_; }
    std::array<char, 24>& created_goal_name() { return created_goal_name_; }
    [[nodiscard]] bool collision_allowed() const { return collision_allowed_; }
    void set_collision_allowed(bool value) { collision_allowed_ = value; }
    bool& get_update_super_sectors() { return rail_editor_flag_; }
    [[nodiscard]] bool rail_editor_flag() const { return rail_editor_flag_; }
    void set_rail_editor_flag(bool value) { rail_editor_flag_ = value; }
    std::unique_ptr<park_editing::ParkBuilderMode>& park_editor() { return park_editor_; }

private:
    // (copy_buffer.cpp)
    maths::Vec4 rail_origin_{};
    memory::PoolPair rail_pools_{};

    // (edited_goal.cpp)
    std::array<char, 104> goal_name_buffer_{};

    // (edited_goal_io.cpp)
    std::array<char, 24> created_goal_name_{};

    // (park_editor_cfuncs.cpp)
    bool collision_allowed_{true};

    // (park_map.cpp)
    bool rail_editor_flag_{true};

    // (park_io.cpp)
    std::unique_ptr<park_editing::ParkBuilderMode> park_editor_{};
};

// The state (made on first use, never destroyed).
ParkEditingState& park_editing_state();

} // namespace park_editing
