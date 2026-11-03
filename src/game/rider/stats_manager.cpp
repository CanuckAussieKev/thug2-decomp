// The stats manager component: the story mode's stat challenges. See stats_manager.h.

#include "game/rider/stats_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/core/dbg.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/rider/rider.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/skater_state.h"
#include "game/rider/physics_control.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace objects {
namespace {
constexpr uint32_t kStatsManagerReInit = 0x82CE30CE; // StatsManager_ReInit
constexpr uint32_t kSTATSManager = 0x4FF8DAC6; // STATSManager
constexpr uint32_t kStatsManagerReset = 0x57879C96; // StatsManager_Reset
constexpr uint32_t kStatsManagerActivateGoals = 0xEE59073C; // StatsManager_ActivateGoals
constexpr uint32_t kStatsManagerDeactivateGoals = 0xF1745991; // StatsManager_DeactivateGoals
constexpr uint32_t kStatsManagerGetGoalStatus = 0x78E7D855; // StatsManager_GetGoalStatus
constexpr uint32_t kStatsManagerUnlockAmGoals = 0xB341B5A5; // StatsManager_UnlockAmGoals
constexpr uint32_t kStatsManagerUnlockProGoals = 0xB7FAC894; // StatsManager_UnlockProGoals
constexpr uint32_t kStatsManagerGetLastAirComboScore = 0x629D29FA; // StatsManager_GetLastAirComboScore (letter case unknown)
constexpr uint32_t kStatsManagerClearLastAirComboScore = 0xB1652DEC; // StatsManager_ClearLastAirComboScore
constexpr uint32_t kBUMPSTATS = 0xB8688A8Au; // BUMP_STATS
constexpr uint32_t kStatsGoals = 0x4E46CC28u; // stats_goals
constexpr uint32_t kGoalType = 0x0FFD7BBEu; // GoalType
constexpr uint32_t kStattype = 0xC2455018u; // stattype
constexpr uint32_t kValue = 0xE288A7CBu; // Value
constexpr uint32_t kValueString = 0xCD021293u; // value_string
constexpr uint32_t kTextName = 0xC4745838u; // Text
constexpr uint32_t kComplete = 0xBD982FDCu; // Complete
constexpr uint32_t kValueTrick = 0xD8A9D3CAu; // value_trick
constexpr uint32_t kValueTaps = 0x3C2FB496u; // value_taps
constexpr uint32_t kIsCareer = 0x1DED1EA4; // is_career
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // Load_MainMenu
constexpr uint32_t kName = 0xA1DC81F9u; // Name
constexpr uint32_t kCustom = 0x0A7BE964; // Custom
constexpr uint32_t kStatsMessageBail = 0xD44EA68F; // stats_message_bail
constexpr uint32_t kGrabTrick = 0xB9AE26D2; // GrabTrick
constexpr uint32_t kFlipTrick = 0xBA15FC7F; // FlipTrick
constexpr uint32_t kStatsMessageLand = 0x750BCF49; // stats_message_land
constexpr uint32_t kFlipSpeed = 0x6DCB497C; // flip_speed
constexpr uint32_t kFlipIncreaseText = 0x585887CA; // flip_increase_text
constexpr uint32_t kAirIncreaseText = 0xA523DF6C; // air_increase_text
constexpr uint32_t kSwitch = 0x9016B4E7; // Switch
constexpr uint32_t kSwitchIncreaseText = 0x1A6B74EA; // switch_increase_text
constexpr uint32_t kLipBalance = 0xAE798769; // lip_balance
constexpr uint32_t kLipIncreaseText = 0x1F1BCDEF; // lip_increase_text
constexpr uint32_t kOllie = 0x9B65D7B8; // Ollie
constexpr uint32_t kOllieIncreaseText = 0xA8386951; // ollie_increase_text
constexpr uint32_t kRun = 0xAF895B3F; // Run
constexpr uint32_t kRunIncreaseText = 0xA17AC668; // run_increase_text
constexpr uint32_t kSpinName = 0xEDF5DB70; // Spin
constexpr uint32_t kSpinIncreaseText = 0xB959CC23; // spin_increase_text
constexpr uint32_t kManualBalance = 0xB1FC0722; // manual_balance
constexpr uint32_t kManualIncreaseText = 0x069F2596; // manual_increase_text
constexpr uint32_t kSpeed = 0xF0D90109; // Speed
constexpr uint32_t kSpeedIncreaseText = 0x1686EBD3; // speed_increase_text
constexpr uint32_t kRailBalance = 0xF73A13E3; // rail_balance
constexpr uint32_t kRailIncreaseText = 0x6E7A996D; // rail_increase_text
constexpr uint32_t kGotIt = 0x2044BAAFu; // got_it
constexpr uint32_t kShowStatsMessage = 0x55E516C8; // show_stats_message
constexpr uint32_t kBeatFirstStatGoal = 0xEF3A4EE5; // beat_first_stat_goal
constexpr uint32_t kStatPlayWinSound = 0x5A90FB86; // stat_play_win_sound
constexpr uint32_t kString = 0x61414D56u; // String
constexpr uint32_t kVstring = 0xC257D69Au; // vstring
constexpr uint32_t kIndex = 0x7F8C98FEu; // Index
constexpr uint32_t kScore = 0xCD66C8AEu; // Score
constexpr uint32_t kShowVertComboMessage = 0x7C9C7232; // show_vert_combo_message
constexpr uint32_t kManualPairTrick = 0x89B7DBE4; // text unknown (the trick counted together with Manual)
constexpr uint32_t kShowedStatMessageSound = 0x41B03551; // showed_stat_message_sound
constexpr uint32_t kConsoleUnhide = 0x7EC5F3E0; // console_unhide
constexpr uint32_t kConsoleHide = 0x3CB06807; // console_hide
constexpr uint32_t kStatus = 0x84FF9AE3u; // Status
constexpr uint32_t kLastAirComboScore = 0x2D3B766Bu; // last_air_combo_score
} // namespace

