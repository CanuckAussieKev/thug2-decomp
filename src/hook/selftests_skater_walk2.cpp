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
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/trigger_component.h"
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
float g_push;      // 0x00528120's answer
double g_divisor;  // 0x0043DAA0's (unrounded)
std::map<uint32_t, float> g_params;
std::map<uint32_t, std::function<void()>> g_effects; // run when the stand-in with that tag is called

// What the stood-in feeler queries find, in order (none left: nothing hit, or g_answers[0x72F0]).
struct Hit {
    bool hit;
    uint16_t flags;      // the face's
    float distance;
    float point[4];
    float normal[4];
    uint32_t object;     // the collision object hit (an arena offset, or 0)
    uint8_t terrain;
    uint32_t owner;      // the object hit (+0x98, an arena offset, or 0)
};
std::vector<Hit> g_hits;

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

// A function returning a double in ST0 (unrounded): FLD qword [value]; RET.
void ReturnDouble(StandIns& s, std::initializer_list<uint32_t> addresses, const double* value)
{
    uint32_t where = reinterpret_cast<uint32_t>(value);
    std::vector<uint8_t> code = {0xDD, 0x05};
    for (int i = 0; i < 4; ++i)
        code.push_back(static_cast<uint8_t>(where >> (8 * i)));
    code.push_back(0xC3);
    for (uint32_t address : addresses)
        s.Bytes(address, code);
}

// A pointer argument: arena-relative, or a marker for the stack (a local, whose place differs between the runs).
uint32_t Where(uint32_t pointer)
{
    uint32_t relative = Rel(pointer);
    return relative - 0xA0000000u < kArenaSize ? relative : 0x57AC;
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
    for (uint32_t a : {0x005277B0u, 0x00526260u, 0x005264F0u, 0x00500820u, 0x00526850u, 0x00527C50u,
                       0x00526BA0u, 0x005299F0u, 0x00527510u, 0x00526E00u, 0x005293F0u})
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
             std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xB0);
             return r.ecx;
         });
    SVectors(s, {0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, 0x7190, true);
    SVectors(s, {0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, 0x71B0, true);
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        Rec(0x72F0, {r.args[0] & 0xFF, r.args[1] & 0xFF});
        auto* feeler = reinterpret_cast<uint8_t*>(r.ecx);
        RecFloat(0x72F1, reinterpret_cast<const float*>(feeler), 8); // the line
        uint32_t cache = *reinterpret_cast<const uint32_t*>(feeler + 0xAC);
        Rec(0x72F2, {cache ? Where(cache) : 0u}); // (its collision cache: one or none)
        if (g_hits.empty())
            return g_answers[0x72F0];
        Hit hit = g_hits.front();
        g_hits.erase(g_hits.begin());
        if (!hit.hit) {
            if (hit.owner) { // (a miss that leaves an object in the feeler)
                uint32_t owner = At(hit.owner);
                std::memcpy(feeler + 0x98, &owner, 4);
            }
            return 0u;
        }
        std::memcpy(feeler + 0x50, &hit.flags, 2);
        std::memcpy(feeler + 0x52, &hit.terrain, 1);
        uint32_t object = hit.object ? At(hit.object) : 0;
        std::memcpy(feeler + 0x54, &object, 4);
        std::memcpy(feeler + 0x74, &hit.distance, 4);
        std::memcpy(feeler + 0x78, hit.point, 16);
        std::memcpy(feeler + 0x88, hit.normal, 16);
        uint32_t owner = hit.owner ? At(hit.owner) : 0;
        std::memcpy(feeler + 0x98, &owner, 4);
        return 1u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx, Where(r.args[0])});
        return 0u;
    });
    // The feeler copied (its line and hit; recorded by content).
    s.At({0x0043A930, AddressOf(&Nx::CFeeler::Assign)}, 4, [](Regs& r) {
        auto* from = reinterpret_cast<const uint8_t*>(r.args[0]);
        Rec(0xA930, {r.ecx, Where(r.args[0]), *reinterpret_cast<const uint32_t*>(from + 0x54)});
        RecFloat(0xA931, reinterpret_cast<const float*>(from), 8);
        RecFloat(0xA932, reinterpret_cast<const float*>(from + 0x74), 9);
        std::memcpy(reinterpret_cast<void*>(r.ecx), from, 0x98);
        return r.ecx;
    });
    s.At({0x00434BF0, AddressOf(&Obj::CTriggerComponent::CheckFeeler)}, 8, [](Regs& r) {
        Rec(0x4BF0, {r.ecx, r.args[0], Where(r.args[1])});
        auto effect = g_effects.find(0x4BF0);
        if (effect != g_effects.end())
            effect->second();
        return 0u;
    });
    s.At({0x00427CD0, AddressOf(&Obj::CMovableContactComponent::StartRiding)}, 4, [](Regs& r) {
        Rec(0x7CD0, {r.ecx, Where(r.args[0])});
        return 0u;
    });
    s.At({0x005196A0}, 4, [](Regs& r) {
        Rec(0x96A0, {r.ecx, r.args[0] & 0xFFFF});
        return 0u;
    });
    S(s, {0x0046B7F0}, 0, 0xB7F0);
    S(s, {0x00460A20, AddressOf(&Obj::CObject::BroadcastEvent)}, 12, 0x0A20);
    S(s, {0x00441FC0}, 0, 0x1FC0);
    S(s, {0x0043EC40}, 0, 0xEC40);
    S(s, {0x005260E0}, 4, 0x60E0);
    S(s, {0x00416DD0, AddressOf(&Obj::CSmtPtr<Obj::CRefCounted>::Release)}, 0, 0x6DD0);
    S(s, {0x00417210, AddressOf(&Nx::CFeeler::HitObjectAlive)}, 0, 0x7210, true);
    S(s, {0x00417240, AddressOf(&Nx::CFeeler::GetHitObject)}, 0, 0x7240, true);
    S(s, {0x0043EB80}, 4, 0xEB80);
    S(s, {0x0041C350, AddressOf(&Nx::CFeeler::Destruct)}, 0, 0xC350, true);
    // Jump's, the barrier jump's and the contacts'.
    S(s, {0x0046B920, AddressOf(&Obj::CControlButton::GetPressedTime)}, 0, 0xB920);
    ReturnFloat(s, {0x00528120}, &g_push);
    ReturnDouble(s, {0x0043DAA0}, &g_divisor);
    S(s, {0x00529160}, 0, 0x9160);
    S(s, {0x00441DE0}, 4, 0x1DE0);
    S(s, {0x004607A0, AddressOf(&Obj::CObject::ClearEventHandler)}, 4, 0x47A0);
    s.At({0x0040AB50, AddressOf(&Nx::CCollCache::Construct)}, 0, [](Regs& r) {
        Rec(0xAB50, {});
        return r.ecx;
    });
    s.At({0x0040ABA0, AddressOf(&Nx::CCollCache::Update)}, 4, [](Regs& r) {
        RecFloat(0xABA0, reinterpret_cast<const float*>(r.args[0]), 8); // (the box)
        return 0u;
    });
    S(s, {0x0040AB40, AddressOf(&Nx::CCollCache::Destruct)}, 0, 0xAB40, true);
    // AnimWait's.
    S(s, {0x00427C70, AddressOf(&Obj::CMovableContactComponent::StopRiding)}, 0, 0x7C70);
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
        g_hits.clear();
        g_frame = 0.0166667f;
        g_gravity = 600.0f;
        g_rehang = 250.0f;
        g_since = 100;
        g_times[0] = 0.5f;
        g_times[1] = 0.8125f;
        g_times[2] = 1.75f;
        g_params = {{0x8E1CF27A, 0.4f}, {0x9B3388FA, 7.5f}, {0xCEE3A3E1, 4.5f}, {0xAF3E4251, 6.25f},
                    {0xD3193D8E, 0.75f}, {0x17B37748, 1.375f}};
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
    StandIns& s = *StandIns::s_current;
    S(s, {0x0052B370, AddressOf(&CWalkComponent::SnapToGround)}, 0, 0xB370);
    S(s, {0x0052C2C0, AddressOf(&CWalkComponent::UpdateContacts)}, 0, 0xC2C0);
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
    StandIns& s = *StandIns::s_current;
    SVectors(s, {0x0052A700, AddressOf(&CWalkComponent::CollideAirMove)}, 8, 0xA700);
    S(s, {0x0052C2C0, AddressOf(&CWalkComponent::UpdateContacts)}, 0, 0xC2C0);
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

