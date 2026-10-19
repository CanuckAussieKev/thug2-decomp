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
#include "game/nx/coll_tri.h"
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
    s.At({0x00414FE0}, 0, [](Regs& r) {
        Record({0x414FE000u, r.ecx});
        return 0u;
    });
    s.At({0x004177B0}, 0, [](Regs& r) {
        Record({0x4177B000u, r.args[0], r.args[1]});
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

} // namespace
