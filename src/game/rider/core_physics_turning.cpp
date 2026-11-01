// The skater's turns: on the ground, down slopes, off walls, jumping off rails, natas spins, leaning, recovering
// and staying upright in the air, and the acid drop's rotation.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/balance_trick.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/core/code_address.h"
#include "engine/net/gamenet_manager.h"
#include "game/rider/skater_score.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/skater_state.h"
#include "game/rider/trick_component.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kRailJumpAngle = 0x38505EF3;        // Rail_Jump_Angle (degrees)
constexpr uint32_t kGroundRotation = 0x374E056B;       // Physics_Ground_Rotation
constexpr uint32_t kGroundSharpRotation = 0x7933A8EF;  // Physics_Ground_Sharp_Rotation
constexpr uint32_t kWallBounceMultiplier = 0xE07EE1A9; // Wall_Bounce_Angle_Multiplier
constexpr uint32_t kSlowTurnOnSlopes = 0x7DD5678B;     // Skater_Slow_Turn_on_slopes
constexpr uint32_t kPerfectRail = 0xCD09E062;          // CHEAT_PERFECT_RAIL
constexpr uint32_t kNatasSpinFactor = 0x6A94598A;      // natas_spin_factor
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;        // spin_count_slop
constexpr uint32_t kAirLeanStat = 0xCBFDD841;          // Physics_Air_Lean_stat
constexpr uint32_t kAirNoLeanTime = 0x5F70EF46;        // Physics_Air_No_Lean_Time
constexpr uint32_t kAirRampLeanTime = 0x12CD2D14;      // Physics_Air_Ramp_Lean_Time
constexpr uint32_t kRecoverRateStat = 0xE7116A96;      // Physics_recover_rate_stat
constexpr uint32_t kUprightSidewaysSpeed = 0xABD57877; // skater_upright_sideways_speed (degrees a second)

constexpr float kRadiansPerDegree = 0.0174532924f;       // 0x00645464
constexpr float kMinusDegreesToRadians = -0.0174532924f; // 0x0064BCCC
constexpr float kDegreesPerRadian = 57.2957764f;         // 0x00645468
constexpr float kSlopeTurnSpeed = 0.001f;                // 0x006461D8
constexpr float kSlowGroundTurnSpeed = 10.0f;            // 0x00646710
constexpr float kSlowGroundTurnRate = 0.00166666671f;    // 0x0064BD38 (1 / 600)
constexpr float kHalfPi = 1.57079637f;                   // 0x00645484
constexpr float kPi = 3.14159274f;                       // 0x0064BB7C
constexpr float kNatasSpinScale = 4000.0f;               // 0x0064B8F8
constexpr float kNatasSpinBase = 5.0f;                   // 0x006469AC
constexpr float kAcidDropEase = 1.5f;                    // 0x00646744
constexpr float kAcidDropEaseScale = -2.0f;              // 0x00646970
constexpr float kRecoverFeelerLength = 500.0f;           // 0x0064BC80
constexpr float kRecoverSteepest = 0.2f;                 // 0x006473AC
constexpr float kRecoverUpright = 0.9f;                  // 0x00649FE4
constexpr float kUprightMaxTilt = 0.95f;                 // 0x0064BC7C
constexpr float kUprightRight = 1.2f;                    // 0x0064BD40
constexpr float kUprightLeft = -1.2f;                    // 0x0064BD3C

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void turn_velocity(maths::Vec4& velocity, double angle)
{
    double s = std::sin(angle), c = std::cos(angle);
    double x = velocity.x, z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

// The skater's matrix and its display matrix turned by `angle` about their own y axes (each a make_yaw_matrix and
// a D3DXMatrixMultiply in the original).
void turn_matrices(SkaterPhysicsModule* component, float angle)
{
    object_of(component)->basis_frame_.yaw_locally(angle);
    component->display_matrix_.yaw_locally(angle);
}

} // namespace

