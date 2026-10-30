// Self-test for the car physics component (src/game/obj/car_physics.cpp, 0x0041B910-0x0041C1ED): the original and
// ours, every call they make stood in for and recorded (the allocation, the base component, the script globals and the
// structure's getters, the object's components, the model's bones, the rotations and D3DX's multiply, the clock, the
// path follower, the suspend component, KeepWithin, the C runtime's asin).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/obj/car_physics.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/obj/suspend_component.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/gfx/model.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CCarPhysicsComponent;

constexpr int kBones = 8;

struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t motion[0x200];
    alignas(16) uint8_t follower[0x100];
    alignas(16) uint8_t suspend[0x40];
    alignas(16) uint8_t skeletonComponent[0x20];
    alignas(16) uint8_t skeleton[0x20];
    alignas(16) uint8_t modelComponent[0x210];
    alignas(16) uint8_t model[0x20];
    alignas(16) CCarPhysicsComponent::BoneDesc bones[kBones];
    alignas(16) Mth::Matrix matrices[kBones];
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
    bool haveParams;      // the structure's getters find their names
    bool haveSkeleton;    // GetComponent(skeleton)
    bool haveFollower;
    bool shouldUpdate;
    int readGlobals;
    double frameSeconds;
};
Answers g_a;

double __cdecl StandInFrameSeconds()
{
    Record({0xEAA0});
    return g_a.frameSeconds;
}
float __cdecl StandInGlobalFloat(uint32_t name)
{
    Record({0x8F00, name});
    return static_cast<float>(name % 997) / 37.0f - 9.0f;
}
float __cdecl StandInKeepWithin(float value, float target, float distance)
{
    Record({0x4010, Bits(value), Bits(target), Bits(distance)});
    return value * 0.75f + target * 0.25f + distance;
}

// The C runtime's asin (_CIasin, 0x00623560: the argument and result on the FPU stack) - the game's C runtime isn't
// started offline: a stand-in at its address, for the original's calls and ours.
double __cdecl StandInAsinValue(double value)
{
    uint32_t bits[2];
    std::memcpy(bits, &value, 8);
    Record({0x3560, bits[0], bits[1]});
    return value * 0.75 - 0.125;
}
__declspec(naked) void StandInAsin()
{
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call StandInAsinValue
        add esp, 8
        ret
    }
}

