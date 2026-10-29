// Self-tests for CAS2-1: the animation track (src/game/gfx/anim_track.cpp) - the original and ours on identical arena
// memory: a track with stand-in controllers (a recording vtable), events (their own vtable, their methods stood in),
// an object, script structures and the pools / memory manager stood in with answers drawn from one random sequence;
// random operations in turn (updates and fades, weights, events loaded / fired / deleted, controllers added / found
// / inserted, member functions, PlayAnim, destruction, the events' destructors), the calls out and the arena
// compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/anim_controller.h"
#include "game/gfx/anim_track.h"
#include "game/gfx/custom_anim_key.h"
#include "game/obj/anim_blender.h"
#include "game/obj/flip_rotate.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/anim_testkit.h"
#include "hook/appmain_components.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;
using Gfx::CAnimTrack;
using Script::CStruct;

constexpr uint32_t kType = 0x7321A8D6;
const uint32_t kControllerTypes[] = {0x7E1940A1, 0x26205DB6, 0x659BF355, 0x6D941203,
                                     0xCB4533FF, 0xCFC5B380, 0xDF5C091A, 0x12345678};

Lcg g_answer(1);                       // every stood-in answer, drawn in call order
std::map<uint32_t, uint32_t> g_types;  // a made structure -> the type added to it
uint32_t g_ids[8];                     // controller ids to look for
uint32_t g_controllerVtable[6];
uint32_t g_fakeObject, g_fakeComponent, g_fakeTable, g_fakeArray, g_fakeEntries, g_fakeStruct;
int g_controllersMade;

float Weird(Lcg& random)
{
    switch (random.Below(10)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::numeric_limits<float>::quiet_NaN();
    case 3:
        return -RandomFloat(random, 0, 3);
    default:
        return RandomFloat(random, 0, 4);
    }
}

// A stood-in controller: the vtable, an id, a priority (left as made).
uint32_t MakeController(uint32_t at)
{
    auto* c = reinterpret_cast<Gfx::CAnimController*>(at);
    c->m_vtable = reinterpret_cast<void**>(g_controllerVtable);
    c->m_id = g_ids[g_controllersMade++ % 8];
    return at;
}

