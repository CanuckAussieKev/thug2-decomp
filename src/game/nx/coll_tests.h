#pragma once

#include "game/mth/vector.h"

namespace Nx {

// The collision code's intersection tests (cdecl helpers). Their arithmetic follows the original's x87 code: values
// are rounded to floats only where it stores them.

// A sphere as the line test takes it: the centre and, after it, the radius.
struct CollSphere {
    Mth::Vector centre;
    float radius;
};

// 0x0040B420: whether the ray from `start` along `direction` hits the triangle v0 v1 v2 from its front (the side
// (v1 - v0) x (v2 - v0) points to; a determinant below 0.03 misses) at a distance along it - *t, set when it gets
// that far - between 0 and 1. The barycentric bounds allow the determinant times 1.00001.
bool RayHitsTriangle(const Mth::Vector& start, const Mth::Vector& direction, const Mth::Vector& v0,
                     const Mth::Vector& v1, const Mth::Vector& v2, float* t);

// 0x0040B610: whether the line through `base` along `end` - `base` passes within the sphere's radius of its centre,
// and the nearer crossing (*t, measured from `start`) is at most `limit`.
bool LineHitsSphere(const Mth::Vector& start, const Mth::Vector& base, const CollSphere& sphere,
                    const Mth::Vector& end, float* t, float limit);

// 0x0040B750: whether the 2D segments (ax, ay) + s (adx, ady) and (bx, by) + u (bdx, bdy) cross for s and u in
// 0..1; *s set (0 for parallel ones).
bool SegmentsCross2D(float ax, float ay, float adx, float ady, float bx, float by, float bdx, float bdy, float* s);

// 0x0040B860: the squared distance from `point` to the triangle v0 v1 v2 (Eberly's regions), and where the nearest
// point is: v0 + s (v1 - v0) + t (v2 - v0) (*s, *t when not null). Returned unrounded (the original leaves it in
// ST0 and some callers compare it before storing). One region keeps the original's slip: below the first edge's
// start (s < 0, t >= 0) e is compared with c without its sign flipped, so it always clamps along the second edge.
double PointTriangleDistanceSquared(const Mth::Vector& point, const Mth::Vector& v0, const Mth::Vector& v1,
                                    const Mth::Vector& v2, float* s, float* t);

// A parallelogram: a corner and its two sides (the rect queries' shape).
struct CollRect {
    Mth::Vector origin;
    Mth::Vector sideA;
    Mth::Vector sideB;
};

// 0x0040D370 (cdecl): where the rect and the triangle v0 v1 v2 cross - two points, true. The triangle's edges cut
// by the rect's plane give a segment P Q (each end on an edge: 1 v0-v1, 2 v1-v2, 3 v2-v0); in the plane the normal
// is largest across, each end inside the rect is a point, and each rect side crossing P Q another (kind 0). Points
// go to points[0], points[1] and their kinds to kinds[0], kinds[1]; false unless two were found (one may still have
// been written).
bool RectHitsTriangle(const CollRect& rect, const Mth::Vector& v0, const Mth::Vector& v1, const Mth::Vector& v2,
                      Mth::Vector* points, int* kinds);

// LineHitsSphere / SegmentsCross2D as the game calls them: their callers (0x0040EC90, 0x0040D370) keep a value in
// EDX across the call, which the originals don't touch - these adapters keep it.
void LineHitsSphereKeepingEdx();
void SegmentsCross2DKeepingEdx();

} // namespace Nx
