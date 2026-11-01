// NOT ORIGINAL - THAW's BMX ridden in THUG2, phase 3: THAW's bike branches in the skater's core physics
// (game/rider/thaw_bmx.h). Our addition at the user's request - not THUG2's code and not PartyMod's.
//
// Each function here is THAW's bike branch (read from THAW's reference build, game/rider/thaw_bmx.h) re-expressed
// on THUG2's component layout: THAW's fields are at other offsets (its +0x9C frame length is THUG2's +0x8C, its pad's
// first sixteen buttons are THUG2's, its timed flags are THUG2's shifted by 8 from +0x68 and by 0x10 from +0xA8 ...).
// Our THUG2 functions call these only while live_now(core) - the feature on and the skater on the bike; otherwise
// nothing here runs.
//
// THAW's pad has buttons THUG2's hasn't: the movement stick's four directions as buttons (LEFTUP / LEFTDOWN / LEFTLEFT
// / LEFTRIGHT, its pad +0x280 / +0x2A0 / +0x2C0 / +0x2E0, made by its input server from the stick: any way off the
// middle past the dead band - thaw_bmx_sticks.cpp keeps them beside THUG2's pad; begin_frame keeps their press times).
// On the bike the d-pad is the camera (THAW turns the stick's d-pad off: bike_start's SetAnalogStickActiveForMenus 0),
// so nothing here reads the d-pad as the stick; on the keyboard the movement keys push the stick all the way.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/checksums.h"
#include "engine/gfx/anim_blend.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "platform/platform_config.h"
#include "platform/game_clock.h"
#include "game/skate/point_tally.h"
#include "platform/run_log.h"
#include "game/rider/skater_state.h"
#include "game/rider/trick_component.h"
#include "engine/objects/input_component.h"
#include "game/rider/skater_score.h"
#include "game/rider/balance_trick.h"

