// Self-tests for AN-14: the triangle collision objects' line tests (a sector's, an instance's, a movable one's) and
// the line into a scaled object's space (src/game/nx/coll_line.cpp, coll_movable.cpp) - the original and ours on
// identical meshes, placements and lines; the records and results compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_line.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Matrix;
using Mth::Vector;
using Nx::CCollObjTriData;
using Nx::CollisionHit;

constexpr int kVertices = 40;
constexpr int kFaces = 60;

struct TriLineArena {
    alignas(16) uint8_t objects[3][0xD8];
    CCollObjTriData geometry[2];
    float plain[kVertices][3];
    uint16_t packed[kVertices][3];
    uint8_t faces10[kFaces * 10];
    uint8_t faces8[kFaces * 8];
    uint8_t rejection[kFaces];
    uint32_t nodes[2][1 + 3 * 2];
    uint16_t lists[2][kFaces * 2];
    CollisionHit hit;
};
TriLineArena g_tl;
bool g_tlOriginal;

template <typename F>
F TlFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_tlOriginal, address));
}
float TlRand(Lcg& random, float range) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range; }

void RecordRecord(const CollisionHit& hit)
{
    const auto* w = reinterpret_cast<const uint32_t*>(&hit);
    for (int k = 0; k < 0x50 / 4; ++k) {
        uint32_t word = w[k];
        if (k == 0x2C / 4)
            word &= 0x00FFFFFF;
        if (k == 0x48 / 4)
            word &= 0x0000FFFF;
        g_trace.push_back(word);
    }
}

Matrix Rotation(Lcg& random)
{
    float yaw = TlRand(random, 3.1f), pitch = TlRand(random, 1.5f), roll = TlRand(random, 3.1f);
    float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll),
          sr = std::sin(roll);
    Matrix m;
    m.row[0] = {cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr, 0.0f};
    m.row[1] = {-cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr, 0.0f};
    m.row[2] = {sy * cp, -sp, cy * cp, 0.0f};
    m.row[3] = {0, 0, 0, 1};
    return m;
}

