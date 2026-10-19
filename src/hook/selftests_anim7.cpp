// Self-tests for AN-7: the triangle collision objects (src/game/nx/coll_static.cpp) - a sector's, an instance, a
// movable one - the original and ours on identical stand-in geometry; calls, allocations and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
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

struct TriArena {
    alignas(16) uint8_t heap[0x3000];
    uint8_t objects[4][0xD8];
    uint8_t geometry[3][0x40];
    uint16_t packed[6][3];
    float plain[6][3];
    uint8_t faces[2][8 * 10];
    uint8_t buffer[16];
    Matrix matrix;
    Vector vectors[4];
    Mth::BBox box;
    uint8_t manager[0x200];
};
TriArena g_tri;
uint32_t g_triUsed;
bool g_triOriginal;
int g_cloneFails;

template <typename F>
F TriFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_triOriginal, address));
}
float TRand(Lcg& random) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 371.0f; }
uint32_t TriBump(uint32_t size)
{
    uint32_t at = reinterpret_cast<uint32_t>(g_tri.heap) + g_triUsed;
    g_triUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}

// Each object's functions: the face flags and terrain (small faces and not), the sector's clones (an instance, a
// copy with and without its buffer, the geometry copy failing), its position move, vertices and boxes; the
// instance's setters, world box, rescale (at and not at its position), clone and debug render; the movable's
// geometry and radius, position, box, clone, render; the destructors with owned buffers.
void TestAnimCollTriObjects()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006918EC, 0x24}, {0x00691914, 0x54}});
    StandIns& s = *new StandIns();
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        return TriBump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0x3A110000u, r.args[0]});
        return TriBump(r.args[0]);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0x3F4E0000u, r.args[0]});
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
    s.At({0x00416970}, 0, [](Regs& r) {
        Record({0x6E0D0000u, r.ecx});
        return 0u;
    });
    s.At({0x00416BF0}, 8, [](Regs& r) {
        Record({0x6C10E000u, r.ecx, r.args[0], r.args[1]});
        return g_cloneFails ? 0u : reinterpret_cast<uint32_t>(g_tri.geometry[2]);
    });
    s.At({0x00416400}, 4, [](Regs& r) {
        Record({0x74A40000u, r.ecx});
        const auto* d = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), d, d + 4);
        return 0u;
    });
    s.At({0x00414D60, AddressOf(&Nx::CollGeometryStubs::DebugRender)}, 16, [](Regs& r) {
        Record({0xD4A40000u, r.ecx, r.args[1], r.args[2], r.args[3]});
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), m, m + 16);
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_triOriginal = original;
        g_trace.clear();
        std::memset(&g_tri, 0xA9, sizeof(g_tri));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        GameVariable<void*>(0x007D1800) = g_tri.manager;
        *reinterpret_cast<uint32_t*>(g_tri.manager + 0x110) = 0x70D0110;
        *reinterpret_cast<uint32_t*>(g_tri.manager + 0x114) = 0x80770114;
        GameVariable<bool>(0x007CC979) = true;
        g_triUsed = 0;
        g_cloneFails = 0;
        Lcg random(0x4139A0);
        for (int i = 0; i < 6; ++i) {
            for (int k = 0; k < 3; ++k) {
                g_tri.packed[i][k] = static_cast<uint16_t>(random.Next());
                g_tri.plain[i][k] = TRand(random);
            }
        }
        for (auto& f : g_tri.faces)
            for (uint8_t& b : f)
                b = static_cast<uint8_t>(random.Next());
        for (int g = 0; g < 3; ++g) {
            auto* geometry = reinterpret_cast<CCollObjTriData*>(g_tri.geometry[g]);
            geometry->m_checksum = 0xC0110000u + g;
            geometry->m_numVertices = static_cast<uint16_t>(g == 2 ? 0 : 6);
            geometry->m_numFaces = 9;
            geometry->m_smallFaces = g == 0;
            geometry->m_compressed = g == 0;
            geometry->m_faces = g_tri.faces[g & 1];
            geometry->m_min = {TRand(random), TRand(random), TRand(random), 1.0f};
            geometry->m_max = {TRand(random), TRand(random), TRand(random), 1.0f};
            geometry->m_vertices = g == 0 ? static_cast<void*>(g_tri.packed) : static_cast<void*>(g_tri.plain);
            geometry->m_unknown3C = 0x3C3C0000u + g;
        }
        for (Vector& row : g_tri.matrix.row)
            row = {TRand(random), TRand(random), TRand(random), TRand(random)};
        for (Vector& v : g_tri.vectors)
            v = {TRand(random), TRand(random), TRand(random), TRand(random)};
        g_tri.box.min = {-20, -20, -20, 1};
        g_tri.box.max = {5, 30, 40, 1};
        for (int i = 0; i < 16; ++i)
            g_tri.buffer[i] = static_cast<uint8_t>(i * 13);
        // the shared origin / orientation the sector's slots 7, 8 return
        std::memset(reinterpret_cast<void*>(0x00691914), 0, 0x54);
        reinterpret_cast<float*>(0x00691914)[0] = 1.5f;
        reinterpret_cast<float*>(0x00691914)[3] = 0.25f;
        if (!original)
            UseOurCollisionVtables();
        auto words = [](const void* p, int n) {
            const auto* w = static_cast<const uint32_t*>(p);
            g_trace.insert(g_trace.end(), w, w + n);
        };
        // a sector's collision object (built by hand: its constructor is outside this range)
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_tri.objects[0]);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC70000;
        sector->m_flags = 0x0102;
        sector->m_terrain = 0x44;
        sector->m_geometry = reinterpret_cast<CCollObjTriData*>(g_tri.geometry[0]);
        sector->m_buffer = g_tri.buffer;
        for (int g = 0; g < 2; ++g) {
            sector->m_geometry = reinterpret_cast<CCollObjTriData*>(g_tri.geometry[g]);
            for (int face = 0; face < 8; ++face)
                Record({TriFn<uint32_t(__thiscall*)(void*, int)>(0x00410BF0)(sector, face),
                        TriFn<uint16_t(__thiscall*)(void*, int)>(0x00411750)(sector, face)});
        }
        sector->m_geometry = reinterpret_cast<CCollObjTriData*>(g_tri.geometry[0]);
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x004123A0)(sector)),
                reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x004123B0)(sector))});
        TriFn<void(__thiscall*)(void*, const Vector*)>(0x00410C20)(sector, &g_tri.vectors[0]);
        for (int i = 0; i < 6; ++i) {
            Vector v{};
            TriFn<Vector*(__thiscall*)(void*, Vector*, int)>(0x00410980)(sector, &v, i);
            words(&v, 4);
        }
        Record({TriFn<bool(__thiscall*)(void*, const Mth::BBox*)>(0x00410C70)(sector, &g_tri.box),
                reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x00412750)(sector))});
        auto clone = TriFn<void*(__thiscall*)(void*, bool)>(0x004139A0);
        void* instance = clone(sector, true);
        void* copy = clone(sector, false);
        sector->m_buffer = nullptr;
        void* copy2 = clone(sector, false);
        g_cloneFails = 1;
        Record({reinterpret_cast<uint32_t>(instance), reinterpret_cast<uint32_t>(copy), reinterpret_cast<uint32_t>(copy2),
                reinterpret_cast<uint32_t>(clone(sector, false))});
        g_cloneFails = 0;
        // the instance
        auto* inst = reinterpret_cast<Nx::CCollStaticInstance*>(instance);
        TriFn<void(__thiscall*)(void*, const Vector*)>(0x00413F50)(inst, &g_tri.vectors[1]);
        TriFn<void(__thiscall*)(void*, const Matrix*)>(0x00413F30)(inst, &g_tri.matrix);
        words(inst, 0xC8 / 4);
        TriFn<void(__thiscall*)(void*, const Vector*)>(0x00413F00)(inst, &g_tri.vectors[2]);
        words(inst, 0xC8 / 4);
        Record({TriFn<bool(__thiscall*)(void*, const Mth::BBox*)>(0x00411460)(inst, &g_tri.box),
                reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x00412830)(inst))});
        Vector moved = g_tri.vectors[3];
        Vector offY = inst->m_matrix.row[3];
        offY.y += 1.0f;
        TriFn<void(__thiscall*)(void*, const Vector*, const Vector*)>(0x00411330)(inst, &offY, &g_tri.vectors[1]);
        words(inst, 0xC8 / 4);
        TriFn<void(__thiscall*)(void*, const Vector*, const Vector*)>(0x00411330)(inst, &moved, &g_tri.vectors[1]);
        TriFn<void(__thiscall*)(void*, const Vector*, const Vector*)>(0x00411330)(inst, &inst->m_matrix.row[3], &g_tri.vectors[3]);
        words(inst, 0xC8 / 4);
        TriFn<void(__thiscall*)(void*, int, int)>(0x00412F50)(inst, 3, 4);
        void* inst2 = TriFn<void*(__thiscall*)(void*, int)>(0x00414CD0)(inst, 5);
        Record({reinterpret_cast<uint32_t>(inst2)});
        auto* built = reinterpret_cast<Nx::CCollStaticInstance*>(g_tri.objects[1]);
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x004127C0)(built))});
        words(built, 0xC8 / 4);
        built->m_buffer = g_tri.buffer;
        TriFn<void(__thiscall*)(void*, void*)>(0x00411480)(built, g_tri.geometry[1]);
        words(built, 0xC8 / 4);
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*, const void*)>(0x00414C30)(g_tri.objects[2], built))});
        TriFn<void(__thiscall*)(void*)>(0x004113B0)(built);
        // the movable triangle object
        auto* mov = reinterpret_cast<Nx::CCollMovTri*>(g_tri.objects[3]);
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*, void*)>(0x00413000)(mov, g_tri.geometry[0]))});
        words(mov, 0xD4 / 4);
        TriFn<void(__thiscall*)(void*, void*)>(0x00411780)(mov, g_tri.geometry[1]);
        TriFn<void(__thiscall*)(void*, const Vector*)>(0x00412F90)(mov, &g_tri.vectors[0]);
        Record({TriFn<bool(__thiscall*)(void*, const Mth::BBox*)>(0x00411810)(mov, &g_tri.box)});
        words(mov, 0xD4 / 4);
        TriFn<void(__thiscall*)(void*, const Vector*)>(0x00412F90)(mov, &g_tri.vectors[3]);
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*)>(0x00412FC0)(mov)),
                TriFn<bool(__thiscall*)(void*, const Mth::BBox*)>(0x00411810)(mov, &g_tri.box)});
        words(mov, 0xD4 / 4);
        TriFn<void(__thiscall*)(void*)>(0x0040B390)(mov);
        TriFn<void(__thiscall*)(void*, int, int)>(0x00413130)(mov, 6, 7);
        void* mov2 = TriFn<void*(__thiscall*)(void*, int)>(0x00414400)(mov, 9);
        Record({reinterpret_cast<uint32_t>(mov2)});
        words(mov, 0xD4 / 4);
        // destructors
        reinterpret_cast<Nx::CCollStatic*>(copy)->m_flags |= 0x8000;
        TriFn<void(__thiscall*)(void*)>(0x00412760)(copy);
        sector->m_buffer = g_tri.buffer; // not owned (flags 0x0102): kept
        TriFn<void(__thiscall*)(void*)>(0x00412760)(sector);
        Record({reinterpret_cast<uint32_t>(sector->m_buffer)});
        inst->m_buffer = g_tri.buffer;
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*, uint8_t)>(0x00413ED0)(inst, 1))});
        built->m_buffer = g_tri.buffer;
        built->m_flags = 0x8000;
        TriFn<void(__thiscall*)(void*)>(0x00412840)(built);
        auto* other = reinterpret_cast<Nx::CCollStaticInstance*>(g_tri.objects[2]);
        other->m_buffer = g_tri.buffer; // not owned: kept
        other->m_flags = 0x4000;
        TriFn<void(__thiscall*)(void*)>(0x00412840)(other);
        Record({reinterpret_cast<uint32_t>(built->m_buffer), reinterpret_cast<uint32_t>(other->m_buffer)});
        reinterpret_cast<Nx::CCollMovTri*>(mov2)->m_flags = 0;
        Record({reinterpret_cast<uint32_t>(TriFn<void*(__thiscall*)(void*, uint8_t)>(0x004143D0)(mov2, 1))});
        TriFn<void(__thiscall*)(void*)>(0x00413070)(mov);
        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* all = reinterpret_cast<const uint32_t*>(&g_tri);
        trace.insert(trace.end(), all, all + sizeof(g_tri) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx triangle collision objects (sector, instance, movable)");
}
SELFTEST(Early, TestAnimCollTriObjects, TestStandIns::RemoveStandIns);

} // namespace
