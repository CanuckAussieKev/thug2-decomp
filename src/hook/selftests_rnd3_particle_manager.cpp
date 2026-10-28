// Self-tests for the engine's flexible particle manager (src/game/gfx/flexible_particle_manager.cpp): the original and
// ours on a manager and stand-in systems in an arena - the memory manager's heaps and contexts, the script values, the
// factory, the camera and the systems' calls stood in; the calls, results and memory compared after each step.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particle_system.h"
#include "game/gfx/pc_flexible_particles.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CFlexibleParticleManager;
using Nx::CParticleSystem;

// What the stand-ins answer.
int g_heapSize;                 // the script integer 0x91CB7552
float g_frameRate;              // the script float 0x3214C818 (read by the patched code below)
bool g_heapFails;               // AddHeap gives null
uint32_t g_madeFlags;           // the factory's system: its flags (4: everything allocated)
bool g_factoryFails;            // the factory gives null
Lcg* g_random;
alignas(16) uint8_t g_camera[0x400];
void* g_systemVtable[4];

void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node->prev = node;
}

float RandomCoordinate(Lcg& random)
{
    switch (random.Below(6)) {
    case 0:
        return 0.0f;
    case 1:
        return static_cast<float>(random.Below(5)) * 0.5f; // (ties)
    case 2:
        return static_cast<float>(random.Below(2000000)) - 1000000.0f; // (far: the differences overflow an int)
    default:
        return static_cast<float>(random.Below(20000)) / 7.0f - 1400.0f;
    }
}

// A system as the factory makes it: random fields, a recording vtable (slot 0 records and unlinks it).
CParticleSystem* NewSystem(Lcg& random, uint32_t name)
{
    auto* system = reinterpret_cast<CParticleSystem*>(Bump(0x150));
    auto* bytes = reinterpret_cast<uint8_t*>(system);
    for (size_t i = 0; i < 0x150; ++i)
        bytes[i] = static_cast<uint8_t>(random.Next());
    system->m_vtable = g_systemVtable;
    system->m_node.data = system;
    system->m_node.next = system->m_node.prev = &system->m_node;
    system->m_name = name;
    system->m_groupId = random.Below(3);
    system->m_wantsTime = random.Below(4) != 0;
    system->m_priority = static_cast<uint8_t>(random.Below(3));
    system->unknown12E = static_cast<uint8_t>(random.Below(3));
    system->m_flags = g_madeFlags;
    system->m_pos.x = RandomCoordinate(random);
    system->m_pos.y = RandomCoordinate(random);
    system->m_pos.z = RandomCoordinate(random);
    for (float& f : system->m_colourScale)
        f = static_cast<float>(random.Below(100)) / 50.0f;
    return system;
}

void StandInManagerCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BB4E0, AddressOf(&Mem::Manager::AddHeap)}, 12, [](Regs& r) {
        Record({0xB4E0, Relocate(r.args[0]), r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return g_heapFails ? 0u : Bump(0x40);
    });
    StandInThis(s, {0x005BABC0, AddressOf(&Mem::Manager::DeleteHeap)}, 1);
    StandInThis(s, {0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 1);
    StandInThis(s, {0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0);
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return static_cast<uint32_t>(g_heapSize);
    });
    // GetFloat returns in ST0: FLD dword [g_frameRate]; RET
    std::vector<uint8_t> fld = {0xD9, 0x05};
    std::vector<uint8_t> address = Dword(reinterpret_cast<uint32_t>(&g_frameRate));
    fld.insert(fld.end(), address.begin(), address.end());
    fld.push_back(0xC3);
    s.Bytes(0x00478F00, fld);
    s.Bytes(AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat)), fld);
    s.At({0x004BCEF0, AddressOf(&Nx::CreateFlexibleParticles)}, 0, [](Regs& r) {
        Record({0xCEF0});
        for (int i = 0; i < 11; ++i)
            Record({Relocate(r.args[i])});
        if (g_factoryFails)
            return 0u;
        return reinterpret_cast<uint32_t>(NewSystem(*g_random, r.args[0]));
    });
    s.At({0x00496FA0, AddressOf(&CParticleSystem::RenderIfVisible)}, 4, [](Regs& r) {
        Record({0x6FA0, Relocate(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Record({0x9C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_camera);
    });
    g_systemVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xF00, Relocate(r.ecx), r.args[0]});
        Unlink(&reinterpret_cast<CParticleSystem*>(r.ecx)->m_node);
        return r.ecx;
    });
    for (int slot = 1; slot < 4; ++slot)
        g_systemVtable[slot] = s.Thunk(0, [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            return 0u;
        });
}