namespace skater::thaw_bmx {

namespace {

template <typename T>
T& get_at(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
maths::Vec4& vector_at(const void* object, uint32_t offset) { return get_at<maths::Vec4>(object, offset); }

// The core physics component's fields (THUG2's; THAW's offset in the comment).
// balance, its lean at +0x54)
// The timed flags (THUG2's; THAW's in the comment).
// THUG2's state flags against THAW's (their offsets in THAW's flags block): kCrouched 0x20, kVertAir 0x30, kOnVert
// 0x40, kSpin 0x70 (the air's turn to face the ramp allowed), kBailing 0x78 (apply_ground_friction's flag), kAcidDrop
// 0x80, kNoSecondSpin 0xD8.
using objects::PoseFlag;

// The skater object's.

// The states.
constexpr int kStateAir = 1;
constexpr int kStateRail = 4;
constexpr int kStateBikeStall = 6; // (THAW's stall state - core_fields::kBikeStall)

// The pad's buttons (offsets in THUG2's pad = THAW's).
constexpr uint32_t kUpButton = 0x00, kDownButton = 0x20, kLeftButton = 0x40, kRightKeyHash = 0x60;
constexpr uint32_t kL1 = 0x80, kR1 = 0xE0, kCircle = 0x140, kSquare = 0x160;

// Checksums.
constexpr uint32_t kManualKind = 0xEF24413B;              // Manual (the balance trick's kind)
constexpr uint32_t kBrakeAcceleration = 0xE5D73F6F;       // Physics_Brake_Acceleration
constexpr uint32_t kFrontBrakeAcceleration = 0x6235801C;  // Physics_Front_Brake_Acceleration
constexpr uint32_t kSkidAcceleration = 0xECFA301C;        // Physics_Brake_Skid_Acceleration
constexpr uint32_t kStandingKickSpeed = 0x4610C2E3;       // Skater_Max_Standing_Kick_Speed_Stat
constexpr uint32_t kCrouchedKickSpeed = 0x92E0247C;       // Skater_Max_Crouched_Kick_Speed_Stat
constexpr uint32_t kGroundRotation = 0x374E056B;          // Physics_Ground_Rotation
constexpr uint32_t kGroundSharpRotation = 0x7933A8EF;     // Physics_Ground_Sharp_Rotation
constexpr uint32_t kMaxSpeedStat = 0x2EACDDB3;            // Skater_Max_Speed_Stat
constexpr uint32_t kGroundNoLeanTime = 0xE978B078;        // Physics_Ground_No_Lean_Time
constexpr uint32_t kNoBalanceFrames = 0x291AC9B9;         // Physics_manual_no_balance_frames
constexpr uint32_t kGroundLeanStat = 0xA2133ED7;          // Physics_Ground_Lean_stat
constexpr uint32_t kAirLeanStat = 0xCBFDD841;             // Physics_Air_Lean_stat
constexpr uint32_t kAirLeanFastStat = 0x8A93C338;         // Physics_Air_Lean_fast_stat
constexpr uint32_t kAirNoLeanTime = 0x5F70EF46;           // Physics_Air_No_Lean_Time
constexpr uint32_t kAirRampLeanTime = 0x12CD2D14;         // Physics_Air_Ramp_Lean_Time
constexpr uint32_t kAirRotationStat = 0x4957DB43;         // Physics_Air_Rotation_stat
constexpr uint32_t kAirRotationFastStat = 0x3FBC1DC9;     // Physics_Air_Rotation_fast_stat
constexpr uint32_t kAirNoRotateTime = 0xA092B2BE;         // Physics_Air_No_Rotate_Time
constexpr uint32_t kAirRampRotateTime = 0x5D756349;       // Physics_Air_Ramp_Rotate_Time
constexpr uint32_t kAutoturnCancelTime = 0xABDD3395;      // skater_autoturn_cancel_time
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;           // spin_count_slop
constexpr uint32_t kEyeLevelStat = 0x542CF0C7;            // Skater_default_head_height
constexpr uint32_t kGroundSnapUp = 0xE4D79235;            // Physics_Ground_Snap_Up
constexpr uint32_t kGroundSnapDown = 0x899BA3D0;          // Physics_Ground_Snap_Down
constexpr uint32_t kGroundStickAngle = 0xEF161C2A;        // Ground_stick_angle
constexpr uint32_t kGroundStickAngleForward = 0x4138283E; // Ground_stick_angle_forward
constexpr uint32_t kForwardCollisionHeight = 0xD4205C9B;  // Skater_First_Forward_Collision_Height
constexpr uint32_t kVertMaxSpeedTime = 0x0F230065;        // Skater_Vert_Max_Speed_Time
constexpr uint32_t kVertMaxMaxSpeedStat = 0x9CCDD908;     // Skater_Vert_Max_Max_Speed_Stat
constexpr uint32_t kLanded = 0x532B16EF;                  // Landed (the exception)
constexpr uint32_t kEndo = 0x4C961392;                    // endo
constexpr uint32_t kNose = 0x7F03932C;                    // nose

// THAW's scripts these spawn (bike_scripts.qb; phase 4 ported THAW's own: tools/pipeline/thaw_bmx/bike_scripts.ts).
constexpr const char* kEnterManualScript = "bike_enter_manual";
constexpr const char* kEnterNoseManualScript = "bike_enter_nosemanual";
constexpr const char* kBailManualScript = "bail_bike_manual";
constexpr const char* kBailNoseManualScript = "bail_bike_nosemanual";
constexpr const char* kExitEndoScript = "bike_exit_endo";
constexpr const char* kForceBailScript = "force_bike_bail";
constexpr const char* kReleaseXupScript = "bike_release_partial_xup";
// THAW's globals (physics.qb), copied into the overlay by ride.ts; THAW's values when they're missing.
constexpr const char* kNormalLerpSpeedBike = "Normal_Lerp_Speed_Bike";
constexpr float kNormalLerpSpeedBikeThaw = 0.5f;
constexpr const char* kForwardJumpBoost = "bike_forward_jump_boost";
constexpr float kForwardJumpBoostThaw = 100.0f;
constexpr const char* kGrindTrickList = "BikeGrindTrickList";
constexpr const char* kStallSlipTime = "stall_slip_time";
constexpr uint32_t kRailTolerance = 0xD0688D4E; // Rail_Tolerance

// THAW's constants.
constexpr float kRadiansPerDegree = 0.0174532924f;  // 0x006DB474
constexpr float kDegreesPerRadian = 57.2957764f;    // 0x006DB478
constexpr float kLerpRate = 60.0f;                  // 0x006DCB7C (THUG2's 0x00645EA0)
constexpr float kWheelBase = 2.0f * kWheelOffset;   // 0x006DB3A4 x 0x006DD14C
constexpr float kWheelFeelDown = -200.0f;           // 0x006E15F8
constexpr float kCrease = 0.2f;                     // 0x006DD574
constexpr float kSlow = 10.0f;                      // 0x006DCFC4
constexpr float kEndoSpeed = 300.0f;                // 0x006DD664
constexpr float kManualBalance = 50.0f;             // 0x006DD6C8 (ResetBikeManualAngle's)
constexpr float kManualTop = 100.0f;                // 0x006DCBC0
constexpr float kEndoExitSpeed = 100.0f;            // 0x006DCBC0
constexpr float kEndoBack = 200.0f;                 // 0x006EDCF4
constexpr float kJumpManualPitch = 0.7f;            // 0x006EDDB0 / 0x006EDDA8 (doubles)
constexpr double kStickScale = 0.0078125;           // 0x006EDD88 (1 / 128)
constexpr float kSlowTurnRamp = 1.0f / 600.0f;      // 0x006EDD90
constexpr int kSlowTurnRampMs = 600;                // 0x258
constexpr float kBackwardsLean = 0.0005f;           // 0x006EDD80
constexpr double kSteerBlendBase = 1.0;             // THAW 0x006DB3A8 (double)
constexpr double kSteerBlendScale = 0.5;            // THAW 0x006E4DE8 (double)
constexpr float kSteerInTime = 0.4f;                // (bike_playturnanim's BlendPeriod: THAW's tree blends the turn in)
constexpr float kSteerOutTime = 0.3f;               // (ours: THUG2's PlayAnim's blend, as the idle that follows plays)
constexpr const char* kSteerSubset = "ThawBmxTurn"; // (ours: the anim subset the turn animation plays in)
constexpr uint32_t kSubsetRemove = 0xAFC2493E;      // Obj_RemoveAnimSubset
constexpr uint32_t kSubsetId = 0x5CFCCF35;          // SubsetId
constexpr int kStateGround = 0;
constexpr double kAcidDropBack = 0.1;        // 0x006EDD70
constexpr double kStallSteepUp = 0.95;       // THAW 0x006EDDC0 (the stall's rail too steep under this up y)
constexpr int kStallSteepFrames = 10;        // (the frames of it before the stall turns into a grind)
constexpr float kStallSlipTimeThaw = 130.0f; // (THAW's stall_slip_time, physics.qb - while the overlay lacks it)

// --- helpers -----------------------------------------------------------------------------------------------------

objects::GameEntity* object_of(const SkaterPhysicsModule* core) { return core->entity_; }
int state_of(const SkaterPhysicsModule* core) { return core->state_->cur_phase_; }
objects::PadSnapshot* pad_state_of(const SkaterPhysicsModule* core) { return &core->input_->pad_primary_; }
float get_frame_length(const SkaterPhysicsModule* core) { return core->frame_length_; }

int get_now() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }
uint32_t get_since(int stamp)
{
    return static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
           (static_cast<uint32_t>(objects::g_clock_offset) - static_cast<uint32_t>(stamp));
}
// A button's time since it was pressed (0x0046B920), as THAW's float compares take it (unsigned).
float get_held_ms(objects::ControlButton* button)
{
    return static_cast<float>(static_cast<uint32_t>(button->get_held_millis()));
}
float get_held_ms(const StickButton& button) { return static_cast<float>(get_since(button.press_time_)); }

// 0x004FDED0 (not rewritten): the physics state changed.
void change_phase(SkaterPhysicsModule* core, int state)
{
    static_cast<SkaterPhysicsModule*>(core)->set_physics_state(state);
}

void clear_timed_flag(const SkaterPhysicsModule* core, PoseFlag flag) { core->state_->set_flag(flag, 0); }

float tuning_float_lookup(uint32_t name, const SkaterPhysicsModule* core)
{
    return objects::read_tuning_float(name, 1, static_cast<objects::GameEntity*>(object_of(core)));
}
int physics_integer(uint32_t name, const SkaterPhysicsModule* core)
{
    return objects::read_tuning_int(name, 1, static_cast<objects::GameEntity*>(object_of(core)));
}
float get_stat(uint32_t name, const SkaterPhysicsModule* core)
{
    return static_cast<objects::Rider*>(object_of(core))->get_scaled_physics(name);
}
// A global of THAW's physics.qb that THUG2 hasn't: THAW's value while the overlay lacks it.
float thaw_global(const char* name, float thaw)
{
    const float value = script::fetch_float(name);
    return value != 0.0f ? value : thaw;
}

double dot3(const maths::Vec4& a, const maths::Vec4& b)
{
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}
double length3(const maths::Vec4& v) { return std::sqrt(dot3(v, v)); }
bool moving_backwards(const SkaterPhysicsModule* core)
{
    const objects::GameEntity* object = object_of(core);
    return dot3(object->vel_, object->basis_frame_.row[2]) < 0.0;
}
float get_pace(const SkaterPhysicsModule* core) { return static_cast<float>(length3(object_of(core)->vel_)); }

// The movement stick's raw axis (THAW's pad +0x348 x / +0x34C y: THUG2's stick_[2] / [3], -128..127). (The d-pad
// isn't read: on the bike it's the camera, as THAW's.)
float stick_raw(const SkaterPhysicsModule* core, int axis) { return pad_state_of(core)->stick_[2 + axis]; }
// Its dead-banded value (THAW's pad +0x358 / +0x35C: THUG2's scaled_stick_[2] / [3], -1..1).
float stick_scaled(const SkaterPhysicsModule* core, int axis) { return pad_state_of(core)->scaled_stick_[2 + axis]; }

// THAW 0x005C8B30 (the input component's pad, +0x1910's +0x20): on the bike a brake button held - L1 the front brake
// (+0x528 set), R1 the rear; the skater's is the d-pad's down.
bool brake_held(const SkaterPhysicsModule* core, BikeState& state)
{
    const objects::PadSnapshot* pad = &core->input_->pad_primary_;
    state.front_brake_ = false;
    if (pad->shoulder_left_a_.held_down_) {
        state.front_brake_ = true;
        return true;
    }
    return pad->shoulder_right_a_.held_down_ != 0;
}

void run_bike_script(const SkaterPhysicsModule* core, const char* name, script::Struct* params = nullptr)
{
    const uint32_t checksum = checksums::hash_from_text(name);
    if (!script::script_exists(checksum)) {
        static uint32_t s_warned = 0;
        if (s_warned != checksum) {
            get_log("thaw_bmx: the script %s isn't loaded (make the overlay again: tools/pipeline/thaw_bmx/convert.ts, "
                    "make_menu_level.ts)",
                    name);
            s_warned = checksum;
        }
        return;
    }
    script::launch_script(checksum, params, static_cast<objects::Entity*>(object_of(core)), false, name);
}

bool wheels_on()
{
    static const bool kEnabled = config_int("features", "thaw_bmx_wheels", 1) != 0;
    return kEnabled;
}

void turn_velocity(maths::Vec4& velocity, float angle)
{
    // (as THUG2's turn_on_ground: x87 sine and cosine)
    const double s = std::sin(static_cast<double>(angle));
    const double c = std::cos(static_cast<double>(angle));
    const double x = velocity.x;
    const double z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

void update_stick_button(StickButton& button, bool down)
{
    if (down && !button.down)
        button.press_time_ = get_now();
    button.down = down;
}

// A wheel's feeler's result (feel_wheel, below).
struct WheelHit {
    bool hit = false;
    maths::Vec4 point{}, normal{};
    uint16_t flags = 0;
    int why = -1; // ([debug] thaw_bmx_wheels's log: -1 no hit, else Bike::WheelFeelResult's)
    float distance = 0.0f, reach = 0.0f, turn = 0.0f, limit = 0.0f;
};
WheelHit feel_wheel(SkaterPhysicsModule* core, maths::Vec4& wheel, const maths::Vec4& last, const maths::Vec4& up,
                    float above);

} // namespace

// --- the maths -----------------------------------------------------------------------------------------------------

namespace bike {

int brake_choice(bool manual, bool nose, bool front_brake, bool endo, bool balance_is_manual)
{
    // THAW 0x005CACA0: in a manual the brake on the wheel in the air does nothing (the rear one in a nose manual, the
    // front one in a manual).
    if (manual && ((nose && !front_brake) || (!nose && front_brake)))
        return 0;
    int choice = 1;
    if (front_brake && endo)
        choice = 2;
    if (!manual && !balance_is_manual)
        choice = 3;
    return choice;
}

float ground_turn_rate(float speed, float max_speed, float rotation, float sharp_rotation, bool braking, bool manual,
                       float pad_axis_x, int held_ms, bool backwards, float balance_lean)
{
    float rate = 0.0f;
    if (pad_axis_x != 0.0f) {
        if (!braking) {
            // Sharper the slower: from the sharp rotation at a standstill to the rotation at the max speed.
            double fraction = static_cast<double>(speed) / max_speed;
            if (fraction > 1.0)
                fraction = 1.0f;
            rate = static_cast<float>((1.0 - static_cast<float>(fraction)) * (sharp_rotation - rotation) + rotation);
        } else {
            rate = sharp_rotation;
        }
        if (speed < kSlow && !manual) {
            // Nearly stopped: ramped in over the first 600 ms the d-pad's button is held, by direction only.
            if (held_ms < kSlowTurnRampMs)
                rate = static_cast<float>(held_ms) * rate * kSlowTurnRamp;
            if (pad_axis_x > 0.0f)
                rate = rate * -1.0f;
        } else {
            rate = static_cast<float>(-static_cast<double>(pad_axis_x) * kStickScale * rate);
        }
    }
    if (backwards)
        rate = manual ? 0.0f - rate : balance_lean * kBackwardsLean;
    return rate;
}

float steer_blend(float rate, float sharp_rotation)
{
    if (sharp_rotation == 0.0f) // (ours: THAW divides by it regardless - bicycle_physics' is 6.5)
        return static_cast<float>(kSteerBlendBase * kSteerBlendScale);
    float share = rate / sharp_rotation;
    if (share < 0.0f)
        share = 0.0f - share;
    return static_cast<float>((share + kSteerBlendBase) * kSteerBlendScale);
}

float steer_in(float in, bool on, float frame_length)
{
    const float out = on ? in + frame_length / kSteerInTime : in - frame_length / kSteerOutTime;
    return out > 1.0f ? 1.0f : (out < 0.0f ? 0.0f : out);
}

ManualOutput manual_step(const ManualInput& in)
{
    ManualOutput out = {in.manual, in.nose, in.endo, in.angle, in.no_balance_frames_, ManualEvent::kNone};
    if (!out.manual) {
        // Steering: no manual. The stick held back (forward) longer than Physics_Ground_No_Lean_Time: a manual (a nose
        // manual); the front brake at speed going forwards: an endo (a nose manual on the front brake).
        if (in.left || in.right)
            return out;
        auto start = [&](bool nose, bool endo, ManualEvent event) {
            out.nose = nose;
            out.manual = true;
            out.angle = 1.0f;
            out.endo = endo;
            out.no_balance_frames_ = in.no_balance_start_;
            out.event = event;
        };
        if (in.up && static_cast<float>(in.up_held_) > in.no_lean_time_)
            start(true, false, ManualEvent::kEnterNoseManual);
        else if (in.down && static_cast<float>(in.down_held_) > in.no_lean_time_)
            start(false, false, ManualEvent::kEnterManual);
        else if (in.front_brake_ && in.speed > kEndoSpeed && in.forward)
            start(true, true, ManualEvent::kEnterEndo);
        else
            return out;
    }
    // The first frames of a manual: no balance yet.
    if (out.no_balance_frames_ > 0) {
        --out.no_balance_frames_;
        return out;
    }
    // The angle's rate: the stick (the other way round in a nose manual); in an endo up while the front brake is
    // held, else down; else falling away from 50 (down below it, over above it).
    float rate = 0.0f;
    if ((in.up || in.down) && !out.endo) {
        rate = out.nose ? 0.0f - in.stick_y_ * in.lean : in.stick_y_ * in.lean;
    } else if (out.endo) {
        rate = in.front_brake_ ? in.lean : static_cast<float>(in.lean * -1.0);
    } else if (out.angle > kManualBalance) {
        rate = in.lean;
    } else if (!(out.angle < 0.0f)) {
        rate = static_cast<float>(in.lean * -1.0);
    }
    if (out.angle > kManualTop && rate > 0.0f) {
        // Over the top: the bail.
        out.event = ManualEvent::kBail;
        out.angle = kManualBalance;
        out.manual = false;
        out.endo = false;
        return out;
    }
    if (rate == 0.0f)
        return out;
    out.angle = in.frame_length_ * rate + out.angle;
    if (out.angle > 0.0f)
        return out;
    // Back on both wheels: an endo slow enough rolls back (THAW's bike_exit_endo), else Landed.
    out.event = out.endo && in.speed < kEndoExitSpeed ? ManualEvent::kExitEndo : ManualEvent::kLanded;
    out.manual = false;
    out.angle = kManualBalance;
    out.endo = false;
    return out;
}

bool jump_boosts(const maths::Vec4& velocity, float stick_y)
{
    const double horizontal =
        std::sqrt(static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x);
    return horizontal == 0.0 && !(stick_y > 0.0f);
}

float settle_air_pitch(float pitch)
{
    // THAW 0x005D7CB0: the pitch's whole degrees round a turn (truncated, the sign kept), a tenth of the way back to
    // the nearest upright each frame.
    auto angle = static_cast<float>(static_cast<int>(pitch) % 360);
    if (angle == 0.0f)
        return 0.0f;
    if (angle > 0.0f)
        return angle > 180.0f ? (360.0f - angle) * 0.1f : angle * -0.1f;
    return -180.0f > angle ? (-360.0f - angle) * 0.1f : angle * -0.1f;
}

int grind_row(float stick_y)
{
    if (-1.0f <= stick_y) {
        if (1.0f < stick_y)
            return 125.0f <= stick_y ? 4 : 3;
        return 0;
    }
    return stick_y <= -125.0f ? 2 : 1;
}

int grind_column(bool left_side, bool sideways, bool backwards, bool* over_grind)
{
    // THAW 0x005CF2F0 (with THAW's caller 0x005E2B39: its third and fourth arguments are both the sideways flag).
    const bool side = backwards ? !left_side : left_side;
    if (over_grind)
        *over_grind = side != sideways;
    return (side ? 1 : 0) | (sideways ? 2 : 0);
}

bool vert_window(float since, float window, bool* raise)
{
    // THAW 0x005CA8F0: the vert max-max speed while the time since is under the window; the heavy friction only once
    // it's past it (THAW asks the clock again for the friction - the same frame's time).
    *raise = since < window;
    return window < since;
}

maths::Vec4 wheel_base_normal(const maths::Vec4& normal, const maths::Vec4& front, const maths::Vec4& rear)
{
    maths::Vec4 base = {front.x - rear.x, front.y - rear.y, front.z - rear.z, front.w - rear.w};
    base.make_unit();
    auto along = static_cast<float>(dot3(base, normal));
    maths::Vec4 result = {normal.x - base.x * along, normal.y - base.y * along, normal.z - base.z * along,
                          normal.w - base.w * along};
    result.make_unit();
    return result;
}

maths::Vec4 wheel_base_up(const maths::Vec4& right, const maths::Vec4& front, const maths::Vec4& rear,
                          const maths::Vec4& hit)
{
    maths::Vec4 d = {front.x - rear.x, front.y - rear.y, front.z - rear.z, 0.0f};
    if (length3(d) > 0.0)
        d.make_unit();
    maths::Vec4 r = {right.x, right.y, right.z, 0.0f};
    if (length3(r) > 0.0)
        r.make_unit();
    maths::Vec4 up = {r.y * d.z - r.z * d.y, r.z * d.x - d.z * r.x, d.y * r.x - r.y * d.x, 0.0f};
    if (length3(up) > 0.0)
        up.make_unit();
    // The way that's nearer the ground's own normal.
    auto flip = [](float u, float n) { return std::fabs(n - u) > std::fabs(u + n); };
    if (flip(up.x, hit.x) || flip(up.y, hit.y) || flip(up.z, hit.z))
        up = {-up.x, -up.y, -up.z, 0.0f};
    return up;
}

WheelFeelResult feel_wheel_on(const WheelFeel& in)
{
    // THAW 0x005D4EF0.
    WheelFeelResult out = {false, false, in.wheel, 1, 0.0f, 0.0f, 0.0f, 0.0f};
    // (THAW: the differences and `along` in SSE floats, the distance on the x87)
    const float dx = in.wheel.x - in.point.x;
    const float dy = in.wheel.y - in.point.y;
    const float dz = in.wheel.z - in.point.z;
    auto distance = static_cast<float>(
        std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx));
    const float along = in.up.z * dz + in.up.y * dy + in.up.x * dx; // (how far above the hit, along up)
    const maths::Vec4 on_hit = {in.point.x + in.normal.x, in.point.y + in.normal.y, in.point.z + in.normal.z,
                                in.point.w + in.normal.w};
    if (!in.skatable) {
        if (along < 0.001f) { // 0x006DBD98
            out.move_wheel_ = true;
            out.wheel = on_hit;
        }
        return out;
    }
    maths::Vec4 direction = in.facing;
    if (in.backwards)
        direction = {-direction.x, -direction.y, -direction.z, -direction.w};
    auto into = static_cast<float>(static_cast<double>(direction.z) * in.normal.z +
                                   static_cast<double>(direction.y) * in.normal.y +
                                   static_cast<double>(direction.x) * in.normal.x);
    maths::Vec4 in_hit = direction, in_ground = direction;
    in_hit.align_to_plane(in.normal);
    in_ground.align_to_plane(in.ground_normal_);
    auto turn =
        static_cast<float>(static_cast<double>(in_ground.x) * in_hit.x + static_cast<double>(in_ground.z) * in_hit.z +
                           static_cast<double>(in_ground.y) * in_hit.y);
    auto limit = static_cast<float>(std::cos(static_cast<double>(in.stick_angle_) * kRadiansPerDegree));
    out.distance = distance;
    out.turn = turn;
    out.limit = limit;
    out.why = 2;
    if (into > 0.0f && turn > 0.0f && limit > turn)
        return out;
    out.why = 3;
    if (along > 0.0f) {
        const float cosine = turn < -1.0f ? -1.0f : (turn > 1.0f ? 1.0f : turn);
        const float mx = in.wheel.x - in.last.x;
        const float my = in.wheel.y - in.last.y;
        const float mz = in.wheel.z - in.last.z;
        const double moved =
            std::sqrt(static_cast<double>(mx) * mx + static_cast<double>(mz) * mz + static_cast<double>(my) * my);
        auto reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
        if (in.snap_down_ > reach)
            reach = in.snap_down_;
        out.reach = reach;
        if (distance > reach)
            return out;
    }
    out.why = 0;
    out.ground = true;
    out.move_wheel_ = true;
    out.wheel = on_hit;
    return out;
}

bool stall_across(const maths::Vec4& start_velocity, const maths::Vec4& direction, float tolerance)
{
    // THAW 0x005E2972: the velocity flattened (y 0) and normalised, its dot with the rail's direction (single
    // precision, z first) - across the rail when Rail_Tolerance is more than its size.
    maths::Vec4 flat = start_velocity;
    flat.y = 0.0f;
    flat.make_unit();
    const float dot = direction.z * flat.z + direction.y * flat.y + flat.x * direction.x;
    return tolerance > std::fabs(dot);
}

StallSlipStep stall_slip(uint32_t elapsed, float slip_time, const maths::Vec4& velocity, const maths::Vec4& kept)
{
    // THAW 0x005E7092: the time since the state changed (unsigned) against stall_slip_time; under it, the kept
    // velocity x (1 / stall_slip_time) taken off - every component, w too.
    StallSlipStep out = {false, velocity};
    if (static_cast<double>(elapsed) > static_cast<double>(slip_time)) {
        out.stall = true;
        return out;
    }
    const float inverse = 1.0f / slip_time;
    out.velocity.x = velocity.x - kept.x * inverse;
    out.velocity.y = velocity.y - kept.y * inverse;
    out.velocity.z = velocity.z - kept.z * inverse;
    out.velocity.w = velocity.w - kept.w * inverse;
    return out;
}

bool stall_steep_step(float up_y, int* frames)
{
    // THAW 0x005E3821: the up's y (the ground normal, just set to it) under 0.95 (a double): the count goes up to
    // 10; at 10 back to grinding, the count to 0. Never reset otherwise.
    if (!(static_cast<double>(up_y) < kStallSteepUp))
        return false;
    if (*frames < kStallSteepFrames) {
        ++*frames;
        return false;
    }
    *frames = 0;
    return true;
}

maths::Vec4 stall_facing(const maths::Vec4& facing, const maths::Vec4& direction)
{
    // THAW 0x005E354E: the facing's dot with the rail's direction (single precision, z first); not at least 0:
    // the direction the other way (w too).
    const float dot = facing.z * direction.z + facing.y * direction.y + facing.x * direction.x;
    const float sign = dot >= 0.0f ? 1.0f : -1.0f;
    return {direction.x * sign, direction.y * sign, direction.z * sign, direction.w * sign};
}

} // namespace bike

// --- the frame -----------------------------------------------------------------------------------------------------

void begin_frame(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    // THAW 0x005E8684: the wheel points as the frame begins.
    state.last_front_wheel_ = state.front_wheel_;
    state.last_rear_wheel_ = state.rear_wheel_;
    // THAW's movement stick buttons (its pad's LEFTUP ...: thaw_bmx_sticks.cpp, from the stick alone).
    const void* pad = pad_state_of(core);
    auto stick = [pad](int index) {
        return static_cast<objects::ControlButton*>(pad_stick_button(pad, stick_button_name(index)))->held_down_ != 0;
    };
    update_stick_button(state.stick_up_, stick(kLeftUp));
    update_stick_button(state.stick_down_, stick(kLeftDown));
    update_stick_button(state.stick_left_, stick(kLeftLeft));
    update_stick_button(state.stick_right_, stick(kLeftRight));
}

void end_frame(SkaterPhysicsModule* core)
{
    {
        // (ours, a diagnostic: the bike's position or velocity no longer finite - logged once)
        static bool s_logged = false;
        const objects::GameEntity* object = object_of(core);
        const maths::Vec4& p = object->at_point_;
        const maths::Vec4& v = object->vel_;
        if (!s_logged &&
            !(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(v.x) &&
              std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) + std::fabs(v.y) + std::fabs(v.z) < 1e6f)) {
            s_logged = true;
            const BikeState& s = *state_of(core, true);
            get_log("thaw_bmx: not finite - state %d pos %g %g %g vel %g %g %g front %g %g %g rear %g %g %g manual %d",
                    state_of(core), p.x, p.y, p.z, v.x, v.y, v.z, s.front_wheel_.x, s.front_wheel_.y, s.front_wheel_.z,
                    s.rear_wheel_.x, s.rear_wheel_.y, s.rear_wheel_.z, s.manual ? 1 : 0);
        }
    }
    // (phase 4, thaw_bmx_tricks.cpp: THAW 0x00620780 - the manual's pose from its angle)
    manual_pose(core);
    // (THAW's turn branch's share - its TurnBlend - on the turn animation's subset)
    steer_anim(core);
    // THAW 0x005C9210: the x-up held with circle - in the air, or in a manual (not a nose manual); let go: out of it.
    BikeState& state = *state_of(core, true);
    if (!state.xup)
        return;
    if (!(state_of(core) == kStateAir || (state.manual && !state.nose_manual_)))
        return;
    if (core->input_->pad_primary_.button_round_.held_down_)
        return;
    state.xup = false;
    // (THAW spawns it on the skater and runs it at once - 0x004322D0, no parameters; here run at once, its body
    // spawning itself.)
    run_bike_script(core, kReleaseXupScript);
}