// --- CollideAirMove, SnapToGround, Begin ---

constexpr uint32_t kFrom = kMisc + 0xD00;       // the move's ends
constexpr uint32_t kTo = kMisc + 0xD10;
constexpr uint32_t kGroundObject = kMisc + 0xD40; // collision objects hit (only their addresses matter)
constexpr uint32_t kOtherObject = kMisc + 0xD48;
constexpr uint32_t kFlags = kMisc + 0xD80;      // the skater component's state flags (+0x30, +0xB0)
constexpr uint32_t kComponent1790 = kMisc + 0xEC0;

Hit MakeHit(uint16_t flags, float distance, float px, float py, float pz, float nx, float ny, float nz, float nw,
            uint32_t object, uint8_t terrain, uint32_t owner)
{
    Hit hit = {};
    hit.hit = true;
    hit.flags = flags;
    hit.distance = distance;
    const float point[4] = {px, py, pz, 1.0f};
    const float normal[4] = {nx, ny, nz, nw};
    std::memcpy(hit.point, point, 16);
    std::memcpy(hit.normal, normal, 16);
    hit.object = object;
    hit.terrain = terrain;
    hit.owner = owner;
    return hit;
}
const Hit kMiss = {};

void LandingSetUp()
{
    Word<uint32_t>(kWalk + 0x177C) = At(kMisc + 0x780);  // the trigger component
    Word<uint32_t>(kWalk + 0x1788) = At(kMisc + 0x7C0);  // the terrain's
    Word<uint32_t>(kWalk + 0x1790) = At(kComponent1790);
    Word<uint32_t>(kSkaterComp + 0x1528) = At(kFlags);
    Word<float>(kWalk + 0x94) = 1.0f;                     // control
}

void CallAirMove(bool original)
{
    auto* from = &Word<Mth::Vector>(kFrom);
    auto* to = &Word<Mth::Vector>(kTo);
    if (original)
        Original<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*)>(0x0052A700)(Walk(), from, to);
    else
        Walk()->CollideAirMove(from, to);
    Snapshot(0x5A1);
}

