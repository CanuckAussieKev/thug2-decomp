// Self-test for the BEAVER VALLEY tree trunk the user got stuck in (docs/pending/pole-tunnel.md): THUG2's ray /
// triangle test 0x0040B420 (Nx::RayHitsTriangle) is one-sided - a face is hit only from the side its winding
// (v1 - v0) x (v2 - v0) faces - and the skater's air code (DoAirPhysics 0x0050CA00) puts him at the hit point plus
// that normal, then pushes him Skater_Min_Distance_To_Wall (8) along it. The trunk, as the THP8 converter made it
// (object 895 Z_Center_g_OG_grounds_plantergrind_03, faces 0-27: an 8-sided prism of radius 8.7 round
// (-2714.75, 12455.3), y 104.4 - 322.9), had its sides wound inwards. Original and ours, on the event's approach:
// - wound inwards: the skater's feeler from outside to inside the trunk hits nothing (the near side faces away),
//   and from inside every feeler hits a side from its front with the normal pointing inwards - so the push-out
//   puts him back inside, near the axis (where the event found him);
// - wound outwards (the converter's fix): the near side is hit and the push-out leaves him outside; from inside
//   nothing is hit.

#include <cmath>
#include <cstdint>
#include <vector>

#include "game/mth/vector.h"
#include "game/nx/coll_tests.h"
#include "hook/appmain_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using AppMainTests::Code;
using Mth::Vector;

constexpr float kCentreX = -2714.75f, kCentreZ = 12455.3f, kBottom = 104.4f, kTop = 322.9f;
constexpr float kPushOut = 1.0f + 8.0f; // the hit's normal, then Skater_Min_Distance_To_Wall (skater_physics)

struct Triangle {
    Vector v0, v1, v2;
};

// The trunk's 16 side triangles, as the converted file has them (inwards: true) or turned round.
std::vector<Triangle> TrunkSides(bool inwards)
{
    static const float ring[8][2] = {{-2714.7f, 12446.6f}, {-2720.8f, 12449.2f}, {-2723.4f, 12455.3f},
                                     {-2720.8f, 12461.4f}, {-2714.7f, 12464.0f}, {-2708.6f, 12461.4f},
                                     {-2706.1f, 12455.3f}, {-2708.6f, 12449.2f}};
    std::vector<Triangle> out;
    for (int i = 0; i < 8; ++i) {
        int j = (i + 1) % 8;
        Vector bi{ring[i][0], kBottom, ring[i][1], 1.0f}, ti{ring[i][0], kTop, ring[i][1], 1.0f};
        Vector bj{ring[j][0], kBottom, ring[j][1], 1.0f}, tj{ring[j][0], kTop, ring[j][1], 1.0f};
        if (inwards) {
            out.push_back({bi, ti, tj}); // (faces 0, 2, 6, 10, ... of object 895)
            out.push_back({bi, tj, bj});
        } else {
            out.push_back({bi, tj, ti});
            out.push_back({bi, bj, tj});
        }
    }
    return out;
}

struct Hit {
    bool hit;
    float t;
    Vector point, normal;
};

// The nearest hit of the line start -> end over the triangles, by 0x0040B420 (original or ours), with the normal and
// point as CCollStatic::CollideWithLine makes them.
Hit Nearest(bool original, const std::vector<Triangle>& triangles, const Vector& start, const Vector& end)
{
    using RayFn = bool(__cdecl*)(const Vector*, const Vector*, const Vector*, const Vector*, const Vector*, float*);
    auto ray = reinterpret_cast<RayFn>(Code(original, 0x0040B420));
    Vector direction{end.x - start.x, end.y - start.y, end.z - start.z, 0.0f};
    Hit best{false, 2.0f, {}, {}};
    for (const Triangle& f : triangles) {
        float t = 0.0f;
        if (!ray(&start, &direction, &f.v0, &f.v1, &f.v2, &t) || !(t < best.t))
            continue;
        double ax = f.v1.x - f.v0.x, ay = f.v1.y - f.v0.y, az = f.v1.z - f.v0.z;
        double bx = f.v2.x - f.v0.x, by = f.v2.y - f.v0.y, bz = f.v2.z - f.v0.z;
        double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        best = {true, t,
                {start.x + direction.x * t, start.y + direction.y * t, start.z + direction.z * t, 1.0f},
                {static_cast<float>(nx / length), static_cast<float>(ny / length), static_cast<float>(nz / length), 0.0f}};
    }
    return best;
}