// --- the turn's lean -----------------------------------------------------------------------------------------------

namespace {

// The turn animation's anim subset (a channel of `object`'s animation blender), or null. (The script member functions
// Obj_AnimSubsetExists / Obj_SetAnimSubsetWeight / Obj_RemoveAnimSubset are entries of the blender's member function
// table, which a call by name - GameEntity::invoke_method_by_name - never reaches: the blender is asked directly.)
gfx::BlendChannel* steer_channel(objects::GameEntity* object, objects::AnimBlenderComponent** blender)
{
    *blender =
        object ? static_cast<objects::AnimBlenderComponent*>(object->fetch_module(objects::AnimBlenderComponent::kType))
               : nullptr;
    return *blender ? (*blender)->find_channel(checksums::hash_from_text(kSteerSubset)) : nullptr;
}

// The subset removed (Obj_RemoveAnimSubset SubsetId=ThawBmxTurn).
void remove_steer_channel(objects::AnimBlenderComponent* blender)
{
    script::Struct* params = script::spawn_param_bag();
    if (!params)
        return;
    params = new (params) script::Struct;
    params->put_name_key(kSubsetId, checksums::hash_from_text(kSteerSubset));
    blender->remove_channel(kSubsetRemove, params, nullptr);
    params->get_clear();
    script::delete_struct(params);
}

} // namespace

