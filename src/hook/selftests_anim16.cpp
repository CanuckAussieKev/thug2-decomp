// Self-tests for AN-18: the rect queries (src/game/nx/coll_tests.cpp, coll_rect.cpp) - the original and ours on the
// same random rects and triangles; results and outputs compared bit for bit.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_rect.h"
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

uint32_t RtBits(float f)
{
    if (f != f)
        return 0x7FC00000u;
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float RtSpread(Lcg& random, float range)
{
    return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range;
}
Vector RtVector(Lcg& random, float range)
{
    return {RtSpread(random, range), RtSpread(random, range), RtSpread(random, range), RtSpread(random, range)};
}

// Rects (any orientation, and lying in each axis plane so every projection is used; degenerate ones) against
// triangles cut by their plane (corners on either side, on the plane, within 1e-5 of it, nearly parallel to it,
// cutting the rect's sides or lying inside or outside it).
void TestAnimCollRectTriangle()
{
    using RectFn = bool(__cdecl*)(const Nx::CollRect*, const Vector*, const Vector*, const Vector*, Vector*, int*);
    int outcomes[4] = {};
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto test = reinterpret_cast<RectFn>(Code(original, 0x0040D370));
        Lcg random(0x40D370);
        // exact cases: a unit rect in z = 0 (a corner's plane distance is its z; 1e-5 is the on-plane limit) and
        // rects whose normal has two equal largest parts
        {
            const float heights[] = {0.0f, 1e-5f, -1e-5f, 2e-5f, -2e-5f, 0.5f, -0.5f, 1.0f, -1.0f, 9.99e-6f};
            Nx::CollRect units[3] = {{{0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0}},
                                     {{0, 0, 0, 1}, {0, 1, 0, 0}, {1, 0, -1, 0}},
                                     {{0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 1, 0}}};
            for (int n = 0; n < 6000; ++n) {
                const Nx::CollRect& rect = units[n % 3];
                Vector corners[3];
                for (Vector& c : corners) {
                    c = {static_cast<float>(static_cast<int>(random.Below(9)) - 2) * 0.25f,
                         static_cast<float>(static_cast<int>(random.Below(9)) - 2) * 0.25f,
                         heights[random.Below(10)], 0.0f};
                    if (n % 3 != 0)
                        c.z += c.x - c.y * 0.5f;
                }
                Vector points[2] = {{-777, -777, -777, -777}, {-777, -777, -777, -777}};
                int kinds[2] = {-7, -7};
                trace.push_back(test(&rect, &corners[0], &corners[1], &corners[2], points, kinds));
                for (const Vector& p : points) {
                    trace.push_back(RtBits(p.x));
                    trace.push_back(RtBits(p.y));
                    trace.push_back(RtBits(p.z));
                    trace.push_back(RtBits(p.w));
                }
                trace.push_back(static_cast<uint32_t>(kinds[0]));
                trace.push_back(static_cast<uint32_t>(kinds[1]));
            }
        }
        for (int n = 0; n < 20000; ++n) {
            float scale = n % 3 == 0 ? 100.0f : n % 3 == 1 ? 5.0f : 0.5f;
            Nx::CollRect rect{RtVector(random, scale), RtVector(random, scale), RtVector(random, scale)};
            switch (n % 7) { // (lying in an axis plane, roughly or exactly)
            case 0:
                rect.sideA.z = 0;
                rect.sideB.z = n % 2 ? 0 : RtSpread(random, scale * 0.01f);
                break;
            case 1:
                rect.sideA.y = 0;
                rect.sideB.y = 0;
                break;
            case 2:
                rect.sideA.x = n % 2 ? 0 : RtSpread(random, scale * 0.01f);
                rect.sideB.x = 0;
                break;
            case 3:
                if (n % 50 == 3)
                    rect.sideB = rect.sideA; // (no area)
                break;
            default:
                break;
            }
            // a triangle around a point of the rect (inside or a little beyond), its corners across the plane
            float u = RtSpread(random, 0.8f) + 0.5f, v = RtSpread(random, 0.8f) + 0.5f;
            Vector centre{rect.origin.x + u * rect.sideA.x + v * rect.sideB.x,
                          rect.origin.y + u * rect.sideA.y + v * rect.sideB.y,
                          rect.origin.z + u * rect.sideA.z + v * rect.sideB.z, 1.0f};
            float size = scale * (n % 4 == 0 ? 2.0f : 0.6f);
            Vector corners[3];
            for (Vector& c : corners) {
                c = {centre.x + RtSpread(random, size), centre.y + RtSpread(random, size),
                     centre.z + RtSpread(random, size), RtSpread(random, 2.0f)};
            }
            if (n % 11 == 0) // (a corner on the plane, or nearly)
                corners[random.Below(3)] = {rect.origin.x + rect.sideA.x * 0.3f, rect.origin.y + rect.sideA.y * 0.3f,
                                            rect.origin.z + rect.sideA.z * 0.3f + (n % 2 ? 0.0f : 1e-7f), 1.0f};
            if (n % 13 == 0) // (two corners equally far: an edge parallel to the plane)
                corners[1] = {corners[0].x + rect.sideA.x, corners[0].y + rect.sideA.y, corners[0].z + rect.sideA.z,
                              corners[0].w};
            if (n % 97 == 0)
                corners[2].y = std::numeric_limits<float>::quiet_NaN();
            Vector points[2];
            int kinds[2] = {-7, -7};
            for (Vector& p : points)
                p = {-777.0f, -777.0f, -777.0f, -777.0f};
            bool hit = test(&rect, &corners[0], &corners[1], &corners[2], points, kinds);
            trace.push_back(hit);
            for (const Vector& p : points) {
                trace.push_back(RtBits(p.x));
                trace.push_back(RtBits(p.y));
                trace.push_back(RtBits(p.z));
                trace.push_back(RtBits(p.w));
            }
            trace.push_back(static_cast<uint32_t>(kinds[0]));
            trace.push_back(static_cast<uint32_t>(kinds[1]));
            if (original)
                ++outcomes[hit ? (kinds[1] == 0 ? 3 : 2) : (kinds[0] != -7 ? 1 : 0)];
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    Log("rect / triangle: %d misses, %d one point, %d two (an end and ...), %d two (a side last)", outcomes[0],
        outcomes[1], outcomes[2], outcomes[3]);
    CompareTraces(expected, actual, "Nx rect / triangle");
}
SELFTEST(Early, TestAnimCollRectTriangle);

// --- the rect queries: a sector's and an instance's faces, the world query, the rect feeler ---

constexpr int kRqVertices = 40;
constexpr int kRqFaces = 60;
constexpr int kRqFakes = 8;

struct RqFake { // a stand-in collision object (only its vtable and checksum are read)
    void** vtable;
    uint32_t checksum;
    uint16_t flags;
    uint8_t stamp;
    uint8_t pad;
    uint32_t geometry;
};

struct RectArena {
    alignas(16) uint8_t objects[2][0xD8];
    Nx::CCollObjTriData geometry[2];
    float plain[kRqVertices][3];
    uint16_t packed[kRqVertices][3];
    uint8_t faces10[kRqFaces * 10];
    uint8_t faces8[kRqFaces * 8];
    uint8_t rejection[kRqFaces];
    uint32_t nodes[2][1 + 3 * 2];
    uint16_t leaves[2][kRqFaces * 2];
    Nx::RectHits hits;
    uint8_t feeler[sizeof(Nx::CRectFeeler)];
    RqFake fakes[kRqFakes];
    Mth::BBox fakeBoxes[kRqFakes];
    uint32_t grids[4];
    uint32_t list[kRqFakes + 1];
    uint8_t cache[0xFD0];
    uint8_t heap[0x100];
};
RectArena* g_ra;
bool g_raOriginal;
uint32_t g_raGrid; // what the rect's grid stand-in answers (0: none)

template <typename F>
F RaFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_raOriginal, address));
}
uint32_t RaWhere(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_ra);
    return word >= base && word < base + sizeof(RectArena) ? 0xA0000000u + (word - base) : word;
}
void RaWords(const void* p, size_t bytes)
{
    const auto* w = static_cast<const uint32_t*>(p);
    for (size_t i = 0; i < bytes / 4; ++i)
        g_trace.push_back(RaWhere(w[i]));
}
int RaIndex(const void* fake) { return static_cast<int>((reinterpret_cast<const RqFake*>(fake) - g_ra->fakes)); }

