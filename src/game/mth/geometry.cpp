#include "game/mth/geometry.h"

#include <cmath>
#include <new>

#include "hook/hook.h"
#include "hook/shadow.h"

namespace Mth {

namespace {

constexpr float kHuge = 10000000.0f;
constexpr float kPi = 3.14159265f;

} // namespace

// 0x004033E0
BBox::BBox() : min{kHuge, kHuge, kHuge, 1.0f}, max{-kHuge, -kHuge, -kHuge, 1.0f} {}

// 0x00403410
BBox::BBox(const Vector& point) : min(point), max(point) {}

// 0x00403450
void BBox::AddPoint(const Vector& point)
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

// 0x0040AB00
BBox& BBox::operator=(const BBox& other)
{
    min = other.min;
    max = other.max;
    return *this;
}

// 0x0040B170 (x, then z, then y; each failing only when strictly beyond)
bool BBox::Intersects(const BBox& other) const
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
bool BBox::Contains(const BBox& other) const
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
Rect::Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}

// 0x00403BB0
Vector& BBox::GetCenter(Vector& out) const
{
    out = {(max.x - min.x) * 0.5f + min.x, (max.y - min.y) * 0.5f + min.y, (max.z - min.z) * 0.5f + min.z, 1.0f};
    return out;
}

// 0x004025A0
int BBox::ContainsPoint(const Vector& point) const
{
    // Written as "not outside" so NaN coordinates count as outside, as in the original.
    if (!(point.x >= min.x) || !(point.y >= min.y) || !(point.z >= min.z))
        return 0;
    if (!(point.x <= max.x) || !(point.y <= max.y) || !(point.z <= max.z))
        return 0;
    return 1;
}

// 0x004034D0
int BBox::CountAxesContaining(const Vector& point, uint32_t& axes) const
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
bool BBox::LineIntersect(const Line& line, Vector& hit, Vector& normal) const
{
    const Vector& s = line.start;
    const Vector& e = line.end;
    if (ContainsPoint(s) && ContainsPoint(e))
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
        double k = (1.0 / dx) * (static_cast<double>(max.x) - s.x);
        float y = static_cast<float>(k * dy + s.y), z = static_cast<float>(k * dz + s.z);
        if (inside_y_z(y, z)) {
            hit = {max.x, y, z, 1.0f};
            normal = {1.0f, 0.0f, 0.0f, 0.0f};
            return true;
        }
    }
    if (s.x < min.x && min.x < e.x && dx > 0.0f) {
        double k = (1.0 / dx) * (static_cast<double>(min.x) - s.x);
        float y = static_cast<float>(k * dy + s.y), z = static_cast<float>(k * dz + s.z);
        if (inside_y_z(y, z)) {
            hit = {min.x, y, z, 1.0f};
            normal = {-1.0f, 0.0f, 0.0f, 0.0f};
            return true;
        }
    }
    if (max.y < s.y && e.y < max.y && dy < 0.0f) {
        double k = (1.0 / dy) * (static_cast<double>(max.y) - s.y);
        float x = static_cast<float>(k * dx + s.x), z = static_cast<float>(k * dz + s.z);
        if (inside_x_z(x, z)) {
            hit = {x, max.y, z, 1.0f};
            normal = {0.0f, 1.0f, 0.0f, 0.0f};
            return true;
        }
    }
    if (s.y < min.y && min.y < e.y && dy > 0.0f) {
        double k = (1.0 / dy) * (static_cast<double>(min.y) - s.y);
        float x = static_cast<float>(k * dx + s.x), z = static_cast<float>(k * dz + s.z);
        if (inside_x_z(x, z)) {
            hit = {x, min.y, z, 1.0f};
            normal = {0.0f, -1.0f, 0.0f, 0.0f};
            return true;
        }
    }
    if (max.z < s.z && e.z < max.z && dz < 0.0f) {
        double k = (1.0 / dz) * (static_cast<double>(max.z) - s.z);
        float x = static_cast<float>(k * dx + s.x), y = static_cast<float>(k * dy + s.y);
        if (inside_x_y(x, y)) {
            hit = {x, y, max.z, 1.0f};
            normal = {0.0f, 0.0f, 1.0f, 0.0f};
            return true;
        }
    }
    if (s.z < min.z && e.z > min.z && dz > 0.0f) {
        double k = (1.0 / dz) * (static_cast<double>(min.z) - s.z);
        float x = static_cast<float>(k * dx + s.x), y = static_cast<float>(k * dy + s.y);
        if (inside_x_y(x, y)) {
            hit = {x, y, min.z, 1.0f};
            normal = {0.0f, 0.0f, -1.0f, 0.0f};
            return true;
        }
    }
    return false;
}

