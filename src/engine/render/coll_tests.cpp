// The collision code's intersection tests.

#include "engine/render/coll_tests.h"

#include <cmath>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/dbg.h"
#include "engine/maths/vec4.h"

namespace render {

constexpr double kCrossTermFactor = 2.0; // the quadratic form's mixed terms are doubled

namespace {

constexpr float kMinDeterminant = 0.03f;
constexpr float kBoundSlack = 1.00001f;
constexpr float kParallel = 1e-11f;
// A segment whose direction is this close to across the triangle's normal (squared, relative) counts as parallel
// to it (0x006453D4).
constexpr float kParallelSegment = 1e-4f;

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// A plane distance this small counts as on the plane (0x006453B8).
constexpr float kOnPlane = 0.00001f;

float get_component(const maths::Vec4& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// Where the triangle edge from `a` to `b` - plane distances da and db, on either side - meets the plane:
// a + (b - a) da / (da - db); a itself when the distances are (nearly) equal. Rounded as the original's x87 code
// does it for P (and for Q on v0 v2): the x and w parts of (b - a) da rounded before the division, y and z not.
maths::Vec4 edge_crossing(const maths::Vec4& a, const maths::Vec4& b, float da, float db)
{
    const double difference = static_cast<double>(da) - db;
    if (!(std::fabs(difference) > kOnPlane))
        return a;
    const double ex = static_cast<double>(b.x) - a.x;
    const double ey = static_cast<double>(b.y) - a.y;
    const float ez = get_difference(b.z, a.z);
    const float ew = get_difference(b.w, a.w);
    auto sx = static_cast<float>(ex * da);
    auto sw = static_cast<float>(static_cast<double>(ew) * da);
    auto inverse = static_cast<float>(1.0 / difference);
    maths::Vec4 p{};
    p.x = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(sx) * inverse)) + a.x);
    p.y = static_cast<float>(static_cast<double>(static_cast<float>(ey * da * inverse)) + a.y);
    p.z = static_cast<float>(static_cast<double>(ez) * da * inverse + a.z);
    p.w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(sw) * inverse)) + a.w);
    return p;
}

// The same as Q's other form computes it: (b - a) da rounded part by part, divided by (da - db) as a float
// (maths::operator/), added to a.
maths::Vec4 edge_crossing_scaled(const maths::Vec4& a, const maths::Vec4& b, float da, float db)
{
    const double difference = static_cast<double>(da) - db;
    if (!(std::fabs(difference) > kOnPlane))
        return a;
    const float ez = get_difference(b.z, a.z);
    const float ew = get_difference(b.w, a.w);
    const maths::Vec4 scaled{.x = static_cast<float>((static_cast<double>(b.x) - a.x) * da),
                             .y = static_cast<float>((static_cast<double>(b.y) - a.y) * da),
                             .z = static_cast<float>(static_cast<double>(ez) * da),
                             .w = static_cast<float>(static_cast<double>(ew) * da)};
    const maths::Vec4 r = maths::operator/(scaled, static_cast<float>(difference));
    return {.x = a.x + r.x, .y = r.y + a.y, .z = r.z + a.z, .w = r.w + a.w};
}

// Whether a point is on the rect's side of a side's line: the line value at or above 0 when the rect lies towards
// positive values, else at or below.
bool get_inside(bool towards_positive, double value) { return towards_positive ? value >= 0.0 : value <= 0.0; }

} // namespace

