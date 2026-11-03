// An edited goal to and from script structures, and its points dropped to the ground (park_editing::EditedGoal): see
// goal_editor.h. From the disassembly of 0x004E93D0-0x004EB7E2.

#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/format.h"
#include "engine/maths/vec4.h"
#include "engine/script/cell.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/render/probe_ray.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/str.h"
#include "game/park_editing/park_editing_state.h"
#include <gsl/pointers>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace park_editing {

namespace {

constexpr std::uint32_t kAccelerationInterval = 0x01181B29U;                // acceleration_interval
constexpr std::uint32_t kAccelerationPercent = 0x076F65C8U;                 // acceleration_percent
constexpr std::uint32_t kAngle = 0xFF7EBAF6U;                               // Angle
constexpr std::uint32_t kCagKeyCombos = 0xDF1451EBU;                        // cag_key_combos
constexpr std::uint32_t kCamAnimText = 0xA7AB3B6DU;                         // cam_anim_text
constexpr std::uint32_t kCanRetryGoal = 0xDBEB46E2U;                        // can_retry_goal
constexpr std::uint32_t kComboSets = 0x0490F171U;                           // combo_sets
constexpr std::uint32_t kComboSize = 0x67DD90CAU;                           // combo_size
constexpr std::uint32_t kControlType = 0x81CFF663U;                         // control_type
constexpr std::uint32_t kCreateCagHelperText = 0xD066A889U;                 // create_cag_helper_text
constexpr std::uint32_t kEditedGoal = 0x981D3AD0U;                          // edited_goal
constexpr std::uint32_t kEditedGoalSuccessCamAnims = 0x8070C110U;           // EditedGoal_success_cam_anims
constexpr std::uint32_t kEditedHighComboGoalText = 0x3693BFE4U;             // edited_high_combo_goal_text
constexpr std::uint32_t kEditedHighScoreGoalText = 0xD6EFB1B5U;             // edited_high_score_goal_text
constexpr std::uint32_t kEmergencyKeyCombos = 0x6D641BCBU;                  // emergency_key_combos
constexpr std::uint32_t kFlag = 0x2E0B1465U;                                // Flag
constexpr std::uint32_t kFullName = 0x243B9C3BU;                            // full_name
constexpr std::uint32_t kGaps = 0xD76C173EU;                                // Gaps
constexpr std::uint32_t kGoalDeScription = 0x0C5D7E6BU;                     // goal_deScription
constexpr std::uint32_t kGoalEditorRefreshGoalObjectPosition = 0x79582240U; // goal_editor_refresh_goal_object_position
constexpr std::uint32_t kGoalFlags = 0xCC3C4CC4U;                           // goal_flags
constexpr std::uint32_t kGoalId = 0x9982E501U;                              // Goal_Id
constexpr std::uint32_t kGoalName = 0xBFECC45BU;                            // goal_name
constexpr std::uint32_t kGoalTetrisKeyCombos = 0x7BE1E689U;                 // goal_tetris_key_combos
constexpr std::uint32_t kGoalText = 0xDA441D9AU;                            // goal_text
constexpr std::uint32_t kGoalTrickBeatKeyCombos = 0x548B4A38U;              // goal_trick_beat_key_combos
constexpr std::uint32_t kGoalType = 0x6D11ED74U;                            // Goal_Type
constexpr std::uint32_t kHeight = 0x0AB21AF0U;                              // Height
constexpr std::uint32_t kKeyCombo = 0xACFDB27AU;                            // Key_Combo
constexpr std::uint32_t kKeyCombos = 0x79704516U;                           // key_combos
constexpr std::uint32_t kLetterInfo = 0xB3C7F1B2U;                          // letter_info
constexpr std::uint32_t kLevel = 0x651533ECU;                               // Level
constexpr std::uint32_t kLevelGoalIndex = 0x60CE75AEU;                      // level_goal_index
constexpr std::uint32_t kLoadSk5EdGameplay = 0xB664035DU;                   // Load_Sk5Ed_gameplay
constexpr std::uint32_t kMaxTricks = 0x89473DB7U;                           // max_tricks
constexpr std::uint32_t kNodePositions = 0x32DF12E0U;                       // node_positions
constexpr std::uint32_t kNumTaps = 0xA4BEE6A1U;                             // num_taps
constexpr std::uint32_t kObjId = 0x99F6CCBBU;                               // obj_id
constexpr std::uint32_t kPedName = 0xB9426608U;                             // ped_name
constexpr std::uint32_t kPedPosition = 0x7F1538E0U;                         // PedPosition
constexpr std::uint32_t kPos = 0x7F261953U;                                 // Pos
constexpr std::uint32_t kProName = 0x71FC348BU;                             // pro_name
constexpr std::uint32_t kRacePerCheckpointTime = 0xD9426372U;               // race_per_checkpoint_time
constexpr std::uint32_t kRaceTotalLaps = 0x1DAAE95BU;                       // race_total_laps
constexpr std::uint32_t kRaceWaypoints = 0xA77B1F07U;                       // race_waypoints
constexpr std::uint32_t kRadius = 0xC48391A5U;                              // Radius
constexpr std::uint32_t kRandomSpin = 0x8C8ABD19U;                          // random_spin
constexpr std::uint32_t kRequirePerfect = 0x687E2C25U;                      // Require_Perfect
constexpr std::uint32_t kRequiredGaps = 0x52D4489EU;                        // required_gaps
constexpr std::uint32_t kRequiredTrickName = 0x730844A3U;                   // required_trick_name
constexpr std::uint32_t kRestartNode = 0x3494F170U;                         // restart_node
constexpr std::uint32_t kScore = 0xCD66C8AEU;                               // Score
constexpr std::uint32_t kSetIndex = 0xEB7D6498U;                            // set_index
constexpr std::uint32_t kSingleCombo = 0xDF47B144U;                         // single_combo
constexpr std::uint32_t kSpin = 0xEDF5DB70U;                                // Spin
constexpr std::uint32_t kSuccessCamAnims = 0x27CD32E1U;                     // success_cam_anims
constexpr std::uint32_t kTargetId = 0x531E4D28U;                            // targetId
constexpr std::uint32_t kText = 0xC4745838U;                                // Text
constexpr std::uint32_t kTime = 0x906B67BAU;                                // Time
constexpr std::uint32_t kTimeLimit = 0xC51BF6E3U;                           // time_limit
constexpr std::uint32_t kTimeToStopAddingTricks = 0xA6D96603U;              // time_to_stop_adding_tricks
constexpr std::uint32_t kTrickTime = 0xDED8540AU;                           // trick_time
constexpr std::uint32_t kTricktrisBlockSize = 0x45A375C9U;                  // tricktris_block_size
constexpr std::uint32_t kTricktrisTotalToWin = 0xBFD3A831U;                 // tricktris_total_to_win
constexpr std::uint32_t kTriggerObjId = 0x02D7E03DU;                        // trigger_obj_id
constexpr std::uint32_t kViewGoalsText = 0x4BC5229DU;                       // view_goals_text
constexpr std::uint32_t kWinMessage = 0x5DE57284U;                          // win_message
constexpr std::uint32_t kWonGoal = 0x14938611U;                             // won_goal
constexpr std::size_t kTextBufferSize = 0x80;    // a goal's generated text
constexpr int kLetterCount = 5;                  // SKATE / COMBO letters
constexpr int kWaypointDefaultValue = 5;         // a race waypoint's second integer (meaning not known)
constexpr std::uint32_t kGoalManagerCheckpointTime =
    0xA4038EC9U; // the goal manager's name for the per-checkpoint time (text unknown)
constexpr std::uint32_t kGoalManagerOn = 0xA302D7CDU;    // a parameter the goal manager reads as 1 (text unknown)
constexpr std::uint32_t kWaypointNodeName = 0x40C698AFU; // a race waypoint's node name (text unknown)
std::array<char, kNodeNameSize>& goal_io_name_buffer()
{
    static std::array<char, kNodeNameSize> goal_io_name_buffer{};
    return goal_io_name_buffer;
}

constexpr float kDropHeight = 10000.0F; // 0x0064B308

template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

gsl::owner<script::Array*> new_array_of(uint32_t size, script::QbValueKind type)
{
    gsl::owner<script::Array*> array = script::new_array();
    array->configure_size_kind(size, type);
    return array;
}
script::Struct* structure_element(script::Array* array, uint32_t index)
{
    return array->struct_at(index);
}

} // namespace

