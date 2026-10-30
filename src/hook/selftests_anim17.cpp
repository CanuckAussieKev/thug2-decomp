// Self-tests for AN-19: the sphere queries (src/game/nx/coll_sphere.cpp) - the original and ours on the same random
// spheres, moves and triangles, a mesh's sector, stand-in objects through a cache and grids, the sphere feeler;
// results and outputs compared bit for bit.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_sphere.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"
#include "game/gfx/engine_scene_list.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Vector;
using Nx::CollSphere;

uint32_t SpBits(float f)
{
    if (f != f)
        return 0x7FC00000u;
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float SpSpread(Lcg& random, float range)
{
    return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range;
}
Vector SpVector(Lcg& random, float range, float w = 1.0f)
{
    return {SpSpread(random, range), SpSpread(random, range), SpSpread(random, range), w};
}
void SpRecord(std::vector<uint32_t>& trace, const Vector& v)
{
    trace.push_back(SpBits(v.x));
    trace.push_back(SpBits(v.y));
    trace.push_back(SpBits(v.z));
    trace.push_back(SpBits(v.w));
}

// A triangle near the origin, a sphere placed against it in one of several ways, and a move towards (or past) it.
struct SpCase {
    Vector v[3];
    CollSphere sphere;
    Vector motion;
    float limit;
};
SpCase SpMake(Lcg& random, int n)
{
    SpCase c;
    float size = n % 3 == 0 ? 50.0f : 8.0f;
    for (Vector& v : c.v)
        v = SpVector(random, size, n % 11 == 0 ? SpSpread(random, 2.0f) : 0.0f);
    if (n % 37 == 0)
        c.v[2] = c.v[1]; // (no area)
    // a point of the triangle's plane, inside it or beyond an edge or a corner
    float a = SpSpread(random, 0.9f) + 0.33f, b = SpSpread(random, 0.9f) + 0.33f;
    Vector on{c.v[0].x + a * (c.v[1].x - c.v[0].x) + b * (c.v[2].x - c.v[0].x),
              c.v[0].y + a * (c.v[1].y - c.v[0].y) + b * (c.v[2].y - c.v[0].y),
              c.v[0].z + a * (c.v[1].z - c.v[0].z) + b * (c.v[2].z - c.v[0].z), 1.0f};
    if (n % 7 == 1)
        on = c.v[random.Below(3)]; // (at a corner)
    Vector normal{(c.v[1].y - c.v[0].y) * (c.v[2].z - c.v[0].z) - (c.v[1].z - c.v[0].z) * (c.v[2].y - c.v[0].y),
                  (c.v[1].z - c.v[0].z) * (c.v[2].x - c.v[0].x) - (c.v[1].x - c.v[0].x) * (c.v[2].z - c.v[0].z),
                  (c.v[1].x - c.v[0].x) * (c.v[2].y - c.v[0].y) - (c.v[1].y - c.v[0].y) * (c.v[2].x - c.v[0].x), 0.0f};
    normal.Normalize();
    float radius = size * (0.05f + static_cast<float>(random.Below(100)) / 400.0f);
    float away = radius * (SpSpread(random, 3.0f)); // (in front, behind, within the radius or not)
    c.sphere.centre = {on.x + normal.x * away, on.y + normal.y * away, on.z + normal.z * away, SpSpread(random, 2.0f)};
    c.sphere.radius = radius;
    float speed = size * (n % 5 == 0 ? 0.1f : 1.5f);
    c.motion = {-normal.x * away * 2.0f + SpSpread(random, speed), -normal.y * away * 2.0f + SpSpread(random, speed),
                -normal.z * away * 2.0f + SpSpread(random, speed), n % 9 == 0 ? 1.0f : SpSpread(random, 2.0f)};
    if (n % 13 == 0)
        c.motion = {0, 0, 0, 1}; // (not moving)
    c.limit = n % 4 == 0 ? 0.5f : n % 4 == 1 ? 1.0f : 2.0f;
    if (n % 97 == 0)
        c.sphere.centre.y = std::numeric_limits<float>::quiet_NaN();
    return c;
}

// Segments through spheres (crossing twice, once, grazing, missing, inside, zero length); moving spheres against
// still edges; corners and their edges; triangles (the face, each edge and corner, from in front and behind,
// already touching or not, moving or not); standing spheres against triangles.
void TestAnimCollSphereTests()
{
    using SegFn = bool(__cdecl*)(const Mth::Line*, const CollSphere*, int*, Vector*, int);
    using EdgeFn = bool(__cdecl*)(const Mth::Line*, const Vector*, const CollSphere*, const Vector*, float*, float,
                                  int*, Vector*);
    using CornerFn = bool(__cdecl*)(const Vector*, const CollSphere*, const Vector*, const Vector*, const Vector*,
                                    const Vector*, const Vector*, const Vector*, const Vector*, float*, float, int*,
                                    Vector*, Vector*);
    using TriFn = bool(__cdecl*)(const CollSphere*, const Vector*, const Vector*, const Vector*, float*, float,
                                 const Vector*, Vector*, Vector*);
    using StandFn = bool(__cdecl*)(const CollSphere*, const Vector*, const Vector*, const Vector*, Vector*, Vector*);
    int hits[5] = {};
    // (our capsule tests - Line::DistanceSqrToLine / DistanceSqrToPoint, an earlier batch's - round in floats where
    // the original keeps doubles: both runs use the original's, so these tests compare this batch's code)
    StandIns& s = *new StandIns();
    for (auto [ours, theirs] : {std::pair<uint32_t, uint32_t>{AddressOf(&Mth::Capsule::IntersectsSegment), 0x004032E0u},
                                {AddressOf(&Mth::Capsule::ContainsPoint), 0x00403330u}})
        s.Bytes(ours, Branch(0xE9, ours, OriginalFunction(theirs)));
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto seg = reinterpret_cast<SegFn>(Code(original, 0x0040E280));
        auto edge = reinterpret_cast<EdgeFn>(Code(original, 0x0040E560));
        auto corner = reinterpret_cast<CornerFn>(Code(original, 0x0040E940));
        auto tri = reinterpret_cast<TriFn>(Code(original, 0x0040EC90));
        auto stand = reinterpret_cast<StandFn>(Code(original, 0x0040F9E0));
        Lcg random(0x40EC90);
        const Vector zero{0, 0, 0, 1};
        // exact cases: segments starting / ending on the surface or grazing it; a corner on the surface; a zero
        // divisor; a flat triangle with spheres touching it exactly, reaching it exactly at the limit, and a
        // triangle with no area
        {
            CollSphere unit{{0, 0, 0, 1}, 2.0f};
            const Vector ends[][2] = {{{2, 0, 0, 1}, {6, 0, 0, 1}}, {{-6, 0, 0, 1}, {2, 0, 0, 1}},
                                      {{0, 2, 0, 1}, {4, 2, 0, 1}}, {{-6, 0, 0, 1}, {-2, 0, 0, 1}}};
            for (const auto& e : ends) {
                Mth::Line line;
                line.start = e[0];
                line.end = e[1];
                int count = -7;
                Vector points[2] = {{-777, -777, -777, -777}, {-777, -777, -777, -777}};
                trace.push_back(seg(&line, &unit, &count, points, 0));
                trace.push_back(static_cast<uint32_t>(count));
                SpRecord(trace, points[0]);
                SpRecord(trace, points[1]);
            }
            Vector flat[3] = {{0, 0, 0, 0}, {8, 0, 0, 0}, {0, 8, 0, 0}};
            CollSphere onSurface{{3, 0, 0, 1}, 3.0f}; // (the corner v0 exactly on its surface)
            Vector cross{0, 1, 0, 0}, side{1, 0, 0, 0}, along{5, 0, 0, 1}, still{0, 0, 0, 1};
            {
                // (moving across the cross: a zero divisor deciding which edge)
                CollSphere beside{{-12, 0.5f, 0, 1}, 1.0f};
                Vector down{0, -1, 0, 0}, fast{20, 0, 0, 1};
                float t = -777.0f;
                int count2 = 0;
                Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                trace.push_back(corner(&flat[0], &beside, &fast, &still, &flat[0], &flat[1], &flat[2], &down, &side,
                                       &t, 1.0f, &count2, &point, &normal));
                trace.push_back(SpBits(t));
                SpRecord(trace, point);
            }
            for (const CollSphere& sphere : {onSurface, CollSphere{{-3, 2, 0, 1}, 1.0f}, CollSphere{{-10, -1, 0, 1}, 1.0f}}) {
                float t = -777.0f;
                int count2 = 0;
                Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                trace.push_back(corner(&flat[0], &sphere, &along, &still, &flat[0], &flat[1], &flat[2], &cross, &side,
                                       &t, 1.0f, &count2, &point, &normal));
                trace.push_back(SpBits(t));
                SpRecord(trace, point);
            }
            const CollSphere touching[] = {{{2, 2, 3, 1}, 3.0f}, {{2, 2, -3, 1}, 3.0f}, {{2, 2, 4, 1}, 3.0f},
                                           {{1, 1, 5, 1}, 2.0f}, {{2, 2, -1.5f, 1}, 2.0f}};
            const Vector moves[] = {{0, 0, -1, 1}, {0, 0, 0, 1}, {0, 0, -2, 1}, {0, 0, 1, 1}};
            for (const CollSphere& sphere : touching) {
                for (const Vector& move : moves) {
                    for (float limit : {1.0f, 0.5f, 3.0f, 3.5f}) {
                        float t = -777.0f;
                        Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                        trace.push_back(tri(&sphere, &flat[0], &flat[1], &flat[2], &t, limit, &move, &point, &normal));
                        trace.push_back(SpBits(t));
                        SpRecord(trace, point);
                        SpRecord(trace, normal);
                        t = -777.0f;
                        trace.push_back(tri(&sphere, &flat[0], &flat[1], &flat[1], &t, limit, &move, &point, &normal));
                        trace.push_back(SpBits(t));
                        SpRecord(trace, point);
                    }
                }
                Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                trace.push_back(stand(&sphere, &flat[0], &flat[2], &flat[1], &point, &normal));
                SpRecord(trace, point);
            }
        }
        for (int n = 0; n < 6000; ++n) {
            SpCase c = SpMake(random, n);
            // a segment at the sphere
            Mth::Line line;
            line.start = SpVector(random, c.sphere.radius * 3.0f);
            line.start.x += c.sphere.centre.x;
            line.start.y += c.sphere.centre.y;
            line.start.z += c.sphere.centre.z;
            line.end = {2.0f * c.sphere.centre.x - line.start.x + SpSpread(random, c.sphere.radius),
                        2.0f * c.sphere.centre.y - line.start.y, 2.0f * c.sphere.centre.z - line.start.z,
                        SpSpread(random, 2.0f)};
            if (n % 6 == 1)
                line.end = {line.start.x + (c.sphere.centre.x - line.start.x) * 0.5f,
                            line.start.y + (c.sphere.centre.y - line.start.y) * 0.5f,
                            line.start.z + (c.sphere.centre.z - line.start.z) * 0.5f, 1.0f};
            if (n % 17 == 0)
                line.end = line.start;
            int count = -7;
            Vector points[2] = {{-777, -777, -777, -777}, {-777, -777, -777, -777}};
            bool result = seg(&line, &c.sphere, &count, points, 5);
            trace.push_back(result);
            trace.push_back(static_cast<uint32_t>(count));
            SpRecord(trace, points[0]);
            SpRecord(trace, points[1]);
            hits[0] += result;
            // the moving sphere against the triangle's edges and corners
            for (int k = 0; k < 3; ++k) {
                Mth::Line e;
                e.start = c.v[k];
                e.end = c.v[(k + 1) % 3];
                float t = -777.0f;
                count = -7;
                points[0] = {-777, -777, -777, -777};
                Vector moving = SpVector(random, 20.0f, n % 2 ? 1.0f : 0.5f);
                const Vector& edgeMotion = n % 4 == 0 ? moving : zero;
                result = edge(&e, &edgeMotion, &c.sphere, &c.motion, &t, c.limit, &count, points);
                trace.push_back(result);
                trace.push_back(SpBits(t));
                trace.push_back(static_cast<uint32_t>(count));
                SpRecord(trace, points[0]);
                hits[1] += result;
            }
            {
                int k = static_cast<int>(random.Below(3));
                Vector cross = SpVector(random, 1.0f, 0.0f), side = SpVector(random, 1.0f, 0.0f);
                if (n % 19 == 0)
                    cross = {0, 0, 0, 0}; // (a zero divisor)
                float t = -777.0f;
                int count2 = 0;
                Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                Vector moving = SpVector(random, 20.0f, 1.0f);
                result = corner(&c.v[k], &c.sphere, &c.motion, n % 3 == 0 ? &moving : &zero, &c.v[0], &c.v[1], &c.v[2], &cross, &side, &t,
                                c.limit, &count2, &point, &normal);
                trace.push_back(result);
                trace.push_back(SpBits(t));
                trace.push_back(static_cast<uint32_t>(count2));
                SpRecord(trace, point);
                SpRecord(trace, normal);
                hits[2] += result;
            }
            // the triangle
            {
                float t = -777.0f;
                Vector point{-777, -777, -777, -777}, normal{-5, -5, -5, -5};
                result = tri(&c.sphere, &c.v[0], &c.v[1], &c.v[2], &t, c.limit, &c.motion, &point, &normal);
                trace.push_back(result);
                trace.push_back(SpBits(t));
                SpRecord(trace, point);
                SpRecord(trace, normal);
                hits[3] += result;
                point = {-777, -777, -777, -777};
                normal = {-5, -5, -5, -5};
                result = stand(&c.sphere, &c.v[0], &c.v[1], &c.v[2], &point, &normal);
                trace.push_back(result);
                SpRecord(trace, point);
                SpRecord(trace, normal);
                hits[4] += result;
            }
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    Log("sphere tests: %d segment, %d edge, %d corner, %d triangle, %d standing hits", hits[0], hits[1], hits[2],
        hits[3], hits[4]);
    CompareTraces(expected, actual, "Nx sphere tests");
}
SELFTEST(Early, TestAnimCollSphereTests, TestStandIns::RemoveStandIns);

// --- the sphere queries: a sector's faces, the world query, the sphere feeler ---

constexpr int kSqVertices = 40;
constexpr int kSqFaces = 60;
constexpr int kSqFakes = 8;

struct SqFake {
    void** vtable;
    uint32_t checksum;
    uint16_t flags;
    uint8_t stamp;
    uint8_t pad;
    uint32_t geometry;
};

struct SphereArena {
    alignas(16) uint8_t sector[0x18];
    Nx::CCollObjTriData geometry[2];
    float plain[kSqVertices][3];
    uint16_t packed[kSqVertices][3];
    uint8_t faces10[kSqFaces * 10];
    uint8_t faces8[kSqFaces * 8];
    Nx::SphereHits hits;
    uint8_t feeler[sizeof(Nx::CSphereFeeler)];
    SqFake fakes[kSqFakes];
    Mth::BBox fakeBoxes[kSqFakes];
    uint32_t grids[4];
    uint32_t list[kSqFakes + 1];
    uint8_t cache[0xFD0];
};
SphereArena* g_sa;
bool g_saOriginal;
uint32_t g_saGrid;

template <typename F>
F SaFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_saOriginal, address));
}
uint32_t SaWhere(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_sa);
    return word >= base && word < base + sizeof(SphereArena) ? 0xA0000000u + (word - base) : word;
}
void SaWords(const void* p, size_t bytes)
{
    const auto* w = static_cast<const uint32_t*>(p);
    for (size_t i = 0; i < bytes / 4; ++i)
        g_trace.push_back(SaWhere(w[i]));
}
int SaIndex(const void* fake) { return static_cast<int>(reinterpret_cast<const SqFake*>(fake) - g_sa->fakes); }

