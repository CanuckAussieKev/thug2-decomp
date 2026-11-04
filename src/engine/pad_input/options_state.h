#pragma once

#include <cstdint>

// NOT ORIGINAL - the graphics options the original's input set-up clears and its registry loader reads
// (os_layer::init_input, load_registry_options - engine/pad_input/port_controls.h): the original's globals beside
// the graphics options (0x007D6434...) that have no other home.
namespace os_layer {

// Made on first use, never destroyed.
class OptionsState {
public:
    void set_option37(uint8_t value) { option37_ = value; }
    [[nodiscard]] uint16_t options38_and39() const { return options38_and39_; }
    void set_options38_and39(uint16_t value) { options38_and39_ = value; }
    void set_unknown643b(uint8_t value) { unknown643b_ = value; }
    void set_distance_setting(uint32_t value) { distance_setting_ = value; }

private:
    uint8_t option37_{}; // (the graphics options: port_controls.h)
    uint16_t options38_and39_{}; // (the distance clipping flag follows: gfx/settings.cpp)
    uint8_t unknown643b_{};
    uint32_t distance_setting_{};
};

// The state (made on first use, never destroyed).
OptionsState& options_state();

} // namespace os_layer
