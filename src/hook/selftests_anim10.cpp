// Self-tests for AN-11: the collision intersection tests (src/game/nx/coll_tests.cpp) - the original and ours on the
// same random rays, lines and segments (aimed so that hits, misses, grazes and degenerate cases all occur); the
// results and outputs compared bit for bit.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/mth/vector.h"
#include "game/nx/coll_tests.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Vector;

// A float's bits (any NaN as one: x87 and SSE code pass different NaNs on - their sign and payload don't matter).
uint32_t Bits(float f)
{
    if (f != f)
        return 0x7FC00000u;
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float Spread(Lcg& random, float range) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range; }

Vector RandomVector(Lcg& random, float range)
{
    return {Spread(random, range), Spread(random, range), Spread(random, range), Spread(random, range)};
}

// A value now and then replaced by something awkward: NaN, an infinity, zero, a huge or tiny one.
float Awkward(Lcg& random, float value)
{
    switch (random.Below(60)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return std::numeric_limits<float>::infinity();
    case 2:
        return 0.0f;
    case 3:
        return value * 1e18f;
    case 4:
        return value * 1e-18f;
    default:
        return value;
    }
}

// Rays at triangles (from either side, near their edges and corners, along their planes, some too short), lines at
// spheres (through, grazing, missing, from inside, with limits either side of the crossing) and 2D segment pairs
// (crossing, touching, parallel, collinear, nearly parallel).
void TestAnimCollIntersections()
{
    using RayFn = bool(__cdecl*)(const Vector*, const Vector*, const Vector*, const Vector*, const Vector*, float*);
    using SphereFn = bool(__cdecl*)(const Vector*, const Vector*, const Nx::CollSphere*, const Vector*, float*, float);
    using SegmentFn = bool(__cdecl*)(float, float, float, float, float, float, float, float, float*);
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto ray = reinterpret_cast<RayFn>(Code(original, 0x0040B420));
        auto sphereTest = reinterpret_cast<SphereFn>(Code(original, 0x0040B610));
        auto segments = reinterpret_cast<SegmentFn>(Code(original, 0x0040B750));
        Lcg random(0x40B420);
        // exact cases on the triangle (0,0,0) (4,0,0) (0,4,0), facing +z: straight down from z = 5 by 5 (t = 1), by
        // 10 (t = 0.5), by 2.5 (too short); on an edge (u = 0); just past an edge inside the 1.00001 slack and
        // beyond it; from behind (t < 0 going away, the back face); from below
        {
            const Vector v0{0, 0, 0, 1}, v1{4, 0, 0, 1}, v2{0, 4, 0, 1};
            const Vector cases[][2] = {
                {{1, 1, 5, 1}, {0, 0, -5, 0}},        {{1, 1, 5, 1}, {0, 0, -10, 0}},
                {{1, 1, 5, 1}, {0, 0, -2.5f, 0}},     {{0, 1, 5, 1}, {0, 0, -5, 0}},
                {{4.00002f, 0, 5, 1}, {0, 0, -5, 0}}, {{4.0004f, 0, 5, 1}, {0, 0, -5, 0}},
                {{1, 1, -5, 1}, {0, 0, -5, 0}},       {{1, 1, -5, 1}, {0, 0, 5, 0}},
                {{1, 1, 5, 1}, {0, 0, 5, 0}},         {{3, 1, 5, 1}, {0, 0, -5, 0}},
                {{2, 2, 5, 1}, {0, 0, -5, 0}},        {{0, 0, 5, 1}, {0, 0, -5, 0}},
            };
            for (const auto& c : cases) {
                float t = -777.0f;
                bool hit = ray(&c[0], &c[1], &v0, &v1, &v2, &t);
                trace.push_back(hit);
                trace.push_back(Bits(t));
            }
        }
        for (int n = 0; n < 4000; ++n) {
            float scale = n % 3 == 0 ? 100.0f : n % 3 == 1 ? 4.0f : 0.25f;
            Vector v0 = RandomVector(random, scale), v1 = RandomVector(random, scale), v2 = RandomVector(random, scale);
            if (n % 17 == 0)
                v2 = v1; // degenerate
            // aim at a point of the triangle's plane, inside or just outside it
            float a = Spread(random, 0.7f) + 0.35f, b = Spread(random, 0.7f) + 0.35f;
            Vector target{v0.x + a * (v1.x - v0.x) + b * (v2.x - v0.x), v0.y + a * (v1.y - v0.y) + b * (v2.y - v0.y),
                          v0.z + a * (v1.z - v0.z) + b * (v2.z - v0.z), 1.0f};
            Vector start = RandomVector(random, scale * 3.0f);
            float reach = n % 5 == 0 ? 0.5f : n % 5 == 1 ? 1.0f : n % 5 == 2 ? -1.5f : 1.5f;
            Vector direction{(target.x - start.x) * reach, (target.y - start.y) * reach, (target.z - start.z) * reach,
                             Spread(random, 1.0f)};
            if (n % 23 == 0)
                direction = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z, 0.0f}; // along the plane
            direction.x = Awkward(random, direction.x);
            start.z = Awkward(random, start.z);
            float t = -777.0f;
            bool hit = ray(&start, &direction, &v0, &v1, &v2, &t);
            trace.push_back(hit);
            trace.push_back(Bits(t));
        }
        for (int n = 0; n < 4000; ++n) {
            float scale = n % 2 == 0 ? 50.0f : 2.0f;
            Nx::CollSphere sphere{RandomVector(random, scale), Spread(random, scale * 0.5f) + scale * 0.5f};
            Vector start = RandomVector(random, scale * 2.0f);
            Vector base = n % 3 == 0 ? start : RandomVector(random, scale * 2.0f);
            // aim near the centre: inside the radius, at it, or beyond
            float off = Spread(random, 2.0f);
            Vector end{sphere.centre.x + off * sphere.radius, sphere.centre.y + Spread(random, 1.0f) * sphere.radius,
                       sphere.centre.z, 1.0f};
            if (n % 4 == 0)
                end = {base.x * 2.0f - sphere.centre.x, base.y * 2.0f - sphere.centre.y, base.z, 1.0f};
            end.y = Awkward(random, end.y);
            sphere.radius = Awkward(random, sphere.radius);
            float limit = n % 7 == 0 ? std::numeric_limits<float>::infinity() : Spread(random, 2.0f);
            float t = -777.0f;
            bool hit = sphereTest(&start, &base, &sphere, &end, &t, limit);
            trace.push_back(hit);
            trace.push_back(Bits(t));
            // again with the limit at the crossing found, rounded to a float
            float again = -777.0f;
            trace.push_back(sphereTest(&start, &base, &sphere, &end, &again, t));
        }
        {
            // crossing, but so small the denominator (1e-12) counts as parallel: s = 0
            float s = -777.0f;
            trace.push_back(segments(0.0f, 0.5e-6f, 1e-6f, 0.0f, 0.5e-6f, 0.0f, 0.0f, 1e-6f, &s));
            trace.push_back(Bits(s));
        }
        for (int n = 0; n < 4000; ++n) {
            float scale = n % 2 == 0 ? 30.0f : 1.0f;
            float ax = Spread(random, scale), ay = Spread(random, scale);
            float adx = Spread(random, scale), ady = Spread(random, scale);
            float bx = Spread(random, scale), by = Spread(random, scale);
            float bdx = Spread(random, scale), bdy = Spread(random, scale);
            switch (n % 9) {
            case 0: // parallel
                bdx = adx * 0.5f;
                bdy = ady * 0.5f;
                break;
            case 1: // collinear and overlapping
                bx = ax + adx * 0.25f;
                by = ay + ady * 0.25f;
                bdx = adx;
                bdy = ady;
                break;
            case 2: // touching at an end
                bx = ax + adx;
                by = ay + ady;
                break;
            case 3: // nearly parallel
                bdx = adx * 1.0000001f;
                bdy = ady;
                break;
            case 4: // through the middle
                bx = ax + adx * 0.5f - bdx * 0.5f;
                by = ay + ady * 0.5f - bdy * 0.5f;
                break;
            default:
                break;
            }
            adx = Awkward(random, adx);
            by = Awkward(random, by);
            float s = -777.0f;
            bool cross = segments(ax, ay, adx, ady, bx, by, bdx, bdy, &s);
            trace.push_back(cross);
            trace.push_back(Bits(s));
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    Log("intersections: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx intersection tests (ray / triangle, line / sphere, 2D segments)");
}
SELFTEST(Early, TestAnimCollIntersections);