void AirMoves(bool original)
{
    LandingSetUp();
    const Hit wall = MakeHit(0x2, 0.4375f, 11.6f, 18.2f, -2.9f, 0.8f, 0.0f, 0.6f, 0.0f, kGroundObject, 7, 0);
    const Hit wallTilted = MakeHit(0x3, 0.3333333f, 11.65f, 18.15f, -2.85f, -0.6f, 0.1f, 0.79f, 0.25f, kOtherObject, 2,
                                   kRidden);
    const Hit floor = MakeHit(0x1, 0.6f, 12.0f, 17.0f, -2.5f, 0.0f, 1.0f, 0.0f, 0.0f, kGroundObject, 3, kRidden);
    const Hit floorBare = MakeHit(0x0, 0.6f, 12.0f, 17.0f, -2.5f, 0.1f, 0.99f, 0.0f, 0.0f, kGroundObject, 3, 0);
    struct Case {
        std::vector<Hit> hits;
        bool halt;
        int tracker;          // 0 none, 1 its object gone, 2 riding kRidden
        float velocity[4];
        float facing[4];
        uint32_t magnitude;   // (bits)
        float threshold;
    };
    Hit missOwned = kMiss;    // (a miss leaving an object in the feeler)
    missOwned.owner = kRidden;
    std::vector<Case> sweep;  // walls with normals whose w rounds differently when the offset is rounded first
    for (int k = 0; k < 24; ++k) {
        Hit w = wall;
        w.normal[3] = 0.0137f + 0.0731f * k;
        w.point[3] = 1.0f + 0.0193f * k;
        sweep.push_back({{w, kMiss}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f});
    }
    sweep.push_back({{missOwned}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f});
    // (a facing with y: the velocity's y after taking off the ridden object's counts; a velocity exactly across it)
    sweep.push_back({{floorBare}, false, 2, {3, 0.5f, -4, 0.25f}, {0.6f, 0.3f, -0.74f, 0}, 0x3F800000, 1.0f});
    sweep.push_back({{floorBare}, false, 2, {3, 0.5f, -4, 0.25f}, {0.6f, 0.3f, -0.74f, 0}, 0x00000000, 1.0f});
    sweep.push_back({{floor}, false, 0, {2, 0.5f, 2, 0.25f}, {0.5f, 0, 0.5f - 1.0f, 0}, 0x3F800000, 1.0f});
    const Case fixed[] = {
        {{}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{wall, kMiss}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{wall, wallTilted}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{wallTilted, kMiss}, false, 0, {1, 0, 1, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floor}, true, 0, {3, 0, -4, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floor}, false, 0, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floor}, false, 0, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x00000000, 6.0f},
        {{floor}, false, 0, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x00000000, 4.0f},
        {{floor}, false, 0, {3, 0.0f, -4, 0.0f}, {0.6f, 0, -0.8f, 0}, 0x00000000, 5.0f},
        {{floor}, false, 0, {-3, 0.5f, 4, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floor}, false, 0, {4, 0.5f, 3, 0}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floor}, false, 0, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x7FC00000, 9.0f},
        {{floorBare}, false, 1, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floorBare}, false, 2, {3, 0.5f, -4, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x3F800000, 1.0f},
        {{floorBare}, false, 2, {-1.7f, 0.5f, 12.9f, 0.25f}, {0.6f, 0, -0.8f, 0}, 0x00000000, 1.0f},
    };
    std::vector<Case> cases(std::begin(fixed), std::end(fixed));
    cases.insert(cases.end(), sweep.begin(), sweep.end());
    for (const Case& c : cases) {
        Unhalt();
        g_effects.clear();
        if (c.halt)
            g_effects[0x4BF0] = Halt;
        g_hits = c.hits;
        g_answers[0x72F0] = 0;
        SetVector(kFrom, 10.5f, 20.25f, -3.75f, 1.0f);
        SetVector(kTo, 13.1f, 15.6f, -1.3f, 0.75f);
        SetVector(kWalk + 0x24, 12.375f, -3.1f, 1001.7f, 1.0f);
        SetVector(kWalk + 0x34, c.velocity[0], c.velocity[1], c.velocity[2], c.velocity[3]);
        SetVector(kWalk + 0x48, c.facing[0], c.facing[1], c.facing[2], c.facing[3]);
        Word<uint32_t>(kWalk + 0x94) = c.magnitude;
        g_rehang = c.threshold;
        Word<uint32_t>(kRide + 0x18) = c.tracker ? At(kTracker) : 0;
        Word<uint32_t>(kTracker) = c.tracker == 2 ? At(kRidden) : 0;
        CallAirMove(original);
    }
}

void CallSnap(bool original)
{
    if (original)
        Original<void(__thiscall*)(void*)>(0x0052B370)(Walk());
    else
        Walk()->SnapToGround();
    Snapshot(0x5A2);
}

void Snaps(bool original)
{
    LandingSetUp();
    struct Case {
        std::vector<Hit> hits;
        float y;              // the position's
        float offset;         // the step smoothing's height
        uint32_t ground;      // the trigger feeler's hit object (+0x1808)
        bool hasGround;
        int haltAt;           // halted by the n-th 0x00434BF0 (0 never)
        bool input;
        bool still;           // the old position the same as the position
        uint32_t special, hang, ladder; // 0x005299F0, 0x00441FC0, 0x0043EC40
    };
    auto floorAt = [](float y, uint32_t object, uint8_t terrain) {
        return MakeHit(0x1, 0.5f, 12.375f, y, 1001.7f, 0.0f, 1.0f, 0.0f, 0.0f, object, terrain, 0);
    };
    const Hit wall = MakeHit(0x2, 0.5f, 12.0f, -3.0f, 1001.0f, 0.6f, 0.0f, 0.8f, 0.5f, kOtherObject, 1, kRidden);
    const Case cases[] = {
        {{floorAt(-3.3f, kGroundObject, 4)}, -3.1f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-1.9f, kOtherObject, 5)}, -3.1f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-7.5f, kOtherObject, 6)}, -3.1f, 2.75f, kGroundObject, false, 0, false, false, 0, 0, 0},
        {{floorAt(-5.1f, kGroundObject, 0)}, -3.1f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(1.9f, kGroundObject, 0)}, -3.1f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-2.25f, kGroundObject, 0)}, -3.0f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-3.75f, kGroundObject, 0)}, -3.0f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-6.0f, kGroundObject, 0)}, -3.0f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{floorAt(-6.0f, kOtherObject, 0)}, -3.0f, 2.75f, kGroundObject, true, 1, false, false, 0, 0, 0},
        {{floorAt(-6.0f, kOtherObject, 0)}, -3.0f, 2.75f, kGroundObject, true, 2, false, false, 0, 0, 0},
        {{floorAt(-6.0f, kOtherObject, 0)}, -3.0f, 2.75f, kGroundObject, false, 1, false, false, 0, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, false, false, 0, 0, 0},
        {{wall, wall}, -3.1f, 2.75f, kGroundObject, false, 0, false, false, 0, 0, 0},
        {{kMiss, wall}, -3.1f, 2.75f, kGroundObject, true, 0, false, false, 0, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, true, 1, false, false, 0, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, true, true, 1, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, true, true, 0, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, true, false, 1, 0, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, false, false, 1, 1, 0},
        {{kMiss, kMiss}, -3.1f, 2.75f, kGroundObject, false, 0, false, false, 0, 0, 1},
    };
    for (const Case& c : cases) {
        Unhalt();
        g_effects.clear();
        if (c.haltAt) {
            int count = c.haltAt;
            g_effects[0x4BF0] = [count]() mutable {
                if (--count == 0)
                    Halt();
            };
        }
        g_hits = c.hits;
        g_answers[0x72F0] = 0;
        SetVector(kWalk + 0x24, 12.375f, c.y, 1001.7f, 1.0f);
        if (c.still)
            SetVector(kWalk + 0x68, 12.375f, c.y, 1001.7f, 1.0f);
        else
            SetVector(kWalk + 0x68, 12.0f, -3.0f, 1001.0f, 1.0f);
        Word<float>(kWalk + 0x714) = c.offset;
        Word<uint32_t>(kWalk + 0x17B4 + 0x54) = At(c.ground);
        Word<uint8_t>(kWalk + 0x1864) = c.hasGround ? 1 : 0;
        Word<uint8_t>(kInput + 0x1A0) = c.input ? 1 : 0;
        g_answers[0x99F0] = c.special;
        g_answers[0x1FC0] = c.hang;
        g_answers[0xEC40] = c.ladder;
        Word<uint32_t>(kRide + 0x18) = At(kTracker);
        Word<uint32_t>(kTracker) = At(kRidden);
        CallSnap(original);
    }
}