bool __fastcall RqTouches(RqFake* self, void*, const Mth::BBox* box)
{
    Record({0x70C40013u, static_cast<uint32_t>(RaIndex(self))});
    RaWords(box, sizeof(*box));
    return RaIndex(self) % 3 != 1;
}
bool __fastcall RqSlotRect(RqFake* self, void*, const Nx::CollRect* rect, const Mth::BBox* box, Nx::RectHits* hits)
{
    Record({0x0EC70015u, static_cast<uint32_t>(RaIndex(self)), RaWhere(reinterpret_cast<uint32_t>(rect)),
            RaWhere(reinterpret_cast<uint32_t>(hits)), static_cast<uint32_t>(hits->count)});
    RaWords(box, sizeof(*box));
    if (RaIndex(self) % 2 == 0 && hits->count < Nx::RectHits::kMost)
        hits->hits[hits->count++].checksum = self->checksum;
    return true;
}
void* g_rqVtable[24];

Mth::Matrix RqRotation(Lcg& random)
{
    float yaw = RtSpread(random, 3.1f), pitch = RtSpread(random, 1.5f);
    float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    Mth::Matrix m;
    m.row[0] = {cy, 0.0f, -sy, 0.0f};
    m.row[1] = {sy * sp, cp, cy * sp, 0.0f};
    m.row[2] = {sy * cp, -sp, cy * cp, 0.0f};
    m.row[3] = {0, 0, 0, 1};
    return m;
}