// 0x005002D0
void SkaterPhysicsModule::turn_for_rail_jump()
{
    objects::PadInputModule* input = input_;
    float angle;
    if (thaw_bmx::live_now(this)) {
        // Not original: THAW 0x005CF010 - on the bike the stick's left / right (THAW's LEFTLEFT / LEFTRIGHT).
        const thaw_bmx::BikeState* bike = thaw_bmx::state_of(this, false);
        if (bike && bike->stick_left_.down)
            angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kRadiansPerDegree);
        else if (bike && bike->stick_right_.down)
            angle =
                static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kMinusDegreesToRadians);
        else
            return;
    } else if (input->pad_primary_.left_side_.held_down_)
        angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kRadiansPerDegree);
    else if (input->pad_primary_.right_side_.held_down_)
        angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kMinusDegreesToRadians);
    else
        return;
    turn_velocity(entity_->vel_, angle);
    turn_matrices(this, angle);
}

// 0x00503560
void SkaterPhysicsModule::turn_on_ground()
{
    if (thaw_bmx::live_now(this)) { // (not original: the bike's own turning, THAW 0x005D61F0)
        thaw_bmx::turn_on_ground(this);
        return;
    }
    objects::PadSnapshot* pad = &input_->pad_primary_;
    float speed = static_cast<float>(length_unrounded(entity_->vel_));
    objects::GameEntity* object = object_of(this);
    // Left / right (&pad->left_side_ / +0x60); with the sharp turn button (+0x20) harder - and, slower than 10, the turn
    // ramps up over the first 600 ms the button is held.
    objects::ControlButton* button;
    bool sharp;
    double rate;
    if (pad->left_side_.held_down_) {
        button = &pad->left_side_;
        sharp = pad->btn_down_.held_down_ != 0;
        rate = tuning_float_lookup(sharp ? kGroundSharpRotation : kGroundRotation, object);
    } else if (pad->right_side_.held_down_) {
        button = &pad->right_side_;
        sharp = pad->btn_down_.held_down_ != 0;
        rate = -static_cast<double>(tuning_float_lookup(sharp ? kGroundSharpRotation : kGroundRotation, object));
    } else {
        return;
    }
    if (sharp) {
        float full = static_cast<float>(rate);
        if (speed < kSlowGroundTurnSpeed) {
            int held = time_since_press(button);
            rate = held < 600 ? static_cast<double>(held) * full * kSlowGroundTurnRate : full;
        }
    }
    if (rate == 0.0)
        return;
    double step = rate * frame_length_;
    float angle = static_cast<float>(step);
    last_spin_backside_ = step > 0.0 ? 1 : 0;
    if (!lock_velocity_)
        turn_velocity(entity_->vel_, angle);
    turn_matrices(this, angle);
}

// 0x005039F0
float SkaterPhysicsModule::deflect_from_wall(const maths::Vec4* normal, float* turn, float scale)
{
    // Not original: THAW 0x005CCBF0 (the on-bike flag tested first, 0x005CCBF6) - on the bike rolling backwards into a
    // wall: stopped, no bounce (`turn` left as it is).
    if (thaw_bmx::live_now(this) && thaw_bmx::rolling_backwards(this)) {
        entity_->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
        return 0.0f;
    }
    const maths::Vec4& side = entity_->basis_frame_.row[0];
    float cosine =
        static_cast<float>(static_cast<double>(side.z) * normal->z + static_cast<double>(side.y) * normal->y +
                           static_cast<double>(normal->x) * side.x);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    double unrounded = std::acos(static_cast<double>(cosine)); // (the C runtime's, 0x00623630)
    float angle = static_cast<float>(unrounded);
    if (unrounded > kHalfPi)
        angle = angle - kPi;
    double amount = static_cast<double>(script::fetch_float(kWallBounceMultiplier)) * angle * scale;
    *turn = static_cast<float>(amount);
    turn_velocity(entity_->vel_, amount); // (by the unrounded turn)
    turn_matrices(this, *turn);
    return angle;
}