// 0x0040B420 (NaN in the determinant or the first barycentric coordinate doesn't rule a hit out by itself)
bool ray_hits_triangle(const maths::Vec4& start, const maths::Vec4& direction, const maths::Vec4& v0,
                       const maths::Vec4& v1, const maths::Vec4& v2, float* t)
{
    const maths::Vec4 leg_alpha{.x = get_difference(v1.x, v0.x), .y = get_difference(v1.y, v0.y), .z = get_difference(v1.z, v0.z),
                                .w = get_difference(v1.w, v0.w)};
    const float e2x = get_difference(v2.x, v0.x);
    const float e2y = get_difference(v2.y, v0.y);
    const float e2z = get_difference(v2.z, v0.z);
    // p = direction x edge2
    const double px = static_cast<double>(e2z) * direction.y - static_cast<double>(e2y) * direction.z;
    const double py = static_cast<double>(e2x) * direction.z - static_cast<double>(e2z) * direction.x;
    const double pz = static_cast<double>(e2y) * direction.x - static_cast<double>(e2x) * direction.y;
    const double determinant = (leg_alpha.z * pz + px * leg_alpha.x) + py * leg_alpha.y;
    if (determinant < kMinDeterminant)
        return false;
    auto det = static_cast<float>(determinant);
    auto bound = static_cast<float>(static_cast<double>(det) * kBoundSlack);
    const maths::Vec4 offset{.x = get_difference(start.x, v0.x), .y = get_difference(start.y, v0.y),
                             .z = get_difference(start.z, v0.z), .w = get_difference(start.w, v0.w)};
    auto u = static_cast<float>((offset.z * pz + offset.y * py) + offset.x * px);
    if (u < 0.0f || u > bound)
        return false;
    const maths::Vec4 q = maths::cross_vec(offset, leg_alpha);
    const double v = (static_cast<double>(q.z) * direction.z + static_cast<double>(q.x) * direction.x) +
                     static_cast<double>(q.y) * direction.y;
    if (v < 0.0 || u + v > bound)
        return false;
    const double along = (1.0 / det) * ((static_cast<double>(q.z) * e2z + static_cast<double>(q.y) * e2y) +
                                        static_cast<double>(q.x) * e2x);
    *t = static_cast<float>(along);
    return along <= 1.0 && !(along < 0.0);
}

// 0x0040B610
bool line_hits_sphere(const maths::Vec4& start, const maths::Vec4& base, const CollSphere& sphere,
                      const maths::Vec4& end, float* t, float limit)
{
    const double dx = static_cast<double>(end.x) - base.x;
    const double dy = static_cast<double>(end.y) - base.y;
    const double dz = static_cast<double>(end.z) - base.z;
    const float mx = get_difference(sphere.centre.x, start.x);
    const float my = get_difference(sphere.centre.y, start.y);
    const float mz = get_difference(sphere.centre.z, start.z);
    // the line's distance from the centre, squared, times its length squared: |m x d|^2
    const double cx = my * dz - mz * dy;
    const double cy = mz * dx - dz * mx;
    const double cz = mx * dy - my * dx;
    auto radius2 = static_cast<float>(static_cast<double>(sphere.radius) * sphere.radius);
    auto length2 = static_cast<float>((dz * dz + dx * dx) + dy * dy);
    const double crossed2 = (cz * cz + cy * cy) + cx * cx;
    if (static_cast<double>(length2) * radius2 < crossed2)
        return false;
    const double b = (mz * dz + my * dy) + mx * dx;
    const double c =
        ((static_cast<double>(mz) * mz + static_cast<double>(my) * my) + static_cast<double>(mx) * mx) - radius2;
    const double inverse = 1.0 / std::sqrt(std::fabs(b * b - c * length2));
    const double along = (inverse * c) / (1.0 - inverse * b);
    *t = static_cast<float>(along);
    return !(along > limit);
}

// 0x0040B750 (the first sign test uses the denominator before it's rounded to a float, the rest after)
bool segments_cross_2d(Ray2 a, Ray2 b, float* s)
{
    const auto bx = b.x;
    const auto by = b.y;
    const auto bdx = b.dx;
    const auto bdy = b.dy;
    const auto ax = a.x;
    const auto ay = a.y;
    const auto adx = a.dx;
    const auto ady = a.dy;
    const double dx = static_cast<double>(ax) - bx;
    const double dy = static_cast<double>(ay) - by;
    auto numerator = static_cast<float>(bdx * dy - dx * bdy);
    auto u_numerator = static_cast<float>(dy * adx - dx * ady);
    const double denominator_exact = static_cast<double>(adx) * bdy - static_cast<double>(ady) * bdx;
    auto denominator = static_cast<float>(denominator_exact);
    // each numerator between 0 and the denominator, whichever its sign (NaN passes)
    auto within = [](float numerator, float den, bool positive) {
        if (positive)
            return !(numerator < 0.0f) && !(numerator > den);
        return !(numerator > 0.0f) && !(numerator < den);
    };
    if (!within(numerator, denominator, denominator_exact > 0.0))
        return false;
    if (!within(u_numerator, denominator, denominator > 0.0f))
        return false;
    *s = std::fabs(denominator) < kParallel ? 0.0f : numerator / denominator;
    return true;
}