std::array<script::MemberFunctionEntry, kStatsManagerMemberFunctionCount>& stats_manager_member_functions()
{
    static std::array<script::MemberFunctionEntry, kStatsManagerMemberFunctionCount> stats_manager_member_functions{{
        {.callsign_ = kStatsManagerReInit, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerReset, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerActivateGoals, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerDeactivateGoals, .owner_kind_ = kSTATSManager},
        {.callsign_ = kStatsManagerGetGoalStatus, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerUnlockAmGoals, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerUnlockProGoals, .owner_kind_ = kSTATSManager}, {.callsign_ = kStatsManagerGetLastAirComboScore, .owner_kind_ = kSTATSManager},
        {.callsign_ = kStatsManagerClearLastAirComboScore, .owner_kind_ = kSTATSManager}, {.callsign_ = 0, .owner_kind_ = kSTATSManager}}};
    return stats_manager_member_functions;
}

namespace {

constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;

// This frame's state (cur_phase_).
constexpr uint32_t kStateOther = 0x26A7CADF;
constexpr uint32_t kStateLaunched = 0xB39B4F1B; // the skater state's +0x30
constexpr uint32_t kStateAir = 0x439F4704;      // in the air (physics state 1) without +0x30

// Goal kinds.
constexpr uint32_t kRise = 0x39E7537B;      // a jump's rise (inches over 12)
constexpr uint32_t kDrop = 0x7E064AD0;      // a jump's drop
constexpr uint32_t kAirCombo = 0x1DBBB148;  // a combo's score made in the air (landed)
constexpr uint32_t kAirHeight = 0x430B80C4; // height above the take-off
constexpr uint32_t kAirValue58 = 0x509ACA95;
constexpr uint32_t kComboScore = 0x4EC3CFB5;
constexpr uint32_t kMultiplier = 0x5B6556A4;
constexpr uint32_t kCountA80 = 0x364E8C16;
constexpr uint32_t kCountA84 = 0x68703AE9;
constexpr uint32_t kTrickCount = 0x69B0F836;
constexpr uint32_t kLeapReach = 0x8F6F9F05;   // horizontal
constexpr uint32_t kAirDistance = 0xC3EC3EED; // horizontal, from take-off to landing
constexpr uint32_t kScore562370 = 0x9068B4E5;

// Balance goal kinds and trick types.
constexpr uint32_t kGrindTime = 0xC1336019;
constexpr uint32_t kManualTime = 0xD9FFC141;
constexpr uint32_t kLipTime = 0xB67CFF14;
constexpr uint32_t kSlide = 0x8D10119D;
constexpr uint32_t kGrind = 0x255ED86F;
constexpr uint32_t kNoseManual = 0x0AC90769;
constexpr uint32_t kManual = 0xEF24413B;
constexpr uint32_t kLip = 0xA549B57B;

constexpr float kFeet = 12.0f; // 0x0064546C

void launch_script(uint32_t name, script::Struct* params = nullptr)
{
    script::launch_script(name, params, nullptr, false, nullptr);
}

void* skate_module() { return static_cast<void*>(modules::Skate::sole()); }
RiderIdentity* first_profile() { return static_cast<modules::Skate*>(skate_module())->fetch_account(0); }
void* game_flow() { return goals::get_objective_board(); }

// The skater's score component and its counts.
void* point_tally(GameEntity* skater) { return core::down_cast<Rider>(skater)->get_points_total(); }
int count_tricks(void* score, uint32_t trick, int a, int b, int c)
{
    return static_cast<modules::PointTally*>(score)->count_trick_after_taps(trick, a, static_cast<uint8_t>(b) != 0, c);
}

// 0x00423680: the distance between two points (z, x, then y; unrounded).
double span_to(const maths::Vec4& a, const maths::Vec4& b)
{
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}

// a - b, wrapping as the original's 32-bit subtraction.
int get_minus(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }

// The original's float-to-int (_ftol: towards zero).
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); } // (the low 32 bits)

} // namespace