void CallBegin(bool original, bool onGround)
{
    if (original)
        Original<void(__thiscall*)(void*, bool)>(0x0052AF40)(Walk(), onGround);
    else
        Walk()->Begin(onGround);
    Snapshot(0x5A3);
}

void Begins(bool original)
{
    LandingSetUp();
    struct Case {
        bool onGround;
        uint8_t flag30, flagB0;
        uint32_t turn;          // +0x72C (bits)
        float velocity[3];
        float forward[3];
    };
    const Case cases[] = {
        {true, 1, 1, 0x3F800000, {3, 4, 5}, {0, 0, 1}},
        {false, 0, 0, 0x3F800000, {3, 4, 5}, {0, 0, 1}},
        {false, 1, 0, 0x00000000, {3, 4, 5}, {0, 0, 1}},
        {false, 1, 0, 0x3FC00000, {3, 4, 5}, {0.28f, 0.0f, 0.96f}},
        {false, 1, 1, 0x7FC00000, {-2.5f, 7, 0.125f}, {0.6f, 0.0f, -0.8f}},
        {false, 0, 1, 0x3F800000, {0, 9, 0}, {0.3f, 0.9f, 0.4f}},
        {false, 0, 0, 0x3F800000, {0.0005f, 9, 0}, {0.0f, 1.0f, 0.0f}},
        {false, 0, 0, 0x3F800000, {0.001f, 9, 0}, {0.0f, 1.0f, 0.0f}},
        {false, 0, 0, 0x3F800000, {0, 9, 0}, {0.0f, 1.0f, 0.0005f}},
        {false, 0, 0, 0x3F800000, {13.37f, -2, -0.3f}, {0.0f, 1.0f, 0.0f}},
    };
    std::vector<Case> all(std::begin(cases), std::end(cases));
    for (int k = 0; k < 24; ++k) // (thrown off a vert ramp: x and y's products added unrounded)
        all.push_back({false, 1, 0, 0x3FC00000, {3.1f + 0.377f * k, 4.3f - 0.291f * k, 5},
                       {0.28f + 0.0123f * k, 0.0f, 0.96f}});
    g_params[0x17B37748] = 1.3791f;
    for (const Case& c : all) {
        Word<uint8_t>(kWalk + 0x1958) = 0; // (set only off the flat)
        Word<uint8_t>(kWalk + 0x18B0) = 1;
        Word<uint8_t>(kFlags + 0x30) = c.flag30;
        Word<uint8_t>(kFlags + 0xB0) = c.flagB0;
        Word<uint32_t>(kWalk + 0x72C) = c.turn;
        Word<float>(kWalk + 0x730) = 0.7f;
        SetVector(kWalk + 0x734, 0.0f, 1.0f, 0.0f, 1.0f);
        SetVector(kObject + 0xAC, c.velocity[0], c.velocity[1], c.velocity[2], 0.5f);
        SetVector(kObject + 0x8C, c.forward[0], c.forward[1], c.forward[2], 0.25f);
        SetVector(kObject + 0x7C, 0.2f, 0.1f, -0.7f, 0.5f);
        Word<uint32_t>(kRide + 0x18) = At(kTracker);
        Word<uint32_t>(kTracker) = At(kRidden);
        Word<uint32_t>(kComponent1790 + 0x18) = 0x5555;
        CallBegin(original, c.onGround);
    }
}

void TestWalkLanding()
{
    SaveGlobals({{0x00696D60, 4, {}}, {0x00691974, 4, {}}});
    RunCase("Walk: the air's move", AirMoves);
    RunCase("Walk: onto the ground", Snaps);
    RunCase("Walk: beginning", Begins);
    Cleanup();
}

// --- Jump, TryBarrierJump, UpdateContacts ---

constexpr uint32_t kOtherMover = kSkaters + 0x200; // a moving object hit (m_vel +0xAC)

void JumpParams()
{
    g_params[0x12333EBD] = 350.0f;   // hold_time_for_max_jump
    g_params[0x246D0BF3] = 0.55f;    // min_jump_factor
    g_params[0xF2FA5845] = 0.8125f;  // hang_jump_factor
    g_params[0x63D62A21] = 210.3f;   // jump_velocity
    g_params[0xDA85F5AE] = 6.75f;    // barrier_jump_highest_barrier
    g_params[0x99978D2B] = 2.2f;     // feeler_length
    g_params[0x6DA7F696] = 1.3f;     // feeler_height
    g_params[0x78E6A5EC] = 40.0f;    // barrier_jump_max_angle
    g_params[0x72660978] = 0.6f;     // barrier_jump_min_clearance
    g_params[0x38D36700] = 400.0f;   // barrier_jump_delay
    g_params[0xA20C43B7] = 1.1f;     // push_feeler_length
    g_params[0x4D16F37D] = 3.5f;     // push_strength
    g_params[0x0BD6A05D] = 2.0f;     // min_anim_run_speed
}

