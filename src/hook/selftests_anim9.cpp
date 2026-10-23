// Self-tests for AN-9: the collision geometry turned and scaled, the shared face table, the box's clone, the
// slots that pass on to the geometry and the movable collision factory (src/game/nx/coll_tri.cpp,
// coll_movable.cpp, coll_static.cpp) - the original and ours on identical memory; allocations, frees, the calls
// out and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_static.h"
#include "game/gfx/engine_glue.h"
#include "game/nx/coll_tri.h"
#include "game/nx/coll_world.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Matrix;
using Mth::Vector;
using Nx::CCollBSPNode;
using Nx::CCollObjTriData;

struct TurnArena {
    alignas(16) uint8_t heap[0x3000];
    CCollObjTriData geometry[3];
    float plain[8][3];
    uint16_t packed[8][3];
    uint32_t nodes[2][1 + 3 * 2];
    uint8_t faces[4 * 10];
    uint16_t lists[2][6];
    Vector vectors[8];
    uint8_t objects[2][0xD8];
    uint8_t manager[0x200];
};
TurnArena g_turn;
uint32_t g_turnUsed;
bool g_turnOriginal;

template <typename F>
F TurnFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_turnOriginal, address));
}
float TurnRand(Lcg& random) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 613.0f; }
uint32_t TurnBump(uint32_t size)
{
    uint32_t at = reinterpret_cast<uint32_t>(g_turn.heap) + g_turnUsed;
    g_turnUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}
uint32_t P(const void* p) { return reinterpret_cast<uint32_t>(p); }

// A three-node tree: a root split on x (sides swapped) over two leaves.
CCollBSPNode* SmallTree(int t)
{
    uint32_t* memory = g_turn.nodes[t];
    memory[0] = 3;
    auto* nodes = reinterpret_cast<CCollBSPNode*>(memory + 1);
    nodes[0].m_split = ((40 - 24 * t) << 2) | (t == 0 ? 0 : 2);
    nodes[0].m_link = P(&nodes[1]) | 1;
    for (int i = 0; i < 2; ++i) {
        nodes[1 + i].m_split = 3 | ((2 + i) << 16);
        nodes[1 + i].m_link = P(g_turn.lists[t] + i * 2);
    }
    for (int i = 0; i < 6; ++i)
        g_turn.lists[t][i] = static_cast<uint16_t>((i + t) % 4);
    return nodes;
}

