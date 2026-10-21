// Self-tests for the motion component's life, wait and simpler member functions (motion_component.cpp) and the two
// vector helpers placed among them (Mth::Vector::operator/=, Mth::GetDistance): original vs ours on a stand-in
// component, every callee stood in for (motion_testkit.h).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/motion.h"
#include "game/obj/path_follower.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/motion_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace MotionKit;
using Obj::CMotionComponent;
using Script::CScript;
using Script::CStruct;

// --- stand-ins ---
StandIn Clock() { return {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}}; }
StandIn RndCall() { return {0x00403DE0, 0, 1, Ret::Int, false, 0, {Ours(&Mth::Rnd)}}; }
StandIn LinkCount()
{
    return {0x0045C3A0, 0, 1, Ret::Int, false, 0,
            {Ours(static_cast<uint32_t (*)(uint32_t)>(&SkateScript::GetLinkCount))}, [](Frame&, Result& r) {
                static const uint32_t counts[] = {0, 1, 2, 5};
                r.eax = counts[Next() % 4];
            }};
}
StandIn FrameSeconds() { return {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)}}; }
StandIn StopWaiting() { return {0x004728F0, 0, 0, Ret::None, true, 0, {Ours(&CScript::StopWaiting)}}; }
StandIn MoveToNodeCall() { return {0x00426C10, 8, 2, Ret::None, true}; }
StandIn OrientToNodeCall() { return {0x00425970, 4, 1, Ret::None, true, 0, {Ours(&CMotionComponent::OrientToNode)}}; }
StandIn FollowerConstruct()
{
    return {0x0046A1D0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CPathFollower::Construct)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn FollowerDestroy()
{
    return {0x00469B50, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CPathFollower::Destroy)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn ManagerFree() { return {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}}; }
StandIn BaseConstruct()
{
    return {0x0045D730, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CBaseComponent::Construct)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn BaseDestroyBody() { return {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}}; }
StandIn CallocCall() { return Calloc(Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))); }

// One run over the saved arena (the allocator rewound).
template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    g_allocNext = kAlloc;
    return RunOnArena(seed, call);
}

using Member = bool (CMotionComponent::*)(CStruct*, CScript*);
using OriginalMember = bool(__thiscall*)(CMotionComponent*, CStruct*, CScript*);

// A member function, original vs ours, after `setup` (which fills the arena and g_params) for each variant.
template <typename Setup>
bool CompareMember(const char* what, uint32_t address, Member ours, int variants, Setup setup)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0x2545F491u * static_cast<uint32_t>(variant + 1);
        g_params.clear();
        g_components.clear();
        g_allocFails = false;
        FillMotionArena();
        setup(variant);
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, [&](uint8_t*) {
            return static_cast<uint32_t>(Original<OriginalMember>(address)(Motion(), Params(), TheScript()));
        });
        auto actual = Run(seed, [&](uint8_t*) { return static_cast<uint32_t>((Motion()->*ours)(Params(), TheScript())); });
        if (!Compare(expected, actual, what, variant))
            return false;
    }
    return true;
}

// Random motion flags.
void RandomFlags()
{
    Motion()->m_motionFlags = Next() & 0x3FF;
    Motion()->m_motionFlags2 = Next() & 0x7FF;
}

constexpr uint32_t kOff = 0xD443A2BC;