// 0x0051CB90
gsl::owner<ObjectModule*> AttributeTracker::fabricate() { return new AttributeTracker; }

// 0x0051C440
void AttributeTracker::publish_method_table()
{
    script::register_member_function_table(stats_manager_member_functions());
}

// 0x0051C5A0
AttributeTracker::AttributeTracker()
{
    set_kind_tag(kType);
    air_start_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    air_end_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    jump_start_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    jump_end_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
}

// 0x0051C460 (0x0051C640 the deleting one)
AttributeTracker::~AttributeTracker() = default;

// 0x0051C470
int AttributeTracker::get_goal_status(int index) { return core::at(goals_, index).cur_standing_; }

// 0x0051C480
void AttributeTracker::set_goal_status(int index, int status)
{
    core::at(goals_, index).cur_standing_ = status;
    if (status == 1)
        goal_done_ = true;
}

// 0x0051C670
void AttributeTracker::configure_from_data(script::Struct*)
{
    goal_done_ = false;
    activated_ = false;
    cur_phase_ = 0;
    last_state_ = 0;
    air_phase_ = 0;
    air_height_ = 0.0f;
    air_best_ = 0;
    air_start_points_ = 0;
    air_start_multiplier_ = 0.0f;
    jump_phase_ = 0;
    count_a80_ = 0;
    count_a84_ = 0;
    air_score_ = 0;
    first_update_ = true;
    skater_ = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(entity()->ident()));
    if (!skater_) {
        skater_state_ = nullptr;
        physics_control_ = nullptr;
    } else {
        skater_state_ = core::down_cast<RiderPoseState>(skater_->fetch_module(kSkaterState));
        physics_control_ = core::down_cast<RiderBoardPhysics>(skater_->fetch_module(kSkaterPhysicsControl));
    }
    difficulty_ = static_cast<goals::GoalManager*>(game_flow())->get_skill_tier(true);
    debug::printf("STATSManager: difficulty ====================== %i\n",
                  difficulty_); // "STATSManager: difficulty ... %i"
    if (script::fetch_int_value(kBUMPSTATS) == 1)
        raise_stats_ = true;

    script::Array* goals = script::fetch_list(kStatsGoals);
    const int count = static_cast<int>(goals->extent());
    for (int i = 0; i < count; ++i) {
        auto* source = goals->struct_at(static_cast<uint32_t>(i));
        Goal& goal = core::at(goals_, i);
        uint32_t checksum = 0;
        if (source->has_entry_named(kGoalType)) {
            source->read_hash_value(kGoalType, &checksum, 1);
            goal.kind_ = checksum;
        }
        if (source->has_entry_named(kStattype)) {
            source->read_hash_value(kStattype, &checksum, 1);
            goal.attr_key_ = checksum;
        }
        script::Array* by_difficulty = nullptr;
        if (source->has_entry_named(kValue)) {
            by_difficulty = nullptr;
            source->fetch_list(kValue, &by_difficulty, 1);
            goal.aim_point_ = static_cast<int>(by_difficulty->fetch_part(static_cast<uint32_t>(difficulty_)));
        }
        if (source->has_entry_named(kValueString)) {
            by_difficulty = nullptr;
            source->fetch_list(kValueString, &by_difficulty, 1);
            goal.text_ = by_difficulty->find_inline_text(static_cast<uint32_t>(difficulty_));
        }
        if (source->has_entry_named(kTextName)) {
            const char* name = ""; // ""
            source->fetch_text(kTextName, &name, 1);
            goal.label_ = name;
        }
        if (source->has_entry_named(kComplete)) {
            int ignored = 0;
            source->fetch_int_value(kComplete, &ignored, 1);
            goal.cur_standing_ = 0;
        }
        if (source->has_entry_named(kValueTrick)) {
            by_difficulty = nullptr;
            source->fetch_list(kValueTrick, &by_difficulty, 1);
            goal.trick_ = by_difficulty->fetch_part(static_cast<uint32_t>(difficulty_));
        }
        if (source->has_entry_named(kValueTaps)) {
            by_difficulty = nullptr;
            source->fetch_list(kValueTaps, &by_difficulty, 1);
            goal.value20_ = by_difficulty->fetch_part(static_cast<uint32_t>(difficulty_));
        }
        goal.met_ = false;
    }
}

