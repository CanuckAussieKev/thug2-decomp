// The skater's turns: on the ground, down slopes, off walls, jumping off rails, natas spins, leaning, recovering
// and staying upright in the air, and the acid drop's rotation.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "base/maths/mat4.h"
#include "base/core/enum_ops.h"
#include "base/maths/rotation_blend.h"
#include "base/maths/vec4.h"
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
#include "game/rider/skater_score.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/skater_state.h"
#include "game/rider/trick_component.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"
#include "engine/render/coll_obj.h"
#include "base/maths/trig.h"

namespace skater {

using namespace physics;

namespace {
constexpr int kSlowTurnHeldMs = 600;                // held for less than this, the turn rate builds up

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

constexpr float kRadiansPerDegree = 0.0174532924F;       // 0x00645464
constexpr float kMinusDegreesToRadians = -0.0174532924F; // 0x0064BCCC
constexpr float kDegreesPerRadian = 57.2957764F;         // 0x00645468
constexpr float kSlopeTurnSpeed = 0.001F;                // 0x006461D8
constexpr float kSlowGroundTurnSpeed = 10.0F;            // 0x00646710
constexpr float kSlowGroundTurnRate = 0.00166666671F;    // 0x0064BD38 (1 / 600)
constexpr float kHalfPi = 1.57079637F;                   // 0x00645484
constexpr float kNatasSpinScale = 4000.0F;               // 0x0064B8F8
constexpr float kNatasSpinBase = 5.0F;                   // 0x006469AC
constexpr float kAcidDropEase = 1.5F;                    // 0x00646744
constexpr float kAcidDropEaseScale = -2.0F;              // 0x00646970
constexpr float kRecoverFeelerLength = 500.0F;           // 0x0064BC80
constexpr float kRecoverSteepest = 0.2F;                 // 0x006473AC
constexpr float kRecoverUpright = 0.9F;                  // 0x00649FE4
constexpr float kUprightMaxTilt = 0.95F;                 // 0x0064BC7C
constexpr float kUprightRight = 1.2F;                    // 0x0064BD40
constexpr float kUprightLeft = -1.2F;                    // 0x0064BD3C

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void turn_velocity(maths::Vec4& velocity, double angle)
{
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    const double x = velocity.x;
    const double z = velocity.z;
    velocity.x = static_cast<float>((cosine * x) + (sine * z));
    velocity.z = static_cast<float>((cosine * z) + -(x * sine));
}

} // namespace

// The skater's matrix and its display matrix turned by `angle` about their own y axes (each a make_yaw_matrix and
// a D3DXMatrixMultiply in the original).
void SkaterPhysicsModule::turn_matrices(float angle)
{
    object_of(this)->basis_frame().yaw_locally(angle);
    ground_.display_matrix.yaw_locally(angle);
}

// 0x005002D0
void SkaterPhysicsModule::turn_for_rail_jump()
{
    const objects::PadInputModule* input = input_;
    float angle = 0.0F;
    if (thaw_bmx::live_now(this)) {
        // Not original: THAW 0x005CF010 - on the bike the stick's left / right (THAW's LEFTLEFT / LEFTRIGHT).
        const thaw_bmx::BikeState* bike = thaw_bmx::state_of(this, false);
        if (bike && bike->stick_left_.down) {
            angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kRadiansPerDegree);
        } else if (bike && bike->stick_right_.down) {
            angle =
                static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kMinusDegreesToRadians);
        } else {
            return;
        }
    } else if (input->pad_primary().left_side().held_down() != 0U) {
        angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kRadiansPerDegree);
    } else if (input->pad_primary().right_side().held_down() != 0U) {
        angle = static_cast<float>(static_cast<double>(script::fetch_float(kRailJumpAngle)) * kMinusDegreesToRadians);
    } else {
        return;
    }
    turn_velocity(entity()->vel(), angle);
    turn_matrices(angle);
}