void StandInTrackCalls(StandIns& s)
{
    StandInMemory(s);
    // The script structures' pools and the memory system.
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Relocate(r.ecx)});
        return Bump(16);
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Record({0xA3B0, Relocate(r.ecx), Relocate(r.args[0])});
        return g_answer.Below(2);
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Record({0x1950});
        return 0u;
    });
    AtFloatResult(s, {0x005BEAA0, AddressOf(&Tmr::GetSlomoFrameSeconds)}, 0, [](Regs&) {
        Record({0xEAA0});
        float values[] = {1.0f / 60.0f, 0.02f, 0.5f, 0.0f, 3.0f};
        return values[g_answer.Below(5)];
    });
    // The script structures.
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Relocate(r.ecx)});
        g_types.erase(r.ecx);
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7130, Relocate(r.ecx)});
        return 0u;
    });
    // (Script::DeleteStruct 0x00409070 runs: the pools below record it)
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7D40, Relocate(r.ecx), r.args[0], r.args[1]});
             if (r.args[0] == kType)
                 g_types[r.ecx] = r.args[1];
             return 0u;
         });
    auto adder = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
            return 0u;
        };
    };
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         adder(0x7C60));
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8,
         adder(0x7B80));
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8,
         adder(0x8670));
    // Getters: found or not, the value drawn (the type of a structure made here: what was added).
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            uint32_t name = r.args[0];
            Record({tag, Relocate(r.ecx), name, r.args[2] & 0xFF});
            auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
            if (tag == 0x6950 && name == kType) {
                auto it = g_types.find(r.ecx);
                *out = it != g_types.end() ? it->second : kControllerTypes[g_answer.Below(8)];
                return 1u;
            }
            bool always = tag == 0x6950 && (name == 0xC451F558 || name == 0x98549BA4); // (event, anim)
            if (!always && g_answer.Below(4) == 0)
                return 0u;
            switch (tag) {
            case 0x6630: // integers: a frame or a priority
                *out = g_answer.Below(900) - 300;
                break;
            case 0x66D0:
                *out = std::bit_cast<uint32_t>(Weird(g_answer));
                break;
            case 0x6950:
                *out = g_answer.Below(3) ? g_ids[g_answer.Below(8)] : 0x900 + g_answer.Below(4);
                break;
            case 0x67C0:
                if (g_answer.Below(3) == 0)
                    return 0u;
                *out = g_fakeStruct;
                break;
            case 0x6860: // the events: up to four
                reinterpret_cast<Script::CArray*>(g_fakeArray)->m_size = g_answer.Below(5);
                *out = g_fakeArray;
                break;
            }
            return 1u;
        };
    };
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, getter(0x6950));
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, getter(0x67C0));
    s.At({0x00476860,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, getter(0x6860));
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Relocate(r.ecx), r.args[0]});
        return g_answer.Below(2);
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return g_answer.Below(5) ? g_fakeTable : 0u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Relocate(r.ecx), r.args[0]});
        return g_fakeEntries + 16 * r.args[0];
    });
    // The object and its anim blender.
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Relocate(r.ecx), r.args[0]});
        return g_answer.Below(5) ? g_fakeComponent : 0u;
    });
    s.At({0x00419AD0, AddressOf(&Obj::CAnimBlenderComponent::GetFirstChannel58)}, 0, [](Regs& r) {
        Record({0x9AD0, Relocate(r.ecx)});
        return g_answer.Below(5) ? 0xE7E7u : 0u;
    });
    // The timeline's and the track's own (not rewritten here), the events'.
    StandInThis(s, {0x00481690, AddressOf(&Gfx::CAnimTimeline::DestroyBody)}, 0);
    StandInThis(s, {0x004816B0, AddressOf(&Gfx::CAnimTimeline::AddTime)}, 1);
    StandInThis(s, {0x00481870, AddressOf(&Gfx::CAnimTrack::DeleteControllers)}, 0);
    StandInThis(s, {0x004818B0, AddressOf(&Gfx::CAnimTrack::RemoveController)}, 1);
    StandInThis(s, {0x004819A0, AddressOf(&Gfx::CAnimTrack::ArmEvents)}, 0);
    StandInThis(s, {0x004865D0, AddressOf(&Gfx::CAnimScriptKey::Set)}, 3);
    StandInThis(s, {0x00486150, AddressOf(&Gfx::CCustomAnimKey::SetActive)}, 1);
    StandInThis(s, {0x00486560, AddressOf(&Gfx::CAnimScriptKey::DestroyBody)}, 0);
    s.At({0x00486160, AddressOf(&Gfx::CCustomAnimKey::ProcessIfActive)}, 4, [](Regs& r) {
        Record({0x6160, Relocate(r.ecx), Relocate(r.args[0])});
        return g_answer.Below(2);
    });
    s.At({0x004860C0, AddressOf(&Gfx::CCustomAnimKey::WithinRange)}, 12, [](Regs& r) {
        Record({0x60C0, Relocate(r.ecx), r.args[0], r.args[1], r.args[2]});
        return g_answer.Below(2);
    });
    // The controllers' constructors.
    auto construct = [](uint32_t tag, int args) {
        return [tag, args](Regs& r) {
            Record({tag, Relocate(r.ecx)});
            for (int i = 0; i < args; ++i)
                Record({Relocate(r.args[i])});
            return MakeController(r.ecx);
        };
    };
    using namespace Gfx;
    s.At({0x004A5010, AddressOf(&CWeightedAnimController::Construct)}, 8, construct(0x5010, 2));
    s.At({0x00488300}, 8, construct(0x8300, 2));
    s.At({0x004A5350, AddressOf(&CPartialAnimController::Construct)}, 4, construct(0x5350, 1));
    s.At({0x004A49F0, AddressOf(&CWobbleController::Construct)}, 4, construct(0x49F0, 1));
    s.At({0x004A4500, AddressOf(&CStandardAnimController::Construct)}, 4, construct(0x4500, 1));
    s.At({0x004A5730, AddressOf(&CPoseController::Construct)}, 4, construct(0x5730, 1));
    s.At({0x004E2C50, AddressOf(&Obj::CFlipRotateComponent::Construct)}, 4, construct(0x2C50, 1));
    s.At({0x004A4F30, AddressOf(static_cast<void* (*)(size_t, int)>(&CStandardAnimController::operator new))}, 0,
         [](Regs& r) {
             Record({0x4F30, r.args[0], r.args[1]});
             return Bump(r.args[0]);
         });
    // The controllers' vtable: 0 Destroy, 1 GetPose, 2, 3 CallMemberFunction (mostly "not mine"), 4 InitFromStructure,
    // 5 Update.
    const int args[6] = {1, 1, 1, 3, 1, 0};
    for (int slot = 0; slot < 6; ++slot) {
        g_controllerVtable[slot] = reinterpret_cast<uint32_t>(
            s.Thunk(static_cast<uint16_t>(4 * args[slot]), [slot, count = args[slot]](Regs& r) {
                Record({0xC000u + slot, Relocate(r.ecx)});
                for (int i = 0; i < count; ++i)
                    Record({Relocate(r.args[i])});
                if (slot == 3)
                    return g_answer.Below(3) ? 2u : g_answer.Below(2);
                return r.ecx;
            }));
    }
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// A track in the arena: random state, controllers, events (or none), fade, times.
CAnimTrack* MakeTrack(Lcg& random)
{
    auto* t = reinterpret_cast<CAnimTrack*>(Bump(sizeof(CAnimTrack)));
    t->m_vtable = reinterpret_cast<void**>(0x00648794);
    t->m_state = static_cast<int>(random.Below(3));
    t->m_controllerCount = static_cast<int>(random.Below(4));
    for (int i = 0; i < 6; ++i) {
        t->mp_controllers[i] = reinterpret_cast<Gfx::CAnimController*>(MakeController(Bump(0x20)));
        t->mp_controllers[i]->m_priority = static_cast<int>(random.Below(4)) * 100 + (random.Below(2) ? 500 : 0) - 200;
    }
    t->m_fadeRate = random.Below(4) ? 1.0f / 60.0f : Weird(random);
    t->m_fadeLeft = Weird(random);
    t->m_fadeScale = random.Below(3) ? RandomFloat(random, 0, 2) : Weird(random);
    t->mp_object = reinterpret_cast<Obj::CCompositeObject*>(g_fakeObject);
    t->m_eventsOn = random.Below(4) != 0;
    t->m_direction = random.Below(3) ? 0 : -1;
    t->m_start = Weird(random);
    t->m_current = RandomFloat(random, 0, 3);
    t->m_end = random.Below(5) ? RandomFloat(random, 0.5f, 4) : 0.0f;
    t->m_anim = 0xA111;
    t->m_eventCount = random.Below(4);
    if (t->m_eventCount) {
        auto* block = reinterpret_cast<uint32_t*>(Bump(4 + 0x14 * t->m_eventCount));
        block[0] = random.Below(5) ? t->m_eventCount : 0;
        t->mp_events = reinterpret_cast<Gfx::CAnimEvent*>(block + 1);
        for (uint32_t i = 0; i < t->m_eventCount; ++i) {
            t->mp_events[i].m_vtable = reinterpret_cast<void**>(0x006487BC);
            t->mp_events[i].m_frame = static_cast<int>(random.Below(100));
        }
    } else {
        t->mp_events = random.Below(2) ? nullptr : reinterpret_cast<Gfx::CAnimEvent*>(Bump(0x20) + 4);
    }
    return t;
}