double FromAxis(float x, float z) { return std::hypot(static_cast<double>(x) - kCentreX, static_cast<double>(z) - kCentreZ); }

void TestPoleTunnelOneSided()
{
    // The event's approach (every 10 frames: (-2790, 300) -> (-2712, 278, 12456), in the air): along (0.555, 0.832)
    // at ~14.3 inches a frame, passing 2.8 inches from the axis. One frame's feet feeler from 20 inches before the
    // axis (outside) to 5.7 before it (inside); the forward feeler (8.1 up) reaches 10 further, still short of the far
    // side. Then a frame later from inside to 12 past the axis, through the far side.
    const float dx = 0.555f, dz = 0.832f, side = 2.8f, y = 280.0f;
    auto along = [&](float s, float up) {
        return Vector{kCentreX + dx * s - dz * side, y + up, kCentreZ + dz * s + dx * side, 1.0f};
    };
    struct Line {
        Vector start, end;
        const char* what;
    };
    const Line lines[] = {
        {along(-20.0f, 0.0f), along(-5.7f, 0.0f), "feet feeler, outside -> inside"},
        {along(-20.0f, 8.1f), along(4.3f, 8.1f), "forward feeler (8.1 up, 10 further)"},
        {along(-5.7f, 0.0f), along(12.0f, 0.0f), "feet feeler, inside -> past the far side"},
    };
    bool ok = true;
    for (bool inwards : {true, false}) {
        std::vector<Triangle> sides = TrunkSides(inwards);
        for (const Line& line : lines) {
            Hit o = Nearest(true, sides, line.start, line.end);
            Hit u = Nearest(false, sides, line.start, line.end);
            bool same = o.hit == u.hit && (!o.hit || (o.t == u.t && o.normal.x == u.normal.x && o.normal.z == u.normal.z));
            float px = o.point.x + o.normal.x * kPushOut, pz = o.point.z + o.normal.z * kPushOut;
            Log("pole tunnel (%s): %s: %s%s", inwards ? "sides wound inwards, as converted" : "sides wound outwards",
                line.what, o.hit ? "hit" : "no hit", same ? "" : " - OURS DIFFERS");
            if (o.hit)
                Log("  at (%.1f, %.1f) %.1f from the axis, normal (%.2f, %.2f) -> pushed out to (%.1f, %.1f), %.1f from "
                    "the axis (radius 8.7)",
                    o.point.x, o.point.z, FromAxis(o.point.x, o.point.z), o.normal.x, o.normal.z, px, pz,
                    FromAxis(px, pz));
            ok = ok && same;
            // What each winding does, as the event showed it.
            if (inwards && &line == &lines[0])
                ok = ok && !o.hit; // (in through the near side)
            if (inwards && &line == &lines[1])
                ok = ok && !o.hit;
            if (inwards && &line == &lines[2])
                ok = ok && o.hit && FromAxis(px, pz) < 8.0; // (put back inside, near the axis)
            if (!inwards && &line == &lines[0])
                ok = ok && o.hit && FromAxis(px, pz) > 8.7; // (kept outside)
            if (!inwards && &line == &lines[2])
                ok = ok && !o.hit;
        }
    }
    Check(ok, "pole tunnel: one-sided ray tests on the BEAVER VALLEY trunk (original and ours; inwards traps, outwards holds)");
}

} // namespace

SELFTEST(Early, TestPoleTunnelOneSided);
