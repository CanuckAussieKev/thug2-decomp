// The Vibration component: see vibration_component.h. From the disassembly of 0x0043D490-0x0043D83F (0x0043D490 and
// the member function 0x0043D6F0 aren't Ghidra functions; Ghidra split 0x0043D610 at 0x0043D61F).

#include "engine/objects/vibration_component.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/camera_lookaround.h"
#include "base/core/checked_index.h"

namespace objects {
namespace {
constexpr uint32_t kVibrate = 0xDF8A5F0A; // Vibrate
constexpr uint32_t kVibration = 0x404EB936; // Vibration
} // namespace

std::array<script::MemberFunctionEntry, 2>& vibration_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, 2> vibration_component_member_functions{{
        {.callsign_ = kVibrate, .owner_kind_ = kVibration}, {.callsign_ = 0, .owner_kind_ = kVibration}}};
    return vibration_component_member_functions;
}

namespace {

constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kActuator = 0x114FC131;
constexpr uint32_t kPercent = 0x9E497FC6;
constexpr uint32_t kDuration = 0x79A07F3F;
const float kMilliseconds = 1000.0F; // 0x006453C0

// The original's _ftol (0x006236FC): truncated towards zero (the low half of the 64-bit result).
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }


} // namespace

// 0x0043D670
gsl::owner<ObjectModule*> PadRumbleModule::fabricate() { return new PadRumbleModule; }

// (the constructor: inlined in fabricate in the original)
PadRumbleModule::PadRumbleModule()
{
    set_kind_tag(kType);
    for (int i = kActuators - 1; i >= 0; --i) {
        core::at(timers_, i).running = false;
    }
}

// 0x0043D490
void PadRumbleModule::publish_method_table()
{
    script::register_member_function_table(vibration_component_member_functions());
}

// 0x0043D4B0 (0x0043D5E0 the deleting one)
PadRumbleModule::~PadRumbleModule()
{
    if (pad_dev_) {
        for (int i = kActuators - 1; i >= 0; --i) {
            pad_dev_->fire_actuator(i, 0);
        }
    }
}

// 0x0043D610
void PadRumbleModule::tick_frame()
{
    if (!active_) {
        hold_off(true);
        return;
    }
    update_timers();
}

// 0x0043D61F
void PadRumbleModule::update_timers()
{
    for (int i = kActuators - 1; i >= 0; --i) {
        Stopwatch& timer = core::at(timers_, i);
        if (!timer.running) {
            continue;
        }
        const int start = timer.start;
        const int clock = frame_clock::game_clock_elapsed();
        const uint32_t elapsed =
            static_cast<uint32_t>(clock) + (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start));
        if (elapsed >= timer.duration) {
            pad_dev_->fire_actuator(i, 0);
            timer.running = false;
        }
    }
}

// 0x0043D520
void PadRumbleModule::stop_all()
{
    if (!active_) {
        return;
    }
    for (int i = kActuators - 1; i >= 0; --i) {
        pad_dev_->fire_actuator(i, 0);
        core::at(timers_, i).running = false;
    }
}

// 0x0043D550
void PadRumbleModule::reset_device() { pad_dev_->clear_rumble(); }

// 0x0043D560
void PadRumbleModule::change_live(bool active)
{
    if (active == active_) {
        return;
    }
    if (!active) {
        active_ = false;
        hold_off(true);
        if (pad_dev_) {
            pad_dev_->stop_actuators();
            pad_dev_->stop_rumble();
        }
    } else {
        active_ = true;
        hold_off(false);
        if (pad_dev_) {
            pad_dev_->enable_actuators();
        }
    }
}

// 0x0043D5C0
void PadRumbleModule::change_gadget(sio::InputPort* device)
{
    pad_dev_ = device;
    if (active_) {
        device->enable_actuators();
    } else {
        device->stop_rumble();
    }
}

// 0x0043D6F0
bool PadRumbleModule::vibrate(script::Struct* params, script::Script* /*script*/)
{
    if (!params->has_bare_flag(kOff) &&
        (simulation_halted() || ((entity()->composite_flags() & kCompositeFlagPaused) != 0U))) {
        return true;
    }
    if (!active_) {
        return true;
    }
    int actuator = 0;
    params->fetch_int_value(kActuator, &actuator, 0);
    float percent = 0.0F;
    params->fetch_float(kPercent, &percent, 0);
    if (params->has_bare_flag(kOff)) {
        pad_dev_->stop_actuators();
        for (int i = kActuators - 1; i >= 0; --i) {
            core::at(timers_, i).running = false;
        }
    } else {
        pad_dev_->fire_actuator(actuator, ftol(percent));
    }
    float duration = 0.0F;
    if (params->fetch_float(kDuration, &duration, 0)) {
        core::at(timers_, actuator).running = true;
        core::at(timers_, actuator).duration = static_cast<uint32_t>(ftol(static_cast<double>(duration) * kMilliseconds));
        const int clock = frame_clock::game_clock_elapsed();
        core::at(timers_, actuator).start = clock + clock_offset();
    } else {
        core::at(timers_, actuator).running = false;
    }
    return true;
}

} // namespace objects