// A random mesh (plain with 10-byte faces, compressed with 8-byte faces; per-face rejection bytes - the sector's
// buffer, the small faces' last byte - and a rejection scale), a sector over it, an instance of it (scaled,
// turned, placed) and a movable triangle object (turned, placed); lines through and around the mesh with the
// record's distance and flags varied; the instance's line into its space.
void TestAnimCollTriangleLines()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x00691910, 1}, {0x0067C6B8, 0x1000}, {0x00691968, 12}});
    auto runOnce = [&](bool original) {
        g_tlOriginal = original;
        g_trace.clear();
        std::memset(&g_tl, 0xA1, sizeof(g_tl));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        auto* table = reinterpret_cast<uint16_t*>(0x0067C6B8);
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
        GameVariable<bool>(0x00691910) = false;
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x413B50);
        for (int v = 0; v < kVertices; ++v) {
            for (int k = 0; k < 3; ++k) {
                g_tl.plain[v][k] = TlRand(random, 40.0f);
                g_tl.packed[v][k] = static_cast<uint16_t>((g_tl.plain[v][k] + 50.0f) * 16.0f);
            }
        }
        for (int f = 0; f < kFaces; ++f) {
            auto* wide = reinterpret_cast<uint16_t*>(g_tl.faces10 + f * 10);
            uint8_t* small = g_tl.faces8 + f * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            small[0] = static_cast<uint8_t>(wide[0]);
            small[1] = static_cast<uint8_t>(wide[0] >> 8);
            small[2] = static_cast<uint8_t>(wide[1]);
            small[3] = static_cast<uint8_t>(wide[1] >> 8);
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(random.Below(kVertices));
                wide[2 + k] = corner;
                small[4 + k] = static_cast<uint8_t>(corner);
            }
            g_tl.rejection[f] = f % 7 == 3 ? 255 : static_cast<uint8_t>(random.Below(40));
            small[7] = static_cast<uint8_t>(random.Below(40));
        }
        float scale = 0.7f;
        // a tree for each geometry (used for the second half of its lines): a root split on x at 0, the faces with
        // a corner below it in the less leaf, those with a corner at or above it in the greater one
        auto tree = [&](int g, const CCollObjTriData& geometry) {
            uint32_t* memory = g_tl.nodes[g];
            memory[0] = 3;
            auto* nodes = reinterpret_cast<Nx::CCollBSPNode*>(memory + 1);
            uint16_t* less = g_tl.lists[g];
            uint16_t* greater = less + kFaces;
            int lessCount = 0, greaterCount = 0;
            for (int f = 0; f < kFaces; ++f) {
                bool below = false, above = false;
                for (int k = 0; k < 3; ++k) {
                    float x = g_tl.plain[geometry.FaceVertex(f, k)][0];
                    (x < 0.0f ? below : above) = true;
                }
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
            CCollObjTriData& geometry = g_tl.geometry[g];
            std::memset(&geometry, 0, sizeof(geometry));
            geometry.m_checksum = 0x6E0A0000u + g;
            geometry.m_numVertices = kVertices;
            geometry.m_numFaces = kFaces;
            geometry.m_smallFaces = g == 1;
            geometry.m_compressed = g == 1;
            geometry.m_faces = g == 1 ? g_tl.faces8 : g_tl.faces10;
            geometry.m_min = {-50, -50, -50, 1};
            geometry.m_max = {50, 50, 50, 1};
            geometry.m_vertices = g == 1 ? static_cast<void*>(g_tl.packed) : static_cast<void*>(g_tl.plain);
            std::memcpy(&geometry.m_unknown3C, &scale, 4);
        }
        // the objects
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_tl.objects[0]);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC7;
        sector->m_buffer = g_tl.rejection;
        auto* instance = reinterpret_cast<Nx::CCollStaticInstance*>(g_tl.objects[1]);
        TlFn<void*(__thiscall*)(void*)>(0x004127C0)(instance);
        auto* movable = reinterpret_cast<Nx::CCollMovTri*>(g_tl.objects[2]);
        auto collideSector = TlFn<bool(__thiscall*)(void*, const Mth::Line*, const Vector*, CollisionHit*, const Mth::BBox*)>(0x00413B50);
        auto collideInstance = TlFn<bool(__thiscall*)(void*, const Mth::Line*, const Vector*, CollisionHit*, const Mth::BBox*)>(0x00413FF0);
        auto collideMovable = TlFn<bool(__thiscall*)(void*, const Mth::Line*, const Vector*, CollisionHit*, const Mth::BBox*)>(0x00414460);
        int hits = 0;
        for (int g = 0; g < 2; ++g) {
            CCollObjTriData* geometry = &g_tl.geometry[g];
            sector->m_geometry = geometry;
            TlFn<void(__thiscall*)(void*, void*)>(0x00411480)(instance, geometry);
            TlFn<void*(__thiscall*)(void*, void*)>(0x00413000)(movable, geometry);
            for (int n = 0; n < 1500; ++n) {
                geometry->m_bsp = n < 750 ? nullptr : tree(g, *geometry);
                if (n % 100 == 0) {
                    Matrix m = Rotation(random);
                    Vector place{TlRand(random, 200.0f), TlRand(random, 200.0f), TlRand(random, 200.0f), 1.0f};
                    Vector size{TlRand(random, 1.0f) + 1.5f, TlRand(random, 1.0f) + 1.5f, TlRand(random, 1.0f) + 1.5f, 1.0f};
                    TlFn<void(__thiscall*)(void*, const Vector*)>(0x00413F50)(instance, &size);
                    TlFn<void(__thiscall*)(void*, const Matrix*)>(0x00413F30)(instance, &m);
                    TlFn<void(__thiscall*)(void*, const Vector*)>(0x00413F00)(instance, &place);
                    Matrix m2 = Rotation(random);
                    TlFn<void(__thiscall*)(void*, const Matrix*)>(0x00412070)(movable, &m2);
                    TlFn<void(__thiscall*)(void*, const Vector*)>(0x00412F90)(movable, &place);
                }
                int which = n % 3;
                // a line through a point in or near the object
                Vector centre = which == 0 ? Vector{0, 0, 0, 1}
                                           : which == 1 ? instance->m_matrix.row[3] : movable->m_position;
                Vector target{centre.x + TlRand(random, 60.0f), centre.y + TlRand(random, 60.0f),
                              centre.z + TlRand(random, 60.0f), 1.0f};
                Vector way{TlRand(random, 1.0f), TlRand(random, 1.0f), TlRand(random, 1.0f), 0.0f};
                float before = TlRand(random, 100.0f) + 120.0f, after = TlRand(random, 100.0f) + 120.0f;
                Mth::Line line;
                line.start = {target.x - way.x * before, target.y - way.y * before, target.z - way.z * before, 1.0f};
                line.end = {target.x + way.x * after, target.y + way.y * after, target.z + way.z * after, 1.0f};
                Vector direction{line.end.x - line.start.x, line.end.y - line.start.y, line.end.z - line.start.z,
                                 0.0f};
                Mth::BBox lineBox(line.start);
                lineBox.AddPoint(line.end);
                CollisionHit& hit = g_tl.hit;
                std::memset(&hit, 0x4E, sizeof(hit));
                hit.distance = n % 4 == 0 ? 0.4f : 2.0f;
                hit.ignoreFlags = n % 5 == 1 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hit.requireFlags = n % 5 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
                hit.hit = false;
                hit.callback = nullptr;
                GameVariable<bool>(0x00691910) = n % 9 == 0;
                if (n % 50 == 7) {
                    float tiny = n % 100 == 7 ? 0.01f : 1e6f; // a scale rejecting nothing / everything
                    std::memcpy(&geometry->m_unknown3C, &tiny, 4);
                }
                bool result = which == 0   ? collideSector(sector, &line, &direction, &hit, &lineBox)
                              : which == 1 ? collideInstance(instance, &line, &direction, &hit, &lineBox)
                                           : collideMovable(movable, &line, &direction, &hit, &lineBox);
                std::memcpy(&geometry->m_unknown3C, &scale, 4);
                hits += result;
                Record({result});
                RecordRecord(hit);
            }
            geometry->m_bsp = nullptr;
            // the sector's rejection threshold at exactly 255 (only faces whose byte is 255 tested): lines along x
            // at 10 from the minimum corner (plain geometry only: there the corner is used for nothing else)
            if (g == 0) {
                float exact = 25.55f;
                std::memcpy(&geometry->m_unknown3C, &exact, 4);
                for (int n = 0; n < 200; ++n) {
                    float a = static_cast<float>(static_cast<int>(random.Below(81)) - 40) * 0.25f;
                    float b = static_cast<float>(static_cast<int>(random.Below(81)) - 40) * 0.25f;
                    geometry->m_min = {0.0f, a + 10.0f, b, 1.0f};
                    Mth::Line line;
                    line.start = {-100.0f, a, b, 1.0f};
                    line.end = {100.0f, a, b, 1.0f};
                    Vector direction{200.0f, 0.0f, 0.0f, 0.0f};
                    Mth::BBox lineBox(line.start);
                    lineBox.AddPoint(line.end);
                    CollisionHit& hit = g_tl.hit;
                    std::memset(&hit, 0x4E, sizeof(hit));
                    hit.distance = 2.0f;
                    hit.ignoreFlags = 0;
                    hit.requireFlags = 0;
                    hit.hit = false;
                    hit.callback = nullptr;
                    GameVariable<bool>(0x00691910) = false;
                    Record({collideSector(sector, &line, &direction, &hit, &lineBox)});
                    RecordRecord(hit);
                }
                std::memcpy(&geometry->m_unknown3C, &scale, 4);
                geometry->m_min = {-50, -50, -50, 1};
            }
            // the instance's line into its space
            for (int n = 0; n < 50; ++n) {
                Mth::Line line, local;
                line.start = {TlRand(random, 300.0f), TlRand(random, 300.0f), TlRand(random, 300.0f), 1.0f};
                line.end = {TlRand(random, 300.0f), TlRand(random, 300.0f), TlRand(random, 300.0f), TlRand(random, 2.0f)};
                TlFn<void(__thiscall*)(void*, const Mth::Line*, Mth::Line*)>(0x00410A30)(instance, &line, &local);
                const auto* w = reinterpret_cast<const uint32_t*>(&local);
                g_trace.insert(g_trace.end(), w, w + 8);
            }
        }
        if (original)
            Log("triangle lines: %d hits of 3000", hits);
        EndRunPatches();
        return g_trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    CompareTraces(expected, actual, "Nx triangle objects' line tests");
}
SELFTEST(Early, TestAnimCollTriangleLines, TestStandIns::RemoveStandIns);

} // namespace