// 0x00403C00
void BBox::GetClosestSurfacePoint(const Vector& point, Vector& out) const
{
    uint32_t axes;
    if (CountAxesContaining(point, axes) == 3) {
        // Inside: move along whichever axis reaches a face soonest.
        float best_value = 0.0f, best_distance = 0.0f; // always set on the first pass (axis 0)
        int best_axis = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float p = (&point.x)[axis], lo = (&min.x)[axis], hi = (&max.x)[axis];
            float value = hi, distance = hi - p;
            if (!(distance <= p - lo)) {
                value = lo;
                distance = p - lo;
            }
            if (axis == 0 || distance < best_distance) {
                best_axis = axis;
                best_value = value;
                best_distance = distance;
            }
        }
        out = point;
        (&out.x)[best_axis] = best_value;
        return;
    }

    // Outside: keep the coordinates that are within range and clamp the rest.
    out.w = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float p = (&point.x)[axis], lo = (&min.x)[axis], hi = (&max.x)[axis];
        float& o = (&out.x)[axis];
        if (axes & (1u << axis))
            o = p;
        else
            o = (p <= lo) ? lo : hi; // NaN goes to max, as in the original
    }
}

// 0x00403220
Capsule::Capsule() : radius(0.0f) {}

// 0x00403230
Capsule::Capsule(const Vector& start, const Vector& offset, float r) : line(start, start + offset), radius(r) {}

// 0x004032A0
Capsule::Capsule(const Line& segment, float r) : line(segment), radius(r) {}

// 0x00403390
float Capsule::Volume() const
{
    float length = (line.end - line.start).Length();
    return radius * radius * radius * (4.0f / 3.0f * kPi) + length * radius * radius * kPi;
}

// 0x00403330
bool Capsule::ContainsPoint(const Vector& point, Vector* offset) const
{
    Vector local_offset;
    float distance_sqr = line.DistanceSqrToPoint(point, &local_offset);
    if (!(radius * radius >= distance_sqr))
        return false;
    if (offset)
        *offset = local_offset;
    return true;
}

