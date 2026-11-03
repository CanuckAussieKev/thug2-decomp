// The CameraLookAround component (GameObjects::FreeLookModule): see camera_lookaround.h. From the
// disassembly of 0x004E3F60-0x004E4336 (0x004E3FA0 isn't a Ghidra function).

#include "engine/objects/camera_lookaround.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>

#include "engine/objects/control_pad.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/thaw_bmx.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

constexpr uint32_t kCameraLookAround = 0x81C7122D;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kTurnTo = 0xB8F23899, kStopTurning = 0xE3B327C0;
constexpr uint32_t kLookaroundOnDPadOn = 0xDB02EE92, kLookaroundOnDPadOff = 0x72D2BEE3; // (THAW's members)
constexpr uint32_t kHeading = 0xFD4BC03E, kTilt = 0xE3C07609, kTime = 0x906B67BA, kFactor = 0x48D4868B;
constexpr uint32_t kSecondPad = 0x702247A5, kNoButton = 0xF3E055E1; // script globals
constexpr uint32_t kSpecialObject = 0x967C138C;
constexpr uint32_t kSpecialScript = 0x60871393, kScript = 0xA1B1146D;

constexpr float kUpScale = 0.699999988079071F;    // 0x0064B2C8
constexpr float kDownScale = 0.5F;                // 0x006453BC
constexpr float kTiltScale = -1.399999976158142F; // 0x0064B2C4
constexpr float kHeadingScale = 3.0F;             // 0x006467A8
constexpr float kNear = 0.0010000000474974513F;   // 0x006461D8
constexpr float kEase = 3.75F;                    // 0x0064B2C0

// A button's press this frame forgotten.

// Where an angle eases to and the frame's share of the way.
struct EaseStep {
    double target{};
    float frame{};
};

// One angle eased toward `target` (the original's x87 sequence: the difference rounded, its size unrounded).
void get_ease(float& angle, EaseStep ease_step)
{
    const auto target = ease_step.target;
    const auto frame = ease_step.frame;
    const double difference = target - angle;
    auto rounded = static_cast<float>(difference);
    if (std::fabs(difference) > kNear)
        angle = static_cast<float>((static_cast<double>(rounded) * frame * kEase) + angle);
    else
        angle = static_cast<float>(target);
}

} // namespace

// 0x004E42E0
gsl::owner<ObjectModule*> FreeLookModule::fabricate() { return new FreeLookModule; }

// (the constructor: inlined in fabricate in the original)
FreeLookModule::FreeLookModule() { set_kind_tag(kCameraLookAround); }

// 0x004E3F60 (0x004E4010 the deleting one)
FreeLookModule::~FreeLookModule() = default;

// 0x004E3F70
void FreeLookModule::configure_from_data(script::Struct* /*params*/)
{
    lean_angle_ = 0.0F;
    heading_ = 0.0F;
    factor_ = 1.0F;
    locked_ = false;
    turning_ = false;
}

// 0x004E4040
void FreeLookModule::seal() { input_ptr_ = core::down_cast<PadInputModule>(entity()->fetch_module(kInput)); }

// 0x004E4000
void FreeLookModule::set_locked(bool locked) { locked_ = locked; }

// 0x004E3FA0
void FreeLookModule::turn_to(LookTurn look_turn)
{
    const auto heading = look_turn.heading;
    const auto tilt = look_turn.tilt;
    const auto time = look_turn.time;
    const auto factor = look_turn.factor;
    target_heading_ = heading;
    turning_ = true;
    target_tilt_ = tilt;
    if (time > 0.0F) {
        const double inverse = 1.0 / time;
        heading_rate_ = static_cast<float>((static_cast<double>(heading) - heading_) * inverse);
        tilt_rate_ = static_cast<float>((static_cast<double>(tilt) - lean_angle_) * inverse);
    }
    seconds_remaining_ = time;
    factor_ = factor;
}

