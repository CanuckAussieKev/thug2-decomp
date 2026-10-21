// Self-tests for the Walk component's update and its ground, air and animation-wait states (walk_moves.cpp): the
// original and ours run on the same stand-in arena (gamenet_testkit.h's), everything they call outside stood in for
// by recording code (some of it changing the state, halting the walk... as the scenario asks); the calls, the state
// after each call and the arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/script/struct.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using Obj::CWalkComponent;

constexpr uint32_t kWalk = 0x30000;            // the component (0x1960)
constexpr uint32_t kObject = kSkaters;         // its object (m_pos +0x4C, m_vel +0xAC)
constexpr uint32_t kRidden = kSkaters + 0x100; // what it stands on / is inside
constexpr uint32_t kRide = kMisc + 0x500;      // the ride component: +0x18 the tracker
constexpr uint32_t kTracker = kMisc + 0x600;   // CRideTracker (0xB0)
constexpr uint32_t kAnim = kMisc + 0x6C0;      // the animation component
constexpr uint32_t kTrigger = kMisc + 0x740;   // +0x26 the walk halted
constexpr uint32_t kInput = kMisc + 0xA00;     // the input component (0x200)
constexpr uint32_t kSkaterComp = kMisc + 0xC00;
constexpr uint32_t kParams = kMisc + 0xC80;

float g_frame;
float g_gravity;
float g_rehang;
uint32_t g_since;
float g_times[3];
std::map<uint32_t, float> g_params;
std::map<uint32_t, std::function<void()>> g_effects; // run when the stand-in with that tag is called

void RecFloat(uint32_t tag, const float* values, int count)
{
    g_trace.push_back(tag);
    for (int i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &values[i], 4);
        g_trace.push_back(bits);
    }
}

uint32_t HashWords(uint32_t offset, uint32_t size)
{
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < size; i += 4)
        words.push_back(ArenaWordOrHigh(g_storage.Get(), kArenaSize, Word<uint32_t>(offset + i)));
    return HashBytes(reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4);
}
void Snapshot(uint32_t tag)
{
    g_trace.insert(g_trace.end(), {tag, HashWords(kWalk, 0x1960), HashWords(kSkaters, 0x200),
                                   HashWords(kMisc + 0x500, 0x300), GameVariable<uint32_t>(0x00691974)});
    GameVariable<uint32_t>(0x00691974) = 0x12345; // (the current collision cache: cleared or not)
}

// A recording stand-in: the tag, ECX (unless `noEcx` - a stack object) and `pop` / 4 arguments; then the scenario's
// effect for the tag; the answer g_answers[tag] (or 0).
void S(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, bool noEcx = false)
{
    s.At(addresses, pop, [tag, pop, noEcx](Regs& r) {
        g_trace.push_back(tag);
        if (!noEcx)
            g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        auto effect = g_effects.find(tag);
        if (effect != g_effects.end())
            effect->second();
        auto it = g_answers.find(tag);
        return it == g_answers.end() ? 0u : it->second;
    });
}
// A stand-in recording vectors (by their contents) given by pointer in arguments `first`.. (`count` of them).
void SVectors(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, bool noEcx = false)
{
    s.At(addresses, pop, [tag, pop, noEcx](Regs& r) {
        g_trace.push_back(tag);
        if (!noEcx)
            g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            RecFloat(tag + 1, reinterpret_cast<const float*>(r.args[i]), 4);
        auto effect = g_effects.find(tag);
        if (effect != g_effects.end())
            effect->second();
        auto it = g_answers.find(tag);
        return it == g_answers.end() ? 0u : it->second;
    });
}
// A function returning a float in ST0: FLD [value]; RET.
void ReturnFloat(StandIns& s, std::initializer_list<uint32_t> addresses, const float* value)
{
    uint32_t where = reinterpret_cast<uint32_t>(value);
    std::vector<uint8_t> code = {0xD9, 0x05};
    for (int i = 0; i < 4; ++i)
        code.push_back(static_cast<uint8_t>(where >> (8 * i)));
    code.push_back(0xC3);
    for (uint32_t address : addresses)
        s.Bytes(address, code);
}

