// Self-tests for the balance of a balance trick (src/game/skater/manual.cpp): the original and ours run the same
// starts and frames on an identical stand-in skater, components, score and rail in an arena; what they call outside
// (the physics values, script globals, the clock, the random numbers, the score's displays, the anim blender, the
// skater's events) is stood in for by recording code - the float-returning ones by C++ functions jumped to
// (viewer_testkit.h for the rest) - and the calls and the whole arena are compared.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/skater/manual.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CBalance;

// Arena layout.
constexpr uint32_t kBalance = 0x0000; // 0x58
constexpr uint32_t kSkater = 0x0100;  // the skater (0x108)
constexpr uint32_t kTrick = 0x0300;   // SkaterBalanceTrick (+0x18 the type)
constexpr uint32_t kScore = 0x0340;   // SkaterScore (+0x18 the score)
constexpr uint32_t kScoreObject = 0x0380;
constexpr uint32_t kInput = 0x2000;   // input (+0x20 its control data)
constexpr uint32_t kButtons = 0x2100; // the held bytes, one per button (by the button's low byte)
constexpr uint32_t kCore = 0x3000;    // SkaterCorePhysics (+0x22C the rail, +0x1528 a pointer: +0x28 a flag)
constexpr uint32_t kRail = 0x4800;    // (+0x4C its id)
constexpr uint32_t kFlags = 0x4900;
constexpr uint32_t kOthers = 0x4A00;  // StatsManager, animblender
constexpr uint32_t kScript = 0x5000;  // a CScript made for the skater

std::map<uint32_t, float> g_physics;
std::map<uint32_t, float> g_floats;
float g_frame;
int g_clock;
bool g_sameRail;
bool g_netGame;
bool g_blender;
int g_rnd;

