#pragma once

namespace maths {

// NOT ORIGINAL - a point or an offset in a plane (a pixel on the screen, a position in the 2D layout): x and y.
// A value type (no invariant): its data public, compared member by member.
struct Vec2 {
    float x;
    float y;
    bool operator==(const Vec2&) const = default;
};

} // namespace maths
