// An edited goal to and from script structures, and its points dropped to the ground (park_editing::EditedGoal): see
// goal_editor.h. From the disassembly of 0x004E93D0-0x004EB7E2.

#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/render/probe_ray.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "platform/compiler.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace park_editing {

char g_created_goal_name[24];
namespace {
char g_goal_io_name_buffer[100];

constexpr float kDropHeight = 10000.0f; // 0x0064B308
constexpr uint32_t kStructureArray = 10, kChecksumArray = 13, kIntegerArray = 1;

template <typename... Args>
int get_sprintf(char* out, const char* format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

char* created_goal_name() { return g_created_goal_name; } // the shared CreatedGoal<n> buffer

script::Struct* spawn_param_table()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
script::Array* new_array_of(uint32_t size, uint32_t type)
{
    script::Array* array = script::alloc_script_array();
    array = array ? new (array) script::Array : nullptr;
    array->configure_size_kind(size, static_cast<script::QbValueKind>(type));
    return array;
}
script::Struct* structure_element(script::Array* array, uint32_t index)
{
    return reinterpret_cast<script::Struct*>(array->fetch_part(index));
}

} // namespace

// 0x004E93D0
void EditedGoal::drop_point_to_ground(int index)
{
    maths::Vec4& pos = path_vertices_[index].pos;
    maths::Vec4 start = {pos.x, pos.y + kDropHeight, pos.z, pos.w};
    const maths::Vec4 end = {pos.x, pos.y - kDropHeight, pos.z, pos.w};
    render::ProbeRay feeler;
    feeler.change_origin(&start);
    feeler.change_finish(&end);
    if (!feeler.get_hit_shape(true, false)) {
        // Missed: again from higher up.
        start.y = start.y + kDropHeight;
        feeler.change_origin(&start);
        feeler.change_finish(&end);
        if (!feeler.get_hit_shape(true, false))
            return;
    }
    pos = feeler.contact_pos_;
}

// 0x004E9590
void EditedGoal::place_points_on_ground()
{
    char* name = g_goal_io_name_buffer;
    for (int i = 0; std::cmp_less(i, vertex_total_); ++i) {
        drop_point_to_ground(i);
        script::Struct params;
        uint32_t node = 0;
        if (i == 0) {
            get_sprintf(name, "TRG_EditedGoal%d_Pro", level_goal_number_);
            node = checksums::hash_from_text(name);
        } else if (i > 1) {
            get_sprintf(name, "TRG_EditedGoal%d_Object_%d", level_goal_number_, i - 2);
            node = checksums::hash_from_text(name);
        }
        params.put_name_key("Name", node);
        const EditedGoalPoint& point = path_vertices_[i];
        params.put_vec3("Pos", point.pos.x, point.height + point.pos.y, point.pos.z);
        script::launch_script(0x79582240, &params, nullptr, false, nullptr);
        params.get_clear();
    }
}

// 0x004EB550
bool EditedGoal::place_point_and_step_back(const maths::Vec4& pos, float height, float angle)
{
    if (current_point_ == 0 || !can_place_point(pos, height))
        return false;
    path_vertices_[current_point_].pos = pos;
    path_vertices_[current_point_].height = height;
    path_vertices_[current_point_].angle = angle;
    place_goal_marker(current_point_);
    if (current_point_ == vertex_total_)
        ++vertex_total_;
    --current_point_;
    run_marker_object_script(current_point_);
    if (current_point_ == 0) {
        script::Struct* params = script::spawn_param_bag();
        params = params ? new (params) script::Struct : nullptr;
        params->put_name_key(0u, 0x7F1538E0u);
        script::launch_script(0xD066A889, params, nullptr, false, nullptr);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    return true;
}

// 0x004E96D0
void EditedGoal::decode_from_struct(script::Struct* params)
{
    init_edited_goal(reinterpret_cast<uint8_t*>(this));
    taken_count_ = true;
    won_ = false;
    uint32_t level = 0;
    params->read_hash_value(0x651533EC, &level, 0);
    if (level == 0xB664035D) // (Load_Sk5Ed_gameplay counts as the park editor's)
        level = kParkEditorLevel;
    level_ = level;
    params->fetch_int_value(0x60CE75AE, &level_goal_number_, 0);
    params->read_hash_value(0x6D11ED74, &kind_tag_, 0);
    change_species(kind_tag_);
    params->fetch_int_value(0xCD66C8AE, &points_held_, 0);
    params->fetch_int_value(0xC51BF6E3, &time_limit_, 0);
    params->read_hash_value(0x81CFF663, &control_type_, 0);
    const char* empty = ""; // ""
    const char* text = empty;
    params->fetch_text(0x4BC5229D, &text, 0);
    std::strcpy(label_, text);
    text = empty;
    params->fetch_text(0x0C5D7E6B, &text, 0);
    std::strcpy(summary_text_, text);
    text = empty;
    params->fetch_text(0x5DE57284, &text, 0);
    std::strcpy(win_message_, text);
    text = empty;
    params->fetch_text(0xB9426608, &text, 0);
    std::strcpy(ped_name_, text);
    script::Array* points = nullptr;
    if (params->fetch_list(0x32DF12E0, &points, 0)) {
        vertex_total_ = static_cast<uint8_t>(points->extent_);
        for (uint32_t i = 0; i < vertex_total_; ++i) {
            script::Struct* point = structure_element(points, i);
            point->fetch_vec(0x7F261953, reinterpret_cast<script::VectorValue*>(&path_vertices_[i].pos), 0);
            point->fetch_float(0x0AB21AF0, &path_vertices_[i].height, 0);
            point->fetch_float(0xFF7EBAF6, &path_vertices_[i].angle, 0);
        }
    }
    read_specific_params(params);
}

// 0x004E9BC0
void EditedGoal::write_key_combo_sets(script::Struct* out)
{
    if (num_key_combo_sets_ == 0)
        return;
    script::Array* sets = new_array_of(static_cast<uint32_t>(num_key_combo_sets_), kStructureArray);
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        const KeyComboSet& set = key_combo_sets_[i];
        script::Struct* entry = spawn_param_table();
        entry->put_int(0xEB7D6498, set.set);
        if (set.must_be_flawless_)
            entry->put_name_key(0u, 0x687E2C25u);
        entry->put_int(0xEDF5DB70, set.spin * 180);
        entry->put_int(0xA4BEE6A1, set.tap_total_);
        sets->set_element(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    out->append_array_ref(0x0490F171, sets);
}

// 0x004E98F0
void EditedGoal::write_goal_key_combos(script::Struct* out)
{
    if (num_key_combo_sets_ == 0) {
        debug::printf("!!!!!!!! Warning! No combo sets specified, using emergency_key_combos\n");
        out->put_name_key(0x7BE1E689, 0x6D641BCB);
        return;
    }
    script::Array* all_sets = script::fetch_list(0xDF1451EB);
    // How many key combos the sets hold (a set without its array: a null read, as the original).
    uint32_t total = 0;
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        script::Array* combos = nullptr;
        structure_element(all_sets, key_combo_sets_[i].set)->fetch_list(0x79704516, &combos, 0);
        total += combos->extent_;
    }
    script::Array* result = new_array_of(total, kStructureArray);
    uint32_t index = 0;
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        const KeyComboSet& set = key_combo_sets_[i];
        script::Array* combos = nullptr;
        structure_element(all_sets, set.set)->fetch_list(0x79704516, &combos, 0);
        for (uint32_t j = 0; j < combos->extent_; ++j) {
            script::Struct* entry = spawn_param_table();
            if (combos->kind_tag_ == kChecksumArray)
                entry->put_name_key(0xACFDB27A, combos->fetch_part(j));
            else
                entry->append_structure_if_any(structure_element(combos, j));
            if (set.tap_total_)
                entry->put_int(0xA4BEE6A1, set.tap_total_);
            if (set.must_be_flawless_)
                entry->put_name_key(0u, 0x687E2C25u);
            if (twist_rate_)
                entry->put_int(0x8C8ABD19, twist_rate_);
            result->set_element(index++, reinterpret_cast<uint32_t>(entry));
        }
    }
    out->append_array_ref(kind_tag_ == kGoalTypeKeyCombo ? 0x548B4A38 : 0x7BE1E689, result);
}

// 0x004E9D60
void EditedGoal::write_trick_settings(script::Struct* out, int for_goal_manager)
{
    out->put_int(0x01181B29, acceleration_interval_);
    out->push_float(0x076F65C8, acceleration_percent_);
    out->put_int(0xDED8540A, trick_time_);
    out->put_int(0xA6D96603, time_to_stop_adding_tricks_);
    out->put_int(0x89473DB7, max_tricks_);
    if (for_goal_manager == 1) {
        write_goal_key_combos(out);
        return;
    }
    out->put_int(0xEDF5DB70, twist_rate_);
    write_key_combo_sets(out);
}

// 0x004E9E00
void EditedGoal::write_specific_params(script::Struct* out, int for_goal_manager)
{
    char text[0x80];
    switch (kind_tag_) {
    case kGoalTypeRace:
        if (for_goal_manager == 1) {
            if (race_per_checkpoint_time_ > 0)
                out->put_int(0xA4038EC9, race_per_checkpoint_time_);
        } else {
            out->put_int(0xD9426372, race_per_checkpoint_time_);
        }
        out->put_int(0x1DAAE95B, race_total_laps_);
        break;
    case kGoalTypeComboTricks:
        out->put_name_key(0u, static_cast<uint32_t>(kGoalTypeCombo));
        out->put_int(0x67DD90CA, chain_length_);
        if (single_combo_) {
            out->put_name_key(0u, 0xDF47B144u);
            max_tricks_ = chain_length_;
        }
        write_trick_settings(out, for_goal_manager);
        break;
    case kGoalTypeGap:
        out->append_word(0x730844A3, required_trick_);
        if (for_goal_manager == 1) {
            script::Array* flags = new_array_of(num_gaps_, kChecksumArray);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i) {
                get_sprintf(text, "got_%d", i + 1);
                flags->set_element(static_cast<uint32_t>(i), checksums::hash_from_text(text));
            }
            out->append_array_ref(0xCC3C4CC4, flags);
            script::Array* gaps = new_array_of(num_gaps_, kIntegerArray);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
                gaps->set_element(static_cast<uint32_t>(i), gaps_[i]);
            out->append_array_ref(0x52D4489E, gaps);
        } else if (num_gaps_) {
            script::Array* gaps = new_array_of(num_gaps_, kIntegerArray);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
                gaps->set_element(static_cast<uint32_t>(i), gaps_[i]);
            out->append_array_ref(0xD76C173E, gaps);
        }
        break;
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        out->put_int(0xCD66C8AE, points_held_);
        if (for_goal_manager == 1) {
            const char* format = script::fetch_text(kind_tag_ == kGoalTypeHighCombo ? 0x3693BFE4 : 0xD6EFB1B5);
            get_sprintf(text, format, points_held_);
            out->append_word(0xDA441D9A, text);
        }
        break;
    case kGoalTypeTrickAttack:
        write_trick_settings(out, for_goal_manager);
        break;
    case kGoalTypeKeyCombo:
        if (for_goal_manager == 1)
            write_goal_key_combos(out);
        else
            write_key_combo_sets(out);
        break;
    case kGoalTypeTricktris:
        out->put_name_key(0u, static_cast<uint32_t>(kGoalTypeTricktris));
        out->put_int(0x45A375C9, stack_block_span_);
        out->put_int(0xBFD3A831, tricktris_total_to_win_);
        max_tricks_ = stack_block_span_;
        write_trick_settings(out, for_goal_manager);
        break;
    default:
        break;
    }
}