void StandIn(StandIns& s)
{
    s.Bytes(0x00623560, Branch(0xE9, 0x00623560, reinterpret_cast<void*>(&StandInAsin)));
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.component, 0xCD, sizeof(g_world.component)); // (dirty: what's left unset shows)
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
    s.At({0x0046F060, AddressOf(Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.Bytes(0x00478F00, Branch(0xE9, 0x00478F00, reinterpret_cast<void*>(&StandInGlobalFloat)));
    uint32_t oursFloat = AddressOf(static_cast<float (*)(uint32_t)>(Script::GetFloat));
    s.Bytes(oursFloat, Branch(0xE9, oursFloat, reinterpret_cast<void*>(&StandInGlobalFloat)));
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return static_cast<uint32_t>(g_a.readGlobals);
    });
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x66D0, r.ecx, r.args[0], Rel(r.args[1]), Bits(*out), r.args[2]});
        if (!g_a.haveParams || (r.args[0] ^ g_a.seed) % 3 == 0)
            return 0u;
        *out = static_cast<float>(r.args[0] % 101) / 13.0f;
        return 1u;
    });
    using GetIntegerFn = bool (Script::CStruct::*)(uint32_t, int*, int);
    s.At({0x00476630, AddressOf(static_cast<GetIntegerFn>(&Script::CStruct::GetInteger))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<int*>(r.args[1]);
        Record({0x6630, r.ecx, r.args[0], Rel(r.args[1]), static_cast<uint32_t>(*out), r.args[2]});
        if (!g_a.haveParams)
            return 0u;
        *out = static_cast<int>(g_a.seed % 3);
        return 1u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        switch (r.args[0]) {
        case 0x0A015E17:
            return reinterpret_cast<uint32_t>(g_world.motion);
        case 0xCE0CA665:
            return reinterpret_cast<uint32_t>(g_world.suspend);
        case 0x222756D5:
            return g_a.haveSkeleton ? reinterpret_cast<uint32_t>(g_world.skeletonComponent) : 0u;
        case 0x286A8D26:
            return reinterpret_cast<uint32_t>(g_world.modelComponent);
        default:
            return 0u;
        }
    });
    s.At({0x00493040, AddressOf(&Nx::CModel::GetAssetHierarchy)}, 0, [](Regs& r) {
        Record({0x3040, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.bones);
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2]), HashBytes(reinterpret_cast<const uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 0.5f - b[15 - i] * 0.25f + static_cast<float>(i);
        std::memcpy(out, result, sizeof(result));
        return r.args[0];
    });
    for (uint32_t address : {0x00404DD0u, 0x00404E70u}) {
        auto ours = address == 0x00404DD0u ? AddressOf(Mth::CreateRotateXMatrix) : AddressOf(Mth::CreateRotateZMatrix);
        s.At({address, ours}, 0, [address](Regs& r) {
            auto* m = reinterpret_cast<float*>(r.args[0]);
            float angle;
            std::memcpy(&angle, &r.args[1], 4);
            Record({address, Rel(r.args[0]), r.args[1]});
            for (int i = 0; i < 16; ++i)
                m[i] = angle * static_cast<float>(i + 1) + static_cast<float>(address & 0xFF);
            return r.args[0];
        });
    }
    s.Bytes(0x005BEAA0, Branch(0xE9, 0x005BEAA0, reinterpret_cast<void*>(&StandInFrameSeconds)));
    uint32_t oursFrame = AddressOf(&Tmr::GetSlomoFrameSeconds);
    s.Bytes(oursFrame, Branch(0xE9, oursFrame, reinterpret_cast<void*>(&StandInFrameSeconds)));
    s.Bytes(0x00404010, Branch(0xE9, 0x00404010, reinterpret_cast<void*>(&StandInKeepWithin)));
    uint32_t oursKeep = AddressOf(Mth::KeepWithin);
    s.Bytes(oursKeep, Branch(0xE9, oursKeep, reinterpret_cast<void*>(&StandInKeepWithin)));
    s.At({0x00423510, AddressOf(&Obj::CMotionPathMembers::GetPathFollower)}, 0, [](Regs& r) {
        Record({0x3510, Rel(r.ecx)});
        return g_a.haveFollower ? reinterpret_cast<uint32_t>(g_world.follower) : 0u;
    });
    s.At({0x00433EA0, AddressOf(&Obj::CSuspendComponent::ShouldAnimate)}, 4, [](Regs& r) {
        Record({0x3EA0, Rel(r.ecx), r.args[0]});
        return g_a.shouldUpdate ? 1u : 0u;
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
    Lcg random(0xCA700000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 2000);
    FillFloats(random, g_world.object, sizeof(g_world.object), 2000);
    FillFloats(random, g_world.motion, sizeof(g_world.motion), 300);
    FillFloats(random, g_world.follower, sizeof(g_world.follower), 300);
    FillFloats(random, reinterpret_cast<uint8_t*>(g_world.bones), sizeof(g_world.bones), 300);
    FillFloats(random, reinterpret_cast<uint8_t*>(g_world.matrices), sizeof(g_world.matrices), 300);
    auto* c = reinterpret_cast<CCarPhysicsComponent*>(g_world.component);
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->m_object = object;
    object->m_objectFlags = seed % 9 == 4 ? 1u : 0u;
    std::memset(g_world.suspend, 0, sizeof(g_world.suspend));
    g_world.suspend[0x30] = seed % 11 == 5 ? 1 : 0;
    std::memset(g_world.skeletonComponent, 0, sizeof(g_world.skeletonComponent));
    *reinterpret_cast<void**>(g_world.skeletonComponent + 0x18) = g_world.skeleton;
    std::memset(g_world.skeleton, 0, sizeof(g_world.skeleton));
    *reinterpret_cast<void**>(g_world.skeleton + 4) = g_world.matrices;
    *reinterpret_cast<int*>(g_world.skeleton + 0x14) = static_cast<int>(seed % 9);
    *reinterpret_cast<void**>(g_world.modelComponent + 0x208) = g_world.model;
    static const uint32_t names[] = {0x88C21962, 0x80212262, 0x1E45B7C1, 0x69428757, 0xF04BD6ED,
                                     0x6E2F434E, 0xF72612F4, 0x12345678, 0x80212263};
    for (int i = 0; i < kBones; ++i) {
        auto& bone = g_world.bones[i];
        bone.name = names[(i + seed) % 9];
        bone.index = static_cast<int8_t>((i * 3 + seed) % kBones);
        bone.parent = static_cast<int16_t>(i == 0 ? 0 : (i - 1 + seed) % i);
    }
    // the motion's speed, the path follower's turn
    static const float speeds[] = {0.0f, 245.5f, -80.25f, 1234.5f, 7.0f};
    *reinterpret_cast<float*>(g_world.motion + 0x30) = speeds[seed % 5];
    static const float turns[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.0f, -3.0f};
    *reinterpret_cast<float*>(g_world.follower + 0xE0) = turns[seed % 6];
}

constexpr uint32_t kAddresses[] = {0x0041BD90, 0x0041B910, 0x0041BAD0, 0x0041B930, 0x0041BB00, 0x0041B940, 0x0041BE10,
                                   0x0041C1B0, 0x0041BB30, 0x0041C0B0, 0x0041BF00, 0x0041B9E0, 0x0041BA20};

void DumpComponent(std::vector<uint32_t>& trace)
{
    for (size_t i = 0; i < sizeof(CCarPhysicsComponent) / 4; ++i)
        trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
}

// Made (and out of memory), registered, constructed on dirty memory, destroyed; InitFromStructure with and without
// parameters; Finalize with and without a skeleton; the angles from many states (the spin past 360 by several turns,
// below 0, NaN; no follower, no turn, a negative turn, the ratio beyond +-1; the tilt's divisor 0, limits, the globals
// re-read or not); the bones (body shown or not); Update's early outs; the members.
void TestCarPhysicsComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CCarPhysicsComponent*>(g_world.component);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.callocFails = seed == 6;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041BD90))()));
            g_a.callocFails = false;
            DumpComponent(trace);
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x0041B910))();
            ResetWorld(seed);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x0041BAD0))(c)));
            DumpComponent(trace);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041B930))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041BB00))(
                c, seed % 3)));
            ResetWorld(seed);
            g_a.haveParams = seed % 3 != 0;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041B940))(c, params);
            g_a.haveSkeleton = seed % 7 != 2;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041BE10))(c);
            DumpComponent(trace);
            // The members.
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041B9E0))(
                c, params, script));
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041BA20))(
                c, params, script));
            // The angles from several states.
            Lcg random(0x5E000000 + seed);
            static const double frames[] = {1.0 / 60.0, 0.1, 0.0123456789, 2.5, 1.0 / 30.0};
            for (int k = 0; k < 8; ++k) {
                g_a.frameSeconds = frames[random.Below(5)];
                g_a.haveFollower = random.Below(5) != 0;
                g_a.readGlobals = static_cast<int>(random.Below(3)) - 1;
                uint32_t pick = random.Next();
                static const float spins[] = {359.5f, 1000.25f, -0.5f, -725.0f, 10.0f, 360.0f, 0.0f};
                c->m_wheelSpin = pick % 13 == 12 ? nan : spins[pick % 7];
                static const float bases[] = {1.0f, 1e6f, -1e6f, 0.0f, 33.5f};
                c->m_wheelBase = bases[(pick >> 4) % 5];
                if ((pick >> 8) % 4 == 0)
                    c->m_tiltDivisor = 0.0f;
                if ((pick >> 8) % 4 == 1)
                    c->m_tiltDivisor = 1e-3f;
                if ((pick >> 10) % 5 == 0)
                    *reinterpret_cast<float*>(g_world.follower + 0xE0) = nan;
                if ((pick >> 12) % 6 == 0) // (the follower's point on its own position: an infinite ratio)
                    std::memcpy(g_world.follower + 4, g_world.follower + 0xB0, 12);
                *reinterpret_cast<float*>(g_world.motion + 0x30) += static_cast<float>(k) * 3.5f;
                if (k == 0) { // (no speed: the spin as it is - exactly 720 / 1080, turned down to 360)
                    *reinterpret_cast<float*>(g_world.motion + 0x30) = 0.0f;
                    c->m_wheelSpin = seed & 1 ? 720.0f : 1080.0f;
                }
                if (k >= 5) { // (the tilt within wide limits: a divisor of 0 gives 0)
                    g_a.readGlobals = 0;
                    c->m_minTilt = -50.0f;
                    c->m_maxTilt = 50.0f;
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041BB30))(c);
                DumpComponent(trace);
            }
            // The bones, body shown or hidden; UpdateSkeleton directly.
            for (int k = 0; k < 2; ++k) {
                c->m_bodyShown = k == 0;
                c->mp_skeleton = g_world.skeleton;
                c->mp_model = g_world.model;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C0B0))(c);
            }
            reinterpret_cast<void(__cdecl*)(void*, void*, float, float, float)>(Code(original, 0x0041BF00))(
                g_world.skeleton, g_world.model, 12.5f, -370.0f, 45.0f);
            // Update: each early out, then the whole of it.
            for (int k = 0; k < 4; ++k) {
                ResetWorld(seed);
                g_a.shouldUpdate = k != 1;
                c->mp_suspend = g_world.suspend;
                c->mp_motion = g_world.motion;
                c->mp_model = g_world.model;
                c->mp_skeleton = k == 2 ? nullptr : g_world.skeleton;
                if (k == 3)
                    g_world.suspend[0x30] = 0;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C1B0))(c);
                DumpComponent(trace);
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.matrices) / 4; ++i)
                trace.push_back(reinterpret_cast<uint32_t*>(g_world.matrices)[i]);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "car physics component: made, initialised, finalised, its angles, bones, update");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestCarPhysicsComponent, RemoveStandIns);
