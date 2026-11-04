#include "base/maths/shape_tests.h"
#include "base/maths/vec4.h"
#include "base/maths/trig.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace maths {

namespace {

constexpr float kHuge = 10000000.0F;
constexpr float kMidpoint = 0.5F;         // halfway between a box's corners
constexpr float kParallelRayStep = 1e-6F; // stands in for a ray direction component of exactly zero
constexpr float kSphereVolumeFactor = 4.0F / 3.0F;
constexpr float kParallelDeterminant = 1e-6F; // segments with a smaller determinant are parallel

} // namespace

// 0x004033E0
BBox::BBox() : min{.x = kHuge, .y = kHuge, .z = kHuge, .w = 1.0F}, max{.x = -kHuge, .y = -kHuge, .z = -kHuge, .w = 1.0F}
{}

// 0x00403410
BBox::BBox(const Vec4& point) noexcept : min(point), max(point) {}

// 0x00403450
void BBox::append_spot(const Vec4& point)
{
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
}

// 0x0040B170 (x, then z, then y; each failing only when strictly beyond)
bool BBox::intersects(const BBox& other) const
{
    if (min.x > other.max.x || other.min.x > max.x) {
        return false;
    }
    if (min.z > other.max.z || other.min.z > max.z) {
        return false;
    }
    if (min.y > other.max.y || other.min.y > max.y) {
        return false;
    }
    return true;
}

// 0x0040B1D0
bool BBox::get_holds(const BBox& other) const
{
    if (!(other.min.x >= min.x) || !(other.max.x <= max.x)) {
        return false;
    }
    if (!(other.min.y >= min.y) || !(other.max.y <= max.y)) {
        return false;
    }
    if (!(other.min.z >= min.z) || !(other.max.z <= max.z)) {
        return false;
    }
    return true;
}

// 0x00404060 (Ghidra's signature database labels this MFC's CRect constructor; it is the same simple code)
Rect2::Rect2(const Edges& edges) : edges_(edges) {}

// 0x00403BB0
Vec4& BBox::get_center(Vec4& out) const
{
    out = {.x = ((max.x - min.x) * kMidpoint) + min.x,
           .y = ((max.y - min.y) * kMidpoint) + min.y,
           .z = ((max.z - min.z) * kMidpoint) + min.z,
           .w = 1.0F};
    return out;
}

// 0x004025A0
int BBox::contains_point(const Vec4& point) const
{
    // Written as "not outside" so NaN coordinates count as outside, as in the original.
    if (!(point.x >= min.x) || !(point.y >= min.y) || !(point.z >= min.z)) {
        return 0;
    }
    if (!(point.x <= max.x) || !(point.y <= max.y) || !(point.z <= max.z)) {
        return 0;
    }
    return 1;
}

// 0x004034D0
int BBox::count_axes_containing(const Vec4& point, uint32_t& axes) const
{
    axes = 0;
    int count = 0;
    if (point.x >= min.x && point.x <= max.x) {
        ++count;
        axes |= 1;
    }
    if (point.y >= min.y && point.y <= max.y) {
        ++count;
        axes |= 2;
    }
    if (point.z >= min.z && point.z <= max.z) {
        ++count;
        axes |= 4;
    }
    return count;
}