void set_steer_anim(SkaterPhysicsModule* core, bool on) { state_of(core, true)->steer_anim_ = on; }

void steer_anim_off(SkaterPhysicsModule* core)
{
    BikeState* state = state_of(core, false);
    if (!state)
        return;
    state->steer_anim_ = false;
    if (state->steer_in_ == 0.0f) // (nothing shown: a subset left at weight 0 shows nothing)
        return;
    state->steer_in_ = 0.0f;
    auto* skater = static_cast<objects::GameEntity*>(object_of(core));
    for (objects::GameEntity* object : {skater, bike_object_of(skater)}) {
        objects::AnimBlenderComponent* blender = nullptr;
        if (steer_channel(object, &blender))
            remove_steer_channel(blender);
    }
}

void steer_anim(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    // (off the ground - an ollie, a bail, a grind - the turn goes as THAW's next branch blends in)
    const bool on = state.steer_anim_ && state_of(core) == kStateGround;
    const float before = state.steer_in_;
    state.steer_in_ = bike::steer_in(state.steer_in_, on, get_frame_length(core));
    if (before == 0.0f && state.steer_in_ == 0.0f)
        return;
    auto* skater = static_cast<objects::GameEntity*>(object_of(core));
    for (objects::GameEntity* object : {skater, bike_object_of(skater)}) {
        objects::AnimBlenderComponent* blender = nullptr;
        gfx::BlendChannel* channel = steer_channel(object, &blender);
        if (!channel)
            continue;
        if (state.steer_in_ == 0.0f) {
            remove_steer_channel(blender);
        } else {
            // (Obj_SetAnimSubsetWeight's blend_to, no blend period: the weight at once)
            const float weight = state.steer_in_ * state.steer_blend_;
            auto bits = std::bit_cast<uint32_t>(weight);
            channel->blend_to(bits, 0.0f);
        }
    }
}

// --- on the ground -------------------------------------------------------------------------------------------------

void get_on_ground(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    state.air_pitch_ = 0.0f; // (THAW 0x005E4200: +0x66C)
    state.tire_tap_ = false; // (THAW 0x005E4752)
}