void SetContact(int i, bool hit, float nx, float ny, float nz, float nw, uint16_t flags = 0, bool clear = false,
                float distance = 0.75f, float speed = 0.0f)
{
    uint32_t c = kWalk + 0xA0 + 0xCC * i;
    Word<uint8_t>(c) = hit ? 1 : 0;
    SetVector(c + 4, nx, ny, nz, nw);
    Word<uint8_t>(c + 0x14) = clear ? 1 : 0;
    Word<float>(c + 0x18) = speed;
    Word<uint16_t>(c + 0x1C + 0x50) = flags;
    Word<float>(c + 0x1C + 0x74) = distance;
}
void ClearContacts()
{
    for (int i = 0; i < 8; ++i)
        SetContact(i, false, 0.1f * i, 0.0f, 0.3f, 0.0f, 0, false, 5.0f, 0.0f);
}

void CallJump(bool original)
{
    original ? Original<Plain>(0x0052B7D0)(Walk()) : Walk()->Jump();
    Snapshot(0x5A4);
}

void Jumps(bool original)
{
    StandIns& s = *StandIns::s_current;
    S(s, {0x0052AB90, AddressOf(&CWalkComponent::LeaveRide)}, 8, 0xAB90);
    LandingSetUp();
    JumpParams();
    g_rehang = 7.5f; // jump_horiz_speed
    struct Case {
        int state;
        uint32_t held;
        bool hasGround, halt;
        uint32_t magnitude; // (bits)
        float control[3];
        float velocity[3];
        float push;
        double divisor;
    };
    const Case cases[] = {
        {0, 0, false, false, 0x00000000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.8f, 1.37},
        {0, 100, true, false, 0x00000000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.8f, 1.37},
        {0, 100, true, true, 0x00000000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.8f, 1.37},
        {1, 100, true, true, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.8f, 1.37},
        {0, 350, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.8f, 1.3700000000000001},
        {0, 700, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.95f, 1.3700000000000001},
        {1, 0x80000000u, false, false, 0x3F800000, {0.6f, 0.1f, -0.8f}, {3, 0.5f, -4}, 0.95f, 0.7123456789},
        {1, 123, false, false, 0x3F400000, {0.6f, 0, -0.8f}, {-3, 0.5f, 4}, 0.6f, 1.1111111111},
        {0, 123, false, false, 0x7FC00000, {0.8f, 0, 0.6f}, {0, 0.5f, 0}, 0.6f, 1.1111111111},
        {0, 7, false, false, 0x00000000, {0.8f, 0, 0.6f}, {0.05f, 0.02f, -0.07f}, 0.6f, 1.25},
        {0, 7, false, false, 0x00000000, {0.8f, 0, 0.6f}, {0.06f, 0.05f, -0.06f}, 0.6f, 1.25},
        {2, 7, true, false, 0x3F800000, {0.8f, 0, 0.6f}, {3, 0.5f, -4}, 0.6f, 1.25},
        {3, 7, true, false, 0x3F800000, {0.8f, 0, 0.6f}, {3, 0.5f, -4}, 0.6f, 1.25},
        {4, 7, true, false, 0x3F800000, {0.8f, 0, 0.6f}, {3, 0.5f, -4}, 0.6f, 1.25},
        {5, 7, true, false, 0x3F800000, {0.8f, 0, 0.6f}, {3, 0.5f, -4}, 0.6f, 1.25},
        {-1, 7, true, false, 0x3F800000, {0.8f, 0, 0.6f}, {3, 0.5f, -4}, 0.6f, 1.25},
    };
    std::vector<Case> all(std::begin(cases), std::end(cases));
    for (int k = 0; k < 12; ++k) { // (rounding: the hold factor, the speed, the hang's plane)
        all.push_back({0, 13u + 29u * k, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {-3.3f + 0.1f * k, 0.5f, 4},
                       0.37f + 0.05f * k, 1.0 + 0.0137 * k});
        all.push_back({k & 1 ? 2 : 4, 7, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.6f, 1.25});
    }
    // (along the control only through y; projected 5 long against a speed of 4.8; left too slow to have a heading)
    all.push_back({0, 50, false, false, 0x3F800000, {0.6f, 0.1f, -0.8f}, {0.3f, 5.0f, 0.4f}, 0.6f, 1.25});
    all.push_back({0, 50, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.64f, 1.0});
    all.push_back({0, 50, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {0.05f, 0, -0.05f}, 0.0f, 1.0});
    const size_t planes = all.size();
    for (int k = 0; k < 32; ++k) // (the hang's plane: large terms that nearly cancel, summed in the original's order)
        all.push_back({k & 1 ? 2 : 4, 7, false, false, 0x3F800000, {0.6f, 0, -0.8f}, {3, 0.5f, -4}, 0.6f, 1.25});
    int n = 0;
    for (const Case& c : all) {
        ++n;
        Unhalt();
        g_effects.clear();
        if (c.halt)
            g_effects[0x4BF0] = Halt;
        Word<int>(kWalk + 0x18) = c.state;
        g_answers[0xB920] = c.held;
        Word<uint8_t>(kWalk + 0x1864) = c.hasGround ? 1 : 0;
        Word<uint32_t>(kWalk + 0x94) = c.magnitude;
        SetVector(kWalk + 0x84, c.control[0], c.control[1], c.control[2], 0.375f);
        SetVector(kWalk + 0x34, c.velocity[0], c.velocity[1], c.velocity[2], 0.625f);
        SetVector(kWalk + 0x48, 0.28f, 0.0f, 0.96f, 0.125f);
        SetVector(kWalk + 0x24, 12.375f + 0.731f * n, -3.1f + 0.377f * n, 1001.7f - 0.913f * n, 1.0f);
        SetVector(kWalk + 0x71C, 0.3f * n, 36.7f, -7.7f * n, 0.0f);
        SetVector(kWalk + 0x18B4, 0.36f, 0.1f, -0.93f, 0.5f);
        if (static_cast<size_t>(n) > planes) {
            SetVector(kWalk + 0x24, 1.0e7f + 37.0f * n, 0.3f + 0.0123f * n, 3.870968e6f + 11.0f * n, 1.0f);
            SetVector(kWalk + 0x71C, 0.37f * n, 0.011f, -0.29f * n, 0.0f);
        }
        Word<float>(kWalk + 0x18D0) = 2.5f + 0.1f * n;
        Word<uint8_t>(kWalk + 0x18B0) = 0;
        SetVector(kObject + 0xAC, 1.5f, -2.25f, 3.0f, 0.75f);
        g_push = c.push;
        g_divisor = c.divisor;
        CallJump(original);
    }
}

