// Self-tests for AN-20: the capsule queries (src/game/nx/coll_capsule.cpp) and the segment / triangle distance
// (src/game/nx/coll_tests.cpp) - the original and ours on the same random segments, capsules and triangles and a
// mesh's sector; results and outputs compared bit for bit.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/nx/coll_capsule.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Vector;

uint32_t CpBits(float f)
{
    if (f != f)
        return 0x7FC00000u;
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
void CpDouble(std::vector<uint32_t>& trace, double d)
{
    if (d != d) {
        trace.push_back(0x7FF80000u);
        trace.push_back(0u);
        return;
    }
    uint32_t b[2];
    std::memcpy(b, &d, 8);
    trace.push_back(b[0]);
    trace.push_back(b[1]);
}
float CpSpread(Lcg& random, float range)
{
    return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range;
}
Vector CpVector(Lcg& random, float range, float w = 1.0f)
{
    return {CpSpread(random, range), CpSpread(random, range), CpSpread(random, range), w};
}
void CpRecord(std::vector<uint32_t>& trace, const Vector& v)
{
    trace.push_back(CpBits(v.x));
    trace.push_back(CpBits(v.y));
    trace.push_back(CpBits(v.z));
    trace.push_back(CpBits(v.w));
}
// A point of the triangle's plane at v0 + a e1 + b e2, lifted `up` along (a multiple of) its normal.
Vector CpOnPlane(const Vector* v, float a, float b, float up)
{
    float e1x = v[1].x - v[0].x, e1y = v[1].y - v[0].y, e1z = v[1].z - v[0].z;
    float e2x = v[2].x - v[0].x, e2y = v[2].y - v[0].y, e2z = v[2].z - v[0].z;
    Vector n{e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x, 0.0f};
    float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    float scale = length > 0.0f ? up / length : 0.0f;
    return {v[0].x + a * e1x + b * e2x + n.x * scale, v[0].y + a * e1y + b * e2y + n.y * scale,
            v[0].z + a * e1z + b * e2z + n.z * scale, 1.0f};
}

// Our Line::DistanceSqrToLine (an earlier batch's) rounds in floats where the original keeps doubles: both runs use
// the original's, so these tests compare this batch's code.
void CpUseOriginalLineDistance(StandIns& s)
{
    uint32_t ours = AddressOf(&Mth::Line::DistanceSqrToLine);
    s.Bytes(ours, Branch(0xE9, ours, OriginalFunction(0x00402640)));
}

// Segments against triangles in every region (ends below, inside and above the plane's crossing; beyond each edge
// and corner; crossing the interior; parallel to the face; zero length; a triangle with no area; NaN), and capsules
// against triangles (in front, behind, within the radius or not).
void TestAnimCollCapsuleTests()
{
    using SegTriFn = double(__cdecl*)(const Mth::Line*, const Vector*, const Vector*, const Vector*, const Vector*,
                                      float*, float*, float*);
    using CapTriFn = bool(__cdecl*)(const Mth::Capsule*, const Vector*, const Vector*, const Vector*, Vector*, Vector*,
                                    Vector*);
    StandIns& s = *new StandIns();
    CpUseOriginalLineDistance(s);
    int hits = 0, crossing = 0;
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto segTri = reinterpret_cast<SegTriFn>(Code(original, 0x0040BDC0));
        auto capTri = reinterpret_cast<CapTriFn>(Code(original, 0x0040FC00));
        Lcg random(0x40BDC0);
        auto one = [&](const Mth::Line& line, const Vector* v, float radius) {
            float sv = -777.0f, tv = -777.0f, along = -777.0f;
            Vector normal{0.25f, -0.5f, 0.75f, 0.0f};
            double d = segTri(&line, &normal, &v[0], &v[1], &v[2], &sv, &tv, &along);
            CpDouble(trace, d);
            trace.push_back(CpBits(sv));
            trace.push_back(CpBits(tv));
            trace.push_back(CpBits(along));
            crossing += original && d == 0.0;
            // (null outputs)
            CpDouble(trace, segTri(&line, &normal, &v[0], &v[1], &v[2], nullptr, nullptr, nullptr));
            Mth::Capsule capsule(line, radius);
            Vector point{-777, -777, -777, -777}, n{-5, -5, -5, -5}, offset{-9, -9, -9, -9};
            bool result = capTri(&capsule, &v[0], &v[1], &v[2], &point, &n, &offset);
            trace.push_back(result);
            CpRecord(trace, point);
            CpRecord(trace, n);
            CpRecord(trace, offset);
            hits += original && result;
        };
        // exact cases: a flat triangle; segments crossing it, parallel to it, ending on it, along an edge, through
        // a corner, of no length
        {
            const Vector flat[3] = {{0, 0, 0, 0}, {8, 0, 0, 0}, {0, 8, 0, 0}};
            const Vector ends[][2] = {
                {{2, 2, 3, 1}, {2, 2, -3, 1}},  {{2, 2, 3, 1}, {2, 2, 0, 1}},    {{1, 1, 2, 1}, {9, 1, 2, 1}},
                {{-2, -2, 1, 1}, {10, -2, 1, 1}}, {{0, 0, 5, 1}, {0, 0, -5, 1}},  {{8, 0, 1, 1}, {8, 0, -1, 1}},
                {{-1, 0, 0, 1}, {9, 0, 0, 1}},  {{4, 4, 2, 1}, {4, 4, 2, 1}},    {{2, 2, 0, 1}, {2, 2, 0, 1}},
                {{6, 6, 3, 1}, {6, 6, -3, 1}},  {{-3, -3, 3, 1}, {-3, -3, -3, 1}}, {{10, -1, -2, 1}, {12, -1, -4, 1}},
                {{-3, 4, 1, 1}, {-3, 4, -1, 1}}, {{4, -3, 1, 1}, {4, -3, -1, 1}}, {{-2, 12, 2, 1}, {-2, 12, 5, 1}},
                {{3, 0, 2, 1}, {3, 0, -2, 1}},  {{0, 3, 2, 1}, {0, 3, -2, 1}},  {{4, 4, 2, 1}, {4, 4, -2, 1}},
                {{3, 0, 4, 1}, {3, 0, 2, 1}},   {{0, 3, -4, 1}, {0, 3, -2, 1}}, {{4, 4, 6, 1}, {4, 4, 2, 1}}};
            for (const auto& e : ends) {
                for (float radius : {1.5f, 2.0f, 3.0f}) {
                    Mth::Line line(e[0], e[1]);
                    one(line, flat, radius);
                    Mth::Line back(e[1], e[0]);
                    one(back, flat, radius);
                }
            }
            const Vector line3[3] = {{0, 0, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0}}; // (no area)
            one(Mth::Line({1, 1, 1, 1}, {1, -1, -1, 1}), line3, 2.0f);
        }
        for (int n = 0; n < 8000; ++n) {
            Vector v[3];
            float size = n % 3 == 0 ? 40.0f : 6.0f;
            for (Vector& corner : v)
                corner = CpVector(random, size, n % 11 == 0 ? CpSpread(random, 2.0f) : 0.0f);
            if (n % 41 == 0)
                v[2] = v[1];
            // the ends: at the triangle's parameters (a, b) - below 0, inside, beyond the far edge - above or below
            // the plane
            auto pick = [&] {
                switch (random.Below(4)) {
                case 0: return CpSpread(random, 0.5f) - 0.6f;
                case 1: return CpSpread(random, 0.3f) + 0.3f;
                case 2: return CpSpread(random, 0.5f) + 1.2f;
                default: return CpSpread(random, 2.0f);
                }
            };
            float a0 = pick(), b0 = pick(), a1 = pick(), b1 = pick();
            float up0 = CpSpread(random, size), up1 = CpSpread(random, size);
            switch (n % 6) {
            case 0: up1 = up0; break;                        // parallel
            case 1: up1 = -up0 * CpSpread(random, 2.0f); break; // crossing the plane (or not)
            case 2: up1 = up0 * (1.0f + CpSpread(random, 0.001f)); break; // nearly parallel
            default: break;
            }
            Mth::Line line(CpOnPlane(v, a0, b0, up0), CpOnPlane(v, a1, b1, up1));
            line.start.w = CpSpread(random, 2.0f);
            line.end.w = CpSpread(random, 2.0f);
            if (n % 29 == 0)
                line.end = line.start;
            if (n % 97 == 0)
                line.start.y = std::numeric_limits<float>::quiet_NaN();
            one(line, v, size * (0.02f + static_cast<float>(random.Below(100)) / 200.0f));
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    Log("capsule tests: %d capsule hits, %d segments through a face", hits, crossing);
    CompareTraces(expected, actual, "Nx segment / triangle distance and capsule / triangle");
}
SELFTEST(Early, TestAnimCollCapsuleTests, TestStandIns::RemoveStandIns);

// --- slot 17: a sector's faces against capsules ---

constexpr int kCqVertices = 40;
constexpr int kCqFaces = 60;

struct CapsuleArena {
    alignas(16) uint8_t sector[0x18];
    Nx::CCollObjTriData geometry[2];
    float plain[kCqVertices][3];
    uint16_t packed[kCqVertices][3];
    uint8_t faces10[kCqFaces * 10];
    uint8_t faces8[kCqFaces * 8];
    Nx::CapsuleHits hits;
};
CapsuleArena* g_ca;

uint32_t CaWhere(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_ca);
    return word >= base && word < base + sizeof(CapsuleArena) ? 0xA0000000u + (word - base) : word;
}

// A sector's faces against capsules (plain and compressed geometry, flags ignored / required, the list nearly full
// and full, modes 0 / 1 / 2, with and without a sweep).
void TestAnimCollCapsuleQueries()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x0067C6B8, 0x1000}});
    StandIns& s = *new StandIns();
    CpUseOriginalLineDistance(s);
    g_ca = static_cast<CapsuleArena*>(VirtualAlloc(nullptr, sizeof(CapsuleArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    int found = 0;
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        std::memset(g_ca, 0xA7, sizeof(CapsuleArena));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        auto* table = reinterpret_cast<uint16_t*>(0x0067C6B8);
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x4114E0);
        for (int v = 0; v < kCqVertices; ++v) {
            for (int k = 0; k < 3; ++k) {
                g_ca->plain[v][k] = CpSpread(random, 40.0f);
                g_ca->packed[v][k] = static_cast<uint16_t>((g_ca->plain[v][k] + 50.0f) * 16.0f);
            }
        }
        for (int f = 0; f < kCqFaces; ++f) {
            auto* wide = reinterpret_cast<uint16_t*>(g_ca->faces10 + f * 10);
            uint8_t* small = g_ca->faces8 + f * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            std::memcpy(small, wide, 4);
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(random.Below(kCqVertices));
                wide[2 + k] = corner;
                small[4 + k] = static_cast<uint8_t>(corner);
            }
            small[7] = 0;
        }
        for (int g = 0; g < 2; ++g) {
            Nx::CCollObjTriData& geometry = g_ca->geometry[g];
            std::memset(&geometry, 0, sizeof(geometry));
            geometry.m_checksum = 0x6E0A0000u + g;
            geometry.m_numVertices = kCqVertices;
            geometry.m_numFaces = kCqFaces;
            geometry.m_smallFaces = g == 1;
            geometry.m_compressed = g == 1;
            geometry.m_faces = g == 1 ? g_ca->faces8 : g_ca->faces10;
            geometry.m_min = {-50, -50, -50, 1};
            geometry.m_max = {50, 50, 50, 1};
            geometry.m_vertices = g == 1 ? static_cast<void*>(g_ca->packed) : static_cast<void*>(g_ca->plain);
        }
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_ca->sector);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC7;
        auto slot = reinterpret_cast<bool(__thiscall*)(void*, const Mth::Capsule*, const Mth::BBox*, Nx::CapsuleHits*)>(
            Code(original, 0x004114E0));
        for (int g = 0; g < 2; ++g) {
            sector->m_geometry = &g_ca->geometry[g];
            for (int n = 0; n < 600; ++n) {
                Vector start = CpVector(random, 40.0f, CpSpread(random, 2.0f));
                Vector end{start.x + CpSpread(random, 20.0f), start.y + CpSpread(random, 20.0f),
                           start.z + CpSpread(random, 20.0f), CpSpread(random, 2.0f)};
                Mth::Capsule capsule(Mth::Line(start, end), static_cast<float>(random.Below(100)) / 10.0f + 0.5f);
                Mth::BBox box({-60, -60, -60, 1});
                box.AddPoint({60, 60, 60, 1});
                Nx::CapsuleHits& hits = g_ca->hits;
                std::memset(&hits, 0x6B, sizeof(hits));
                hits.count = n % 23 == 0 ? 32 : n % 17 == 0 ? 30 : 0;
                hits.ignoreFlags = n % 5 == 1 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hits.requireFlags = n % 5 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hits.mode = static_cast<int>(random.Below(3));
                hits.sweep = n % 4 == 3 ? 1.0f : n % 8 == 2 ? -0.0f : n % 8 == 5 ? -0.5f : 0.0f;
                bool result = slot(sector, &capsule, &box, &hits);
                found += original && hits.count > 0 && hits.count < 30;
                trace.push_back(result);
                const auto* words = reinterpret_cast<const uint32_t*>(&hits);
                for (size_t i = 0; i < sizeof(hits) / 4; ++i)
                    trace.push_back(CaWhere(words[i]));
            }
        }
        EndRunPatches();
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(g_ca, 0, MEM_RELEASE);
    delete &s;
    Log("capsule queries: %d with hits; %u trace words", found, static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx capsule queries (slot 17)");
}
SELFTEST(Early, TestAnimCollCapsuleQueries, TestStandIns::RemoveStandIns);

} // namespace
