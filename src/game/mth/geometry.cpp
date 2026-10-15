#include "game/mth/geometry.h"

#include <cmath>
#include <new>

#include "hook/hook.h"

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
    BBox* ConstructBBoxAt(const Vector& point) { return new (this) BBox(point); }
    Capsule* ConstructCapsule(const Vector& start, const Vector& offset, float radius)
    {
        return new (this) Capsule(start, offset, radius);
    }
    Capsule* ConstructCapsuleFromLine(const Line& line, float radius) { return new (this) Capsule(line, radius); }
};

} // namespace

REPLACE(0x00402D60, &Line::DistanceSqrToPoint);
REPLACE(0x00402F00, Mth::ClosestPointsBetweenLines);
REPLACE(0x00403330, &Capsule::ContainsPoint);
REPLACE(0x00403230, &GeometryAdapter::ConstructCapsule);
REPLACE(0x004032A0, &GeometryAdapter::ConstructCapsuleFromLine);
REPLACE(0x00403390, &Capsule::Volume);
REPLACE(0x004033E0, &GeometryAdapter::ConstructBBox);
REPLACE(0x00403410, &GeometryAdapter::ConstructBBoxAt);
REPLACE(0x00403450, &BBox::AddPoint);
