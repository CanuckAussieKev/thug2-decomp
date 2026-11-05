#pragma once

#include <array>
#include <cstddef>

namespace objects {

constexpr std::size_t kStatOverrideCount = 10;

// NOT ORIGINAL (the class; its field is the skater's +0x290-+0x2B7): the skater's ten stats as its profile (or classic
// mode) set them - each one -1 (by its bits) when not set. Rider::fetch_tally puts the cheats, the skate module's
// forced value and the online rules on top. Owned by the skater, in its place there.
class StatOverrides {
public:
    // Not original - the field's offset checked against the original's at compile time (rider.cpp).
    static void check_layout();

    // 0x0057FE30: stat `index` set to `value`, kept within 0-10 when 0x005A1180 says so.
    void set(int index, float value);
    // Whether stat `index` is unset (-1, compared by its bits).
    [[nodiscard]] bool is_unset(int index) const;
    [[nodiscard]] float value(int index) const;

private:
    std::array<float, kStatOverrideCount> values_{}; // +0x00 (the skater's +0x290)
};

} // namespace objects
