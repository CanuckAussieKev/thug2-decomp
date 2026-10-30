// Self-tests for the Walk component's pieces in src/game/skater/walk_moves.cpp: the original and ours run on the same
// stand-in arena (gamenet_testkit.h's), everything they call outside stood in for by recording code (or the game's
// own vector code); the calls and the arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skater/walk_camera.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using Obj::CWalkComponent;

constexpr uint32_t kWalk = 0x30000;          // the component (0x1960)
constexpr uint32_t kObject = kSkaters;       // its object (m_vel at +0xAC)
constexpr uint32_t kRidden = kSkaters + 0x100; // what it stands on
constexpr uint32_t kRide = kMisc + 0x500;    // the ride component: +0x18 the tracker
constexpr uint32_t kTracker = kMisc + 0x600; // CRideTracker (0xB0)
constexpr uint32_t kAnim = kMisc + 0x6C0;    // the animation component; its vtable at +0x40
constexpr uint32_t kTrigger = kMisc + 0x740; // the trigger state (+0x24)
constexpr uint32_t kCamera = kMisc + 0x900;  // the walk camera (0x90)
constexpr uint32_t kParams = kMisc + 0xA00;  // walk_parameters
constexpr uint32_t kNew = 0x34000;           // what Calloc gives

bool g_netGame;
bool g_updateOk;
bool g_callocFails;
std::map<uint32_t, float> g_params;