// --- life ---
void TestMotionLife()
{
    StandIns standIns({CallocCall(), FollowerConstruct(), FollowerDestroy(), ManagerFree(), BaseConstruct(),
                       BaseDestroyBody()});
    bool ok = true;
    for (int variant = 0; variant < 200 && ok; ++variant) {
        g_rng = 0x9E3779B9u * static_cast<uint32_t>(variant + 1);
        FillMotionArena();
        g_allocFails = variant % 7 == 3;
        uint8_t* follower = Next() % 3 ? ArenaPtr(kFollower) : nullptr;
        ArenaAt<uint32_t>(kFollower) = 0x00647A84;
        ArenaAt<uint8_t*>(kMotion + 0x1EC) = follower;
        ArenaAt<uint8_t*>(kMotion + 0x1CC) = Next() % 2 ? ArenaPtr(kMine) : nullptr;
        ArenaAt<uint8_t*>(kMotion + 0x1F0) = Next() % 2 ? ArenaPtr(kTrail) : nullptr;
        ArenaAt<uint32_t>(kMotion + 0x08) = Next() & 1; // (not COMPONENT_FLAG_SCRIPT_WAITING: no scripts to tell)
        SaveStart();
        uint32_t seed = Next();
        auto both = [&](const char* what, auto original, auto ours) {
            auto expected = Run(seed, original);
            auto actual = Run(seed, ours);
            ok = ok && Compare(expected, actual, what, variant);
        };
        both(
            "motion Create", [](uint8_t*) { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00426650)()); },
            [](uint8_t*) { return reinterpret_cast<uint32_t>(CMotionComponent::Create()); });
        both(
            "motion Construct",
            [](uint8_t*) {
                return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x00424AA0)(Motion()));
            },
            [](uint8_t*) { return reinterpret_cast<uint32_t>(Motion()->Construct()); });
        both(
            "motion DestroyBody",
            [](uint8_t*) {
                Original<void(__thiscall*)(void*)>(0x00424AE0)(Motion());
                return 0u;
            },
            [](uint8_t*) {
                Motion()->DestroyBody();
                return 0u;
            });
        uint32_t flags = Next() & 3;
        both(
            "motion Destroy",
            [&](uint8_t*) {
                return reinterpret_cast<uint32_t>(
                    Original<void*(__thiscall*)(void*, unsigned int)>(0x004266F0)(Motion(), flags));
            },
            [&](uint8_t*) { return reinterpret_cast<uint32_t>(Motion()->Destroy(flags)); });
        both(
            "motion Reset",
            [](uint8_t*) {
                Original<void(__thiscall*)(void*)>(0x00423D00)(Motion());
                return 0u;
            },
            [](uint8_t*) {
                Motion()->Reset();
                return 0u;
            });
        both(
            "motion InitPathFollower",
            [](uint8_t*) {
                Original<void(__thiscall*)(void*, void*)>(0x00423C90)(Motion(), ArenaPtr(kOtherObj));
                return 0u;
            },
            [](uint8_t*) {
                Motion()->InitPathFollower(reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kOtherObj)));
                return 0u;
            });
    }
    Check(ok, "motion component: Create, Construct, DestroyBody, Destroy, Reset, InitPathFollower (original vs ours)");
}

// --- the wait, the member function by name, InitFromStructure ---
void TestMotionWait()
{
    StandIns standIns({StopWaiting(), MoveToNodeCall(), OrientToNodeCall(), GetChecksum()});
    bool ok = true;
    int stopped = 0;
    for (int variant = 0; variant < 3000 && ok; ++variant) {
        g_rng = 0x51ED270Bu * static_cast<uint32_t>(variant + 1);
        g_params.clear();
        FillMotionArena();
        RandomFlags();
        ArenaAt<uint32_t>(kScript + 0xA4) = Next() % 12;
        static const float speeds[] = {0.0f, -0.0f, 1.0f, -2.0f, 0.001f};
        ArenaAt<float>(kMotion + 0x30) = speeds[Next() % 5];
        if (Next() % 20 == 0)
            ArenaAt<uint32_t>(kMotion + 0x30) = 0x7FC00000; // (NaN)
        static const uint32_t names[] = {0x8819DD8B, 0x8819DD8C, 0, 0x5CF1B7D4};
        uint32_t name = names[Next() % 4];
        uint32_t arg = Next();
        if (Next() % 2)
            GiveChecksum(0xA1DC81F9, Next());
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, [&](uint8_t*) {
            Original<void(__thiscall*)(void*, void*, uint32_t)>(0x004236C0)(Motion(), TheScript(), arg);
            return 0u;
        });
        auto actual = Run(seed, [&](uint8_t*) {
            Motion()->ProcessWait(TheScript(), arg);
            return 0u;
        });
        ok = Compare(expected, actual, "motion ProcessWait", variant);
        stopped += Called(expected, 0x004728F0);
        expected = Run(seed, [&](uint8_t*) {
            return static_cast<uint32_t>(Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x004274D0)(
                Motion(), name, Params(), TheScript()));
        });
        actual = Run(seed, [&](uint8_t*) { return static_cast<uint32_t>(Motion()->CallMemberFunction(name, Params(), TheScript())); });
        ok = ok && Compare(expected, actual, "motion CallMemberFunction", variant);
        expected = Run(seed, [&](uint8_t*) {
            Original<void(__thiscall*)(void*, void*)>(0x00426720)(Motion(), Params());
            return 0u;
        });
        actual = Run(seed, [&](uint8_t*) {
            Motion()->InitFromStructure(Params());
            return 0u;
        });
        ok = ok && Compare(expected, actual, "motion InitFromStructure", variant);
    }
    Check(ok && stopped > 100, "motion component: ProcessWait (wait types 0-11), CallMemberFunction, InitFromStructure");
}