void CallBarrier(bool original)
{
    bool jumped = original ? Original<bool(__thiscall*)(void*)>(0x0052BC70)(Walk()) : Walk()->TryBarrierJump();
    Rec(0xBC70, {jumped ? 1u : 0u});
    Snapshot(0x5A5);
}

void Barriers(bool original)
{
    StandIns& s = *StandIns::s_current;
    S(s, {0x0052AB90, AddressOf(&CWalkComponent::LeaveRide)}, 8, 0xAB90);
    LandingSetUp();
    JumpParams();
    const Hit block = MakeHit(0x1, 0.5f, 0, 0, 0, 0, 0, 1, 0, kGroundObject, 0, 0);
    struct Case {
        bool flag70;
        int setup;              // which contacts (see below)
        std::vector<Hit> hits;
        bool hasGround, halt;
    };
    const std::vector<Case> cases = {
        {true, 1, {}, false, false},
        {false, 0, {}, false, false},
        {false, 1, {block}, false, false},
        {false, 1, {kMiss, kMiss, kMiss, block}, false, false},
        {false, 1, {kMiss, kMiss, kMiss, kMiss, kMiss, kMiss, kMiss, kMiss, kMiss, kMiss}, true, false},
        {false, 1, {kMiss, block}, true, true},
        {false, 2, {kMiss, block, kMiss, kMiss, block}, false, false},
        {false, 2, {kMiss, kMiss, kMiss, block, kMiss, block}, true, false},
        {false, 3, {kMiss, kMiss, block, kMiss, block}, false, false},
        {false, 4, {kMiss, kMiss, block, block}, false, false},
        {false, 5, {kMiss, kMiss, block}, false, false},
        {false, 1, {kMiss, kMiss, kMiss, block}, false, false},
    };
    std::vector<Case> all = cases;
    for (int k = 0; k < 24; ++k) // (the feelers' ends for many facings)
        all.push_back({false, 3, {kMiss, kMiss, kMiss, block, block}, false, false});
    int n = 0;
    for (const Case& c : all) {
        ++n;
        Unhalt();
        g_effects.clear();
        if (c.halt)
            g_effects[0x4BF0] = Halt;
        Word<uint8_t>(kFlags + 0x70) = c.flag70 ? 1 : 0;
        ClearContacts();
        // 1: straight ahead, head-on; 2: also both sides (one angled away, one a flag 0x2 face already clear);
        // 3: the sides only, nearly head-on (within 40 degrees: cos 40 = 0.766); 4: just past 40 degrees.
        if (c.setup == 1 || c.setup == 2)
            SetContact(0, true, -0.6f, 0.0f, 0.8f, 0.25f);
        if (c.setup == 2) {
            SetContact(1, true, 0.8f, 0.0f, 0.6f, 0.0f, 0, true);
            SetContact(6, true, -0.6f, 0.0f, 0.8f, 0.0f, 0x2, true);
        }
        if (c.setup == 3) {
            SetContact(1, true, -0.2f, 0.0f, 0.97f, 0.5f);
            SetContact(6, true, -0.9f, 0.0f, 0.43f, 0.0f);
        }
        if (c.setup == 4)
            SetContact(1, true, -0.95f, 0.0f, -0.3f, 0.0f, 0, true);
        if (c.setup == 5) { // (a contact clear from before but not touching now)
            SetContact(0, true, -0.6f, 0.0f, 0.8f, 0.25f);
            SetContact(6, false, -0.6f, 0.0f, 0.8f, 0.0f, 0, true);
        }
        g_hits = c.hits;
        g_answers[0x72F0] = 0;
        Word<uint8_t>(kWalk + 0x1864) = c.hasGround ? 1 : 0;
        SetVector(kWalk + 0x84, 0.6f, 0.0f, -0.8f, 0.0f);
        SetVector(kWalk + 0x48, 0.6f + 0.01f * n, 0.0f, -0.8f, 0.25f);
        SetVector(kWalk + 0x24, 12.375f + 0.731f * n, -3.1f + 0.377f * n, 1001.7f - 0.913f * n, 1.0f + 0.13f * n);
        if (n == 12) // (high up: the barrier's top found below where it started)
            Word<float>(kWalk + 0x28) = 20.0f;
        // (facings whose feeler ends show the rounding: found by out/scripts/search_round.py)
        static const float facings[][3] = {
            {0.3182440996170044f, 0.9480088353157043f, 0.43056967854499817f},
            {-0.4610297679901123f, -0.8873846530914307f, 0.18763907253742218f},
            {-0.11279186606407166f, -0.9936186075210571f, -0.2802896499633789f},
            {-0.15449213981628418f, -0.9879940152168274f, 0.1704937368631363f},
            {0.9493868350982666f, -0.31410932540893555f, 0.26652783155441284f},
            {-0.981549859046936f, -0.19120633602142334f, 0.015360434539616108f},
            {0.6775124073028564f, -0.7355113625526428f, -0.37769049406051636f},
            {-0.04440072551369667f, -0.9990137815475464f, -0.014810461550951004f},
            {0.9122633337974548f, 0.4096042513847351f, -0.056298062205314636f},
            {-0.04182950779795647f, 0.9991247653961182f, -0.11605804413557053f},
            {-0.7444955706596375f, 0.6676273941993713f, 0.1303531378507614f},
            {-0.5443621873855591f, -0.8388503193855286f, -0.3893773853778839f}};
        if (n > 12 && n - 13 < 12) {
            const float* f = facings[n - 13];
            SetVector(kWalk + 0x48, f[0], 0.0f, f[1], 0.25f);
            Word<float>(kWalk + 0x2C) = f[2];
        } else if (n > 12) {
            SetVector(kWalk + 0x48, 0.6f - 0.0613f * n, 0.0f, -0.8f + 0.0291f * n, 0.25f);
        }
        g_gravity = 600.0f + 13.7f * n;
        CallBarrier(original);
    }
}