// 0x00403550
bool BBox::segment_crossing(const Seg& line, CrossingOut out) const
{
    Vec4& hit = *out.hit;
    Vec4& normal = *out.normal;
    const Vec4& start_point = line.start;
    const Vec4& end_point = line.end;
    if ((contains_point(start_point) != 0) && (contains_point(end_point) != 0)) {
        return false;
    }

    // Both ends beyond the same face: no hit.
    if (max.y < start_point.y && end_point.y > max.y) {
        return false;
    }
    if (start_point.y < min.y && end_point.y < min.y) {
        return false;
    }
    if (max.x < start_point.x && end_point.x > max.x) {
        return false;
    }
    if (start_point.x < min.x && end_point.x < min.x) {
        return false;
    }
    if (max.z < start_point.z && end_point.z > max.z) {
        return false;
    }
    if (start_point.z < min.z && end_point.z < min.z) {
        return false;
    }

    float dx = end_point.x - start_point.x;
    float dy = end_point.y - start_point.y;
    float dz = end_point.z - start_point.z;
    if (dx == 0.0F) {
        dx = kParallelRayStep;
    }
    if (dy == 0.0F) {
        dy = kParallelRayStep;
    }
    if (dz == 0.0F) {
        dz = kParallelRayStep;
    }

    // Crossing a face at fraction k along the segment; the hit must be strictly inside the face. (k and the
    // crossing in double, rounded to floats where the original's x87 code stores them.)
    auto inside_y_z = [&](float y, float z) { return y < max.y && min.y < y && z < max.z && min.z < z; };
    auto inside_x_z = [&](float x, float z) { return x < max.x && min.x < x && z < max.z && min.z < z; };
    auto inside_x_y = [&](float x, float y) { return x < max.x && min.x < x && y < max.y && min.y < y; };

    if (max.x < start_point.x && end_point.x < max.x && dx < 0.0F) {
        const double ratio = (1.0 / dx) * (static_cast<double>(max.x) - start_point.x);
        auto y = static_cast<float>((ratio * dy) + start_point.y);
        auto z = static_cast<float>((ratio * dz) + start_point.z);
        if (inside_y_z(y, z)) {
            hit = {.x = max.x, .y = y, .z = z, .w = 1.0F};
            normal = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
            return true;
        }
    }
    if (start_point.x < min.x && min.x < end_point.x && dx > 0.0F) {
        const double ratio = (1.0 / dx) * (static_cast<double>(min.x) - start_point.x);
        auto y = static_cast<float>((ratio * dy) + start_point.y);
        auto z = static_cast<float>((ratio * dz) + start_point.z);
        if (inside_y_z(y, z)) {
            hit = {.x = min.x, .y = y, .z = z, .w = 1.0F};
            normal = {.x = -1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
            return true;
        }
    }
    if (max.y < start_point.y && end_point.y < max.y && dy < 0.0F) {
        const double ratio = (1.0 / dy) * (static_cast<double>(max.y) - start_point.y);
        auto x = static_cast<float>((ratio * dx) + start_point.x);
        auto z = static_cast<float>((ratio * dz) + start_point.z);
        if (inside_x_z(x, z)) {
            hit = {.x = x, .y = max.y, .z = z, .w = 1.0F};
            normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
            return true;
        }
    }
    if (start_point.y < min.y && min.y < end_point.y && dy > 0.0F) {
        const double ratio = (1.0 / dy) * (static_cast<double>(min.y) - start_point.y);
        auto x = static_cast<float>((ratio * dx) + start_point.x);
        auto z = static_cast<float>((ratio * dz) + start_point.z);
        if (inside_x_z(x, z)) {
            hit = {.x = x, .y = min.y, .z = z, .w = 1.0F};
            normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F, .w = 0.0F};
            return true;
        }
    }
    if (max.z < start_point.z && end_point.z < max.z && dz < 0.0F) {
        const double ratio = (1.0 / dz) * (static_cast<double>(max.z) - start_point.z);
        auto x = static_cast<float>((ratio * dx) + start_point.x);
        auto y = static_cast<float>((ratio * dy) + start_point.y);
        if (inside_x_y(x, y)) {
            hit = {.x = x, .y = y, .z = max.z, .w = 1.0F};
            normal = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
            return true;
        }
    }
    if (start_point.z < min.z && end_point.z > min.z && dz > 0.0F) {
        const double ratio = (1.0 / dz) * (static_cast<double>(min.z) - start_point.z);
        auto x = static_cast<float>((ratio * dx) + start_point.x);
        auto y = static_cast<float>((ratio * dy) + start_point.y);
        if (inside_x_y(x, y)) {
            hit = {.x = x, .y = y, .z = min.z, .w = 1.0F};
            normal = {.x = 0.0F, .y = 0.0F, .z = -1.0F, .w = 0.0F};
            return true;
        }
    }
    return false;
}