// A random rect near `centre`.
Nx::CollRect RqRect(Lcg& random, const Vector& centre, float size)
{
    Nx::CollRect rect;
    rect.sideA = {RtSpread(random, size), RtSpread(random, size), RtSpread(random, size), RtSpread(random, 0.5f)};
    rect.sideB = {RtSpread(random, size), RtSpread(random, size), RtSpread(random, size), RtSpread(random, 0.5f)};
    rect.origin = {centre.x - 0.5f * rect.sideA.x - 0.5f * rect.sideB.x + RtSpread(random, size * 0.3f),
                   centre.y - 0.5f * rect.sideA.y - 0.5f * rect.sideB.y + RtSpread(random, size * 0.3f),
                   centre.z - 0.5f * rect.sideA.z - 0.5f * rect.sideB.z + RtSpread(random, size * 0.3f), 1.0f};
    return rect;
}

// Hits along a few lines for JoinHits: pieces sharing ends (either way round, within 0.1 or not), duplicates,
// pieces with other normals or directions.
void RqJoinHits(Lcg& random, Nx::CRectFeeler* feeler)
{
    int count = static_cast<int>(random.Below(Nx::RectHits::kMost + 1));
    std::memset(&feeler->m_hits.hits, 0x3C, sizeof(feeler->m_hits.hits));
    Vector along[3] = {{1, 0, 0, 0}, {0.6f, 0.8f, 0, 0}, {0, 0.28f, 0.96f, 0}};
    Vector normals[2] = {{0, 1, 0, 0}, {0, 0.996f, 0.0892f, 0}};
    for (int i = 0; i < count; ++i) {
        Nx::RectHit& hit = feeler->m_hits.hits[i];
        int line = static_cast<int>(random.Below(3));
        float t0 = static_cast<float>(random.Below(8)), t1 = t0 + 1.0f;
        if (random.Below(4) == 0)
            std::swap(t0, t1);
        float nudge = random.Below(3) == 0 ? (random.Below(2) ? 0.05f : 0.2f) : 0.0f;
        const Vector& d = along[line];
        hit.ends[0].point = {d.x * t0 + nudge, d.y * t0 + line, d.z * t0, static_cast<float>(random.Below(5))};
        hit.ends[1].point = {d.x * t1, d.y * t1 + line, d.z * t1 - nudge, static_cast<float>(random.Below(5))};
        hit.ends[0].edge = {static_cast<float>(i), 1, 2, 3};
        hit.ends[1].edge = {static_cast<float>(-i), 4, 5, 6};
        hit.ends[0].onTriangleEdge = random.Below(2) != 0;
        hit.ends[1].onTriangleEdge = random.Below(2) != 0;
        hit.normal = normals[random.Below(5) == 0 ? 1 : 0];
        if (random.Below(9) == 0)
            hit.normal = {0, 0.98f, 0.199f, 0};
        hit.checksum = 0xC0DE0000u + i;
        hit.faceFlag40 = static_cast<uint8_t>(random.Below(2) * 0x40);
        if (i > 0 && random.Below(6) == 0) { // (a duplicate, maybe reversed)
            const Nx::RectHit& earlier = feeler->m_hits.hits[random.Below(static_cast<uint32_t>(i))];
            hit.ends[0].point = earlier.ends[random.Below(2)].point;
            hit.ends[1].point = earlier.ends[random.Below(2)].point;
            hit.ends[1].point.x += random.Below(2) ? 0.0f : 0.09f;
        }
    }
    feeler->m_hits.count = count;
    std::memset(feeler->m_joined, 0x5D, sizeof(feeler->m_joined));
    feeler->m_joinedCount = -5;
    RaFn<void(__thiscall*)(void*)>(0x00417C00)(feeler);
    Record({static_cast<uint32_t>(feeler->m_joinedCount)});
    RaWords(feeler->m_joined, sizeof(feeler->m_joined));
}