bool ground_gravity_applies(SkaterPhysicsModule* core, const maths::Vec4& gravity)
{
    BikeState& state = *state_of(core, true);
    if (!(get_pace(core) < kSlow))
        return true;
    maths::Vec4 direction = gravity;
    if (length3(direction) > 0.0)
        direction.make_unit();
    if (!(dot3(object_of(core)->basis_frame_.row[2], direction) < 0.0))
        return true;
    return state.manual && !brake_held(core, state);
}

bool should_brake(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    const objects::PadSnapshot* pad = &core->input_->pad_primary_;
    state.front_brake_ = false;
    if (pad->shoulder_left_a_.held_down_)
        state.front_brake_ = true;
    else if (!pad->shoulder_right_a_.held_down_)
        return false;
    return core->can_brake_ != 0;
}

void get_brake(SkaterPhysicsModule* core)
{
    const BikeState& state = *state_of(core, true);
    const bool balance_is_manual = core->balance_->trick_ == kManualKind;
    const int choice =
        bike::brake_choice(state.manual, state.nose_manual_, state.front_brake_, state.endo, balance_is_manual);
    if (choice == 0)
        return;
    core->braking_ = 1;
    if (static_cast<SkaterPhysicsModule*>(core)->may_turn_on_slope()) { // (turning on the slope instead)
        core->braking_ = 0;
        return;
    }
    static const uint32_t kNames[] = {0, kBrakeAcceleration, kFrontBrakeAcceleration, kSkidAcceleration};
    const float slowing = get_frame_length(core) * tuning_float_lookup(kNames[choice], core);
    maths::Vec4& velocity = object_of(core)->vel_;
    if (length3(velocity) < static_cast<double>(slowing) * 2.0) {
        velocity = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    maths::Vec4 slow = velocity;
    slow.set_length(0.0f - slowing);
    velocity.x += slow.x;
    velocity.y += slow.y;
    velocity.z += slow.z;
    velocity.w += slow.w;
}

bool can_kick(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    if (core->no_kick_ || brake_held(core, state) || state.manual || moving_backwards(core))
        return false;
    const bool crouched = core->state_->flag(PoseFlag::kCrouched).on || core->crouch_stats_;
    const float most = get_stat(crouched ? kCrouchedKickSpeed : kStandingKickSpeed, core);
    return !(most < get_pace(core));
}

bool may_accelerate(SkaterPhysicsModule* core)
{
    const BikeState& state = *state_of(core, true);
    return !moving_backwards(core) && !state.manual;
}

void turn_on_ground(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    objects::PadSnapshot* pad = pad_state_of(core);
    const float pad_axis_x = stick_raw(core, 0);
    int held = 0;
    // (THAW 0x005D6342: its &pad->right_side_ / +0x40 - the d-pad's right / left, not the stick's buttons; with the
    // stick off the d-pad on the bike, the time since the d-pad was last pressed, as THAW's)
    if (pad_axis_x != 0.0f)
        held = static_cast<int>(pad->right_side_.held_down_ ? pad->right_side_.get_held_millis()
                                                            : pad->left_side_.get_held_millis());
    const float sharp = tuning_float_lookup(kGroundSharpRotation, core);
    const float rate = bike::ground_turn_rate(get_pace(core), get_stat(kMaxSpeedStat, core),
                                              tuning_float_lookup(kGroundRotation, core), sharp,
                                              pad_axis_x != 0.0f && brake_held(core, state), state.manual, pad_axis_x,
                                              held, moving_backwards(core), core->balance_->manual_.lean_);
    // THAW 0x005D61F0's +0x570: the turn animation's share, every frame on the ground.
    state.steer_blend_ = bike::steer_blend(rate, sharp);
    {
        // ([debug] thaw_bmx_steer=1, ours: a line each ground frame - riding backwards and the turn's lean)
        static const bool kLog = config_int("debug", "thaw_bmx_steer", 0) != 0;
        if (kLog) {
            const maths::Vec4& v = object_of(core)->vel_;
            const maths::Vec4& f = object_of(core)->basis_frame_.row[2];
            get_log("thaw_bmx steer: speed %.0f along %.2f stick %.0f rate %.3f blend %.2f in %.2f anim %d manual %d",
                    get_pace(core), length3(v) > 0.0 ? dot3(v, f) / length3(v) : 0.0, pad_axis_x, rate,
                    state.steer_blend_, state.steer_in_, state.steer_anim_ ? 1 : 0, state.manual ? 1 : 0);
        }
    }
    // The skid's wait (BikeExitSkid's 60 frames) counted down on the ground.
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): the original's, as written
    if (state.skid_timer_ > 0 && --state.skid_timer_ == 0)
        state.can_skid_ = true;
    if (rate == 0.0f)
        return;
    const float step = get_frame_length(core) * rate;
    core->last_spin_backside_ = step > 0.0f ? 1 : 0;
    objects::GameEntity* object = object_of(core);
    if (!core->lock_velocity_)
        turn_velocity(object->vel_, step);
    object->basis_frame_.yaw_locally(step);
    core->display_matrix_.yaw_locally(step);
    // THAW 0x005C9710: the wheel points along the new facing.
    set_wheel_points(state, object->at_point_, object->basis_frame_.row[2]);
    // THAW 0x005D6579 (the manual flag +0x525): in a manual the turn
    // is the manual's spin - added to the trick's rotation and shown (THAW's 0x0056D060 with the air pitch: THUG2's
    // rotation display, the spin only). Otherwise both wheels are felt for again at their new places (THAW 0x005D4EF0
    // from the snap's feeler start to 200 below): either off the ground, the turn is undone (the skater's and the
    // display's matrices turned back - not the velocity - and the wheel points put back along the old facing).
    if (state.manual) {
        float& total = core->trick_->spin_total_;
        total = step * kDegreesPerRadian + total;
        void* display = core->score_->points_held_;
        static_cast<modules::PointTally*>(display)->advance_turn(static_cast<int>(static_cast<int64_t>(total)));
        return;
    }
    const maths::Vec4 up = object->basis_frame_.row[1];
    const float above = tuning_float_lookup(kGroundSnapUp, core);
    const bool front_down = feel_wheel(core, state.front_wheel_, state.last_front_wheel_, up, above).hit;
    const bool rear_down = feel_wheel(core, state.rear_wheel_, state.last_rear_wheel_, up, above).hit;
    if (front_down && rear_down)
        return;
    object->basis_frame_.yaw_locally(-step);
    core->display_matrix_.yaw_locally(-step);
    set_wheel_points(state, object->at_point_, object->basis_frame_.row[2]);
}

void manual_pitch(SkaterPhysicsModule* core)
{
    // (THAW returns while its balance trick component's +0x1C is set; THUG2's layout differs there: here, while one
    // of THUG2's balance tricks runs.)
    if (core->balance_->trick_ != 0)
        return;
    BikeState& state = *state_of(core, true);
    objects::GameEntity* object = object_of(core);
    bike::ManualInput in = {};
    in.manual = state.manual;
    in.nose = state.nose_manual_;
    in.endo = state.endo;
    in.angle = state.manual_angle_;
    in.no_balance_frames_ = state.no_balance_frames_;
    in.up = state.stick_up_.down;
    in.down = state.stick_down_.down;
    in.left = state.stick_left_.down;
    in.right = state.stick_right_.down;
    in.up_held_ = static_cast<int>(get_held_ms(state.stick_up_));
    in.down_held_ = static_cast<int>(get_held_ms(state.stick_down_));
    in.front_brake_ = core->input_->pad_primary_.shoulder_left_a_.held_down_ != 0;
    in.stick_y_ = stick_scaled(core, 1);
    in.speed = get_pace(core);
    in.forward = dot3(object->vel_, object->basis_frame_.row[2]) > 0.0;
    in.no_lean_time_ = tuning_float_lookup(kGroundNoLeanTime, core);
    in.no_balance_start_ = physics_integer(kNoBalanceFrames, core);
    in.lean = get_stat(kGroundLeanStat, core);
    in.frame_length_ = get_frame_length(core);
    const bike::ManualOutput out = bike::manual_step(in);
    state.manual = out.manual;
    state.nose_manual_ = out.nose;
    state.endo = out.endo;
    state.manual_angle_ = out.angle;
    state.no_balance_frames_ = out.no_balance_frames_;
    switch (out.event) {
    case bike::ManualEvent::kEnterNoseManual:
        run_bike_script(core, kEnterNoseManualScript);
        break;
    case bike::ManualEvent::kEnterManual:
        run_bike_script(core, kEnterManualScript);
        break;
    case bike::ManualEvent::kEnterEndo: {
        state.endo_velocity_ = object->vel_;
        script::Struct* params = script::spawn_param_bag();
        params->put_int(kEndo, 1);
        run_bike_script(core, kEnterNoseManualScript, params);
        script::delete_struct(params);
        break;
    }
    case bike::ManualEvent::kBail:
        run_bike_script(core, in.nose ? kBailNoseManualScript : kBailManualScript);
        break;
    case bike::ManualEvent::kLanded:
        static_cast<objects::Entity*>(object)->post_own_event(kLanded, nullptr);
        break;
    case bike::ManualEvent::kExitEndo: {
        // Rolled back the way it came at 200 (the endo's velocity flattened, turned round).
        maths::Vec4 back = state.endo_velocity_;
        back.y = 0.0f;
        back.make_unit();
        maths::Vec4& velocity = object->vel_;
        velocity = {-back.x * kEndoBack, -back.y * kEndoBack, -back.z * kEndoBack, back.w};
        run_bike_script(core, kExitEndoScript);
        break;
    }
    default:
        break;
    }
}