// 0x0040B860 (a = |e0|^2, b = e0.e1, c = |e1|^2, d = e0.D, e = e1.D, f = |D|^2 with e0 = v1 - v0, e1 = v2 - v0,
// D = v0 - point; rounded where the original's x87 code stores, a few terms kept unrounded as it keeps them)
double point_triangle_distance_squared(const maths::Vec4& point, const maths::Vec4& v0, const maths::Vec4& v1,
                                       const maths::Vec4& v2, TriangleNearest nearest)
{
    const auto out = nearest.out;
    const auto t_out = nearest.t_out_;
    const double dx = static_cast<double>(v0.x) - point.x;
    const double dy = static_cast<double>(v0.y) - point.y;
    const double dz = static_cast<double>(v0.z) - point.z;
    const double e0x_exact = static_cast<double>(v1.x) - v0.x;
    const double e0y_exact = static_cast<double>(v1.y) - v0.y;
    const double e0z_exact = static_cast<double>(v1.z) - v0.z;
    auto e0x = static_cast<float>(e0x_exact), e0y = static_cast<float>(e0y_exact), e0z = static_cast<float>(e0z_exact);
    const double e1x_exact = static_cast<double>(v2.x) - v0.x;
    auto e1x = static_cast<float>(e1x_exact);
    const float e1y = get_difference(v2.y, v0.y);
    const float e1z = get_difference(v2.z, v0.z);
    auto a = static_cast<float>((e0z_exact * e0z + e0y_exact * e0y_exact) + static_cast<double>(e0x) * e0x);
    auto b = static_cast<float>((static_cast<double>(e0z) * e1z + static_cast<double>(e0y) * e1y) + e0x * e1x_exact);
    auto c = static_cast<float>((static_cast<double>(e1z) * e1z + static_cast<double>(e1y) * e1y) +
                                static_cast<double>(e1x) * e1x);
    auto d = static_cast<float>((e0z * dz + e0y_exact * dy) + e0x * dx);
    auto e = static_cast<float>((e1z * dz + e1y * dy) + e1x * dx);
    double f = (dx * dx + dz * dz) + dy * dy;
    auto det = static_cast<float>(std::fabs(static_cast<double>(c) * a - static_cast<double>(b) * b));
    auto s = static_cast<float>(static_cast<double>(e) * b - static_cast<double>(d) * c);
    const double t = static_cast<double>(d) * b - static_cast<double>(e) * a;

    float so = 0.0f, to = 0.0f;
    double result = f;
    // the endings the regions share
    auto at_v1 = [&]() { // s = 1, t = 0
        so = 1.0f;
        to = 0.0f;
        result = (kCrossTermFactor * d + f) + a;
    };
    auto at_v2 = [&]() { // s = 0, t = 1
        so = 0.0f;
        to = 1.0f;
        result = (kCrossTermFactor * e + f) + c;
    };
    auto along_edge0 = [&]() { // t = 0, s = -d / a
        const double q = -(static_cast<double>(d) / a);
        so = static_cast<float>(q);
        result = f + q * d;
    };
    auto along_edge1 = [&]() { // s = 0, t = -e / c
        const double q = -(static_cast<double>(e) / c);
        to = static_cast<float>(q);
        result = f + q * e;
    };
    // the general quadratic at (so, to) - its first t term from `t_term`, which is to or the unrounded 1 - so
    auto general = [&](double t_term) {
        const double first = ((t_term * c + static_cast<double>(so) * b) + 2.0 * e) * to;
        const double second = ((static_cast<double>(to) * b + static_cast<double>(so) * a) + 2.0 * d) * so;
        result = f + (first + second);
    };
    auto denominator = [&]() { return (static_cast<double>(a) - kCrossTermFactor * b) + c; };
    // "less" as most of the original's tests here read the flags: NaN counts as less
    auto below = [](double x, double y) { return !(x >= y); };

    if (s + t <= det) {
        if (s < 0.0f) {
            if (t < 0.0) { // region 4
                if (d < 0.0f) {
                    to = 0.0f;
                    if (below(-d, a))
                        along_edge0();
                    else
                        at_v1();
                } else {
                    so = 0.0f;
                    if (!below(e, 0.0))
                        to = 0.0f;
                    else if (below(-e, c))
                        along_edge1();
                    else
                        at_v2();
                }
            } else { // region 3 (e not negated: the original's slip)
                so = 0.0f;
                if (!below(e, 0.0))
                    to = 0.0f;
                else if (below(e, c))
                    along_edge1();
                else
                    at_v2();
            }
        } else if (t < 0.0) { // region 5
            to = 0.0f;
            if (!below(d, 0.0))
                so = 0.0f;
            else if (below(-d, a))
                along_edge0();
            else
                at_v1();
        } else { // region 0: inside
            const double inverse = 1.0 / det;
            so = static_cast<float>(s * inverse);
            to = static_cast<float>(t * inverse);
            general(to);
        }
    } else if (s < 0.0f) { // region 2
        auto near0 = static_cast<float>(static_cast<double>(d) + b);
        const double close_a = static_cast<double>(e) + c;
        if (close_a > near0) {
            const double numerator = close_a - near0;
            const double den = denominator();
            if (below(numerator, den)) {
                so = static_cast<float>(numerator / den);
                const double rest = 1.0 - so;
                to = static_cast<float>(rest);
                general(rest);
            } else {
                at_v1();
            }
        } else {
            so = 0.0f;
            if (close_a <= 0.0)
                at_v2();
            else if (below(e, 0.0))
                along_edge1();
            else
                to = 0.0f;
        }
    } else if (t < 0.0) { // region 6
        auto near0 = static_cast<float>(static_cast<double>(e) + b);
        const double close_a = static_cast<double>(d) + a;
        if (close_a > near0) {
            const double numerator = close_a - near0;
            const double den = denominator();
            if (below(numerator, den)) {
                to = static_cast<float>(numerator / den);
                so = static_cast<float>(1.0 - to);
                general(to);
            } else {
                at_v2();
            }
        } else {
            to = 0.0f;
            if (close_a <= 0.0)
                at_v1();
            else if (below(d, 0.0))
                along_edge0();
            else
                so = 0.0f;
        }
    } else { // region 1
        const double numerator = ((static_cast<double>(e) + c) - b) - d;
        auto numerator_rounded = static_cast<float>(numerator);
        if (numerator <= 0.0) {
            at_v2();
        } else if (below(numerator_rounded, denominator())) {
            so = static_cast<float>(numerator_rounded / denominator());
            const double rest = 1.0 - so;
            to = static_cast<float>(rest);
            general(rest);
        } else {
            at_v1();
        }
    }
    if (out)
        *out = so;
    if (t_out)
        *t_out = to;
    return std::fabs(result);
}

