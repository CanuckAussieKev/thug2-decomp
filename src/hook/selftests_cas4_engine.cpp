// Self-tests for the engine's own entry points (src/game/gfx/engine.cpp): original and ours with every engine call
// stood in by recording code (the platform, screen, viewports, managers, scenes, the mesh loads, the C runtime's
// sprintf for the original), on a stand-in list of scenes in an arena; random sequences of every function, the
// calls, results, strings, globals and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/particles.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_font.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/render.h"
#include "game/gfx/scene_load.h"
#include "game/gfx/screen.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/viewport.h"
#include "game/script/struct.h"
#include "hook/cas4_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace Cas4Tests;

Lcg* g_rand;
void* g_sceneSlots[4];
void* g_weatherSlots[4];
const char g_platform[] = "Xbx";
Script::CSymbolTableEntry* g_symbols[0x1000];

uint32_t Answer() { return g_rand->Below(3) ? 1u : 0u; }

void StandInEngineCalls(StandIns& s)
{
    StandInMemory(s);
    // (a cdecl call recorded with its arguments; `strings`: which arguments are strings, recorded by their text)
    auto call = [&s](std::initializer_list<uint32_t> addresses, int args, uint32_t strings, bool answer) {
        uint32_t tag = *addresses.begin() & 0xFFFFFF;
        s.At(addresses, 0, [tag, args, strings, answer](Regs& r) {
            Record({tag});
            for (int i = 0; i < args; ++i)
                Record({strings & (1u << i) ? Hash(reinterpret_cast<const char*>(r.args[i])) : Relocate(r.args[i])});
            return answer ? Answer() : 0u;
        });
    };
    auto method = [&s](std::initializer_list<uint32_t> addresses, int args, bool answer) {
        uint32_t tag = *addresses.begin() & 0xFFFFFF;
        s.At(addresses, static_cast<uint16_t>(4 * args), [tag, args, answer](Regs& r) {
            Record({tag, Relocate(r.ecx)});
            for (int i = 0; i < args; ++i)
                Record({Relocate(r.args[i])});
            return answer ? Answer() : r.ecx;
        });
    };
    using namespace Nx;
    call({0x004B3FD0, AddressOf(&AllocateTextPool)}, 1, 0, false);
    call({0x004CD8A0, AddressOf(&CreateWindowPool)}, 0, 0, false);
    call({0x004A0780, AddressOf(&SetScreenAspectRatio)}, 1, 0, false);
    call({0x004A07A0, AddressOf(&SetScreenAngle)}, 1, 0, false);
    call({0x004A0B70, AddressOf(&SetScreenFieldOfView)}, 1, 0, false);
    call({0x004A0840, AddressOf(&CreateViewports)}, 0, 0, false);
    call({0x004A0AD0, AddressOf(&SetScreenMode)}, 1, 0, false);
    method({0x0049AF20, AddressOf(&CFlexibleParticleManager::Construct)}, 0, false);
    method({0x0048E3E0}, 0, false);
    call({0x00492150, AddressOf(&Engine::InitEffects)}, 0, 0, false);
    call({0x004B25F0, AddressOf(&StartEngine)}, 0, 0, false);
    call({0x004B2750, AddressOf(&FinishRendering)}, 0, 0, false);
    call({0x004A0760, AddressOf(&DeleteMarkedCameras)}, 0, 0, false);
    call({0x00491790, AddressOf(&Engine::UpdateFlashes)}, 0, 0, false);
    call({0x004919C0, AddressOf(&Engine::UpdateSpawnedPolys)}, 0, 0, false);
    call({0x00491F70, AddressOf(&Engine::AgeAllTextureSplats)}, 0, 0, false);
    call({0x00492000, AddressOf(&Engine::UpdateShatters)}, 0, 0, false);
    method({0x00496680, AddressOf(&CParticleManager::UpdateParticles)}, 0, false);
    method({0x0048E2D0}, 0, false);
    call({0x004904E0}, 0, 0, false);
    call({0x004B2D80, AddressOf(&RenderWorld)}, 0, 0, false);
    call({0x004B3AD0, AddressOf(&LoadMeshFromMemory)}, 5, 0, true);
    call({0x004B3A20, AddressOf(&LoadMeshFile)}, 2, 1, true);
    call({0x004B6920, AddressOf(&DeleteModelLights)}, 1, 0, true);
    call({0x004DB570, AddressOf(&SphereOutsideView), AddressOf(&SphereVisible)}, 2, 0, true);
    call({0x004B2990, AddressOf(&UnloadScene)}, 1, 0, true);
    s.At({0x004B2320, AddressOf(&PlatformExtension)}, 0, [](Regs&) {
        Record({0x2320});
        return Ptr(g_platform);
    });
    s.At({0x006234FE}, 0, [](Regs& r) { // (the original's C runtime sprintf: done by ours)
        return StandInSprintf(r);
    });
    method({0x0049CB00, AddressOf(&CScene::GetSector)}, 1, true);
    method({0x0049DDE0, AddressOf(&CScene::ToggleAddScene)}, 0, true);
    method({0x0049E200, AddressOf(&CScene::UnloadAddedScene)}, 0, true);
    s.At({0x0049C910, AddressOf(&CScene::FinishLoad)}, 8, [](Regs& r) {
        Record({0xC910, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Relocate(r.args[1])});
        return 0u;
    });
    g_sceneSlots[0] = s.Thunk(4, [](Regs& r) {
        Record({0x5CE0, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_weatherSlots[2] = s.Thunk(4, [](Regs& r) {
        Record({0x3EA2, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
}

// Random sequences of the engine's entry points over a list of stand-in scenes (both runs alike).
void TestEngineEntryPoints()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA720, 0x20}, {0x006FA7BC, 0x10},
                                          {0x006FA7D4, 8}, {0x006FA7E0, 0x14}, {0x007D1EC0, 1}, {0x00701378, 4},
                                          {0x006F3624, 4}});
    StandIns& s = *new StandIns();
    StandInEngineCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareRuns("engine entry points", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            std::memset(g_symbols, 0, sizeof(g_symbols));
            GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_symbols; // (camera_fov: none, 0)
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x9D00 + round);
            Lcg answers(0xAE00 + round);
            g_rand = &answers;
            uint32_t particles = Bump(0x20);
            uint32_t weather = Bump(0x10);
            *reinterpret_cast<void***>(weather) = g_weatherSlots;
            GameVariable<uint32_t>(0x006FA7C4) = particles;
            GameVariable<uint32_t>(0x006FA7C8) = weather;
            GameVariable<uint32_t>(0x006FA7BC) = Bump(8);
            // the scenes' list: its head (data -1) and some scenes
            auto& head = GameVariable<Mem::ListNode>(0x006FA7E0);
            head.vtable = reinterpret_cast<void**>(0x0064A001);
            head.data = reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFF));
            head.priority = 0;
            head.next = &head;
            head.prev = &head;
            std::vector<uint32_t> scenes;
            int count = static_cast<int>(random.Below(5));
            for (int i = 0; i < count + 2; ++i) {
                uint32_t scene = Bump(0x284);
                *reinterpret_cast<void***>(scene) = g_sceneSlots;
                reinterpret_cast<uint32_t*>(scene)[0x84 / 4] = 0xC0 + random.Below(4);
                scenes.push_back(scene);
                if (i >= count)
                    continue; // (two scenes not in the list)
                auto* node = reinterpret_cast<Mem::ListNode*>(Bump(0x14));
                node->vtable = reinterpret_cast<void**>(0x00648A38);
                node->data = reinterpret_cast<void*>(scene);
                node->priority = 0;
                node->prev = head.prev;
                node->next = &head;
                head.prev->next = node;
                head.prev = node;
            }
            char* out = reinterpret_cast<char*>(Bump(0x200));
            static const char* const kNames[] = {"skaters\\Ped_M_Head.skin", "MODELS\\Board.MDL", "levels\\NJ\\nj",
                                                 "a.skin.mdl", "Plain", "x.mdl.skin"};
            std::vector<uint32_t> trace;
            for (int step = 0; step < 30; ++step) {
                uint32_t op = random.Below(22);
                trace.push_back(0xC0DE0000 + op);
                switch (op) {
                case 0:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x0048C1D0)(random.Below(500));
                    break;
                case 1:
                    Fn<void(__cdecl*)()>(original, 0x0048BF20)();
                    break;
                case 2:
                    GameVariable<uint8_t>(0x007D1EC0) = static_cast<uint8_t>(random.Below(2));
                    Fn<void(__cdecl*)()>(original, 0x0048C330)();
                    break;
                case 3:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x0048C0A0)(random.Next());
                    break;
                case 4:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x0048C0B0)()));
                    break;
                case 5:
                    if (random.Below(4) == 0)
                        GameVariable<int>(0x006FA734) = static_cast<int>(random.Below(7)) - 1;
                    Fn<void(__cdecl*)()>(original, 0x0048C0D0)();
                    break;
                case 6:
                    if (random.Below(4) == 0)
                        GameVariable<uint8_t>(0x006FA7D6) = static_cast<uint8_t>(random.Next());
                    Fn<void(__cdecl*)()>(original, 0x0048C0F0)();
                    break;
                case 7:
                    Fn<void(__cdecl*)(int, int)>(original, 0x0048C110)(static_cast<int>(random.Next()),
                                                                       static_cast<int>(random.Next()));
                    break;
                case 8:
                    GameVariable<uint32_t>(0x006FA7D8) = random.Next();
                    trace.push_back(Fn<uint32_t(__cdecl*)()>(original, 0x0048C1C0)());
                    break;
                case 9: {
                    std::memset(out, 0x5A, 0x200);
                    trace.push_back(Fn<uint8_t(__cdecl*)(const char*, char*)>(original, 0x0048BFE0)(
                        kNames[random.Below(6)], out));
                    trace.push_back(Hash(out));
                    break;
                }
                case 10:
                    trace.push_back(Fn<uint32_t(__cdecl*)(uint32_t, const uint8_t*, int, void*, void*, int, uint32_t,
                                                          uint32_t)>(original, 0x0048C030)(
                        random.Next(), reinterpret_cast<const uint8_t*>(Bump(16)), static_cast<int>(random.Below(99)),
                        reinterpret_cast<void*>(random.Below(2) ? Bump(8) : 0), reinterpret_cast<void*>(Bump(8)),
                        static_cast<int>(random.Next()), random.Next(), random.Next()));
                    break;
                case 11:
                    std::memset(out, 0x5A, 0x200);
                    Fn<void(__cdecl*)(char*, const char*, uint32_t)>(original, 0x0048C390)(out, kNames[random.Below(6)],
                                                                                             random.Next());
                    trace.push_back(Hash(out));
                    break;
                case 12:
                    trace.push_back(Fn<uint8_t(__cdecl*)(void*)>(original, 0x0048C3C0)(
                        reinterpret_cast<void*>(random.Below(4) ? Bump(16) : 0)));
                    break;
                case 13:
                    trace.push_back(Fn<uint32_t(__cdecl*)(const char*, void*, int, bool)>(original, 0x0048C3D0)(
                        kNames[random.Below(6)], reinterpret_cast<void*>(Bump(8)), static_cast<int>(random.Below(3)),
                        random.Below(2) != 0));
                    break;
                case 14: {
                    GameVariable<int>(0x00701378) = static_cast<int>(random.Below(4));
                    float centre[4] = {1.0f, 2.0f, 3.0f, 1.0f};
                    trace.push_back(Fn<uint32_t(__cdecl*)(const float*, float)>(original, 0x0048C490)(
                        centre, static_cast<float>(random.Below(50))));
                    break;
                }
                case 15:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048C510)(random.Next())));
                    break;
                case 16: {
                    uint32_t scene = Bump(0x284);
                    *reinterpret_cast<void***>(scene) = g_sceneSlots;
                    scenes.push_back(scene);
                    Fn<void(__cdecl*)(uint32_t, const char*, void*, bool, void*)>(original, 0x0048C560)(
                        scene, kNames[random.Below(6)], reinterpret_cast<void*>(Bump(8)), random.Below(2) != 0,
                        reinterpret_cast<void*>(Bump(8)));
                    break;
                }
                case 17:
                    Fn<void(__cdecl*)()>(original, 0x0048C650)();
                    break;
                case 18: {
                    uint32_t scene = scenes[random.Below(static_cast<uint32_t>(scenes.size()))];
                    trace.push_back(Fn<uint8_t(__cdecl*)(uint32_t, int)>(original, 0x0048C690)(
                        scene, static_cast<int>(random.Below(3))));
                    break;
                }
                case 19:
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048C700)(0xC0 + random.Below(5))));
                    break;
                case 20: { // a node unlinked by its destructor (body, or deleting)
                    Mem::ListNode* node = head.next;
                    if (node == &head)
                        break;
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x0048C4B0)(node);
                    else
                        trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                            original, 0x0048C4D0)(node, random.Below(2) | 2u)));
                    break;
                }
                default: // more viewports or fewer
                    GameVariable<int>(0x00701378) = static_cast<int>(random.Below(3));
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                g_trace.clear();
            }
            DumpArena(trace);
            for (auto& [address, size] : memory.ranges) {
                if (address == 0x006F3624 || address == 0x007D1800)
                    continue;
                for (uint32_t i = 0; i < size; i += 4) {
                    uint32_t word = 0;
                    std::memcpy(&word, reinterpret_cast<uint8_t*>(address) + i, size - i < 4 ? size - i : 4);
                    trace.push_back(ArenaRelocate(word));
                }
            }
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "engine entry points: started, frames, the setters and toggles, skin names, model loads, scene file "
              "names, deletes, the one-viewport sphere test, the scene list (sectors, added, toggled, unloaded, "
              "found, nodes destroyed)");
}

} // namespace

SELFTEST(Early, TestEngineEntryPoints, RemoveStandIns);
