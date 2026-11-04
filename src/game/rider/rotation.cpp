// The skater's turning in the air (spins, breaking out of vert), with PartyMod's PS2-style controls built in.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles,
// rounded to float only where the original stores them.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/rider/rotation.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "base/maths/maths.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/control_pad.h"
#include "game/rider/controls.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "engine/sys/game_clock.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "game/rider/trick_component.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/maths/trig.h"

namespace skater {

namespace {
constexpr uint32_t kPiBits = 0x40490FDB;             // pi as the original stores it
constexpr uint32_t kRadiansPerDegreeBits = 0x3C8EFA35; // pi / 180 as the original stores it
constexpr uint32_t kDegreesPerRadianBits = 0x42652EE0; // 180 / pi as the original stores it

using objects::PoseFlag; // (kSpin: cleared once a spin has gone on long enough)

// A button of the pad: {bool down, int time it changed, ..., +0x10 bool pressed this frame}.
constexpr uint32_t kJump = 0x00; // (offsets from the pad's +0x20)
constexpr uint32_t kSpinLeft2 = 0x40;
constexpr uint32_t kSpinRight2 = 0x60;
constexpr uint32_t kSpinLeft = 0x80;  // L1
constexpr uint32_t kTriggerA = 0xA0;  // PartyMod's "R2/L2" pair
constexpr uint32_t kSpinRight = 0xE0; // R1
constexpr uint32_t kTriggerB = 0x100;
constexpr uint32_t kButton140 = 0x140;
constexpr uint32_t kButton160 = 0x160;

// Script values (checksums).
constexpr uint32_t kSpinDelay = 0xA5A02449;
constexpr uint32_t kSpinFaceRampTime = 0xABDD3395;
constexpr uint32_t kSpinRampStart = 0xA092B2BE;
constexpr uint32_t kSpinRampEnd = 0x5D756349;
constexpr uint32_t kSpinSpeed = 0x4957DB43;
constexpr uint32_t kTapSpinSpeed = 0xF7A2ACC1;
constexpr uint32_t kFaceRampMaxTilt = 0x61224F2E;
constexpr uint32_t kFaceRampSpeed = 0x9DB33213;
constexpr uint32_t kRotationSnap = 0x50C5CC2F;
constexpr uint32_t kBreakVertJumpTime = 0x9D2F8CC8;
constexpr uint32_t kBreakVertSlow = 0x13F33B41;
constexpr uint32_t kBreakVertUpScale = 0x848C5CD6;
constexpr uint32_t kBreakVertPitch = 0xD757F4BB;

constexpr float kRadiansPerDegree = 0.0174532924F; // 0x00645464
constexpr float kDegreesPerRadian = 57.2957764F;   // 0x00645468
constexpr float kBreakVertPush = 24.0F;            // 0x00645EE8
static_assert(std::bit_cast<uint32_t>(maths::kPi) == kPiBits && std::bit_cast<uint32_t>(kRadiansPerDegree) == kRadiansPerDegreeBits &&
                  std::bit_cast<uint32_t>(kDegreesPerRadian) == kDegreesPerRadianBits,
              "the original's constants");

// The game clock (ms) plus the offset at 0x00696D60: the time a flag or button changed.
int get_now() { return frame_clock::game_clock_elapsed() + objects::clock_offset(); }


// A physics value from script: the skater's physics structure's `name`, else the
// global (asserted when `required`).
double tuning_float_lookup(uint32_t name, int required, objects::GameEntity* object)
{
    return objects::read_tuning_float(checksums::Name{name}, required, object);
}
int tuning_int_lookup(uint32_t name, int required, objects::GameEntity* object)
{
    return objects::read_tuning_int(checksums::Name{name}, required, object);
}
// A value set by the skater's stats (0x00580790: script range scaled by the stat level, unrounded).
double stat_value(objects::GameEntity* object, uint32_t name)
{
    return core::down_cast<objects::Rider>(object)->get_scaled_physics(name); // (rounded to a float by ours)
}

// How long a button has been down - read as unsigned by the first pair of spin buttons, signed by
// the second.
int get_held_time(objects::ControlButton* button) { return button->get_held_millis(); }
float get_held_seconds(objects::ControlButton* button)
{
    return static_cast<float>(static_cast<double>(static_cast<uint32_t>(get_held_time(button))));
}
// Forget a button's press this frame.
void take_press(objects::ControlButton* button) { button->reset_activation(); }

maths::Mat4& body_transform(objects::GameEntity* object) { return object->basis_frame(); }

// The skater's matrix turned so that it faces along its velocity (x and z), kept upright about that.
void face_along_velocity(objects::GameEntity* object)
{
    maths::Vec4 direction = object->vel();
    direction.make_unit();
    maths::Vec4& forward = core::at(object->basis_frame().row, 2);
    forward.x = direction.x;
    forward.z = direction.z;
    forward.make_unit();
    body_transform(object).square_up_axes(2);
}

} // namespace

// 0x00504060
void SkaterPhysicsModule::get_spin()
{
    objects::PadSnapshot* pad = &input_->pad_primary();
    objects::GameEntity* object = entity();
    if (no_spin_ != 0U) {
        return;
    }
    objects::RiderPoseState* flags = state_;
    if ((flags->flag(PoseFlag::kAcidDrop).on != 0U) && (flags->flag(PoseFlag::kSpin).on != 0U)) {
        state_->set_flag(PoseFlag::kSpin, 0);
    }

    float speed = 0.0F;
    auto frame_length = [this] { return static_cast<double>(frame_length_); };
    const bool spinning = tap_spin_ != 0U ? read_tap_spin(pad, speed) : read_held_spin(pad, speed);

    // The second pair of spin buttons: the speed ramps up from 0 at one held time to full at another.
    if ((state_->flag(PoseFlag::kNoSecondSpin).on == 0U) && !spinning) {
        read_second_spin(pad, speed);
    }

    if (speed == 0.0F) {
        face_ramp(speed);
        if (speed == 0.0F) {
            return;
        }
    }

    last_spin_backside_ = static_cast<uint8_t>(speed > 0.0F);
    auto step = static_cast<float>(static_cast<double>(speed) * frame_length());
    body_transform(object).yaw_locally(step);
    display_matrix_.yaw_locally(step);
    float& total = trick_->spin_total();
    total = static_cast<float>((static_cast<double>(step) * kDegreesPerRadian) + total);
    // Shown as an integer (the C runtime's _ftol2: truncated) - except when the flags say not to and the total
    // is short of a whole turn by more than the snap (360 - snap + 0.1).
    flags = state_;
    if ((flags->flag(PoseFlag::kVertAir).on != 0U) && (flags->flag(PoseFlag::kAcidDrop).on == 0U)) {
        auto size = static_cast<float>(std::fabs(static_cast<double>(total)));
        const double snap = 360.0F - tuning_float_lookup(kRotationSnap, 1, object) + 0.1F;
        if (!(snap <= size)) {
            return;
        }
    }
    void* display = score_->points_held();
    static_cast<modules::PointTally*>(display)->advance_turn(static_cast<int>(static_cast<int64_t>(total)));
}

// Each tap of a spin button queues half a turn, spun at the tap spin speed.
bool SkaterPhysicsModule::read_tap_spin(objects::PadSnapshot* pad, float& speed)
{
    bool spinning = false;
    objects::GameEntity* object = entity();
    float& queued = queued_spin_;
    // Each tap queues half a turn (unless the other button is down).
    if (pad->shoulder_left_a().fired() != 0U) {
        take_press(&pad->shoulder_left_a());
        if (pad->shoulder_right_a().held_down() == 0U) {
            queued = static_cast<float>(static_cast<double>(queued) + maths::kPi);
        }
    }
    if (pad->shoulder_right_a().fired() != 0U) {
        take_press(&pad->shoulder_right_a());
        if (pad->shoulder_left_a().held_down() == 0U) {
            queued = static_cast<float>(static_cast<double>(queued) - maths::kPi);
        }
    }
    if (queued != 0.0F) {
        spinning = true;
        speed = queued < 0.0F ? -1.0F : 1.0F;
        const double turn = stat_value(object, kTapSpinSpeed) * speed;
        const double step = turn * static_cast<double>(frame_length_);
        if (std::fabs(queued) < std::fabs(step)) {
            queued = 0.0F;
        } else {
            queued = static_cast<float>(queued - step);
        }
        speed = static_cast<float>(turn);
    }
    return spinning;
}

// A spin button held.
bool SkaterPhysicsModule::read_held_spin(objects::PadSnapshot* pad, float& speed)
{
    bool spinning = false;
    objects::GameEntity* object = entity();
    // Holding one button (not both) turns once it's been held longer than the spin delay - always, with
    // PartyMod's PS2 controls (it NOPs the delay checks' jumps; the delay is still read). Held long enough,
    // the skater can no longer be turned to face the ramp.
    objects::ControlButton* left = &pad->shoulder_left_a();
    objects::ControlButton* right = &pad->shoulder_right_a();
    if ((left->held_down() != 0U) && (right->held_down() == 0U)) {
        float held = get_held_seconds(left);
        if (tuning_float_lookup(kSpinDelay, 0, nullptr) < held || ps2_controls()) {
            speed = static_cast<float>(stat_value(object, kSpinSpeed));
            spinning = true;
            held = get_held_seconds(left);
            if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < held) {
                state_->set_flag(PoseFlag::kSpin, 0);
            }
        }
    }
    if ((right->held_down() != 0U) && (left->held_down() == 0U)) {
        float held = get_held_seconds(right);
        if (tuning_float_lookup(kSpinDelay, 0, nullptr) < held || ps2_controls()) {
            speed = static_cast<float>(-stat_value(object, kSpinSpeed));
            spinning = true;
            held = get_held_seconds(right);
            if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < held) {
                state_->set_flag(PoseFlag::kSpin, 0);
            }
        }
    }
    return spinning;
}

