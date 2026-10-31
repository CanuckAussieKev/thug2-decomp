#pragma once

#include <cstdint>

#include "game/mth/vec4.h"

namespace Maths {

// Integer rectangle. Field meanings are assumed (left, top, right, bottom) until its users are decompiled.
struct Rect2 {
    Rect2(int left, int top, int right, int bottom);

    int left, top, right, bottom;
};

// Axis-aligned bounding box.
class BBox {
public:
    // An "empty" box: min at +10,000,000 and max at -10,000,000, so the first AppendSpot sets both.
    BBox();
    // A zero-size box at `point`.
    explicit BBox(const Vec4& point);

    // 0x0040AB00
    BBox& operator=(const BBox& other);
    // Grow the box to include `point` (x, y, z only).
    void AppendSpot(const Vec4& point);
    // 0x0040B170: whether the boxes overlap (edges touching counts; NaN doesn't rule it out).
    bool Intersects(const BBox& other) const;
    // 0x0040B1D0: whether `other` lies within this box (edges included; NaN rules it out).
    bool Holds(const BBox& other) const;
    // Centre of the box (w = 1).
    Vec4& GetCenter(Vec4& out) const;

    // 1 if min <= point <= max on every axis (edges included; NaN gives 0). Returns int: the original sets
    // all of EAX.
    int ContainsPoint(const Vec4& point) const;
    // Number of axes (0-3) on which point lies within the box; `axes` gets a bit per axis (1 = x, 2 = y, 4 = z).
    int CountAxesContaining(const Vec4& point, uint32_t& axes) const;
    // Where the segment first enters the box through a face, checking +X, -X, +Y, -Y, +Z, -Z in that order.
    // Fills `hit` (w = 1) and the face's outward normal. False if both ends are inside, both are beyond the
    // same face, or the segment misses every face.
    bool SegmentCrossing(const Seg& line, Vec4& hit, Vec4& normal) const;
    // For a point inside: the nearest point on the surface (moving along one axis; ties go to the earlier
    // axis, and to max over min). For a point outside: the point clamped onto the box, w = 1.
    void GetClosestSurfacePoint(const Vec4& point, Vec4& out) const;

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
    float Loudness() const;

    // True if `point` is within the capsule. If `offset` is given it receives the point's offset from the
    // closest point on the segment (whether or not the point is inside).
    bool ContainsPoint(const Vec4& point, Vec4* offset) const;

    // 1 if `segment` passes within the capsule, else 0. Optionally reports where (s on the capsule's line,
    // t on the segment, see Seg::DistanceSqrToLine) and the squared distance between them. Returns int, not
    // bool: the original sets all of EAX.
    int IntersectsSegment(const Seg& segment, float* s, float* t, float* squaredGap) const;

    Seg line;
    float radius;
};

// A point and a 32-bit value after it (+0x10). Only its default constructor (0x00403210: the point left unset,
// the value 0) is known; the object built by 0x00418330 starts with one and then sets both. Assumed to be a
// sphere (centre, radius) until its users are decompiled.
struct Sphere {
    Sphere() : radius(0.0f) {}

    Vec4 center;
    float radius;
};

// 0x004817D0 (cdecl): whether `point` is no further than `max` and no nearer than `min` on x, y and z (edges
// included; a NaN rules nothing out).
bool __cdecl PointInBox(const Vec4* point, const Vec4* max, const Vec4* min);

} // namespace Mth