// 0x004EA850
void EditedGoal::write_goal_manager_params(script::Struct* out)
{
    char name[0x64];
    out->append_structure_if_any(goal_type_structure(kind_tag_));
    out->append_structure_if_any(goal_type_defaults(kind_tag_));
    name[0] = '\0';
    get_sprintf(name, "TRG_EditedGoal%d_Pro", level_goal_number_);
    out->put_name_key(0x02D7E03D, checksums::hash_from_text(name));
    name[0] = '\0';
    get_sprintf(name, "TRG_EditedGoal%d_Restart", level_goal_number_);
    out->put_name_key(0x3494F170, checksums::hash_from_text(name));
    out->put_name_key(0xA302D7CD, 1u);
    out->put_name_key(0u, 0xDBEB46E2u);
    switch (kind_tag_) {
    case kGoalTypeSkate: {
        static const uint32_t kLetters[5] = {0xBA54BD5C, 0x150A97C2, 0x84CA8B0A, 0xB091B445, 0x008085F0};
        for (int i = 0; i < 5; ++i) {
            name[0] = '\0';
            get_sprintf(name, "TRG_EditedGoal%d_Object_%d", level_goal_number_, i);
            out->put_name_key(kLetters[i], checksums::hash_from_text(name));
        }
        break;
    }
    case kGoalTypeCombo: {
        static const char* const kLetters[5] = {"C", "O", "M", "B", "O"};
        script::Array* letters = new_array_of(5, kStructureArray);
        for (int i = 0; i < 5; ++i) {
            script::Struct* letter = spawn_param_table();
            name[0] = '\0';
            get_sprintf(name, "TRG_EditedGoal%d_Object_%d", level_goal_number_, i);
            letter->put_name_key(0x99F6CCBB, checksums::hash_from_text(name));
            letter->append_word(0xC4745838, kLetters[i]);
            letters->set_element(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(letter));
        }
        out->append_array_ref(0xB3C7F1B2, letters);
        break;
    }
    case kGoalTypeRace: {
        const int count = vertex_total_ - 2;
        script::Array* waypoints = new_array_of(static_cast<uint32_t>(count), kStructureArray);
        script::Array* flags = new_array_of(static_cast<uint32_t>(count), kChecksumArray);
        char flag[0x64];
        for (int i = 0; i < count; ++i) {
            script::Struct* waypoint = spawn_param_table();
            waypoint->put_name_key(0x40C698AF, edited_goal_node_name(i + 2, level_goal_number_));
            get_sprintf(flag, "got_%d", i + 1);
            flags->set_element(static_cast<uint32_t>(i), checksums::hash_from_text(flag));
            waypoint->put_name_key(0x2E0B1465, checksums::hash_from_text(flag));
            if (race_per_checkpoint_time_ > 0)
                waypoint->put_int(0x906B67BA, race_per_checkpoint_time_);
            waypoint->put_int(0xC48391A5, 5);
            waypoints->set_element(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(waypoint));
        }
        out->append_array_ref(0xA77B1F07, waypoints);
        out->append_array_ref(0xCC3C4CC4, flags);
        break;
    }
    default:
        break;
    }
    write_specific_params(out, 1);
    if (summary_text_[0])
        out->append_word(0x0C5D7E6B, summary_text_);
    if (win_message_[0]) {
        // The win message's dialog: a copy of the global template array with the text and the pro's node.
        script::Array* source = script::fetch_list(0x8070C110);
        script::Array* dialog = script::alloc_script_array();
        dialog = dialog ? new (dialog) script::Array : nullptr;
        script::duplicate_array(dialog, source);
        structure_element(dialog, 0)->append_word(0xA7AB3B6D, win_message_);
        name[0] = '\0';
        get_sprintf(name, "TRG_EditedGoal%d_Pro", level_goal_number_);
        const uint32_t pro = checksums::hash_from_text(name);
        structure_element(dialog, 0)->put_name_key(0x531E4D28, pro);
        out->append_array_ref(0x27CD32E1, dialog);
    }
    get_sprintf(created_goal_name(), "CreatedGoal%d", level_goal_number_);
    out->append_word(0xBFECC45B, created_goal_name());
    if (label_[0])
        out->append_word(0x4BC5229D, label_);
    if (ped_name_[0])
        out->append_word(0x243B9C3B, ped_name_);
    out->put_int(0x906B67BA, time_limit_);
    out->put_name_key(0x81CFF663, control_type_);
    out->put_name_key(0u, 0x981D3AD0u);
}