void TestAnimTrack()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006F92C8, 16}, {0x006F3614, 16}});
    StandIns& s = *new StandIns();
    StandInTrackCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("animation tracks", reported, [&](bool original) {
            g_trace.clear();
            g_types.clear();
            ResetArena(0xEE);
            if (!original)
                AnimTests::UseOurSlots({{0x00648794, 7}, {0x006487BC, 3}, {0x006487B0, 3}});
            Lcg random(0xCA52 + round);
            g_answer = Lcg(0x7AC0 + round);
            g_controllersMade = 0;
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<uint8_t>(0x007CC979) = 1;
            for (uint32_t pairs : {0x006F92C8u, 0x006F3614u}) {
                auto& p = GameVariable<Mem::PoolPair>(pairs);
                p.current = static_cast<int>(random.Below(2));
                p.unknown4 = 0;
                p.pools[0] = random.Below(3) ? reinterpret_cast<Mem::Pool*>(Bump(0x10)) : nullptr;
                p.pools[1] = random.Below(3) ? reinterpret_cast<Mem::Pool*>(Bump(0x10)) : nullptr;
            }
            for (uint32_t& id : g_ids)
                id = 0x1D00 + random.Below(6);
            g_fakeObject = Bump(0x40);
            g_fakeComponent = Bump(0x40);
            g_fakeTable = Bump(0x10);
            g_fakeArray = Bump(0x10);
            g_fakeEntries = Bump(0x40);
            g_fakeStruct = Bump(0x10);
            CAnimTrack* t = MakeTrack(random);
            for (int step = 0; step < 24; ++step) {
                Record({0x57E9, static_cast<uint32_t>(step)});
                switch (random.Below(15)) {
                case 0:
                    Fn<void(__thiscall*)(void*)>(original, 0x00481AC0)(t);
                    break;
                case 1:
                    Record({Fn<uint32_t(__thiscall*)(void*, float)>(original, 0x00481B40)(t, Weird(random)) & 0xFF});
                    break;
                case 2: {
                    double w = Fn<double(__thiscall*)(void*)>(original, 0x00481B80)(t);
                    uint64_t bits = std::bit_cast<uint64_t>(w);
                    Record({static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)});
                    break;
                }
                case 3:
                    Fn<void(__thiscall*)(void*)>(original, 0x00481BB0)(t);
                    break;
                case 4:
                    g_failAllocations = {random.Below(4) == 0}; // (the events' array)
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00481C00)(t, 0xA000 + random.Below(4));
                    g_failAllocations.clear();
                    break;
                case 5:
                    if (t->m_eventCount && !t->mp_events)
                        break;
                    Fn<void(__thiscall*)(void*, float, float)>(original, 0x00481E10)(t, Weird(random), Weird(random));
                    break;
                case 6:
                    if (t->m_controllerCount < 6)
                        Record({Relocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x00481FB0)(
                            t, reinterpret_cast<void*>(g_fakeStruct + 4)))});
                    break;
                case 7:
                    Record({Relocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00481A80)(
                        t, random.Below(4) ? g_ids[random.Below(8)] : 0x777))});
                    break;
                case 8:
                    if (t->m_controllerCount < 6) {
                        int priority = static_cast<int>(random.Below(6)) * 100 - 100;
                        Fn<void(__thiscall*)(void*, void*, int)>(original, 0x00481A30)(
                            t, reinterpret_cast<void*>(MakeController(Bump(0x20))), priority);
                    }
                    break;
                case 9: {
                    if (t->m_controllerCount >= 6)
                        break;
                    const uint32_t names[] = {0x83654874, 0x986D274E, 0xC68365EB, 0xC68365EB, 0x5F495AE0};
                    uint32_t name = names[random.Below(5)];
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(original, 0x00482200)(
                        t, name, reinterpret_cast<void*>(g_fakeStruct + 8),
                        reinterpret_cast<void*>(random.Below(2) ? 0x5C51 : 0))});
                    break;
                }
                case 10: {
                    if (t->m_controllerCount > 2)
                        break;
                    void* params = random.Below(3) ? reinterpret_cast<void*>(g_fakeStruct + 12) : nullptr;
                    Fn<void(__thiscall*)(void*, uint32_t, float, float, int, float, float, uint32_t, void*)>(
                        original, 0x004823C0)(t, 0xA000 + random.Below(4), Weird(random), Weird(random),
                                              static_cast<int>(random.Below(5)), Weird(random), Weird(random),
                                              random.Below(2) ? 0x101u : 0x200u, params);
                    break;
                }
                case 11:
                    // The destructor (by body or slot 0, freed or not); then a new track.
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x00481F20)(t);
                    else
                        Record({Relocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00482340)(
                            t, random.Below(2) ? 0x101u : 0x102u))}); // (a byte: bit 0 frees)
                    t = MakeTrack(random);
                    break;
                case 12:
                    Record({Relocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x00481EE0)(0x74))});
                    Fn<void(__cdecl*)(void*)>(original, 0x00482360)(reinterpret_cast<void*>(Bump(0x74)));
                    break;
                case 13: {
                    // The events' destructors: an array (count before it) or one; the base's.
                    uint32_t count = 1 + random.Below(3);
                    auto* block = reinterpret_cast<uint32_t*>(Bump(4 + 0x14 * count));
                    block[0] = count;
                    auto* events = reinterpret_cast<Gfx::CAnimEvent*>(block + 1);
                    for (uint32_t i = 0; i < count; ++i)
                        Record({Relocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481A20)(&events[i]))});
                    uint32_t flags = random.Below(4) | (random.Below(2) ? 0x40u : 0u);
                    Record({Relocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00481DB0)(events, flags))});
                    Record({Relocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004819F0)(
                        reinterpret_cast<void*>(Bump(0x14)), random.Below(2) ? 0x81u : 0x2u))});
                    break;
                }
                default:
                    t->m_state = static_cast<int>(random.Below(3));
                    t->m_direction = random.Below(2) ? 0 : -1;
                    t->m_fadeLeft = Weird(random);
                    t->m_fadeRate = random.Below(2) ? 0.0f : Weird(random);
                    break;
                }
            }
            EndRunPatches();
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "animation tracks: updates and fades, weights, events loaded / fired / deleted, controllers made / "
              "inserted / found, member functions, PlayAnim, destruction, the class's pools, the events' destructors");
}

} // namespace

SELFTEST(Early, TestAnimTrack, RemoveStandIns);
