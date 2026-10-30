// Self-tests for the colour cycle component (src/game/obj/color_cycle.cpp, 0x0041B280-0x0041B57F) and the camera
// component (camera_component.cpp, 0x0041B6D0-0x0041B903): the original and ours, every call they make stood in for
// and recorded (the allocations - the script structure pool and the manager -, the base component, the script
// structure's getters and adders, the SetObjectColor helper, the clock, the camera, the composite object manager).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/camera.h"
#include "game/obj/camera_component.h"
#include "game/obj/color_cycle.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/script/game_cfuncs.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CCameraComponent;
using Obj::CColorCycleComponent;

struct World {
    alignas(16) uint8_t component[0x90];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t structs[4][0x10];
    alignas(16) uint8_t camera[0xD0];
    alignas(16) uint8_t array[0x10];
    alignas(16) uint8_t vectors[8][0x10];
    alignas(16) uint8_t manager[0x40];
    alignas(16) uint8_t pool[0x80];
    void* managerVtable[8];
    void* cameraVtable[8];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

struct Answers {
    uint32_t seed;
    bool callocFails;
    bool cameraFails;
    bool havePeriod, haveColors;
    int structAt;
    double frameSeconds;
};
Answers g_a;

double __cdecl StandInFrameSeconds()
{
    Record({0xEAA0});
    return g_a.frameSeconds;
}

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (r.args[0] == 0xC4) {
            if (g_a.cameraFails)
                return 0u;
            std::memset(g_world.camera, 0, sizeof(g_world.camera));
            return reinterpret_cast<uint32_t>(g_world.camera);
        }
        if (g_a.callocFails)
            return 0u;
        // (dirty, not zeroed: what the constructor leaves unset shows)
        std::memset(g_world.component, 0xCD, sizeof(g_world.component));
        return reinterpret_cast<uint32_t>(g_world.component);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The script structure's allocation: the pool (when there is one) or the manager.
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.structs[g_a.structAt++ % 4]);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return g_a.seed % 7 == 3 ? 0u : reinterpret_cast<uint32_t>(g_world.structs[g_a.structAt++ % 4]);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Rel(r.ecx)});
        return r.ecx;
    });
    using AddChecksumFn = void (Script::CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&Script::CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddIntegerFn = void (Script::CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&Script::CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005A3E80, AddressOf(CFuncs::SetNodeColor)}, 0, [](Regs& r) {
        Record({0x3E80, r.args[0], Rel(r.args[1])});
        return 1u;
    });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    s.Bytes(0x005BEAA0, Branch(0xE9, 0x005BEAA0, reinterpret_cast<void*>(&StandInFrameSeconds)));
    uint32_t ours = AddressOf(&Tmr::GetSlomoFrameSeconds);
    s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<void*>(&StandInFrameSeconds)));
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x66D0, r.ecx, r.args[0], Rel(r.args[1]), Rel(*reinterpret_cast<uint32_t*>(out)), r.args[2]});
        if (!g_a.havePeriod)
            return 0u;
        *out = 250.0f + static_cast<float>(g_a.seed) * 33.3f;
        return 1u;
    });
    using GetArrayFn = bool (Script::CStruct::*)(uint32_t, Script::CArray**, int);
    s.At({0x00476860, AddressOf(static_cast<GetArrayFn>(&Script::CStruct::GetArray))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6860, r.ecx, r.args[0], Rel(r.args[1]), Rel(*out), r.args[2]});
        if (!g_a.haveColors)
            return 0u;
        *out = reinterpret_cast<uint32_t>(g_world.array);
        return 1u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.vectors[r.args[0] % 8]);
    });
    s.At({0x0046F060, AddressOf(Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The camera.
    s.At({0x00485BA0, AddressOf(&Gfx::Camera::Construct)}, 0, [](Regs& r) {
        Record({0x5BA0, Rel(r.ecx)});
        *reinterpret_cast<void***>(r.ecx) = g_world.cameraVtable;
        return r.ecx;
    });
    s.At({0x00485A70, AddressOf(&Gfx::Camera::SetPos)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x5A70, Rel(r.ecx), Rel(r.args[0]), v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x00485A90, AddressOf(&Gfx::Camera::SetMatrix)}, 4, [](Regs& r) {
        Record({0x5A90, Rel(r.ecx), Rel(r.args[0]), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64)});
        return r.ecx;
    });
    g_world.cameraVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xCA00, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_world.managerVtable[4] = s.Thunk(8, [](Regs& r) {
        Record({0x4004, Rel(r.ecx), Rel(r.args[0]), r.args[1]});
        return 0u;
    });
}

void FillFloats(Lcg& random, uint8_t* p, size_t size, int spread)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(random.Below(2 * spread + 1)) - spread) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0xC0C00000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 2000);
    FillFloats(random, g_world.object, sizeof(g_world.object), 2000);
    FillFloats(random, reinterpret_cast<uint8_t*>(g_world.vectors), sizeof(g_world.vectors), 2000);
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    object->m_id = 0x0B1E0000 + seed;
    auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_world.component);
    c->m_object = object;
    std::memset(g_world.array, 0, sizeof(g_world.array));
    reinterpret_cast<Script::CArray*>(g_world.array)->m_size = seed % 6;
    std::memset(g_world.manager, 0, sizeof(g_world.manager));
    *reinterpret_cast<void***>(g_world.manager) = g_world.managerVtable;
}