// Rounds of random steps on a manager made by Construct (or not), each step's calls, results and the memory recorded.
void TestFlexibleParticleManager()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00700EF4, 4}, {0x006FA7B8, 4},
                                          {0x006FA7D4, 1}, {0x00658555, 1}});
    StandIns& s = *new StandIns();
    StandInManagerCalls(s);
    g_relocate = ArenaRelocate;
    const uint32_t kConstruct = 0x0049AF20, kDestroyBody = 0x0049AA10, kDestroy = 0x0049B040,
                   kDeleteTemp = 0x0049A9D0, kCreateTemp = 0x0049AA80, kHasTemp = 0x0049A8E0, kRender = 0x0049AB50,
                   kFind = 0x0049AC20, kCreate = 0x0049AC60, kRemove = 0x0049AD60, kNow = 0x0049AD90,
                   kWhenEmpty = 0x0049ADB0, kGroup = 0x0049ADE0, kNonPersistent = 0x0049AE30, kColour = 0x0049AE80,
                   kHead = 0x0049AEF0, kCompare = 0x0049A8F0;
    bool ok = true, reported = false;
    for (int round = 0; round < 90; ++round) {
        ok &= CompareTwoRuns("flexible particle manager", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x5E00 + round);
            g_random = &random;
            g_madeFlags = 4;
            std::memset(g_camera, 0, sizeof(g_camera));
            g_factoryFails = false;
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<CFlexibleParticleManager*>(Bump(sizeof(CFlexibleParticleManager)));
            // constructed: the heap size given or not, the frame rate 0 / a value / NaN, allocations failing
            g_heapSize = random.Below(2) ? static_cast<int>(random.Below(0x8000)) : 0;
            static const float kRates[] = {0.0f, 2.5f, 60.0f, -0.0f, 3.0f, -2.0f};
            g_frameRate = round % 17 == 3 ? std::numeric_limits<float>::quiet_NaN() : kRates[random.Below(6)];
            g_heapFails = random.Below(8) == 0;
            int failAt = random.Below(4) == 0 ? 1 + static_cast<int>(random.Below(3)) : 0;
            for (int i = 1; i <= failAt; ++i)
                g_failAllocations.push_back(i == failAt);
            if (round % 9 == 4) // (the region's, then nothing: no list's head)
                g_failAllocations = {false, false, true, true, true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, kConstruct)(manager)));
            g_failAllocations.clear();
            trace.push_back(GameVariable<uint32_t>(0x00700EF4));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            if (!manager->mp_systems) { // (no list: only what works without one)
                Fn<void(__thiscall*)(void*, const float*)>(original, kColour)(manager, nullptr);
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, kFind)(manager, 1)));
                Fn<void(__thiscall*)(void*, bool)>(original, kRender)(manager, true);
                Fn<void(__thiscall*)(void*)>(original, kDestroyBody)(manager);
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                DumpArena(trace);
                return trace;
            }
            for (int step = 0; step < 40; ++step) {
                g_trace.clear();
                uint32_t name = 0x100 + random.Below(6);
                switch (random.Below(12)) {
                case 0:
                case 1:
                case 2: { // a new system
                    g_madeFlags = random.Below(6) ? (random.Next() | 4u) : (random.Next() & ~4u);
                    g_factoryFails = random.Below(10) == 0;
                    float unknown = static_cast<float>(random.Below(1000)) / 3.0f;
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, int, int,
                                                                           uint32_t, uint32_t, int, int, float, int,
                                                                           int, bool, const char*)>(original, kCreate)(
                        manager, name, random.Next(), static_cast<int>(random.Below(50)),
                        static_cast<int>(random.Below(3)), random.Next(), random.Next(),
                        static_cast<int>(random.Below(2)), static_cast<int>(random.Below(9)), unknown,
                        static_cast<int>(random.Below(5)), random.Below(3) ? 0 : static_cast<int>(random.Next()),
                        random.Below(2) != 0, reinterpret_cast<const char*>(0x00646164))));
                    break;
                }
                case 3: { // found; its flags and drawing changed (as the systems' own code does)
                    uint32_t found = Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, kFind)(manager, name);
                    trace.push_back(ArenaRelocate(found));
                    if (found && random.Below(2)) {
                        auto* system = reinterpret_cast<CParticleSystem*>(found);
                        system->m_flags = random.Next() & 0x1FFFE;
                        system->m_wantsTime = random.Below(2) != 0;
                    }
                    break;
                }
                case 4:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, random.Below(2) ? kNow : kWhenEmpty)(manager, name);
                    break;
                case 5:
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*, uint32_t)>(original, kGroup)(manager, random.Below(3));
                    else
                        Fn<void(__thiscall*)(void*)>(original, kNonPersistent)(manager);
                    break;
                case 6: {
                    float rgba[4];
                    for (float& f : rgba)
                        f = static_cast<float>(random.Below(256)) / 128.0f;
                    Fn<void(__thiscall*)(void*, const float*)>(original, kColour)(manager, rgba);
                    break;
                }
                case 7: { // drawn: viewport, paused, forced; the camera somewhere
                    GameVariable<int>(0x006FA7B8) = static_cast<int>(random.Below(2));
                    GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(random.Below(2));
                    auto* camera = reinterpret_cast<Gfx::Camera*>(g_camera);
                    camera->GetPos()->x = RandomCoordinate(random);
                    camera->GetPos()->y = RandomCoordinate(random);
                    camera->GetPos()->z = RandomCoordinate(random);
                    Fn<void(__thiscall*)(void*, bool)>(original, kRender)(manager, random.Below(2) != 0);
                    break;
                }
                case 8: { // a system taken out of the list (or null)
                    CParticleSystem* system =
                        reinterpret_cast<CParticleSystem*>(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, kFind)(
                            manager, name));
                    Fn<void(__thiscall*)(void*, void*)>(original, kRemove)(manager, random.Below(4) ? system : nullptr);
                    break;
                }
                case 9: // a temporary heap: made (any size; the region's memory failing) or gone
                    if (random.Below(2)) {
                        g_heapFails = random.Below(4) == 0;
                        if (random.Below(5) == 0)
                            g_failAllocations.push_back(random.Below(2) != 0);
                        trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, kCreateTemp)(
                                            manager, random.Below(0x10000)) &
                                        0xFF);
                        g_failAllocations.clear();
                    } else {
                        Fn<void(__thiscall*)(void*)>(original, kDeleteTemp)(manager);
                    }
                    break;
                case 10:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, kHasTemp)(manager) & 0xFF);
                    break;
                default: { // two systems compared directly
                    CParticleSystem* a = NewSystem(random, 0);
                    CParticleSystem* b = random.Below(4) ? NewSystem(random, 0) : a;
                    if (random.Below(3) == 0)
                        b->m_pos = a->m_pos;
                    trace.push_back(static_cast<uint32_t>(
                        Fn<int(__cdecl*)(const void*, const void*)>(original, kCompare)(&a, &b)));
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                memory.Dump(trace);
            }
            g_trace.clear();
            // destroyed: the body, or freed (or not); the list's head through its own vtable when that's all
            switch (round % 3) {
            case 0:
                Fn<void(__thiscall*)(void*)>(original, kDestroyBody)(manager);
                break;
            case 1:
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, kDestroy)(manager, (round / 3) & 1)));
                break;
            default: {
                auto* other = reinterpret_cast<Mem::ListNode*>(Bump(sizeof(Mem::ListNode)));
                other->next = other->prev = manager->mp_systems;
                manager->mp_systems->next = manager->mp_systems->prev = other;
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, kHead)(
                    manager->mp_systems, (round / 3) & 1)));
                break;
            }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "flexible particle manager: construct, create / find / destroy / kill / groups / colour / remove, "
              "temporary heap, drawing far to near, destroy");
}

} // namespace

SELFTEST(Early, TestFlexibleParticleManager, RemoveStandIns);