// The game-facing adapters of the line / sphere and 2D segment tests keep EDX (their callers rely on it).
void TestAnimCollIntersectionsKeepEdx()
{
    Vector p{1, 2, 3, 1}, q{4, 5, 6, 1};
    Nx::CollSphere sphere{{2, 3, 4, 1}, 5};
    float t = 0, s = 0;
    uint32_t kept = 0, kept2 = 0;
    void* line = reinterpret_cast<void*>(&Nx::LineHitsSphereKeepingEdx);
    void* segments = reinterpret_cast<void*>(&Nx::SegmentsCross2DKeepingEdx);
    __asm {
        push 0x3F800000
        lea eax, t
        push eax
        lea eax, q
        push eax
        lea eax, sphere
        push eax
        lea eax, p
        push eax
        push eax
        mov edx, 0x5A5A1234
        call line
        add esp, 0x18
        mov kept, edx
        lea eax, s
        push eax
        push 0x3F800000
        push 0
        push 0
        push 0
        push 0
        push 0x3F800000
        push 0
        push 0
        mov edx, 0x6B6B4321
        call segments
        add esp, 0x24
        mov kept2, edx
    }
    Check(kept == 0x5A5A1234u && kept2 == 0x6B6B4321u, "intersection adapters keep EDX");
}
SELFTEST(Early, TestAnimCollIntersectionsKeepEdx);