// A sector's and an instance's faces against random rects (plain and compressed geometry; flags ignored / required;
// the list nearly full and full), rects into the instance's space, the world query through a cache (holding the rect
// or not) and through the grids (none, some), the rect feeler (built on dirty memory, its query with its own cache
// or the current one, its hits joined), the hit copies and the small pieces next to them.
void TestAnimCollRectQueries()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x00691910, 1}, {0x0067C6B8, 0x1000}, {0x0069198C, 4}, {0x00693998, 1}});
    StandIns& s = *new StandIns();
    s.At({0x0048C940, AddressOf(&Nx::GetFirstCollGrid)}, 0, [](Regs&) {
        Record({0x48C94000u}); // (the original's argument, the rect, isn't passed by ours: it isn't read)
        return g_raGrid;
    });
    s.At({0x00418710, AddressOf(&Nx::CCollGrid::ObjectsInBox)}, 4, [](Regs& r) {
        Record({0x41871000u, RaWhere(r.ecx)});
        RaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::BBox));
        return 0x12345678u;
    });
    s.At({0x0048C980, AddressOf(&Nx::GetCollGridsOnLine)}, 0, [](Regs& r) {
        Record({0x48C98000u});
        RaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::Line));
        return g_raGrid ? reinterpret_cast<uint32_t>(g_ra->grids) : 0u;
    });
    s.At({0x004188C0, AddressOf(&Nx::CCollGrid::ObjectsAlongLine)}, 8, [](Regs& r) {
        Record({0x4188C000u, r.ecx, r.args[1] != 0});
        RaWords(reinterpret_cast<void*>(r.args[0]), sizeof(Mth::Line));
        return reinterpret_cast<uint32_t>(g_ra->list);
    });
    for (void*& slot : g_rqVtable)
        slot = nullptr;
    g_rqVtable[13] = reinterpret_cast<void*>(&RqTouches);
    g_rqVtable[15] = reinterpret_cast<void*>(&RqSlotRect);
    g_ra = static_cast<RectArena*>(VirtualAlloc(nullptr, sizeof(RectArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto runOnce = [&](bool original) {
        g_raOriginal = original;
        g_trace.clear();
        std::memset(g_ra, 0xA7, sizeof(RectArena));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        auto* table = reinterpret_cast<uint16_t*>(0x0067C6B8);
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x410C90);
        for (int v = 0; v < kRqVertices; ++v) {
            for (int k = 0; k < 3; ++k) {
                g_ra->plain[v][k] = RtSpread(random, 40.0f);
                g_ra->packed[v][k] = static_cast<uint16_t>((g_ra->plain[v][k] + 50.0f) * 16.0f);
            }
        }
        for (int f = 0; f < kRqFaces; ++f) {
            auto* wide = reinterpret_cast<uint16_t*>(g_ra->faces10 + f * 10);
            uint8_t* small = g_ra->faces8 + f * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            std::memcpy(small, wide, 4);
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(random.Below(kRqVertices));
                wide[2 + k] = corner;
                small[4 + k] = static_cast<uint8_t>(corner);
            }
            small[7] = 0;
            if (f % 13 == 5) // (a face with no area: no normal)
                wide[4] = wide[3], small[6] = small[5];
        }
        // a tree for each geometry (for half the queries): a root split on x at 0, the faces with a corner below in
        // the less leaf, those with one at or above in the greater
        auto tree = [&](int g) {
            auto* nodes = reinterpret_cast<Nx::CCollBSPNode*>(g_ra->nodes[g] + 1);
            g_ra->nodes[g][0] = 3;
            uint16_t* less = g_ra->leaves[g];
            uint16_t* greater = less + kRqFaces;
            int lessCount = 0, greaterCount = 0;
            for (int f = 0; f < kRqFaces; ++f) {
                bool below = false, above = false;
                for (int k = 0; k < 3; ++k)
                    (g_ra->plain[g_ra->geometry[g].FaceVertex(f, k)][0] < 0.0f ? below : above) = true;
                if (below)
                    less[lessCount++] = static_cast<uint16_t>(f);
                if (above)
                    greater[greaterCount++] = static_cast<uint16_t>(f);
            }
            nodes[0].m_split = 0;
            nodes[0].m_link = reinterpret_cast<uint32_t>(&nodes[1]);
            nodes[1].m_split = 3 | (lessCount << 16);
            nodes[1].m_link = reinterpret_cast<uint32_t>(less);
            nodes[2].m_split = 3 | (greaterCount << 16);
            nodes[2].m_link = reinterpret_cast<uint32_t>(greater);
            return nodes;
        };
        for (int g = 0; g < 2; ++g) {
            Nx::CCollObjTriData& geometry = g_ra->geometry[g];
            std::memset(&geometry, 0, sizeof(geometry));
            geometry.m_checksum = 0x6E0A0000u + g;
            geometry.m_numVertices = kRqVertices;
            geometry.m_numFaces = kRqFaces;
            geometry.m_smallFaces = g == 1;
            geometry.m_compressed = g == 1;
            geometry.m_faces = g == 1 ? g_ra->faces8 : g_ra->faces10;
            geometry.m_min = {-50, -50, -50, 1};
            geometry.m_max = {50, 50, 50, 1};
            geometry.m_vertices = g == 1 ? static_cast<void*>(g_ra->packed) : static_cast<void*>(g_ra->plain);
            geometry.m_unknown3C = 0x3F340000u + g;
        }
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_ra->objects[0]);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC7;
        sector->m_buffer = g_ra->rejection;
        auto* instance = reinterpret_cast<Nx::CCollStaticInstance*>(g_ra->objects[1]);
        RaFn<void*(__thiscall*)(void*)>(0x004127C0)(instance);
        auto rectSector = RaFn<bool(__thiscall*)(void*, const Nx::CollRect*, const Mth::BBox*, Nx::RectHits*)>(0x00410C90);
        auto rectInstance = RaFn<bool(__thiscall*)(void*, const Nx::CollRect*, const Mth::BBox*, Nx::RectHits*)>(0x004128B0);
        int found = 0;
        for (int g = 0; g < 2; ++g) {
            Nx::CCollObjTriData* geometry = &g_ra->geometry[g];
            sector->m_geometry = geometry;
            RaFn<void(__thiscall*)(void*, void*)>(0x00411480)(instance, geometry);
            for (int n = 0; n < 800; ++n) {
                geometry->m_bsp = n < 400 ? nullptr : tree(g);
                if (n % 100 == 0) {
                    Mth::Matrix m = RqRotation(random);
                    Vector place{RtSpread(random, 200.0f), RtSpread(random, 200.0f), RtSpread(random, 200.0f), 1.0f};
                    Vector size{RtSpread(random, 1.0f) + 1.5f, RtSpread(random, 1.0f) + 1.5f,
                                RtSpread(random, 1.0f) + 1.5f, RtSpread(random, 1.0f) + 1.5f};
                    RaFn<void(__thiscall*)(void*, const Vector*)>(0x00413F50)(instance, &size);
                    RaFn<void(__thiscall*)(void*, const Mth::Matrix*)>(0x00413F30)(instance, &m);
                    RaFn<void(__thiscall*)(void*, const Vector*)>(0x00413F00)(instance, &place);
                }
                bool placed = n % 2 == 1;
                Vector centre = placed ? instance->m_matrix.row[3] : Vector{0, 0, 0, 1};
                centre.x += RtSpread(random, 30.0f);
                centre.y += RtSpread(random, 30.0f);
                centre.z += RtSpread(random, 30.0f);
                Nx::CollRect rect = RqRect(random, centre, n % 5 == 0 ? 90.0f : 25.0f);
                Mth::BBox box(rect.origin);
                box.AddPoint({rect.origin.x + rect.sideA.x + rect.sideB.x, rect.origin.y + rect.sideA.y + rect.sideB.y,
                              rect.origin.z + rect.sideA.z + rect.sideB.z, 1.0f});
                box.AddPoint({rect.origin.x + rect.sideA.x, rect.origin.y + rect.sideA.y, rect.origin.z + rect.sideA.z, 1});
                box.AddPoint({rect.origin.x + rect.sideB.x, rect.origin.y + rect.sideB.y, rect.origin.z + rect.sideB.z, 1});
                Nx::RectHits& hits = g_ra->hits;
                std::memset(&hits, 0x6B, sizeof(hits));
                hits.count = n % 23 == 0 ? 32 : n % 17 == 0 ? 30 : 0;
                hits.ignoreFlags = n % 5 == 1 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hits.requireFlags = n % 5 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                bool result = placed ? rectInstance(instance, &rect, &box, &hits) : rectSector(sector, &rect, &box, &hits);
                found += hits.count > 0 && hits.count != 32;
                Record({static_cast<uint32_t>(result)});
                RaWords(&hits, sizeof(hits));
            }
            geometry->m_bsp = nullptr;
            for (int n = 0; n < 50; ++n) {
                Nx::CollRect rect = RqRect(random, {RtSpread(random, 300.0f), 0, 0, 1}, 100.0f), local;
                RaFn<void(__thiscall*)(void*, const Nx::CollRect*, Nx::CollRect*)>(0x00410AA0)(instance, &rect, &local);
                RaWords(&local, sizeof(local));
            }
        }
        if (original)
            Log("rect queries: %d with hits", found);

        // the world query: fake objects through a cache or the grids
        for (int i = 0; i < kRqFakes; ++i) {
            g_ra->fakes[i] = {g_rqVtable, 0xFA4E0000u + i, 0, 0, 0, 0};
            g_ra->fakeBoxes[i].min = {RtSpread(random, 100.0f), RtSpread(random, 100.0f), RtSpread(random, 100.0f), 1};
            g_ra->fakeBoxes[i].max = {g_ra->fakeBoxes[i].min.x + 40, g_ra->fakeBoxes[i].min.y + 40,
                                      g_ra->fakeBoxes[i].min.z + 40, 1};
            g_ra->list[i] = reinterpret_cast<uint32_t>(&g_ra->fakes[i]);
        }
        g_ra->list[kRqFakes] = 0;
        auto* cache = reinterpret_cast<Nx::CCollCache*>(g_ra->cache);
        cache->m_box.min = {-150, -150, -150, 1};
        cache->m_box.max = {150, 150, 150, 1};
        for (int i = 0; i < kRqFakes; ++i) {
            cache->m_entries[i].object = reinterpret_cast<Nx::CCollObj*>(&g_ra->fakes[i]);
            cache->m_entries[i].geometry = &g_ra->fakeBoxes[i];
        }
        cache->m_statics = 6;
        cache->m_movables = 2;
        auto world = RaFn<bool(__cdecl*)(const Nx::CollRect*, Nx::RectHits*, Nx::CCollCache*)>(0x0040FEE0);
        for (int n = 0; n < 200; ++n) {
            Nx::CollRect rect = RqRect(random, {RtSpread(random, 100.0f), RtSpread(random, 100.0f), 0, 1},
                                       n % 4 == 0 ? 300.0f : 30.0f);
            g_raGrid = n % 5 == 0 ? 0u : 0x6D1D0000u;
            for (int k = 0; k < 4; ++k)
                g_ra->grids[k] = k < static_cast<int>(n % 4) ? 0x6D1D0000u + k : 0u;
            Nx::RectHits& hits = g_ra->hits;
            std::memset(&hits, 0x6B, sizeof(hits));
            hits.count = 9;
            Record({static_cast<uint32_t>(world(&rect, &hits, n % 3 ? cache : nullptr)), static_cast<uint32_t>(hits.count)});
        }

        // the rect feeler
        auto* feeler = reinterpret_cast<Nx::CRectFeeler*>(g_ra->feeler);
        Record({RaWhere(reinterpret_cast<uint32_t>(RaFn<void*(__thiscall*)(void*)>(0x004182D0)(feeler)))});
        RaWords(feeler, sizeof(Nx::CRectFeeler));
        for (int n = 0; n < 20; ++n) {
            feeler->m_rect = RqRect(random, {RtSpread(random, 100.0f), 0, RtSpread(random, 100.0f), 1}, 40.0f);
            feeler->mp_cache = n % 2 ? cache : nullptr;
            GameVariable<Nx::CCollCache*>(0x0069198C) = n % 4 == 0 ? cache : nullptr;
            g_raGrid = 0x6D1D0000u;
            Record({RaFn<uint32_t(__thiscall*)(void*, int)>(0x00417840)(feeler, 77) & 0xFFu,
                    static_cast<uint32_t>(feeler->m_hits.count)});
        }
        for (int n = 0; n < 400; ++n)
            RqJoinHits(random, feeler);
        RaFn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, int)>(0x00417860)(feeler, 1, 2, 3, 4);

        // the copies and small pieces
        Nx::RectHit copy;
        std::memset(&copy, 0x11, sizeof(copy));
        Record({RaWhere(reinterpret_cast<uint32_t>(RaFn<void*(__thiscall*)(void*, const void*)>(0x00417AD0)(
            &copy, &feeler->m_joined[0])))});
        RaWords(&copy, sizeof(copy));
        std::memset(&copy, 0x22, sizeof(copy));
        RaFn<void*(__thiscall*)(void*, const void*)>(0x00417B60)(&copy, &feeler->m_hits.hits[1]);
        RaWords(&copy, sizeof(copy));
        RaFn<void(__thiscall*)(void*, uint32_t)>(0x0040FEC0)(sector, 0xC4EC4u);
        sector->m_geometry = nullptr;
        RaFn<void(__thiscall*)(void*, uint32_t)>(0x0040FEC0)(sector, 0xC4EC5u);
        Record({sector->m_checksum, g_ra->geometry[1].m_checksum});
        for (int g = 0; g < 2; ++g) {
            uint32_t next = reinterpret_cast<uint32_t>(
                RaFn<uint8_t*(__thiscall*)(void*, uint8_t*, const void*)>(0x00410BC0)(sector, g_ra->heap, &g_ra->geometry[g]));
            Record({RaWhere(next), RaWhere(reinterpret_cast<uint32_t>(sector->m_buffer)),
                    static_cast<uint32_t>(sector->m_unknown14)});
        }
        Vector turned{1.5f, -2.25f, 3.0f, 7.0f};
        Mth::Matrix m = RqRotation(random);
        Record({RaWhere(reinterpret_cast<uint32_t>(
            RaFn<void*(__thiscall*)(void*, const Mth::Matrix*)>(0x00411840)(&turned, &m)) == reinterpret_cast<uint32_t>(&turned))});
        RaWords(&turned, sizeof(turned));
        EndRunPatches();
        return g_trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(g_ra, 0, MEM_RELEASE);
    delete &s;
    Log("rect queries: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx rect queries and the rect feeler");
}
SELFTEST(Early, TestAnimCollRectQueries, TestStandIns::RemoveStandIns);

} // namespace