// 0x00503560
void SkaterPhysicsModule::turn_on_ground()
{
    if (thaw_bmx::live_now(this)) { // (not original: the bike's own turning, THAW 0x005D61F0)
        thaw_bmx::turn_on_ground(this);
        return;
    }
    objects::PadSnapshot* pad = &input_->pad_primary();
    auto speed = static_cast<float>(length_unrounded(entity()->vel()));
    objects::GameEntity* object = object_of(this);
    // Left / right (&pad->left_side_ / +0x60); with the sharp turn button (+0x20) harder - and, slower than 10, the turn
    // ramps up over the first 600 ms the button is held.
    objects::ControlButton* button = nullptr;
    bool sharp = false;
    double rate = 0.0;
    if (pad->left_side().held_down() != 0U) {
        button = &pad->left_side();
        sharp = pad->btn_down().held_down() != 0;
        rate = tuning_float_lookup(sharp ? kGroundSharpRotation : kGroundRotation, object);
    } else if (pad->right_side().held_down() != 0U) {
        button = &pad->right_side();
        sharp = pad->btn_down().held_down() != 0;
        rate = -static_cast<double>(tuning_float_lookup(sharp ? kGroundSharpRotation : kGroundRotation, object));
    } else {
        return;
    }
    if (sharp) {
        auto full = static_cast<float>(rate);
        if (speed < kSlowGroundTurnSpeed) {
            const int held = time_since_press(button);
            rate = held < kSlowTurnHeldMs ? static_cast<double>(held) * full * kSlowGroundTurnRate : full;
        }
    }
    if (rate == 0.0) {
        return;
    }
    const double step = rate * frame_.length;
    auto angle = static_cast<float>(step);
    last_spin_backside_ = step > 0.0 ? 1 : 0;
    if (lock_velocity_ == 0U) {
        turn_velocity(entity()->vel(), angle);
    }
    turn_matrices(angle);
}

// 0x005039F0
float SkaterPhysicsModule::deflect_from_wall(const maths::Vec4* normal, float* turn, float scale)
{
    // Not original: THAW 0x005CCBF0 (the on-bike flag tested first, 0x005CCBF6) - on the bike rolling backwards into a
    // wall: stopped, no bounce (`turn` left as it is).
    if (thaw_bmx::live_now(this) && thaw_bmx::rolling_backwards(this)) {
        entity()->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        return 0.0F;
    }
    const maths::Vec4& side = core::at(entity()->basis_frame().row, 0);
    auto cosine =
        static_cast<float>((static_cast<double>(side.z) * normal->z) + (static_cast<double>(side.y) * normal->y) +
                           (static_cast<double>(normal->x) * side.x));
    if (cosine > 1.0F) {
        cosine = 1.0F;
    } else if (cosine < -1.0F) {
        cosine = -1.0F;
    }
    const double unrounded = std::acos(static_cast<double>(cosine)); // (the C runtime's, 0x00623630)
    auto angle = static_cast<float>(unrounded);
    if (unrounded > kHalfPi) {
        angle = angle - maths::kPi;
    }
    const double amount = static_cast<double>(script::fetch_float(kWallBounceMultiplier)) * angle * scale;
    *turn = static_cast<float>(amount);
    turn_velocity(entity()->vel(), amount); // (by the unrounded turn)
    turn_matrices(*turn);
    return angle;
}

// 0x00501B00
bool SkaterPhysicsModule::turn_down_slope()
{
    if (!may_turn_on_slope()) {
        return false;
    }
    objects::GameEntity* object = object_of(this);
    // Along the velocity, or the facing when hardly moving: turned towards straight down the slope.
    const maths::Vec4 heading =
        length_unrounded(object->vel()) < kSlopeTurnSpeed ? core::at(object->basis_frame().row, 2) : object->vel();
    maths::Vec4 down = {.x = 0.0F, .y = -1.0F, .z = 0.0F, .w = 1.0F};
    down.flatten_onto_plane(ground_.floor_normal);
    object = object_of(this);
    const double off = maths::get_turn_around_axis(heading, down, core::at(object->basis_frame().row, 1));
    auto angle = static_cast<float>(off);
    const float sign = off < 0.0 ? -1.0F : 1.0F;
    const double turn = static_cast<double>(tuning_float_lookup(kSlowTurnOnSlopes, object)) * frame_.length * sign;
    auto step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn)) {
        step = angle;
    }
    turn_matrices(step);
    return true;
}

