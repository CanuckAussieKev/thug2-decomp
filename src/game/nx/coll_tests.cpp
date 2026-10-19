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

// 0x0040B860 (a = |e0|^2, b = e0.e1, c = |e1|^2, d = e0.D, e = e1.D, f = |D|^2 with e0 = v1 - v0, e1 = v2 - v0,
// D = v0 - point; rounded where the original's x87 code stores, a few terms kept unrounded as it keeps them)
double PointTriangleDistanceSquared(const Mth::Vector& point, const Mth::Vector& v0, const Mth::Vector& v1,
                                    const Mth::Vector& v2, float* sOut, float* tOut)
{
    double dx = static_cast<double>(v0.x) - point.x;
    double dy = static_cast<double>(v0.y) - point.y;
    double dz = static_cast<double>(v0.z) - point.z;
    double e0xExact = static_cast<double>(v1.x) - v0.x;
    double e0yExact = static_cast<double>(v1.y) - v0.y;
    double e0zExact = static_cast<double>(v1.z) - v0.z;
    float e0x = static_cast<float>(e0xExact), e0y = static_cast<float>(e0yExact), e0z = static_cast<float>(e0zExact);
    double e1xExact = static_cast<double>(v2.x) - v0.x;
    float e1x = static_cast<float>(e1xExact);
    float e1y = Difference(v2.y, v0.y);
    float e1z = Difference(v2.z, v0.z);
    float a = static_cast<float>((e0zExact * e0z + e0yExact * e0yExact) + static_cast<double>(e0x) * e0x);
    float b = static_cast<float>((static_cast<double>(e0z) * e1z + static_cast<double>(e0y) * e1y) + e0x * e1xExact);
    float c = static_cast<float>((static_cast<double>(e1z) * e1z + static_cast<double>(e1y) * e1y) +
                                 static_cast<double>(e1x) * e1x);
    float d = static_cast<float>((e0z * dz + e0yExact * dy) + e0x * dx);
    float e = static_cast<float>((e1z * dz + e1y * dy) + e1x * dx);
    double f = (dx * dx + dz * dz) + dy * dy;
    float det = static_cast<float>(std::fabs(static_cast<double>(c) * a - static_cast<double>(b) * b));
    float s = static_cast<float>(static_cast<double>(e) * b - static_cast<double>(d) * c);
    double t = static_cast<double>(d) * b - static_cast<double>(e) * a;

    float so = 0.0f, to = 0.0f;
    double result = f;
    // the endings the regions share
    auto atV1 = [&]() { // s = 1, t = 0
        so = 1.0f;
        to = 0.0f;
        result = (2.0 * d + f) + a;
    };
    auto atV2 = [&]() { // s = 0, t = 1
        so = 0.0f;
        to = 1.0f;
        result = (2.0 * e + f) + c;
    };
    auto alongEdge0 = [&]() { // t = 0, s = -d / a
        double q = -(static_cast<double>(d) / a);
        so = static_cast<float>(q);
        result = f + q * d;
    };
    auto alongEdge1 = [&]() { // s = 0, t = -e / c
        double q = -(static_cast<double>(e) / c);
        to = static_cast<float>(q);
        result = f + q * e;
    };
    // the general quadratic at (so, to) - its first t term from `tTerm`, which is to or the unrounded 1 - so
    auto general = [&](double tTerm) {
        double first = ((tTerm * c + static_cast<double>(so) * b) + 2.0 * e) * to;
        double second = ((static_cast<double>(to) * b + static_cast<double>(so) * a) + 2.0 * d) * so;
        result = f + (first + second);
    };
    auto denominator = [&]() { return (static_cast<double>(a) - 2.0 * b) + c; };
    // "less" as most of the original's tests here read the flags: NaN counts as less
    auto below = [](double x, double y) { return !(x >= y); };

    if (s + t <= det) {
        if (s < 0.0f) {
            if (t < 0.0) { // region 4
                if (d < 0.0f) {
                    to = 0.0f;
                    if (below(-d, a))
                        alongEdge0();
                    else
                        atV1();
                } else {
                    so = 0.0f;
                    if (!below(e, 0.0))
                        to = 0.0f;
                    else if (below(-e, c))
                        alongEdge1();
                    else
                        atV2();
                }
            } else { // region 3 (e not negated: the original's slip)
                so = 0.0f;
                if (!below(e, 0.0))
                    to = 0.0f;
                else if (below(e, c))
                    alongEdge1();
                else
                    atV2();
            }
        } else if (t < 0.0) { // region 5
            to = 0.0f;
            if (!below(d, 0.0))
                so = 0.0f;
            else if (below(-d, a))
                alongEdge0();
            else
                atV1();
        } else { // region 0: inside
            double inverse = 1.0 / det;
            so = static_cast<float>(s * inverse);
            to = static_cast<float>(t * inverse);
            general(to);
        }
    } else if (s < 0.0f) { // region 2
        float near0 = static_cast<float>(static_cast<double>(d) + b);
        double near1 = static_cast<double>(e) + c;
        if (near1 > near0) {
            double numerator = near1 - near0;
            double den = denominator();
            if (below(numerator, den)) {
                so = static_cast<float>(numerator / den);
                double rest = 1.0 - so;
                to = static_cast<float>(rest);
                general(rest);
            } else {
                atV1();
            }
        } else {
            so = 0.0f;
            if (near1 <= 0.0)
                atV2();
            else if (below(e, 0.0))
                alongEdge1();
            else
                to = 0.0f;
        }
    } else if (t < 0.0) { // region 6
        float near0 = static_cast<float>(static_cast<double>(e) + b);
        double near1 = static_cast<double>(d) + a;
        if (near1 > near0) {
            double numerator = near1 - near0;
            double den = denominator();
            if (below(numerator, den)) {
                to = static_cast<float>(numerator / den);
                so = static_cast<float>(1.0 - to);
                general(to);
            } else {
                atV2();
            }
        } else {
            to = 0.0f;
            if (near1 <= 0.0)
                atV1();
            else if (below(d, 0.0))
                alongEdge0();
            else
                so = 0.0f;
        }
    } else { // region 1
        double numerator = ((static_cast<double>(e) + c) - b) - d;
        float numeratorRounded = static_cast<float>(numerator);
        if (numerator <= 0.0) {
            atV2();
        } else if (below(numeratorRounded, denominator())) {
            so = static_cast<float>(numeratorRounded / denominator());
            double rest = 1.0 - so;
            to = static_cast<float>(rest);
            general(rest);
        } else {
            atV1();
        }
    }
    if (sOut)
        *sOut = so;
    if (tOut)
        *tOut = to;
    return std::fabs(result);
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
REPLACE(0x0040B860, Nx::PointTriangleDistanceSquared);
REPLACE_UNCHECKED(0x0040B610, Nx::LineHitsSphereKeepingEdx);
REPLACE_UNCHECKED(0x0040B750, Nx::SegmentsCross2DKeepingEdx);
