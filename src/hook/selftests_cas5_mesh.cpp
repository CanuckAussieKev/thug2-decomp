// Self-tests for the engine's mesh, the splat slots, the vertex stack and the loading bar's commands
// (src/game/gfx/engine_mesh.cpp, loading_bar_cfuncs.cpp): original and ours on objects in an arena, Pip, the collision
// geometry, the script structure and the loading screen stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/file/pip.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/loading_bar.h"
#include "game/gfx/loading_screen.h"
#include "game/nx/coll_tri.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

void StandInMeshCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005B7270, AddressOf(static_cast<void (*)(const char*)>(&Pip::Unload))}, 0, [](Regs& r) {
        Record({0x7270, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004169C0, AddressOf(&Nx::CCollObjTriData::Relocate)}, 24, [](Regs& r) {
        Record({0x69C0, Relocate(r.ecx)});
        for (int i = 0; i < 6; ++i)
            Record({Relocate(r.args[i])});
        return 1u;
    });
    s.At({0x00414D30, AddressOf(&Nx::CCollObjTriData::SetUpAllFaces)}, 0, [](Regs& r) {
        Record({0x4D30, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x004B6B80, AddressOf(&Nx::UpdateLoadingBarColors)}, 0, [](Regs&) {
        Record({0x6B80});
        return 0u;
    });
    s.At({0x004B7400, AddressOf(&Nx::StartLoadingBar)}, 0, [](Regs& r) {
        Record({0x7400, r.args[0]});
        return 0u;
    });
    s.At({0x004B6F70, AddressOf(&Nx::FreeLoadingScreenSprite)}, 0, [](Regs&) {
        Record({0x6F70});
        return 0u;
    });
    s.At({0x004B6F10, AddressOf(&Nx::HideLoadingScreen)}, 0, [](Regs&) {
        Record({0x6F10});
        return 0u;
    });
    s.At({0x004B78B0, AddressOf(&Nx::ShowLoadingScreen)}, 0, [](Regs& r) {
        Record({0x78B0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         12, [](Regs& r) {
             Record({0x6630, r.ecx, r.args[0], r.args[2]});
             // (always there: a missing one leaves the original's local unset)
             *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(r.args[0] ^ r.ecx);
             return 1u;
         });
}

void TestEngineMesh()
{
    GameMemory& memory = *new GameMemory({{0x0067DDB4, 0x24}, {0x006FC498, 8}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMeshCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("mesh", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x4800 + round);
            std::vector<uint32_t> trace;
            auto* mesh = reinterpret_cast<Nx::CMesh*>(Bump(sizeof(Nx::CMesh)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00490A60)(mesh)));
            for (int step = 0; step < 8; ++step) {
                g_trace.clear();
                switch (random.Below(9)) {
                case 0:
                    if (random.Below(2))
                        std::memcpy(mesh->m_collisionName, "skin.col", 9);
                    Fn<void(__thiscall*)(void*)>(original, 0x00490A90)(mesh);
                    break;
                case 1: { // collision data: a header, 0-3 objects, the rest
                    auto* data = reinterpret_cast<uint32_t*>(Bump(0x400) + 4 * random.Below(4));
                    for (int i = 0; i < 0x40; ++i)
                        data[i] = random.Below(16);
                    data[1] = random.Below(4);
                    data[0x20 + 3] = random.Below(64); // (where the face lists are, after the nodes' word)
                    for (int i = 0x24; i < 0x100; ++i) // (the objects' boxes)
                        reinterpret_cast<float*>(data)[i] = static_cast<float>(static_cast<int>(random.Below(20)) - 10);
                    std::memset(reinterpret_cast<uint8_t*>(data) + 0x300, 0, 0x40);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x00490B80)(mesh, data) & 0xFF);
                    break;
                }
                case 2:
                    mesh->mp_hierarchy = reinterpret_cast<void*>(random.Next());
                    mesh->m_hierarchyCount = random.Next();
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00490AB0)(mesh));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00490AC0)(mesh));
                    break;
                case 3: {
                    static const char* const names[] = {"skin", "skin.skin", "a.b.c", ".x", ""};
                    char out[64];
                    std::memset(out, 0x55, sizeof(out));
                    Fn<void(__cdecl*)(char*, const char*)>(original, 0x00490AD0)(out, names[random.Below(5)]);
                    trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(out), sizeof(out)));
                    break;
                }
                case 4: {
                    auto* slots = reinterpret_cast<uint32_t*>(Bump(0x800));
                    for (int i = 0; i < 0x200; ++i)
                        slots[i] = random.Below(5) ? random.Below(50) : 0;
                    if (random.Below(2))
                        for (int i = 0; i < 0x100; ++i)
                            slots[i] |= 1; // (none free)
                    if (random.Below(3) == 0)
                        for (int i = 0; i < 0x100; ++i)
                            slots[0x100 + i] = slots[i] / 2; // (every age negative)
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00490CD0)(slots, random.Below(0x101)));
                    break;
                }
                case 5: {
                    auto* stack = reinterpret_cast<Nx::sVertexStack*>(Bump(sizeof(Nx::sVertexStack)));
                    Fn<void(__thiscall*)(void*)>(original, 0x00490D20)(stack);
                    stack->vertexSize = 4 + 4 * random.Below(6);
                    uint8_t vertex[0x20];
                    for (int i = static_cast<int>(random.Below(5)); i > 0; --i) {
                        for (uint8_t& b : vertex)
                            b = static_cast<uint8_t>(random.Next());
                        Fn<void(__thiscall*)(void*, const uint8_t*)>(original, 0x00490D50)(stack, vertex);
                    }
                    break;
                }
                case 6:
                    Fn<void(__cdecl*)(float)>(original, 0x00490770)(
                        random.Below(4) ? static_cast<float>(static_cast<int>(random.Below(5)) - 2) : std::bit_cast<float>(0x7FC00000u));
                    break;
                case 7: {
                    static const uint32_t commands[] = {0x004907A0, 0x004907F0, 0x00490840, 0x004908E0, 0x00490960, 0x004909E0};
                    trace.push_back(
                        Fn<uint32_t(__cdecl*)(uint32_t, void*)>(original, commands[random.Below(6)])(random.Next(), nullptr) & 0xFF);
                    break;
                }
                default:
                    if (random.Below(2))
                        Fn<void(__cdecl*)(const char*, int, int)>(original, 0x00490890)(
                            "loading", static_cast<int>(random.Next()), static_cast<int>(random.Next()));
                    else
                        Fn<void(__cdecl*)()>(original, 0x004908C0)();
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            if (random.Below(2))
                std::memcpy(mesh->m_collisionName, "skin2.col", 10);
            if (random.Below(2))
                Fn<void(__thiscall*)(void*)>(original, 0x00490B60)(mesh);
            else
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x00490C90)(mesh, random.Below(4))));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "mesh: made, collision added / removed, getters, the collision file's name, splat slots, the vertex "
              "stack, the loading bar's commands and screen");
}

} // namespace

SELFTEST(Early, TestEngineMesh, RemoveStandIns);
