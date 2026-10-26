// The collision code's intersection tests.

#include "game/nx/coll_tests.h"

#include <cmath>

#include "game/core/dbg.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr float kMinDeterminant = 0.03f;
constexpr float kBoundSlack = 1.00001f;
constexpr float kParallel = 1e-11f;
// A segment whose direction is this close to across the triangle's normal (squared, relative) counts as parallel
// to it (0x006453D4).
constexpr float kParallelSegment = 1e-4f;

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// A plane distance this small counts as on the plane (0x006453B8).
constexpr float kOnPlane = 0.00001f;

float Component(const Mth::Vector& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// Where the triangle edge from `a` to `b` - plane distances da and db, on either side - meets the plane:
// a + (b - a) da / (da - db); a itself when the distances are (nearly) equal. Rounded as the original's x87 code
// does it for P (and for Q on v0 v2): the x and w parts of (b - a) da rounded before the division, y and z not.
Mth::Vector EdgeCrossing(const Mth::Vector& a, const Mth::Vector& b, float da, float db)
{
    double difference = static_cast<double>(da) - db;
    if (!(std::fabs(difference) > kOnPlane))
        return a;
    double ex = static_cast<double>(b.x) - a.x;
    double ey = static_cast<double>(b.y) - a.y;
    float ez = Difference(b.z, a.z);
    float ew = Difference(b.w, a.w);
    float sx = static_cast<float>(ex * da);
    float sw = static_cast<float>(static_cast<double>(ew) * da);
    float inverse = static_cast<float>(1.0 / difference);
    Mth::Vector p;
    p.x = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(sx) * inverse)) + a.x);
    p.y = static_cast<float>(static_cast<double>(static_cast<float>(ey * da * inverse)) + a.y);
    p.z = static_cast<float>(static_cast<double>(ez) * da * inverse + a.z);
    p.w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(sw) * inverse)) + a.w);
    return p;
}

// The same as Q's other form computes it: (b - a) da rounded part by part, divided by (da - db) as a float
// (Mth::operator/), added to a.
Mth::Vector EdgeCrossingScaled(const Mth::Vector& a, const Mth::Vector& b, float da, float db)
{
    double difference = static_cast<double>(da) - db;
    if (!(std::fabs(difference) > kOnPlane))
        return a;
    float ez = Difference(b.z, a.z);
    float ew = Difference(b.w, a.w);
    Mth::Vector scaled{static_cast<float>((static_cast<double>(b.x) - a.x) * da),
                       static_cast<float>((static_cast<double>(b.y) - a.y) * da),
                       static_cast<float>(static_cast<double>(ez) * da), static_cast<float>(static_cast<double>(ew) * da)};
    Mth::Vector r = Mth::operator/(scaled, static_cast<float>(difference));
    return {a.x + r.x, r.y + a.y, r.z + a.z, r.w + a.w};
}

// Whether a point is on the rect's side of a side's line: the line value at or above 0 when the rect lies towards
// positive values, else at or below.
bool Inside(bool towardsPositive, double value) { return towardsPositive ? value >= 0.0 : value <= 0.0; }

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