// --- jumping -------------------------------------------------------------------------------------------------------

void jump_boost(SkaterPhysicsModule* core)
{
    // THAW 0x005DDDC0: standing still (no horizontal speed at all) with the stick not pulled back: pushed along the
    // facing.
    objects::GameEntity* object = object_of(core);
    maths::Vec4& velocity = object->vel_;
    if (!bike::jump_boosts(velocity, stick_raw(core, 1)))
        return;
    const float boost = thaw_global(kForwardJumpBoost, kForwardJumpBoostThaw);
    const maths::Vec4& facing = object->basis_frame_.row[2];
    velocity.x += facing.x * boost;
    velocity.y += facing.y * boost;
    velocity.z += facing.z * boost;
    velocity.w += facing.w * boost;
}

void jump_pitch(SkaterPhysicsModule* core)
{
    // THAW 0x005DDDC0: out of a manual, pitched by the manual's angle (0.7 of it; a nose manual the other way).
    const BikeState& state = *state_of(core, true);
    if (!state.manual)
        return;
    auto angle = static_cast<float>(state.manual_angle_ * (state.nose_manual_ ? kJumpManualPitch : -kJumpManualPitch) *
                                    kRadiansPerDegree);
    object_of(core)->basis_frame_.pitch_locally(angle);
    core->display_matrix_.pitch_locally(angle);
}

// --- in the air ----------------------------------------------------------------------------------------------------

void on_air(SkaterPhysicsModule* core)
{
    // THAW 0x005DF880: no manual in the air.
    state_of(core, true)->manual = false;
}

void air_wheel_points(SkaterPhysicsModule* core)
{
    // THAW 0x005E0476 (the on-bike flag), after the display matrix
    // is the skater's again: the wheel points from the skater's position and facing.
    const objects::GameEntity* object = object_of(core);
    set_wheel_points(*state_of(core, true), object->at_point_, object->basis_frame_.row[2]);
}

void air_rotation(SkaterPhysicsModule* core)
{
    // THAW 0x005D7CB0.
    objects::RiderPoseState* flags = core->state_;
    if (flags->flag(PoseFlag::kBailing).on)
        return;
    BikeState& state = *state_of(core, true);
    objects::GameEntity* object = object_of(core);
    objects::PadSnapshot* pad = &core->input_->pad_primary_;
    // The stick (a diagonal normalised).
    float sx = stick_scaled(core, 0), sy = stick_scaled(core, 1);
    if (sx != 0.0f && sy != 0.0f) {
        const double length = std::sqrt(static_cast<double>(sx) * sx + static_cast<double>(sy) * sy);
        sx = static_cast<float>(sx / length);
        sy = static_cast<float>(sy / length);
    }
    const bool fast = pad->shoulder_left_a_.held_down_ != 0;
    // The pitch: up / down, after the no-lean time, ramped in.
    float pitch = 0.0f;
    const float lean = get_stat(fast ? kAirLeanFastStat : kAirLeanStat, core);
    const float no_lean = tuning_float_lookup(kAirNoLeanTime, core);
    const float ramp_lean = tuning_float_lookup(kAirRampLeanTime, core);
    int held = 0;
    if (sy != 0.0f) {
        pitch = 0.0f - lean * sy;
        held = static_cast<int>(get_held_ms(pad->up_dir_.held_down_ ? &pad->up_dir_ : &pad->btn_down_));
    }
    auto held_f = static_cast<float>(held);
    if (!(no_lean < held_f))
        pitch = 0.0f;
    if (ramp_lean > held_f)
        pitch = (held_f - no_lean) * pitch / (ramp_lean - no_lean);
    // (On the bike the air's turn to face the ramp is off.)
    clear_timed_flag(core, PoseFlag::kSpin);
    // The spin: left / right, likewise.
    float spin = 0.0f;
    if (!flags->flag(PoseFlag::kNoSecondSpin).on) {
        const float rotation = get_stat(fast ? kAirRotationFastStat : kAirRotationStat, core);
        int held2 = 0;
        if (sx != 0.0f) {
            spin = 0.0f - rotation * sx;
            held2 = static_cast<int>(get_held_ms(pad->left_side_.held_down_ ? &pad->left_side_ : &pad->right_side_));
        }
        auto held_2f = static_cast<float>(held2);
        if (tuning_float_lookup(kAutoturnCancelTime, core) < held_2f)
            clear_timed_flag(core, PoseFlag::kSpin);
        const float no_rotate = tuning_float_lookup(kAirNoRotateTime, core);
        const float ramp_rotate = tuning_float_lookup(kAirRampRotateTime, core);
        if (!(no_rotate < held_2f))
            spin = 0.0f;
        if (ramp_rotate > held_2f)
            spin = (held_2f - no_rotate) * spin / (ramp_rotate - no_rotate);
    }
    // The pitch so far (degrees), or let go: back towards upright - kept first as it was (THAW +0x18C0,
    // 0x005D80FC, which the landing's PitchingForward / PitchingBackward read; the ground leaves it).
    state.last_air_pitch_ = state.air_pitch_;
    bool pitched = false;
    float pitch_step = 0.0f;
    if (pitch != 0.0f) {
        pitch_step = get_frame_length(core) * pitch;
        state.air_pitch_ = pitch_step * kDegreesPerRadian + state.air_pitch_;
        pitched = true;
    } else {
        const float settle = bike::settle_air_pitch(state.air_pitch_);
        if (settle != 0.0f) {
            state.air_pitch_ += settle;
            pitch_step = settle * kRadiansPerDegree;
            pitched = true;
        }
    }
    if (pitched && !flags->flag(PoseFlag::kVertAir).on) {
        // (In vert THAW pitches the model only - through its model component, which THUG2's hasn't: not here.)
        object->basis_frame_.pitch_locally(pitch_step);
        core->display_matrix_.pitch_locally(pitch_step);
    }
    float spin_step = 0.0f;
    if (spin != 0.0f) {
        core->last_spin_backside_ = spin > 0.0f ? 1 : 0;
        spin_step = get_frame_length(core) * spin;
        object->basis_frame_.yaw_locally(spin_step);
        core->display_matrix_.yaw_locally(spin_step);
    }
    if (spin_step != 0.0f || pitch_step != 0.0f) {
        // The trick's rotation total, shown as THUG2's Spin shows it (short of a whole turn by more than the slop
        // in vert, not; THAW shows the flips too - THUG2's display has no place for them).
        float& total = core->trick_->spin_total_;
        total = spin_step * kDegreesPerRadian + total;
        bool show = true;
        if (flags->flag(PoseFlag::kVertAir).on && !flags->flag(PoseFlag::kAcidDrop).on && !pitched) {
            const double snap = 360.0f - tuning_float_lookup(kSpinCountSlop, core) + 0.1f;
            show = snap <= std::fabs(static_cast<double>(total));
        }
        if (show) {
            void* display = core->score_->points_held_;
            static_cast<modules::PointTally*>(display)->advance_turn(static_cast<int>(static_cast<int64_t>(total)));
        }
    }
    // The rider's head into something (not on vert): the bail.
    if (!flags->flag(PoseFlag::kVertAir).on) {
        const maths::Vec4& position = object->at_point_;
        const maths::Vec4& up = object->basis_frame_.row[1];
        const float head = tuning_float_lookup(kEyeLevelStat, core);
        core->probe_start_ = position;
        core->probe_end_ = {position.x + up.x * head, position.y + up.y * head, position.z + up.z * head,
                            position.w + up.w * head};
        if (static_cast<SkaterPhysicsModule*>(core)->probe_touches_zone(0x10, 0))
            run_bike_script(core, kForceBailScript);
    }
}

// --- the ground under the wheels -------------------------------------------------------------------------------------

