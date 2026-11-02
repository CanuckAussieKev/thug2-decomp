// The goal editor's small helpers: see goal_editor_util.h. From the disassembly of 0x004E6600-0x004E6B6E.

#include "game/park_editing/goal_editor_util.h"

#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/core/checksums.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/goal_editor.h"

namespace park_editing {

namespace {

constexpr uint32_t kSkate = 0x54166ACD, kCombo = 0x4EC3CFB5, kCone = 0x25904450;

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

} // namespace

// 0x004E6600
const char* goal_object_model(uint32_t type, int index)
{
    if (type == kCone)
        return "Mdl_Cone/mdl_cone.mdl";
    if (type == kCombo) {
        switch (index) {
        case 0:
            return R"(gameobjects\combo\goal_combo_c\goal_combo_c.mdl)";
        case 1:
        case 4:
            return R"(gameobjects\combo\goal_combo_o\goal_combo_o.mdl)";
        case 2:
            return R"(gameobjects\combo\goal_combo_m\goal_combo_m.mdl)";
        case 3:
            return R"(gameobjects\combo\goal_combo_b\goal_combo_b.mdl)";
        default:
            return nullptr;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0:
            return R"(gameobjects\skate\letter_s\letter_s.mdl)";
        case 1:
            return R"(gameobjects\skate\letter_k\letter_k.mdl)";
        case 2:
            return R"(gameobjects\skate\letter_a\letter_a.mdl)";
        case 3:
            return R"(gameobjects\skate\letter_t\letter_t.mdl)";
        case 4:
            return R"(gameobjects\skate\letter_e\letter_e.mdl)";
        default:
            return nullptr;
        }
    }
    return nullptr;
}

// 0x004E66A0
uint32_t goal_object_name(uint32_t type, int index)
{
    if (type == kCombo) {
        switch (index) {
        case 0:
            return 0x7BF1BA66;
        case 1:
        case 4:
            return 0x7247F64D;
        case 2:
            return 0x9C499761;
        case 3:
            return 0x0CF68AF0;
        default:
            return 0;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0:
            return 0xF75425EA;
        case 1:
            return 0xE438BDBC;
        case 2:
            return 0x04ED54A2;
        case 3:
            return 0x6930B049;
        case 4:
            return 0x038090BB;
        default:
            return 0;
        }
    }
    return 0;
}

// 0x004E6730
uint32_t edited_goal_node_name(int which, int goal)
{
    std::array<char, 100> name{};
    core::at(name, 0) = '\0';
    if (which == 0)
        get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", goal);
    else if (which == 1)
        get_sprintf(name.data(), "TRG_EditedGoal%d_Restart", goal);
    else
        get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", goal, which - 2);
    return checksums::hash_from_text(name.data());
}

// 0x004E67A0
script::Struct* goal_type_structure(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D:
        return script::read_sub_struct(0xEF8F345F);
    case kCombo:
        return script::read_sub_struct(0xB4F5801D);
    case 0x0FC6F698:
    case 0x7FE2238A:
    case 0xEA7BA666:
        return script::read_sub_struct(0x675816B5);
    case kCone:
        return script::read_sub_struct(0x34785A5B);
    case kSkate:
        return script::read_sub_struct(0x3328976A);
    case 0x61C5D092:
        return script::read_sub_struct(0x5756A675);
    case 0xD0DD047B:
        return script::read_sub_struct(0xD3828793);
    case 0xEC414B76:
        return script::read_sub_struct(0xB520A510);
    default:
        return nullptr;
    }
}

// 0x004E6890
script::Struct* goal_type_defaults(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D:
        return script::read_sub_struct(0x28C7B799);
    case kCombo:
        return script::read_sub_struct(0xFECBAB7C);
    case 0x0FC6F698:
        return script::read_sub_struct(0x6626B0D8);
    case kCone:
        return script::read_sub_struct(0xFD6CAF2A);
    case kSkate:
        return script::read_sub_struct(0xE41E0E04);
    case 0x61C5D092:
        return script::read_sub_struct(0x9005703A);
    case 0xEA7BA666:
        return script::read_sub_struct(0xAD585D92);
    case 0x7FE2238A:
        return script::read_sub_struct(0x38C1D87E);
    case 0xD0DD047B: {
        script::Struct* defaults = script::read_sub_struct(0x97FEFF8F);
        script::run_script_once(0xFA54B475, nullptr, nullptr, defaults);
        return defaults;
    }
    case 0xEC414B76:
        return script::read_sub_struct(0xAB62B082);
    default:
        return nullptr;
    }
}

// 0x004E69C0
int goal_editor_constant82() { return 0x82; }

// 0x004E69D0
void init_edited_goal(EditedGoal* goal)
{
    goal->acceleration_interval_ = 5;
    goal->time_to_stop_adding_tricks_ = 5;
    goal->taken_count_ = false;
    goal->level_ = 0;
    goal->label_.front() = 0;
    goal->summary_text_.front() = 0;
    goal->win_message_.front() = 0;
    goal->ped_name_.front() = 0;
    goal->kind_tag_ = 0;
    goal->vertex_total_ = 0;
    goal->min_points_ = 0;
    goal->max_points_ = 0;
    goal->current_point_ = 0;
    goal->last_point_placed_ = false;
    goal->won_ = false;
    goal->points_held_ = 100;
    goal->time_limit_ = 120;
    goal->race_total_laps_ = 1;
    goal->race_per_checkpoint_time_ = 0;
    goal->acceleration_percent_ = 0.1f;
    goal->trick_time_ = 3000;
    goal->max_tricks_ = 11;
    goal->twist_rate_ = 360;
    goal->single_combo_ = false;
    goal->chain_length_ = 2;
    goal->stack_block_span_ = 3;
    goal->tricktris_total_to_win_ = 8;
    goal->num_key_combo_sets_ = 0;
    goal->control_type_ = kSkate;
    goal->num_gaps_ = 0;
    goal->required_trick_.front() = 0;
    for (EditedGoalPoint& point : goal->path_vertices_) { // (each point's kind and height)
        point.kind = 0;
        point.height = 0.0f;
    }
    goal->level_goal_number_ = 0;
}

// 0x004E6AE0
void run_marker_object_script(int index)
{
    script::Struct params;
    char* name = g_goal_name_buffer.data();
    get_sprintf(name, "GoalEditorMarkerObject%d", index);
    params.put_name_key(0xA1DC81F9, checksums::hash_from_text(name));
    script::launch_script(0xDCF3A25E, &params, nullptr, false, nullptr);
    params.get_clear();
}

} // namespace park_editing