// Each quarter turn (0-4) and a scale (one factor negative) of plain and compressed geometry with and without a
// tree, around a centre; the face table filled once; the box's clone; each forwarding slot; the factory for each
// type and count.
void TestAnimCollTurnScale()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006918EC, 0x24}, {0x0067C6B8, 0x1000}});
    StandIns& s = *new StandIns();
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        return TurnBump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xC7A00000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xC7A10000u});
        return 0u;
    });
    s.At({0x0048DCD0}, 8, [](Regs& r) {
        Record({0xD4DC0000u, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00414FE0, AddressOf(&Nx::CCollObjTriData::AddOcclusionPolys)}, 0, [](Regs& r) {
        Record({0x414FE000u, r.ecx});
        return 0u;
    });
    // (the original's second argument - `value`, unused by RegisterMovable - isn't passed by ours: not recorded)
    s.At({0x004177B0, AddressOf(&Nx::RegisterMovable)}, 0, [](Regs& r) {
        Record({0x4177B000u, r.args[0]});
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_turnOriginal = original;
        g_trace.clear();
        std::memset(&g_turn, 0xA5, sizeof(g_turn));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        GameVariable<void*>(0x007D1800) = g_turn.manager;
        *reinterpret_cast<uint32_t*>(g_turn.manager + 0x110) = 0x70D0110;
        *reinterpret_cast<uint32_t*>(g_turn.manager + 0x114) = 0x80770114;
        GameVariable<bool>(0x007CC979) = true;
        g_turnUsed = 0;
        Lcg random(0x4164F0);
        for (auto& v : g_turn.plain)
            for (float& f : v)
                f = TurnRand(random);
        for (auto& v : g_turn.packed)
            for (uint16_t& c : v)
                c = static_cast<uint16_t>(random.Below(500));
        for (int f = 0; f < 4; ++f) {
            auto* face = reinterpret_cast<uint16_t*>(g_turn.faces + f * 10);
            for (int k = 0; k < 5; ++k)
                face[k] = static_cast<uint16_t>(k < 2 ? random.Next() : random.Below(8));
        }
        for (Vector& v : g_turn.vectors)
            v = {TurnRand(random), TurnRand(random), TurnRand(random), TurnRand(random)};
        g_turn.vectors[1] = {1.25f, -0.5f, 3.0f, 2.0f}; // a scale
        CCollObjTriData& a = g_turn.geometry[0];
        CCollObjTriData& b = g_turn.geometry[1];
        CCollObjTriData& c = g_turn.geometry[2];
        std::memset(&a, 0, sizeof(a) * 3);
        a.m_checksum = 0xA1;
        a.m_numVertices = 8;
        a.m_numFaces = 4;
        a.m_min = {-17, -16, -15, 1};
        a.m_max = {16, 17, 18, 1};
        a.m_vertices = g_turn.plain;
        a.m_faces = g_turn.faces;
        a.m_bsp = SmallTree(0);
        a.m_unknown3C = 0x3C3C;
        b = a;
        b.m_checksum = 0xB1;
        b.m_compressed = true;
        b.m_min = {-6.5f, -2.0f, -11.25f, 1};
        b.m_max = {25, 30, 20, 1};
        b.m_vertices = g_turn.packed;
        b.m_bsp = SmallTree(1);
        if (!original)
            UseOurCollisionVtables();
        auto words = [](const void* p, int n) {
            const auto* w = static_cast<const uint32_t*>(p);
            g_trace.insert(g_trace.end(), w, w + n);
        };
        auto state = [&]() {
            words(&a, 32);
            words(g_turn.plain, 24);
            words(g_turn.packed, 12);
            words(g_turn.nodes, sizeof(g_turn.nodes) / 4);
        };
        auto turn = TurnFn<void(__thiscall*)(void*, const Vector*, int)>(0x004164F0);
        auto scale = TurnFn<void(__thiscall*)(void*, const Vector*, const Vector*)>(0x00416730);
        for (int turns = 0; turns <= 4; ++turns) {
            turn(&a, &g_turn.vectors[0], turns);
            turn(&b, &g_turn.vectors[2], turns);
            state();
        }
        a.m_unknown3C = 0x3C3D;
        b.m_unknown3C = 0x3C3E;
        scale(&a, &g_turn.vectors[3], &g_turn.vectors[1]);
        scale(&b, &g_turn.vectors[4], &g_turn.vectors[1]);
        state();
        a.m_bsp = nullptr;
        b.m_bsp = nullptr;
        turn(&a, &g_turn.vectors[5], 1);
        scale(&b, &g_turn.vectors[5], &g_turn.vectors[1]);
        state();
        // the shared face table: filled while its first entry isn't 0
        auto* table = reinterpret_cast<uint16_t*>(0x0067C6B8);
        std::memset(table, 0x77, 0x1000);
        table[0] = 0xFFFF;
        auto setUp = TurnFn<bool(__thiscall*)(void*)>(0x00414D30);
        Record({setUp(&a)});
        words(table, 0x400);
        table[3] = 0x4444;
        b.m_bsp = reinterpret_cast<CCollBSPNode*>(g_turn.nodes[1] + 1);
        Record({setUp(&b)});
        words(table, 4);
        table[0] = 0x7777; // any first entry but 0 fills it again
        table[3] = 0x4444;
        Record({setUp(&b)});
        words(table, 4);
        // the box's clone
        auto* box = reinterpret_cast<Nx::CCollMovBBox*>(g_turn.objects[0]);
        TurnFn<void*(__thiscall*)(void*)>(0x00412550)(box);
        box->m_geometry = &a;
        box->m_flags = 0x0211;
        box->m_faceFlags = 0x12345;
        box->m_terrain = 0x66;
        void* copy = TurnFn<void*(__thiscall*)(void*, int)>(0x00414BE0)(box, 7);
        Record({P(copy)});
        words(copy, 0xD0 / 4);
        // the forwarding slots
        auto* holder = reinterpret_cast<Nx::CCollStatic*>(g_turn.objects[1]);
        std::memset(holder, 0, 0x18);
        holder->m_vtable = reinterpret_cast<void**>(0x00645C18);
        a.m_bsp = reinterpret_cast<CCollBSPNode*>(g_turn.nodes[0] + 1);
        holder->m_geometry = &a;
        TurnFn<void(__thiscall*)(void*, const Vector*, int)>(0x0040AE80)(holder, &g_turn.vectors[7], 2);
        TurnFn<void(__thiscall*)(void*, const Vector*, const Vector*)>(0x0040AE90)(holder, &g_turn.vectors[7],
                                                                               &g_turn.vectors[1]);
        state();
        TurnFn<void(__thiscall*)(void*, int, int)>(0x0040AEA0)(holder, 11, 12);
        TurnFn<void(__thiscall*)(void*)>(0x0040AEB0)(holder);
        Vector inside{-30, 0, 0, 1};
        Record({TurnFn<bool(__thiscall*)(void*, const Vector*, int)>(0x0040AEC0)(holder, &inside, 5)});
        words(reinterpret_cast<void*>(0x0069196C), 1);
        // the factory: boxes round 0, 1, 3 geometries; triangle objects of 0, 1, 3; another type
        auto make = TurnFn<void*(__cdecl*)(int, CCollObjTriData*, int, int)>(0x00414990);
        c = b;
        c.m_min = {-40, 2, -1, 1};
        c.m_max = {3, 50, 4, 0.5f};
        for (int type : {1, 4, 2, 0}) {
            for (int count : {0, 1, 3}) {
                void* made = make(type, &a, count, 0x100 * type + count);
                Record({P(made)});
                if (made)
                    words(made, (type == 1 ? 0xD0 : count == 1 ? 0xD4 : 0x3C) / 4);
            }
        }
        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* all = reinterpret_cast<const uint32_t*>(&g_turn);
        trace.insert(trace.end(), all, all + sizeof(g_turn) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx geometry turned / scaled, face table, box clone, forwarders, factory");
}
SELFTEST(Early, TestAnimCollTurnScale, TestStandIns::RemoveStandIns);

// Occlusion polygons from a 4x4 grid of vertices (some lifted, a few just within or beyond the 0.05 normal
// tolerance) split into triangle pairs in shuffled corner orders and windings, with stray, repeated and degenerate
// triangles; plain with 10-byte faces and compressed with 8-byte faces. The quads handed on are recorded.
struct OccArena {
    float plain[276][3];
    uint16_t packed[276][3];
    uint8_t faces10[48 * 10];
    uint8_t faces8[48 * 8];
    CCollObjTriData geometry[2];
};
OccArena g_occ;
bool g_occOriginal;

void TestAnimCollOcclusion()
{
    StandIns& s = *new StandIns();
    s.At({0x004B22C0, AddressOf(&Nx::AddDebugShape)}, 0, [](Regs& r) {
        Record({0x0CC10000u, r.args[0], r.args[2]});
        const auto* w = reinterpret_cast<const uint32_t*>(r.args[1]);
        g_trace.insert(g_trace.end(), w, w + 16);
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_occOriginal = original;
        g_trace.clear();
        std::memset(&g_occ, 0, sizeof(g_occ));
        Lcg random(0x414FE0);
        // (a lift of 0.16 tilts a triangle's normal by about 0.04: within the 0.05 tolerance, beyond 0.03)
        const float lifts[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.03f, 0.4f, 0.0f, 0.0f, 0.16f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f};
        for (int v = 0; v < 16; ++v) {
            g_occ.plain[v][0] = static_cast<float>(v % 4) * 4.0f;
            g_occ.plain[v][1] = static_cast<float>(v / 4) * 4.0f;
            g_occ.plain[v][2] = lifts[v];
        }
        // a tilted plane's quad T0..T3 (z = x / 2 + 10) at 272..275, and the same points at 16..19 (the same low
        // byte): corner indices compared as 16 bits
        const float tilted[4][3] = {{20, 0, 20}, {24, 0, 22}, {24, 4, 22}, {20, -4, 20}};
        for (int t = 0; t < 4; ++t) {
            std::memcpy(g_occ.plain[272 + t], tilted[t], 12);
            std::memcpy(g_occ.plain[16 + t], tilted[t], 12);
        }
        for (int v = 0; v < 276; ++v)
            for (int k = 0; k < 3; ++k)
                g_occ.packed[v][k] = static_cast<uint16_t>((g_occ.plain[v][k] + 8.0f) * 16.0f);
        int faces = 0;
        auto add = [&](int c0, int c1, int c2) {
            auto* wide = reinterpret_cast<uint16_t*>(g_occ.faces10 + faces * 10);
            uint8_t* small = g_occ.faces8 + faces * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            const int corners[3] = {c0, c1, c2};
            for (int k = 0; k < 3; ++k) {
                wide[2 + k] = static_cast<uint16_t>(corners[k]);
                small[4 + k] = static_cast<uint8_t>(corners[k]);
            }
            ++faces;
        };
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                int v00 = y * 4 + x, v10 = v00 + 1, v01 = v00 + 4, v11 = v00 + 5;
                int t1[3] = {v00, v10, v11}, t2[3] = {v00, v11, v01};
                if (random.Below(2)) {
                    t1[0] = v00; t1[1] = v10; t1[2] = v01;
                    t2[0] = v10; t2[1] = v11; t2[2] = v01;
                }
                int r1 = static_cast<int>(random.Below(3)), r2 = static_cast<int>(random.Below(3));
                bool flip = random.Below(5) == 0;
                add(t1[r1], t1[(r1 + 1) % 3], t1[(r1 + 2) % 3]);
                if (flip)
                    add(t2[r2], t2[(r2 + 2) % 3], t2[(r2 + 1) % 3]);
                else
                    add(t2[r2], t2[(r2 + 1) % 3], t2[(r2 + 2) % 3]);
            }
        }
        add(0, 5, 10);  // a stray triangle across the grid
        add(5, 5, 6);   // degenerate
        add(0, 1, 5);   // a repeat of a corner pair
        add(15, 14, 10);
        add(1, 1, 0);   // repeated corners
        add(3, 2, 6);
        // the pairs shuffled
        for (int f = faces - 1; f > 0; --f) {
            int g = static_cast<int>(random.Below(static_cast<uint32_t>(f + 1)));
            uint8_t tmp[10];
            std::memcpy(tmp, g_occ.faces10 + f * 10, 10);
            std::memcpy(g_occ.faces10 + f * 10, g_occ.faces10 + g * 10, 10);
            std::memcpy(g_occ.faces10 + g * 10, tmp, 10);
            std::memcpy(tmp, g_occ.faces8 + f * 8, 8);
            std::memcpy(g_occ.faces8 + f * 8, g_occ.faces8 + g * 8, 8);
            std::memcpy(g_occ.faces8 + g * 8, tmp, 8);
        }
        // at the end, in order: a collinear face and a degenerate one sharing two corners (both normals zero); the
        // tilted quad's two triangles with face i's shared corners at 0 and 1, 1 and 2, 0 and 2; two faces
        // sharing a corner and a corner whose index differs only above the low byte; a last adjacent pair
        add(0, 1, 2);
        add(1, 1, 2);
        add(16, 273, 274);
        add(272, 274, 275);
        add(272, 273, 274);
        add(273, 272, 275);
        add(274, 272, 273);
        add(273, 272, 275);
        add(273, 274, 272);
        add(273, 272, 275);
        CCollObjTriData& plain = g_occ.geometry[0];
        plain.m_checksum = 0x0CC1A;
        plain.m_numVertices = 276;
        plain.m_numFaces = static_cast<uint16_t>(faces);
        plain.m_faces = g_occ.faces10;
        plain.m_vertices = g_occ.plain;
        CCollObjTriData& packed = g_occ.geometry[1];
        packed = plain;
        packed.m_checksum = 0x0CC1B;
        packed.m_smallFaces = true;
        packed.m_compressed = true;
        packed.m_faces = g_occ.faces8;
        packed.m_vertices = g_occ.packed;
        packed.m_min = {-8.0f, -8.0f, -8.0f, 1.0f};
        auto occlude = reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00414FE0));
        for (CCollObjTriData& g : g_occ.geometry) {
            occlude(&g);
            Record({0xE0D00000u});
        }
        uint16_t numFaces = plain.m_numFaces;
        for (uint16_t n = 0; n < 3; ++n) {
            plain.m_numFaces = n;
            occlude(&plain);
            Record({0xE0D10000u + n});
        }
        plain.m_numFaces = numFaces;
        return g_trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    Log("occlusion polys: %u trace words", static_cast<unsigned>(expected.size()));
    Check(expected.size() > 100, "occlusion polys: the test geometry makes quads");
    CompareTraces(expected, actual, "Nx geometry occlusion polygons");
}
SELFTEST(Early, TestAnimCollOcclusion, TestStandIns::RemoveStandIns);

} // namespace