void Install(StandIns& s)
{
    InstallCommon(s);
    // Update's.
    for (uint32_t a : {0x00527F20u, 0x00527DB0u, 0x00528370u, 0x00440BF0u, 0x004425B0u, 0x00527600u, 0x005278D0u,
                       0x00527BE0u, 0x00527080u, 0x00527420u, 0x00529CE0u, 0x00529800u})
        S(s, {a}, 0, a & 0xFFFF);
    S(s, {0x00528400}, 4, 0x8400);
    S(s, {0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, 0x09F0);
    S(s, {0x005007A0}, 0, 0x07A0);
    ReturnFloat(s, {0x005BEAA0, AddressOf(&Tmr::GetSlomoFrameSeconds)}, &g_frame);
    StubCdecl(s, {0x00479020, AddressOf(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))},
              0x9020, 1, [](Regs&) { return At(kParams); });
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(
                          &Script::CStruct::GetFloat))},
         12, [](Regs& r) {
             Rec(0x66D0, {r.ecx, r.args[0], r.args[2]});
             *reinterpret_cast<float*>(r.args[1]) = g_params[r.args[0]];
             return 1u;
         });
    StubCdecl(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0xECE0, 0, [](Regs&) { return 5000u; });
    // Ground's.
    S(s, {0x0052A620, AddressOf(&CWalkComponent::UpdateRide)}, 0, 0xA620);
    for (uint32_t a : {0x005277B0u, 0x00526260u, 0x0052B370u, 0x005264F0u, 0x00500820u, 0x00526850u, 0x00527C50u,
                       0x00526BA0u, 0x005299F0u, 0x00527510u, 0x00526E00u, 0x005293F0u, 0x0052C2C0u})
        S(s, {a}, 0, a & 0xFFFF);
    SVectors(s, {0x00528950}, 4, 0x8950);
    SVectors(s, {0x00427B30}, 8, 0x7B30);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1])),
                     r.args[2], Hash(reinterpret_cast<const char*>(r.args[3]))});
        return 0u;
    });
    // Air's.
    ReturnFloat(s, {0x005284C0}, &g_gravity);
    ReturnFloat(s, {0x0043DA70}, &g_rehang);
    SVectors(s, {0x0052A700}, 8, 0xA700);
    StubCdecl(s, {0x00420250}, 0x0250, 1, [](Regs&) { return g_since; });
    s.At({0x0043F030}, 0x20, [](Regs& r) {
        Rec(0xF030, {r.ecx});
        RecFloat(0xF031, reinterpret_cast<const float*>(&r.args[0]), 8);
        return g_answers[0xF030];
    });
    SVectors(s, {0x00442170}, 8, 0x2170);
    // (the constructor returns this)
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0,
         [](Regs& r) {
             Rec(0x7760, {});
             return r.ecx;
         });
    SVectors(s, {0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, 0x7190, true);
    SVectors(s, {0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, 0x71B0, true);
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        Rec(0x72F0, {r.args[0] & 0xFF, r.args[1] & 0xFF});
        return g_answers[0x72F0];
    });
    S(s, {0x00417210, AddressOf(&Nx::CFeeler::HitObjectAlive)}, 0, 0x7210, true);
    S(s, {0x00417240, AddressOf(&Nx::CFeeler::GetHitObject)}, 0, 0x7240, true);
    S(s, {0x0043EB80}, 4, 0xEB80);
    S(s, {0x0041C350}, 0, 0xC350, true);
    // AnimWait's.
    S(s, {0x00427C70}, 0, 0x7C70);
    s.At({0x00416F00, AddressOf(&Obj::CRideTracker::Update)}, 4, [](Regs& r) {
        Rec(0x6F00, {r.ecx, r.args[0]});
        return g_answers[0x6F00];
    });
    s.At({0x0041A0E0, AddressOf(&Obj::CAnimBlenderComponent::GetTimes)}, 12, [](Regs& r) {
        Rec(0xA0E0, {r.ecx});
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float*>(r.args[i]) = g_times[i];
        return 0u;
    });
}