// 0x00403C00
void BBox::get_closest_surface_point(const Vec4& point, Vec4& out) const
{
    uint32_t axes = 0;
    if (count_axes_containing(point, axes) == 3) {
        // Inside: move along whichever axis reaches a face soonest.
        float best_value = 0.0F;
        float best_distance = 0.0F; // always set on the first pass (axis 0)
        int winning_axis = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const float coordinate = point[axis];
            const float lo = min[axis];
            const float hi = max[axis];
            float value = hi;
            float distance = hi - coordinate;
            if (!(distance <= coordinate - lo)) {
                value = lo;
                distance = coordinate - lo;
            }
            if (axis == 0 || distance < best_distance) {
                winning_axis = axis;
                best_value = value;
                best_distance = distance;
            }
        }
        out = point;
        out[winning_axis] = best_value;
        return;
    }

    // Outside: keep the coordinates that are within range and clamp the rest.
    out.w = 1.0F;
    for (int axis = 0; axis < 3; ++axis) {
        const float coordinate = point[axis];
        const float lo = min[axis];
        const float hi = max[axis];
        float& clamped = out[axis];
        if ((axes & (1U << axis)) != 0U) {
            clamped = coordinate;
        } else {
            clamped = (coordinate <= lo) ? lo : hi; // NaN goes to max, as in the original
        }
    }
}

// 0x00403220
Capsule::Capsule() : radius(0.0F) {}

// 0x00403230
Capsule::Capsule(const Vec4& start, const Vec4& offset, float sphere_radius)
    : line(start, start + offset), radius(sphere_radius)
{}

// 0x004032A0
Capsule::Capsule(const Seg& segment, float sphere_radius) : line(segment), radius(sphere_radius) {}

// 0x00403390
float Capsule::get_loudness() const
{
    const float length = (line.end - line.start).len_of();
    return (radius * radius * radius * (kSphereVolumeFactor * kPi)) + (length * radius * radius * kPi);
}

// 0x00403330
bool Capsule::contains_point(const Vec4& point, Vec4* offset) const
{
    Vec4 local_offset{};
    const float squared_gap = line.distance_sqr_to_point(point, &local_offset);
    if (!(radius * radius >= squared_gap)) {
        return false;
    }
    if (offset) {
        *offset = local_offset;
    }
    return true;
}

// 0x00402D60
float Seg::distance_sqr_to_point(const Vec4& point, Vec4* offset) const
{
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float dz = end.z - start.z;
    const float dw = end.w - start.w;
    const float vx = point.x - start.x;
    const float vy = point.y - start.y;
    const float vz = point.z - start.z;

    // Project onto the segment: before the start, past the end, or in between.
    const float along = (vx * dx) + (vy * dy) + (vz * dz);
    const float squared_len = (dx * dx) + (dy * dy) + (dz * dz);
    Vec4 result{};
    if (along <= 0.0F) {
        result = point - start;
    } else if (squared_len <= along) {
        result = point - end;
    } else {
        const float fraction = along / squared_len;
        result = {.x = point.x - ((dx * fraction) + start.x),
                  .y = point.y - ((dy * fraction) + start.y),
                  .z = point.z - ((dz * fraction) + start.z),
                  .w = point.w - ((dw * fraction) + start.w)};
    }
    if (offset) {
        *offset = result;
    }
    return (result.x * result.x) + (result.y * result.y) + (result.z * result.z);
}

namespace {

// Seg::distance_sqr_to_line's quadratic a00 s^2 + 2 a01 s t + a11 t^2 + 2 b0 s + 2 b1 t + c (c: length_sqr, b1 from
// the offset dx dy dz and the other segment's direction), its determinant, and the minimum found: (s, t) and the
// squared distance there.
struct SegmentPairQuadratic {
    float dx{};
    float dy{};
    float dz{};
    float d1x{};
    float d1y{};
    float d1z{};
    float a00{};
    float a01{};
    float a11{};
    float b0{};
    float length_sqr{};
    float det{};
    float along_a = 0.0F;
    float along_b = 0.0F;
    float sqr = 0.0F;

