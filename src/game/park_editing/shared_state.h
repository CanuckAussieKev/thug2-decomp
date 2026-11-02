#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "game/park_editing/park.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/park_editing/park_editor.h"

namespace park_editing {

// NOT ORIGINAL grouping - copy_buffer.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (copy_buffer_shared()).
struct CopyBufferShared {
    maths::Vec4 rail_origin_{};
    memory::PoolPair rail_pools_{};
};
CopyBufferShared& copy_buffer_shared();

} // namespace park_editing

namespace park_editing {

// NOT ORIGINAL grouping - edited_goal.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (edited_goal_shared()).
struct EditedGoalShared {
    std::array<char, 104> goal_name_buffer_{};
};
EditedGoalShared& edited_goal_shared();

} // namespace park_editing

namespace park_editing {

// NOT ORIGINAL grouping - edited_goal_io.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (edited_goal_io_shared()).
struct EditedGoalIoShared {
    std::array<char, 24> created_goal_name_{};
};
EditedGoalIoShared& edited_goal_io_shared();

} // namespace park_editing

namespace park_editing {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (park_editor_cfuncs_shared()).
struct ParkEditorCfuncsShared {
    bool collision_allowed_{true};
};
ParkEditorCfuncsShared& park_editor_cfuncs_shared();

} // namespace park_editing

namespace park_editing {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (park_map_shared()).
struct ParkMapShared {
    bool rail_editor_flag_{true};
};
ParkMapShared& park_map_shared();

} // namespace park_editing
