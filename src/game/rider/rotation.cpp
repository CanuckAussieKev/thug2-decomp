// The skater's turning in the air (spins, breaking out of vert), with PartyMod's PS2-style controls built in.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles,
// rounded to float only where the original stores them.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/rider/rotation.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/control_pad.h"
#include "game/rider/controls.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "game/rider/trick_component.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace skater {

namespace {


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

constexpr float kPi = 3.14159274f;                 // 0x0064BB7C
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kDegreesPerRadian = 57.2957764f;   // 0x00645468
constexpr float kBreakVertPush = 24.0f;            // 0x00645EE8
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490FDB && std::bit_cast<uint32_t>(kRadiansPerDegree) == 0x3C8EFA35 &&
                  std::bit_cast<uint32_t>(kDegreesPerRadian) == 0x42652EE0,
              "the original's constants");

// The game clock (ms) plus the offset at 0x00696D60: the time a flag or button changed.
int get_now() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }

// Clear a state flag (noting when).
void clear_flag(SkaterPhysicsModule* component, PoseFlag flag) { component->state_->set_flag(flag, 0); }

// A physics value from script: the skater's physics structure's `name`, else the
// global (asserted when `required`).
double tuning_float_lookup(uint32_t name, int required, objects::GameEntity* object)
{
    return objects::read_tuning_float(name, required, static_cast<objects::GameEntity*>(object));
}
int tuning_int_lookup(uint32_t name, int required, objects::GameEntity* object)
{
    return objects::read_tuning_int(name, required, static_cast<objects::GameEntity*>(object));
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

maths::Mat4& body_transform(objects::GameEntity* object) { return object->basis_frame_; }

// The skater's matrix turned so that it faces along its velocity (x and z), kept upright about that.
void face_along_velocity(objects::GameEntity* object)
{
    maths::Vec4 direction = object->vel_;
    direction.make_unit();
    maths::Vec4& forward = core::at(object->basis_frame_.row, 2);
    forward.x = direction.x;
    forward.z = direction.z;
    forward.make_unit();
    body_transform(object).square_up_axes(2);
}

} // namespace