// 0x0051C950
bool AttributeTracker::is_operative()
{
    if (!skater_)
        return false;
    void* skate = skate_module();
    if (!goals::game_mode()->is_true(kIsCareer))
        return false;
    if (!activated_ || static_cast<modules::Skate*>(skate)->level() == kLoadMainMenu)
        return false;
    // (Without a name the original compares what its slot held: this.)
    uint32_t name = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    first_profile()->details_params()->read_hash_value(kName, &name, 1);
    return name == kCustom;
}

// 0x0051C9D0
void AttributeTracker::set10(const char* text, int score, int)
{
    if (is_operative() && score != value14_)
        last_text_ = text;
}

// 0x0051CA00
void AttributeTracker::get_reset()
{
    if (!is_operative())
        return;
    launch_script(kStatsMessageBail);
    for (Goal& goal : goals_)
        if (goal.cur_standing_ == 0 && goal.met_)
            goal.met_ = false;
    air_phase_ = 0;
    jump_phase_ = 0;
    count_a80_ = 0;
    count_a84_ = 0;
    jump_start_ = jump_end_;
    air_start_ = air_end_;
}

// 0x0051CAC0
void AttributeTracker::set_value58(float value)
{
    if (is_operative())
        value58_ = value;
}

// 0x0051CAE0
void AttributeTracker::total_of(uint32_t name)
{
    if (!is_operative())
        return;
    if (name == kGrabTrick)
        ++count_a84_;
    else if (name == kFlipTrick)
        ++count_a80_;
}