namespace {

// One wheel's feeler (THAW 0x005D4EF0): THUG2's own test of the ground under the skater (check_for_ground's, 0x00509BD0)
// for one wheel. The wheel's feeler from above the wheel to 200 below it: no hit, no ground. A hit that isn't ground
// (not skatable): the wheel moved out of it (the hit point plus its normal) when it's (nearly) at or below it - no
// ground. Ground: not kept to when it turns away too steeply from the ground the bike is on (the travel direction -
// the facing, turned round rolling backwards - laid into both planes further apart than Ground_stick_angle, or
// Ground_stick_angle_forward with up held, while it runs into the hit's plane), nor when it's further away than the
// wheel could have dropped this frame at that angle (its move since `last`, the frame's start, times the angle's
// tangent - at least Physics_Ground_Snap_Down). Kept: the wheel at the hit point plus its normal. (Phase 3 took any
// skatable hit up to a wheel base below: the bike stuck to the ground over crests and off ledges THAW leaves.)
WheelHit feel_wheel(SkaterPhysicsModule* core, maths::Vec4& wheel, const maths::Vec4& last, const maths::Vec4& up,
                    float above)
{
    WheelHit result;
    core->probe_start_ = {wheel.x + up.x * above, wheel.y + up.y * above, wheel.z + up.z * above,
                          wheel.w + up.w * above};
    core->probe_end_ = {wheel.x + up.x * kWheelFeelDown, wheel.y + up.y * kWheelFeelDown,
                        wheel.z + up.z * kWheelFeelDown, wheel.w + up.w * kWheelFeelDown};
    if (!static_cast<SkaterPhysicsModule*>(core)->probe_touches_zone(0x10, 0))
        return result;
    const maths::Vec4 point = core->feeler_.contact_pos_;
    const maths::Vec4 normal = core->feeler_.surface_dir_;
    bike::WheelFeel feel = {};
    feel.wheel = wheel;
    feel.last = last;
    feel.up = up;
    feel.point = point;
    feel.normal = normal;
    feel.ground_normal_ = core->floor_normal_;
    feel.facing = object_of(core)->basis_frame_.row[2];
    feel.backwards = moving_backwards(core);
    feel.skatable = core->hit_skatable_ != 0;
    feel.stick_angle_ = tuning_float_lookup(
        core->input_->pad_primary_.up_dir_.held_down_ ? kGroundStickAngleForward : kGroundStickAngle, core);
    feel.snap_down_ = tuning_float_lookup(kGroundSnapDown, core);
    const bike::WheelFeelResult out = bike::feel_wheel_on(feel);
    result.why = out.why;
    result.distance = out.distance;
    result.reach = out.reach;
    result.turn = out.turn;
    result.limit = out.limit;
    if (out.move_wheel_)
        wheel = out.wheel;
    if (!out.ground)
        return result;
    result.hit = true;
    result.point = point;
    result.normal = normal;
    result.flags = core->feeler_.hit_.face_flags_;
    return result;
}

// THAW 0x005D9650: the wheels pinned to the ground under them.
void snap_wheels(SkaterPhysicsModule* core, BikeState& state, WheelHit& front, WheelHit& rear, bool& main_hit)
{
    objects::GameEntity* object = object_of(core);
    const maths::Vec4 up = object->basis_frame_.row[1];
    const maths::Vec4 facing = object->basis_frame_.row[2];
    const float above = tuning_float_lookup(kGroundSnapUp, core);
    front = feel_wheel(core, state.front_wheel_, state.last_front_wheel_, up, above);
    rear = feel_wheel(core, state.rear_wheel_, state.last_rear_wheel_, up, above);
    // A crease between the two (the normals more than ~78 degrees apart): the leading wheel's hit dropped.
    if (front.hit && rear.hit && dot3(front.normal, rear.normal) < kCrease) {
        if (!moving_backwards(core))
            front.hit = false;
        else
            rear.hit = false;
    }
    if (front.hit)
        state.front_wheel_ = front.point;
    if (rear.hit)
        state.rear_wheel_ = rear.point;
    // Under the skater (the component's own feeler, kept for the ground check after).
    const maths::Vec4 position = object->at_point_;
    core->probe_start_ = {position.x + up.x * above, position.y + up.y * above, position.z + up.z * above,
                          position.w + up.w * above};
    core->probe_end_ = {position.x + up.x * kWheelFeelDown, position.y + up.y * kWheelFeelDown,
                        position.z + up.z * kWheelFeelDown, position.w + up.w * kWheelFeelDown};
    main_hit = static_cast<SkaterPhysicsModule*>(core)->probe_touches_zone(0x10, 0);
    if (main_hit && tuning_float_lookup(kGroundSnapDown, core) < core->feeler_.reach_)
        main_hit = false;
    const maths::Vec4 main_point = core->feeler_.contact_pos_;
    // One wheel down: the other a wheel base from it - towards the ground under the skater, else along the facing.
    auto towards = [&](const maths::Vec4& from) {
        maths::Vec4 d = main_hit ? maths::Vec4{main_point.x - from.x, main_point.y - from.y, main_point.z - from.z,
                                               main_point.w - from.w}
                                 : facing;
        if (main_hit)
            d.make_unit();
        return d;
    };
    if (rear.hit && !front.hit) {
        const maths::Vec4 d = towards(state.rear_wheel_);
        state.front_wheel_ = {state.rear_wheel_.x + d.x * kWheelBase, state.rear_wheel_.y + d.y * kWheelBase,
                              state.rear_wheel_.z + d.z * kWheelBase, state.rear_wheel_.w + d.w * kWheelBase};
    } else if (front.hit && !rear.hit) {
        // (THAW subtracts here too - the rear a wheel base on from the front, past it, when the ground under the
        // skater was found: its slip; the rear goes back from the front, as the other case.)
        const maths::Vec4 d =
            main_hit ? towards(state.front_wheel_) : maths::Vec4{-facing.x, -facing.y, -facing.z, -facing.w};
        state.rear_wheel_ = {state.front_wheel_.x + d.x * kWheelBase, state.front_wheel_.y + d.y * kWheelBase,
                             state.front_wheel_.z + d.z * kWheelBase, state.front_wheel_.w + d.w * kWheelBase};
    }
    // Something between the rider and a wheel: the wheel there (out by its normal); nearly stopped, the bike pushed
    // back.
    for (maths::Vec4* wheel : {&state.front_wheel_, &state.rear_wheel_}) {
        render::ProbeRay feeler;
        feeler.line_.start = {position.x + up.x * kWheelOffset, position.y + up.y * kWheelOffset,
                              position.z + up.z * kWheelOffset, position.w + up.w * kWheelOffset};
        feeler.line_.end = {wheel->x + up.x, wheel->y + up.y, wheel->z + up.z, wheel->w + up.w};
        if (feeler.get_hit_shape(true, false)) {
            *wheel = {feeler.contact_pos_.x + feeler.surface_dir_.x, feeler.contact_pos_.y + feeler.surface_dir_.y,
                      feeler.contact_pos_.z + feeler.surface_dir_.z, feeler.contact_pos_.w + feeler.surface_dir_.w};
            maths::Vec4& velocity = object->vel_;
            if (length3(velocity) < kSlow)
                velocity = {-velocity.x, -velocity.y, -velocity.z, -velocity.w};
        }
    }
}

} // namespace

bool rolling_backwards(SkaterPhysicsModule* core) { return moving_backwards(core); }

bool vert_push_held(SkaterPhysicsModule* core, int push_time)
{
    // THAW 0x005DA210 on the bike: LEFTUP held longer than Skater_vert_push_time, neither LEFTLEFT nor LEFTRIGHT nor
    // square nor circle.
    const BikeState& state = *state_of(core, true);
    const objects::PadSnapshot* pad = &core->input_->pad_primary_;
    return state.stick_up_.down && push_time < static_cast<int>(get_since(state.stick_up_.press_time_)) &&
           !state.stick_left_.down && !state.stick_right_.down && !pad->button_box_.held_down_ &&
           !pad->button_round_.held_down_;
}

bool vert_tap_held(SkaterPhysicsModule* core)
{
    // THAW 0x005DA210 on the bike: R1 with LEFTUP or LEFTDOWN, or the tire tap looked for in vert (+0x54C).
    const BikeState& state = *state_of(core, true);
    return (core->input_->pad_primary_.shoulder_right_a_.held_down_ &&
            (state.stick_up_.down || state.stick_down_.down)) ||
           state.tap_armed_;
}

// --- steps, crests and vert (phase 6) --------------------------------------------------------------------------------

float stuck_check_raise(SkaterPhysicsModule* core)
{
    // THAW 0x005C2820: on the bike (+0x524) the start's y plus Skater_First_Forward_Collision_Height.
    return tuning_float_lookup(kForwardCollisionHeight, core);
}

void note_vert_ground(SkaterPhysicsModule* core)
{
    // THAW 0x005E4579: the ground feeler's face vert (THAW's +0x3E9 - read as its feeler check's vert byte, THUG2's
    // +0x229) - the time noted.
    if (!core->hit_vert_)
        return;
    BikeState& state = *state_of(core, true);
    state.vert_ground_noted_ = true;
    state.vert_ground_time_ = get_now();
}

bool vert_speed_allowance(SkaterPhysicsModule* core, float* max_max)
{
    // THAW 0x005CA8F0: within Skater_Vert_Max_Speed_Time of the last vert ground the max-max speed is
    // Skater_Vert_Max_Max_Speed_Stat's, and no heavy air friction over the max speed.
    const BikeState& state = *state_of(core, true);
    if (!state.vert_ground_noted_)
        return true;
    bool raise = false;
    const bool friction = bike::vert_window(static_cast<float>(get_since(state.vert_ground_time_)),
                                            tuning_float_lookup(kVertMaxSpeedTime, core), &raise);
    if (raise)
        *max_max = get_stat(kVertMaxMaxSpeedStat, core);
    return friction;
}