// The second pair of spin buttons: the speed ramps up from 0 at one held time to full at another.
void SkaterPhysicsModule::read_second_spin(objects::PadSnapshot* pad, float& speed)
{
    objects::GameEntity* object = entity();
    int held = 0;
    if (pad->left_side().held_down() != 0U) {
        speed = static_cast<float>(stat_value(object, kSpinSpeed));
        held = get_held_time(&pad->left_side());
        if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < static_cast<float>(held)) {
            state_->set_flag(PoseFlag::kSpin, 0);
        }
    }
    if (pad->right_side().held_down() != 0U) {
        speed = static_cast<float>(-stat_value(object, kSpinSpeed));
        held = get_held_time(&pad->right_side());
        if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < static_cast<float>(held)) {
            state_->set_flag(PoseFlag::kSpin, 0);
        }
    }
    auto start = static_cast<float>(tuning_float_lookup(kSpinRampStart, 1, object));
    const double end = tuning_float_lookup(kSpinRampEnd, 1, object);
    const double held_time = held;
    if (held_time <= start) {
        speed = 0.0F;
    }
    if (held_time < end) {
        speed = static_cast<float>((held_time - start) * speed / (end - start));
    }
}

// Not spinning: the skater turned to face along the ramp while it's allowed and they're tilted far enough.
void SkaterPhysicsModule::face_ramp(float& speed)
{
    objects::GameEntity* object = entity();
    // Not spinning: turn the skater to face along the ramp while it's allowed and they're tilted far enough.
    objects::RiderPoseState* flags = state_;
    if ((flags->flag(PoseFlag::kVertAir).on != 0U) && (flags->flag(PoseFlag::kSpin).on != 0U)) {
        const float tilt = maths::confine(core::at(object->basis_frame().row, 2).y, -1.0F, 1.0F);
        auto angle = static_cast<float>(std::acos(static_cast<double>(tilt))); // (the C runtime's, 0x00623630)
        if (tuning_float_lookup(kFaceRampMaxTilt, 1, object) * kRadiansPerDegree > angle) {
            state_->set_flag(PoseFlag::kSpin, 0);
        } else {
            auto off = static_cast<float>(maths::get_turn_around_axis(
                core::at(object->basis_frame().row, 2), vert_facing_, core::at(object->basis_frame().row, 1)));
            auto rate = static_cast<float>(tuning_float_lookup(kFaceRampSpeed, 1, object));
            const double turn = static_cast<double>(maths::get_sign(off)) * rate;
            speed = static_cast<float>(turn);
            if (std::fabs(off) < std::fabs(turn * static_cast<double>(frame_length_))) {
                // (the last bit: exactly what's left)
                speed = static_cast<float>(off / static_cast<double>(frame_length_));
                state_->set_flag(PoseFlag::kSpin, 0);
            }
        }
    }
}

