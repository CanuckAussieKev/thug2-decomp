// An edited goal's own code (park_editing::EditedGoal): see goal_editor.h. From the disassembly of 0x004E6B80-0x004E7B1E and
// 0x004E93C0.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/format.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/park_editing/park_editor.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "platform/crt.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace park_editing {

std::array<char, 104> g_goal_name_buffer;

namespace {

constexpr float kPi = 3.14159274101257324f;            // 0x006453D8
constexpr float kDegreesPerRadian = 57.2957763671875f; // 0x00645468

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

// The goal editor's shared name buffer (also GoalEditorMarkerObject<n>'s).
std::array<char, 104>& name_buffer() { return g_goal_name_buffer; }

uint32_t current_level() { return modules::Skate::sole()->level_; }

} // namespace

// 0x004E93C0
EditedGoal::EditedGoal() { init_edited_goal(this); }

// 0x004E6B80
void EditedGoal::drop_gap_entry(uint8_t gap)
{
    for (int i = 0; std::cmp_less(i, num_gaps_); ++i) {
        if (core::at(gaps_, i) != gap)
            continue;
        for (int j = i; j < num_gaps_ - 1; ++j)
            core::at(gaps_, j) = core::at(gaps_, j + 1);
        --num_gaps_;
        return;
    }
}

// 0x004E6BF0
void EditedGoal::remove_gap_and_renumber(uint8_t gap)
{
    drop_gap_entry(gap);
    for (int i = 0; std::cmp_less(i, num_gaps_); ++i) {
        if (core::at(gaps_, i) > gap)
            --core::at(gaps_, i);
    }
}

// 0x004E7390
void EditedGoal::register_gap_entry(uint8_t gap)
{
    const uint8_t count = num_gaps_;
    for (int i = 0; std::cmp_less(i, num_gaps_); ++i) {
        if (core::at(gaps_, i) == gap)
            return;
    }
    if (count >= 10) {
        debug::printf("Warning! MAX_GAPS limit reached!\n");
        return;
    }
    core::at(gaps_, count) = gap;
    ++num_gaps_;
}

// 0x004E6C40
void EditedGoal::add_key_combo_set(int set, int must_be_flawless, int spin, int tap_total)
{
    int index = num_key_combo_sets_;
    bool found = false;
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        if (std::cmp_equal(core::at(key_combo_sets_, i).set, set)) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found)
        num_key_combo_sets_ = index + 1;
    KeyComboSet& entry = core::at(key_combo_sets_, index);
    entry.tap_total_ = static_cast<uint8_t>(tap_total);
    entry.must_be_flawless_ = static_cast<uint8_t>(must_be_flawless);
    entry.set = static_cast<uint8_t>(set);
    entry.spin = static_cast<uint8_t>(spin / 180);
}

// 0x004E6CD0
void EditedGoal::remove_key_combo_set(int set)
{
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        if (std::cmp_not_equal(core::at(key_combo_sets_, i).set, set))
            continue;
        for (int j = i; j < num_key_combo_sets_ - 1; ++j)
            core::at(key_combo_sets_, j) = core::at(key_combo_sets_, j + 1);
        --num_key_combo_sets_;
        return;
    }
}

// 0x004E6D50
KeyComboSet* EditedGoal::find_key_combo_set(int set)
{
    const int count = num_key_combo_sets_;
    for (int i = 0; i < count; ++i) {
        if (std::cmp_equal(core::at(key_combo_sets_, i).set, set))
            return &core::at(key_combo_sets_, i);
    }
    return nullptr;
}