    [[nodiscard]] float compute_b1() const { return (-dy * d1y) + (-dz * d1z) + (-dx * d1x); }

    // Minimise along one edge of the unit square.
    void edge_s0(float b1) // s = 0, t free
    {
        along_a = 0.0F;
        if (b1 >= 0.0F) {
            along_b = 0.0F;
            sqr = length_sqr;
        } else if (a11 <= -b1) {
            along_b = 1.0F;
            sqr = b1 + b1 + length_sqr + a11;
        } else {
            along_b = -b1 / a11;
            sqr = (-(b1 / a11) * b1) + length_sqr;
        }
    }
    void edge_t0() // t = 0, s free
    {
        along_b = 0.0F;
        if (b0 >= 0.0F) {
            along_a = 0.0F;
            sqr = length_sqr;
        } else if (a00 <= -b0) {
            along_a = 1.0F;
            sqr = b0 + b0 + length_sqr + a00;
        } else {
            along_a = -b0 / a00;
            sqr = (-(b0 / a00) * b0) + length_sqr;
        }
    }
    void edge_s1(float b1) // s = 1, t free
    {
        along_a = 1.0F;
        const float tmp = b1 + a01;
        if (tmp < 0.0F) {
            if (-tmp < a11) {
                along_b = -tmp / a11;
                sqr = b0 + b0 + (tmp * along_b) + length_sqr + a00;
            } else {
                along_b = 1.0F;
                sqr = tmp + b0 + tmp + b0 + length_sqr + a11 + a00;
            }
        } else {
            along_b = 0.0F;
            sqr = b0 + b0 + length_sqr + a00;
        }
    }
    // t = 1 with s free, when b0 + a01 < 0 (the caller checks).
    void edge_t1_inside(float tmp, float b1)
    {
        along_b = 1.0F;
        if (-tmp < a00) {
            along_a = -tmp / a00;
            sqr = b1 + b1 + (tmp * along_a) + length_sqr + a11;
        } else {
            along_a = 1.0F;
            sqr = tmp + b1 + tmp + b1 + length_sqr + a11 + a00;
        }
    }
    void corner_s0_t1(float b1)
    {
        along_a = 0.0F;
        along_b = 1.0F;
        sqr = b1 + b1 + length_sqr + a11;
    }