void CallContacts(bool original)
{
    bool touching = original ? Original<bool(__thiscall*)(void*)>(0x0052C2C0)(Walk()) : Walk()->UpdateContacts();
    Rec(0xC2C0, {touching ? 1u : 0u});
    Snapshot(0x5A6);
}

void Contacts(bool original)
{
    StandIns& s = *StandIns::s_current;
    S(s, {0x0052BC70, AddressOf(&CWalkComponent::TryBarrierJump)}, 0, 0xBC70);
    LandingSetUp();
    JumpParams();
    SetVector(kOtherMover + 0xAC, 0.75f, 0.5f, -1.25f, 0.0f);
    auto wall = [](float nx, float ny, float nz, float nw, float distance, uint32_t owner = 0) {
        return MakeHit(0x1, distance, 1, 2, 3, nx, ny, nz, nw, kGroundObject, 0, owner);
    };
    // The hits for feelers 0..7 (`at` lists which feelers hit, the others miss).
    auto around = [](std::initializer_list<std::pair<int, Hit>> at) {
        std::vector<Hit> hits(8, kMiss);
        for (const auto& [i, hit] : at)
            hits[i] = hit;
        return hits;
    };
    struct Case {
        int state;
        std::vector<Hit> hits;
        float velocity[4];
        bool alive;        // the hit object alive (0x00417210)
        int object;        // 0x00417240: 0 kRidden, 1 kOtherMover; the tracker: 2 none, 3 its object gone
        bool allowed;      // +0x98
        bool timed;        // +0x17A8
        int since;         // +0x17AC
        uint32_t jumpAnswer, barrierAnswer; // 0x00441DE0, TryBarrierJump
        bool jumpPressed, jumpHeld, halted;
        float groundY;     // the ground's normal's y
        float speed;       // +0x704
    };
    const Hit facingIt = wall(-0.6f, 0.2f, 0.8f, 0.25f, 0.3f);
    const Hit sideways = wall(0.8f, 0.1f, 0.6f, 0.5f, 0.9f);
    const Hit opposite = wall(0.6f, 0.0f, -0.8f, 0.0f, 0.7f);
    const Hit tilted = wall(-0.3f, 0.9f, 0.3f, 0.0f, 0.2f);
    const Hit flatFacing = wall(0.0f, 0.4f, 0.0f, 0.0f, 0.2f);
    const std::vector<Case> cases = {
        {0, {}, {3, 0.5f, -4, 0.25f}, false, 0, false, true, 0, 0, 0, false, false, false, 1.0f, 3.0f},
        {0, around({{0, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, false, 0, 0, 0, false, false, false, 1.0f, 3.0f},
        {0, around({{0, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74000, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74000, 1, 0, false, false, false, 1.0f, 3.0f},
        {0, around({{6, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74000, 0, 0, false, false, false, 1.0f, 3.0f},
        {0, around({{6, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74600, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{6, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74601, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{0, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 80000, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{2, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 74000, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{0, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, false, true, 74000, 0, 1, false, false, false, 1.0f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 1, 0, true, false, false, 1.0f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 1, 0, true, true, false, 1.0f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, true, false, false, 1.0f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, true, 1.0f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.3f, 3.0f},
        {0, around({{3, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.5f, 3.0f},
        {0, around({{3, sideways}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.4999f, 3.0f},
        {0, {}, {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {2, around({{3, facingIt}, {4, sideways}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{7, tilted}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{7, flatFacing}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{7, facingIt}, {0, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{2, sideways}, {5, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 1.5f},
        {1, around({{2, facingIt}, {5, opposite}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{2, facingIt}, {5, opposite}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 1.5f},
        {1, around({{1, opposite}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, true, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, true, 1, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, true, 2, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, true, 3, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {0.00005f, 0, -0.00005f, 0}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{2, tilted}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 2.0f},
        {1, around({{1, facingIt}}), {3, 0.5f, -4, 0.25f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 2.0000002f},
        {1, around({{1, wall(-1, 0, 0, 0, 0.9f)}}), {3, 0, 0, 0.5f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
        {1, around({{1, wall(-1, 0, 0, 0.5f, 0.9f)}}), {3, 0, 0.0001f, 0.5f}, false, 0, true, true, 0, 0, 0, false, false, false, 0.2f, 3.0f},
    };
    std::vector<Case> all = cases;
    for (int k = 0; k < 8; ++k) { // (slid straight back along its velocity: the length equal to the speed along it)
        Hit back = wall(0.0f, 0.0f, 1.0f, 0.5f + 0.37f * k, 0.9f);
        all.push_back({1, around({{3, back}}), {0, 3.0f + 0.1f * k, -4, 0.3f + 0.013f * k}, false, 0, true, true, 0, 0, 0,
                       false, false, false, 0.2f, 3.0f});
    }
    for (int k = 0; k < 24; ++k) { // (rounding: steep ground's push and the contacts' push)
        Hit nearby = wall(-0.6f + 0.011f * k, 0.2f, 0.8f, 0.25f + 0.07f * k, 0.05f + 0.01f * k);
        all.push_back({0, around({{3, nearby}}), {3.1f, 0.5f, -4.3f, 0.25f}, false, 0, false, false, 0, 0, 0, false, false,
                       false, 0.1f + 0.01f * k, 3.0f});
    }
    for (int k = 0; k < 16; ++k) { // (rounding: the slide's unrounded x and w, the push, the fall)
        Hit a = wall(-0.6f + 0.031f * k, 0.2f, 0.8f - 0.017f * k, 0.25f + 0.07f * k, 0.1f + 0.03f * k);
        Hit b = wall(0.8f - 0.043f * k, 0.1f, 0.6f + 0.011f * k, 0.5f - 0.02f * k, 0.9f);
        all.push_back({k & 1, around({{k % 7, a}, {(k + 3) % 7, b}}), {3.1f + 0.21f * k, 0.5f - 0.03f * k, -4.3f + 0.17f * k, 0.25f + 0.11f * k},
                       (k & 2) != 0, 1, true, true, 0, 0, 0, false, false, false, 0.2f, 1.0f + 0.4f * k});
    }
    int n = 0;
    for (const Case& c : all) {
        ++n;
        Unhalt();
        if (c.halted)
            Halt();
        g_effects.clear();
        ClearContacts();
        Word<int>(kWalk + 0x18) = c.state;
        g_hits = c.hits;
        g_answers[0x72F0] = 0;
        g_answers[0x7210] = c.alive ? 1 : 0;
        g_answers[0x7240] = At(c.object == 1 ? kOtherMover : kRidden);
        Word<uint32_t>(kRide + 0x18) = c.object == 2 ? 0 : At(kTracker);
        Word<uint32_t>(kTracker) = c.object == 3 ? 0 : At(kRidden);
        Word<uint8_t>(kWalk + 0x98) = c.allowed ? 1 : 0;
        Word<uint8_t>(kWalk + 0x17A8) = c.timed ? 1 : 0;
        Word<int>(kWalk + 0x17AC) = c.since;
        g_answers[0x1DE0] = c.jumpAnswer;
        g_answers[0xBC70] = c.barrierAnswer;
        Word<uint8_t>(kInput + 0x100) = c.jumpPressed ? 1 : 0;
        Word<uint8_t>(kWalk + 0x9A) = c.jumpHeld ? 1 : 0;
        SetVector(kWalk + 0x1794, 0.6f, c.groundY, 0.7f + 0.01f * n, 0.375f);
        Word<float>(kWalk + 0x704) = c.speed;
        Word<uint8_t>(kWalk + 0x186C) = 0x55;
        SetVector(kWalk + 0x34, c.velocity[0], c.velocity[1], c.velocity[2], c.velocity[3]);
        SetVector(kWalk + 0x48, 0.6f + 0.01f * n, 0.0f, -0.8f, 0.25f);
        SetVector(kWalk + 0x24, 12.375f + 0.731f * n, -3.1f + 0.377f * n, 1001.7f - 0.913f * n, 1.0f + 0.13f * n);
        SetVector(kWalk + 0x71C, 0.3f * n, 0.7f, -0.77f * n, 0.125f);
        Word<float>(kWalk + 0x44) = 6.5f - 0.3f * n;
        Word<float>(kWalk + 0x80) = 1.0f / (55.0f + n);
        SetVector(kWalk + 0x1794, 0.6f + 0.0137f * n, c.groundY, 0.7f + 0.01f * n, 0.375f + 0.031f * n);
        Word<float>(kWalk + 0x30) = 1.0f + 0.0137f * n;
        g_gravity = 600.0f + 13.7f * n;
        CallContacts(original);
    }
    // (a feeler's direction and the fall's ends for each state and facing, every feeler missing)
    // (pushes whose unrounded size shows in the position: found by out/scripts/search_push.py)
    const float pushes[][2] = {{0.14201825857162476f, 0.02380952425301075f}, {-0.1370023638010025f, 0.03333333507180214f},
                               {0.13268065452575684f, 0.029411764815449715f}, {-0.06948266178369522f, 0.014925372786819935f},
                               {-0.10654174536466599f, 0.02083333395421505f}, {-0.0738891139626503f, 0.022727273404598236f},
                               {-0.1719105988740921f, 0.012658228166401386f}, {-0.1145993322134018f, 0.01123595517128706f}};
    for (const auto& p : pushes) {
        ClearContacts();
        Unhalt();
        Word<int>(kWalk + 0x18) = 0;
        g_hits = around({{3, wall(0.0f, 0.0f, 1.0f, 0.0f, 0.1f)}});
        Word<uint8_t>(kWalk + 0x98) = 0;
        Word<uint8_t>(kInput + 0x100) = 0;
        SetVector(kWalk + 0x1794, 1.0f, 0.3f, 0.0f, 0.0f);
        SetVector(kWalk + 0x24, p[0], 0.0f, p[0], 1.0f);
        Word<float>(kWalk + 0x80) = p[1];
        CallContacts(original);
    }
    for (int k = 0; k < 48; ++k) {
        ClearContacts();
        Word<int>(kWalk + 0x18) = k % 3;
        g_hits.clear();
        SetVector(kWalk + 0x48, 0.017f * k - 0.5f, 0.0f, 0.3f + 0.01f * k, 0.25f);
        SetVector(kWalk + 0x24, 12.375f + 0.731f * k, -3.1f + 0.377f * k, 1001.7f - 0.913f * k, 1.0f + 0.0731f * k);
        SetVector(kWalk + 0x71C, 0.3f * k, 0.7f, -0.77f * k, 0.1234f + 0.0177f * k);
        SetVector(kWalk + 0x34, 3.1f + 0.21f * k, 0.5f - 0.03f * k, -4.3f + 0.17f * k, 0.25f + 0.011f * k);
        Word<float>(kWalk + 0x44) = 6.5f - 0.31f * k;
        Word<float>(kWalk + 0x80) = 1.0f / (51.0f + k);
        g_gravity = 600.0f + 13.7f * k;
        CallContacts(original);
    }
}

void TestWalkJumps()
{
    SaveGlobals({{0x00696D60, 4, {}}, {0x00691974, 4, {}}});
    RunCase("Walk: jumping", Jumps);
    RunCase("Walk: a barrier jump", Barriers);
    RunCase("Walk: the contacts", Contacts);
    Cleanup();
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
SELFTEST(Early, TestWalkLanding, GameNetKit::Cleanup);
SELFTEST(Early, TestWalkJumps, GameNetKit::Cleanup);