// 0x0040BDC0 (with e1 = v1 - v0, e2 = v2 - v0, d = end - start, D = v0 - start: the 3x3 system for the segment's
// t and the triangle's s, t solved by Cramer's rule; rounded where the original's x87 code stores)
double segment_triangle_distance_squared(const maths::Seg& segment, const maths::Vec4& /*normal*/,
                                         const maths::Vec4& v0, const maths::Vec4& v1, const maths::Vec4& v2, SegmentNearest nearest)
{
    const auto out = nearest.out;
    const auto t_out = nearest.t_out_;
    const auto along_out = nearest.along_out_;
    const maths::Vec4& start = segment.start;
    const double rel_x = static_cast<double>(v0.x) - start.x;
    const double rel_y = static_cast<double>(v0.y) - start.y;
    const double rel_z = static_cast<double>(v0.z) - start.z;
    const float dx = get_difference(segment.end.x, start.x), dy = get_difference(segment.end.y, start.y),
                dz = get_difference(segment.end.z, start.z);
    const float e1x = get_difference(v1.x, v0.x), e1y = get_difference(v1.y, v0.y), e1z = get_difference(v1.z, v0.z);
    const float e2x = get_difference(v2.x, v0.x), e2y = get_difference(v2.y, v0.y), e2z = get_difference(v2.z, v0.z);
    auto product = [](float a, float b) { return static_cast<double>(a) * b; };

    const auto dd = static_cast<float>((product(dz, dz) + product(dy, dy)) + product(dx, dx));
    const auto de1 = static_cast<float>((product(e1z, -dz) + product(e1y, -dy)) + product(-dx, e1x)); // -(d.e1)
    const auto de2 = static_cast<float>((product(e2y, -dy) + product(-dx, e2x)) + product(-dz, e2z)); // -(d.e2)
    const auto e1e1 = static_cast<float>((product(e1z, e1z) + product(e1y, e1y)) + product(e1x, e1x));
    const auto e1e2 = static_cast<float>((product(e1z, e2z) + product(e2y, e1y)) + product(e2x, e1x));
    const auto e2e2 = static_cast<float>((product(e2y, e2y) + product(e2x, e2x)) + product(e2z, e2z));
    const auto d_d = static_cast<float>((dz * -rel_z + dy * -rel_y) + -rel_x * dx); // -(d.D)
    const auto e_1d = static_cast<float>((e1z * rel_z + e1y * rel_y) + e1x * rel_x);
    const auto e_2d = static_cast<float>((rel_z * e2z + e2y * rel_y) + e2x * rel_x);

    // The triangle's normal (unnormalised), to tell a segment (nearly) parallel to it.
    const double nx = product(e2y, e1z) - product(e1y, e2z);
    const double ny = product(e1x, e2z) - product(e1z, e2x);
    const double nz = product(e1y, e2x) - product(e2y, e1x);
    const double dn = (dz * nz + dy * ny) + nx * dx;
    const bool parallel = !((((nx * nx + nz * nz) + ny * ny) * dd) * kParallelSegment <= dn * dn);

    float along = 0.0f, s = 0.0f, t = 0.0f;
    auto finish = [&](double result) {
        if (along_out)
            *along_out = along;
        if (out)
            *out = s;
        if (t_out)
            *t_out = t;
        return result;
    };
    // The segment against one triangle edge, and against the triangle from one of its ends.
    auto edge = [&](const maths::Vec4& a, const maths::Vec4& b, float* segment_t, float* edge_t) {
        return segment.distance_sqr_to_line(maths::Seg(a, b), segment_t, edge_t);
    };
    auto from_point = [&](const maths::Vec4& point, float* point_s, float* point_t) {
        return point_triangle_distance_squared(point, v0, v1, v2, {.out = point_s, .t_out_ = point_t});
    };

    float best = 0.0f;
    // A later edge, compared unrounded with the best so far: v0 v1 (s along it), v0 v2 (t) or v1 v2 (w: s = 1 - w,
    // t = w).
    auto try_edge01 = [&] {
        float segment_t = 0.0f, edge_t = 0.0f;
        const double distance = edge(v0, v1, &segment_t, &edge_t);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segment_t;
            s = edge_t;
            t = 0.0f;
        }
    };
    auto try_edge02 = [&] {
        float segment_t = 0.0f, edge_t = 0.0f;
        const double distance = edge(v0, v2, &segment_t, &edge_t);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segment_t;
            s = 0.0f;
            t = edge_t;
        }
    };
    auto try_edge12 = [&] {
        float segment_t = 0.0f, edge_t = 0.0f;
        const double distance = edge(v1, v2, &segment_t, &edge_t);
        auto rest = static_cast<float>(1.0 - edge_t);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segment_t;
            s = rest;
            t = edge_t;
        }
    };
    // Last, one of the segment's ends (rounded before it's compared), and the result.
    auto finish_with_end = [&](bool at_end) {
        const maths::Vec4 point = at_end ? segment.end : segment.start;
        float point_s = 0.0f, point_t = 0.0f;
        auto distance = static_cast<float>(from_point(point, &point_s, &point_t));
        if (distance < best) {
            along = at_end ? 1.0f : 0.0f;
            s = point_s;
            t = point_t;
            return finish(distance);
        }
        return finish(best);
    };

    if (parallel) {
        best = static_cast<float>(edge(v0, v1, &along, &s));
        t = 0.0f;
        try_edge02();
        try_edge12();
        float point_s = 0.0f, point_t = 0.0f;
        const double distance = from_point(start, &point_s, &point_t);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = 0.0f;
            s = point_s;
            t = point_t;
        }
        return finish_with_end(true);
    }

    // Where the segment's line crosses the triangle's plane.
    const double cofactor11 = static_cast<double>(e2e2) * e1e1 - static_cast<double>(e1e2) * e1e2;
    const auto cofactor12 = static_cast<float>(product(e1e2, de2) - product(e2e2, de1));
    const auto cofactor13 = static_cast<float>(product(e1e2, de1) - product(e1e1, de2));
    const auto cofactor23 = static_cast<float>(product(de2, de1) - product(e1e2, dd));
    const double inverse = 1.0 / ((product(cofactor13, de2) + product(cofactor12, de1)) + cofactor11 * dd);
    const auto rhs_d = static_cast<float>(-(d_d * inverse));
    const auto rhs1 = static_cast<float>(-(e_1d * inverse));
    const double rhs2_exact = -(inverse * e_2d);
    const auto rhs2 = static_cast<float>(rhs2_exact);
    along = static_cast<float>((rhs2_exact * cofactor13 + product(rhs1, cofactor12)) + rhs_d * cofactor11);
    const double exact =
        ((product(e2e2, dd) - product(de2, de2)) * rhs1 + product(rhs2, cofactor23)) + product(rhs_d, cofactor12);
    s = static_cast<float>(exact);
    t = static_cast<float>(((product(e1e1, dd) - product(de1, de1)) * rhs2 + product(rhs1, cofactor23)) +
                           product(rhs_d, cofactor13));

    const bool beyond_edge12 = !(t + exact <= 1.0);
    const bool below_edge02 = exact < 0.0; // (s < 0: on v0 v2's far side)
    const bool below_edge01 = t < 0.0f;
    if (along < 0.0f || !(along <= 1.0f)) {
        // The crossing is off the segment: the nearer of the nearest edges and the segment's end.
        const bool at_end = !(along < 0.0f);
        if (!beyond_edge12) {
            if (below_edge02) {
                best = static_cast<float>(edge(v0, v2, &along, &t));
                s = 0.0f;
                if (below_edge01)
                    try_edge01();
            } else if (below_edge01) {
                best = static_cast<float>(edge(v0, v1, &along, &s));
                t = 0.0f;
            } else {
                const double distance = from_point(at_end ? segment.end : start, &s, &t);
                along = at_end ? 1.0f : 0.0f;
                return finish(distance);
            }
        } else if (below_edge02) {
            best = static_cast<float>(edge(v0, v2, &along, &t));
            s = 0.0f;
            try_edge12();
        } else if (below_edge01) {
            best = static_cast<float>(edge(v0, v1, &along, &s));
            t = 0.0f;
            try_edge12();
        } else {
            best = static_cast<float>(edge(v1, v2, &along, &t));
            s = static_cast<float>(1.0 - t);
        }
        return finish_with_end(at_end);
    }

    // The crossing is on the segment: the nearest edges (rounded before they're compared).
    if (!beyond_edge12) {
        if (below_edge02) {
            if (!below_edge01) {
                const double distance = edge(v0, v2, &along, &t);
                s = 0.0f;
                return finish(distance);
            }
            best = static_cast<float>(edge(v0, v2, &along, &t));
            s = 0.0f;
            float segment_t = 0.0f, edge_t = 0.0f;
            auto distance = static_cast<float>(edge(v0, v1, &segment_t, &edge_t));
            if (distance < best) {
                along = segment_t;
                s = edge_t;
                t = 0.0f;
                return finish(distance);
            }
            return finish(best);
        }
        if (below_edge01) {
            const double distance = edge(v0, v1, &along, &s);
            t = 0.0f;
            return finish(distance);
        }
        debug::printf("uh oh...\n");
        return finish(0.0f);
    }
    if (!below_edge02 && !below_edge01) {
        const double distance = edge(v1, v2, &along, &t);
        s = static_cast<float>(1.0 - t);
        return finish(distance);
    }
    if (below_edge02) {
        best = static_cast<float>(edge(v0, v2, &along, &t));
        s = 0.0f;
    } else {
        best = static_cast<float>(edge(v0, v1, &along, &s));
        t = 0.0f;
    }
    float segment_t = 0.0f, edge_t = 0.0f;
    auto distance = static_cast<float>(edge(v1, v2, &segment_t, &edge_t));
    auto rest = static_cast<float>(1.0 - edge_t);
    if (distance < best) {
        along = segment_t;
        s = rest;
        t = edge_t;
        return finish(distance);
    }
    return finish(best);
}