// 0x00501B00
bool SkaterPhysicsModule::turn_down_slope()
{
    if (!may_turn_on_slope())
        return false;
    objects::GameEntity* object = object_of(this);
    // Along the velocity, or the facing when hardly moving: turned towards straight down the slope.
    maths::Vec4 heading = length_unrounded(object->vel_) < kSlopeTurnSpeed ? object->basis_frame_.row[2] : object->vel_;
    maths::Vec4 down = {0.0f, -1.0f, 0.0f, 1.0f};
    down.flatten_onto_plane(floor_normal_);
    object = object_of(this);
    double off = maths::get_turn_around_axis(heading, down, object->basis_frame_.row[1]);
    float angle = static_cast<float>(off);
    float sign = off < 0.0 ? -1.0f : 1.0f;
    double turn = static_cast<double>(tuning_float_lookup(kSlowTurnOnSlopes, object)) * frame_length_ * sign;
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    turn_matrices(this, step);
    return true;
}

// 0x00504730
void SkaterPhysicsModule::natas_spin()
{
    auto* career = modules::g_skate_module->career_.get();
    if (career->is_code_on(kPerfectRail)) {
        // (the perfect rail cheat: the balance held)
        career = modules::g_skate_module->career_.get();
        career->set_global_flag(script::fetch_int_value(kPerfectRail));
        balance_->lip_.lean_ = 0.0f;
        balance_->lip_.lean_speed_ = 0.0f;
        reinterpret_cast<uint8_t&>(online::g_cheat_flag) = 1;
    }
    balance_->lip_.tick_frame();
    float factor = script::fetch_float(kNatasSpinFactor);
    // (read as a double - the unrounded x87 result: get_lean 0x004F8F20 isn't called as a float function)
    double balance = std::fabs(
        reinterpret_cast<double(__thiscall*)(void*)>(code_address(&objects::RiderTeeterMoves::get_lean))(balance_));
    // The spin slows the further off balance.
    double rate = factor - balance / (kNatasSpinScale / static_cast<double>(factor)) + kNatasSpinBase;
    if (natas_counter_clockwise_)
        rate = -rate;
    last_spin_backside_ = rate > 0.0 ? 1 : 0;
    float step = static_cast<float>(rate * frame_length_);
    turn_matrices(this, step);
    float& total = trick_->spin_total_;
    total = static_cast<float>(static_cast<double>(step) * kDegreesPerRadian + total);
    // Shown as an integer - but (as Spin) not while short of a whole turn by more than the slop.
    objects::RiderPoseState* flags = state_;
    bool show = true;
    if (flags->flag(PoseFlag::kVertAir).on && !flags->flag(PoseFlag::kAcidDrop).on) {
        float size = std::fabs(total);
        double snap = 360.0f - static_cast<double>(tuning_float_lookup(kSpinCountSlop, object_of(this))) + 0.1f;
        show = snap <= size;
    }
    if (show) {
        int shown = static_cast<int>(ftol(trick_->spin_total_));
        void* display = score_->points_held_;
        static_cast<modules::PointTally*>(display)->advance_turn(shown);
    }
    natas_spin_time_ = get_now();
}

// 0x00504910
void SkaterPhysicsModule::lean_in_air()
{
    objects::RiderPoseState* flags = state_;
    if (flags->flag(PoseFlag::kAcidDrop).on || flags->flag(PoseFlag::kNoSecondSpin).on ||
        flags->flag(PoseFlag::kVertAir).on)
        return;
    objects::PadSnapshot* pad = &input_->pad_primary_;
    if (pad->button_round_.held_down_ || pad->button_box_.held_down_)
        return;
    float lean = 0.0f;
    int held = 0;
    if (pad->up_dir_.held_down_) {
        lean = static_cast<float>(stat_value(object_of(this), kAirLeanStat));
        held = time_since_press(&pad->up_dir_);
    }
    if (pad->btn_down_.held_down_) {
        lean = static_cast<float>(-stat_value(object_of(this), kAirLeanStat));
        held = time_since_press(&pad->btn_down_);
    }
    // None for a while after the button is pressed, then ramping up to full.
    float start = script::fetch_float(kAirNoLeanTime);
    float end = script::fetch_float(kAirRampLeanTime);
    float time = static_cast<float>(held);
    if (static_cast<double>(held) < start || static_cast<double>(held) == start)
        return;
    if (time < end)
        lean = static_cast<float>((static_cast<double>(time) - start) * lean / (static_cast<double>(end) - start));
    entity_->basis_frame_.pitch_locally(lean * frame_length_);
    display_matrix_.pitch_locally(lean * frame_length_);
}