bool __fastcall SqTouches(SqFake* self, void*, const Mth::BBox* box)
{
    Record({0x70C40013u, static_cast<uint32_t>(SaIndex(self))});
    SaWords(box, sizeof(*box));
    return SaIndex(self) % 3 != 1;
}
bool __fastcall SqSlotSphere(SqFake* self, void*, const CollSphere* sphere, const Mth::BBox* box, Nx::SphereHits* hits)
{
    Record({0x5F4E0016u, static_cast<uint32_t>(SaIndex(self)), SaWhere(reinterpret_cast<uint32_t>(sphere)),
            SaWhere(reinterpret_cast<uint32_t>(hits)), static_cast<uint32_t>(hits->count)});
    SaWords(box, sizeof(*box));
    if (SaIndex(self) % 2 == 0 && hits->count < Nx::SphereHits::kMost)
        hits->hits[hits->count++].checksum = self->checksum;
    return true;
}
void* g_sqVtable[24];

// A sector's faces against spheres standing and moving (plain and compressed geometry, flags ignored / required,
// the list nearly full and full, modes 0 / 1 / 2), the world query through a cache and through the grids (with and
// without a move), the sphere feeler (built on dirty memory, its query with its own cache or the current one).
void TestAnimCollSphereQueries()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x0067C6B8, 0x1000}, {0x00691990, 4}, {0x00693998, 1}});
    StandIns& s = *new StandIns();
    for (auto [ours, theirs] : {std::pair<uint32_t, uint32_t>{AddressOf(&Mth::Capsule::IntersectsSegment), 0x004032E0u},
                                {AddressOf(&Mth::Capsule::ContainsPoint), 0x00403330u}})
        s.Bytes(ours, Branch(0xE9, ours, OriginalFunction(theirs)));
    s.At({0x0048C940, AddressOf(&Nx::GetFirstCollGrid)}, 0, [](Regs&) {
        Record({0x48C94000u}); // (the original's argument, the sphere, isn't passed by ours: it isn't read)
        return g_saGrid;
    });
    s.At({0x00418710, AddressOf(&Nx::CCollGrid::ObjectsInBox)}, 4, [](Regs& r) {
        Record({0x41871000u, SaWhere(r.ecx)});
        SaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::BBox));
        return 0x12345678u;
    });
    s.At({0x0048C980, AddressOf(&Nx::GetCollGridsOnLine)}, 0, [](Regs& r) {
        Record({0x48C98000u});
        SaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::Line));
        return g_saGrid ? reinterpret_cast<uint32_t>(g_sa->grids) : 0u;
    });
    s.At({0x004188C0, AddressOf(&Nx::CCollGrid::ObjectsAlongLine)}, 8, [](Regs& r) {
        Record({0x4188C000u, r.ecx, r.args[1] != 0});
        SaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::Line));
        return reinterpret_cast<uint32_t>(g_sa->list);
    });
    for (void*& slot : g_sqVtable)
        slot = nullptr;
    g_sqVtable[13] = reinterpret_cast<void*>(&SqTouches);
    g_sqVtable[16] = reinterpret_cast<void*>(&SqSlotSphere);
    g_sa = static_cast<SphereArena*>(VirtualAlloc(nullptr, sizeof(SphereArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    int found = 0;
    auto runOnce = [&](bool original) {
        g_saOriginal = original;
        g_trace.clear();
        std::memset(g_sa, 0xA7, sizeof(SphereArena));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        auto* table = reinterpret_cast<uint16_t*>(0x0067C6B8);
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x4110B0);
        for (int v = 0; v < kSqVertices; ++v) {
            for (int k = 0; k < 3; ++k) {
                g_sa->plain[v][k] = SpSpread(random, 40.0f);
                g_sa->packed[v][k] = static_cast<uint16_t>((g_sa->plain[v][k] + 50.0f) * 16.0f);
            }
        }
        for (int f = 0; f < kSqFaces; ++f) {
            auto* wide = reinterpret_cast<uint16_t*>(g_sa->faces10 + f * 10);
            uint8_t* small = g_sa->faces8 + f * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            std::memcpy(small, wide, 4);
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(random.Below(kSqVertices));
                wide[2 + k] = corner;
                small[4 + k] = static_cast<uint8_t>(corner);
            }
            small[7] = 0;
        }
        for (int g = 0; g < 2; ++g) {
            Nx::CCollObjTriData& geometry = g_sa->geometry[g];
            std::memset(&geometry, 0, sizeof(geometry));
            geometry.m_checksum = 0x6E0A0000u + g;
            geometry.m_numVertices = kSqVertices;
            geometry.m_numFaces = kSqFaces;
            geometry.m_smallFaces = g == 1;
            geometry.m_compressed = g == 1;
            geometry.m_faces = g == 1 ? g_sa->faces8 : g_sa->faces10;
            geometry.m_min = {-50, -50, -50, 1};
            geometry.m_max = {50, 50, 50, 1};
            geometry.m_vertices = g == 1 ? static_cast<void*>(g_sa->packed) : static_cast<void*>(g_sa->plain);
        }
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_sa->sector);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC7;
        auto slot = SaFn<bool(__thiscall*)(void*, const CollSphere*, const Mth::BBox*, Nx::SphereHits*)>(0x004110B0);
        for (int g = 0; g < 2; ++g) {
            sector->m_geometry = &g_sa->geometry[g];
            for (int n = 0; n < 600; ++n) {
                CollSphere sphere{SpVector(random, 40.0f, SpSpread(random, 2.0f)),
                                  static_cast<float>(random.Below(100)) / 10.0f + 0.5f};
                Mth::BBox box({-60, -60, -60, 1});
                box.AddPoint({60, 60, 60, 1});
                Nx::SphereHits& hits = g_sa->hits;
                std::memset(&hits, 0x6B, sizeof(hits));
                hits.count = n % 23 == 0 ? 32 : n % 17 == 0 ? 30 : 0;
                hits.ignoreFlags = n % 5 == 1 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hits.requireFlags = n % 5 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hits.mode = static_cast<int>(random.Below(3));
                hits.sweep = n % 3 == 0 ? 0.0f : n % 3 == 1 ? 1.0f : static_cast<float>(random.Below(5)) * 0.5f;
                if (n % 7 == 6)
                    hits.sweep = -0.5f;
                hits.direction = SpVector(random, 30.0f, n % 2 ? 1.0f : 0.5f);
                bool result = slot(sector, &sphere, &box, &hits);
                found += original && hits.count > 0 && hits.count < 30;
                Record({static_cast<uint32_t>(result)});
                SaWords(&hits, sizeof(hits));
            }
        }
        // the world query
        for (int i = 0; i < kSqFakes; ++i) {
            g_sa->fakes[i] = {g_sqVtable, 0xFA4E0000u + i, 0, 0, 0, 0};
            g_sa->fakeBoxes[i].min = {SpSpread(random, 100.0f), SpSpread(random, 100.0f), SpSpread(random, 100.0f), 1};
            g_sa->fakeBoxes[i].max = {g_sa->fakeBoxes[i].min.x + 40, g_sa->fakeBoxes[i].min.y + 40,
                                      g_sa->fakeBoxes[i].min.z + 40, 1};
            g_sa->list[i] = reinterpret_cast<uint32_t>(&g_sa->fakes[i]);
        }
        g_sa->list[kSqFakes] = 0;
        auto* cache = reinterpret_cast<Nx::CCollCache*>(g_sa->cache);
        cache->m_box.min = {-150, -150, -150, 1};
        cache->m_box.max = {150, 150, 150, 1};
        for (int i = 0; i < kSqFakes; ++i) {
            cache->m_entries[i].object = reinterpret_cast<Nx::CCollObj*>(&g_sa->fakes[i]);
            cache->m_entries[i].geometry = &g_sa->fakeBoxes[i];
        }
        cache->m_statics = 6;
        cache->m_movables = 2;
        auto world = SaFn<bool(__cdecl*)(const CollSphere*, Nx::SphereHits*, Nx::CCollCache*)>(0x00410240);
        for (int n = 0; n < 200; ++n) {
            CollSphere sphere{SpVector(random, 100.0f, SpSpread(random, 2.0f)),
                              n % 4 == 0 ? 120.0f : static_cast<float>(random.Below(300)) / 10.0f};
            g_saGrid = n % 5 == 0 ? 0u : 0x6D1D0000u;
            for (int k = 0; k < 4; ++k)
                g_sa->grids[k] = k < static_cast<int>(n % 4) ? 0x6D1D0000u + k : 0u;
            Nx::SphereHits& hits = g_sa->hits;
            std::memset(&hits, 0x6B, sizeof(hits));
            hits.count = 9;
            hits.sweep = n % 3 == 0 ? 0.0f : SpSpread(random, 2.0f);
            hits.direction = SpVector(random, 40.0f, SpSpread(random, 2.0f));
            Record({static_cast<uint32_t>(world(&sphere, &hits, n % 3 ? cache : nullptr)), static_cast<uint32_t>(hits.count)});
        }
        // the sphere feeler
        auto* feeler = reinterpret_cast<Nx::CSphereFeeler*>(g_sa->feeler);
        Vector centre{1.5f, -2.5f, 3.25f, 1.0f};
        Record({SaWhere(reinterpret_cast<uint32_t>(
            SaFn<void*(__thiscall*)(void*, const Vector*, float)>(0x00418330)(feeler, &centre, 7.5f)))});
        SaWords(feeler, sizeof(Nx::CSphereFeeler));
        for (int n = 0; n < 20; ++n) {
            feeler->m_sphere.centre = SpVector(random, 80.0f);
            feeler->mp_cache = n % 2 ? cache : nullptr;
            GameVariable<Nx::CCollCache*>(0x00691990) = n % 4 == 0 ? cache : nullptr;
            g_saGrid = 0x6D1D0000u;
            Record({SaFn<uint32_t(__thiscall*)(void*, int)>(0x00418300)(feeler, 77) & 0xFFu,
                    static_cast<uint32_t>(feeler->m_hits.count), static_cast<uint32_t>(feeler->m_hits.mode)});
        }
        EndRunPatches();
        return g_trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(g_sa, 0, MEM_RELEASE);
    delete &s;
    Log("sphere queries: %d with hits; %u trace words", found, static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx sphere queries and the sphere feeler");
}
SELFTEST(Early, TestAnimCollSphereQueries, TestStandIns::RemoveStandIns);

} // namespace