constexpr uint32_t kColorAddresses[] = {0x0041B3C0, 0x0041B280, 0x0041B2A0, 0x0041B2E0, 0x0041B310, 0x0041B440,
                                        0x0041B580, 0x0041B560, 0x0041B570, 0x0041B2B0, 0x0041B390};

// Made (and out of memory), registered, destroyed; colours set (0-5, from the script or not), periods; switched on and
// off from each state; updated through several periods (the time reaching the period exactly, NaN, slow motion);
// structures from the pool or the manager (or none: not for the paths that would crash).
void TestColorCycleComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F3614, 16}, {0x007CC979, 1}});
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            memory.Zero();
            GameVariable<bool>(0x007CC979) = true;
            if (seed & 1)
                GameVariable<void*>(0x006F361C) = g_world.pool; // (the current pool, 0: from the manager otherwise)
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kColorAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CColorCycleComponent*>(g_world.component);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.callocFails = seed == 4;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041B3C0))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CColorCycleComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x0041B280))();
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B2A0))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041B2E0))(
                c, seed % 3)));
            ResetWorld(seed);
            Lcg random(0x7A000000 + seed);
            c->m_on = (seed & 2) != 0;
            c->m_current = static_cast<int>(seed % 3);
            c->m_colorCount = 1 + static_cast<int>(seed % 4);
            c->m_time = static_cast<float>(seed % 5) * 40.0f;
            c->m_period = seed % 9 == 7 ? std::numeric_limits<float>::quiet_NaN() : 100.0f + 10.0f * (seed % 3);
            // The members (period / colours from the script or not), the colours directly.
            g_a.havePeriod = seed % 5 != 2;
            g_a.haveColors = seed % 5 != 3;
            if (seed % 4 == 0) {
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041B2B0))(
                    c, params, script));
                if (g_a.haveColors) {
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(
                        Code(original, 0x0041B390))(c, params, script));
                }
            } else if (seed % 4 == 1) {
                reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041B310))(c, g_world.array);
            }
            for (size_t i = 0; i < sizeof(CColorCycleComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            // Switched on / off and updated (time reaching the period exactly, past it, slow motion).
            static const double frames[] = {1.0 / 60.0, 0.1, 0.0123456789, 0.03, 1.0 / 30.0};
            for (int k = 0; k < 10; ++k) {
                uint32_t pick = random.Next();
                if (pick % 5 == 0) {
                    uint32_t which = pick & 0x100 ? 0x0041B560 : 0x0041B570;
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, which))(
                        c, params, script));
                } else if (pick % 5 == 1) {
                    reinterpret_cast<void(__thiscall*)(void*, bool)>(Code(original, 0x0041B440))(c, (pick & 0x200) != 0);
                } else {
                    g_a.frameSeconds = frames[(pick >> 4) % 5];
                    if (pick % 3 == 0) { // (landing exactly on the period, or just short of it before rounding)
                        g_a.frameSeconds = pick & 0x40 ? 0.1 : 0.09999999999;
                        c->m_time = c->m_period - 100.0f;
                    }
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B580))(c);
                }
                trace.push_back(Bits(c->m_time));
                trace.push_back(static_cast<uint32_t>(c->m_current));
                trace.push_back(c->m_on);
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(CColorCycleComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "colour cycle component: made, its colours and period, on / off, updated");
    }
    delete &s;
}

// Made (the camera out of memory too), destroyed with and without a camera; InitFromStructure, Finalize, Update.
void TestCameraComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F0FB8, 4}});
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            GameVariable<void*>(0x006F0FB8) = g_world.manager;
            Originals guard(original, {0x0041B840, 0x0041B730, 0x0041B6D0, 0x0041B7C0, 0x0041B7F0, 0x0041B810, 0x0041B8A0});
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CCameraComponent*>(g_world.component);
            g_a.callocFails = seed == 3;
            g_a.cameraFails = seed == 5;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041B840))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CCameraComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            if (!c->mp_camera) {
                *reinterpret_cast<void***>(g_world.camera) = g_world.cameraVtable;
                c->mp_camera = reinterpret_cast<Gfx::Camera*>(g_world.camera);
            }
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041B7F0))(c, reinterpret_cast<void*>(0x5C70));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B810))(c);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B8A0))(c);
            if (seed & 1)
                c->mp_camera = nullptr;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B6D0))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t*>(c)[0]));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041B7C0))(
                c, seed % 3)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "camera component: made, destroyed, initialised, finalised, updated");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestColorCycleComponent, RemoveStandIns);
SELFTEST(Early, TestCameraComponent, RemoveStandIns);