WheelGround wheel_ground_check(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    objects::GameEntity* object = object_of(core);
    // (THAW 0x005E51DA: the wheels only out of a manual, a tire tap and flag +0x78 (THUG2's +0x70); otherwise the
    // skater's ground check with the wheel points at the skater's sides.)
    if (!wheels_on() || state.manual || state.tire_tap_ || core->state_->flag(PoseFlag::kBailing).on) {
        set_wheel_points(state, object->at_point_, object->basis_frame_.row[2]);
        return WheelGround::kWheelsNotUsed;
    }
    set_wheel_points(state, object->at_point_, object->basis_frame_.row[2]);
    WheelHit front, rear;
    bool main_hit = false;
    snap_wheels(core, state, front, rear, main_hit);
    // (THAW's flag +0x40, vert under a wheel; THAW also sets its +0x58 from face flag 0x100 - THUG2's equivalent isn't
    // known: not here.)
    const uint8_t vert = (front.hit && (front.flags & 8)) || (rear.hit && (rear.flags & 8));
    core->state_->set_flag(PoseFlag::kOnVert, vert);
    // Off the ground: no wheel down, or only the trailing one (the leading wheel over an edge).
    const bool backwards = moving_backwards(core);
    if ((!front.hit && !rear.hit) || (front.hit && !rear.hit && backwards) || (!front.hit && rear.hit && !backwards)) {
        static const bool kLog = config_int("debug", "thaw_bmx_wheels", 0) != 0;
        if (kLog) {
            const maths::Vec4& p = object->at_point_;
            get_log(
                "thaw_bmx wheels off: pos %.1f %.1f %.1f speed %.1f%s | front %d why %d dist %.2f reach %.2f turn %.3f "
                "limit %.3f | rear %d why %d dist %.2f reach %.2f turn %.3f limit %.3f",
                p.x, p.y, p.z, get_pace(core), backwards ? " backwards" : "", front.hit ? 1 : 0, front.why,
                front.distance, front.reach, front.turn, front.limit, rear.hit ? 1 : 0, rear.why, rear.distance,
                rear.reach, rear.turn, rear.limit);
        }
        return WheelGround::kWheelsOff;
    }
    // On it: midway between the wheels, the ground normal across the wheel base.
    maths::Vec4& position = object->at_point_;
    position = {
        (state.front_wheel_.x + state.rear_wheel_.x) * 0.5f, (state.front_wheel_.y + state.rear_wheel_.y) * 0.5f,
        (state.front_wheel_.z + state.rear_wheel_.z) * 0.5f, (state.front_wheel_.w + state.rear_wheel_.w) * 0.5f};
    const maths::Vec4 up = bike::wheel_base_up(object->basis_frame_.row[0], state.front_wheel_, state.rear_wheel_,
                                               core->feeler_.surface_dir_);
    static_cast<SkaterPhysicsModule*>(core)->turn_to_normal(up); // (THAW's 0x005D0BD0: THUG2's own)
    if (state_of(core) != kStateAir)
        static_cast<SkaterPhysicsModule*>(core)->keep_upright(); // (THAW 0x005CE810, on the ground)
    return main_hit ? WheelGround::kWheelsOnGround : WheelGround::kWheelsOnGroundNoHit;
}

bool lerp_display_normal(SkaterPhysicsModule* core)
{
    if (state_of(core) == kStateAir)
        return false;
    const BikeState& state = *state_of(core, true);
    // THUG2's 0x004FEA50 with THAW's bike target and speed.
    const maths::Vec4 target = bike::wheel_base_normal(core->floor_normal_, state.front_wheel_, state.rear_wheel_);
    float& t = core->normal_blend_;
    maths::Vec4& display = core->drawn_normal_;
    if (t != 0.0f) {
        const maths::Vec4& old = core->old_normal_;
        if (old == target) {
            t = 0.0f;
        } else {
            t = t - get_frame_length(core) * thaw_global(kNormalLerpSpeedBike, kNormalLerpSpeedBikeThaw) * kLerpRate;
            if (!(t > 0.0f)) {
                t = 0.0f;
                display = target;
            } else {
                display = {(old.x - target.x) * t + target.x, (old.y - target.y) * t + target.y,
                           (old.z - target.z) * t + target.z, (old.w - target.w) * t + target.w};
                display.make_unit();
            }
        }
    }
    maths::Vec4& display_up = core->display_matrix_.row[1];
    if (display_up != display) {
        display_up = display;
        core->display_matrix_.square_up_axes(1);
        const objects::GameEntity* object = object_of(core);
        core->display_matrix_.row[0] = object->basis_frame_.row[0];
        core->display_matrix_.row[2] = object->basis_frame_.row[2];
    }
    return true;
}

// --- grinding, acid drops ------------------------------------------------------------------------------------------

script::Array* grind_trick_row(SkaterPhysicsModule* core, bool left_side, bool sideways, bool backwards,
                               uint32_t* column)
{
    BikeState& state = *state_of(core, true);
    *column = static_cast<uint32_t>(bike::grind_column(left_side, sideways, backwards, &state.over_grind_));
    script::Array* list = script::fetch_list(kGrindTrickList);
    if (!list || list->extent_ == 0)
        return nullptr;
    // THAW reads the raw stick alone here (no d-pad at full deflection, unlike stick_raw).
    auto row = static_cast<uint32_t>(bike::grind_row(pad_state_of(core)->stick_[3]));
    if (row >= list->extent_)
        row = 0;
    return reinterpret_cast<script::Array*>(list->fetch_part(row));
}

bool grind_side(SkaterPhysicsModule*, bool left_side, bool bad_ledge_on_that_side)
{
    return bad_ledge_on_that_side ? !left_side : left_side;
}

bool acid_drop_direction(SkaterPhysicsModule* core, maths::Vec4* direction)
{
    const maths::Vec4& facing = object_of(core)->basis_frame_.row[2];
    maths::Vec4 d = {facing.x, 0.0f, facing.z, facing.w};
    const double length = std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.z) * d.z);
    if (length > 0.0) {
        auto inverse = static_cast<float>(1.0 / length);
        d = {d.x * inverse, 0.0f, d.z * inverse, d.w * inverse};
    }
    if (stick_raw(core, 1) > kAcidDropBack)
        d = {-d.x, -d.y, -d.z, d.w};
    *direction = d;
    return true;
}

// --- the stall ---------------------------------------------------------------------------------------------------

void grind_stall(SkaterPhysicsModule* core, const maths::Vec4& start_velocity, const maths::Vec4& direction)
{
    // THAW 0x005E2935. (THAW: R1 or the skater's ability 0x16 - THAW's skater stalls too; only
    // the bike's is ported. THAW runs the rail's TriggerScript here, with its stall event 0x20000 when R1 is held - our
    // grind start ran THUG2's before getting onto the rail, and THUG2's events have no stall.)
    BikeState& state = *state_of(core, true);
    if (!core->input_->pad_primary_.shoulder_right_a_.held_down_) {
        state.stall_script_ = false;
        return;
    }
    if (bike::stall_across(start_velocity, direction, script::fetch_float(kRailTolerance)))
        change_phase(core, kStateBikeStall);
    state.stall_velocity_ = object_of(core)->vel_;
    state.stall_script_ = true;
}

void rail_stall_slip(SkaterPhysicsModule* core)
{
    // THAW 0x005E7074.
    BikeState* state = state_of(core, false);
    if (!state || !state->stall_script_)
        return;
    if (state_of(core) != kStateRail) {
        state->stall_script_ = false;
        return;
    }
    const float slip_time = thaw_global(kStallSlipTime, kStallSlipTimeThaw);
    maths::Vec4& velocity = object_of(core)->vel_;
    const bike::StallSlipStep step =
        bike::stall_slip(get_since(core->state_changed_at_), slip_time, velocity, state->stall_velocity_);
    if (step.stall) {
        state->stall_script_ = false;
        change_phase(core, kStateBikeStall);
        return;
    }
    velocity = step.velocity;
}

bool stall_steep_rail(SkaterPhysicsModule* core)
{
    // THAW 0x005E3801. (THAW also sets +0x728, and +0x729 off the bike with the facing down the slope - nothing
    // of THAW's reads either.)
    BikeState& state = *state_of(core, true);
    if (!bike::stall_steep_step(object_of(core)->basis_frame_.row[1].y, &state.stall_steep_frames_))
        return false;
    change_phase(core, kStateRail);
    return true;
}

int enter_wheelie_from_air(SkaterPhysicsModule* core, script::Struct* params)
{
    // THAW 0x005CA3B0.
    BikeState& state = *state_of(core, true);
    state.manual = true;
    state.manual_angle_ = kManualBalance;
    state.no_balance_frames_ = physics_integer(kNoBalanceFrames, core);
    state.nose_manual_ = params && params->has_bare_flag(kNose);
    const objects::PadSnapshot* pad = &core->input_->pad_primary_;
    if ((state.nose_manual_ ? pad->shoulder_left_a_ : pad->shoulder_right_a_).held_down_)
        object_of(core)->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    state.tire_tap_ = false;
    return 1;
}

} // namespace skater::thaw_bmx