void SetVector(uint32_t offset, float x, float y, float z, float w)
{
    float* v = &Word<float>(offset);
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
}

void SetUpWalk()
{
    for (uint32_t i = 0; i < 0x1960; i += 4)
        Word<uint32_t>(kWalk + i) = 0x3F000000u + (i << 4); // floats between 0.5 and 1
    Word<uint32_t>(kWalk + 0x0C) = At(kObject);
    Word<uint32_t>(kWalk + 0x1770) = At(kInput);
    Word<uint32_t>(kWalk + 0x1774) = At(kAnim);
    Word<uint32_t>(kWalk + 0x1780) = At(kTrigger);
    Word<uint32_t>(kWalk + 0x1784) = At(kRide);
    Word<uint32_t>(kWalk + 0x178C) = At(kSkaterComp);
    Word<int>(kWalk + 0x18) = 0;
    Word<int>(kWalk + 0x1C) = 74000;
    Word<int>(kWalk + 0x20) = 0;
    Word<uint32_t>(kWalk + 0x78) = 0x12345678;
    Word<uint8_t>(kWalk + 0x9A) = 0;
    Word<uint8_t>(kWalk + 0x718) = 0;
    Word<int>(kWalk + 0x710) = 0;
    SetVector(kWalk + 0x24, 12.375f, -3.1f, 1001.7f, 1.0f);   // position
    SetVector(kWalk + 0x34, 4.25f, 0.0f, -7.125f, 0.5f);      // velocity
    Word<float>(kWalk + 0x44) = 6.5f;                          // vertical speed
    SetVector(kWalk + 0x48, 0.6f, 0.0f, -0.8f, 0.0f);         // facing
    SetVector(kWalk + 0x68, 12.0f, -3.0f, 1001.0f, 1.0f);     // old position
    Word<float>(kWalk + 0x80) = 1.0f / 60.0f;
    Word<float>(kWalk + 0x714) = 2.75f;                        // the step smoothing's height
    SetVector(kWalk + 0x71C, 1000.3f, 36.7f, -7777.7f, 0.0f);        // the ride check's offset
    SetVector(kWalk + 0x189C, 1.5f, 0.25f, -0.75f, 1.0f);     // extra velocity
    Word<float>(kWalk + 0x18AC) = 0.0f;                        // its top
    SetVector(kObject + 0x4C, 100.5f, 20.25f, -300.125f, 1.0f);
    SetVector(kRidden + 0xAC, -1.7f, 0.45f, 12.9f, 0.0f);
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = At(kRidden);
    SetVector(kTracker + 0x5C, 0.125f, -0.0625f, 0.3f, 0.75f);
    const float turn[16] = {0.98f, 0.0f, -0.199f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.199f, 0.0f, 0.98f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(&Word<float>(kTracker + 0x6C), turn, sizeof(turn));
    Word<uint8_t>(kTracker + 0xAC) = 1;
    GameVariable<int>(0x00696D60) = 70000;
    GameVariable<uint32_t>(0x00691974) = 0x12345;
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_effects.clear();
        g_frame = 0.0166667f;
        g_gravity = 600.0f;
        g_rehang = 250.0f;
        g_since = 100;
        g_times[0] = 0.5f;
        g_times[1] = 0.8125f;
        g_times[2] = 1.75f;
        g_params = {{0x8E1CF27A, 0.4f}, {0x9B3388FA, 7.5f}};
        StandIns* s = new StandIns;
        SetUpWalk();
        Install(*s);
        scenario(original);
        Rec(0xC0DE, {GameVariable<uint32_t>(0x00691974)});
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

CWalkComponent* Walk() { return &Word<CWalkComponent>(kWalk); }
using Plain = void(__thiscall*)(void*);
void Run(bool original, uint32_t address, void (CWalkComponent::*ours)())
{
    original ? Original<Plain>(address)(Walk()) : (Walk()->*ours)();
    Snapshot(0x5A0);
}
void Halt() { Word<uint8_t>(kTrigger + 0x26) = 1; }
void Unhalt() { Word<uint8_t>(kTrigger + 0x26) = 0; }

void Updates(bool original)
{
    // The states' updates and the pieces Update calls (the original's and ours).
    StandIns& s = *StandIns::s_current;
    S(s, {0x0052D300, AddressOf(&CWalkComponent::UpdateGround)}, 0, 0xD300);
    S(s, {0x0052CEF0, AddressOf(&CWalkComponent::UpdateAir)}, 0, 0xCEF0);
    S(s, {0x0052AD10, AddressOf(&CWalkComponent::UpdateAnimWait)}, 0, 0xAD10);
    S(s, {0x0052A2C0, AddressOf(&CWalkComponent::UpdateAnimSpeed)}, 0, 0xA2C0);
    S(s, {0x00529F80, AddressOf(&CWalkComponent::UpdateCameraOverride)}, 0, 0x9F80);
    for (int state = 0; state <= 5; ++state) {
        for (int variant = 0; variant < 6; ++variant) {
            g_effects.clear();
            Unhalt();
            Word<int>(kWalk + 0x18) = state;
            Word<uint8_t>(kWalk + 0x718) = static_cast<uint8_t>(variant == 0); // (Update clears it)
            Word<uint8_t>(kWalk + 0x9A) = static_cast<uint8_t>(variant & 1);
            Word<uint8_t>(kInput + 0x100) = static_cast<uint8_t>((variant >> 1) & 1);
            Word<int>(kWalk + 0x710) = variant - 2;
            Word<int>(kWalk + 0x1C) = variant < 3 ? 74000 : 74501 - 1; // (the clock: 75000)
            uint32_t stateTag[] = {0xD300, 0xCEF0, 0x0BF0, 0x25B0, 0xAD10};
            if (state <= 4) {
                if (variant == 2)
                    g_effects[stateTag[state]] = Halt;
                if (variant == 3)
                    g_effects[stateTag[state]] = [] { Word<uint8_t>(kWalk + 0x718) = 1; };
                if (variant == 4)
                    g_effects[stateTag[state]] = [] { Word<int>(kWalk + 0x18) = 1; };
                if (variant == 5)
                    g_effects[stateTag[state]] = [] { Word<int>(kWalk + 0x18) = 0; };
            }
            if (variant == 1)
                g_effects[0x7600] = Halt;
            if (variant == 4)
                Word<int>(kWalk + 0x1C) = 74500; // exactly 500 ms
            Run(original, 0x0052D4B0, &CWalkComponent::Update);
        }
    }
}

void Ground(bool original)
{
    struct Case {
        uint32_t anim;
        float skid;
        bool contacts;
        int state;
        bool halted;
        int effect; // 0 none, 1 0x52B370 changes the state, 2 it halts
        bool inside;
        float vx, vz;
    };
    const Case cases[] = {
        {0x1D537EFF, 0.3f, true, 0, false, 0, false, 0, 0},
        {0x1D537EFF, 0.4f, false, 0, false, 0, false, 0, 0},
        {0x1D537EFF, 0.5f, true, 0, false, 0, false, 0, 0},
        {0x9B46E749, 0.1f, true, 1, false, 0, false, 0, 0},
        {0x12345678, 0.1f, true, 0, true, 0, false, 0, 0},
        {0x12345678, 0.1f, true, 0, false, 1, true, 0, 0},
        {0x12345678, 0.1f, true, 0, false, 2, true, 0, 0},
        {0x12345678, 0.1f, true, 0, false, 0, true, 8.0f, 6.0f},
        {0x12345678, 0.1f, true, 0, false, 0, true, 8.0f, 6.01f},
        {0x12345678, 0.1f, true, 0, false, 0, true, -3.5f, 13.25f},
        {0x12345678, 0.1f, true, 0, false, 0, true, 0.5f, -0.25f},
    };
    for (const Case& c : cases) {
        g_effects.clear();
        Unhalt();
        Word<uint32_t>(kWalk + 0x78) = c.anim;
        Word<float>(kWalk + 0x1870) = c.skid;
        g_answers[0xC2C0] = c.contacts ? 1 : 0;
        Word<int>(kWalk + 0x18) = c.state;
        if (c.halted)
            Halt();
        if (c.effect == 1)
            g_effects[0xB370] = [] { Word<int>(kWalk + 0x18) = 2; };
        if (c.effect == 2)
            g_effects[0xB370] = Halt;
        g_answers[0x7B30] = c.inside ? At(kRidden) : 0;
        SetVector(kRidden + 0xAC, c.vx, 0.45f, c.vz, 0.0f);
        Word<uint8_t>(kWalk + 0x186C) = 1;
        Word<uint8_t>(kWalk + 0x1958) = 1;
        Run(original, 0x0052D300, &CWalkComponent::UpdateGround);
    }
    // the move's rounding: x's move kept unrounded - positions of the move's size
    g_answers[0x7B30] = 0;
    uint32_t seed = 4321;
    for (int i = 0; i < 64; ++i) {
        Unhalt();
        g_effects.clear();
        seed = seed * 1103515245u + 12345u;
        float speed = static_cast<float>(seed >> 8) / 65536.0f - 128.0f;
        Word<int>(kWalk + 0x18) = 0;
        Word<float>(kWalk + 0x80) = 0.0137f + static_cast<float>(i) * 0.0011f;
        SetVector(kWalk + 0x34, speed, speed * 0.5f, -speed, speed * 0.25f);
        SetVector(kWalk + 0x24, speed * 0.0731f, speed * 0.0193f, -speed * 0.0457f, 1.0f);
        Run(original, 0x0052D300, &CWalkComponent::UpdateGround);
    }
}

void Air(bool original)
{
    struct Case {
        bool contacts;
        int halt;         // 0 never; 1 at the start; 2 after the move; 3 after 0x5293F0
        bool stateChange; // the move lands it
        float top;        // the extra velocity's top
        bool pad;
        bool held;
        int previous;
        uint32_t since;
        uint32_t grabA, grabB; // 0x43F030, 0x442170
        bool pad2;
        uint32_t special;      // 0x5299F0
        uint32_t acid;         // 0x527510
        uint32_t inside;
        uint32_t hit, alive;
    };
    const Case cases[] = {
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 0, 0, 0},
        {false, 1, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 0, 0, 0},
        {true, 2, false, 1.0e6f, false, false, 0, 0, 0, 0, false, 0, 0, 0, 0, 0},
        {true, 0, true, -1.0e6f, false, false, 0, 0, 0, 0, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 0, 0, 1, 0, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 0, 0, 0, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, true, 0, 0, 1, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 2, 100, 1, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 3, 400, 1, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 4, 250, 0, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 4, 251, 0, 1, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, true, false, 5, 0, 0, 0, false, 0, 0, 0, 0, 0},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, true, 1, 0, 0, 0, 0},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, true, 0, 1, 0, 0, 0},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, false, 1, 0, 0, 0, 0},
        {true, 3, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 1, 1, 1},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 1, 0, 1},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 1, 1, 0},
        {true, 0, false, 0.0f, false, false, 0, 0, 0, 0, false, 0, 0, 1, 1, 1},
    };
    for (const Case& c : cases) {
        g_effects.clear();
        Unhalt();
        Word<int>(kWalk + 0x18) = 1;
        g_answers[0xC2C0] = c.contacts ? 1 : 0;
        if (c.halt == 1)
            Halt();
        if (c.halt == 2)
            g_effects[0xA700] = Halt;
        if (c.halt == 3)
            g_effects[0x93F0] = Halt;
        if (c.stateChange)
            g_effects[0xA700] = [] { Word<int>(kWalk + 0x18) = 0; };
        Word<float>(kWalk + 0x18AC) = c.top;
        Word<uint8_t>(kInput + 0x100) = c.pad ? 1 : 0;
        Word<uint8_t>(kWalk + 0x9A) = c.held ? 1 : 0;
        Word<int>(kWalk + 0x20) = c.previous;
        g_since = c.since;
        g_answers[0xF030] = c.grabA;
        g_answers[0x2170] = c.grabB;
        Word<uint8_t>(kInput + 0x1A0) = c.pad2 ? 1 : 0;
        g_answers[0x99F0] = c.special;
        g_answers[0x7510] = c.acid;
        g_answers[0x7B30] = c.inside ? At(kRidden) : 0;
        g_answers[0x72F0] = c.hit;
        g_answers[0x7210] = c.alive;
        g_answers[0x7240] = At(kRidden);
        Word<uint8_t>(kWalk + 0x186C) = 1;
        Run(original, 0x0052CEF0, &CWalkComponent::UpdateAir);
        GameVariable<uint32_t>(0x00691974) = 0x12345;
    }
    // the move's rounding
    const float frames[] = {1.0f / 60.0f, 0.0333333f, 0.0171f};
    const float speeds[] = {-1.7f, 13.37f, 0.123456f, 777.7f, -31.4159f};
    for (float frame : frames) {
        for (float speed : speeds) {
            Unhalt();
            g_effects.clear();
            Word<int>(kWalk + 0x18) = 1;
            Word<float>(kWalk + 0x80) = frame;
            SetVector(kWalk + 0x34, speed, speed * 0.37f, -speed * 1.3f, speed * 0.01f);
            SetVector(kWalk + 0x189C, -speed * 0.11f, 0.03f, speed, 0.5f);
            Word<float>(kWalk + 0x44) = speed * 0.7f;
            SetVector(kWalk + 0x24, 1234.567f + speed, -98.7654f, 31.0001f * speed, 1.0f);
            g_gravity = 600.0f + speed;
            Run(original, 0x0052CEF0, &CWalkComponent::UpdateAir);
        }
    }
}