// 0x0051CBF0
void AttributeTracker::announce_met_goals()
{
    if (!is_operative())
        return;
    RiderIdentity* profile = first_profile();
    gsl::owner<script::Struct*> params = script::new_struct();
    bool announced = false;
    for (Goal& goal : goals_) {
        if (goal.cur_standing_ != 0 || !goal.met_)
            continue;
        if (!announced) {
            launch_script(kStatsMessageLand);
            announced = true;
        }
        const uint32_t stat = goal.attr_key_;
        goal.cur_standing_ = 1;
        // The stat's name (a global string per stat).
        uint32_t text = 0;
        switch (stat) {
        case kFlipSpeed:
            text = kFlipIncreaseText;
            break;
        case kStateAir:
            text = kAirIncreaseText;
            break;
        case kSwitch:
            text = kSwitchIncreaseText;
            break;
        case kLipBalance:
            text = kLipIncreaseText;
            break;
        case kOllie:
            text = kOllieIncreaseText;
            break;
        case kRun:
            text = kRunIncreaseText;
            break;
        case kSpinName:
            text = kSpinIncreaseText;
            break;
        case kManualBalance:
            text = kManualIncreaseText;
            break;
        case kSpeed:
            text = kSpeedIncreaseText;
            break;
        case kRailBalance:
            text = kRailIncreaseText;
            break;
        default:
            text = 0;
            break;
        }
        if (text)
            params->append_word(kTextName, script::fetch_text(text));
        params->put_int(checksums::Name{kGotIt}, 1);
        launch_script(kShowStatsMessage, params);
        if (raise_stats_)
            profile->set_ability_level(stat, profile->fetch_tally(stat) + 1);
    }
    if (announced) {
        debug::printf("updating skater stats\n"); // "updating skater stats"
        core::down_cast<Rider>(skater_)->set_stats_from_profile(profile);
        if (!goal_done_ && static_cast<goals::GoalManager*>(game_flow())->may_begin_objective()) {
            launch_script(kBeatFirstStatGoal);
            goal_done_ = true;
        }
        launch_script(kStatPlayWinSound);
    }
    script::free_struct(params);
    count_a80_ = 0;
    count_a84_ = 0;
}

// 0x0051CE80
void AttributeTracker::meet_goal(int index)
{
    if (!is_operative())
        return;
    const script::StructPtr params = script::make_struct();
    Goal& goal = core::at(goals_, index);
    params->append_word(kString, goal.label_);
    params->put_int(checksums::Name{kValue}, goal.aim_point_);
    if (goal.text_)
        params->append_word(kVstring, goal.text_);
    params->put_int(checksums::Name{kIndex}, index);
    launch_script(kShowStatsMessage, params.get());
    goal.met_ = true;
}

