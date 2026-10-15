#pragma once

#include "game/mth/vector.h"

namespace Mth {

// Axis-aligned bounding box.
class BBox {
public:
    // An "empty" box: min at +10,000,000 and max at -10,000,000, so the first AddPoint sets both.
    BBox();
    // A zero-size box at `point`.
    explicit BBox(const Vector& point);

    // Grow the box to include `point` (x, y, z only).
    void AddPoint(const Vector& point);

    Vector min;
    Vector max;
};

// A line segment with a radius: every point within `radius` of the segment.
class Capsule {
public:
    // From a start point and the offset to the end point.
    Capsule(const Vector& start, const Vector& offset, float radius);
    Capsule(const Line& line, float radius);

    // Sphere volume plus the cylinder along the segment.
    float Volume() const;

    // True if `point` is within the capsule. If `offset` is given it receives the point's offset from the
    // closest point on the segment (whether or not the point is inside).
    bool ContainsPoint(const Vector& point, Vector* offset) const;

    Line line;
    float radius;
};

} // namespace Mth
