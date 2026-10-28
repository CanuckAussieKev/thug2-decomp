// Self-tests for the engine's texture projection (src/game/gfx/engine_texture_projection.cpp) and the viewport list
// (viewport_list.cpp), RND4-6: the original and ours over stand-in projections (a recording vtable), objects, script
// structures (the getters answering from a table), cameras, grids and collision objects; the matrix multiply, the
// view test and the sector look-up stood in; every result, call and the arena compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/dbg.h"
#include "game/gfx/engine_texture_projection.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/viewport_list.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CTextureProjection;

void* g_projVtable[4];
void* g_collVtable[24];
Lcg* g_random;
std::map<uint32_t, std::vector<uint32_t>> g_params; // a structure's values by name (a vector: four words)
uint8_t* g_camera;          // the first viewport's camera (null: none)
uint32_t g_gridsWanted;     // how many grids the line reaches
bool g_noObjects;           // a grid's look-up finds nothing

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// A collision object whose box (slot 21) is near the origin, some far away.
Nx::CCollObj* MakeColl()
{
    auto* coll = reinterpret_cast<Nx::CCollObj*>(Block(0x40));
    std::memset(coll, 0, 0x40);
    coll->m_vtable = g_collVtable;
    coll->m_checksum = 0xC0000 + (g_random->Next() & 0xFF);
    auto* box = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(coll) + 0x10);
    float x = RandomFloat(*g_random, -300, 300), y = RandomFloat(*g_random, -300, 300),
          z = RandomFloat(*g_random, -300, 300);
    float s = RandomFloat(*g_random, 0, 60);
    float b[8] = {x, y, z, 1, x + s, y + s, z + s, 1};
    if (g_random->Below(8) == 0)
        b[g_random->Below(3)] = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(box, b, sizeof(b));
    return coll;
}

void StandInProjection(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, r.args[0]});
        return 0u;
    });
    using Script::CStruct;
    auto getter = [](uint32_t tag, int words) {
        return [tag, words](Regs& r) {
            Record({tag, r.args[0], r.args[2]});
            auto it = g_params.find(r.args[0]);
            if (it == g_params.end())
                return 0xFFFFFF00u;
            for (int i = 0; i < words; ++i)
                reinterpret_cast<uint32_t*>(r.args[1])[i] = it->second[static_cast<size_t>(i)];
            return 0xFFFFFF01u;
        };
    };
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630, 1));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0, 1));
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950, 1));
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, getter(0x6FB0, 4));
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, Relocate(r.ecx), r.args[0]});
        return 0x7E770000u + (r.args[0] & 0xFFFF);
    });
    // the matrix multiply (D3DX): the matrices recorded, a product of sorts out
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* a = reinterpret_cast<const float*>(r.args[1]);
        auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Relocate(r.args[0])});
        for (int i = 0; i < 16; ++i)
            Record({std::bit_cast<uint32_t>(a[i]), std::bit_cast<uint32_t>(b[i])});
        for (int i = 0; i < 16; ++i)
            out[i] = a[i] - b[i];
        return r.args[0];
    });
    // Our maths run the original's (the same results by their own tests; here the projection's own arithmetic is
    // what's compared): normalise, look-at, orthographic, perspective, vector times matrix.
    s.At({AddressOf(&Mth::Vector::Normalize)}, 0, [](Regs& r) {
        return GameFunction<uint32_t(__thiscall*)(uint32_t)>(0x004023F0)(r.ecx);
    });
    s.At({AddressOf(Mth::CreateLookAtMatrix)}, 0, [](Regs& r) {
        return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00404F50)(
            r.args[0], r.args[1], r.args[2], r.args[3]);
    });
    s.At({AddressOf(Mth::CreateOrthographicMatrix)}, 0, [](Regs& r) {
        return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00405080)(
            r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]);
    });
    s.At({AddressOf(Mth::CreatePerspectiveMatrix)}, 0, [](Regs& r) {
        return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x004050F0)(
            r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]);
    });
    s.At({AddressOf(static_cast<Mth::Vector (*)(const Mth::Vector&, const Mth::Matrix&)>(&Mth::operator*))}, 0,
         [](Regs& r) {
             return GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(0x0041F470)(r.args[0], r.args[1],
                                                                                               r.args[2]);
         });
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Record({0x09C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_camera);
    });
    s.At({0x00485A00}, 0, [](Regs& r) {
        Record({0x5A00, Relocate(r.ecx)});
        return r.ecx + 0x4C;
    });
    s.At({0x0048C490}, 0, [](Regs& r) {
        auto* centre = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xC490, centre[0], centre[1], centre[2], centre[3], r.args[1]});
        return g_random->Below(3) == 0 ? 1u : 0u;
    });
    // the grids along the line (the line recorded), their objects in the box (the box recorded)
    s.At({0x0048C980}, 0, [](Regs& r) {
        auto* line = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xC980});
        for (int i = 0; i < 8; ++i)
            Record({line[i]});
        auto* grids = reinterpret_cast<uint32_t*>(Block(4 * (g_gridsWanted + 1)));
        for (uint32_t i = 0; i < g_gridsWanted; ++i)
            grids[i] = reinterpret_cast<uint32_t>(Block(0x10));
        grids[g_gridsWanted] = 0;
        return reinterpret_cast<uint32_t>(grids);
    });
    s.At({0x00418710, AddressOf(&Nx::CCollGrid::ObjectsInBox)}, 4, [](Regs& r) {
        auto* box = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x8710, Relocate(r.ecx)});
        for (int i = 0; i < 8; ++i)
            Record({box[i]});
        if (g_noObjects && g_random->Below(2))
            return 0u;
        uint32_t count = g_random->Below(6);
        auto* objects = reinterpret_cast<uint32_t*>(Block(4 * (count + 1)));
        for (uint32_t i = 0; i < count; ++i)
            objects[i] = reinterpret_cast<uint32_t>(MakeColl());
        objects[count] = 0;
        return reinterpret_cast<uint32_t>(objects);
    });
    s.At({0x0048C510}, 0, [](Regs& r) {
        Record({0xC510, r.args[0]});
        return 0x5EC00000u + r.args[0];
    });
    for (int slot = 0; slot < 24; ++slot) {
        g_collVtable[slot] = s.Thunk(0, [slot](Regs& r) {
            Record({0xC000u + slot, Relocate(r.ecx)});
            return r.ecx + 0x10;
        });
    }
    // the projection's slots: [1] projected (the two arrays' contents recorded)
    for (int slot = 0; slot < 4; ++slot) {
        g_projVtable[slot] = s.Thunk(slot == 1 ? 8 : 4, [slot](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx), Relocate(r.args[0])});
            if (slot == 1) {
                Record({Relocate(r.args[1])});
                auto* sectors = reinterpret_cast<const uint32_t*>(r.args[0]);
                auto* colls = reinterpret_cast<const uint32_t*>(r.args[1]);
                for (int i = 0; i < 128; ++i) {
                    Record({Relocate(sectors[i]), Relocate(colls[i])});
                    if (!sectors[i] && !colls[i])
                        break;
                }
            }
            return 0u;
        });
    }
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