// 0x00504060
void SkaterPhysicsModule::get_spin()
{
    objects::PadSnapshot* pad = &input_->pad_primary_;
    objects::GameEntity* object = entity_;
    if (no_spin_)
        return;
    objects::RiderPoseState* flags = state_;
    if (flags->flag(PoseFlag::kAcidDrop).on && flags->flag(PoseFlag::kSpin).on)
        clear_flag(this, PoseFlag::kSpin);

    bool spinning = false;
    float speed = 0.0f;
    float& queued = queued_spin_;
    auto frame_length = [this] { return static_cast<double>(frame_length_); };
    if (tap_spin_) {
        // Each tap queues half a turn (unless the other button is down).
        if (pad->shoulder_left_a_.fired_) {
            take_press(&pad->shoulder_left_a_);
            if (!pad->shoulder_right_a_.held_down_)
                queued = static_cast<float>(static_cast<double>(queued) + kPi);
        }
        if (pad->shoulder_right_a_.fired_) {
            take_press(&pad->shoulder_right_a_);
            if (!pad->shoulder_left_a_.held_down_)
                queued = static_cast<float>(static_cast<double>(queued) - kPi);
        }
        if (queued != 0.0f) {
            spinning = true;
            speed = queued < 0.0f ? -1.0f : 1.0f;
            const double turn = stat_value(object, kTapSpinSpeed) * speed;
            const double step = turn * frame_length();
            if (std::fabs(queued) < std::fabs(step))
                queued = 0.0f;
            else
                queued = static_cast<float>(queued - step);
            speed = static_cast<float>(turn);
        }
    } else {
        // Holding one button (not both) turns once it's been held longer than the spin delay - always, with
        // PartyMod's PS2 controls (it NOPs the delay checks' jumps; the delay is still read). Held long enough,
        // the skater can no longer be turned to face the ramp.
        objects::ControlButton* left = &pad->shoulder_left_a_;
        objects::ControlButton* right = &pad->shoulder_right_a_;
        if (left->held_down_ && !right->held_down_) {
            float held = get_held_seconds(left);
            if (tuning_float_lookup(kSpinDelay, 0, nullptr) < held || ps2_controls()) {
                speed = static_cast<float>(stat_value(object, kSpinSpeed));
                spinning = true;
                held = get_held_seconds(left);
                if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < held)
                    clear_flag(this, PoseFlag::kSpin);
            }
        }
        if (right->held_down_ && !left->held_down_) {
            float held = get_held_seconds(right);
            if (tuning_float_lookup(kSpinDelay, 0, nullptr) < held || ps2_controls()) {
                speed = static_cast<float>(-stat_value(object, kSpinSpeed));
                spinning = true;
                held = get_held_seconds(right);
                if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < held)
                    clear_flag(this, PoseFlag::kSpin);
            }
        }
    }

    // The second pair of spin buttons: the speed ramps up from 0 at one held time to full at another.
    if (!state_->flag(PoseFlag::kNoSecondSpin).on && !spinning) {
        int held = 0;
        if (pad->left_side_.held_down_) {
            speed = static_cast<float>(stat_value(object, kSpinSpeed));
            held = get_held_time(&pad->left_side_);
            if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < static_cast<float>(held))
                clear_flag(this, PoseFlag::kSpin);
        }
        if (pad->right_side_.held_down_) {
            speed = static_cast<float>(-stat_value(object, kSpinSpeed));
            held = get_held_time(&pad->right_side_);
            if (tuning_float_lookup(kSpinFaceRampTime, 1, object) < static_cast<float>(held))
                clear_flag(this, PoseFlag::kSpin);
        }
        auto start = static_cast<float>(tuning_float_lookup(kSpinRampStart, 1, object));
        const double end = tuning_float_lookup(kSpinRampEnd, 1, object);
        const double t = held;
        if (t <= start)
            speed = 0.0f;
        if (t < end)
            speed = static_cast<float>((t - start) * speed / (end - start));
    }

    if (speed == 0.0f) {
        // Not spinning: turn the skater to face along the ramp while it's allowed and they're tilted far enough.
        flags = state_;
        if (flags->flag(PoseFlag::kVertAir).on && flags->flag(PoseFlag::kSpin).on) {
            const float tilt = maths::confine(core::at(object->basis_frame_.row, 2).y, -1.0f, 1.0f);
            auto angle = static_cast<float>(std::acos(static_cast<double>(tilt))); // (the C runtime's, 0x00623630)
            if (tuning_float_lookup(kFaceRampMaxTilt, 1, object) * kRadiansPerDegree > angle) {
                clear_flag(this, PoseFlag::kSpin);
            } else {
                auto off = static_cast<float>(maths::get_turn_around_axis(core::at(object->basis_frame_.row, 2), vert_facing_,
                                                                          core::at(object->basis_frame_.row, 1)));
                auto rate = static_cast<float>(tuning_float_lookup(kFaceRampSpeed, 1, object));
                const double turn = static_cast<double>(maths::get_sign(off)) * rate;
                speed = static_cast<float>(turn);
                if (std::fabs(off) < std::fabs(turn * frame_length())) {
                    // (the last bit: exactly what's left)
                    speed = static_cast<float>(off / frame_length());
                    clear_flag(this, PoseFlag::kSpin);
                }
            }
        }
        if (speed == 0.0f)
            return;
    }

    last_spin_backside_ = speed > 0.0f;
    auto step = static_cast<float>(static_cast<double>(speed) * frame_length());
    body_transform(object).yaw_locally(step);
    display_matrix_.yaw_locally(step);
    float& total = trick_->spin_total_;
    total = static_cast<float>(static_cast<double>(step) * kDegreesPerRadian + total);
    // Shown as an integer (the C runtime's _ftol2: truncated) - except when the flags say not to and the total
    // is short of a whole turn by more than the snap (360 - snap + 0.1).
    flags = state_;
    if (flags->flag(PoseFlag::kVertAir).on && !flags->flag(PoseFlag::kAcidDrop).on) {
        auto size = static_cast<float>(std::fabs(static_cast<double>(total)));
        const double snap = 360.0f - tuning_float_lookup(kRotationSnap, 1, object) + 0.1f;
        if (!(snap <= size))
            return;
    }
    void* display = score_->points_held_;
    static_cast<modules::PointTally*>(display)->advance_turn(static_cast<int>(static_cast<int64_t>(total)));
}

