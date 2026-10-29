// Self-tests for the engine's loaded scenes (src/game/gfx/engine_scene_list.cpp): original and ours on a stand-in
// scene list (the head at 0x006FA7E0, scenes in an arena), every call they make into the rest of the engine stood in
// and recorded; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/scene_load.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/vector.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CScene;

const char* const kNames[] = {"alpha", "bravo", "sky_one", "delta", "echo"};

CScene* FindScene(uint32_t checksum)
{
    auto* head = reinterpret_cast<Mem::ListNode*>(0x006FA7E0);
    for (Mem::ListNode* node = head->next; node != head; node = node->next) {
        if (static_cast<CScene*>(node->data)->m_checksum == checksum)
            return static_cast<CScene*>(node->data);
    }
    return nullptr;
}

void StandInSceneListCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x0048C690}, 0, [](Regs& r) {
        Record({0xC690, Relocate(r.args[0]), r.args[1]});
        return 1u;
    });
    s.At({0x0048C700}, 0, [](Regs& r) {
        Record({0xC700, r.args[0]});
        return reinterpret_cast<uint32_t>(FindScene(r.args[0]));
    });
    s.At({0x0048C560}, 0, [](Regs& r) {
        Record({0xC560, Relocate(r.args[0]), Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3],
                r.args[4] ? HashBytes(reinterpret_cast<const uint8_t*>(r.args[4]), 16) : 0u, r.args[5]});
        return 0u;
    });
    s.At({0x0048C650}, 0, [](Regs&) {
        Record({0xC650});
        return 0u;
    });
    s.At({0x0048C4B0}, 0, [](Regs& r) {
        Record({0xC4B0, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x0049EFD0, AddressOf(&Nx::UnloadTexDict)}, 0, [](Regs& r) {
        Record({0xEFD0, r.args[0]});
        return 1u;
    });
    s.At({0x004B2300, AddressOf(&Nx::ClearDebugLines)}, 0, [](Regs&) {
        Record({0x2300});
        return 0u;
    });
    s.At({0x004B2C30, AddressOf(&Nx::AddTextureShadowCaster)}, 0, [](Regs& r) {
        Record({0x2C30, r.args[0], r.args[1], Relocate(r.args[2])});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return 1234u;
    });
    s.At({0x0049C9A0, AddressOf(&CScene::PrintCollStatics)}, 0, [](Regs& r) {
        Record({0xC9A0, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x0049CB00, AddressOf(&CScene::GetSector)}, 4, [](Regs& r) {
        uint32_t checksum = reinterpret_cast<CScene*>(r.ecx)->m_checksum;
        Record({0xCB00, Relocate(r.ecx), r.args[0]});
        return (r.args[0] + checksum) % 3 == 0 ? 0x5EC70000u : 0u;
    });
    s.At({0x0049D1A0, AddressOf(&CScene::GetDebugInfo)}, 4, [](Regs& r) {
        Record({0xD1A0, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004781B0, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CStruct*)>(
                           &Script::CStruct::AddStructurePointer))},
         8, [](Regs& r) {
             Record({0x81B0, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
             return 0u;
         });
    s.At({0x004B28C0, AddressOf(&Nx::CreateScene)}, 0, [](Regs& r) {
        Record({0x28C0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2] & 0xFF});
        return Bump(0x280);
    });
    s.At({0x004B36D0, AddressOf(&Nx::LoadSceneFromFile)}, 0, [](Regs& r) {
        Record({0x36D0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2] & 0xFF,
                r.args[3] & 0xFF, r.args[4] & 0xFF,
                r.args[5] ? HashBytes(reinterpret_cast<const uint8_t*>(r.args[5]), 16) : 0u});
        if (r.args[5]) // (the arena filled in by the load)
            reinterpret_cast<uint32_t*>(r.args[5])[1] = 0x77;
        return Bump(0x280);
    });
    s.At({0x004B3490, AddressOf(&Nx::LoadSceneFromMemory)}, 0, [](Regs& r) {
        Record({0x3490, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4] & 0xFF});
        return Bump(0x280);
    });
    s.At({0x0049E200, AddressOf(&CScene::UnloadAddedScene)}, 0, [](Regs& r) {
        Record({0xE200, Relocate(r.ecx)});
        return 1u;
    });
    s.At({0x0049F010, AddressOf(&Nx::FindTexDict)}, 0, [](Regs& r) {
        Record({0xF010, r.args[0]});
        return (r.args[0] & 1) ? 0x7E000000u : 0u;
    });
    s.At({0x0049F0C0, AddressOf(&Nx::LoadTexDict)}, 0, [](Regs& r) {
        Record({0xF0C0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0x7E100000u;
    });
    s.At({0x0049C940, AddressOf(&CScene::FinishAdd)}, 8, [](Regs& r) {
        Record({0xC940, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
}

float RandomCoordinate(Lcg& random)
{
    switch (random.Below(8)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
    case 2:
    case 3: // (ties with a box's edge)
        return static_cast<float>(static_cast<int>(random.Below(3)) * 50 - 50);
    default:
        return static_cast<float>(static_cast<int>(random.Below(200)) - 100);
    }
}

// Rounds of random calls on a list of 0-5 scenes (skies or not, with textures and a collision grid or not).
void TestEngineSceneList()
{
    GameMemory& memory = *new GameMemory({{0x006FA7E0, 0x14}, {0x006FA738, 0x80}, {0x0067DC84, 4}, {0x007CC979, 1},
                                          {0x007D1800, 4}, {0x006F3614, 16}, {0x00786AB6, 1}});
    StandIns& s = *new StandIns();
    StandInSceneListCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("scene list", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x5C00 + round);
            std::vector<uint32_t> trace;
            // The list.
            auto* head = reinterpret_cast<Mem::ListNode*>(0x006FA7E0);
            head->data = reinterpret_cast<void*>(-1);
            head->next = head;
            head->prev = head;
            int count = static_cast<int>(round % 8 == 7 ? 14 + random.Below(5) : random.Below(6));
            std::vector<CScene*> scenes;
            for (int i = 0; i < count; ++i) {
                auto* scene = reinterpret_cast<CScene*>(Bump(sizeof(CScene)));
                std::memset(scene, 0, sizeof(CScene));
                scene->m_checksum = Crc::GenerateCRCFromString(kNames[random.Below(5)]);
                scene->m_isSky = random.Below(3) == 0;
                scene->mp_texDict = random.Below(3) ? reinterpret_cast<Nx::CTexDict*>(0x7D000000 + 0x10 * i) : nullptr;
                if (random.Below(3)) {
                    auto* grid = reinterpret_cast<Nx::CCollGrid*>(Bump(0x40));
                    auto* box = reinterpret_cast<float*>(grid);
                    for (int axis = 0; axis < 3; ++axis) {
                        float a = RandomCoordinate(random), b = RandomCoordinate(random);
                        box[axis] = a < b ? a : b;
                        box[4 + axis] = a < b ? b : a;
                        if (count > 10) { // (many grids everything touches: the 15 limit)
                            box[axis] = -1000.0f;
                            box[4 + axis] = 1000.0f;
                        }
                    }
                    scene->mp_collGrid = grid;
                }
                if (random.Below(2))
                    std::memcpy(scene->m_addName, "levels\\bravo\\bravo.scn.Xbx", 27);
                auto* node = reinterpret_cast<Mem::ListNode*>(Bump(sizeof(Mem::ListNode)));
                node->vtable = reinterpret_cast<void**>(0x00648A38);
                node->data = scene;
                node->priority = 0;
                node->prev = head->prev;
                node->next = head;
                head->prev->next = node;
                head->prev = node;
                scenes.push_back(scene);
            }
            GameVariable<uint32_t>(0x0067DC84) = random.Below(8);
            GameVariable<uint8_t>(0x00786AB6) = static_cast<uint8_t>(random.Below(2) ? 0 : 1 + random.Below(2));
            for (int step = 0; step < 10; ++step) {
                g_trace.clear();
                switch (step < 4 ? 7 : random.Below(17)) {
                case 0:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x0048C7F0)()));
                    break;
                case 1:
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__cdecl*)(const char*)>(original, 0x0048C750)(kNames[random.Below(5)])));
                    break;
                case 2:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x0048C770)()));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x0048C7B0)()));
                    break;
                case 3:
                    trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x0048C810)() & 0xFF);
                    break;
                case 4: {
                    CScene* scene = !scenes.empty() && random.Below(2) ? scenes[random.Below(scenes.size())] : nullptr;
                    Fn<void(__cdecl*)(uint32_t, uint32_t, CScene*)>(original, 0x0048C870)(0x7A000000 + step,
                                                                                       0x7B000000, scene);
                    break;
                }
                case 5:
                    Fn<void(__cdecl*)()>(original, 0x0048C8F0)();
                    break;
                case 6:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x0048C940)()));
                    break;
                case 7: {
                    Mth::Line line;
                    float* p = reinterpret_cast<float*>(&line);
                    for (int i = 0; i < 8; ++i)
                        p[i] = RandomCoordinate(random);
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(const Mth::Line*)>(original, 0x0048C980)(&line)));
                    break;
                }
                case 8:
                    trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x0048CA80)());
                    break;
                case 9:
                    trace.push_back(Fn<uint32_t(__cdecl*)(void*, void*)>(original, 0x0048CAF0)(nullptr, nullptr) & 0xFF);
                    break;
                case 10:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x0048CB00)(0x7C000000);
                    break;
                case 11: {
                    auto* h = reinterpret_cast<uint8_t*>(Bump(0x14));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048CC30)(h)));
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048CC50)(h, random.Below(4))));
                    break;
                }
                case 12:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(const char*, uint32_t, int)>(
                        original, 0x0048CC80)(kNames[random.Below(5)], 0x7D100000, static_cast<int>(random.Next()))));
                    break;
                case 13:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(const char*, uint32_t, int, int, int, int)>(
                        original, 0x0048CCB0)(kNames[random.Below(5)], 0x7D200000, static_cast<int>(random.Next()),
                                              static_cast<int>(random.Next()), static_cast<int>(random.Next()),
                                              static_cast<int>(random.Below(9)))));
                    break;
                case 14:
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__cdecl*)(const char*, uint32_t, uint32_t, int, int, int, int)>(original, 0x0048CD80)(
                            kNames[random.Below(5)], 0x7D300000, 0x7D400000, static_cast<int>(random.Next()),
                            static_cast<int>(random.Next()), static_cast<int>(random.Next()),
                            static_cast<int>(random.Below(9)))));
                    break;
                case 15:
                    if (!scenes.empty()) {
                        const char* name = random.Below(2) ? "bravo" : "zulu";
                        CScene* scene = scenes[random.Below(scenes.size())];
                        const char* sceneName = nullptr;
                        for (const char* n : kNames) {
                            if (Crc::GenerateCRCFromString(n) == scene->m_checksum)
                                sceneName = n;
                        }
                        trace.push_back(ArenaRelocate(
                            Fn<uint32_t(__cdecl*)(const char*, const char*)>(original, 0x0048CDD0)(sceneName, name)));
                    }
                    break;
                default:
                    trace.push_back(
                        Fn<uint32_t(__cdecl*)(const char*)>(original, 0x0048CEF0)(kNames[random.Below(5)]) & 0xFF);
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "scene list: first node, by name, main / sky, unloaded, shadow caster, statistics, collision grids, "
              "sector names, scenes made / loaded / added / reloaded, the head");
}

} // namespace

SELFTEST(Early, TestEngineSceneList, RemoveStandIns);