// 0x0040BDC0 (with e1 = v1 - v0, e2 = v2 - v0, d = end - start, D = v0 - start: the 3x3 system for the segment's
// t and the triangle's s, t solved by Cramer's rule; rounded where the original's x87 code stores)
double SegmentTriangleDistanceSquared(const Mth::Line& segment, const Mth::Vector& /*normal*/, const Mth::Vector& v0,
                                      const Mth::Vector& v1, const Mth::Vector& v2, float* sOut, float* tOut,
                                      float* alongOut)
{
    const Mth::Vector& start = segment.start;
    const double Dx = static_cast<double>(v0.x) - start.x;
    const double Dy = static_cast<double>(v0.y) - start.y;
    const double Dz = static_cast<double>(v0.z) - start.z;
    const float dx = Difference(segment.end.x, start.x), dy = Difference(segment.end.y, start.y),
                dz = Difference(segment.end.z, start.z);
    const float e1x = Difference(v1.x, v0.x), e1y = Difference(v1.y, v0.y), e1z = Difference(v1.z, v0.z);
    const float e2x = Difference(v2.x, v0.x), e2y = Difference(v2.y, v0.y), e2z = Difference(v2.z, v0.z);
    auto product = [](float a, float b) { return static_cast<double>(a) * b; };

    const float dd = static_cast<float>((product(dz, dz) + product(dy, dy)) + product(dx, dx));
    const float de1 = static_cast<float>((product(e1z, -dz) + product(e1y, -dy)) + product(-dx, e1x)); // -(d.e1)
    const float de2 = static_cast<float>((product(e2y, -dy) + product(-dx, e2x)) + product(-dz, e2z)); // -(d.e2)
    const float e1e1 = static_cast<float>((product(e1z, e1z) + product(e1y, e1y)) + product(e1x, e1x));
    const float e1e2 = static_cast<float>((product(e1z, e2z) + product(e2y, e1y)) + product(e2x, e1x));
    const float e2e2 = static_cast<float>((product(e2y, e2y) + product(e2x, e2x)) + product(e2z, e2z));
    const float dD = static_cast<float>((dz * -Dz + dy * -Dy) + -Dx * dx); // -(d.D)
    const float e1D = static_cast<float>((e1z * Dz + e1y * Dy) + e1x * Dx);
    const float e2D = static_cast<float>((Dz * e2z + e2y * Dy) + e2x * Dx);

    // The triangle's normal (unnormalised), to tell a segment (nearly) parallel to it.
    const double nx = product(e2y, e1z) - product(e1y, e2z);
    const double ny = product(e1x, e2z) - product(e1z, e2x);
    const double nz = product(e1y, e2x) - product(e2y, e1x);
    const double dn = (dz * nz + dy * ny) + nx * dx;
    const bool parallel = !((((nx * nx + nz * nz) + ny * ny) * dd) * kParallelSegment <= dn * dn);

    float along = 0.0f, s = 0.0f, t = 0.0f;
    auto finish = [&](double result) {
        if (alongOut)
            *alongOut = along;
        if (sOut)
            *sOut = s;
        if (tOut)
            *tOut = t;
        return result;
    };
    // The segment against one triangle edge, and against the triangle from one of its ends.
    auto edge = [&](const Mth::Vector& a, const Mth::Vector& b, float* segmentT, float* edgeT) {
        return segment.DistanceSqrToLine(Mth::Line(a, b), segmentT, edgeT);
    };
    auto fromPoint = [&](const Mth::Vector& point, float* pointS, float* pointT) {
        return PointTriangleDistanceSquared(point, v0, v1, v2, pointS, pointT);
    };

    float best = 0.0f;
    // A later edge, compared unrounded with the best so far: v0 v1 (s along it), v0 v2 (t) or v1 v2 (w: s = 1 - w,
    // t = w).
    auto tryEdge01 = [&] {
        float segmentT, edgeT;
        double distance = edge(v0, v1, &segmentT, &edgeT);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segmentT;
            s = edgeT;
            t = 0.0f;
        }
    };
    auto tryEdge02 = [&] {
        float segmentT, edgeT;
        double distance = edge(v0, v2, &segmentT, &edgeT);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segmentT;
            s = 0.0f;
            t = edgeT;
        }
    };
    auto tryEdge12 = [&] {
        float segmentT, edgeT;
        double distance = edge(v1, v2, &segmentT, &edgeT);
        float rest = static_cast<float>(1.0 - edgeT);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = segmentT;
            s = rest;
            t = edgeT;
        }
    };
    // Last, one of the segment's ends (rounded before it's compared), and the result.
    auto finishWithEnd = [&](bool atEnd) {
        Mth::Vector point = atEnd ? segment.end : segment.start;
        float pointS, pointT;
        float distance = static_cast<float>(fromPoint(point, &pointS, &pointT));
        if (distance < best) {
            along = atEnd ? 1.0f : 0.0f;
            s = pointS;
            t = pointT;
            return finish(distance);
        }
        return finish(best);
    };

    if (parallel) {
        best = static_cast<float>(edge(v0, v1, &along, &s));
        t = 0.0f;
        tryEdge02();
        tryEdge12();
        float pointS, pointT;
        double distance = fromPoint(start, &pointS, &pointT);
        if (distance < best) {
            best = static_cast<float>(distance);
            along = 0.0f;
            s = pointS;
            t = pointT;
        }
        return finishWithEnd(true);
    }

    // Where the segment's line crosses the triangle's plane.
    const double cofactor11 = static_cast<double>(e2e2) * e1e1 - static_cast<double>(e1e2) * e1e2;
    const float cofactor12 = static_cast<float>(product(e1e2, de2) - product(e2e2, de1));
    const float cofactor13 = static_cast<float>(product(e1e2, de1) - product(e1e1, de2));
    const float cofactor23 = static_cast<float>(product(de2, de1) - product(e1e2, dd));
    const double inverse = 1.0 / ((product(cofactor13, de2) + product(cofactor12, de1)) + cofactor11 * dd);
    const float rhsD = static_cast<float>(-(dD * inverse));
    const float rhs1 = static_cast<float>(-(e1D * inverse));
    const double rhs2Exact = -(inverse * e2D);
    const float rhs2 = static_cast<float>(rhs2Exact);
    along = static_cast<float>((rhs2Exact * cofactor13 + product(rhs1, cofactor12)) + rhsD * cofactor11);
    const double sExact = ((product(e2e2, dd) - product(de2, de2)) * rhs1 + product(rhs2, cofactor23)) +
                          product(rhsD, cofactor12);
    s = static_cast<float>(sExact);
    t = static_cast<float>(((product(e1e1, dd) - product(de1, de1)) * rhs2 + product(rhs1, cofactor23)) +
                           product(rhsD, cofactor13));

    const bool beyondEdge12 = !(t + sExact <= 1.0);
    const bool belowEdge02 = sExact < 0.0; // (s < 0: on v0 v2's far side)
    const bool belowEdge01 = t < 0.0f;
    if (along < 0.0f || !(along <= 1.0f)) {
        // The crossing is off the segment: the nearer of the nearest edges and the segment's end.
        const bool atEnd = !(along < 0.0f);
        if (!beyondEdge12) {
            if (belowEdge02) {
                best = static_cast<float>(edge(v0, v2, &along, &t));
                s = 0.0f;
                if (belowEdge01)
                    tryEdge01();
            } else if (belowEdge01) {
                best = static_cast<float>(edge(v0, v1, &along, &s));
                t = 0.0f;
            } else {
                double distance = fromPoint(atEnd ? segment.end : start, &s, &t);
                along = atEnd ? 1.0f : 0.0f;
                return finish(distance);
            }
        } else if (belowEdge02) {
            best = static_cast<float>(edge(v0, v2, &along, &t));
            s = 0.0f;
            tryEdge12();
        } else if (belowEdge01) {
            best = static_cast<float>(edge(v0, v1, &along, &s));
            t = 0.0f;
            tryEdge12();
        } else {
            best = static_cast<float>(edge(v1, v2, &along, &t));
            s = static_cast<float>(1.0 - t);
        }
        return finishWithEnd(atEnd);
    }

    // The crossing is on the segment: the nearest edges (rounded before they're compared).
    if (!beyondEdge12) {
        if (belowEdge02) {
            if (!belowEdge01) {
                double distance = edge(v0, v2, &along, &t);
                s = 0.0f;
                return finish(distance);
            }
            best = static_cast<float>(edge(v0, v2, &along, &t));
            s = 0.0f;
            float segmentT, edgeT;
            float distance = static_cast<float>(edge(v0, v1, &segmentT, &edgeT));
            if (distance < best) {
                along = segmentT;
                s = edgeT;
                t = 0.0f;
                return finish(distance);
            }
            return finish(best);
        }
        if (belowEdge01) {
            double distance = edge(v0, v1, &along, &s);
            t = 0.0f;
            return finish(distance);
        }
        Dbg::Printf("uh oh...\n");
        return finish(0.0f);
    }
    if (!belowEdge02 && !belowEdge01) {
        double distance = edge(v1, v2, &along, &t);
        s = static_cast<float>(1.0 - t);
        return finish(distance);
    }
    if (belowEdge02) {
        best = static_cast<float>(edge(v0, v2, &along, &t));
        s = 0.0f;
    } else {
        best = static_cast<float>(edge(v0, v1, &along, &s));
        t = 0.0f;
    }
    float segmentT, edgeT;
    float distance = static_cast<float>(edge(v1, v2, &segmentT, &edgeT));
    float rest = static_cast<float>(1.0 - edgeT);
    if (distance < best) {
        along = segmentT;
        s = rest;
        t = edgeT;
        return finish(distance);
    }
    return finish(best);
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