void AnimWait(bool original)
{
    SetVector(kWalk + 0x18E4, 10.0f, 2.0f, -4.0f, 1.0f);
    SetVector(kWalk + 0x1904, 13.5f, 6.25f, -9.0f, 1.0f);
    SetVector(kWalk + 0x191C, -0.28f, 0.0f, 0.96f, 0.0f);
    Word<float>(kWalk + 0x1914) = 1.25f;
    Word<float>(kWalk + 0x1918) = 3.5f;
    Word<float>(kWalk + 0x192C) = 0.6f;
    Word<float>(kWalk + 0x1930) = 1.5707963f;
    SetVector(kWalk + 0x1934, 0.0f, 0.0f, 0.0f, 0.0f);
    // the ride: none, its object gone, Update failing, carried (turned or not, tilted)
    Word<uint32_t>(kRide + 0x18) = 0;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    Word<uint32_t>(kRide + 0x18) = At(kTracker);
    Word<uint32_t>(kTracker) = 0;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    Word<uint32_t>(kTracker) = At(kRidden);
    g_answers[0x6F00] = 0;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    g_answers[0x6F00] = 1;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    Word<uint8_t>(kTracker + 0xAC) = 0;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    Word<uint8_t>(kTracker + 0xAC) = 1;
    Word<float>(kTracker + 0x6C + 0x04) = 0.2f;
    Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    Word<uint32_t>(kRide + 0x18) = 0;
    // through the animation: before, during and past the turn's time
    const float currents[] = {0.5f, 0.6f, 0.71f, 0.9f, 1.1f, 1.3f, 1.75f, 2.0f};
    for (float current : currents) {
        g_times[1] = current;
        Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
    }
    // other turns and facings
    const float turns[] = {-2.5f, 0.3f, 3.1f, 0.0f};
    for (float turn : turns) {
        Word<float>(kWalk + 0x1930) = turn;
        SetVector(kWalk + 0x191C, 0.7071f * turn, 0.0f, 0.37f, 0.25f);
        for (float current : {0.55f, 0.75f, 0.95f}) {
            g_times[1] = current;
            Run(original, 0x0052AD10, &CWalkComponent::UpdateAnimWait);
        }
    }
}

void TestWalkUpdate()
{
    SaveGlobals({{0x00696D60, 4, {}}, {0x00691974, 4, {}}});
    RunCase("Walk: Update", Updates);
    RunCase("Walk: on the ground", Ground);
    RunCase("Walk: in the air", Air);
    RunCase("Walk: waiting on an animation", AnimWait);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestWalkUpdate, GameNetKit::Cleanup);