// 0x004E93D0
void EditedGoal::drop_point_to_ground(int index)
{
    maths::Vec4& pos = core::at(path_vertices_, index).pos;
    maths::Vec4 start = {.x = pos.x, .y = pos.y + kDropHeight, .z = pos.z, .w = pos.w};
    const maths::Vec4 end = {.x = pos.x, .y = pos.y - kDropHeight, .z = pos.z, .w = pos.w};
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
    pos = feeler.contact_pos();
}

// 0x004E9590
void EditedGoal::place_points_on_ground()
{
    char* name = goal_io_name_buffer().data();
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
        const EditedGoalPoint& point = core::at(path_vertices_, i);
        params.put_vec3("Pos", {.x = point.pos.x, .y = point.height + point.pos.y, .z = point.pos.z});
        script::launch_script(kGoalEditorRefreshGoalObjectPosition, &params, nullptr, false, nullptr);
        params.get_clear();
    }
}

// 0x004EB550
bool EditedGoal::place_point_and_step_back(const maths::Vec4& pos, EditedGoal::PointPose point_pose)
{
    const auto height = point_pose.height;
    const auto angle = point_pose.angle;
    if (current_point_ == 0 || !can_place_point(pos, height))
        return false;
    core::at(path_vertices_, current_point_).pos = pos;
    core::at(path_vertices_, current_point_).height = height;
    core::at(path_vertices_, current_point_).angle = angle;
    place_goal_marker(current_point_);
    if (current_point_ == vertex_total_)
        ++vertex_total_;
    --current_point_;
    run_marker_object_script(current_point_);
    if (current_point_ == 0) {
        gsl::owner<script::Struct*> params = script::new_struct();
        params->put_name_key(checksums::Name{0U}, kPedPosition);
        script::launch_script(kCreateCagHelperText, params, nullptr, false, nullptr);
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
    init_edited_goal(this);
    taken_count_ = true;
    won_ = false;
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    if (level == kLoadSk5EdGameplay) // (Load_Sk5Ed_gameplay counts as the park editor's)
        level = kParkEditorLevel;
    level_ = level;
    params->fetch_int_value(kLevelGoalIndex, &level_goal_number_, 0);
    params->read_hash_value(kGoalType, &kind_tag_, 0);
    change_species(kind_tag_);
    params->fetch_int_value(kScore, &points_held_, 0);
    params->fetch_int_value(kTimeLimit, &time_limit_, 0);
    params->read_hash_value(kControlType, &control_type_, 0);
    const char* empty = ""; // ""
    const char* text = empty;
    params->fetch_text(kViewGoalsText, &text, 0);
    text::copy_into(label_, text);
    text = empty;
    params->fetch_text(kGoalDeScription, &text, 0);
    text::copy_into(summary_text_, text);
    text = empty;
    params->fetch_text(kWinMessage, &text, 0);
    text::copy_into(win_message_, text);
    text = empty;
    params->fetch_text(kPedName, &text, 0);
    text::copy_into(ped_name_, text);
    script::Array* points = nullptr;
    if (params->fetch_list(kNodePositions, &points, 0)) {
        vertex_total_ = static_cast<uint8_t>(points->extent());
        for (uint32_t i = 0; i < vertex_total_; ++i) {
            script::Struct* point = structure_element(points, i);
            point->fetch_vec(kPos, (&core::at(path_vertices_, i).pos), 0);
            point->fetch_float(kHeight, &core::at(path_vertices_, i).height, 0);
            point->fetch_float(kAngle, &core::at(path_vertices_, i).angle, 0);
        }
    }
    read_specific_params(params);
}

// 0x004E9BC0
void EditedGoal::write_key_combo_sets(script::Struct* out)
{
    if (num_key_combo_sets_ == 0)
        return;
    gsl::owner<script::Array*> sets = new_array_of(static_cast<uint32_t>(num_key_combo_sets_), script::QbValueKind::kStruct);
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        const KeyComboSet& set = core::at(key_combo_sets_, i);
        gsl::owner<script::Struct*> entry = script::new_struct();
        entry->put_int(checksums::Name{kSetIndex}, set.set);
        if (set.must_be_flawless_)
            entry->put_name_key(checksums::Name{0U}, kRequirePerfect);
        entry->put_int(checksums::Name{kSpin}, set.spin * kSpinUnitDegrees);
        entry->put_int(checksums::Name{kNumTaps}, set.tap_total_);
        sets->set_cell(static_cast<uint32_t>(i), script::Cell::of(entry));
    }
    out->append_array_ref(kComboSets, sets);
}

