// The collision code's intersection tests.

#include "game/nx/coll_tests.h"

#include <cmath>

#include "hook/hook.h"

namespace Nx {

namespace {

constexpr float kMinDeterminant = 0.03f;
constexpr float kBoundSlack = 1.00001f;
constexpr float kParallel = 1e-11f;

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

} // namespace

// 0x0040B420 (NaN in the determinant or the first barycentric coordinate doesn't rule a hit out by itself)
bool RayHitsTriangle(const Mth::Vector& start, const Mth::Vector& direction, const Mth::Vector& v0,
                     const Mth::Vector& v1, const Mth::Vector& v2, float* t)
{
    Mth::Vector edge1{Difference(v1.x, v0.x), Difference(v1.y, v0.y), Difference(v1.z, v0.z), Difference(v1.w, v0.w)};
    float e2x = Difference(v2.x, v0.x), e2y = Difference(v2.y, v0.y), e2z = Difference(v2.z, v0.z);
    // p = direction x edge2
    double px = static_cast<double>(e2z) * direction.y - static_cast<double>(e2y) * direction.z;
    double py = static_cast<double>(e2x) * direction.z - static_cast<double>(e2z) * direction.x;
    double pz = static_cast<double>(e2y) * direction.x - static_cast<double>(e2x) * direction.y;
    double determinant = (edge1.z * pz + px * edge1.x) + py * edge1.y;
    if (determinant < kMinDeterminant)
        return false;
    float det = static_cast<float>(determinant);
    float bound = static_cast<float>(static_cast<double>(det) * kBoundSlack);
    Mth::Vector offset{Difference(start.x, v0.x), Difference(start.y, v0.y), Difference(start.z, v0.z),
                       Difference(start.w, v0.w)};
    float u = static_cast<float>((offset.z * pz + offset.y * py) + offset.x * px);
    if (u < 0.0f || u > bound)
        return false;
    Mth::Vector q = Mth::CrossProduct(offset, edge1);
    double v = (static_cast<double>(q.z) * direction.z + static_cast<double>(q.x) * direction.x) +
               static_cast<double>(q.y) * direction.y;
    if (v < 0.0 || u + v > bound)
        return false;
    double along = (1.0 / det) *
                   ((static_cast<double>(q.z) * e2z + static_cast<double>(q.y) * e2y) + static_cast<double>(q.x) * e2x);
    *t = static_cast<float>(along);
    return along <= 1.0 && !(along < 0.0);
}

// 0x0040B610
bool LineHitsSphere(const Mth::Vector& start, const Mth::Vector& base, const CollSphere& sphere,
                    const Mth::Vector& end, float* t, float limit)
{
    double dx = static_cast<double>(end.x) - base.x;
    double dy = static_cast<double>(end.y) - base.y;
    double dz = static_cast<double>(end.z) - base.z;
    float mx = Difference(sphere.centre.x, start.x);
    float my = Difference(sphere.centre.y, start.y);
    float mz = Difference(sphere.centre.z, start.z);
    // the line's distance from the centre, squared, times its length squared: |m x d|^2
    double cx = my * dz - mz * dy;
    double cy = mz * dx - dz * mx;
    double cz = mx * dy - my * dx;
    float radius2 = static_cast<float>(static_cast<double>(sphere.radius) * sphere.radius);
    float length2 = static_cast<float>((dz * dz + dx * dx) + dy * dy);
    double crossed2 = (cz * cz + cy * cy) + cx * cx;
    if (static_cast<double>(length2) * radius2 < crossed2)
        return false;
    double b = (mz * dz + my * dy) + mx * dx;
    double c = ((static_cast<double>(mz) * mz + static_cast<double>(my) * my) + static_cast<double>(mx) * mx) - radius2;
    double inverse = 1.0 / std::sqrt(std::fabs(b * b - c * length2));
    double along = (inverse * c) / (1.0 - inverse * b);
    *t = static_cast<float>(along);
    return !(along > limit);
}

// 0x0040B750 (the first sign test uses the denominator before it's rounded to a float, the rest after)
bool SegmentsCross2D(float ax, float ay, float adx, float ady, float bx, float by, float bdx, float bdy, float* s)
{
    double dx = static_cast<double>(ax) - bx;
    double dy = static_cast<double>(ay) - by;
    float sNumerator = static_cast<float>(bdx * dy - dx * bdy);
    float uNumerator = static_cast<float>(dy * adx - dx * ady);
    double denominatorExact = static_cast<double>(adx) * bdy - static_cast<double>(ady) * bdx;
    float denominator = static_cast<float>(denominatorExact);
    // each numerator between 0 and the denominator, whichever its sign (NaN passes)
    auto within = [](float numerator, float den, bool positive) {
        if (positive)
            return !(numerator < 0.0f) && !(numerator > den);
        return !(numerator > 0.0f) && !(numerator < den);
    };
    if (!within(sNumerator, denominator, denominatorExact > 0.0))
        return false;
    if (!within(uNumerator, denominator, denominator > 0.0f))
        return false;
    *s = std::fabs(denominator) < kParallel ? 0.0f : sNumerator / denominator;
    return true;
}

// The arguments copied below the saved EDX (each push moves the next one into the same place).
__declspec(naked) void LineHitsSphereKeepingEdx()
{
    __asm {
        push edx
        push dword ptr [esp + 0x1C]
        push dword ptr [esp + 0x1C]
        push dword ptr [esp + 0x1C]
        push dword ptr [esp + 0x1C]
        push dword ptr [esp + 0x1C]
        push dword ptr [esp + 0x1C]
        call LineHitsSphere
        add esp, 0x18
        pop edx
        ret
    }
}

__declspec(naked) void SegmentsCross2DKeepingEdx()
{
    __asm {
        push edx
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        call SegmentsCross2D
        add esp, 0x24
        pop edx
        ret
    }
}

} // namespace Nx

REPLACE(0x0040B420, Nx::RayHitsTriangle);
REPLACE_UNCHECKED(0x0040B610, Nx::LineHitsSphereKeepingEdx);
REPLACE_UNCHECKED(0x0040B750, Nx::SegmentsCross2DKeepingEdx);