// 0x004E6DA0
void EditedGoal::change_species(uint32_t type)
{
    kind_tag_ = type;
    core::at(path_vertices_, 0).kind = kPointPro;
    core::at(path_vertices_, 0).height = 0.0f;
    core::at(path_vertices_, 1).kind = kPointStart;
    core::at(path_vertices_, 1).height = 0.0f;
    script::Struct* defaults = goal_type_structure(type);
    switch (kind_tag_) {
    case kGoalTypeRace:
        min_points_ = 4;
        max_points_ = 12;
        for (int i = 2; i < 13; ++i) {
            core::at(path_vertices_, i).kind = kPointCheckpoint;
            core::at(path_vertices_, i).height = 0.0f;
        }
        break;
    case kGoalTypeCombo:
        min_points_ = 7;
        max_points_ = 7;
        for (int i = 2; std::cmp_less(i, min_points_); ++i) {
            core::at(path_vertices_, i).kind = kPointObject;
            core::at(path_vertices_, i).height = 0.0f;
        }
        break;
    case kGoalTypeSkate:
        min_points_ = 7;
        max_points_ = 7;
        for (int i = 2; i < 13; ++i) {
            core::at(path_vertices_, i).kind = kPointObject;
            core::at(path_vertices_, i).height = 0.0f;
        }
        break;
    case kGoalTypeComboTricks:
    case kGoalTypeGap:
    case kGoalTypeScore:
    case kGoalTypeTrickAttack:
    case kGoalTypeKeyCombo:
    case kGoalTypeTricktris:
    case kGoalTypeHighCombo:
        min_points_ = 2;
        max_points_ = 2;
        if (kind_tag_ == kGoalTypeKeyCombo)
            twist_rate_ = 0;
        break;
    default:
        break;
    }
    if (!defaults)
        return;
    defaults->fetch_int_value(0xC51BF6E3, &time_limit_, 0);
    defaults->fetch_int_value(0xCD66C8AE, &points_held_, 0);
    const char* text = ""; // ""
    defaults->fetch_text(0x4BC5229D, &text, 0);
    std::strcpy(label_.data(), text);
}

// 0x004E6F70
void EditedGoal::set_countdown_seconds(int seconds)
{
    time_limit_ = seconds;
    if (kind_tag_ == kGoalTypeComboTricks || kind_tag_ == kGoalTypeTrickAttack || kind_tag_ == kGoalTypeTricktris) {
        if (time_to_stop_adding_tricks_ > seconds - 5)
            time_to_stop_adding_tricks_ = seconds - 5;
    }
}

// 0x004E7050
void EditedGoal::place_goal_marker(int index)
{
    script::Struct params;
    switch (core::at(path_vertices_, index).kind) {
    case kPointPro:
        params.put_name_key(0x7321A8D6, 0x061A741E);
        break;
    case kPointStart:
        params.put_name_key(0x7321A8D6, 0x1806DDF8);
        params.append_word(0x286A8D26, "gameobjects\\p1_cursor\\p1_cursor.mdl");
        break;
    case kPointObject:
        params.put_name_key(0x7321A8D6, 0x71FD11F5);
        params.append_word(0x286A8D26, goal_object_model(kind_tag_, index - 2));
        params.put_name_key(0u, 0x3BD176D9u); // (a flag: no name)
        break;
    case kPointCheckpoint:
        params.put_name_key(0x7321A8D6, 0xF0FF0841);
        params.append_word(0x286A8D26, goal_object_model(kind_tag_, index - 2));
        break;
    default:
        break;
    }
    get_sprintf(name_buffer().data(), "GoalEditorMarkerObject%d", index);
    params.put_name_key(0xA1DC81F9, checksums::hash_from_text(name_buffer().data()));
    const EditedGoalPoint& point = core::at(path_vertices_, index);
    params.put_vec3(0x7F261953, point.pos.x, point.height + point.pos.y, point.pos.z);
    params.push_float(0xFF7EBAF6, point.angle * kDegreesPerRadian);
    script::launch_script(0xEFC08BAE, &params, nullptr, false, nullptr);
    params.get_clear();
}

// 0x004E7780
void EditedGoal::create_markers()
{
    for (int i = 1; std::cmp_less(i, vertex_total_); ++i)
        place_goal_marker(i);
    current_point_ = 0;
    won_ = false;
}