void SetParam(uint32_t name, float value) { g_params[name] = {std::bit_cast<uint32_t>(value)}; }

void TestTextureProjection()
{
    GameMemory memory({{0x00700F20, 4}, {0x00701378, 4}, {0x00700F40, 0x200}, {0x00701140, 0x200}});
    StandIns& s = *new StandIns();
    StandInProjection(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("texture projection", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x7A0 + round);
            Lcg standIns(0x7A1 + 3 * round);
            g_random = &standIns;
            GameVariable<uint32_t>(0x00700F20) = 0xD1C70000;
            std::memset(reinterpret_cast<void*>(0x00700F40), 0xAB, 0x200);
            std::memset(reinterpret_cast<void*>(0x00701140), 0xCD, 0x200);
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            auto* projection = reinterpret_cast<CTextureProjection*>(Block(sizeof(CTextureProjection)));
            for (uint32_t i = 0; i < sizeof(CTextureProjection); ++i)
                reinterpret_cast<uint8_t*>(projection)[i] = static_cast<uint8_t>(random.Next());
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049FE20)(projection));
            DumpArena(g_trace);
            projection->vtable = g_projVtable;
            projection->m_flags = random.Next();
            // its structure: a direction, range, size or field of view, offset, angle, texture, ...
            g_params.clear();
            auto vec = [&](uint32_t name, float scale) {
                g_params[name] = {std::bit_cast<uint32_t>(RandomFloat(random, -scale, scale)),
                                  std::bit_cast<uint32_t>(RandomFloat(random, -scale, scale)),
                                  std::bit_cast<uint32_t>(RandomFloat(random, -scale, scale)),
                                  std::bit_cast<uint32_t>(1.0f)};
            };
            if (random.Below(4))
                vec(0xB1DE1E91, 2);
            if (random.Below(4))
                SetParam(0xDF61A4BD, RandomFloat(random, 0, 400));
            switch (random.Below(3)) {
            case 0:
                SetParam(0x083FDB95, RandomFloat(random, 1, 60));
                break;
            case 1:
                SetParam(0x17E7321E, RandomFloat(random, 0.2f, 2));
                if (random.Below(2))
                    SetParam(0x0F7C3CAA, RandomFloat(random, 0.5f, 2));
                break;
            default:
                break;
            }
            if (random.Below(2))
                g_params[0xDC46BA66] = {random.Next()};
            if (random.Below(2))
                vec(0xA6F5352F, 30);
            if (random.Below(2))
                SetParam(0xFF7EBAF6, RandomFloat(random, -180, 180));
            g_params[0x7D99F28D] = {random.Next()};
            if (random.Below(2))
                g_params[0xDD6BB3D5] = {random.Next()};
            if (random.Below(2))
                g_params[0x12602598] = {random.Below(4)};
            if (random.Below(2))
                g_params[0xAF06447B] = {random.Below(3)};
            if (random.Below(2))
                SetParam(0xE979B3D3, RandomFloat(random, 0, 1000));
            if (random.Below(2))
                SetParam(0x7FCAEAB1, RandomFloat(random, 0, 900));
            At<void(__thiscall*)(void*, void*)>(original, 0x004A04C0)(projection, Block(4));
            // followed with an object, a few frames
            auto* object = reinterpret_cast<Obj::CCompositeObject*>(Block(0x200));
            for (int frame = 0; frame < 4; ++frame) {
                auto* f = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(object) + 0x4C);
                for (int i = 0; i < 4; ++i)
                    f[i] = i == 3 ? 1.0f : RandomFloat(random, -200, 200);
                auto* m = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(object) + 0xC0);
                for (int i = 0; i < 16; ++i)
                    m[i] = RandomFloat(random, -1.5f, 1.5f);
                GameVariable<int>(0x00701378) = static_cast<int>(random.Below(3));
                g_camera = random.Below(4) ? Block(0x80) : nullptr;
                if (g_camera) {
                    auto* p = reinterpret_cast<float*>(g_camera + 0x4C);
                    for (int i = 0; i < 4; ++i)
                        p[i] = RandomFloat(random, -400, 400);
                    if (random.Below(6) == 0)
                        p[random.Below(3)] = std::numeric_limits<float>::quiet_NaN();
                }
                g_gridsWanted = random.Below(3);
                g_noObjects = random.Below(3) == 0;
                g_gridsWanted += g_noObjects ? 1 : 0;
                At<void(__thiscall*)(void*, void*)>(original, 0x0049FFE0)(projection, object);
                if (random.Below(3) == 0) {
                    projection->m_flags = random.Next() & 7;
                    At<void(__thiscall*)(void*)>(original, 0x0049FEF0)(projection);
                }
            }
            At<void(__thiscall*)(void*, void*, void*)>(original, 0x0049FE10)(projection, nullptr, nullptr);
            // its node unlinked from a list
            auto* head = reinterpret_cast<Mem::ListNode*>(Block(sizeof(Mem::ListNode)));
            auto* node = reinterpret_cast<Mem::ListNode*>(projection);
            head->next = head->prev = node;
            node->next = node->prev = head;
            At<void(__thiscall*)(void*)>(original, 0x004A04A0)(projection);
            GameMemory({{0x00700F40, 0x200}, {0x00701140, 0x200}}).Dump(g_trace);
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "texture projection: construct, set up, followed (distance, view, grids, boxes), matrix, unlink");
}