    // Not parallel: scaled by det, the unconstrained minimum is at (s, t).
    void minimise_crossing()
    {
        const float b1 = compute_b1();
        along_a = (a01 * b1) - (a11 * b0);
        along_b = (a01 * b0) - (a00 * b1);
        if (along_a < 0.0F) {
            const float tmp = b0 + a01;
            if (!(along_b < 0.0F) && !(along_b <= det) && tmp < 0.0F) {
                edge_t1_inside(tmp, b1);
            } else if (along_b < 0.0F && b0 < 0.0F) {
                edge_t0();
            } else {
                edge_s0(b1);
            }
        } else if (!(along_a <= det)) {
            const float tmp = b0 + a01;
            if (along_b < 0.0F && -b0 < a00) {
                edge_t0();
            } else if (!(along_b < 0.0F) && !(along_b <= det) && -tmp <= a00) {
                if (tmp < 0.0F) {
                    along_b = 1.0F;
                    along_a = -tmp / a00;
                    sqr = b1 + b1 + (tmp * along_a) + length_sqr + a11;
                } else {
                    corner_s0_t1(b1);
                }
            } else {
                edge_s1(b1);
            }
        } else if (along_b < 0.0F) {
            edge_t0();
        } else if (!(along_b <= det)) {
            const float tmp = b0 + a01;
            if (tmp < 0.0F) {
                edge_t1_inside(tmp, b1);
            } else {
                corner_s0_t1(b1);
            }
        } else {
            // Interior: the unconstrained minimum is inside the unit square.
            const float inv = 1.0F / det;
            along_a *= inv;
            along_b *= inv;
            sqr = ((b0 + b0 + (along_a * a00) + (along_b * a01)) * along_a) +
                  ((b1 + b1 + (along_a * a01) + (along_b * a11)) * along_b) + length_sqr;
        }
    }
    // Parallel, pointing the same way.
    void minimise_same_way()
    {
        if (a00 <= -b0) {
            along_b = 0.0F;
            along_a = 1.0F;
            sqr = b0 + b0 + length_sqr + a00;
        } else if (!(b0 <= 0.0F)) {
            const float b1 = compute_b1();
            along_a = 0.0F;
            if (-a01 <= b0) {
                along_b = 1.0F;
                sqr = b1 + b1 + length_sqr + a11;
            } else {
                along_b = -(b0 / a01);
                sqr = ((b1 + b1 + (along_b * a11)) * along_b) + length_sqr;
            }
        } else {
            along_a = -(b0 / a00);
            along_b = 0.0F;
            sqr = (along_a * b0) + length_sqr;
        }
    }
    // Parallel, pointing opposite ways.
    void minimise_opposite_ways()
    {
        if (b0 >= 0.0F) {
            along_a = 0.0F;
            along_b = 0.0F;
            sqr = length_sqr;
        } else if (!(-b0 <= a00)) {
            along_a = 1.0F;
            const float b1 = compute_b1();
            if (-(b0 + a00) < a01) {
                along_b = -((b0 + a00) / a01);
                const float tmp = b1 + a01;
                sqr = b0 + b0 + (((along_b * a11) + tmp + tmp) * along_b) + length_sqr + a00;
            } else {
                along_b = 1.0F;
                const float tmp = b1 + b0 + a01;
                sqr = tmp + tmp + length_sqr + a11 + a00;
            }
        } else {
            along_a = -(b0 / a00);
            along_b = 0.0F;
            sqr = (along_a * b0) + length_sqr;
        }
    }
};

} // namespace

// 0x00402640
// Region-based closest points between two segments. With this = P0 + s*D0 and other = P1 + t*D1, the squared
// distance is a quadratic in (s, t):  a00 s^2 + 2 a01 s t + a11 t^2 + 2 b0 s + 2 b1 t + c.  Its unconstrained
// minimum is checked against the unit square, and the edge or corner it falls outside of is minimised instead.
// The branch order follows the original exactly (it matters for which edge wins at region boundaries).
double Seg::distance_sqr_to_line(const Seg& other, float* s_out, float* t_out) const
{
    const float dx = start.x - other.start.x;
    const float dy = start.y - other.start.y;
    const float dz = start.z - other.start.z;
    const float d0x = end.x - start.x;
    const float d0y = end.y - start.y;
    const float d0z = end.z - start.z;
    const float d1x = other.end.x - other.start.x;
    const float d1y = other.end.y - other.start.y;
    const float d1z = other.end.z - other.start.z;

    const float a00 = (d0y * d0y) + (d0z * d0z) + (d0x * d0x);
    const float a01 = (-d0y * d1y) + (-d0z * d1z) + (-d0x * d1x);
    const float a11 = (d1y * d1y) + (d1z * d1z) + (d1x * d1x);
    const float b0 = (d0z * dz) + (d0y * dy) + (d0x * dx);
    const float length_sqr = (dy * dy) + (dz * dz) + (dx * dx);
    const float det = std::fabs((a11 * a00) - (a01 * a01));
    SegmentPairQuadratic quadratic{.dx = dx,
                                   .dy = dy,
                                   .dz = dz,
                                   .d1x = d1x,
                                   .d1y = d1y,
                                   .d1z = d1z,
                                   .a00 = a00,
                                   .a01 = a01,
                                   .a11 = a11,
                                   .b0 = b0,
                                   .length_sqr = length_sqr,
                                   .det = det,
                                   .sqr = length_sqr};

    // Comparisons are written so NaNs take the same branches as in the original.
    if (!(det < kParallelDeterminant)) {
        quadratic.minimise_crossing();
    } else if (a01 <= 0.0F) {
        quadratic.minimise_same_way();
    } else {
        quadratic.minimise_opposite_ways();
    }

    if (s_out) {
        *s_out = quadratic.along_a;
    }
    if (t_out) {
        *t_out = quadratic.along_b;
    }
    return std::fabs(quadratic.sqr);
}