// 0x00402D60
float Line::DistanceSqrToPoint(const Vector& point, Vector* offset) const
{
    float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
    float dw = end.w - start.w;
    float vx = point.x - start.x, vy = point.y - start.y, vz = point.z - start.z;

    // Project onto the segment: before the start, past the end, or in between.
    float along = vx * dx + vy * dy + vz * dz;
    float length_sqr = dx * dx + dy * dy + dz * dz;
    Vector result;
    if (along <= 0.0f) {
        result = point - start;
    } else if (length_sqr <= along) {
        result = point - end;
    } else {
        float t = along / length_sqr;
        result = {point.x - (dx * t + start.x), point.y - (dy * t + start.y), point.z - (dz * t + start.z),
                  point.w - (dw * t + start.w)};
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
double Line::DistanceSqrToLine(const Line& other, float* s_out, float* t_out) const
{
    const float dx = start.x - other.start.x, dy = start.y - other.start.y, dz = start.z - other.start.z;
    const float d0x = end.x - start.x, d0y = end.y - start.y, d0z = end.z - start.z;
    const float d1x = other.end.x - other.start.x, d1y = other.end.y - other.start.y,
                d1z = other.end.z - other.start.z;

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
        float tmp = b1 + a01;
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
            float tmp = b0 + a01;
            if (!(t < 0.0f) && !(t <= det) && tmp < 0.0f)
                edge_t1_inside(tmp, b1);
            else if (t < 0.0f && b0 < 0.0f)
                edge_t0();
            else
                edge_s0(b1);
        } else if (!(s <= det)) {
            float tmp = b0 + a01;
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
            float tmp = b0 + a01;
            if (tmp < 0.0f)
                edge_t1_inside(tmp, b1);
            else
                corner_s0_t1(b1);
        } else {
            // Interior: the unconstrained minimum is inside the unit square.
            float inv = 1.0f / det;
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
                float tmp = b1 + a01;
                sqr = b0 + b0 + (t * a11 + tmp + tmp) * t + c + a00;
            } else {
                t = 1.0f;
                float tmp = b1 + b0 + a01;
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
int Capsule::IntersectsSegment(const Line& segment, float* s, float* t, float* distance_sqr) const
{
    float distance = static_cast<float>(line.DistanceSqrToLine(segment, s, t));
    if (distance_sqr)
        *distance_sqr = distance;
    return distance <= radius * radius ? 1 : 0;
}

namespace {

constexpr float kEpsilon = 1e-5f;

bool LongEnough(float x, float y, float z)
{
    return kEpsilon <= std::fabs(x) || kEpsilon <= std::fabs(y) || kEpsilon <= std::fabs(z);
}

// Limit to [0, 1]; NaN becomes 1, as in the original.
float Clamp01(float value)
{
    if (!(value <= 1.0f))
        return 1.0f;
    return value < 0.0f ? 0.0f : value;
}

} // namespace

// 0x00402F00
bool ClosestPointsBetweenLines(const Line& a, const Line& b, Vector& point_a, Vector& point_b, float& s,
                               float& t, bool clamp)
{
    float vx = b.end.x - b.start.x, vy = b.end.y - b.start.y, vz = b.end.z - b.start.z;
    if (!LongEnough(vx, vy, vz))
        return false;
    float ux = a.end.x - a.start.x, uy = a.end.y - a.start.y, uz = a.end.z - a.start.z;
    if (!LongEnough(ux, uy, uz))
        return false;

    float wx = a.start.x - b.start.x, wy = a.start.y - b.start.y, wz = a.start.z - b.start.z;
    float e = vx * wx + vy * wy + vz * wz;
    float uv = uy * vy + uz * vz + ux * vx;
    float vv = vy * vy + vz * vz + vx * vx;
    float denominator = (uy * uy + uz * uz + ux * ux) * vv - uv * uv;
    if (!(kEpsilon <= std::fabs(denominator)))
        return false; // parallel

    s = (uv * e - (ux * wx + uy * wy + uz * wz) * vv) / denominator;
    if (clamp)
        s = Clamp01(s);
    t = (uv * s + e) / vv;
    if (clamp)
        t = Clamp01(t);

    point_a = {ux * s + a.start.x, uy * s + a.start.y, uz * s + a.start.z, (a.end.w - a.start.w) * s + a.start.w};
    point_b = {vx * t + b.start.x, vy * t + b.start.y, vz * t + b.start.z, (b.end.w - b.start.w) * t + b.start.w};
    return true;
}

// 0x004817D0
bool __cdecl PointInBox(const Vector* point, const Vector* max, const Vector* min)
{
    return !(point->x > max->x) && !(point->y > max->y) && !(point->z > max->z) && !(point->x < min->x) &&
           !(point->y < min->y) && !(point->z < min->z);
}

} // namespace Mth

// --- replacements -------------------------------------------------------------------------------------
namespace {

using Mth::BBox;
using Mth::Capsule;
using Mth::Line;
using Mth::Vector;

// Constructors keep the original __thiscall shape (object in ECX, returned in EAX).
struct GeometryAdapter {
    BBox* ConstructBBox() { return new (this) BBox(); }
    Capsule* ConstructEmptyCapsule() { return new (this) Capsule(); }
    Mth::Sphere* ConstructSphere() { return new (this) Mth::Sphere(); }
    BBox* ConstructBBoxAt(const Vector& point) { return new (this) BBox(point); }
    Capsule* ConstructCapsule(const Vector& start, const Vector& offset, float radius)
    {
        return new (this) Capsule(start, offset, radius);
    }
    Capsule* ConstructCapsuleFromLine(const Line& line, float radius) { return new (this) Capsule(line, radius); }
    Mth::Rect* ConstructRect(int left, int top, int right, int bottom)
    {
        return new (this) Mth::Rect(left, top, right, bottom);
    }
};

} // namespace

REPLACE(0x00402D60, &Line::DistanceSqrToPoint);
REPLACE(0x00402F00, Mth::ClosestPointsBetweenLines);
REPLACE(0x00403BB0, &BBox::GetCenter);
REPLACE(0x00404060, &GeometryAdapter::ConstructRect);
REPLACE_SHADOW(0x004025A0, &BBox::ContainsPoint);
REPLACE(0x004034D0, &BBox::CountAxesContaining);
REPLACE(0x00403550, &BBox::LineIntersect);
REPLACE(0x00403C00, &BBox::GetClosestSurfacePoint);
REPLACE(0x00402640, &Line::DistanceSqrToLine);
REPLACE(0x004032E0, &Capsule::IntersectsSegment);
REPLACE(0x00403330, &Capsule::ContainsPoint);
REPLACE(0x00403210, &GeometryAdapter::ConstructSphere);
REPLACE(0x00403220, &GeometryAdapter::ConstructEmptyCapsule);
REPLACE(0x00403230, &GeometryAdapter::ConstructCapsule);
REPLACE(0x004032A0, &GeometryAdapter::ConstructCapsuleFromLine);
REPLACE_SHADOW(0x00403390, &Capsule::Volume);
REPLACE(0x004033E0, &GeometryAdapter::ConstructBBox);
REPLACE(0x00403410, &GeometryAdapter::ConstructBBoxAt);
REPLACE(0x00403450, &BBox::AddPoint);
REPLACE(0x0040AB00, &BBox::operator=);
REPLACE(0x0040B170, &BBox::Intersects);
REPLACE(0x0040B1D0, &BBox::Contains);
REPLACE(0x004817D0, Mth::PointInBox);