// 0x0040D370 (P's side tests use its line values unrounded, Q's rounded to floats; each pair of sides' direction
// test the first time unrounded, the second time rounded - as the original's x87 code keeps them)
bool rect_hits_triangle(const CollRect& rect, const maths::Vec4& v0, const maths::Vec4& v1, const maths::Vec4& v2,
                        std::span<maths::Vec4, 2> points, std::span<int, 2> kinds)
{
    const maths::Vec4& o = rect.origin;
    const maths::Vec4& a = rect.side_a_;
    const maths::Vec4& b = rect.side_b_;
    // the rect's plane: n = a x b, and the triangle's corners' distances from it
    auto nx = static_cast<float>(static_cast<double>(b.z) * a.y - static_cast<double>(b.y) * a.z);
    auto ny = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(b.z) * a.x);
    auto nz = static_cast<float>(static_cast<double>(b.y) * a.x - static_cast<double>(a.y) * b.x);
    const double d = -((static_cast<double>(ny) * o.y + static_cast<double>(nz) * o.z) + static_cast<double>(nx) * o.x);
    auto d0 = static_cast<float>(
        ((static_cast<double>(nz) * v0.z + static_cast<double>(ny) * v0.y) + static_cast<double>(nx) * v0.x) + d);
    auto d1 = static_cast<float>(
        ((static_cast<double>(nx) * v1.x + static_cast<double>(ny) * v1.y) + static_cast<double>(nz) * v1.z) + d);
    auto d2 = static_cast<float>(
        ((static_cast<double>(nx) * v2.x + static_cast<double>(nz) * v2.z) + static_cast<double>(ny) * v2.y) + d);
    if (d0 > 0.0f && d1 > 0.0f && d2 > 0.0f)
        return false;
    if (d0 < 0.0f && d1 < 0.0f && d2 < 0.0f)
        return false;
    if (std::fabs(d0) < kOnPlane)
        d0 = 0.0f;
    if (std::fabs(d1) < kOnPlane)
        d1 = 0.0f;
    if (std::fabs(d2) < kOnPlane)
        d2 = 0.0f;

    // the segment P Q where the plane cuts the triangle
    maths::Vec4 p{}, q{};
    int p_kind = 0, q_kind = 0;
    if ((d0 <= 0.0f && d1 >= 0.0f) || (d0 >= 0.0f && d1 <= 0.0f)) {
        p_kind = 1;
        p = edge_crossing(v0, v1, d0, d1);
        if ((d1 <= 0.0f && d2 >= 0.0f) || (d1 >= 0.0f && d2 <= 0.0f)) {
            q_kind = 2;
            q = edge_crossing_scaled(v1, v2, d1, d2);
        } else {
            q_kind = 3;
            q = edge_crossing_scaled(v2, v0, d2, d0);
        }
    } else {
        p_kind = 2;
        p = edge_crossing(v1, v2, d1, d2);
        q_kind = 3;
        q = edge_crossing(v0, v2, d0, d2);
    }

    // in the plane the normal is largest across
    const float ax = std::fabs(nx);
    const float ay = std::fabs(ny);
    const float az = std::fabs(nz);
    int i = 1, j = 2;
    if (ax < az && ay < az) {
        i = 0;
        j = 1;
    } else if (ax < ay && az < ay) {
        i = 0;
        j = 2;
    }
    const float o1 = get_component(o, i);
    const float o2 = get_component(o, j);
    const float a1 = get_component(a, i);
    const float a2 = get_component(a, j);
    const float b1 = get_component(b, i);
    const float b2 = get_component(b, j);
    const float p1 = get_component(p, i);
    const float p2 = get_component(p, j);
    const float q1 = get_component(q, i);
    const float q2 = get_component(q, j);

    // each end against the sides' lines: side 0 (o along a), side 1 (o + b along a) ...
    const float neg_a1 = -a1;
    auto c0 = static_cast<float>(static_cast<double>(a1) * o2 - static_cast<double>(a2) * o1);
    const double p_along_a = static_cast<double>(neg_a1) * p2 + static_cast<double>(a2) * p1;
    const double q_along_a = static_cast<double>(neg_a1) * q2 + static_cast<double>(q1) * a2;
    auto q_along_af = static_cast<float>(q_along_a);
    const double b_side = static_cast<double>(neg_a1) * b2 + static_cast<double>(b1) * a2;
    auto b_sidef = static_cast<float>(b_side);
    const bool p_in0 = get_inside(b_side > 0.0, c0 + p_along_a);
    const bool q_in0 = get_inside(b_side > 0.0, static_cast<float>(q_along_a + c0));
    if (!p_in0 && !q_in0)
        return false;
    const float far2 = b2 + o2;
    const float far1 = b1 + o1;
    auto c1 = static_cast<float>(static_cast<double>(far2) * a1 - static_cast<double>(far1) * a2);
    const bool p_in1 = b_sidef < 0.0f ? p_along_a + c1 >= 0.0 : p_along_a + c1 <= 0.0;
    const float q_end1 = q_along_af + c1;
    const bool quat_src_a = b_sidef < 0.0f ? q_end1 >= 0.0f : q_end1 <= 0.0f;
    if (!p_in1 && !quat_src_a)
        return false;
    // ... side 2 (o along b), side 3 (o + a along b)
    const float neg_b1 = -b1;
    auto c2 = static_cast<float>(static_cast<double>(b1) * o2 - static_cast<double>(b2) * o1);
    const double p_along_b = static_cast<double>(neg_b1) * p2 + static_cast<double>(p1) * b2;
    const double q_along_b = static_cast<double>(neg_b1) * q2 + static_cast<double>(q1) * b2;
    auto q_along_bf = static_cast<float>(q_along_b);
    const double a_side = static_cast<double>(neg_b1) * a2 + static_cast<double>(b2) * a1;
    auto a_sidef = static_cast<float>(a_side);
    const bool p_in2 = get_inside(a_side > 0.0, c2 + p_along_b);
    const bool quat_src_b = get_inside(a_side > 0.0, static_cast<float>(q_along_b + c2));
    if (!p_in2 && !quat_src_b)
        return false;
    const float far4 = a2 + o2;
    const float far3 = a1 + o1;
    auto c3 = static_cast<float>(static_cast<double>(far4) * b1 - static_cast<double>(far3) * b2);
    const bool p_in3 = a_sidef < 0.0f ? p_along_b + c3 >= 0.0 : p_along_b + c3 <= 0.0;
    const float q_end3 = q_along_bf + c3;
    const bool q_in3 = a_sidef < 0.0f ? q_end3 >= 0.0f : q_end3 <= 0.0f;
    if (!p_in3 && !q_in3)
        return false;

    // the ends inside the rect, then the sides crossing P Q
    int found = 0;
    const bool p_inside = p_in0 && p_in1 && p_in2 && p_in3;
    const bool q_inside = q_in0 && quat_src_a && quat_src_b && q_in3;
    if (p_inside) {
        core::at(points, 0) = p;
        core::at(kinds, 0) = p_kind;
        if (q_inside) {
            core::at(points, 1) = q;
            core::at(kinds, 1) = q_kind;
            return true;
        }
        found = 1;
    } else if (q_inside) {
        core::at(points, 0) = q;
        core::at(kinds, 0) = q_kind;
        found = 1;
    }
    const float dq1 = q1 - p1;
    const float dq2 = q2 - p2;
    float s = 0.0f;
    auto take = [&](const maths::Vec4& point) { // (true: that was the second)
        core::at(points, found) = point;
        core::at(kinds, found) = 0;
        const bool second = found == 1;
        found = 1;
        return second;
    };
    if (segments_cross_2d({.x = o1, .y = o2, .dx = a1, .dy = a2}, {.x = p1, .y = p2, .dx = dq1, .dy = dq2}, &s)) {
        maths::Vec4 point{.x = s * a.x, .y = s * a.y, .z = s * a.z, .w = s * a.w};
        point = {.x = o.x + point.x, .y = point.y + o.y, .z = point.z + o.z, .w = o.w + point.w};
        if (take(point))
            return true;
    }
    if (segments_cross_2d({.x = far1, .y = far2, .dx = a1, .dy = a2}, {.x = p1, .y = p2, .dx = dq1, .dy = dq2}, &s)) {
        maths::Vec4 point{.x = s * a.x, .y = s * a.y, .z = s * a.z, .w = s * a.w};
        point = {.x = o.x + point.x, .y = point.y + o.y, .z = point.z + o.z, .w = o.w + point.w};
        point = {.x = b.x + point.x, .y = b.y + point.y, .z = b.z + point.z, .w = b.w + point.w};
        if (take(point))
            return true;
    }
    if (segments_cross_2d({.x = o1, .y = o2, .dx = b1, .dy = b2}, {.x = p1, .y = p2, .dx = dq1, .dy = dq2}, &s)) {
        maths::Vec4 point = b;
        point *= s;
        point += o;
        if (take(point))
            return true;
    }
    // (the last side only counts as the second point)
    if (!segments_cross_2d({.x = far3, .y = far4, .dx = b1, .dy = b2}, {.x = p1, .y = p2, .dx = dq1, .dy = dq2}, &s) || found != 1)
        return false;
    maths::Vec4 point = b;
    point *= s;
    point += o;
    point += a;
    core::at(points, 1) = point;
    core::at(kinds, 1) = 0;
    return true;
}

} // namespace render
