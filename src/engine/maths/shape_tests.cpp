#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"

#include <cmath>
#include <cstdint>
#include <new>

namespace maths {

namespace {

constexpr float kHuge = 10000000.0f;
constexpr float kPi = 3.14159265f;

} // namespace

// 0x004033E0
BBox::BBox() : min{.x = kHuge, .y = kHuge, .z = kHuge, .w = 1.0f}, max{.x = -kHuge, .y = -kHuge, .z = -kHuge, .w = 1.0f} {}

// 0x00403410
BBox::BBox(const Vec4& point) noexcept : min(point), max(point) {}

// 0x00403450
void BBox::append_spot(const Vec4& point)
{
    if (point.x < min.x)
        min.x = point.x;
    if (point.y < min.y)
        min.y = point.y;
    if (point.z < min.z)
        min.z = point.z;
    if (point.x > max.x)
        max.x = point.x;
    if (point.y > max.y)
        max.y = point.y;
    if (point.z > max.z)
        max.z = point.z;
}

// 0x0040B170 (x, then z, then y; each failing only when strictly beyond)
bool BBox::intersects(const BBox& other) const
{
    if (min.x > other.max.x || other.min.x > max.x)
        return false;
    if (min.z > other.max.z || other.min.z > max.z)
        return false;
    if (min.y > other.max.y || other.min.y > max.y)
        return false;
    return true;
}

// 0x0040B1D0
bool BBox::get_holds(const BBox& other) const
{
    if (!(other.min.x >= min.x) || !(other.max.x <= max.x))
        return false;
    if (!(other.min.y >= min.y) || !(other.max.y <= max.y))
        return false;
    if (!(other.min.z >= min.z) || !(other.max.z <= max.z))
        return false;
    return true;
}

// 0x00404060 (Ghidra's signature database labels this MFC's CRect constructor; it is the same simple code)
Rect2::Rect2(const Edges& edges) : edges_(edges) {}

// 0x00403BB0
Vec4& BBox::get_center(Vec4& out) const
{
    out = {.x = (max.x - min.x) * 0.5f + min.x, .y = (max.y - min.y) * 0.5f + min.y, .z = (max.z - min.z) * 0.5f + min.z, .w = 1.0f};
    return out;
}

// 0x004025A0
int BBox::contains_point(const Vec4& point) const
{
    // Written as "not outside" so NaN coordinates count as outside, as in the original.
    if (!(point.x >= min.x) || !(point.y >= min.y) || !(point.z >= min.z))
        return 0;
    if (!(point.x <= max.x) || !(point.y <= max.y) || !(point.z <= max.z))
        return 0;
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
    const Vec4& s = line.start;
    const Vec4& e = line.end;
    if (contains_point(s) && contains_point(e))
        return false;

    // Both ends beyond the same face: no hit.
    if (max.y < s.y && e.y > max.y)
        return false;
    if (s.y < min.y && e.y < min.y)
        return false;
    if (max.x < s.x && e.x > max.x)
        return false;
    if (s.x < min.x && e.x < min.x)
        return false;
    if (max.z < s.z && e.z > max.z)
        return false;
    if (s.z < min.z && e.z < min.z)
        return false;

    float dx = e.x - s.x, dy = e.y - s.y, dz = e.z - s.z;
    if (dx == 0.0f)
        dx = 1e-6f;
    if (dy == 0.0f)
        dy = 1e-6f;
    if (dz == 0.0f)
        dz = 1e-6f;

    // Crossing a face at fraction k along the segment; the hit must be strictly inside the face. (k and the
    // crossing in double, rounded to floats where the original's x87 code stores them.)
    auto inside_y_z = [&](float y, float z) { return y < max.y && min.y < y && z < max.z && min.z < z; };
    auto inside_x_z = [&](float x, float z) { return x < max.x && min.x < x && z < max.z && min.z < z; };
    auto inside_x_y = [&](float x, float y) { return x < max.x && min.x < x && y < max.y && min.y < y; };

    if (max.x < s.x && e.x < max.x && dx < 0.0f) {
        const double k = (1.0 / dx) * (static_cast<double>(max.x) - s.x);
        auto y = static_cast<float>(k * dy + s.y), z = static_cast<float>(k * dz + s.z);
        if (inside_y_z(y, z)) {
            hit = {.x = max.x, .y = y, .z = z, .w = 1.0f};
            normal = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
            return true;
        }
    }
    if (s.x < min.x && min.x < e.x && dx > 0.0f) {
        const double k = (1.0 / dx) * (static_cast<double>(min.x) - s.x);
        auto y = static_cast<float>(k * dy + s.y), z = static_cast<float>(k * dz + s.z);
        if (inside_y_z(y, z)) {
            hit = {.x = min.x, .y = y, .z = z, .w = 1.0f};
            normal = {.x = -1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
            return true;
        }
    }
    if (max.y < s.y && e.y < max.y && dy < 0.0f) {
        const double k = (1.0 / dy) * (static_cast<double>(max.y) - s.y);
        auto x = static_cast<float>(k * dx + s.x), z = static_cast<float>(k * dz + s.z);
        if (inside_x_z(x, z)) {
            hit = {.x = x, .y = max.y, .z = z, .w = 1.0f};
            normal = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
            return true;
        }
    }
    if (s.y < min.y && min.y < e.y && dy > 0.0f) {
        const double k = (1.0 / dy) * (static_cast<double>(min.y) - s.y);
        auto x = static_cast<float>(k * dx + s.x), z = static_cast<float>(k * dz + s.z);
        if (inside_x_z(x, z)) {
            hit = {.x = x, .y = min.y, .z = z, .w = 1.0f};
            normal = {.x = 0.0f, .y = -1.0f, .z = 0.0f, .w = 0.0f};
            return true;
        }
    }
    if (max.z < s.z && e.z < max.z && dz < 0.0f) {
        const double k = (1.0 / dz) * (static_cast<double>(max.z) - s.z);
        auto x = static_cast<float>(k * dx + s.x), y = static_cast<float>(k * dy + s.y);
        if (inside_x_y(x, y)) {
            hit = {.x = x, .y = y, .z = max.z, .w = 1.0f};
            normal = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
            return true;
        }
    }
    if (s.z < min.z && e.z > min.z && dz > 0.0f) {
        const double k = (1.0 / dz) * (static_cast<double>(min.z) - s.z);
        auto x = static_cast<float>(k * dx + s.x), y = static_cast<float>(k * dy + s.y);
        if (inside_x_y(x, y)) {
            hit = {.x = x, .y = y, .z = min.z, .w = 1.0f};
            normal = {.x = 0.0f, .y = 0.0f, .z = -1.0f, .w = 0.0f};
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
        float best_value = 0.0f, best_distance = 0.0f; // always set on the first pass (axis 0)
        int winning_axis = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const float p = point[axis];
            const float lo = min[axis];
            const float hi = max[axis];
            float value = hi, distance = hi - p;
            if (!(distance <= p - lo)) {
                value = lo;
                distance = p - lo;
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
    out.w = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float p = point[axis];
        const float lo = min[axis];
        const float hi = max[axis];
        float& o = out[axis];
        if (axes & (1u << axis))
            o = p;
        else
            o = (p <= lo) ? lo : hi; // NaN goes to max, as in the original
    }
}

// 0x00403220
Capsule::Capsule() : radius(0.0f) {}

// 0x00403230
Capsule::Capsule(const Vec4& start, const Vec4& offset, float r) : line(start, start + offset), radius(r) {}

// 0x004032A0
Capsule::Capsule(const Seg& segment, float r) : line(segment), radius(r) {}

// 0x00403390
float Capsule::get_loudness() const
{
    const float length = (line.end - line.start).len_of();
    return radius * radius * radius * (4.0f / 3.0f * kPi) + length * radius * radius * kPi;
}

// 0x00403330
bool Capsule::contains_point(const Vec4& point, Vec4* offset) const
{
    Vec4 local_offset{};
    const float squared_gap = line.distance_sqr_to_point(point, &local_offset);
    if (!(radius * radius >= squared_gap))
        return false;
    if (offset)
        *offset = local_offset;
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
    const float along = vx * dx + vy * dy + vz * dz;
    const float squared_len = dx * dx + dy * dy + dz * dz;
    Vec4 result{};
    if (along <= 0.0f) {
        result = point - start;
    } else if (squared_len <= along) {
        result = point - end;
    } else {
        const float t = along / squared_len;
        result = {.x = point.x - (dx * t + start.x), .y = point.y - (dy * t + start.y), .z = point.z - (dz * t + start.z),
                  .w = point.w - (dw * t + start.w)};
    }
    if (offset)
        *offset = result;
    return result.x * result.x + result.y * result.y + result.z * result.z;
}

// 0x00402640
// Region-based closest points between two segments. With this = P0 + s*D0 and other = P1 + t*D1, the squared
// distance is a quadratic in (s, t):  a00 s^2 + 2 a01 s t + a11 t^2 + 2 b0 s + 2 b1 t + c.  Its unconstrained
// minimum is checked against the unit square, and the edge or corner it falls outside of is minimised instead.
// The branch order follows the original exactly (it matters for which edge wins at region boundaries).
double Seg::distance_sqr_to_line(const Seg& other, float* s_out, float* t_out) const
{
    const float dx = start.x - other.start.x, dy = start.y - other.start.y, dz = start.z - other.start.z;
    const float d0x = end.x - start.x, d0y = end.y - start.y, d0z = end.z - start.z;
    const float d1x = other.end.x - other.start.x, d1y = other.end.y - other.start.y, d1z = other.end.z - other.start.z;

    const float a00 = d0y * d0y + d0z * d0z + d0x * d0x;
    const float a01 = -d0y * d1y + -d0z * d1z + -d0x * d1x;
    const float a11 = d1y * d1y + d1z * d1z + d1x * d1x;
    const float b0 = d0z * dz + d0y * dy + d0x * dx;
    const float c = dy * dy + dz * dz + dx * dx;
    const float det = std::fabs(a11 * a00 - a01 * a01);
    auto compute_b1 = [&] { return -dy * d1y + -dz * d1z + -dx * d1x; };

    float s = 0.0f, t = 0.0f, sqr = c;

    // Minimise along one edge of the unit square.
    auto edge_s0 = [&](float b1) { // s = 0, t free
        s = 0.0f;
        if (b1 >= 0.0f) {
            t = 0.0f;
            sqr = c;
        } else if (a11 <= -b1) {
            t = 1.0f;
            sqr = b1 + b1 + c + a11;
        } else {
            t = -b1 / a11;
            sqr = -(b1 / a11) * b1 + c;
        }
    };
    auto edge_t0 = [&] { // t = 0, s free
        t = 0.0f;
        if (b0 >= 0.0f) {
            s = 0.0f;
            sqr = c;
        } else if (a00 <= -b0) {
            s = 1.0f;
            sqr = b0 + b0 + c + a00;
        } else {
            s = -b0 / a00;
            sqr = -(b0 / a00) * b0 + c;
        }
    };
    auto edge_s1 = [&](float b1) { // s = 1, t free
        s = 1.0f;
        const float tmp = b1 + a01;
        if (tmp < 0.0f) {
            if (-tmp < a11) {
                t = -tmp / a11;
                sqr = b0 + b0 + tmp * t + c + a00;
            } else {
                t = 1.0f;
                sqr = tmp + b0 + tmp + b0 + c + a11 + a00;
            }
        } else {
            t = 0.0f;
            sqr = b0 + b0 + c + a00;
        }
    };
    // t = 1 with s free, when b0 + a01 < 0 (the caller checks).
    auto edge_t1_inside = [&](float tmp, float b1) {
        t = 1.0f;
        if (-tmp < a00) {
            s = -tmp / a00;
            sqr = b1 + b1 + tmp * s + c + a11;
        } else {
            s = 1.0f;
            sqr = tmp + b1 + tmp + b1 + c + a11 + a00;
        }
    };
    auto corner_s0_t1 = [&](float b1) {
        s = 0.0f;
        t = 1.0f;
        sqr = b1 + b1 + c + a11;
    };

    // Comparisons are written so NaNs take the same branches as in the original.
    if (!(det < 1e-6f)) {
        // Not parallel: scaled by det, the unconstrained minimum is at (s, t).
        const float b1 = compute_b1();
        s = a01 * b1 - a11 * b0;
        t = a01 * b0 - a00 * b1;
        if (s < 0.0f) {
            const float tmp = b0 + a01;
            if (!(t < 0.0f) && !(t <= det) && tmp < 0.0f)
                edge_t1_inside(tmp, b1);
            else if (t < 0.0f && b0 < 0.0f)
                edge_t0();
            else
                edge_s0(b1);
        } else if (!(s <= det)) {
            const float tmp = b0 + a01;
            if (t < 0.0f && -b0 < a00) {
                edge_t0();
            } else if (!(t < 0.0f) && !(t <= det) && -tmp <= a00) {
                if (tmp < 0.0f) {
                    t = 1.0f;
                    s = -tmp / a00;
                    sqr = b1 + b1 + tmp * s + c + a11;
                } else {
                    corner_s0_t1(b1);
                }
            } else {
                edge_s1(b1);
            }
        } else if (t < 0.0f) {
            edge_t0();
        } else if (!(t <= det)) {
            const float tmp = b0 + a01;
            if (tmp < 0.0f)
                edge_t1_inside(tmp, b1);
            else
                corner_s0_t1(b1);
        } else {
            // Interior: the unconstrained minimum is inside the unit square.
            const float inv = 1.0f / det;
            s *= inv;
            t *= inv;
            sqr = (b0 + b0 + s * a00 + t * a01) * s + (b1 + b1 + s * a01 + t * a11) * t + c;
        }
    } else if (a01 <= 0.0f) {
        // Parallel, pointing the same way.
        if (a00 <= -b0) {
            t = 0.0f;
            s = 1.0f;
            sqr = b0 + b0 + c + a00;
        } else if (!(b0 <= 0.0f)) {
            const float b1 = compute_b1();
            s = 0.0f;
            if (-a01 <= b0) {
                t = 1.0f;
                sqr = b1 + b1 + c + a11;
            } else {
                t = -(b0 / a01);
                sqr = (b1 + b1 + t * a11) * t + c;
            }
        } else {
            s = -(b0 / a00);
            t = 0.0f;
            sqr = s * b0 + c;
        }
    } else {
        // Parallel, pointing opposite ways.
        if (b0 >= 0.0f) {
            s = 0.0f;
            t = 0.0f;
            sqr = c;
        } else if (!(-b0 <= a00)) {
            s = 1.0f;
            const float b1 = compute_b1();
            if (-(b0 + a00) < a01) {
                t = -((b0 + a00) / a01);
                const float tmp = b1 + a01;
                sqr = b0 + b0 + (t * a11 + tmp + tmp) * t + c + a00;
            } else {
                t = 1.0f;
                const float tmp = b1 + b0 + a01;
                sqr = tmp + tmp + c + a11 + a00;
            }
        } else {
            s = -(b0 / a00);
            t = 0.0f;
            sqr = s * b0 + c;
        }
    }

    if (s_out)
        *s_out = s;
    if (t_out)
        *t_out = t;
    return std::fabs(sqr);
}

// 0x004032E0
int Capsule::intersects_segment(const Seg& segment, SegmentParams params_out) const
{
    const auto s = params_out.s;
    const auto t = params_out.t;
    const auto squared_gap = params_out.squared_gap_;
    auto distance = static_cast<float>(line.distance_sqr_to_line(segment, s, t));
    if (squared_gap)
        *squared_gap = distance;
    return distance <= radius * radius ? 1 : 0;
}

namespace {

constexpr float kEpsilon = 1e-5f;

bool long_enough(float x, float y, float z)
{
    return kEpsilon <= std::fabs(x) || kEpsilon <= std::fabs(y) || kEpsilon <= std::fabs(z);
}

// Limit to [0, 1]; NaN becomes 1, as in the original.
float clamp01(float value)
{
    if (!(value <= 1.0f))
        return 1.0f;
    return value < 0.0f ? 0.0f : value;
}

} // namespace

// 0x00402F00
bool closest_points_between_lines(const Seg& a, const Seg& b, Vec4& point_a, Vec4& point_b, float& s, float& t,
                                  bool clamp)
{
    const float vx = b.end.x - b.start.x;
    const float vy = b.end.y - b.start.y;
    const float vz = b.end.z - b.start.z;
    if (!long_enough(vx, vy, vz))
        return false;
    const float ux = a.end.x - a.start.x;
    const float uy = a.end.y - a.start.y;
    const float uz = a.end.z - a.start.z;
    if (!long_enough(ux, uy, uz))
        return false;

    const float wx = a.start.x - b.start.x;
    const float wy = a.start.y - b.start.y;
    const float wz = a.start.z - b.start.z;
    const float e = vx * wx + vy * wy + vz * wz;
    const float uv = uy * vy + uz * vz + ux * vx;
    const float vv = vy * vy + vz * vz + vx * vx;
    const float denominator = (uy * uy + uz * uz + ux * ux) * vv - uv * uv;
    if (!(kEpsilon <= std::fabs(denominator)))
        return false; // parallel

    s = (uv * e - (ux * wx + uy * wy + uz * wz) * vv) / denominator;
    if (clamp)
        s = clamp01(s);
    t = (uv * s + e) / vv;
    if (clamp)
        t = clamp01(t);

    point_a = {.x = ux * s + a.start.x, .y = uy * s + a.start.y, .z = uz * s + a.start.z, .w = (a.end.w - a.start.w) * s + a.start.w};
    point_b = {.x = vx * t + b.start.x, .y = vy * t + b.start.y, .z = vz * t + b.start.z, .w = (b.end.w - b.start.w) * t + b.start.w};
    return true;
}

// 0x004817D0
bool point_in_box(const Vec4* point, const Vec4* max, const Vec4* min)
{
    return !(point->x > max->x) && !(point->y > max->y) && !(point->z > max->z) && !(point->x < min->x) &&
           !(point->y < min->y) && !(point->z < min->z);
}

} // namespace maths
