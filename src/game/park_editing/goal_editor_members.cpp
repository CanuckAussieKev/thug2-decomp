// The goal editor component's script member functions (park_editing::ObjectiveAuthoring, the table at 0x00684270): see
// goal_editor.h. From the disassembly of 0x004E8490-0x004E8F45, 0x004EA810, 0x004EBFF0 and 0x004EC1D0-0x004EC212
// (none of them Ghidra functions).

#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/format.h"
#include "engine/maths/vec4.h"
#include "game/park_editing/goal_editor.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/goal_editor_util.h"
#include "platform/crt.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park_editing_state.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace park_editing {

namespace {

constexpr std::uint32_t kAngle = 0xFF7EBAF6u; // Angle
constexpr std::uint32_t kExcludeParkEditorGoals = 0xB99F6330u; // ExcludeParkEditorGoals
constexpr std::uint32_t kGapNumber = 0x3AC9AB5Eu; // gap_number
constexpr std::uint32_t kGoalEditorUpdateCursorPosition = 0xDEA7FE56u; // goal_editor_update_cursor_position
constexpr std::uint32_t kLevel = 0x651533ECu; // Level
constexpr std::uint32_t kMarkUnbeaten = 0xC6322A25u; // MarkUnbeaten
constexpr std::uint32_t kMaxGoals = 0x8BC2E0DBu; // max_goals
constexpr std::uint32_t kNumTaps = 0xA4BEE6A1u; // num_taps
constexpr std::uint32_t kPos = 0x7F261953u; // Pos
constexpr std::uint32_t kRequirePerfect = 0x687E2C25u; // Require_Perfect
constexpr std::uint32_t kSetIndex = 0xEB7D6498u; // set_index
constexpr std::uint32_t kSingleCombo = 0xDF47B144u; // single_combo
constexpr std::uint32_t kSpin = 0xEDF5DB70u; // Spin
constexpr std::uint32_t kClearParkGoals = 0x98439808u; // a bare flag of nuke_all_goals: clear the park's goals, not the level's (text unknown)

constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D;

template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

const char* vacant() { return ""; } // ""

} // namespace

// 0x004E8490
bool ObjectiveAuthoring::edited_goal_add_gap(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params)) {
        int gap = 0;
        params->fetch_int_value(kGapNumber, &gap, 0);
        goal->register_gap_entry(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E84E0
bool ObjectiveAuthoring::edited_goal_remove_gap(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params)) {
        int gap = 0;
        params->fetch_int_value(kGapNumber, &gap, 0);
        goal->drop_gap_entry(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E8530
bool ObjectiveAuthoring::edited_goal_got_gap(script::Struct* params, script::Script* /*script*/)
{
    const EditedGoal* goal = get_goal_from_params(params);
    if (!goal)
        return false;
    int gap = 0;
    params->fetch_int_value(kGapNumber, &gap, 0);
    for (int i = 0; std::cmp_less(i, goal->num_gaps()); ++i) {
        if (core::at(goal->gaps(), i) == static_cast<uint8_t>(gap))
            return true;
    }
    return false;
}

// 0x004E8590
bool ObjectiveAuthoring::add_key_combo_set(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params)) {
        int set = 0, spin = 0, taps = 0;
        params->fetch_int_value(kSetIndex, &set, 0);
        params->fetch_int_value(kSpin, &spin, 0);
        params->fetch_int_value(kNumTaps, &taps, 0);
        goal->add_key_combo_set({.set = set, .must_be_flawless_ = params->has_bare_flag(kRequirePerfect), .spin = spin, .tap_total_ = taps});
    }
    return true;
}

// 0x004E8630
bool ObjectiveAuthoring::remove_key_combo_set(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params)) {
        int set = 0;
        params->fetch_int_value(kSetIndex, &set, 0);
        goal->remove_key_combo_set(set);
    }
    return true;
}

// 0x004E8680
bool ObjectiveAuthoring::get_key_combo_set(script::Struct* params, script::Script* script)
{
    EditedGoal* goal = get_goal_from_params(params);
    if (!goal)
        return true;
    int set = 0;
    params->fetch_int_value(kSetIndex, &set, 0);
    const KeyComboSet* entry = goal->find_key_combo_set(set);
    if (!entry)
        return false;
    script->args()->put_int(checksums::Name{kSpin}, entry->spin * kSpinUnitDegrees);
    script->args()->put_int(checksums::Name{kNumTaps}, entry->tap_total_);
    if (entry->must_be_flawless_)
        script->args()->put_name_key(checksums::Name{0u}, kRequirePerfect);
    return true;
}