// 0x004032E0
int Capsule::intersects_segment(const Seg& segment, SegmentParams params_out) const
{
    auto* const along_a = params_out.s;
    auto* const along_b = params_out.t;
    auto* const squared_gap = params_out.squared_gap_;
    auto distance = static_cast<float>(line.distance_sqr_to_line(segment, along_a, along_b));
    if (squared_gap) {
        *squared_gap = distance;
    }
    return distance <= radius * radius ? 1 : 0;
}

namespace {

constexpr float kEpsilon = 1e-5F;

bool long_enough(float x, float y, float z)
{
    return kEpsilon <= std::fabs(x) || kEpsilon <= std::fabs(y) || kEpsilon <= std::fabs(z);
}

// Limit to [0, 1]; NaN becomes 1, as in the original.
float clamp01(float value)
{
    if (!(value <= 1.0F)) {
        return 1.0F;
    }
    return value < 0.0F ? 0.0F : value;
}

} // namespace

// 0x00402F00
bool closest_points_between_lines(const Seg& a, const Seg& b, Vec4& point_a, Vec4& point_b, float& along_a,
                                  float& along_b, bool clamp)
{
    const float vx = b.end.x - b.start.x;
    const float vy = b.end.y - b.start.y;
    const float vz = b.end.z - b.start.z;
    if (!long_enough(vx, vy, vz)) {
        return false;
    }
    const float ux = a.end.x - a.start.x;
    const float uy = a.end.y - a.start.y;
    const float uz = a.end.z - a.start.z;
    if (!long_enough(ux, uy, uz)) {
        return false;
    }

    const float wx = a.start.x - b.start.x;
    const float wy = a.start.y - b.start.y;
    const float wz = a.start.z - b.start.z;
    const float dot_vw = (vx * wx) + (vy * wy) + (vz * wz);
    const float uv = (uy * vy) + (uz * vz) + (ux * vx);
    const float vv = (vy * vy) + (vz * vz) + (vx * vx);
    const float denominator = (((uy * uy) + (uz * uz) + (ux * ux)) * vv) - (uv * uv);
    if (!(kEpsilon <= std::fabs(denominator))) {
        return false; // parallel
    }

    along_a = ((uv * dot_vw) - (((ux * wx) + (uy * wy) + (uz * wz)) * vv)) / denominator;
    if (clamp) {
        along_a = clamp01(along_a);
    }
    along_b = ((uv * along_a) + dot_vw) / vv;
    if (clamp) {
        along_b = clamp01(along_b);
    }

    point_a = {.x = (ux * along_a) + a.start.x,
               .y = (uy * along_a) + a.start.y,
               .z = (uz * along_a) + a.start.z,
               .w = ((a.end.w - a.start.w) * along_a) + a.start.w};
    point_b = {.x = (vx * along_b) + b.start.x,
               .y = (vy * along_b) + b.start.y,
               .z = (vz * along_b) + b.start.z,
               .w = ((b.end.w - b.start.w) * along_b) + b.start.w};
    return true;
}

// 0x004817D0
bool point_in_box(const Vec4* point, const Vec4* max, const Vec4* min)
{
    return !(point->x > max->x) && !(point->y > max->y) && !(point->z > max->z) && !(point->x < min->x) &&
           !(point->y < min->y) && !(point->z < min->z);
}

} // namespace maths
