#pragma once

#include <array>
#include <cstdint>
#include "platform/platform_input.h"

// NOT ORIGINAL - the platform layer's state its files share: the graphics options and draw distance read from the
// registry, DirectInput, the joysticks with their rumble effects and force resolutions, and the joysticks' last two
// states (the original's globals).
namespace os_layer {

// The platform layer's state its files share: the graphics options and draw distance read from the registry,
// DirectInput, the joysticks with their rumble effects and force resolutions, and the joysticks' last two states. Made
// on first use, never destroyed.
class PlatformState {
public:
    void set_option37(uint8_t value) { option37_ = value; }
    [[nodiscard]] uint16_t options38_and39() const { return options38_and39_; }
    void set_options38_and39(uint16_t value) { options38_and39_ = value; }
    void set_unknown643b(uint8_t value) { unknown643b_ = value; }
    void set_distance_setting(uint32_t value) { distance_setting_ = value; }
    IDirectInput8A*& direct_input() { return direct_input_; }
    std::array<IDirectInputDevice8A*, kPorts>& joysticks() { return joysticks_; }
    std::array<IDirectInputEffect*, kPorts>& effects() { return effects_; }
    std::array<uint32_t, kPorts>& force_axis_counts() { return force_axis_counts_; }
    std::array<uint32_t, 2 * kPorts>& force_resolutions() { return force_resolutions_; }
    std::array<DIJOYSTATE2, 2>& joystick_states() { return joystick_states_; }
    std::array<DIJOYSTATE2, 2>& last_joystick_states() { return last_joystick_states_; }

private:
    // (platform.cpp)
    uint8_t option37_{}; // (the graphics options: platform_input.h)
    uint16_t options38_and39_{}; // (the distance clipping flag follows: gfx/settings.cpp)
    uint8_t unknown643b_{};
    uint32_t distance_setting_{};

    // (platform_input.cpp)
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
PlatformState& platform_state();

} // namespace os_layer
