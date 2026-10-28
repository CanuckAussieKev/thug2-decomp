// Self-tests for the engine's shadows (src/game/gfx/engine_shadows.cpp, RND5-3): the original and ours over dirty
// stand-in memory - the cameras' constructor / SetPos / SetMatrix, the render targets, the shadow casters, the
// model calls (made, loaded, drawn, active, shadow on), the file name and the table's FlushItem stood in and
// recorded (positions and matrices bit for bit); the normalisation and the matrix scale run as they are.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/camera.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_shadows.h"
#include "game/gfx/model.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;

RecordingVtable g_objectVtable; // cameras, render targets, models: [0] deletes

void RecordFloats(const void* p, int count)
{
    for (int i = 0; i < count; ++i)
        Record({static_cast<const uint32_t*>(p)[i]});
}

void StandInShadowCalls(StandIns& s)
{
    StandInMemory(s);
    StandInScriptStructure(s);
    g_objectVtable.Make(s, {1});
    s.At({0x00485BA0, AddressOf(&Gfx::Camera::Construct)}, 0, [](Regs& r) {
        Record({0x5BA0, Relocate(r.ecx)});
        *reinterpret_cast<uint32_t**>(r.ecx) = g_objectVtable.slots;
        return r.ecx;
    });
    s.At({0x00485A70, AddressOf(&Gfx::Camera::SetPos)}, 4, [](Regs& r) {
        Record({0x5A70, Relocate(r.ecx)});
        RecordFloats(reinterpret_cast<void*>(r.args[0]), 4);
        std::memcpy(reinterpret_cast<void*>(r.ecx + 0x5C), reinterpret_cast<void*>(r.args[0]), 16);
        return 0u;
    });
    s.At({0x00485A90, AddressOf(&Gfx::Camera::SetMatrix)}, 4, [](Regs& r) {
        Record({0x5A90, Relocate(r.ecx)});
        RecordFloats(reinterpret_cast<void*>(r.args[0]), 16);
        return r.ecx + 0x6C;
    });
    s.At({0x0048BF50, AddressOf(&Nx::CreateRenderTarget)}, 0, [](Regs& r) {
        Record({0xBF50, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3]});
        uint32_t target = Bump(0x20);
        *reinterpret_cast<uint32_t**>(target) = g_objectVtable.slots;
        return target;
    });
    StandInCdecl(s, {0x0048C870}, 3);
    StandInCdecl(s, {0x0048BF70, AddressOf(&Nx::RemoveTextureShadowCaster)}, 1);
    StandInCdecl(s, {0x0048BF60, AddressOf(&Nx::AimTextureShadowCaster)}, 2);
    StandInCdecl(s, {0x0048C3C0}, 1);
    s.At({0x0048BFA0, AddressOf(&Nx::CreateModel)}, 0, [](Regs&) {
        Record({0xBFA0});
        uint32_t model = Bump(0x50);
        *reinterpret_cast<uint32_t**>(model) = g_objectVtable.slots;
        return model;
    });
    s.At({0x00487A70}, 0, [](Regs& r) {
        Record({0x7A70, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        // (the buffer arrives zeroed: its whole 0x200 bytes)
        const auto* buffer = reinterpret_cast<const uint8_t*>(r.args[2]);
        Record({HashBytes(buffer, 0x200)});
        std::memcpy(reinterpret_cast<char*>(r.args[2]), "shadow.mdl", 11);
        return 0u;
    });
    s.At({0x004935E0, AddressOf(&Nx::CModel::AddFileGeom)}, 32, [](Regs& r) {
        Record({0x35E0, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        for (int i = 1; i < 8; ++i)
            Record({r.args[i] & 0xFF});
        return 1u;
    });
    s.At({0x00493090, AddressOf(&Nx::CModel::Render)}, 12, [](Regs& r) {
        Record({0x3090, Relocate(r.ecx), r.args[1] & 0xFF, r.args[2]});
        RecordFloats(reinterpret_cast<void*>(r.args[0]), 16);
        return 1u;
    });
    s.At({0x00493C60, AddressOf(&Nx::CModel::SetActive)}, 4, [](Regs& r) {
        Record({0x3C60, Relocate(r.ecx), r.args[0] & 0xFF});
        return 1u;
    });
    s.At({0x004949B0, AddressOf(&Nx::CModel::EnableShadow)}, 4, [](Regs& r) {
        Record({0x49B0, Relocate(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        Record({0x80D0, Relocate(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        return 0u;
    });
}

Mth::Vector RandomVector(Lcg& random, float range)
{
    return {AwkwardFloat(random, -range, range), RandomFloat(random, -range, range), RandomFloat(random, -range, range),
            random.Below(3) ? 1.0f : RandomFloat(random, -2, 2)};
}

// Each kind of shadow made over dirty memory and driven at random; the volumes' list and edge table.
void TestEngineShadows()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x007013D8, 0x40}, {0x00701418, 0x18}});
    StandIns& s = *new StandIns();
    StandInShadowCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 50; ++round) {
        ok &= CompareTwoRuns("engine shadows", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5AD + round);
            Lcg random(0x5AD0 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<uint8_t>(0x007CC979) = 1;
            // The edge table: 4 buckets in the arena, some keys in it.
            auto* table = &GameVariable<Lst::HashTable>(0x00701418);
            table->m_bits = 2;
            table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
            std::memset(table->m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
            for (int i = 0; i < 4; ++i) {
                if (random.Below(4) == 0)
                    continue;
                auto* edges = reinterpret_cast<Nx::ShadowEdges*>(Bump(0x10));
                edges->edges = random.Below(3) ? reinterpret_cast<uint8_t*>(Bump(0x50)) : nullptr;
                edges->count = static_cast<int>(random.Below(3));
                edges->lastMesh = 0;
                edges->users = 1 + static_cast<int>(random.Below(2));
                table->m_buckets[i] = {0x100u * static_cast<uint32_t>(random.Below(3)) + static_cast<uint32_t>(i),
                                       random.Below(5) ? edges : nullptr, nullptr};
            }
            auto* model = reinterpret_cast<uint8_t*>(Bump(0x50));
            int kind = static_cast<int>(random.Below(3));
            uint8_t* shadow = reinterpret_cast<uint8_t*>(Bump(0x70));
            for (int i = 0; i < 0x70; ++i)
                shadow[i] = static_cast<uint8_t>(random.Next());
            Mth::Vector direction = RandomVector(random, 1);
            float distance = RandomFloat(random, 1, 100);
            if (kind == 0)
                Record({Relocate(At<uint32_t(__thiscall*)(void*, void*, Mth::Vector, float)>(original, 0x004A19C0)(
                    shadow, model, direction, distance))});
            else if (kind == 1)
                Record({Relocate(At<uint32_t(__thiscall*)(void*)>(original, 0x004A1750)(shadow))});
            else
                Record({Relocate(At<uint32_t(__thiscall*)(void*, void*, Mth::Vector, float)>(original, 0x004A1F80)(
                    shadow, model, direction, distance))});
            for (int step = 0; step < 16; ++step) {
                Mth::Vector v = RandomVector(random, 900);
                if (kind != 1 && random.Below(2)) { // (at the camera's offset: the products' rounding shows)
                    float d = *reinterpret_cast<float*>(shadow + (kind == 0 ? 0x20 : 0x64));
                    const auto* light = reinterpret_cast<float*>(shadow + (kind == 0 ? 0x10 : 0x50));
                    v.x = static_cast<float>(static_cast<double>(d) * light[0]);
                }
                switch (random.Below(7)) {
                case 0:
                    if (kind == 0)
                        At<void(__thiscall*)(void*, Mth::Vector)>(original, 0x004A1910)(shadow, v);
                    else if (kind == 2)
                        At<void(__thiscall*)(void*, Mth::Vector)>(original, 0x004A1E70)(shadow, v);
                    break;
                case 1:
                    if (kind != 1) {
                        Mth::Vector d = RandomVector(random, 1);
                        At<void(__thiscall*)(void*, const Mth::Vector*)>(original, kind == 0 ? 0x004A1BA0 : 0x004A21A0)(
                            shadow, &d);
                    }
                    break;
                case 2:
                    if (kind == 1) {
                        if (random.Below(2))
                            *reinterpret_cast<float*>(shadow + 0xC) = RandomFloat(random, 0.1f, 3);
                        if (random.Below(2))
                            *reinterpret_cast<float*>(shadow + 0x10) = RandomFloat(random, -3, 3);
                        Mth::Matrix m;
                        for (int k = 0; k < 4; ++k)
                            m.row[k] = RandomVector(random, 2);
                        Mth::Vector normal = RandomVector(random, 1);
                        if (random.Below(2)) // (the offset's rounding shows)
                            v.x = -static_cast<float>(static_cast<double>(normal.x) *
                                                      *reinterpret_cast<float*>(shadow + 0x10));
                        At<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Matrix*, Mth::Vector)>(
                            original, 0x004A1CD0)(shadow, &v, &m, normal);
                    } else {
                        Mth::Matrix m;
                        At<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Matrix*, Mth::Vector)>(
                            original, 0x004A18A0)(shadow, &v, &m, v);
                    }
                    break;
                case 3:
                    if (kind == 1)
                        At<void(__thiscall*)(void*, const char*)>(original, 0x004A17E0)(
                            shadow, random.Below(2) ? "Ped_Shadow" : "Car_Shadow");
                    break;
                case 4:
                    if (kind == 1)
                        At<void(__thiscall*)(void*)>(original, random.Below(2) ? 0x004A1880 : 0x004A1890)(shadow);
                    else if (kind == 2) {
                        Mth::Matrix m;
                        for (int k = 0; k < 4; ++k)
                            m.row[k] = RandomVector(random, 2);
                        Record({Relocate(At<uint32_t(__thiscall*)(void*, const Mth::Matrix*)>(original, 0x004A1EE0)(
                            shadow, &m))});
                    }
                    break;
                case 5: { // the edge table
                    uint32_t key = 0x100u * random.Below(3) + random.Below(4);
                    Record({Relocate(At<uint32_t(__cdecl*)(uint32_t)>(original, 0x004A1EF0)(key))});
                    if (random.Below(2))
                        At<void(__cdecl*)(uint32_t)>(original, 0x004A1F30)(key);
                    break;
                }
                default: { // the volumes' list
                    auto* list = &GameVariable<uint8_t*>(0x007013D8);
                    for (int i = 0; i < 16; ++i)
                        list[i] = random.Below(3) == 0 ? shadow : random.Below(2) ? model : nullptr;
                    At<void(__cdecl*)(void*)>(original, 0x004A1E40)(shadow);
                    break;
                }
                }
            }
            if (kind == 2) { // edges to let go of (some), on the list
                if (random.Below(3)) {
                    uint32_t key = 0x100u * random.Below(3) + random.Below(4);
                    *reinterpret_cast<uint32_t*>(shadow + 0x4C) = key;
                    auto* edges = static_cast<Nx::ShadowEdges*>(
                        GameVariable<Lst::HashTable>(0x00701418).GetItem(key));
                    *reinterpret_cast<void**>(shadow + 0x60) = edges ? edges : random.Below(2) ? reinterpret_cast<Nx::ShadowEdges*>(Bump(0x10)) : nullptr;
                    if (auto* e = *reinterpret_cast<Nx::ShadowEdges**>(shadow + 0x60); e && !edges) {
                        e->edges = nullptr;
                        e->count = static_cast<int>(random.Below(2));
                        e->users = 5;
                    }
                }
                auto* list = &GameVariable<uint8_t*>(0x007013D8);
                list[random.Below(16)] = shadow;
            }
            uint32_t destroy = kind == 0 ? 0x004A18E0 : kind == 1 ? 0x004A1990 : 0x004A22C0;
            if (random.Below(4) == 0) {
                At<void(__thiscall*)(void*)>(original, kind == 0 ? 0x004A16E0 : kind == 1 ? 0x004A1780 : 0x004A2120)(
                    shadow);
                Record({*reinterpret_cast<uint32_t*>(shadow)});
                At<void(__thiscall*)(void*)>(original, 0x004A16D0)(shadow);
                Record({*reinterpret_cast<uint32_t*>(shadow)});
                *reinterpret_cast<uint32_t**>(shadow) = g_objectVtable.slots;
                Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A18B0)(
                    shadow, static_cast<int>(random.Below(4))))});
                Record({*reinterpret_cast<uint32_t*>(shadow)});
                Record({Relocate(At<uint32_t(__thiscall*)(void*)>(original, 0x004A16C0)(shadow))});
            } else {
                Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, destroy)(
                    shadow, static_cast<int>(random.Below(4))))});
            }
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine shadows: detailed, simple and volume shadows made, placed, turned, drawn, destroyed; the edge "
              "table and the volumes' list");
}

} // namespace

SELFTEST(Early, TestEngineShadows, RemoveStandIns);
