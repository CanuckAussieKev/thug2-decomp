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
#include "game/park_editing/park_editing_state.h"
#include "engine/maths/vec4.h"
#include "engine/maths/trig.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace park_editing {

namespace {

constexpr std::uint32_t kAccelerationInterval = 0x01181B29u; // acceleration_interval
constexpr std::uint32_t kAccelerationPercent = 0x076F65C8u; // acceleration_percent
constexpr std::uint32_t kAngle = 0xFF7EBAF6u; // Angle
constexpr std::uint32_t kAngles = 0x9D2D0915u; // Angles
constexpr std::uint32_t kCheckpoint = 0xF0FF0841u; // Checkpoint
constexpr std::uint32_t kComboSets = 0x0490F171u; // combo_sets
constexpr std::uint32_t kComboSize = 0x67DD90CAu; // combo_size
constexpr std::uint32_t kCreateCagHelperText = 0xD066A889u; // create_cag_helper_text
constexpr std::uint32_t kGaps = 0xD76C173Eu; // Gaps
constexpr std::uint32_t kGoalEditorCreateMarkerObject = 0xEFC08BAEu; // goal_editor_create_marker_object
constexpr std::uint32_t kGoalEditorObjectHeight = 0x71EC75E9u; // GoalEditor_ObjectHeight
constexpr std::uint32_t kLetter = 0x71FD11F5u; // Letter
constexpr std::uint32_t kMaxTricks = 0x89473DB7u; // max_tricks
constexpr std::uint32_t kModel = 0x286A8D26u; // Model
constexpr std::uint32_t kName = 0xA1DC81F9u; // Name
constexpr std::uint32_t kNumTaps = 0xA4BEE6A1u; // num_taps
constexpr std::uint32_t kPed = 0x061A741Eu; // Ped
constexpr std::uint32_t kPos = 0x7F261953u; // Pos
constexpr std::uint32_t kRacePerCheckpointTime = 0xD9426372u; // race_per_checkpoint_time
constexpr std::uint32_t kRaceTotalLaps = 0x1DAAE95Bu; // race_total_laps
constexpr std::uint32_t kRequirePerfect = 0x687E2C25u; // Require_Perfect
constexpr std::uint32_t kRequiredTrickName = 0x730844A3u; // required_trick_name
constexpr std::uint32_t kRestart = 0x1806DDF8u; // Restart
constexpr std::uint32_t kScore = 0xCD66C8AEu; // Score
constexpr std::uint32_t kSetIndex = 0xEB7D6498u; // set_index
constexpr std::uint32_t kSingleCombo = 0xDF47B144u; // single_combo
constexpr std::uint32_t kSpin = 0xEDF5DB70u; // Spin
constexpr std::uint32_t kTimeLimit = 0xC51BF6E3u; // time_limit
constexpr std::uint32_t kTimeToStopAddingTricks = 0xA6D96603u; // time_to_stop_adding_tricks
constexpr std::uint32_t kTrickTime = 0xDED8540Au; // trick_time
constexpr std::uint32_t kTricktrisBlockSize = 0x45A375C9u; // tricktris_block_size
constexpr std::uint32_t kTricktrisTotalToWin = 0xBFD3A831u; // tricktris_total_to_win
constexpr std::uint32_t kType = 0x7321A8D6u; // Type
constexpr std::uint32_t kViewGoalsText = 0x4BC5229Du; // view_goals_text
constexpr int kRaceMaxPoints = 12;
constexpr int kComboPointCount = 7;
constexpr int kSkatePointCount = 7;
constexpr int kTrickCutoffSeconds = 5;        // trick adding stops this long before the time is up
constexpr std::uint32_t kLetterObjectFlag = 0x3BD176D9u; // a bare flag on a letter's object (text unknown)
constexpr float kPlacementMargin = 30.0f;     // how far from the park's edge a point may be placed

constexpr float kDegreesPerRadian = 57.2957763671875f; // 0x00645468

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

// The goal editor's shared name buffer (also GoalEditorMarkerObject<n>'s).

uint32_t current_level() { return modules::Skate::sole()->level(); }

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
    if (count >= kMaxGaps) {
        debug::printf("Warning! MAX_GAPS limit reached!\n");
        return;
    }
    core::at(gaps_, count) = gap;
    ++num_gaps_;
}

