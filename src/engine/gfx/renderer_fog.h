#pragma once

#include <cstdint>

namespace render {

// NOT ORIGINAL (the class; its fields were the original's globals): the distance fog as the device was last told -
// where it begins and ends, and whether it is on. Held by the renderer.
class FogState {
public:
    [[nodiscard]] float begin_distance() const { return begin_distance_; }
    void set_begin_distance(float value) { begin_distance_ = value; }
    [[nodiscard]] float end() const { return end_; }
    void set_end(float value) { end_ = value; }
    [[nodiscard]] uint32_t active() const { return active_; }
    void set_active(uint32_t value) { active_ = value; }

private:
    float begin_distance_{};
    float end_{};
    uint32_t active_{};
};

} // namespace render