// 0x00507120
void SkaterPhysicsModule::break_vert(bool force)
{
    objects::GameEntity* object = entity_;
    objects::PadSnapshot* pad = &input_->pad_primary_;
    // The two buttons: both (PartyMod's PS2 controls: either - its JNZs at 0x00507184 / 0x005071AE).
    auto triggers = [&] {
        return ps2_controls() ? pad->shoulder_left_b_.held_down_ || pad->shoulder_right_b_.held_down_
                              : pad->shoulder_left_b_.held_down_ && pad->shoulder_right_b_.held_down_;
    };
    bool jump_held = false;
    // Not original: THAW 0x005DA210 (the on-bike flag read first, 0x005DA218) - on the bike the movement stick's up
    // held (THAW's LEFTUP, not left / right) instead of the d-pad's, and R1 with the stick up or down (the tire tap's
    // buttons), or a tire tap looked for in vert, takes the second path (a spine transfer, else pushed off the wall)
    // instead of falling away from it.
    const bool bike = thaw_bmx::live_now(this);
    if (bike) {
        jump_held = thaw_bmx::vert_push_held(this, tuning_int_lookup(kBreakVertJumpTime, 1, object));
    } else if (pad->up_dir_.held_down_) {
        const int limit = tuning_int_lookup(kBreakVertJumpTime, 1, object);
        jump_held = get_held_time(&pad->up_dir_) > limit && !pad->left_side_.held_down_ &&
                    !pad->right_side_.held_down_ && !pad->button_box_.held_down_ && !pad->button_round_.held_down_;
    }
    if (!jump_held && !triggers() && !transfer_blocked_ && !force)
        return;

    auto& velocity = object->vel_;
    if (triggers() || transfer_blocked_ || (bike && thaw_bmx::vert_tap_held(this))) {
        if (try_spine_transfer()) {
            display_matrix_ = body_transform(object);
            return;
        }
        velocity[0] = static_cast<float>(velocity[0] - static_cast<double>(drawn_normal_.x) * kBreakVertPush);
        velocity[2] = static_cast<float>(velocity[2] - static_cast<double>(drawn_normal_.z) * kBreakVertPush);
        body_transform(object).pitch_locally(
            static_cast<float>(tuning_float_lookup(kBreakVertPitch, 1, object) * kRadiansPerDegree));
        clear_flag(this, PoseFlag::kVertAir);
        clear_flag(this, PoseFlag::kVertWall);
        clear_flag(this, PoseFlag::kLeftVert);
        state_->set_flag(PoseFlag::kRecoverInAir, 1);
        face_along_velocity(object);
        return; // (the display matrix isn't updated on this path)
    }

    // Slowed away from the wall by the speed times a script value; the upward speed scaled.
    double length_sqr = static_cast<double>(velocity[0]) * velocity[0] + static_cast<double>(velocity[1]) * velocity[1];
    length_sqr += static_cast<double>(velocity[2]) * velocity[2];
    auto length_sqr_stored = static_cast<float>(length_sqr);
    const double slow =
        tuning_float_lookup(kBreakVertSlow, 1, object) * std::sqrt(static_cast<double>(length_sqr_stored));
    velocity[0] = static_cast<float>(velocity[0] - slow * drawn_normal_.x);
    velocity[2] = static_cast<float>(velocity[2] - slow * drawn_normal_.z);
    velocity[1] = static_cast<float>(tuning_float_lookup(kBreakVertUpScale, 1, object) * velocity[1]);
    body_transform(object).pitch_locally(
        static_cast<float>(tuning_float_lookup(kBreakVertPitch, 1, object) * kRadiansPerDegree));
    clear_flag(this, PoseFlag::kVertAir);
    clear_flag(this, PoseFlag::kVertWall);
    clear_flag(this, PoseFlag::kLeftVert);
    clear_flag(this, PoseFlag::kNoAcidDrop);
    face_along_velocity(object);
    display_matrix_ = body_transform(object);
}

} // namespace skater
