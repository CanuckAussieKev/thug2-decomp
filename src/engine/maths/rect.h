#pragma once

#include "engine/maths/vec2.h"

namespace maths {

// NOT ORIGINAL - a rectangle in a plane by its edges: x0 the left, y0 the top, x1 the right, y1 the bottom (screen,
// window or layout coordinates: y grows downwards). A value type (no invariant): its data public, compared member by
// member; its helpers are free functions.
struct Rect {
    float x0;
    float y0;
    float x1;
    float y1;
    bool operator==(const Rect&) const = default;
};

[[nodiscard]] constexpr float width_of(const Rect& rect) { return rect.x1 - rect.x0; }
[[nodiscard]] constexpr float height_of(const Rect& rect) { return rect.y1 - rect.y0; }

// NOT ORIGINAL - an extent in a plane: a width and a height (a window's, an image's, a box's).
struct Size {
    float width;
    float height;
    bool operator==(const Size&) const = default;
};

// NOT ORIGINAL - a point in whole pixels (a window's or a texture's).
struct PixelPoint {
    int x;
    int y;
    bool operator==(const PixelPoint&) const = default;
};

// NOT ORIGINAL - an extent in whole pixels: a width and a height (a window's, a back buffer's, a texture's).
struct PixelSize {
    int width;
    int height;
    bool operator==(const PixelSize&) const = default;
};

// NOT ORIGINAL - a rectangle by its top left corner and its size (where the original keeps x, y, width, height - a
// Rect's right and bottom would round differently).
struct Box {
    Vec2 at;
    Size size;
    bool operator==(const Box&) const = default;
};

} // namespace maths
