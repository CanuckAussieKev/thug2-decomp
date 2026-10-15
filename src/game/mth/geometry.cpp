#include "game/mth/geometry.h"

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

REPLACE(0x00403230, &GeometryAdapter::ConstructCapsule);
REPLACE(0x004032A0, &GeometryAdapter::ConstructCapsuleFromLine);
REPLACE(0x00403390, &Capsule::Volume);
REPLACE(0x004033E0, &GeometryAdapter::ConstructBBox);
REPLACE(0x00403410, &GeometryAdapter::ConstructBBoxAt);
REPLACE(0x00403450, &BBox::AddPoint);
