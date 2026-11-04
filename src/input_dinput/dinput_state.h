#pragma once

#include <windows.h>
#include <dinput.h>

#include <array>
#include <cstdint>

#include "engine/pad_input/port_controls.h"

// NOT ORIGINAL - the DirectInput side's state its files share: DirectInput, the joysticks with their rumble effects and
// force resolutions, and the joysticks' last two states (the original's globals).
namespace os_layer {

// Made on first use, never destroyed.
class DirectInputState {
public:
    IDirectInput8A*& direct_input() { return direct_input_; }
    std::array<IDirectInputDevice8A*, kPorts>& joysticks() { return joysticks_; }
    std::array<IDirectInputEffect*, kPorts>& effects() { return effects_; }
    std::array<uint32_t, kPorts>& force_axis_counts() { return force_axis_counts_; }
    std::array<uint32_t, 2 * kPorts>& force_resolutions() { return force_resolutions_; }
    std::array<DIJOYSTATE2, 2>& joystick_states() { return joystick_states_; }
    std::array<DIJOYSTATE2, 2>& last_joystick_states() { return last_joystick_states_; }

private:
    IDirectInput8A* direct_input_{};
    std::array<IDirectInputDevice8A*, kPorts> joysticks_{};
    std::array<IDirectInputEffect*, kPorts> effects_{}; // each joystick's constant-force effect (rumble)
    std::array<uint32_t, kPorts> force_axis_counts_{};
    std::array<uint32_t, 2 * kPorts> force_resolutions_{};
    // The joysticks' states: this frame's and the last.
    std::array<DIJOYSTATE2, 2> joystick_states_{};
    std::array<DIJOYSTATE2, 2> last_joystick_states_{};
};

// The state (made on first use, never destroyed).
DirectInputState& direct_input_state();

} // namespace os_layer
