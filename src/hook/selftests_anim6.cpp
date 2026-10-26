// Self-tests for AN-6: the movable / movable-box / scaled collision objects (src/game/nx/coll_movable.cpp) and the
// geometry's raw vertices (src/game/nx/coll_tri.cpp) - the original and ours on identical stand-in state.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_movable.h"
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

struct MovArena {
    alignas(16) uint8_t objects[4][0xD0];
    uint8_t geometry[2][0x40];
    uint16_t packed[8][3];
    float plain[8][3];
    Matrix matrices[3];
    Vector vectors[4];
    Mth::BBox boxes[2];
    uint8_t cache[0xFCC + 4];
    uint8_t out[0x40];
};
MovArena g_mov;
bool g_movOriginal;

template <typename F>
F MovFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_movOriginal, address));
}
float Rand(Lcg& random) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 371.0f; }

// Every function on objects built on dirty memory with random transforms: constructors and copies, the
// orientation (also transposed in place), positions, scales and the inverse, vertices from compressed and plain
// geometry, a box to local space, the face flags, the debug box, the destructors (an owned geometry, a registered
// cache holding the movables).
void TestAnimCollMovables()
{
    GameMemory memory({{0x007D1800, 4}, {0x006918EC, 0x24}});
    StandIns& s = *new StandIns();
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    s.At({0x00416970, AddressOf(&Nx::CCollObjTriData::Destruct)}, 0, [](Regs& r) {
        Record({0x6E0D0000u, r.ecx});
        return 0u;
    });
    s.At({0x0048D090}, 12, [](Regs& r) {
        Record({0xD4A70000u, r.ecx, r.args[1], r.args[2]});
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), m, m + 16);
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_movOriginal = original;
        g_trace.clear();
        std::memset(&g_mov, 0xA7, sizeof(g_mov));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        GameVariable<void*>(0x007D1800) = g_mov.out;
        Lcg random(0x412490);
        for (Matrix& m : g_mov.matrices)
            for (Vector& row : m.row)
                row = {Rand(random), Rand(random), Rand(random), Rand(random)};
        for (Vector& v : g_mov.vectors)
            v = {Rand(random), Rand(random), Rand(random), Rand(random)};
        g_mov.vectors[2] = {2.5f, -0.75f, 13.0f, 0.5f}; // (a scale)
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 3; ++k) {
                g_mov.packed[i][k] = static_cast<uint16_t>(random.Next());
                g_mov.plain[i][k] = Rand(random);
            }
        }
        for (int g = 0; g < 2; ++g) {
            auto* geometry = reinterpret_cast<CCollObjTriData*>(g_mov.geometry[g]);
            geometry->m_checksum = 0xC0110000u + g;
            geometry->m_compressed = g == 0;
            geometry->m_min = {Rand(random), Rand(random), Rand(random), 1.0f};
            geometry->m_max = {Rand(random), Rand(random), Rand(random), 1.0f};
            geometry->m_vertices = g == 0 ? static_cast<void*>(g_mov.packed) : static_cast<void*>(g_mov.plain);
        }
        g_mov.boxes[0].min = {-3.0f, 4.5f, -100.0f, 1.0f};
        g_mov.boxes[0].max = {7.25f, 40.0f, 3.0f, 0.5f};
        if (!original)
            UseOurCollisionVtables();
        auto obj = [](int i) { return reinterpret_cast<Nx::CCollMovable*>(g_mov.objects[i]); };
        auto words = [](const void* p, int n) {
            const auto* w = static_cast<const uint32_t*>(p);
            g_trace.insert(g_trace.end(), w, w + n);
        };
        // raw vertices
        for (int g = 0; g < 2; ++g) {
            for (int i = 0; i < 8; ++i) {
                Vector a{}, b{9, 9, 9, 9};
                MovFn<Vector*(__thiscall*)(void*, Vector*, int)>(0x0040B230)(g_mov.geometry[g], &a, i);
                MovFn<void(__thiscall*)(void*, int, Vector*)>(0x0040B2D0)(g_mov.geometry[g], i, &b);
                words(&a, 4);
                words(&b, 4);
            }
        }
        // the movable
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x00411FA0)(obj(0)))});
        words(obj(0), 0xA4 / 4);
        MovFn<void(__thiscall*)(void*, const Matrix*)>(0x00412070)(obj(0), &g_mov.matrices[0]);
        MovFn<void(__thiscall*)(void*, const Matrix*)>(0x00412FF0)(obj(0), &g_mov.matrices[1]);
        obj(0)->m_position = g_mov.vectors[0];
        obj(0)->m_geometry = reinterpret_cast<CCollObjTriData*>(g_mov.geometry[0]);
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x00412050)(obj(0))),
                reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x00412060)(obj(0)))});
        for (int i = 0; i < 8; ++i) {
            Vector v{};
            MovFn<Vector*(__thiscall*)(void*, Vector*, int)>(0x00412230)(obj(0), &v, i);
            words(&v, 4);
        }
        MovFn<void(__thiscall*)(void*, const Mth::BBox*, Mth::BBox*)>(0x004122C0)(obj(0), &g_mov.boxes[0], &g_mov.boxes[1]);
        words(&g_mov.boxes[1], 8);
        MovFn<void(__thiscall*)(void*, int)>(0x0040AE40)(obj(0), 0xA0A0);
        Record({static_cast<uint32_t>(MovFn<int(__thiscall*)(void*)>(0x0040AE50)(obj(0)))});
        obj(0)->m_visitStamp = 0x33;
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*, const void*)>(0x00412660)(obj(1), obj(0)))});
        // transposes: in place and from another
        Matrix t = g_mov.matrices[2];
        Record({reinterpret_cast<uint32_t>(MovFn<Matrix*(__thiscall*)(Matrix*, const Matrix*)>(0x004120D0)(&g_mov.matrices[2], &g_mov.matrices[2]))});
        Record({reinterpret_cast<uint32_t>(MovFn<Matrix*(__thiscall*)(Matrix*, const Matrix*)>(0x004120D0)(&g_mov.matrices[1], &t))});
        MovFn<void(__thiscall*)(Matrix*)>(0x00412150)(&g_mov.matrices[0]);
        // the movable box
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x00412550)(obj(2)))});
        words(obj(2), 0xD0 / 4);
        auto* box = reinterpret_cast<Nx::CCollMovBBox*>(obj(2));
        for (int i = 0; i < 4; ++i)
            box->m_unknownCA[i] = static_cast<uint8_t>(0x51 + i);
        MovFn<void(__thiscall*)(void*, const Matrix*)>(0x00412070)(box, &g_mov.matrices[1]);
        box->m_position = g_mov.vectors[1];
        MovFn<void(__thiscall*)(void*, void*)>(0x00412600)(box, g_mov.geometry[1]);
        box->m_faceFlags = 0xF1A9;
        box->m_terrain = 0x7E;
        Record({MovFn<uint32_t(__thiscall*)(void*, int)>(0x0040AE60)(box, 3), MovFn<uint16_t(__thiscall*)(void*, int)>(0x0040AE70)(box, 4)});
        MovFn<void(__thiscall*)(void*, int, int)>(0x004126E0)(box, 1, 2);
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*, const void*)>(0x00413700)(obj(3), box))});
        // the scaled object
        auto* scaled = reinterpret_cast<Nx::CCollScaled*>(obj(1));
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x004123C0)(scaled))});
        words(scaled, 0xA0 / 4);
        MovFn<void(__thiscall*)(void*, const Vector*)>(0x004136A0)(scaled, &g_mov.vectors[2]);
        MovFn<void(__thiscall*)(void*, const Matrix*)>(0x00413640)(scaled, &g_mov.matrices[0]);
        MovFn<void(__thiscall*)(void*, const Vector*)>(0x00413610)(scaled, &g_mov.vectors[3]);
        words(scaled, 0xA0 / 4);
        scaled->m_geometry = reinterpret_cast<CCollObjTriData*>(g_mov.geometry[1]);
        scaled->m_scale.w = 0.75f;
        for (int i = 0; i < 8; ++i) {
            Vector v{};
            MovFn<Vector*(__thiscall*)(void*, Vector*, int)>(0x004109D0)(scaled, &v, i);
            words(&v, 4);
        }
        MovFn<void(__thiscall*)(void*)>(0x00412490)(scaled);
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*)>(0x004109C0)(scaled))});
        // destructors: a registered cache holding the movables; owned / unowned geometry
        auto* cache = reinterpret_cast<Nx::CCollCache*>(g_mov.cache);
        std::memset(cache, 0, sizeof(Nx::CCollCache));
        cache->m_entries[0] = {reinterpret_cast<Nx::CCollObj*>(obj(2)), nullptr};
        cache->m_entries[1] = {reinterpret_cast<Nx::CCollObj*>(obj(0)), nullptr};
        cache->m_entries[2] = {reinterpret_cast<Nx::CCollObj*>(obj(3)), nullptr};
        cache->m_count = 3;
        cache->m_statics = 1;
        cache->m_movables = 2;
        Nx::RegisterCollCache(cache);
        obj(0)->m_flags = 0x8000;
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*, uint8_t)>(0x004135B0)(obj(0), 1))});
        obj(3)->m_flags = 0x8000;
        obj(3)->m_geometry = reinterpret_cast<CCollObjTriData*>(g_mov.geometry[0]);
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*, uint8_t)>(0x004136D0)(obj(3), 0))});
        MovFn<void(__thiscall*)(void*)>(0x004125F0)(box);
        scaled->m_flags = 0x8000;
        Record({reinterpret_cast<uint32_t>(MovFn<void*(__thiscall*)(void*, uint8_t)>(0x004135E0)(scaled, 1))});
        scaled->m_flags = 0xC000;
        MovFn<void(__thiscall*)(void*)>(0x00412460)(scaled);
        MovFn<void(__thiscall*)(void*)>(0x004121A0)(obj(1));
        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* all = reinterpret_cast<const uint32_t*>(&g_mov);
        trace.insert(trace.end(), all, all + sizeof(g_mov) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx movable / movable box / scaled collision objects, raw vertices");
}
SELFTEST(Early, TestAnimCollMovables, TestStandIns::RemoveStandIns);

} // namespace