// 0x004E7210
bool EditedGoal::near_other_point(const maths::Vec4& pos)
{
    const float radius = script::fetch_float(0x71B20276);
    const float start_to_object_radius = script::fetch_float(0x3F6E9739);
    const int count = vertex_total_;
    const int current = current_point_;
    for (int i = 0; i < count; ++i) {
        if (i == current)
            continue;
        const EditedGoalPoint& point = core::at(path_vertices_, i);
        const int current_kind = core::at(path_vertices_, current).kind;
        double within = radius;
        if ((current_kind == kPointObject && point.kind == kPointStart) ||
            (current_kind == kPointStart && point.kind == kPointObject))
            within = start_to_object_radius;
        // (x's difference rounded to a float; y's and z's not, as the original's x87 sums)
        auto dx = static_cast<float>(static_cast<double>(point.pos.x) - pos.x);
        const double dy = (static_cast<double>(point.pos.y) + point.height) - pos.y;
        const double dz = static_cast<double>(point.pos.z) - pos.z;
        const double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
        if (distance < within)
            return true;
    }
    return false;
}

// 0x004E7300
bool EditedGoal::can_place_point(const maths::Vec4& pos, float height)
{
    if (current_level() == kParkEditorLevel && !ParkBuilderMode::is_inside_park(pos, 30.0f))
        return false;
    maths::Vec4 raised = pos;
    raised.y = pos.y + height;
    return !near_other_point(raised);
}

// 0x004E77C0
bool EditedGoal::place_point(const maths::Vec4& pos, float height, float angle)
{
    last_point_placed_ = false;
    if (!can_place_point(pos, height))
        return false;
    EditedGoalPoint& point = core::at(path_vertices_, current_point_);
    point.pos = pos;
    core::at(path_vertices_, current_point_).height = height;
    core::at(path_vertices_, current_point_).angle = angle;
    if (current_point_ == max_points_ - 1)
        last_point_placed_ = true;
    if (current_point_ == 0)
        script::launch_script(0xD066A889, nullptr, nullptr, false, nullptr);
    place_goal_marker(current_point_);
    ++current_point_;
    run_marker_object_script(current_point_);
    if (current_point_ < vertex_total_)
        return true;
    vertex_total_ = current_point_;
    if (current_point_ == max_points_) {
        current_point_ = static_cast<uint8_t>(max_points_ - 1);
        return true;
    }
    // The next point starts where this one is.
    core::at(path_vertices_, current_point_).pos = pos;
    core::at(path_vertices_, current_point_).height = height;
    core::at(path_vertices_, current_point_).angle = angle;
    if ((kind_tag_ == kGoalTypeCombo || kind_tag_ == kGoalTypeSkate) && current_point_ == 2) {
        float& next = core::at(path_vertices_, 2).height;
        next = static_cast<float>(static_cast<double>(script::fetch_float(0x71EC75E9)) + next);
    }
    return true;
}

// 0x004E7960
bool EditedGoal::get_current_point(maths::Vec4* pos, float* height, float* angle)
{
    *pos = core::at(path_vertices_, current_point_).pos;
    *height = core::at(path_vertices_, current_point_).height;
    *angle = core::at(path_vertices_, current_point_).angle;
    return true;
}

// 0x004E79C0
void EditedGoal::write_node_position(int index, uint32_t node_name)
{
    script::Struct* node = level_nodes::fetch_vertex(node_name, true);
    const EditedGoalPoint& point = core::at(path_vertices_, index);
    node->put_vec3(0x7F261953, point.pos.x, point.height + point.pos.y, point.pos.z);
    node->put_vec3(0x9D2D0915, 0.0f, point.angle - kPi, 0.0f);
    if (static_cast<uint32_t>(index) >= 2) {
        node->put_name_key(0x7321A8D6, goal_object_name(kind_tag_, index - 2));
        node->append_word(0x286A8D26, goal_object_model(kind_tag_, index - 2));
    }
}