// --- hovering ---
void TestMotionHover()
{
    StandIns standIns({Clock(), RndCall(), GetFloat(), ContainsFlag()});
    ClockOffsetZero clock;
    bool ok = true;
    for (int variant = 0; variant < 3000 && ok; ++variant) {
        g_rng = 0x6C8E9CF5u * static_cast<uint32_t>(variant + 1);
        FillMotionArena();
        static const int periods[] = {1, 7, 1000, 4096, 123457, 0x40000000, 0x7FFFFFFF};
        ArenaAt<int>(kMotion + 0x1DC) = periods[Next() % 7] + static_cast<int>(Next() % 3);
        if (ArenaAt<int>(kMotion + 0x1DC) <= 0)
            ArenaAt<int>(kMotion + 0x1DC) = 1;
        GameVariable<int>(0x00696D60) = Next() % 2 ? 0 : static_cast<int>(Next());
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, [](uint8_t*) {
            Original<void(__thiscall*)(void*)>(0x00423740)(Motion());
            return 0u;
        });
        auto actual = Run(seed, [](uint8_t*) {
            Motion()->UpdateHover();
            return 0u;
        });
        ok = Compare(expected, actual, "motion UpdateHover", variant);
    }
    GameVariable<int>(0x00696D60) = 0;
    ok = ok && CompareMember("Obj_Hover", 0x004238D0, &CMotionComponent::Obj_Hover, 2000, [](int) {
        RandomFlags();
        if (Next() % 4 == 0)
            GiveFlag(kOff);
        if (Next() % 2)
            Give(0xC9FDE32C, RandomFloat(20.0f));
        if (Next() % 3) {
            static const float frequencies[] = {1.0f, 0.5f, 3.0f, 0.001f, 1e-9f, -2.0f, 1000.0f, 7.3f, 0.0f};
            Give(0xA80BEA4A, frequencies[Next() % 9]);
        }
    });
    Check(ok, "motion component: UpdateHover (periods, clock offsets), Obj_Hover (Off, Amp, Freq)");
}

