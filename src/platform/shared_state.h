#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "platform/platform_input.h"

namespace os_layer {

// NOT ORIGINAL grouping - platform.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (platform_shared()).
struct PlatformShared {
    uint8_t option37_{}; // (the graphics options: platform_input.h)
    uint16_t options38_and39_{}; // (the distance clipping flag follows: gfx/settings.cpp)
    uint8_t unknown643b_{};
    uint32_t distance_setting_{};
};
PlatformShared& platform_shared();

} // namespace os_layer

namespace os_layer {

// NOT ORIGINAL grouping - platform_input.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (platform_input_shared()).
struct PlatformInputShared {
    IDirectInput8A* direct_input_{};
    std::array<IDirectInputDevice8A*, kPorts> joysticks_{};
    std::array<IDirectInputEffect*, kPorts> effects_{}; // each joystick's constant-force effect (rumble)
    std::array<uint32_t, kPorts> force_axis_counts_{};
    std::array<uint32_t, 2 * kPorts> force_resolutions_{};
    // The joysticks' states: this frame's and the last.
    std::array<DIJOYSTATE2, 2> joystick_states_{};
    std::array<DIJOYSTATE2, 2> last_joystick_states_{};
};
PlatformInputShared& platform_input_shared();

} // namespace os_layer