// 0x00504A60
void SkaterPhysicsModule::rotate_in_acid_drop()
{
    if (!state_->flag(PoseFlag::kAcidDrop).on)
        return;
    double elapsed = static_cast<double>(frame_length_) + drop_time_;
    drop_time_ = static_cast<float>(elapsed);
    double t = elapsed / drop_duration_;
    if (t > 1.0)
        t = 1.0f;
    // Eased: 3t^2 - 2t^3.
    float eased = static_cast<float>((t - kAcidDropEase) * t * t * kAcidDropEaseScale);
    maths::Mat4 slerped;
    drop_slerp_.fetch_mat(slerped, eased);
    // The change since the last frame's matrix applied to the skater's.
    maths::Mat4 last = drop_last_matrix_;
    last.invert_orthonormal();
    maths::Mat4 change;
    maths::get_multiply(change, last, slerped);
    maths::Mat4& matrix = entity_->basis_frame_;
    maths::get_multiply(matrix, matrix, change);
    display_matrix_ = entity_->basis_frame_;
    drop_last_matrix_ = slerped;
}

// 0x00504B40
void SkaterPhysicsModule::recover_in_air()
{
    if (state_->flag(PoseFlag::kAcidDrop).on)
        return;
    if (entity_->vel_.y > 0.0f && ride_->ride_)
        return;
    // Ground (not vert, not too steep) within 500 below.
    render::ProbeRay feeler;
    feeler.line_.start = entity_->at_point_;
    feeler.line_.end = entity_->at_point_;
    feeler.line_.end.y = feeler.line_.end.y - kRecoverFeelerLength;
    if (!feeler.get_hit_shape(true, false))
        return;
    if (feeler.hit_.face_flags_ & 8)
        return;
    if (feeler.surface_dir_.y < kRecoverSteepest)
        return;
    if (state_->flag(PoseFlag::kVertAir).on) {
        set_timed_flag(this, PoseFlag::kVertAir, 0);
        transfer_blocked_ = 0;
    }
    set_timed_flag(this, PoseFlag::kRecoverInAir, 1);
    objects::GameEntity* object = object_of(this);
    if (object->basis_frame_.row[1].y > kRecoverUpright)
        return;
    // Tipped over: turned back upright about the horizontal axis across the up vector.
    float rate = static_cast<float>(stat_value(object, kRecoverRateStat));
    object = object_of(this);
    const maths::Vec4& up = object->basis_frame_.row[1];
    maths::Vec4 flat = {up.x, 0.0f, up.z, up.w};
    flat.make_unit();
    maths::Vec4 axis = {-flat.z, 0.0f, flat.x, 0.0f};
    object->basis_frame_.spin_by(axis, rate * frame_length_);
    display_matrix_ = entity_->basis_frame_;
}

// 0x00504DF0
void SkaterPhysicsModule::keep_upright()
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& forward = object->basis_frame_.row[2];
    const maths::Vec4& up = object->basis_frame_.row[1];
    if (std::fabs(forward.y) > kUprightMaxTilt)
        return;
    // Leaning sideways (the up vector across the facing): rolled back at skater_upright_sideways_speed.
    double sideways = static_cast<double>(forward.x) * up.z + -static_cast<double>(forward.z) * up.x +
                      static_cast<double>(up.y) * 0.0f;
    double angle;
    if (sideways > static_cast<double>(frame_length_) * kUprightRight) {
        angle = static_cast<double>(script::fetch_float(kUprightSidewaysSpeed)) * kRadiansPerDegree * frame_length_;
    } else if (sideways < static_cast<double>(frame_length_) * kUprightLeft) {
        angle = -(static_cast<double>(script::fetch_float(kUprightSidewaysSpeed)) * kRadiansPerDegree * frame_length_);
    } else {
        return;
    }
    entity_->basis_frame_.roll_in_own_frame(static_cast<float>(angle));
    display_matrix_ = entity_->basis_frame_;
}

} // namespace skater