// 0x0051CF90
void AttributeTracker::tick_frame()
{
    if (!is_operative())
        return;
    if (first_update_) {
        air_phase_ = 0;
        jump_phase_ = 0;
        first_update_ = false;
        return;
    }
    auto* score = static_cast<modules::PointTally*>(point_tally(skater_));
    points_held_ = truncate(score->pot() * static_cast<double>(score->multiplier()));
    const float multiplier = score->multiplier();
    multiplier_ = multiplier;
    path_vertices_ = multiplier == 0.0f ? 0 : truncate(points_held_ / static_cast<double>(multiplier));

    const int physics_state = skater_state_->cur_phase();
    cur_phase_ = kStateOther;
    const RiderPoseState* control_state = physics_control_->pose();
    const maths::Vec4& pos = entity()->at_point();
    if (!control_state->in_vehicle() && control_state->physics_mode() == objects::RiderPoseState::PhysicsMode::kSkating) {
        if (skater_state_->flag(PoseFlag::kVertAir).on) {
            cur_phase_ = kStateLaunched;
            if (last_state_ != kStateLaunched) {
                air_phase_ = 1;
                air_height_ = 0.0f;
                air_start_ = pos;
                air_start_points_ = path_vertices_;
                air_start_multiplier_ = multiplier_;
                debug::printf("vert_start_score = %i\n", points_held_); // "vert_start_score = %i"
                air_best_ = 0;
            } else {
                const double height = static_cast<double>(pos.y) - air_start_.y;
                if (height > air_height_)
                    air_height_ = static_cast<float>(height);
                air_score_ = truncate((static_cast<double>(multiplier) - air_start_multiplier_) *
                                      get_minus(path_vertices_, air_start_points_));
                air_best_ = std::max(air_best_, air_score_);
            }
        } else if (last_state_ == kStateLaunched) {
            if (air_phase_ == 1) {
                air_end_ = pos;
                const double gained = static_cast<double>(multiplier_) - air_start_multiplier_;
                const int air_score = truncate(get_minus(path_vertices_, air_start_points_) * gained);
                air_score_ = air_score;
                air_phase_ = 2;
                if (gained > 1.0f && air_score != 0)
                    last_air_combo_score_ = air_score;
            }
            air_score_ = 0;
        } else {
            air_phase_ = 0;
            air_height_ = 0.0f;
            air_best_ = 0;
        }

        if (physics_state == 1 && !skater_state_->flag(PoseFlag::kVertAir).on) {
            cur_phase_ = kStateAir;
            if (last_state_ != kStateAir && !skater_state_->flag(PoseFlag::kBailing).on) {
                jump_phase_ = 1;
                jump_start_ = pos;
            }
        } else if (last_state_ == kStateAir) {
            if (jump_phase_ == 1) {
                jump_end_ = pos;
                jump_phase_ = 2;
            }
        } else {
            jump_phase_ = 0;
        }
    } else {
        jump_phase_ = 0;
    }

    // The open goals.
    bool met = false;
    for (int i = 0; i < kGoals; ++i) {
        const Goal& goal = core::at(goals_, i);
        if (goal.cur_standing_ != 0 || goal.met_)
            continue;
        bool meets = false;
        switch (goal.kind_) {
        case kRise:
            meets = jump_phase_ == 2 &&
                    goal.aim_point_ * static_cast<double>(kFeet) <= static_cast<double>(jump_end_.y) - jump_start_.y;
            break;
        case kDrop:
            meets = jump_phase_ == 2 &&
                    goal.aim_point_ * static_cast<double>(kFeet) <= static_cast<double>(jump_start_.y) - jump_end_.y;
            break;
        case kAirCombo:
            if (air_phase_ == 2 && air_best_ >= goal.aim_point_) {
                script::StructPtr params = script::make_struct();
                params->put_int(checksums::Name{kScore}, air_best_);
                launch_script(kShowVertComboMessage, params.get());
                params.reset();
                meets = true;
            }
            break;
        case kAirHeight:
            meets = air_phase_ != 0 && goal.aim_point_ * static_cast<double>(kFeet) <= air_height_;
            break;
        case kAirValue58:
            meets = air_phase_ != 0 && goal.aim_point_ <= static_cast<double>(value58_);
            break;
        case kComboScore:
            meets = points_held_ >= goal.aim_point_;
            break;
        case kMultiplier:
            meets = goal.aim_point_ <= static_cast<double>(multiplier_);
            break;
        case kCountA80:
            meets = count_a80_ >= goal.aim_point_;
            break;
        case kCountA84:
            meets = count_a84_ >= goal.aim_point_;
            break;
        case kTrickCount: {
            const int target = goal.aim_point_;
            const uint32_t trick = goal.trick_;
            if (count_tricks(score, trick, 0, 0, static_cast<int>(goal.value20_)) >= target) {
                meets = true;
            } else if (trick == kManual) {
                const int manuals = count_tricks(score, trick, 0, 0, 1);
                meets = manuals + count_tricks(score, kManualPairTrick, 0, 0, 1) >= target;
            }
            break;
        }
        case kLeapReach:
            if (jump_phase_ == 2) {
                const maths::Vec4 start = {.x = jump_start_.x, .y = 0.0f, .z = jump_start_.z, .w = 1.0f};
                const maths::Vec4 end = {.x = jump_end_.x, .y = 0.0f, .z = jump_end_.z, .w = 1.0f};
                const double distance = span_to(end, start);
                meets = goal.aim_point_ * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kAirDistance:
            if (air_phase_ == 2) {
                auto target = static_cast<float>(goal.aim_point_);
                air_end_.y = 0.0f;
                air_start_.y = 0.0f;
                const double distance = span_to(air_end_, air_start_);
                meets = target * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kScore562370:
            // (it reads the goal's +0x18, the 0xCD021293 entry)
            meets = score->count_tricks_named(goal.text_) >= goal.aim_point_;
            break;
        default:
            break;
        }
        if (meets) {
            meet_goal(i);
            met = true;
        }
    }
    if (met)
        launch_script(kShowedStatMessageSound);
    last_state_ = cur_phase_;
    value58_ = 0.0f;
    score_before_combo_ = points_held_;
    last_points_ = path_vertices_;
    last_multiplier_ = multiplier_;
}

// 0x0051D6A0
void AttributeTracker::check_balance_time(checksums::Name trick_type_name, float time, int)
{
    const uint32_t trick_type = trick_type_name.value();
    if (!is_operative())
        return;
    bool met = false;
    for (int i = 0; i < kGoals; ++i) {
        const Goal& goal = core::at(goals_, i);
        if (goal.cur_standing_ != 0 || goal.met_)
            continue;
        const int target = goal.aim_point_;
        uint32_t kind = 0;
        if (trick_type == kSlide || trick_type == kGrind)
            kind = kGrindTime;
        else if (trick_type == kNoseManual || trick_type == kManual)
            kind = kManualTime;
        else if (trick_type == kLip)
            kind = kLipTime;
        else
            continue;
        if (goal.kind_ != kind || !(static_cast<float>(target) <= time))
            continue;
        meet_goal(i);
        met = true;
    }
    if (met)
        launch_script(kShowedStatMessageSound);
}

// --- member functions ---

// 0x0051C4A0
bool AttributeTracker::re_init(script::Struct* params, script::Script*)
{
    reapply_from_data(params);
    return true;
}

// 0x0051CB20
bool AttributeTracker::reset_goals(script::Struct*, script::Script*)
{
    get_reset();
    return true;
}

// 0x0051C4C0
bool AttributeTracker::activate_goals(script::Struct*, script::Script*)
{
    activated_ = true;
    launch_script(kConsoleUnhide);
    return true;
}

// 0x0051C4F0
bool AttributeTracker::deactivate_goals(script::Struct*, script::Script*)
{
    activated_ = false;
    launch_script(kConsoleHide);
    return true;
}

// 0x0051CB30
bool AttributeTracker::get_goal_status_member(script::Struct* params, script::Script* script)
{
    int index = 0;
    params->fetch_int_value(0u, &index, 1);
    script->args()->put_int(checksums::Name{kStatus}, core::at(goals_, index).cur_standing_);
    return true;
}

// 0x0051C520
bool AttributeTracker::unlock_am_goals(script::Struct*, script::Script*)
{
    for (Goal& goal : goals_)
        if (goal.cur_standing_ == 2)
            goal.cur_standing_ = 0;
    return true;
}

// 0x0051C550
bool AttributeTracker::unlock_pro_goals(script::Struct*, script::Script*)
{
    for (Goal& goal : goals_)
        if (goal.cur_standing_ == 3)
            goal.cur_standing_ = 0;
    return true;
}

// 0x0051CB70
bool AttributeTracker::get_last_air_combo_score(script::Struct*, script::Script* script) const
{
    script->args()->put_int(checksums::Name{kLastAirComboScore}, last_air_combo_score_);
    return true;
}

// 0x0051C580
bool AttributeTracker::clear_last_air_combo_score(script::Struct*, script::Script*)
{
    last_air_combo_score_ = 0;
    return true;
}

} // namespace objects