void TestViewportList()
{
    GameMemory memory({{0x0067E540, 4}, {0x00701350, 40}, {0x0070134C, 4}});
    StandIns& s = *new StandIns();
    s.At({0x004A0D50}, 4, [](Regs& r) {
        Record({0x0D50, Relocate(r.ecx), r.args[0]});
        return (r.ecx & 0x30) == (r.args[0] & 0x30) ? 0xFFFFFF01u : 0xFFFFFF00u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("viewport list", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0);
            Lcg random(0x7E3 + round);
            int* map = reinterpret_cast<int*>(Block(40));
            for (int i = 0; i < 10; ++i)
                map[i] = random.Below(3) ? static_cast<int>(random.Below(10)) : -1 - static_cast<int>(random.Below(2));
            GameVariable<int*>(0x0067E540) = map;
            for (int i = 0; i < 10; ++i)
                (&GameVariable<uint32_t>(0x00701350))[i] = random.Below(4) ? Bump(0x40) : 0u;
            GameVariable<int>(0x0070134C) = static_cast<int>(random.Next());
            for (int n = -1; n < 11; ++n)
                Record({Relocate(At<uint32_t(__cdecl*)(int)>(original, 0x004A06D0)(n))});
            Record({At<uint32_t(__cdecl*)()>(original, 0x004A0710)()});
            for (int i = 0; i < 4; ++i)
                Record({At<uint32_t(__cdecl*)(uint32_t)>(original, 0x004A0720)(random.Next()) & 0xFF});
            return g_trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "viewport list: the n-th active viewport, the screen mode, a camera marked");
}

} // namespace

SELFTEST(Early, TestTextureProjection, RemoveStandIns);
SELFTEST(Early, TestViewportList, RemoveStandIns);