// 0x00504730
void SkaterPhysicsModule::natas_spin()
{
    auto* career = modules::Skate::sole()->career().get();
    if (career->is_code_on(kPerfectRail)) {
        // (the perfect rail cheat: the balance held)
        career = modules::Skate::sole()->career().get();
        career->set_global_flag(script::fetch_int_value(kPerfectRail));
        balance_->lip().set_lean(0.0F);
        balance_->lip().set_lean_speed(0.0F);
        online::net_game_state().set_cheat_flag(true);
    }
    balance_->lip().tick_frame();
    const float factor = script::fetch_float(kNatasSpinFactor);
    const double balance = std::fabs(static_cast<double>(balance_->get_lean()));
    // The spin slows the further off balance.
    double rate = factor - (balance / (kNatasSpinScale / static_cast<double>(factor))) + kNatasSpinBase;
    if (natas_.counter_clockwise != 0U) {
        rate = -rate;
    }
    last_spin_backside_ = rate > 0.0 ? 1 : 0;
    auto step = static_cast<float>(rate * frame_.length);
    turn_matrices(step);
    float& total = trick_->spin_total();
    total = static_cast<float>((static_cast<double>(step) * kDegreesPerRadian) + total);
    // Shown as an integer - but (as Spin) not while short of a whole turn by more than the slop.
    objects::RiderPoseState* flags = state_;
    bool show = true;
    if ((flags->flag(PoseFlag::kVertAir).on != 0U) && (flags->flag(PoseFlag::kAcidDrop).on == 0U)) {
        const float size = std::fabs(total);
        const double snap = 360.0F - static_cast<double>(tuning_float_lookup(kSpinCountSlop, object_of(this))) + 0.1F;
        show = snap <= size;
    }
    if (show) {
        const int shown = static_cast<int>(ftol(trick_->spin_total()));
        void* display = score_->points_held();
        static_cast<modules::PointTally*>(display)->advance_turn(shown);
    }
    natas_.time = get_now();
}

// 0x00504910
void SkaterPhysicsModule::lean_in_air()
{
    objects::RiderPoseState* flags = state_;
    if ((flags->flag(PoseFlag::kAcidDrop).on != 0U) || (flags->flag(PoseFlag::kNoSecondSpin).on != 0U) ||
        (flags->flag(PoseFlag::kVertAir).on != 0U)) {
        return;
    }
    objects::PadSnapshot* pad = &input_->pad_primary();
    if ((pad->button_round().held_down() != 0U) || (pad->button_box().held_down() != 0U)) {
        return;
    }
    float lean = 0.0F;
    int held = 0;
    if (pad->up_dir().held_down() != 0U) {
        lean = static_cast<float>(stat_value(object_of(this), kAirLeanStat));
        held = time_since_press(&pad->up_dir());
    }
    if (pad->btn_down().held_down() != 0U) {
        lean = static_cast<float>(-stat_value(object_of(this), kAirLeanStat));
        held = time_since_press(&pad->btn_down());
    }
    // None for a while after the button is pressed, then ramping up to full.
    const float start = script::fetch_float(kAirNoLeanTime);
    const float end = script::fetch_float(kAirRampLeanTime);
    auto time = static_cast<float>(held);
    if (static_cast<double>(held) < start || static_cast<double>(held) == start) {
        return;
    }
    if (time < end) {
        lean = static_cast<float>((static_cast<double>(time) - start) * lean / (static_cast<double>(end) - start));
    }
    entity()->basis_frame().pitch_locally(lean * frame_.length);
    ground_.display_matrix.pitch_locally(lean * frame_.length);
}