// 0x004E8720
bool ObjectiveAuthoring::remove_goal_specific_flag(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params)) {
        uint32_t flag = 0;
        params->read_hash_value(0u, &flag, 0);
        if (goal->kind_tag() == kGoalTypeComboTricks && flag == kSingleCombo)
            goal->set_single_combo(false);
    }
    return true;
}

// 0x004E8780
bool ObjectiveAuthoring::set_goal_specific_params(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params))
        goal->read_specific_params(params);
    return true;
}

// 0x004E87A0
bool ObjectiveAuthoring::flag_goal_as_won(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params))
        goal->set_won(true);
    return true;
}

// 0x004E87C0
bool ObjectiveAuthoring::find_unfinished_goal(script::Struct* params, script::Script* /*script*/)
{
    uint32_t type = 0;
    params->read_hash_value("type", &type, 0);
    active_node_ = nullptr;
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.kind_tag() == type && goal.vertex_total() < goal.min_points())
            active_node_ = &goal; // (the last one)
    }
    return active_node_ != nullptr;
}

// 0x004E8900
bool ObjectiveAuthoring::goal_exists(script::Struct* params, script::Script* /*script*/)
{
    return get_goal_from_params(params) != nullptr;
}

// 0x004E8920
bool ObjectiveAuthoring::removed_created_goal(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params))
        drop_objective(goal);
    return true;
}

// 0x004E8950
bool ObjectiveAuthoring::goal_has_all_positions_set(script::Struct* params, script::Script* /*script*/)
{
    const EditedGoal* goal = get_goal_from_params(params);
    return goal && goal->vertex_total() >= goal->min_points();
}

// 0x004E8980
bool ObjectiveAuthoring::set_current_editor_goal(script::Struct* params, script::Script* /*script*/)
{
    active_node_ = get_goal_from_params(params);
    return true;
}

// 0x004E89A0
bool ObjectiveAuthoring::set_editor_goal_type(script::Struct* params, script::Script* /*script*/)
{
    uint32_t type = 0;
    params->read_hash_value("type", &type, 0);
    if (EditedGoal* goal = get_goal_from_params(params))
        goal->change_species(type);
    return true;
}

// 0x004E89F0
bool ObjectiveAuthoring::set_editor_goal_description(script::Struct* params, script::Script* /*script*/)
{
    const char* text = vacant();
    params->fetch_text("text", &text, 0);
    if (EditedGoal* goal = get_goal_from_params(params))
        std::strcpy(goal->summary_text().data(), text);
    return true;
}

// 0x004E8A40
bool ObjectiveAuthoring::set_editor_goal_win_message(script::Struct* params, script::Script* /*script*/)
{
    const char* text = vacant();
    params->fetch_text("text", &text, 0);
    if (EditedGoal* goal = get_goal_from_params(params))
        std::strcpy(goal->win_message().data(), text);
    return true;
}

// 0x004E8AA0
bool ObjectiveAuthoring::get_num_edited_goals(script::Struct* params, script::Script* script)
{
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    int count = count_complete_goals(level);
    if (!level && params->has_bare_flag(kExcludeParkEditorGoals))
        count -= count_complete_goals(kParkEditorLevel);
    script->args()->put_int("NumGoals", count);
    return true;
}

// 0x004E8B30
bool ObjectiveAuthoring::get_current_edited_goal_id(script::Struct* /*params*/, script::Script* script)
{
    script->args()->erase_module("goal_id");
    if (active_node_) {
        char* name = park_editing_state().created_goal_name().data();
        get_sprintf(name, "CreatedGoal%d", active_node_->level_goal_number());
        script->args()->put_name_key("goal_id", checksums::hash_from_text(name));
    }
    return true;
}

// 0x004E8B90
bool ObjectiveAuthoring::write_edited_goal_node_positions(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params))
        goal->emit_node_coords();
    return true;
}

