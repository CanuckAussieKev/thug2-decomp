#pragma once

#include <cstdint>

#include "engine/maths/vec4.h"

namespace maths {

// Integer rectangle. Field meanings are assumed (left, top, right, bottom) until its users are decompiled.
struct Rect2 {
    Rect2(int left, int top, int right, int bottom);

    int left, top, right, bottom;
};

// Axis-aligned bounding box.
class BBox {
public:
    // An "empty" box: min at +10,000,000 and max at -10,000,000, so the first append_spot sets both.
    BBox();
    // A zero-size box at `point`.
    explicit BBox(const Vec4& point);

    // (0x0040AB00: the copy assignment - the implicit one; BBox stays trivially copyable)
    // Grow the box to include `point` (x, y, z only).
    void append_spot(const Vec4& point);
    // 0x0040B170: whether the boxes overlap (edges touching counts; NaN doesn't rule it out).
    bool intersects(const BBox& other) const;
    // 0x0040B1D0: whether `other` lies within this box (edges included; NaN rules it out).
    bool get_holds(const BBox& other) const;
    // Centre of the box (w = 1).
    Vec4& get_center(Vec4& out) const;

    // 1 if min <= point <= max on every axis (edges included; NaN gives 0). Returns int: the original sets
    // all of EAX.
    int contains_point(const Vec4& point) const;
    // Number of axes (0-3) on which point lies within the box; `axes` gets a bit per axis (1 = x, 2 = y, 4 = z).
    int count_axes_containing(const Vec4& point, uint32_t& axes) const;
    // Where the segment first enters the box through a face, checking +X, -X, +Y, -Y, +Z, -Z in that order.
    // Fills `hit` (w = 1) and the face's outward normal. False if both ends are inside, both are beyond the
    // same face, or the segment misses every face.
    bool segment_crossing(const Seg& line, Vec4& hit, Vec4& normal) const;
    // For a point inside: the nearest point on the surface (moving along one axis; ties go to the earlier
    // axis, and to max over min). For a point outside: the point clamped onto the box, w = 1.
    void get_closest_surface_point(const Vec4& point, Vec4& out) const;

    Vec4 min;
    Vec4 max;
};

// A line segment with a radius: every point within `radius` of the segment.
class Capsule {
public:
    // The segment left unset, radius 0.
    Capsule();
    // From a start point and the offset to the end point.
    Capsule(const Vec4& start, const Vec4& offset, float radius);
    Capsule(const Seg& line, float radius);

    // Sphere volume plus the cylinder along the segment.
    float get_loudness() const;

    // True if `point` is within the capsule. If `offset` is given it receives the point's offset from the
    // closest point on the segment (whether or not the point is inside).
    bool contains_point(const Vec4& point, Vec4* offset) const;

    // 1 if `segment` passes within the capsule, else 0. Optionally reports where (s on the capsule's line,
    // t on the segment, see Seg::distance_sqr_to_line) and the squared distance between them. Returns int, not
    // bool: the original sets all of EAX.
    int intersects_segment(const Seg& segment, float* s, float* t, float* squared_gap) const;

    Seg line{};
    float radius;
};

// A point and a 32-bit value after it (+0x10). Only its default constructor (0x00403210: the point left unset,
// the value 0) is known; the object built by 0x00418330 starts with one and then sets both. Assumed to be a
// sphere (centre, radius) until its users are decompiled.
struct Sphere {
    Sphere() = default;

    Vec4 center{};
    float radius{0.0f};
};

// 0x004817D0 (cdecl): whether `point` is no further than `max` and no nearer than `min` on x, y and z (edges
// included; a NaN rules nothing out).
bool point_in_box(const Vec4* point, const Vec4* max, const Vec4* min);

} // namespace maths