// 0x004E6C40
void EditedGoal::add_key_combo_set(ComboSetSpec combo_set)
{
    const auto set = combo_set.set;
    const auto must_be_flawless = combo_set.must_be_flawless_;
    const auto spin = combo_set.spin;
    const auto tap_total = combo_set.tap_total_;
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
    entry.spin = static_cast<uint8_t>(spin / kSpinUnitDegrees);
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
        max_points_ = kRaceMaxPoints;
        for (int i = 2; i < kPathVertexCount; ++i) {
            core::at(path_vertices_, i).kind = kPointCheckpoint;
            core::at(path_vertices_, i).height = 0.0f;
        }
        break;
    case kGoalTypeCombo:
        min_points_ = kComboPointCount;
        max_points_ = kComboPointCount;
        for (int i = 2; std::cmp_less(i, min_points_); ++i) {
            core::at(path_vertices_, i).kind = kPointObject;
            core::at(path_vertices_, i).height = 0.0f;
        }
        break;
    case kGoalTypeSkate:
        min_points_ = kSkatePointCount;
        max_points_ = kSkatePointCount;
        for (int i = 2; i < kPathVertexCount; ++i) {
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
    defaults->fetch_int_value(kTimeLimit, &time_limit_, 0);
    defaults->fetch_int_value(kScore, &points_held_, 0);
    const char* text = ""; // ""
    defaults->fetch_text(kViewGoalsText, &text, 0);
    std::strcpy(label_.data(), text);
}

// 0x004E6F70
void EditedGoal::set_countdown_seconds(int seconds)
{
    time_limit_ = seconds;
    if (kind_tag_ == kGoalTypeComboTricks || kind_tag_ == kGoalTypeTrickAttack || kind_tag_ == kGoalTypeTricktris) {
        if (time_to_stop_adding_tricks_ > seconds - kTrickCutoffSeconds)
            time_to_stop_adding_tricks_ = seconds - kTrickCutoffSeconds;
    }
}

// 0x004E7050
void EditedGoal::place_goal_marker(int index)
{
    script::Struct params;
    switch (core::at(path_vertices_, index).kind) {
    case kPointPro:
        params.put_name_key(checksums::Name{kType}, kPed);
        break;
    case kPointStart:
        params.put_name_key(checksums::Name{kType}, kRestart);
        params.append_word(kModel, "gameobjects\\p1_cursor\\p1_cursor.mdl");
        break;
    case kPointObject:
        params.put_name_key(checksums::Name{kType}, kLetter);
        params.append_word(kModel, goal_object_model(checksums::Name{kind_tag_}, index - 2));
        params.put_name_key(checksums::Name{0u}, kLetterObjectFlag); // (a flag: no name)
        break;
    case kPointCheckpoint:
        params.put_name_key(checksums::Name{kType}, kCheckpoint);
        params.append_word(kModel, goal_object_model(checksums::Name{kind_tag_}, index - 2));
        break;
    default:
        break;
    }
    get_sprintf(park_editing_state().goal_name_buffer().data(), "GoalEditorMarkerObject%d", index);
    params.put_name_key(checksums::Name{kName}, checksums::hash_from_text(park_editing_state().goal_name_buffer().data()));
    const EditedGoalPoint& point = core::at(path_vertices_, index);
    params.put_vec3(checksums::Name{kPos}, {.x = point.pos.x, .y = point.height + point.pos.y, .z = point.pos.z});
    params.push_float(checksums::Name{kAngle}, point.angle * kDegreesPerRadian);
    script::launch_script(kGoalEditorCreateMarkerObject, &params, nullptr, false, nullptr);
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
    if (current_level() == kParkEditorLevel && !ParkBuilderMode::is_inside_park(pos, kPlacementMargin))
        return false;
    maths::Vec4 raised = pos;
    raised.y = pos.y + height;
    return !near_other_point(raised);
}

// 0x004E77C0
bool EditedGoal::place_point(const maths::Vec4& pos, PointPose point_pose)
{
    const auto height = point_pose.height;
    const auto angle = point_pose.angle;
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
        script::launch_script(kCreateCagHelperText, nullptr, nullptr, false, nullptr);
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
        next = static_cast<float>(static_cast<double>(script::fetch_float(kGoalEditorObjectHeight)) + next);
    }
    return true;
}

// 0x004E7960
bool EditedGoal::get_current_point(maths::Vec4* pos, PointPoseOut pose_out)
{
    const auto height = pose_out.height;
    const auto angle = pose_out.angle;
    *pos = core::at(path_vertices_, current_point_).pos;
    *height = core::at(path_vertices_, current_point_).height;
    *angle = core::at(path_vertices_, current_point_).angle;
    return true;
}