// 0x00507120
void SkaterPhysicsModule::break_vert(bool force)
{
    objects::GameEntity* object = entity();
    objects::PadSnapshot* pad = &input_->pad_primary();
    // The two buttons: both (PartyMod's PS2 controls: either - its JNZs at 0x00507184 / 0x005071AE).
    auto triggers = [&] {
        return ps2_controls() ? pad->shoulder_left_b().held_down() || pad->shoulder_right_b().held_down()
                              : pad->shoulder_left_b().held_down() && pad->shoulder_right_b().held_down();
    };
    bool jump_held = false;
    // Not original: THAW 0x005DA210 (the on-bike flag read first, 0x005DA218) - on the bike the movement stick's up
    // held (THAW's LEFTUP, not left / right) instead of the d-pad's, and R1 with the stick up or down (the tire tap's
    // buttons), or a tire tap looked for in vert, takes the second path (a spine transfer, else pushed off the wall)
    // instead of falling away from it.
    const bool bike = thaw_bmx::live_now(this);
    if (bike) {
        jump_held = thaw_bmx::vert_push_held(this, tuning_int_lookup(kBreakVertJumpTime, 1, object));
    } else if (pad->up_dir().held_down() != 0U) {
        const int limit = tuning_int_lookup(kBreakVertJumpTime, 1, object);
        jump_held = get_held_time(&pad->up_dir()) > limit && (pad->left_side().held_down() == 0U) &&
                    (pad->right_side().held_down() == 0U) && (pad->button_box().held_down() == 0U) &&
                    (pad->button_round().held_down() == 0U);
    }
    if (!jump_held && !triggers() && (transfer_blocked_ == 0U) && !force) {
        return;
    }

    auto& velocity = object->vel();
    if (triggers() || (transfer_blocked_ != 0U) || (bike && thaw_bmx::vert_tap_held(this))) {
        if (try_spine_transfer()) {
            display_matrix_ = body_transform(object);
            return;
        }
        velocity[0] = static_cast<float>(velocity[0] - (static_cast<double>(drawn_normal_.x) * kBreakVertPush));
        velocity[2] = static_cast<float>(velocity[2] - (static_cast<double>(drawn_normal_.z) * kBreakVertPush));
        body_transform(object).pitch_locally(
            static_cast<float>(tuning_float_lookup(kBreakVertPitch, 1, object) * kRadiansPerDegree));
        state_->set_flag(PoseFlag::kVertAir, 0);
        state_->set_flag(PoseFlag::kVertWall, 0);
        state_->set_flag(PoseFlag::kLeftVert, 0);
        state_->set_flag(PoseFlag::kRecoverInAir, 1);
        face_along_velocity(object);
        return; // (the display matrix isn't updated on this path)
    }

    // Slowed away from the wall by the speed times a script value; the upward speed scaled.
    double length_sqr =
        (static_cast<double>(velocity[0]) * velocity[0]) + (static_cast<double>(velocity[1]) * velocity[1]);
    length_sqr += static_cast<double>(velocity[2]) * velocity[2];
    auto length_sqr_stored = static_cast<float>(length_sqr);
    const double slow =
        tuning_float_lookup(kBreakVertSlow, 1, object) * std::sqrt(static_cast<double>(length_sqr_stored));
    velocity[0] = static_cast<float>(velocity[0] - (slow * drawn_normal_.x));
    velocity[2] = static_cast<float>(velocity[2] - (slow * drawn_normal_.z));
    velocity[1] = static_cast<float>(tuning_float_lookup(kBreakVertUpScale, 1, object) * velocity[1]);
    body_transform(object).pitch_locally(
        static_cast<float>(tuning_float_lookup(kBreakVertPitch, 1, object) * kRadiansPerDegree));
    state_->set_flag(PoseFlag::kVertAir, 0);
    state_->set_flag(PoseFlag::kVertWall, 0);
    state_->set_flag(PoseFlag::kLeftVert, 0);
    state_->set_flag(PoseFlag::kNoAcidDrop, 0);
    face_along_velocity(object);
    display_matrix_ = body_transform(object);
}

} // namespace skater