// --- the other member functions ---
void TestMotionMembers()
{
    StandIns standIns({RndCall(), LinkCount(), FrameSeconds(), GetFloat(), GetVector(), ContainsFlag(), AddInteger(),
                       AddFloatNamed(), GetComponent()});
    bool ok = true;
    ok = ok && CompareMember("Obj_StorePos", 0x004237D0, &CMotionComponent::Obj_StorePos, 20, [](int) {});
    ok = ok && CompareMember("Obj_StoreNode", 0x00423810, &CMotionComponent::Obj_StoreNode, 20, [](int) {
        ArenaAt<uint8_t*>(kMotion + 0x1EC) = ArenaPtr(kFollower);
        ArenaAt<uint32_t>(kFollower + 0x5C) = Next();
    });
    ok = ok && CompareMember("Obj_SetConstantHeight", 0x00423830, &CMotionComponent::Obj_SetConstantHeight, 200,
                             [](int) {
                                 RandomFlags();
                                 if (Next() % 3 == 0)
                                     GiveFlag(kOff);
                                 if (Next() % 2)
                                     Give(0, RandomFloat(100.0f));
                             });
    ok = ok && CompareMember("Obj_GetRandomLink", 0x00423890, &CMotionComponent::Obj_GetRandomLink, 200,
                             [](int) { ArenaAt<uint32_t>(kMotion + 0x1E8) = Next(); });
    ok = ok && CompareMember("Obj_GetSpeed", 0x004239F0, &CMotionComponent::Obj_GetSpeed, 3000, [](int) {
        RandomFlags();
        uint32_t lock = Next() % 3;
        if (lock)
            g_components.push_back({kObj, 0x5CB5CB7E, kLock});
        ArenaAt<uint8_t>(kLock + 0x8C) = static_cast<uint8_t>(Next());
        RichVector(kObj + 0x4C, 1000.0f); // (the position, the old one, the velocity)
        RichVector(kObj + 0x5C, 1000.0f);
        RichVector(kObj + 0xAC, 1000.0f);
        uint32_t unit = Next() % 4;
        if (unit == 1)
            GiveFlag(0xA18B8F32);
        else if (unit == 2)
            GiveFlag(0xAAD7C80F);
        else if (unit == 3) {
            GiveFlag(0xA18B8F32);
            GiveFlag(0xAAD7C80F);
        }
    });
    ok = ok && CompareMember("Obj_Jump", 0x00423B30, &CMotionComponent::Obj_Jump, 3000, [](int) {
        RandomFlags();
        if (Next() % 2)
            Give(0xF0D90109, RandomFloat(300.0f));
        if (Next() % 2)
            Give(0xA5E2DA58, RandomFloat(500.0f));
        if (Next() % 2)
            Give(0xECC2C699, RandomFloat(10.0f));
        if (Next() % 2)
            Give(0x5C4AE2F9, RandomFloat(10.0f));
        if (Next() % 3 == 0)
            GiveVector(0xFD4BC03E, RandomFloat(1.0f), RandomFloat(1.0f), RandomFloat(1.0f));
        if (Next() % 3 == 0)
            GiveFlag(0xFECFFE49);
        if (Next() % 2)
            Give(0xA1810C27, RandomFloat(100.0f));
    });
    Check(ok, "motion component: Obj_StorePos, Obj_StoreNode, Obj_SetConstantHeight, Obj_GetRandomLink, Obj_GetSpeed, "
              "Obj_Jump (original vs ours)");
}

// --- the vector helpers ---
void TestMotionVectorHelpers()
{
    bool ok = true;
    for (int variant = 0; variant < 2000 && ok; ++variant) {
        g_rng = 0x1234567u * static_cast<uint32_t>(variant + 1);
        Mth::Vector a = {RandomFloat(1000.0f), RandomFloat(10.0f), RandomFloat(1e5f), RandomFloat(2.0f)};
        Mth::Vector b = {RandomFloat(1000.0f), RandomFloat(10.0f), RandomFloat(1e5f), RandomFloat(2.0f)};
        static const float divisors[] = {1.0f, 3.0f, -7.25f, 0.001f, 1e20f, 0.0f};
        float divisor = Next() % 3 ? RandomFloat(50.0f) : divisors[Next() % 6];
        Mth::Vector original = a, ours = a;
        Original<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x00423650)(&original, divisor);
        Mth::Vector* returned = &(ours /= divisor);
        ok = std::memcmp(&original, &ours, sizeof(ours)) == 0 && returned == &ours;
        double d1 = Original<double(__cdecl*)(const Mth::Vector*, const Mth::Vector*)>(0x00423680)(&a, &b);
        double d2 = Mth::GetDistance(a, b);
        ok = ok && std::memcmp(&d1, &d2, sizeof(d1)) == 0;
        if (!ok)
            Log("  vector helpers: variant %d differs", variant);
    }
    Check(ok, "Mth::Vector::operator/= (0x00423650), Mth::GetDistance (0x00423680): original vs ours");
}

} // namespace

SELFTEST(Early, TestMotionLife);
SELFTEST(Early, TestMotionWait);
SELFTEST(Early, TestMotionHover);
SELFTEST(Early, TestMotionMembers);
SELFTEST(Early, TestMotionVectorHelpers);