// 0x00504A60
void SkaterPhysicsModule::rotate_in_acid_drop()
{
    if (state_->flag(PoseFlag::kAcidDrop).on == 0U) {
        return;
    }
    const double elapsed = static_cast<double>(frame_.length) + drop_.time;
    drop_.time = static_cast<float>(elapsed);
    double progress = elapsed / drop_.duration;
    if (progress > 1.0) {
        progress = 1.0F;
    }
    // Eased: 3t^2 - 2t^3.
    auto eased = static_cast<float>((progress - kAcidDropEase) * progress * progress * kAcidDropEaseScale);
    maths::Mat4 slerped{};
    drop_.slerp.fetch_mat(slerped, eased);
    // The change since the last frame's matrix applied to the skater's.
    maths::Mat4 last = drop_.last_matrix;
    last.invert_orthonormal();
    maths::Mat4 change{};
    maths::get_multiply(change, last, slerped);
    maths::Mat4& matrix = entity()->basis_frame();
    maths::get_multiply(matrix, matrix, change);
    ground_.display_matrix = entity()->basis_frame();
    drop_.last_matrix = slerped;
}

// 0x00504B40
void SkaterPhysicsModule::recover_in_air()
{
    if (state_->flag(PoseFlag::kAcidDrop).on != 0U) {
        return;
    }
    if (entity()->vel().y > 0.0F && ride_->ride()) {
        return;
    }
    // Ground (not vert, not too steep) within 500 below.
    render::ProbeRay feeler;
    feeler.line().start = entity()->at_point();
    feeler.line().end = entity()->at_point();
    feeler.line().end.y = feeler.line().end.y - kRecoverFeelerLength;
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }
    if (core::any(feeler.hit().face_flags_ & render::FaceFlag::kVert)) {
        return;
    }
    if (feeler.surface_dir().y < kRecoverSteepest) {
        return;
    }
    if (state_->flag(PoseFlag::kVertAir).on != 0U) {
        set_timed_flag(this, PoseFlag::kVertAir, 0);
        transfer_blocked_ = 0;
    }
    set_timed_flag(this, PoseFlag::kRecoverInAir, 1);
    objects::GameEntity* object = object_of(this);
    if (core::at(object->basis_frame().row, 1).y > kRecoverUpright) {
        return;
    }
    // Tipped over: turned back upright about the horizontal axis across the up vector.
    auto rate = static_cast<float>(stat_value(object, kRecoverRateStat));
    object = object_of(this);
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    maths::Vec4 flat = {.x = up.x, .y = 0.0F, .z = up.z, .w = up.w};
    flat.make_unit();
    const maths::Vec4 axis = {.x = -flat.z, .y = 0.0F, .z = flat.x, .w = 0.0F};
    object->basis_frame().spin_by(axis, rate * frame_.length);
    ground_.display_matrix = entity()->basis_frame();
}

// 0x00504DF0
void SkaterPhysicsModule::keep_upright()
{
    const objects::GameEntity* object = object_of(this);
    const maths::Vec4& forward = core::at(object->basis_frame().row, 2);
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    if (std::fabs(forward.y) > kUprightMaxTilt) {
        return;
    }
    // Leaning sideways (the up vector across the facing): rolled back at skater_upright_sideways_speed.
    const double sideways = (static_cast<double>(forward.x) * up.z) + (-static_cast<double>(forward.z) * up.x) +
                            (static_cast<double>(up.y) * 0.0F);
    double angle = 0.0;
    if (sideways > static_cast<double>(frame_.length) * kUprightRight) {
        angle = static_cast<double>(script::fetch_float(kUprightSidewaysSpeed)) * kRadiansPerDegree * frame_.length;
    } else if (sideways < static_cast<double>(frame_.length) * kUprightLeft) {
        angle = -(static_cast<double>(script::fetch_float(kUprightSidewaysSpeed)) * kRadiansPerDegree * frame_.length);
    } else {
        return;
    }
    entity()->basis_frame().roll_in_own_frame(static_cast<float>(angle));
    ground_.display_matrix = entity()->basis_frame();
}

} // namespace skater