// 0x004EB260
void EditedGoal::export_to_struct(script::Struct* out)
{
    char name[0x64];
    out->put_name_key(0u, 0x981D3AD0u);
    out->put_name_key(0x651533EC, level_);
    out->put_int(0x60CE75AE, level_goal_number_);
    get_sprintf(created_goal_name(), "CreatedGoal%d", level_goal_number_);
    out->put_name_key(0x9982E501, checksums::hash_from_text(created_goal_name()));
    out->put_int(0xC51BF6E3, time_limit_);
    out->put_int(0x14938611, won_);
    out->put_name_key(0x81CFF663, control_type_);
    out->append_word(0x4BC5229D, label_);
    out->append_word(0x0C5D7E6B, summary_text_);
    out->append_word(0x5DE57284, win_message_);
    out->append_word(0xB9426608, ped_name_);
    out->put_name_key(0x6D11ED74, kind_tag_);
    name[0] = '\0';
    get_sprintf(name, "TRG_EditedGoal%d_Pro", level_goal_number_);
    out->put_name_key(0x71FC348B, checksums::hash_from_text(name));
    write_specific_params(out, 0);
    if (!vertex_total_)
        return;
    script::Array* points = new_array_of(vertex_total_, kStructureArray);
    for (uint32_t i = 0; i < vertex_total_; ++i) {
        const EditedGoalPoint& point = path_vertices_[i];
        script::Struct* entry = spawn_param_table();
        entry->put_vec3(0x7F261953, point.pos.x, point.pos.y, point.pos.z);
        entry->push_float(0x0AB21AF0, point.height);
        entry->push_float(0xFF7EBAF6, point.angle);
        points->set_element(i, reinterpret_cast<uint32_t>(entry));
    }
    out->append_array_ref(0x32DF12E0, points);
}

// 0x004EB6B0
void EditedGoal::add_to_goal_manager(bool unwin)
{
    script::Struct* params = spawn_param_table();
    write_goal_manager_params(params);
    goals::GoalManager* manager = goals::get_objective_board();
    get_sprintf(created_goal_name(), "CreatedGoal%d", level_goal_number_);
    manager->enrol_objective(checksums::hash_from_text(created_goal_name()), params);
    get_sprintf(created_goal_name(), "CreatedGoal%d", level_goal_number_);
    goals::Objective* goal = manager->get_goal(checksums::hash_from_text(created_goal_name()), 1);
    goal->mark_viewed();
    if (unwin)
        won_ = false;
    if (won_)
        goal->mark_won();
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace park_editing