float __fastcall PhysicsStandIn(void* self, void*, uint32_t name)
{
    Rec(0x90A0, {reinterpret_cast<uint32_t>(self), name});
    auto it = g_physics.find(name);
    return it == g_physics.end() ? 0.0f : it->second;
}
float __cdecl GetFloatStandIn(uint32_t name)
{
    Rec(0x8F00, {name});
    auto it = g_floats.find(name);
    return it == g_floats.end() ? 0.0f : it->second;
}
double __cdecl FrameStandIn()
{
    Rec(0xEAA0, {});
    return g_frame;
}
float __cdecl SignStandIn(float value)
{
    Rec(0x5FD0, {Bits(value)});
    return value < 0.0f ? -1.0f : (value > 0.0f ? 1.0f : 0.0f);
}

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0x589E5FB3: return At(kTrick);
    case 0xD262802F: return At(kScore);
    case 0x27D7CD28: return At(kInput);
    case 0x5BD63B29: return At(kCore);
    case 0x4FF8DAC6: return At(kOthers);
    case 0x7149B753: return g_blender ? At(kOthers + 0x40) : 0u;
    default: return 0;
    }
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.Bytes(0x004F90A0, Branch(0xE9, 0x004F90A0, reinterpret_cast<void*>(&PhysicsStandIn)));
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&GetFloatStandIn)));
    for (uint32_t address : {0x005BEAA0u, AddressOf(&Tmr::GetSlomoFrameSeconds)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&FrameStandIn)));
    for (uint32_t address : {0x00405FD0u, AddressOf(&Mth::Sign)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&SignStandIn)));
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return ComponentFor(r.args[0]);
    });
    s.At({0x005480C0, AddressOf(&GameNet::FoldedGetters::Word04)}, 0, [](Regs& r) {
        Rec(0x80C0, {r.ecx});
        return g_netGame ? 1u : 0u;
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Rec(0xAEA0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00564270}, 4, [](Regs& r) {
        Rec(0x4270, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0051D6A0}, 12, [](Regs& r) {
        Rec(0xD6A0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0046C270}, 4, [](Regs& r) {
        Rec(0xC270, {r.ecx, r.args[0]});
        return At(kButtons + (r.args[0] & 0xFF));
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Rec(0xECE0, {});
        return static_cast<uint32_t>(g_clock);
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Rec(0x3DE0, {r.args[0]});
        return static_cast<uint32_t>((g_rnd++ * 7) % static_cast<int>(r.args[0]));
    });
    s.At({0x00449970, AddressOf(static_cast<void* (*)(size_t)>(&Script::CScript::operator new))}, 0, [](Regs& r) {
        Rec(0x9970, {r.args[0]});
        return At(kScript);
    });
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Rec(0x38E0, {r.ecx});
        return r.ecx;
    });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Rec(0x09F0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00419BD0}, 8, [](Regs& r) {
        Rec(0x9BD0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00563370}, 8, [](Regs& r) {
        Rec(0x3370, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00563390}, 8, [](Regs& r) {
        Rec(0x3390, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0057C200}, 4, [](Regs& r) {
        Rec(0xC200, {r.ecx, r.args[0]});
        return g_sameRail ? 1u : 0u;
    });
}

void PushBalance(std::vector<uint32_t>& t)
{
    const auto* words = Ptr<uint32_t>(kBalance);
    for (int i = 0; i < 0x58 / 4; ++i)
        t.push_back(Rel(words[i]));
}

// Construct, Init, Reset, the small setters, Stop, the destructors; Start on grinds / slides (no rail yet, the same
// rail, another - its time going negative - a robot rail's short run, both lean signs) and manuals (a nose manual,
// a manual, another balance with both random answers; lean speeds zero, small, large, negative; leans past the bail
// angle); Update over many frames (each button alone and both and none, the ignore and safe periods, both lean
// signs, the random shakes small and large, a bail each way, the meter flipped or not, the displays: hidden, no
// score, up / down buttons or not, no balance trick; a network game; the skater with and without a script).
void TestBalance()
{
    SaveGlobals({{0x007CCE14, 4, {}}, {0x00696D60, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_integers.clear();
        g_rnd = 0;
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CCE14) = At(0x7E00);
        GameVariable<int>(0x00696D60) = 100000;
        g_physics = {{0x1231AE3C, 3.5f},   {0xA4362662, 30.0f},  {0xFBEC5B6C, 0.75f}, {0xC5E1C8EB, 1.25f},
                     {0x8F4DB5D3, 20.0f},  {0x93DD6A10, 1.5f},   {0x6CE10EA4, 1.25f}, {0xF81AB0E9, 1500.0f},
                     {0x06851748, 0.002f}, {0xA9FEF5AB, 0.03f},  {0xB6A634F3, 0.7f},  {0x61AD5596, 2.5f},
                     {0xEF27E8FF, 8.0f},   {0x32EB3308, 12.75f}};
        g_floats = {{0xF54CA12F, 0.4f}, {0x30990680, 33.0f}};
        g_integers[0x8CC8F176] = 400;
        g_integers[0x07DA1621] = 150;
        auto* b = Ptr<CBalance>(kBalance);
        auto* skater = Ptr<Obj::CCompositeObject>(kSkater);
        skater->m_id = 0x51;
        *Ptr<uint32_t>(kScore + 0x18) = At(kScoreObject);
        *Ptr<uint32_t>(kCore + 0x22C) = At(kRail);
        *Ptr<uint32_t>(kCore + 0x1528) = At(kFlags);
        *Ptr<int>(kRail + 0x4C) = 17;
        auto call = [&](uint32_t address, void (CBalance::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(b);
            else
                (b->*ours)();
            PushBalance(t);
        };
        for (int indicator : {0, 1}) {
            g_integers[0x5D07E2BE] = indicator;
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00579950)(b)
                                                               : b->Construct())));
            call(0x005798A0, &CBalance::Reset);
        }
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x00579860)(b, skater);
        else
            b->Init(skater);
        auto start = [&](uint32_t left, uint32_t right, uint32_t trick, bool flipped, bool flip) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, bool, bool)>(0x00579A00)(b, left, right, trick,
                                                                                                         flipped, flip);
            else
                b->Start(left, right, trick, flipped, flip);
            PushBalance(t);
        };
        auto setType = [&](uint32_t type) { *Ptr<uint32_t>(kTrick + 0x18) = type; };
        auto hold = [&](uint32_t button, bool held) { *Ptr<uint8_t>(kButtons + (button & 0xFF)) = held; };
        const uint32_t types[] = {0x255ED86F, 0x8D10119D, 0x0AC90769, 0xEF24413B, 0x12345678};
        const float speeds[] = {0.0f, 5.0f, -8.0f, 30.0f, -30.0f, 0.0f}; // (-8: repeated, under the minimum)
        const float leans[] = {0.0f, 100.0f, -1400.0f, 1300.0f, -50.0f, 2.0f};
        const float runs[] = {0.25f, 2.0f, 0.4f, 0.75f};
        int step = 0;
        for (uint32_t type : types) {
            for (int k = 0; k < 6; ++k) {
                ++step;
                setType(type);
                b->m_leanSpeed = speeds[k];
                b->m_lean = leans[(k + step) % 6];
                b->m_cheese = 1.5f * (step % 3);
                b->m_railTime = 0.5f * (step % 4);
                b->m_rail = step % 3 == 0 ? -1 : 9;
                g_sameRail = step % 2 == 0;
                *Ptr<float>(kScoreObject + 0x154C) = runs[step % 4];
                g_clock = 1000 + step * 37;
                b->m_showMeter = step % 2 == 0; // (Start hides it)
                start(0x10 + (step & 1), 0x20 + (step & 2), 0x7A1C0000u + step, step % 3 == 1, step % 5 == 2);
            }
        }
        // Frames.
        const uint32_t buttonSets[][2] = {{0x10, 0x20}, {0xBC6B118F, 0xE3006FC4}, {0xBC6B118F, 0x20}, {0x11, 0xE3006FC4}};
        for (int set = 0; set < 4; ++set) {
            g_frame = 0.016f + 0.004f * set;
            uint32_t left = buttonSets[set][0], right = buttonSets[set][1];
            setType(types[set]);
            b->m_leanSpeed = speeds[set + 1];
            b->m_lean = leans[set + 1];
            b->m_cheese = 2.0f;
            g_clock = 5000;
            start(left, right, 0x7A1C0100u + set, set & 1, set >= 2);
            for (int frame = 0; frame < 24; ++frame) {
                ++step;
                hold(left, frame % 4 == 1 || frame % 7 == 3);
                hold(right, frame % 4 == 2 || frame % 7 == 3);
                g_clock = 5000 + frame * (set == 2 ? 10 : 60);
                g_netGame = frame % 5 == 0;
                g_blender = frame % 3 != 0;
                b->m_showMeter = frame % 4 != 3;
                b->m_hideDisplay = frame % 9 == 4;
                *Ptr<uint8_t>(kFlags + 0x28) = frame % 2;
                *Ptr<uint32_t>(kScore + 0x18) = frame == 11 ? 0u : At(kScoreObject);
                skater->m_script = frame % 6 == 0 ? nullptr : Ptr<Script::CScript>(kScript + 0x100);
                if (frame == 13)
                    b->m_lean = set & 1 ? -1490.0f : 1490.0f; // pushed past the bail angle
                if (frame == 17)
                    setType(0);
                if (frame == 18)
                    setType(types[set]);
                if (frame == 20)
                    b->m_cheese = 0.005f;
                if (frame % 8 == 4)
                    b->m_leanSpeed = set & 1 ? -3.0f : 3.0f; // (slow: the small random shake)
                call(0x00579CC0, &CBalance::Update);
            }
            call(0x00579920, &CBalance::KeepBestTime);
        }
        // Buttons from the start: the right one alone first (not a release), then the left held through the
        // ignore period (exactly at its end: not yet).
        setType(types[0]);
        b->m_leanSpeed = 5.0f;
        b->m_lean = 10.0f;
        g_clock = 7000;
        start(0x10, 0x20, 0x7A1C0200u, false, false);
        for (int frame = 0; frame < 5; ++frame) {
            hold(0x10, frame > 0);
            hold(0x20, frame == 0);
            g_clock = 7000 + 50 * frame;
            call(0x00579CC0, &CBalance::Update);
        }
        // The displays: up / down on one side only, then both.
        const uint32_t sides[][2] = {{0xBC6B118F, 0x20}, {0x10, 0xE3006FC4}, {0xE3006FC4, 0xBC6B118F}};
        for (const auto& side : sides) {
            setType(types[0]);
            b->m_lean = 0.0f;
            b->m_leanSpeed = 1.0f;
            b->m_hideDisplay = false;
            *Ptr<uint32_t>(kScore + 0x18) = At(kScoreObject);
            start(side[0], side[1], 0x7A1C0300u, false, false);
            call(0x00579CC0, &CBalance::Update);
        }
        b->m_leftButton = 0;
        call(0x00579CC0, &CBalance::Update); // not started: nothing
        b->m_leftButton = 0x10;
        b->m_rightButton = 0;
        call(0x00579CC0, &CBalance::Update);
        // The small ones.
        call(0x00579920, &CBalance::KeepBestTime);
        b->m_time = 99.0f;
        call(0x00579920, &CBalance::KeepBestTime);
        call(0x005798F0, &CBalance::ClearBestTime);
        call(0x00579900, &CBalance::HideDisplay);
        call(0x00579910, &CBalance::ShowDisplay);
        call(0x00579940, &CBalance::ShowMeter);
        for (uint32_t score : {At(kScoreObject), 0u}) {
            *Ptr<uint32_t>(kScore + 0x18) = score;
            b->m_leftButton = b->m_rightButton = 5;
            call(0x005799D0, &CBalance::Stop);
        }
        call(0x00579850, &CBalance::DestroyBody);
        for (unsigned int flags : {0u, 1u}) {
            b->m_vtable = nullptr;
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005799A0)(b, flags)
                                                                : b->Destroy(flags))));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "balance");
}

} // namespace

SELFTEST(Early, TestBalance, ViewerTestKit::Cleanup);