// 0x004E7A80
void EditedGoal::emit_node_coords()
{
    std::array<char, 100> name{};
    for (int i = 0; std::cmp_less(i, vertex_total_); ++i) {
        core::at(name, 0) = '\0';
        if (i == 0)
            get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", level_goal_number_);
        else if (i == 1)
            get_sprintf(name.data(), "TRG_EditedGoal%d_Restart", level_goal_number_);
        else
            get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", level_goal_number_, i - 2);
        write_node_position(i, checksums::hash_from_text(name.data()));
    }
}

// 0x004E73F0
void EditedGoal::read_key_combo_sets(script::Struct* params)
{
    script::Array* sets = nullptr;
    if (!params->fetch_list(0x0490F171, &sets, 0))
        return;
    num_key_combo_sets_ = static_cast<int>(sets->extent_);
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        auto* entry = sets->struct_at(static_cast<uint32_t>(i));
        KeyComboSet& set = core::at(key_combo_sets_, i);
        int value = 0;
        entry->fetch_int_value(0xEB7D6498, &value, 0);
        set.set = static_cast<uint8_t>(value);
        value = 0;
        entry->fetch_int_value(0xEDF5DB70, &value, 0);
        set.spin = static_cast<uint8_t>(value / 180);
        value = 0;
        entry->fetch_int_value(0xA4BEE6A1, &value, 0);
        set.tap_total_ = static_cast<uint8_t>(value);
        set.must_be_flawless_ = entry->has_bare_flag(0x687E2C25);
    }
}

// 0x004E74F0
void EditedGoal::read_trick_settings(script::Struct* params)
{
    params->fetch_int_value(0x01181B29, &acceleration_interval_, 0);
    params->fetch_float(0x076F65C8, &acceleration_percent_, 0);
    params->fetch_int_value(0xDED8540A, &trick_time_, 0);
    params->fetch_int_value(0xA6D96603, &time_to_stop_adding_tricks_, 0);
    params->fetch_int_value(0x89473DB7, &max_tricks_, 0);
    params->fetch_int_value(0xEDF5DB70, &twist_rate_, 0);
    read_key_combo_sets(params);
}

// 0x004E7590
void EditedGoal::read_specific_params(script::Struct* params)
{
    switch (kind_tag_) {
    case kGoalTypeRace:
        params->fetch_int_value(0x1DAAE95B, &race_total_laps_, 0);
        params->fetch_int_value(0xD9426372, &race_per_checkpoint_time_, 0);
        break;
    case kGoalTypeComboTricks:
        params->fetch_int_value(0x67DD90CA, &chain_length_, 0);
        if (params->has_bare_flag(0xDF47B144))
            single_combo_ = true;
        read_trick_settings(params);
        break;
    case kGoalTypeGap: {
        const char* trick = nullptr;
        if (params->fetch_text(0x730844A3, &trick, 0))
            std::strcpy(required_trick_.data(), trick);
        script::Array* gaps = nullptr;
        params->fetch_list(0xD76C173E, &gaps, 0);
        if (!gaps)
            break;
        num_gaps_ = static_cast<uint8_t>(gaps->extent_);
        for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
            core::at(gaps_, i) = static_cast<uint8_t>(gaps->fetch_part(static_cast<uint32_t>(i)));
        break;
    }
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        params->fetch_int_value(0xCD66C8AE, &points_held_, 0);
        break;
    case kGoalTypeTrickAttack:
        read_trick_settings(params);
        break;
    case kGoalTypeKeyCombo:
        read_key_combo_sets(params);
        break;
    case kGoalTypeTricktris:
        params->fetch_int_value(0x45A375C9, &stack_block_span_, 0);
        params->fetch_int_value(0xBFD3A831, &tricktris_total_to_win_, 0);
        read_trick_settings(params);
        break;
    default:
        break;
    }
}

} // namespace park_editing