// 0x0040D370 (P's side tests use its line values unrounded, Q's rounded to floats; each pair of sides' direction
// test the first time unrounded, the second time rounded - as the original's x87 code keeps them)
bool RectHitsTriangle(const CollRect& rect, const Mth::Vector& v0, const Mth::Vector& v1, const Mth::Vector& v2,
                      Mth::Vector* points, int* kinds)
{
    const Mth::Vector& o = rect.origin;
    const Mth::Vector& a = rect.sideA;
    const Mth::Vector& b = rect.sideB;
    // the rect's plane: n = a x b, and the triangle's corners' distances from it
    float nx = static_cast<float>(static_cast<double>(b.z) * a.y - static_cast<double>(b.y) * a.z);
    float ny = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(b.z) * a.x);
    float nz = static_cast<float>(static_cast<double>(b.y) * a.x - static_cast<double>(a.y) * b.x);
    double d = -((static_cast<double>(ny) * o.y + static_cast<double>(nz) * o.z) + static_cast<double>(nx) * o.x);
    float d0 = static_cast<float>(
        ((static_cast<double>(nz) * v0.z + static_cast<double>(ny) * v0.y) + static_cast<double>(nx) * v0.x) + d);
    float d1 = static_cast<float>(
        ((static_cast<double>(nx) * v1.x + static_cast<double>(ny) * v1.y) + static_cast<double>(nz) * v1.z) + d);
    float d2 = static_cast<float>(
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
    Mth::Vector p, q;
    int pKind, qKind;
    if ((d0 <= 0.0f && d1 >= 0.0f) || (d0 >= 0.0f && d1 <= 0.0f)) {
        pKind = 1;
        p = EdgeCrossing(v0, v1, d0, d1);
        if ((d1 <= 0.0f && d2 >= 0.0f) || (d1 >= 0.0f && d2 <= 0.0f)) {
            qKind = 2;
            q = EdgeCrossingScaled(v1, v2, d1, d2);
        } else {
            qKind = 3;
            q = EdgeCrossingScaled(v2, v0, d2, d0);
        }
    } else {
        pKind = 2;
        p = EdgeCrossing(v1, v2, d1, d2);
        qKind = 3;
        q = EdgeCrossing(v0, v2, d0, d2);
    }

    // in the plane the normal is largest across
    float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    int i = 1, j = 2;
    if (ax < az && ay < az) {
        i = 0;
        j = 1;
    } else if (ax < ay && az < ay) {
        i = 0;
        j = 2;
    }
    float o1 = Component(o, i), o2 = Component(o, j);
    float a1 = Component(a, i), a2 = Component(a, j);
    float b1 = Component(b, i), b2 = Component(b, j);
    float p1 = Component(p, i), p2 = Component(p, j);
    float q1 = Component(q, i), q2 = Component(q, j);

    // each end against the sides' lines: side 0 (o along a), side 1 (o + b along a) ...
    float negA1 = -a1;
    float c0 = static_cast<float>(static_cast<double>(a1) * o2 - static_cast<double>(a2) * o1);
    double pAlongA = static_cast<double>(negA1) * p2 + static_cast<double>(a2) * p1;
    double qAlongA = static_cast<double>(negA1) * q2 + static_cast<double>(q1) * a2;
    float qAlongAf = static_cast<float>(qAlongA);
    double bSide = static_cast<double>(negA1) * b2 + static_cast<double>(b1) * a2;
    float bSidef = static_cast<float>(bSide);
    bool pIn0 = Inside(bSide > 0.0, c0 + pAlongA);
    bool qIn0 = Inside(bSide > 0.0, static_cast<float>(qAlongA + c0));
    if (!pIn0 && !qIn0)
        return false;
    float far2 = b2 + o2, far1 = b1 + o1;
    float c1 = static_cast<float>(static_cast<double>(far2) * a1 - static_cast<double>(far1) * a2);
    bool pIn1 = bSidef < 0.0f ? pAlongA + c1 >= 0.0 : pAlongA + c1 <= 0.0;
    float qEnd1 = qAlongAf + c1;
    bool qIn1 = bSidef < 0.0f ? qEnd1 >= 0.0f : qEnd1 <= 0.0f;
    if (!pIn1 && !qIn1)
        return false;
    // ... side 2 (o along b), side 3 (o + a along b)
    float negB1 = -b1;
    float c2 = static_cast<float>(static_cast<double>(b1) * o2 - static_cast<double>(b2) * o1);
    double pAlongB = static_cast<double>(negB1) * p2 + static_cast<double>(p1) * b2;
    double qAlongB = static_cast<double>(negB1) * q2 + static_cast<double>(q1) * b2;
    float qAlongBf = static_cast<float>(qAlongB);
    double aSide = static_cast<double>(negB1) * a2 + static_cast<double>(b2) * a1;
    float aSidef = static_cast<float>(aSide);
    bool pIn2 = Inside(aSide > 0.0, c2 + pAlongB);
    bool qIn2 = Inside(aSide > 0.0, static_cast<float>(qAlongB + c2));
    if (!pIn2 && !qIn2)
        return false;
    float far4 = a2 + o2, far3 = a1 + o1;
    float c3 = static_cast<float>(static_cast<double>(far4) * b1 - static_cast<double>(far3) * b2);
    bool pIn3 = aSidef < 0.0f ? pAlongB + c3 >= 0.0 : pAlongB + c3 <= 0.0;
    float qEnd3 = qAlongBf + c3;
    bool qIn3 = aSidef < 0.0f ? qEnd3 >= 0.0f : qEnd3 <= 0.0f;
    if (!pIn3 && !qIn3)
        return false;

    // the ends inside the rect, then the sides crossing P Q
    int found = 0;
    bool pInside = pIn0 && pIn1 && pIn2 && pIn3;
    bool qInside = qIn0 && qIn1 && qIn2 && qIn3;
    if (pInside) {
        points[0] = p;
        kinds[0] = pKind;
        if (qInside) {
            points[1] = q;
            kinds[1] = qKind;
            return true;
        }
        found = 1;
    } else if (qInside) {
        points[0] = q;
        kinds[0] = qKind;
        found = 1;
    }
    float dq1 = q1 - p1, dq2 = q2 - p2;
    float s;
    auto take = [&](const Mth::Vector& point) { // (true: that was the second)
        points[found] = point;
        kinds[found] = 0;
        bool second = found == 1;
        found = 1;
        return second;
    };
    if (SegmentsCross2D(o1, o2, a1, a2, p1, p2, dq1, dq2, &s)) {
        Mth::Vector point{s * a.x, s * a.y, s * a.z, s * a.w};
        point = {o.x + point.x, point.y + o.y, point.z + o.z, o.w + point.w};
        if (take(point))
            return true;
    }
    if (SegmentsCross2D(far1, far2, a1, a2, p1, p2, dq1, dq2, &s)) {
        Mth::Vector point{s * a.x, s * a.y, s * a.z, s * a.w};
        point = {o.x + point.x, point.y + o.y, point.z + o.z, o.w + point.w};
        point = {b.x + point.x, b.y + point.y, b.z + point.z, b.w + point.w};
        if (take(point))
            return true;
    }
    if (SegmentsCross2D(o1, o2, b1, b2, p1, p2, dq1, dq2, &s)) {
        Mth::Vector point = b;
        point *= s;
        point += o;
        if (take(point))
            return true;
    }
    // (the last side only counts as the second point)
    if (!SegmentsCross2D(far3, far4, b1, b2, p1, p2, dq1, dq2, &s) || found != 1)
        return false;
    Mth::Vector point = b;
    point *= s;
    point += o;
    point += a;
    points[1] = point;
    kinds[1] = 0;
    return true;
}

} // namespace Nx

REPLACE(0x0040B420, Nx::RayHitsTriangle);
REPLACE(0x0040B860, Nx::PointTriangleDistanceSquared);
REPLACE(0x0040BDC0, Nx::SegmentTriangleDistanceSquared);
REPLACE(0x0040D370, Nx::RectHitsTriangle);
REPLACE_UNCHECKED(0x0040B610, Nx::LineHitsSphereKeepingEdx);
REPLACE_UNCHECKED(0x0040B750, Nx::SegmentsCross2DKeepingEdx);