// 0x004E79C0
void EditedGoal::write_node_position(int index, checksums::Name node_label)
{
    const uint32_t node_name = node_label.value();
    script::Struct* node = level_nodes::fetch_vertex(node_name, true);
    const EditedGoalPoint& point = core::at(path_vertices_, index);
    node->put_vec3(checksums::Name{kPos}, {.x = point.pos.x, .y = point.height + point.pos.y, .z = point.pos.z});
    node->put_vec3(checksums::Name{kAngles}, {.x = 0.0f, .y = point.angle - maths::kPi, .z = 0.0f});
    if (static_cast<uint32_t>(index) >= 2) {
        node->put_name_key(checksums::Name{kType}, goal_object_name(checksums::Name{kind_tag_}, index - 2));
        node->append_word(kModel, goal_object_model(checksums::Name{kind_tag_}, index - 2));
    }
}

// 0x004E7A80
void EditedGoal::emit_node_coords()
{
    std::array<char, kNodeNameSize> name{};
    for (int i = 0; std::cmp_less(i, vertex_total_); ++i) {
        core::at(name, 0) = '\0';
        if (i == 0)
            get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", level_goal_number_);
        else if (i == 1)
            get_sprintf(name.data(), "TRG_EditedGoal%d_Restart", level_goal_number_);
        else
            get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", level_goal_number_, i - 2);
        write_node_position(i, checksums::Name{checksums::hash_from_text(name.data())});
    }
}

// 0x004E73F0
void EditedGoal::read_key_combo_sets(script::Struct* params)
{
    script::Array* sets = nullptr;
    if (!params->fetch_list(kComboSets, &sets, 0))
        return;
    num_key_combo_sets_ = static_cast<int>(sets->extent());
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        auto* entry = sets->struct_at(static_cast<uint32_t>(i));
        KeyComboSet& set = core::at(key_combo_sets_, i);
        int value = 0;
        entry->fetch_int_value(kSetIndex, &value, 0);
        set.set = static_cast<uint8_t>(value);
        value = 0;
        entry->fetch_int_value(kSpin, &value, 0);
        set.spin = static_cast<uint8_t>(value / kSpinUnitDegrees);
        value = 0;
        entry->fetch_int_value(kNumTaps, &value, 0);
        set.tap_total_ = static_cast<uint8_t>(value);
        set.must_be_flawless_ = entry->has_bare_flag(kRequirePerfect);
    }
}

// 0x004E74F0
void EditedGoal::read_trick_settings(script::Struct* params)
{
    params->fetch_int_value(kAccelerationInterval, &acceleration_interval_, 0);
    params->fetch_float(kAccelerationPercent, &acceleration_percent_, 0);
    params->fetch_int_value(kTrickTime, &trick_time_, 0);
    params->fetch_int_value(kTimeToStopAddingTricks, &time_to_stop_adding_tricks_, 0);
    params->fetch_int_value(kMaxTricks, &max_tricks_, 0);
    params->fetch_int_value(kSpin, &twist_rate_, 0);
    read_key_combo_sets(params);
}

// 0x004E7590
void EditedGoal::read_specific_params(script::Struct* params)
{
    switch (kind_tag_) {
    case kGoalTypeRace:
        params->fetch_int_value(kRaceTotalLaps, &race_total_laps_, 0);
        params->fetch_int_value(kRacePerCheckpointTime, &race_per_checkpoint_time_, 0);
        break;
    case kGoalTypeComboTricks:
        params->fetch_int_value(kComboSize, &chain_length_, 0);
        if (params->has_bare_flag(kSingleCombo))
            single_combo_ = true;
        read_trick_settings(params);
        break;
    case kGoalTypeGap: {
        const char* trick = nullptr;
        if (params->fetch_text(kRequiredTrickName, &trick, 0))
            std::strcpy(required_trick_.data(), trick);
        script::Array* gaps = nullptr;
        params->fetch_list(kGaps, &gaps, 0);
        if (!gaps)
            break;
        num_gaps_ = static_cast<uint8_t>(gaps->extent());
        for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
            core::at(gaps_, i) = static_cast<uint8_t>(gaps->fetch_part(static_cast<uint32_t>(i)));
        break;
    }
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        params->fetch_int_value(kScore, &points_held_, 0);
        break;
    case kGoalTypeTrickAttack:
        read_trick_settings(params);
        break;
    case kGoalTypeKeyCombo:
        read_key_combo_sets(params);
        break;
    case kGoalTypeTricktris:
        params->fetch_int_value(kTricktrisBlockSize, &stack_block_span_, 0);
        params->fetch_int_value(kTricktrisTotalToWin, &tricktris_total_to_win_, 0);
        read_trick_settings(params);
        break;
    default:
        break;
    }
}

} // namespace park_editing
