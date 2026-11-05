#pragma once

#include <span>

#include "base/maths/vec4.h"

namespace render {

// The collision code's intersection tests (cdecl helpers). Their arithmetic follows the original's x87 code: values
// are rounded to floats only where it stores them.

// A sphere as the line test takes it: the centre and, after it, the radius.
struct CollSphere {
    maths::Vec4 centre;
    float radius;
};

// 0x0040B420: whether the ray from `start` along `direction` hits the triangle v0 v1 v2 from its front (the side
// (v1 - v0) x (v2 - v0) points to; a determinant below 0.03 misses) at a distance along it - *t, set when it gets
// that far - between 0 and 1. The barycentric bounds allow the determinant times 1.00001.
bool ray_hits_triangle(const maths::Vec4& start, const maths::Vec4& direction, const maths::Vec4& v0,
                       const maths::Vec4& v1, const maths::Vec4& v2, float* hit_distance);

// 0x0040B610: whether the line through `base` along `end` - `base` passes within the sphere's radius of its centre,
// and the nearer crossing (*t, measured from `start`) is at most `limit`.
bool line_hits_sphere(const maths::Vec4& start, const maths::Vec4& base, const CollSphere& sphere,
                      const maths::Vec4& end, float* hit_distance, float limit);

// A ray in the plane: its start and its direction.
struct Ray2 {
    float x{};
    float y{};
    float dx{};
    float dy{};
};

// 0x0040B750: whether the 2D segments (ax, ay) + s (adx, ady) and (bx, by) + u (bdx, bdy) cross for s and u in
// 0..1; *s set (0 for parallel ones).
bool segments_cross_2d(Ray2 a, Ray2 b, float* along_first);

// Where the nearest point's results go: the squared distance and the parameter.
struct TriangleNearest {
    float* out{};
    float* t_out_{};
};

// 0x0040B860: the squared distance from `point` to the triangle v0 v1 v2 (Eberly's regions), and where the nearest
// point is: v0 + s (v1 - v0) + t (v2 - v0) (*s, *t when not null). Returned unrounded (the original leaves it in
// ST0 and some callers compare it before storing). One region keeps the original's slip: below the first edge's
// start (s < 0, t >= 0) e is compared with c without its sign flipped, so it always clamps along the second edge.
double point_triangle_distance_squared(const maths::Vec4& point, const maths::Vec4& v0, const maths::Vec4& v1,
                                       const maths::Vec4& v2, TriangleNearest nearest);

// Where the nearest points' results go: the squared distance and the parameters.
struct SegmentNearest {
    float* out{};
    float* t_out_{};
    float* along_out_{};
};

// 0x0040BDC0 (cdecl): the squared distance between `segment` and the triangle v0 v1 v2 (Eberly's segment /
// triangle regions). The nearest points: segment.start + *along (segment.end - segment.start) on the segment and
// v0 + *s (v1 - v0) + *t (v2 - v0) on the triangle (each written when not null). Outside the triangle's interior
// the edges are measured with Seg::distance_sqr_to_line and the segment's ends with point_triangle_distance_squared;
// a segment (nearly) parallel to the triangle tries all three edges and both ends. Some results come back rounded
// to float, some not, as the original's. `normal` isn't read. A segment crossing the triangle's interior prints
// "uh oh..." and returns 0 (*along, *s, *t where it crosses).
double segment_triangle_distance_squared(const maths::Seg& segment, const maths::Vec4& normal, const maths::Vec4& v0,
                                         const maths::Vec4& v1, const maths::Vec4& v2, SegmentNearest nearest);

// A parallelogram: a corner and its two sides (the rect queries' shape).
struct CollRect {
    maths::Vec4 origin;
    maths::Vec4 side_a_;
    maths::Vec4 side_b_;
};

// 0x0040D370 (cdecl): where the rect and the triangle v0 v1 v2 cross - two points, true. The triangle's edges cut
// by the rect's plane give a segment P Q (each end on an edge: 1 v0-v1, 2 v1-v2, 3 v2-v0); in the plane the normal
// is largest across, each end inside the rect is a point, and each rect side crossing P Q another (kind 0). Points
// go to points[0], points[1] and their kinds to kinds[0], kinds[1]; false unless two were found (one may still have
// been written).
bool rect_hits_triangle(const CollRect& rect, const maths::Vec4& v0, const maths::Vec4& v1, const maths::Vec4& v2,
                        std::span<maths::Vec4, 2> points, std::span<int, 2> kinds);

} // namespace render
