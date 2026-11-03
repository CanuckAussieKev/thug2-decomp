// The goal editor's small helpers: see goal_editor_util.h. From the disassembly of 0x004E6600-0x004E6B6E.

#include "game/park_editing/goal_editor_util.h"

#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/format.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editing_state.h"

namespace park_editing {

namespace {

constexpr std::uint32_t kComboB = 0x0CF68AF0u; // Combo_B
constexpr std::uint32_t kComboC = 0x7BF1BA66u; // Combo_C
constexpr std::uint32_t kComboM = 0x9C499761u; // Combo_M
constexpr std::uint32_t kComboO = 0x7247F64Du; // Combo_O
constexpr std::uint32_t kComboSkatetris = 0x0FC6F698u; // ComboSkatetris
constexpr std::uint32_t kEditedGoalExtraParamsCombo = 0xFECBAB7Cu; // EditedGoal_ExtraParams_Combo
constexpr std::uint32_t kEditedGoalExtraParamsComboSkateTris = 0x6626B0D8u; // EditedGoal_ExtraParams_ComboSkateTris
constexpr std::uint32_t kEditedGoalExtraParamsGap = 0x9005703Au; // EditedGoal_ExtraParams_Gap
constexpr std::uint32_t kEditedGoalExtraParamsHighCombo = 0xAB62B082u; // EditedGoal_ExtraParams_HighCombo
constexpr std::uint32_t kEditedGoalExtraParamsHighScore = 0x28C7B799u; // EditedGoal_ExtraParams_HighScore
constexpr std::uint32_t kEditedGoalExtraParamsRace = 0xFD6CAF2Au; // EditedGoal_ExtraParams_Race
constexpr std::uint32_t kEditedGoalExtraParamsSkate = 0xE41E0E04u; // EditedGoal_ExtraParams_Skate
constexpr std::uint32_t kEditedGoalExtraParamsSkateTris = 0x38C1D87Eu; // EditedGoal_ExtraParams_SkateTris
constexpr std::uint32_t kEditedGoalExtraParamsTrickBeat = 0x97FEFF8Fu; // EditedGoal_ExtraParams_TrickBeat
constexpr std::uint32_t kEditedGoalExtraParamsTrickTris = 0xAD585D92u; // EditedGoal_ExtraParams_TrickTris
constexpr std::uint32_t kGap = 0x61C5D092u; // Gap
constexpr std::uint32_t kGoalAmateurCOMBOlineGenericParams = 0xB4F5801Du; // Goal_amateurCOMBOline_genericParams
constexpr std::uint32_t kGoalEditorDeleteMarkerObject = 0xDCF3A25Eu; // goal_editor_delete_marker_object
constexpr std::uint32_t kGoalEditorGetBeatPattern = 0xFA54B475u; // goal_editor_get_beat_pattern
constexpr std::uint32_t kGoalGapsGenericParams = 0x5756A675u; // Goal_Gaps_GenericParams
constexpr std::uint32_t kGoalHighComboGenericParams = 0xB520A510u; // Goal_HighCombo_GenericParams
constexpr std::uint32_t kGoalHighScoreGenericParams = 0xEF8F345Fu; // Goal_HighScore_GenericParams
constexpr std::uint32_t kGoalRaceGenericParams = 0x34785A5Bu; // Goal_Race_GenericParams
constexpr std::uint32_t kGoalSkateLettersGenericParams = 0x3328976Au; // Goal_SkateLetters_genericParams
constexpr std::uint32_t kGoalTetrisGenericParams = 0x675816B5u; // goal_tetris_genericParams
constexpr std::uint32_t kGoalTrickBeatGenericParams = 0xD3828793u; // Goal_TrickBeat_GenericParams
constexpr std::uint32_t kHighCombo = 0xEC414B76u; // HighCombo
constexpr std::uint32_t kHighScore = 0x6FE44C6Du; // HighScore
constexpr std::uint32_t kLetterA = 0x04ED54A2u; // Letter_A
constexpr std::uint32_t kLetterE = 0x038090BBu; // Letter_E
constexpr std::uint32_t kLetterK = 0xE438BDBCu; // Letter_K
constexpr std::uint32_t kLetterS = 0xF75425EAu; // Letter_S
constexpr std::uint32_t kLetterT = 0x6930B049u; // Letter_T
constexpr std::uint32_t kName = 0xA1DC81F9u; // Name
constexpr std::uint32_t kSkateTris = 0x7FE2238Au; // SkateTris
constexpr std::uint32_t kTrickBeat = 0xD0DD047Bu; // TrickBeat
constexpr std::uint32_t kTrickTris = 0xEA7BA666u; // TrickTris
constexpr int kDefaultAccelerationInterval = 5;
constexpr int kDefaultTimeToStopAddingTricks = 5;
constexpr int kDefaultPointsHeld = 100;
constexpr float kDefaultAccelerationPercent = 0.1f;
constexpr int kDefaultTrickTime = 3000;
constexpr int kDefaultMaxTricks = 11;
constexpr int kDefaultTwistRate = 360;
constexpr int kDefaultTricktrisTotalToWin = 8;

constexpr uint32_t kSkate = 0x54166ACD, kCombo = 0x4EC3CFB5, kCone = 0x25904450;

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

} // namespace

// 0x004E6600
const char* goal_object_model(checksums::Name goal_type, int index)
{
    const uint32_t type = goal_type.value();
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
uint32_t goal_object_name(checksums::Name goal_type, int index)
{
    const uint32_t type = goal_type.value();
    if (type == kCombo) {
        switch (index) {
        case 0:
            return kComboC;
        case 1:
        case 4:
            return kComboO;
        case 2:
            return kComboM;
        case 3:
            return kComboB;
        default:
            return 0;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0:
            return kLetterS;
        case 1:
            return kLetterK;
        case 2:
            return kLetterA;
        case 3:
            return kLetterT;
        case 4:
            return kLetterE;
        default:
            return 0;
        }
    }
    return 0;
}

// 0x004E6730
uint32_t edited_goal_node_name(int which, int goal)
{
    std::array<char, kNodeNameSize> name{};
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
    case kHighScore:
        return script::read_sub_struct(kGoalHighScoreGenericParams);
    case kCombo:
        return script::read_sub_struct(kGoalAmateurCOMBOlineGenericParams);
    case kComboSkatetris:
    case kSkateTris:
    case kTrickTris:
        return script::read_sub_struct(kGoalTetrisGenericParams);
    case kCone:
        return script::read_sub_struct(kGoalRaceGenericParams);
    case kSkate:
        return script::read_sub_struct(kGoalSkateLettersGenericParams);
    case kGap:
        return script::read_sub_struct(kGoalGapsGenericParams);
    case kTrickBeat:
        return script::read_sub_struct(kGoalTrickBeatGenericParams);
    case kHighCombo:
        return script::read_sub_struct(kGoalHighComboGenericParams);
    default:
        return nullptr;
    }
}

// 0x004E6890
script::Struct* goal_type_defaults(uint32_t type)
{
    switch (type) {
    case kHighScore:
        return script::read_sub_struct(kEditedGoalExtraParamsHighScore);
    case kCombo:
        return script::read_sub_struct(kEditedGoalExtraParamsCombo);
    case kComboSkatetris:
        return script::read_sub_struct(kEditedGoalExtraParamsComboSkateTris);
    case kCone:
        return script::read_sub_struct(kEditedGoalExtraParamsRace);
    case kSkate:
        return script::read_sub_struct(kEditedGoalExtraParamsSkate);
    case kGap:
        return script::read_sub_struct(kEditedGoalExtraParamsGap);
    case kTrickTris:
        return script::read_sub_struct(kEditedGoalExtraParamsTrickTris);
    case kSkateTris:
        return script::read_sub_struct(kEditedGoalExtraParamsSkateTris);
    case kTrickBeat: {
        script::Struct* defaults = script::read_sub_struct(kEditedGoalExtraParamsTrickBeat);
        script::run_script_once(kGoalEditorGetBeatPattern, nullptr, nullptr, defaults);
        return defaults;
    }
    case kHighCombo:
        return script::read_sub_struct(kEditedGoalExtraParamsHighCombo);
    default:
        return nullptr;
    }
}

// 0x004E69C0
int goal_editor_constant82() { return kGoalNodeCount; }

// 0x004E69D0
void init_edited_goal(EditedGoal* goal)
{
    goal->set_acceleration_interval(kDefaultAccelerationInterval);
    goal->set_time_to_stop_adding_tricks(kDefaultTimeToStopAddingTricks);
    goal->set_taken_count(false);
    goal->set_level(0);
    goal->label().front() = 0;
    goal->summary_text().front() = 0;
    goal->win_message().front() = 0;
    goal->ped_name().front() = 0;
    goal->set_kind_tag(0);
    goal->set_vertex_total(0);
    goal->set_min_points(0);
    goal->set_max_points(0);
    goal->set_current_point(0);
    goal->set_last_point_placed(false);
    goal->set_won(false);
    goal->set_points_held(kDefaultPointsHeld);
    goal->set_time_limit(kDefaultTimeLimitSeconds);
    goal->set_race_total_laps(1);
    goal->set_race_per_checkpoint_time(0);
    goal->set_acceleration_percent(kDefaultAccelerationPercent);
    goal->set_trick_time(kDefaultTrickTime);
    goal->set_max_tricks(kDefaultMaxTricks);
    goal->set_twist_rate(kDefaultTwistRate);
    goal->set_single_combo(false);
    goal->set_chain_length(2);
    goal->set_stack_block_span(3);
    goal->set_tricktris_total_to_win(kDefaultTricktrisTotalToWin);
    goal->set_num_key_combo_sets(0);
    goal->set_control_type(kSkate);
    goal->set_num_gaps(0);
    goal->required_trick().front() = 0;
    for (EditedGoalPoint& point : goal->path_vertices()) { // (each point's kind and height)
        point.kind = 0;
        point.height = 0.0f;
    }
    goal->set_level_goal_number(0);
}

// 0x004E6AE0
void run_marker_object_script(int index)
{
    script::Struct params;
    char* name = park_editing_state().goal_name_buffer().data();
    get_sprintf(name, "GoalEditorMarkerObject%d", index);
    params.put_name_key(checksums::Name{kName}, checksums::hash_from_text(name));
    script::launch_script(kGoalEditorDeleteMarkerObject, &params, nullptr, false, nullptr);
    params.get_clear();
}

} // namespace park_editing