// 0x004E8BB0
bool ObjectiveAuthoring::set_editor_goal_name(script::Struct* params, script::Script* /*script*/)
{
    const char* text = vacant();
    if (active_node_ && params->fetch_text("Name", &text, 0)) {
        active_node_ = get_goal_from_params(params); // (none named: a null write, as the original)
        std::strcpy(active_node_->label().data(), text);
    }
    return true;
}

// 0x004E8C20
bool ObjectiveAuthoring::set_editor_ped_name(script::Struct* params, script::Script* /*script*/)
{
    const char* text = vacant();
    if (active_node_ && params->fetch_text("Name", &text, 0)) {
        active_node_ = get_goal_from_params(params);
        std::strcpy(active_node_->ped_name().data(), text);
    }
    return true;
}

// 0x004E8C90
bool ObjectiveAuthoring::max_edited_goals_reached(script::Struct* params, script::Script* /*script*/)
{
    if (count_used_goals() == kEditedGoalLimit)
        return true;
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    return count_complete_goals(level) >= kGoalsPerLevel;
}

// 0x004E8CF0
bool ObjectiveAuthoring::nuke_all_goals(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kClearParkGoals))
        clear_park_goals();
    else
        clear_level_goals();
    return true;
}

// 0x004E8D30
bool ObjectiveAuthoring::get_max_goals_per_level(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_int(checksums::Name{kMaxGoals}, kGoalsPerLevel);
    return true;
}

// 0x004E8D50
bool ObjectiveAuthoring::set_goal_score(script::Struct* params, script::Script* /*script*/)
{
    active_node_ = get_goal_from_params(params);
    if (active_node_) {
        int score = 0;
        if (params->fetch_int_value(0u, &score, 0))
            active_node_->set_points_held(score);
    }
    return true;
}

// 0x004E8DB0
bool ObjectiveAuthoring::set_goal_time_limit(script::Struct* params, script::Script* /*script*/)
{
    active_node_ = get_goal_from_params(params);
    if (active_node_) {
        int seconds = kDefaultTimeLimitSeconds;
        if (params->fetch_int_value(0u, &seconds, 0))
            active_node_->set_countdown_seconds(seconds);
    }
    return true;
}

// 0x004E8E00
bool ObjectiveAuthoring::set_goal_control_type(script::Struct* params, script::Script* /*script*/)
{
    active_node_ = get_goal_from_params(params);
    if (active_node_) {
        uint32_t control = kGoalTypeSkate;
        if (params->read_hash_value(0u, &control, 0))
            active_node_->set_control_type(control);
    }
    return true;
}

// 0x004E8E60
bool ObjectiveAuthoring::refresh_goal_cursor_position(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (!active_node_)
        return true;
    maths::Vec4 pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    float height = 0.0f, angle = 0.0f;
    active_node_->get_current_point(&pos, {.height = &height, .angle = &angle});
    script::Struct cursor;
    cursor.put_vec3(checksums::Name{kPos}, {.x = pos.x, .y = pos.y + height, .z = pos.z});
    cursor.push_float(checksums::Name{kAngle}, angle);
    script::launch_script(kGoalEditorUpdateCursorPosition, &cursor, nullptr, false, nullptr);
    cursor.get_clear();
    return true;
}

// 0x004EA810
bool ObjectiveAuthoring::edit_goal(script::Struct* params, script::Script* /*script*/)
{
    active_node_ = get_goal_from_params(params);
    if (active_node_) {
        active_node_->create_markers();
        move_cursor_to_current_point();
        refresh_cursor();
    }
    return true;
}

// 0x004EBFF0
bool ObjectiveAuthoring::get_current_edited_goal_info(script::Struct* /*params*/, script::Script* script)
{
    if (active_node_)
        active_node_->export_to_struct(script->args().get());
    return true;
}

// 0x004EC1D0
bool ObjectiveAuthoring::add_edited_goal_to_goal_manager(script::Struct* params, script::Script* /*script*/)
{
    if (EditedGoal* goal = get_goal_from_params(params))
        goal->add_to_goal_manager(params->has_bare_flag(kMarkUnbeaten));
    return true;
}

// 0x004EC200
bool ObjectiveAuthoring::get_created_goals(script::Struct* /*params*/, script::Script* script)
{
    write_goals(script->args().get());
    return true;
}

} // namespace park_editing