// 0x004E98F0
void EditedGoal::write_goal_key_combos(script::Struct* out)
{
    if (num_key_combo_sets_ == 0) {
        debug::printf("!!!!!!!! Warning! No combo sets specified, using emergency_key_combos\n");
        out->put_name_key(checksums::Name{kGoalTetrisKeyCombos}, kEmergencyKeyCombos);
        return;
    }
    script::Array* all_sets = script::fetch_list(kCagKeyCombos);
    // How many key combos the sets hold (a set without its array: a null read, as the original).
    uint32_t total = 0;
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        script::Array* combos = nullptr;
        structure_element(all_sets, core::at(key_combo_sets_, i).set)->fetch_list(kKeyCombos, &combos, 0);
        total += combos->extent();
    }
    gsl::owner<script::Array*> result = new_array_of(total, script::QbValueKind::kStruct);
    uint32_t index = 0;
    for (int i = 0; i < num_key_combo_sets_; ++i) {
        const KeyComboSet& set = core::at(key_combo_sets_, i);
        script::Array* combos = nullptr;
        structure_element(all_sets, set.set)->fetch_list(kKeyCombos, &combos, 0);
        for (uint32_t j = 0; j < combos->extent(); ++j) {
            gsl::owner<script::Struct*> entry = script::new_struct();
            if (combos->kind_tag() == script::QbValueKind::kName)
                entry->put_name_key(checksums::Name{kKeyCombo}, combos->fetch_part(j));
            else
                entry->append_structure_if_any(structure_element(combos, j));
            if (set.tap_total_)
                entry->put_int(checksums::Name{kNumTaps}, set.tap_total_);
            if (set.must_be_flawless_)
                entry->put_name_key(checksums::Name{0U}, kRequirePerfect);
            if (twist_rate_)
                entry->put_int(checksums::Name{kRandomSpin}, twist_rate_);
            result->set_cell(index++, script::Cell::of(entry));
        }
    }
    out->append_array_ref(kind_tag_ == kGoalTypeKeyCombo ? kGoalTrickBeatKeyCombos : kGoalTetrisKeyCombos, result);
}