// 0x004E4060
void FreeLookModule::tick_frame()
{
    PadSnapshot* pad = script::fetch_int_value(kSecondPad) ? &input_ptr_->pad_secondary() : &input_ptr_->pad_primary();
    if (!script::fetch_int_value(kNoButton) && pad->button_choose().fired()) {
        pad->button_choose().reset_activation();
        script::launch_script(entity()->ident() == kSpecialObject ? kSpecialScript : kScript, nullptr, nullptr, false,
                              nullptr);
    }
    if (pad->stick_click_right().fired()) {
        locked_ = !locked_;
        pad->stick_click_right().reset_activation();
    }
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    if (!locked_ && !turning_ && dpad_look_ && skater::thaw_bmx::is_switched_on()) {
        // NOT ORIGINAL - THAW's BMX (THAW 0x5B08F0 with its +0x40 set): the d-pad's inputs instead of
        // the right stick's - with THAW's default inversion -, eased as the stick's (the heading without the stick's
        // x 3). Original behaviour: the stick's, below, always.
        const PadSnapshot* p = pad;
        const skater::thaw_bmx::LookInputs in = skater::thaw_bmx::d_pad_look_inputs(
            p->up_dir().held_down() != 0, p->btn_down().held_down() != 0, p->left_side().held_down() != 0,
            p->right_side().held_down() != 0, skater::thaw_bmx::kLookInvertDefault);
        const float tilt = in.tilt * (in.tilt < 0.0F ? kUpScale : kDownScale) * kTiltScale;
        get_ease(lean_angle_, {.target = tilt, .frame = frame});
        get_ease(heading_, {.target = in.heading, .frame = frame});
    } else if (!locked_ && !turning_) {
        const float stick_y = core::at(pad->scaled_stick(), 1);
        const double tilt = static_cast<double>(stick_y) * (stick_y < 0.0F ? kUpScale : kDownScale) * kTiltScale;
        get_ease(lean_angle_, {.target = tilt, .frame = frame});
        const double heading = static_cast<double>(core::at(pad->scaled_stick(), 0)) * kHeadingScale;
        get_ease(heading_, {.target = heading, .frame = frame});
    }
    auto step = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    if (turning_ && seconds_remaining_ > 0.0F) {
        if (seconds_remaining_ > step) {
            heading_ = static_cast<float>((static_cast<double>(step) * heading_rate_) + heading_);
            lean_angle_ = static_cast<float>((static_cast<double>(step) * tilt_rate_) + lean_angle_);
            seconds_remaining_ = seconds_remaining_ - step;
        } else {
            heading_ = target_heading_;
            lean_angle_ = target_tilt_;
            seconds_remaining_ = 0.0F;
        }
    }
}

// 0x004E4220
int FreeLookModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == kTurnTo) {
        float heading = 0.0F;
        float tilt = 0.0F;
        float time = 0.0F;
        float factor = 1.0F;
        params->fetch_float(kHeading, &heading, 1);
        params->fetch_float(kTilt, &tilt, 1);
        params->fetch_float(kTime, &time, 1);
        params->fetch_float(kFactor, &factor, 0);
        turn_to({.heading = heading, .tilt = tilt, .time = time, .factor = factor});
        return 1;
    }
    if (name == kStopTurning) {
        turning_ = false;
        return 1;
    }
    // NOT ORIGINAL - THAW's BMX: THAW's members (its 0x5B0B50 / 0x5B0B60: +0x40 on / off), bike_start's and
    // bmx_finish_off's - only while the feature is on (else 2, as THUG2's).
    if ((name == kLookaroundOnDPadOn || name == kLookaroundOnDPadOff) && skater::thaw_bmx::is_switched_on()) {
        dpad_look_ = name == kLookaroundOnDPadOn;
        skater::thaw_bmx::note_d_pad_camera(dpad_look_);
        return 1;
    }
    return 2;
}

} // namespace objects