void RecFloat(uint32_t tag, const float* values, int count)
{
    g_trace.push_back(tag);
    for (int i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &values[i], 4);
        g_trace.push_back(bits);
    }
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        return g_callocFails ? 0u : At(kNew);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Rec(0xD730, {r.ecx});
        *reinterpret_cast<uint32_t*>(r.ecx) = 0x00647618;
        return r.ecx;
    });
    s.At({0x00528880, AddressOf(&Obj::WalkContact::Construct)}, 0, [](Regs& r) {
        Rec(0x8880, {r.ecx});
        return r.ecx;
    });
    s.At({0x0040AB50, AddressOf(&Nx::CCollCache::Construct)}, 0, [](Regs& r) {
        Rec(0xAB50, {r.ecx});
        return r.ecx;
    });
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0,
         [](Regs& r) {
             Rec(0x7760, {r.ecx});
             return r.ecx;
         });
    Stub(s, {0x005288B0, AddressOf(&Obj::CWalkComponent::DestroyBody)}, 0, 0x88B0);
    // The walk camera's override: the facing's contents and the rate.
    s.At({0x00525F70, AddressOf(&Obj::CWalkCameraComponent::SetOverrideFacing)}, 8, [](Regs& r) {
        Rec(0x5F70, {r.ecx});
        RecFloat(0x5F71, reinterpret_cast<const float*>(r.args[0]), 4);
        RecFloat(0x5F72, reinterpret_cast<const float*>(&r.args[1]), 1);
        return 0u;
    });
    // walk_parameters and its floats.
    StubCdecl(s, {0x00479020, AddressOf(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))},
              0x9020, 1, [](Regs&) { return At(kParams); });
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(
                          &Script::CStruct::GetFloat))},
         12, [](Regs& r) {
             Rec(0x66D0, {r.ecx, r.args[0], r.args[2]});
             *reinterpret_cast<float*>(r.args[1]) = g_params[r.args[0]];
             return 1u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))},
         8, [](Regs& r) {
             Rec(0x7C60, {r.ecx, r.args[0]});
             RecFloat(0x7C61, reinterpret_cast<const float*>(&r.args[1]), 1);
             return 0u;
         });
    s.At({0x00419B90, AddressOf(&Obj::CAnimBlenderComponent::FirstChannel47E850)}, 16, [](Regs& r) {
        Rec(0x9B90, {r.ecx, r.args[1], r.args[2], r.args[3]});
        RecFloat(0x9B91, reinterpret_cast<const float*>(&r.args[0]), 1);
        return 0u;
    });
    Stub(s, {0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_netGame ? 1u : 0u; });
    // The animation component's member function (vtable +0x30).
    void* member = s.Thunk(12, [](Regs& r) {
        Rec(0xA030, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 1u;
    });
    Word<uint32_t>(kAnim) = At(kAnim + 0x40);
    Word<uint32_t>(kAnim + 0x40 + 0x30) = reinterpret_cast<uint32_t>(member);
    // Riding.
    Stub(s, {0x00427C70, AddressOf(&Obj::CMovableContactComponent::StopRiding)}, 0, 0x7C70);
    s.At({0x00416F00, AddressOf(&Obj::CRideTracker::Update)}, 4, [](Regs& r) {
        Rec(0x6F00, {r.ecx, r.args[0]});
        return g_updateOk ? 1u : 0u;
    });
    Stub(s, {0x00416DD0, AddressOf(&Obj::CSmtPtr<Obj::CRefCounted>::Release)}, 0, 0x6DD0);
    StubCdecl(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0xECE0, 0, [](Regs&) { return 5000u; });
    // The state, the events, the other moves.
    Stub(s, {0x005260E0, AddressOf(&Obj::CWalkComponent::SetState)}, 4, 0x60E0);
    Stub(s, {0x005298D0, AddressOf(&Obj::CWalkComponent::ApplyTransition)}, 0, 0x98D0);
    Stub(s, {0x0052B7D0, AddressOf(&CWalkComponent::Jump)}, 0, 0xB7D0);
    Stub(s, {0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, 0x09F0);
}

void SetVector(uint32_t offset, float x, float y, float z, float w)
{
    float* v = &Word<float>(offset);
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
}

// The component's stand-in state: fields told apart, the components in place.
void SetUpWalk()
{
    for (uint32_t i = 0; i < 0x1960; i += 4)
        Word<uint32_t>(kWalk + i) = 0x3F000000u + (i << 4); // floats between 0.5 and 1
    Word<uint32_t>(kWalk + 0x0C) = At(kObject);
    Word<uint32_t>(kWalk + 0x1760) = At(kCamera);
    Word<uint32_t>(kWalk + 0x1774) = At(kAnim);
    Word<uint32_t>(kWalk + 0x1780) = At(kTrigger);
    Word<uint32_t>(kWalk + 0x1784) = At(kRide);
    SetVector(kWalk + 0x24, 12.375f, -3.1f, 1001.7f, 1.0f);   // position
    SetVector(kWalk + 0x48, 0.6f, 0.0f, -0.8f, 0.0f);         // facing
    SetVector(kWalk + 0x191C, -0.28f, 0.0f, 0.96f, 0.0f);     // the hang move's facing
    Word<float>(kWalk + 0x80) = 1.0f / 60.0f;                 // the frame
    SetVector(kObject + 0xAC, 3.3f, 7.25f, -11.1f, 0.0f);     // the object's velocity
    SetVector(kRidden + 0xAC, -1.7f, 0.45f, 2.9f, 0.0f);      // what it stands on: its velocity
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = At(kRidden);
    SetVector(kTracker + 0x5C, 0.125f, -0.0625f, 0.3f, 0.75f); // carried
    // The turn: about y by a little.
    float* m = &Word<float>(kTracker + 0x6C);
    const float turn[16] = {0.98f, 0.0f, -0.199f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.199f, 0.0f, 0.98f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(m, turn, sizeof(turn));
    Word<uint8_t>(kTracker + 0xAC) = 1;
    GameVariable<int>(0x00696D60) = 70000;
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_netGame = false;
        g_updateOk = true;
        g_callocFails = false;
        g_params = {{0x1A47FFAB, 2.5f}, {0x0AB2DB54, 3.0f}};
        StandIns* s = new StandIns;
        SetUpWalk();
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

CWalkComponent* Walk() { return &Word<CWalkComponent>(kWalk); }
template <typename F>
F Orig(uint32_t address)
{
    return Original<F>(address);
}
// The state after each call (later calls overwrite the same fields): the component, the stand-ins' objects.
// (a hash of the words, arena-relative)
uint32_t HashWords(uint32_t offset, uint32_t size)
{
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < size; i += 4)
        words.push_back(ArenaWordOrHigh(g_storage.Get(), kArenaSize, Word<uint32_t>(offset + i)));
    return HashBytes(reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4);
}
void Snapshot()
{
    g_trace.insert(g_trace.end(), {0x5A0, HashWords(kWalk, 0x1960), HashWords(kSkaters, 0x200),
                                   HashWords(kMisc + 0x500, 0x700)});
}
using Plain = void(__thiscall*)(void*);
void Run(bool original, uint32_t address, void (CWalkComponent::*ours)())
{
    original ? Orig<Plain>(address)(Walk()) : (Walk()->*ours)();
    Snapshot();
}

void Making(bool original)
{
    for (bool fails : {false, true}) {
        g_callocFails = fails;
        auto* made = original ? Orig<Obj::CBaseComponent*(__cdecl*)()>(0x0052A5C0)() : CWalkComponent::Create();
        Rec(0xC0, {reinterpret_cast<uint32_t>(made)});
    }
    auto* walk = original ? Orig<void*(__thiscall*)(void*)>(0x0052A190)(Walk())
                          : static_cast<void*>(Walk()->Construct());
    Rec(0xC1, {reinterpret_cast<uint32_t>(walk)});
    for (unsigned int flags : {0u, 1u, 3u}) {
        auto* result = original ? Orig<void*(__thiscall*)(void*, unsigned int)>(0x0052A290)(Walk(), flags)
                                : static_cast<void*>(Walk()->Destroy(flags));
        Rec(0xC2, {reinterpret_cast<uint32_t>(result)});
    }
}

void Camera(bool original)
{
    const uint32_t states[] = {0x4FE6069C, 0x279B1F0B, 0x2D9815C3, 0x4194ECCA, 0xAF5ABC82, 0xC84243DA,
                               0xFEC9DDED, 0x12345678, 0xF0000000, 0x4FE6069D, 0x4FE6069B, 0};
    for (uint32_t state : states) {
        for (int moving : {0, 1}) {
            Word<uint32_t>(kWalk + 0x78) = state;
            Word<int>(kWalk + 0x1944) = moving;
            Word<uint8_t>(kCamera + 0x78) = 0x55;
            Run(original, 0x00529F80, &CWalkComponent::UpdateCameraOverride);
            g_trace.insert(g_trace.end(), {0xCA, HashWords(kCamera, 0x90)});
        }
        // another facing (the turn's rounding)
        SetVector(kWalk + 0x48, -0.3713f, 0.25f, 0.9285f, 0.5f);
    }
}

void AnimSpeed(bool original)
{
    struct Case {
        int mode;
        float speed;
        float run;
        bool trigger;
        bool net;
        float sent;
    };
    const Case cases[] = {
        {0, 4.0f, 2.0f, true, true, 0.0f},       {4, 4.0f, 2.0f, true, true, 0.0f},
        {-1, 4.0f, 2.0f, true, true, 0.0f},      {1, 4.7f, 2.3f, false, false, 0.0f},
        {1, 4.7f, 2.3f, true, false, 0.0f},      {1, 4.7f, 0.0f, true, false, 0.0f},
        {1, 4.7f, -1.0f, true, false, 0.0f},     {2, 3.1f, 0.0f, true, true, 0.0f},
        {3, 3.1f, 0.0f, false, false, 0.0f},     {3, 3.1f, 0.0f, false, true, 1.0f},
        {3, 3.1f, 0.0f, false, true, 1.0f + 0.04f}, {3, 3.3f, 0.0f, false, true, 1.0f},
        {3, 3.1f, 0.0f, false, true, 1.1333333f}, {3, 3.3f, 0.0f, false, true, 1.0f + 0.1f + 0.0001f},
    };
    for (const Case& c : cases) {
        Word<int>(kWalk + 0x700) = c.mode;
        Word<float>(kWalk + 0x704) = c.speed;
        Word<float>(kWalk + 0x17B0) = c.run;
        Word<uint8_t>(kTrigger + 0x24) = c.trigger ? 1 : 0;
        g_netGame = c.net;
        Word<float>(kWalk + 0x18E0) = c.sent;
        Run(original, 0x0052A2C0, &CWalkComponent::UpdateAnimSpeed);
    }
    // NaN sent: sent again.
    Word<int>(kWalk + 0x700) = 3;
    g_netGame = true;
    Word<uint32_t>(kWalk + 0x18E0) = 0x7FC00000;
    Run(original, 0x0052A2C0, &CWalkComponent::UpdateAnimSpeed);
}

void Riding(bool original)
{
    // No tracker; a tracker without its object; Update failing; carried (turned or not; the facing level or not).
    Word<uint32_t>(kRide + 0x18) = 0;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = 0;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    Word<uint32_t>(kTracker) = At(kRidden);
    g_updateOk = false;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    g_updateOk = true;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    Word<uint8_t>(kTracker + 0xAC) = 0;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    Word<uint8_t>(kTracker + 0xAC) = 1;
    // a tilted turn: the facing leaves the level and is put back
    Word<float>(kTracker + 0x6C + 0x04) = 0.2f;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
    Word<uint32_t>(kWalk + 0x4C) = 0x7FC00000; // NaN y: normalised too
    Word<float>(kTracker + 0x6C + 0x04) = 0.0f;
    Word<float>(kTracker + 0x6C + 0x24) = 0.0f;
    Run(original, 0x0052A620, &CWalkComponent::UpdateRide);
}

void Leaving(bool original)
{
    float vertical = 2.75f;
    auto leave = [&](Mth::Vector* velocity, float* up) {
        if (original)
            Orig<void(__thiscall*)(void*, Mth::Vector*, float*)>(0x0052AB90)(Walk(), velocity, up);
        else
            Walk()->LeaveRide(velocity, up);
        RecFloat(0x1EA0, up, 1);
        Snapshot();
    };
    auto* velocity = &Word<Mth::Vector>(kMisc + 0xB00);
    SetVector(kMisc + 0xB00, -2.5f, 1.0f, 3.75f, 0.25f);
    // with what it stands on: heading along (a dot over 0), then against it, then square to it
    leave(velocity, &vertical);
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = At(kRidden);
    SetVector(kMisc + 0xB00, 2.5f, -1.0f, -3.75f, 0.25f);
    leave(velocity, &vertical);
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = At(kRidden);
    SetVector(kRidden + 0xAC, 0.0f, 0.45f, 0.0f, 0.0f);
    leave(velocity, &vertical);
    // square to it exactly (a dot of 0)
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = At(kRidden);
    SetVector(kRidden + 0xAC, 2.0f, 0.0f, 0.0f, 0.0f);
    SetVector(kMisc + 0xB00, 0.0f, 1.0f, 3.0f, 0.0f);
    leave(velocity, &vertical);
    // the move's rounding: x's move kept unrounded - positions of the move's size
    uint32_t seed = 12345;
    for (int i = 0; i < 64; ++i) {
        seed = seed * 1103515245u + 12345u;
        float speed = static_cast<float>(seed >> 8) / 65536.0f - 128.0f;
        Word<float>(kWalk + 0x80) = 0.0137f + static_cast<float>(i) * 0.0011f;
        Word<uint32_t>(kRide + 0x18) = At(kTracker);
        Word<uint32_t>(kTracker) = At(kRidden);
        SetVector(kRidden + 0xAC, speed, 0.0f, 0.0f, 0.0f);
        SetVector(kMisc + 0xB00, speed, 0.5f, 0.0f, 0.0f);
        SetVector(kWalk + 0x24, speed * 0.0731f, 0.0f, 0.0f, 1.0f);
        leave(velocity, &vertical);
    }
    // a few frames and velocities
    const float frames[] = {1.0f / 60.0f, 0.0333333f, 0.0171f, 0.1f};
    const float speeds[] = {-1.7f, 13.37f, 0.123456f, 777.7f, -31.4159f};
    for (float frame : frames) {
        for (float speed : speeds) {
            Word<float>(kWalk + 0x80) = frame;
            Word<uint32_t>(kRide + 0x18) = At(kTracker);
            Word<uint32_t>(kTracker) = At(kRidden);
            SetVector(kRidden + 0xAC, speed, speed * 0.37f, -speed * 1.3f, speed * 0.01f);
            SetVector(kMisc + 0xB00, speed, 0.5f, -speed, 0.0f);
            SetVector(kWalk + 0x24, 1234.567f + speed, -98.7654f, 31.0001f * speed, 1.0f);
            leave(velocity, &vertical);
        }
    }
    // a tracker without its object; none
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = 0;
    leave(velocity, &vertical);
    leave(velocity, &vertical);
}

void Air(bool original)
{
    // with what it stands on, and without
    for (int i = 0; i < 2; ++i) {
        Run(original, 0x0052B2D0, &CWalkComponent::GoToAir);
        SetVector(kObject + 0xAC, -0.5f, -2.0f, 0.0f, 0.0f);
    }
    // the member functions
    using Member = int(__thiscall*)(void*, void*, void*);
    void* params = &Word<uint8_t>(kMisc + 0xC00);
    void* script = &Word<uint8_t>(kMisc + 0xC40);
    uint32_t r1 = original ? Orig<Member>(0x0052A180)(Walk(), params, script)
                           : Walk()->AnimWaitComplete(static_cast<Script::CStruct*>(params),
                                                      static_cast<Script::CScript*>(script));
    uint32_t r2 = original ? Orig<Member>(0x0052C280)(Walk(), params, script)
                           : Walk()->ReleaseHang(static_cast<Script::CStruct*>(params),
                                                 static_cast<Script::CScript*>(script));
    uint32_t r3 = original ? Orig<Member>(0x0052C290)(Walk(), params, script)
                           : Walk()->ForceToAirFromHang(static_cast<Script::CStruct*>(params),
                                                        static_cast<Script::CScript*>(script));
    Rec(0xA1, {r1 & 0xFF, r2 & 0xFF, r3 & 0xFF});
    if (original)
        Orig<void(__thiscall*)(void*, void*)>(0x0052C2B0)(Walk(), params);
    else
        Walk()->GoToAirFor(params);
}

void TestWalkMoves()
{
    SaveGlobals({{0x00696D60, 4, {}}});
    RunCase("Walk: Create, the constructor, the destructor", Making);
    RunCase("Walk: the camera's override", Camera);
    RunCase("Walk: the animation speed", AnimSpeed);
    RunCase("Walk: riding", Riding);
    RunCase("Walk: leaving a ride", Leaving);
    RunCase("Walk: into the air, member functions", Air);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestWalkMoves, GameNetKit::Cleanup);