// The squared point / triangle distance: points over and beside random triangles in every region (the
// parameters spread over -1..2 on both edges, lifted off the plane), on corners and edges, with degenerate
// (collinear, repeated, zero) triangles, NaN / infinite / huge / tiny values, and with each output pointer left
// null; the distance (all 64 bits), s and t compared.
void TestAnimCollPointTriangle()
{
    using DistanceFn = double(__cdecl*)(const Vector*, const Vector*, const Vector*, const Vector*, float*, float*);
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto distance = reinterpret_cast<DistanceFn>(Code(original, 0x0040B860));
        Lcg random(0x40B860);
        int regions[9] = {};
        for (int n = 0; n < 12000; ++n) {
            float scale = n % 3 == 0 ? 60.0f : n % 3 == 1 ? 3.0f : 0.05f;
            Vector v0 = RandomVector(random, scale), v1 = RandomVector(random, scale), v2 = RandomVector(random, scale);
            switch (n % 41) {
            case 0:
                v2 = v1;
                break;
            case 1:
                v2 = {v0.x + 2.0f * (v1.x - v0.x), v0.y + 2.0f * (v1.y - v0.y), v0.z + 2.0f * (v1.z - v0.z), 1.0f};
                break;
            case 2:
                v1 = v0;
                v2 = v0;
                break;
            default:
                break;
            }
            float ps = Spread(random, 1.5f) + 0.5f, pt = Spread(random, 1.5f) + 0.5f;
            if (n % 7 == 0) { // on a corner or an edge
                ps = static_cast<float>(random.Below(3)) * 0.5f;
                pt = n % 2 ? 0.0f : 1.0f - ps;
            }
            float lift = n % 5 == 0 ? 0.0f : Spread(random, scale);
            Vector point{v0.x + ps * (v1.x - v0.x) + pt * (v2.x - v0.x) + lift * 0.3f,
                         v0.y + ps * (v1.y - v0.y) + pt * (v2.y - v0.y) - lift,
                         v0.z + ps * (v1.z - v0.z) + pt * (v2.z - v0.z) + lift * 0.7f, Spread(random, 1.0f)};
            point.x = Awkward(random, point.x);
            v1.y = Awkward(random, v1.y);
            float so = -777.0f, to = -777.0f;
            float* sp = n % 13 == 0 ? nullptr : &so;
            float* tp = n % 11 == 0 ? nullptr : &to;
            double result = distance(&point, &v0, &v1, &v2, sp, tp);
            uint64_t bits;
            std::memcpy(&bits, &result, 8);
            if (result != result)
                bits = 0x7FF8000000000000ull;
            trace.push_back(static_cast<uint32_t>(bits));
            trace.push_back(static_cast<uint32_t>(bits >> 32));
            trace.push_back(Bits(so));
            trace.push_back(Bits(to));
            // a rough region tally, from the outputs
            int region = so == 0.0f ? (to == 0.0f ? 0 : to == 1.0f ? 1 : 2) : to == 0.0f ? (so == 1.0f ? 3 : 4) : 5;
            ++regions[region];
        }
        // exact ties (integer triangles and points: d = 0, e + c = d + b, sums at 0 ...)
        const Vector triangles[][3] = {
            {{0, 0, 0, 1}, {2, 0, 0, 1}, {0, 2, 0, 1}},  {{0, 0, 0, 1}, {4, 0, 0, 1}, {1, 3, 0, 1}},
            {{1, 1, 0, 1}, {-2, 1, 0, 1}, {1, 4, 2, 1}}, {{0, 0, 0, 1}, {2, 2, 0, 1}, {-2, 2, 0, 1}},
            {{0, 0, 0, 1}, {-3, -3, 0, 1}, {-2, 3, 0, 1}}, // (1, -1, 0): d = 0 below both edges (-0 from a clamp)
        };
        for (const auto& tri : triangles) {
            for (int x = -3; x <= 5; ++x) {
                for (int y = -3; y <= 5; ++y) {
                    for (int z : {0, 1, -2}) {
                        Vector point{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f};
                        float so = -777.0f, to = -777.0f;
                        double result = distance(&point, &tri[0], &tri[1], &tri[2], &so, &to);
                        uint64_t bits;
                        std::memcpy(&bits, &result, 8);
                        trace.push_back(static_cast<uint32_t>(bits));
                        trace.push_back(static_cast<uint32_t>(bits >> 32));
                        trace.push_back(Bits(so));
                        trace.push_back(Bits(to));
                    }
                }
            }
        }
        if (original)
            Log("point / triangle: corner v0 %d, v2 %d, edge 1 %d, v1 %d, edge 0 %d, other %d", regions[0], regions[1],
                regions[2], regions[3], regions[4], regions[5]);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    CompareTraces(expected, actual, "Nx point / triangle squared distance");
}
SELFTEST(Early, TestAnimCollPointTriangle);

} // namespace
