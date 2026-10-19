// Self-tests for AN-5: the collision object base and the multi-object (src/game/nx/coll_obj.cpp) - the original
// and ours on identical stand-in children; every call the children get, the allocations and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Nx::CCollMulti;
using Nx::CCollObj;

struct MultiArena {
    alignas(16) uint8_t heap[0x4000];
    uint8_t multis[3][0x40];
    uint8_t children[6][0x40];  // stand-in children: +0 vtable, +0x10 a box
    void* childVtable[24];
    uint8_t geometry[0x10];
    Mth::BBox box;
};
MultiArena g_multi;
uint32_t g_multiUsed;
bool g_multiOriginal;
int g_noBoxChild;     // the child whose GetBBox says null (-1: none)
int g_falseChild;     // the child whose IsTriangleCollision / CollideWithLine says false

template <typename F>
F MultiFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_multiOriginal, address));
}
int ChildIndex(uint32_t child) { return static_cast<int>((child - reinterpret_cast<uint32_t>(g_multi.children)) / 0x40); }
CCollMulti* Multi(int i) { return reinterpret_cast<CCollMulti*>(g_multi.multis[i]); }
CCollObj* Child(int i) { return reinterpret_cast<CCollObj*>(g_multi.children[i]); }

// The multi-object: constructed (and a copy), children added (stand-ins and another multi), every slot passed
// down (with a child that says no / has no box), the box recomputed and tested, cloned, deleted (children, nodes,
// the head, the owned geometry; out of the caches), the list nodes' and heads' functions.
void TestAnimCollMulti()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006918EC, 0x24}});
    StandIns& s = *new StandIns();
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        uint32_t at = reinterpret_cast<uint32_t>(g_multi.heap) + g_multiUsed;
        g_multiUsed += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(at), 0xCD, r.args[0]);
        return at;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    s.At({0x00416970, AddressOf(&Nx::CCollObjTriData::Destruct)}, 0, [](Regs& r) {
        Record({0x6E0D0000u, r.ecx});
        return 0u;
    });
    // The children's slots: each records its slot, the child and its arguments.
    const uint16_t pops[24] = {4, 0, 4, 4, 4, 4, 4, 0, 0, 8, 0, 4, 4, 4, 16, 0, 0, 0, 0, 8, 0, 0, 0, 0};
    void* slots[24];
    for (int slot = 0; slot < 24; ++slot) {
        slots[slot] = s.Thunk(pops[slot], [slot, pops](Regs& r) {
            int child = ChildIndex(r.ecx);
            g_trace.push_back(0x5C000000u + slot * 0x100 + child);
            for (int i = 0; i < pops[slot] / 4; ++i)
                g_trace.push_back(r.args[i]);
            switch (slot) {
            case 1: case 14: return static_cast<uint32_t>(child != g_falseChild);
            case 7: return r.ecx + 0x10;
            case 8: return r.ecx + 0x20;
            case 12: return r.ecx; // (a clone stands for itself)
            case 20: return child == g_noBoxChild ? 0u : r.ecx + 0x10;
            default: return 0u;
            }
        });
    }
    auto runOnce = [&](bool original) {
        g_multiOriginal = original;
        g_trace.clear();
        std::memset(&g_multi, 0xEE, sizeof(g_multi));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        GameVariable<void*>(0x007D1800) = g_multi.heap; // (a stand-in manager: Allocate and Free are stood in for)
        GameVariable<bool>(0x007CC979) = true;
        g_multiUsed = 0;
        for (int i = 0; i < 24; ++i)
            g_multi.childVtable[i] = slots[i];
        for (int i = 0; i < 6; ++i) {
            *reinterpret_cast<void***>(g_multi.children[i]) = g_multi.childVtable;
            auto* box = reinterpret_cast<Mth::BBox*>(g_multi.children[i] + 0x10);
            box->min = {static_cast<float>(-i), 2.0f * i, -3.0f, 1.0f};
            box->max = {static_cast<float>(i * i), 5.0f + i, -1.0f - i, 1.0f};
        }
        if (!original)
            UseOurCollisionVtables();
        for (int i = 0; i < 3; ++i) {
            std::memset(Multi(i), 0xA5, 0x40);
            Record({reinterpret_cast<uint32_t>(MultiFn<CCollMulti*(__thiscall*)(CCollMulti*)>(0x004133E0)(Multi(i)))});
        }
        auto add = MultiFn<void(__thiscall*)(CCollMulti*, CCollObj*)>(0x00411BE0);
        for (int i = 0; i < 4; ++i)
            add(Multi(0), Child(i));
        add(Multi(1), Child(4));
        add(Multi(1), Child(5));
        CCollMulti* m = Multi(0);
        g_noBoxChild = -1;
        g_falseChild = -1;
        Record({MultiFn<bool(__thiscall*)(CCollMulti*)>(0x00411D90)(m)});
        g_falseChild = 1;
        Record({MultiFn<bool(__thiscall*)(CCollMulti*)>(0x00411D90)(m)});
        Record({MultiFn<bool(__thiscall*)(CCollMulti*)>(0x00411D90)(Multi(2))}); // (no children: true)
        auto getBox = MultiFn<Mth::BBox*(__thiscall*)(CCollMulti*)>(0x00411E50);
        getBox(m);
        MultiFn<void(__thiscall*)(CCollMulti*, void*)>(0x00411CC0)(m, reinterpret_cast<void*>(0x6E0));
        Record({m->m_dirty});
        getBox(m);
        MultiFn<void(__thiscall*)(CCollMulti*, const void*)>(0x00411C30)(m, reinterpret_cast<void*>(0x905));
        Record({m->m_dirty});
        getBox(m);
        MultiFn<void(__thiscall*)(CCollMulti*, const void*)>(0x00411C80)(m, reinterpret_cast<void*>(0x0A1));
        Record({m->m_dirty});
        MultiFn<void(__thiscall*)(CCollMulti*, const void*)>(0x00411C80)(Multi(2), reinterpret_cast<void*>(0x0A2));
        Record({reinterpret_cast<uint32_t>(MultiFn<void*(__thiscall*)(CCollMulti*)>(0x00411F60)(m)),
                reinterpret_cast<uint32_t>(MultiFn<void*(__thiscall*)(CCollMulti*)>(0x00411F80)(m))});
        Mth::Vector v{1, 2, 3, 4};
        MultiFn<Mth::Vector*(__thiscall*)(CCollMulti*, Mth::Vector*, int)>(0x00410960)(m, &v, 3);
        Record({reinterpret_cast<uint32_t&>(v.x), reinterpret_cast<uint32_t&>(v.w)});
        MultiFn<void(__thiscall*)(CCollMulti*, int)>(0x0040AE10)(m, 0x38);
        Record({static_cast<uint32_t>(MultiFn<int(__thiscall*)(CCollMulti*)>(0x0040AE20)(m))});
        Record({MultiFn<uint16_t(__thiscall*)(void*, int)>(0x0040AE30)(m, 5)});
        // The box: from the children (dirty), tested against boxes inside / outside; a child without one.
        auto within = MultiFn<int(__thiscall*)(CCollMulti*, const Mth::BBox*)>(0x00411E80);
        g_multi.box.min = {100, 100, 100, 1};
        g_multi.box.max = {200, 200, 200, 1};
        Record({static_cast<uint32_t>(within(m, &g_multi.box))});
        g_multi.box.min = {0, 0, -2, 1};
        g_multi.box.max = {1, 1, 0, 1};
        Record({static_cast<uint32_t>(within(m, &g_multi.box))});
        Record({reinterpret_cast<uint32_t>(MultiFn<Mth::BBox*(__thiscall*)(CCollMulti*)>(0x00411E50)(m))});
        g_noBoxChild = 2;
        MultiFn<void(__thiscall*)(CCollMulti*, const void*)>(0x00411C30)(m, nullptr); // (dirty again)
        Record({reinterpret_cast<uint32_t>(MultiFn<Mth::BBox*(__thiscall*)(CCollMulti*)>(0x00411E50)(m))});
        Record({static_cast<uint32_t>(within(m, &g_multi.box))});
        g_multi.box.min = {100, 100, 100, 1}; // (no box: anything may touch)
        g_multi.box.max = {200, 200, 200, 1};
        Record({static_cast<uint32_t>(within(m, &g_multi.box))});
        MultiFn<void(__thiscall*)(CCollMulti*)>(0x00411DD0)(Multi(2)); // (no children: an empty valid box)
        g_noBoxChild = -1;
        auto collide = MultiFn<bool(__thiscall*)(CCollMulti*, void*, void*, void*, void*)>(0x00411EC0);
        Record({collide(m, (void*)1, (void*)2, (void*)3, (void*)4)});
        g_falseChild = 0;
        Record({collide(Multi(1), (void*)5, (void*)6, (void*)7, (void*)8)});
        g_falseChild = 4;
        Record({collide(Multi(1), (void*)9, (void*)10, (void*)11, (void*)12)});
        Record({collide(Multi(2), (void*)9, (void*)10, (void*)11, (void*)12)});
        MultiFn<void(__thiscall*)(CCollMulti*, void*, void*)>(0x00411F20)(m, (void*)0x19, (void*)0x20);
        // Clone, copy.
        CCollMulti* clone = MultiFn<CCollMulti*(__thiscall*)(CCollMulti*, int)>(0x00413490)(m, 0x77);
        Record({reinterpret_cast<uint32_t>(clone), clone->m_flags});
        auto* copy = reinterpret_cast<CCollMulti*>(g_multi.multis[2]);
        Record({reinterpret_cast<uint32_t>(MultiFn<CCollMulti*(__thiscall*)(CCollMulti*, const CCollMulti*)>(0x00411D10)(copy, Multi(1)))});
        // Destroyed: a multi with a nested multi child and an owned geometry; the base deleting destructor.
        add(Multi(1), reinterpret_cast<CCollObj*>(clone));
        clone->m_geometry = reinterpret_cast<Nx::CCollObjTriData*>(g_multi.geometry);
        clone->m_flags = 0x8000;
        Multi(1)->m_geometry = reinterpret_cast<Nx::CCollObjTriData*>(g_multi.geometry);
        Multi(1)->m_flags = 0xC000; // (shared: not deleted)
        // A registered cache holding the multis: deleting them takes them out.
        auto* cache = reinterpret_cast<Nx::CCollCache*>(g_multi.heap + 0x2000);
        std::memset(cache, 0, sizeof(Nx::CCollCache));
        cache->m_entries[0] = {reinterpret_cast<Nx::CCollObj*>(Multi(0)), nullptr};
        cache->m_entries[1] = {reinterpret_cast<Nx::CCollObj*>(Multi(1)), nullptr};
        cache->m_entries[2] = {reinterpret_cast<Nx::CCollObj*>(Multi(0)), nullptr};
        cache->m_count = 3;
        cache->m_statics = 2;
        cache->m_movables = 1;
        Nx::RegisterCollCache(cache);
        Record({reinterpret_cast<uint32_t>(MultiFn<CCollMulti*(__thiscall*)(CCollMulti*, uint8_t)>(0x00413460)(Multi(1), 1))});
        m->m_geometry = reinterpret_cast<Nx::CCollObjTriData*>(g_multi.geometry);
        m->m_flags = 0x0001; // (not owned: not deleted)
        MultiFn<void(__thiscall*)(CCollMulti*)>(0x00411B10)(m);
        auto* base = reinterpret_cast<CCollObj*>(g_multi.multis[2]);
        base->m_geometry = reinterpret_cast<Nx::CCollObjTriData*>(g_multi.geometry);
        base->m_flags = 0x8001;
        Record({reinterpret_cast<uint32_t>(MultiFn<CCollObj*(__thiscall*)(CCollObj*, uint8_t)>(0x00413390)(base, 0))});
        Record({reinterpret_cast<uint32_t>(MultiFn<CCollObj*(__thiscall*)(CCollObj*, uint8_t)>(0x00413390)(Child(3), 1))});
        MultiFn<void(__thiscall*)(CCollObj*, void*)>(0x0040B370)(base, g_multi.geometry);
        MultiFn<void(__thiscall*)(CCollObj*, void*)>(0x0040B370)(base, nullptr);
        // The list's own functions on a loose head and node.
        auto* head = reinterpret_cast<Mem::ListNode*>(g_multi.children[5]);
        Record({reinterpret_cast<uint32_t>(MultiFn<Mem::ListNode*(__thiscall*)(void*)>(0x00413180)(head))});
        auto* node = reinterpret_cast<Mem::ListNode*>(g_multi.children[4]);
        node->prev = head;
        node->next = head;
        head->next = node;
        head->prev = node;
        MultiFn<void(__thiscall*)(void*)>(0x004131D0)(node);
        MultiFn<void*(__thiscall*)(void*, uint8_t)>(0x00411880)(node, 1);
        MultiFn<void*(__thiscall*)(void*, uint8_t)>(0x004131A0)(head, 0);
        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_multi);
        trace.insert(trace.end(), words, words + sizeof(g_multi) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx::CCollMulti (children, box, clone, destroy) and CCollObj's base");
}
SELFTEST(Early, TestAnimCollMulti, TestStandIns::RemoveStandIns);

} // namespace