// 0x004E9D60
void EditedGoal::write_trick_settings(script::Struct* out, int for_goal_manager)
{
    out->put_int(checksums::Name{kAccelerationInterval}, acceleration_interval_);
    out->push_float(checksums::Name{kAccelerationPercent}, acceleration_percent_);
    out->put_int(checksums::Name{kTrickTime}, trick_time_);
    out->put_int(checksums::Name{kTimeToStopAddingTricks}, time_to_stop_adding_tricks_);
    out->put_int(checksums::Name{kMaxTricks}, max_tricks_);
    if (for_goal_manager == 1) {
        write_goal_key_combos(out);
        return;
    }
    out->put_int(checksums::Name{kSpin}, twist_rate_);
    write_key_combo_sets(out);
}

// 0x004E9E00
void EditedGoal::write_specific_params(script::Struct* out, int for_goal_manager)
{
    std::array<char, kTextBufferSize> text{};
    switch (kind_tag_) {
    case kGoalTypeRace:
        if (for_goal_manager == 1) {
            if (race_per_checkpoint_time_ > 0)
                out->put_int(checksums::Name{kGoalManagerCheckpointTime}, race_per_checkpoint_time_);
        } else {
            out->put_int(checksums::Name{kRacePerCheckpointTime}, race_per_checkpoint_time_);
        }
        out->put_int(checksums::Name{kRaceTotalLaps}, race_total_laps_);
        break;
    case kGoalTypeComboTricks:
        out->put_name_key(checksums::Name{0U}, static_cast<uint32_t>(kGoalTypeCombo));
        out->put_int(checksums::Name{kComboSize}, chain_length_);
        if (single_combo_) {
            out->put_name_key(checksums::Name{0U}, kSingleCombo);
            max_tricks_ = chain_length_;
        }
        write_trick_settings(out, for_goal_manager);
        break;
    case kGoalTypeGap:
        out->append_word(kRequiredTrickName, required_trick_.data());
        if (for_goal_manager == 1) {
            gsl::owner<script::Array*> flags = new_array_of(num_gaps_, script::QbValueKind::kName);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i) {
                get_sprintf(text.data(), "got_%d", i + 1);
                flags->set_element(static_cast<uint32_t>(i), checksums::hash_from_text(text.data()));
            }
            out->append_array_ref(kGoalFlags, flags);
            gsl::owner<script::Array*> gaps = new_array_of(num_gaps_, script::QbValueKind::kInteger);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
                gaps->set_element(static_cast<uint32_t>(i), core::at(gaps_, i));
            out->append_array_ref(kRequiredGaps, gaps);
        } else if (num_gaps_) {
            gsl::owner<script::Array*> gaps = new_array_of(num_gaps_, script::QbValueKind::kInteger);
            for (int i = 0; std::cmp_less(i, num_gaps_); ++i)
                gaps->set_element(static_cast<uint32_t>(i), core::at(gaps_, i));
            out->append_array_ref(kGaps, gaps);
        }
        break;
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        out->put_int(checksums::Name{kScore}, points_held_);
        if (for_goal_manager == 1) {
            const char* format = script::fetch_text(kind_tag_ == kGoalTypeHighCombo ? kEditedHighComboGoalText : kEditedHighScoreGoalText);
            crt::sprintf_from_data(text.data(), format, points_held_); // (the script's text is the format)
            out->append_word(kGoalText, text.data());
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
        out->put_name_key(checksums::Name{0U}, static_cast<uint32_t>(kGoalTypeTricktris));
        out->put_int(checksums::Name{kTricktrisBlockSize}, stack_block_span_);
        out->put_int(checksums::Name{kTricktrisTotalToWin}, tricktris_total_to_win_);
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
    std::array<char, kNodeNameSize> name{};
    out->append_structure_if_any(goal_type_structure(kind_tag_));
    out->append_structure_if_any(goal_type_defaults(kind_tag_));
    core::at(name, 0) = '\0';
    get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", level_goal_number_);
    out->put_name_key(checksums::Name{kTriggerObjId}, checksums::hash_from_text(name.data()));
    core::at(name, 0) = '\0';
    get_sprintf(name.data(), "TRG_EditedGoal%d_Restart", level_goal_number_);
    out->put_name_key(checksums::Name{kRestartNode}, checksums::hash_from_text(name.data()));
    out->put_name_key(checksums::Name{kGoalManagerOn}, 1U);
    out->put_name_key(checksums::Name{0U}, kCanRetryGoal);
    switch (kind_tag_) {
    case kGoalTypeSkate: {
        static const std::array<uint32_t, 5> kLetters = {{0xBA54BD5C, 0x150A97C2, 0x84CA8B0A, 0xB091B445, 0x008085F0}};
        for (int i = 0; i < kLetterCount; ++i) {
            core::at(name, 0) = '\0';
            get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", level_goal_number_, i);
            out->put_name_key(checksums::Name{core::at(kLetters, i)}, checksums::hash_from_text(name.data()));
        }
        break;
    }
    case kGoalTypeCombo: {
        static const std::array<const char*, 5> kLetters = {{"C", "O", "M", "B", "O"}};
        gsl::owner<script::Array*> letters = new_array_of(kLetterCount, script::QbValueKind::kStruct);
        for (int i = 0; i < kLetterCount; ++i) {
            gsl::owner<script::Struct*> letter = script::new_struct();
            core::at(name, 0) = '\0';
            get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", level_goal_number_, i);
            letter->put_name_key(checksums::Name{kObjId}, checksums::hash_from_text(name.data()));
            letter->append_word(kText, core::at(kLetters, i));
            letters->set_cell(static_cast<uint32_t>(i), script::Cell::of(letter));
        }
        out->append_array_ref(kLetterInfo, letters);
        break;
    }
    case kGoalTypeRace: {
        const int count = vertex_total_ - 2;
        gsl::owner<script::Array*> waypoints = new_array_of(static_cast<uint32_t>(count), script::QbValueKind::kStruct);
        gsl::owner<script::Array*> flags = new_array_of(static_cast<uint32_t>(count), script::QbValueKind::kName);
        std::array<char, kNodeNameSize> flag{};
        for (int i = 0; i < count; ++i) {
            gsl::owner<script::Struct*> waypoint = script::new_struct();
            waypoint->put_name_key(checksums::Name{kWaypointNodeName}, edited_goal_node_name(i + 2, level_goal_number_));
            get_sprintf(flag.data(), "got_%d", i + 1);
            flags->set_element(static_cast<uint32_t>(i), checksums::hash_from_text(flag.data()));
            waypoint->put_name_key(checksums::Name{kFlag}, checksums::hash_from_text(flag.data()));
            if (race_per_checkpoint_time_ > 0)
                waypoint->put_int(checksums::Name{kTime}, race_per_checkpoint_time_);
            waypoint->put_int(checksums::Name{kRadius}, kWaypointDefaultValue);
            waypoints->set_cell(static_cast<uint32_t>(i), script::Cell::of(waypoint));
        }
        out->append_array_ref(kRaceWaypoints, waypoints);
        out->append_array_ref(kGoalFlags, flags);
        break;
    }
    default:
        break;
    }
    write_specific_params(out, 1);
    if (core::at(summary_text_, 0))
        out->append_word(kGoalDeScription, summary_text_.data());
    if (core::at(win_message_, 0)) {
        // The win message's dialog: a copy of the global template array with the text and the pro's node.
        script::Array* source = script::fetch_list(kEditedGoalSuccessCamAnims);
        gsl::owner<script::Array*> dialog = script::new_array();
        script::duplicate_array(dialog, source);
        structure_element(dialog, 0)->append_word(kCamAnimText, win_message_.data());
        core::at(name, 0) = '\0';
        get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", level_goal_number_);
        const uint32_t pro = checksums::hash_from_text(name.data());
        structure_element(dialog, 0)->put_name_key(checksums::Name{kTargetId}, pro);
        out->append_array_ref(kSuccessCamAnims, dialog);
    }
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", level_goal_number_);
    out->append_word(kGoalName, park_editing_state().created_goal_name().data());
    if (core::at(label_, 0))
        out->append_word(kViewGoalsText, label_.data());
    if (core::at(ped_name_, 0))
        out->append_word(kFullName, ped_name_.data());
    out->put_int(checksums::Name{kTime}, time_limit_);
    out->put_name_key(checksums::Name{kControlType}, control_type_);
    out->put_name_key(checksums::Name{0U}, kEditedGoal);
}

// 0x004EB260
void EditedGoal::export_to_struct(script::Struct* out)
{
    std::array<char, kNodeNameSize> name{};
    out->put_name_key(checksums::Name{0U}, kEditedGoal);
    out->put_name_key(checksums::Name{kLevel}, level_);
    out->put_int(checksums::Name{kLevelGoalIndex}, level_goal_number_);
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", level_goal_number_);
    out->put_name_key(checksums::Name{kGoalId}, checksums::hash_from_text(park_editing_state().created_goal_name().data()));
    out->put_int(checksums::Name{kTimeLimit}, time_limit_);
    out->put_int(checksums::Name{kWonGoal}, won_);
    out->put_name_key(checksums::Name{kControlType}, control_type_);
    out->append_word(kViewGoalsText, label_.data());
    out->append_word(kGoalDeScription, summary_text_.data());
    out->append_word(kWinMessage, win_message_.data());
    out->append_word(kPedName, ped_name_.data());
    out->put_name_key(checksums::Name{kGoalType}, kind_tag_);
    core::at(name, 0) = '\0';
    get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", level_goal_number_);
    out->put_name_key(checksums::Name{kProName}, checksums::hash_from_text(name.data()));
    write_specific_params(out, 0);
    if (!vertex_total_)
        return;
    gsl::owner<script::Array*> points = new_array_of(vertex_total_, script::QbValueKind::kStruct);
    for (uint32_t i = 0; i < vertex_total_; ++i) {
        const EditedGoalPoint& point = core::at(path_vertices_, i);
        gsl::owner<script::Struct*> entry = script::new_struct();
        entry->put_vec3(checksums::Name{kPos}, {.x = point.pos.x, .y = point.pos.y, .z = point.pos.z});
        entry->push_float(checksums::Name{kHeight}, point.height);
        entry->push_float(checksums::Name{kAngle}, point.angle);
        points->set_cell(i, script::Cell::of(entry));
    }
    out->append_array_ref(kNodePositions, points);
}

// 0x004EB6B0
void EditedGoal::add_to_goal_manager(bool unwin)
{
    gsl::owner<script::Struct*> params = script::new_struct();
    write_goal_manager_params(params);
    goals::GoalManager* manager = goals::get_objective_board();
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", level_goal_number_);
    manager->enrol_objective(checksums::hash_from_text(park_editing_state().created_goal_name().data()), params);
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", level_goal_number_);
    goals::Objective* goal = manager->get_goal(checksums::hash_from_text(park_editing_state().created_goal_name().data()), 1);
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
