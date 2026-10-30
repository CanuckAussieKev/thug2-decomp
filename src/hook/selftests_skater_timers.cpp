// Self-tests for the skater's rotate, run timer and score components (src/game/skater/rotate_component.cpp,
// run_timer.cpp, skater_score.cpp; 0x00517CC0-0x00518C6E): the original and ours on the same stand-in skater, every
// call they make recorded.

#include <initializer_list>
#include <utility>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/crc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/skater.h"
#include "game/skater/rotate_component.h"
#include "game/skater/run_timer.h"
#include "game/skater/skater_score.h"
#include "game/sys/timer.h"
#include "game/skate/score.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0400, kWalk = 0x0800, kScript = 0x0900,
                   kParams = 0x0A00, kResults = 0x0B00, kScore = 0x0C00, kHeap = 0x4000;

struct Answers {
    uint32_t calloc, heap;
    float slomo, angleInPlane, scriptFloat, physicsValue;
    bool spawned, alwaysSpecial, latest;
    int total, nonGap;
};
Answers g_a;
void* g_vtable[16];

uint32_t Allocate(uint32_t size)
{
    uint32_t at = g_a.heap;
    g_a.heap += (size + 15) & ~15u;
    std::memset(At(at), 0xCD, size);
    return Addr(at);
}

void HashMatrix(uint32_t address) { g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(address), 64)); }

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        return r.args[0] == 0x5BD63B29 ? Addr(kCore) : r.args[0] == 0x726E85AA ? Addr(kWalk) : 0u;
    });
    RecFloat(s, {0x005BEAA0, AddressOf(&Tmr::GetSlomoFrameSeconds)}, 0, 0xEAA0, [](Regs&) { return g_a.slomo; }, false);
    // The matrices: each function's result made from its arguments, so wrong ones show.
    s.At({0x00404EC0, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, int, float)>(&Mth::CreateRotateMatrix))}, 0,
         [](Regs& r) {
             Record({0x4EC0, r.args[1], r.args[2]});
             auto* m = reinterpret_cast<float*>(r.args[0]);
             for (int i = 0; i < 16; ++i)
                 m[i] = static_cast<float>(i * (r.args[1] + 1)) + Float(r.args[2]);
             return r.args[0];
         });
    s.At({0x00404DD0, AddressOf(&Mth::CreateRotateXMatrix)}, 0, [](Regs& r) {
        Record({0x4DD0, r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i) - Float(r.args[1]);
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        Record({0xFA8C, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2])});
        HashMatrix(r.args[1]);
        HashMatrix(r.args[2]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 0.5f + b[15 - i];
        std::memcpy(out, result, 64);
        return r.args[0];
    });
    for (uint32_t address : {0x00405710u, 0x004057B0u}) {
        uint32_t ours = address == 0x00405710 ? AddressOf(&Mth::Matrix::RotateYLocal) : AddressOf(&Mth::Matrix::RotateZLocal);
        Rec(s, {address, ours}, 1, address & 0xFFFF, [](Regs& r) {
            HashMatrix(r.ecx);
            reinterpret_cast<float*>(r.ecx)[5] += Float(r.args[0]);
            return r.ecx;
        });
    }
    Rec(s, {0x00404090, AddressOf(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(
                            &Mth::Matrix::operator=))},
        1, 0x4090, [](Regs& r) {
            HashMatrix(r.args[0]);
            std::memmove(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 64);
            return r.ecx;
        });
    RecFloat(s, {0x004062D0, AddressOf(&Mth::GetAngleInPlane)}, 4, 0x62D0, [](Regs& r) {
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 16));
        return g_a.angleInPlane;
    }, false, true);
    Rec(s, {0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 2, 0xC1B0, [](Regs&) { return Addr(kParams + 0xC0); }, false,
        true);
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Record({0xC050, Rel(r.args[0])});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 10.0f;
             v[1] = -3.0f;
             v[2] = 25.0f;
             v[3] = 1.0f;
             return 1u;
         });
    // The run timer's.
    RecFloat(s, {0x00478F00, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}, 1, 0x8F00,
             [](Regs&) { return g_a.scriptFloat; }, false, true);
    Rec(s, {0x00472E20, AddressOf(&Script::FindSpawnedScriptWithId)}, 1, 0x2E20,
        [](Regs&) { return g_a.spawned ? 0x5C000000u : 0u; }, false, true);
    Rec(s, {0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 6, 0x2F00);
    Rec(s, {0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 2, 0x09F0);
    RecFloat(s, {0x00580790, AddressOf(&Obj::CSkater::GetScaledPhysics)}, 1, 0x0790, [](Regs&) { return g_a.physicsValue; });
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
        2, 0x7D40);
    Rec(s, {0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))},
        2, 0x7C60);
    Rec(s, {0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))},
        2, 0x7B80);
    Rec(s, {0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 3, 0xF060, nullptr, false, true);
    // (0x005BAB90, the base's empty GetDebugInfo: run as it is - ours leaves the call out)
    // The score's.
    Rec(s, {0x00563440, AddressOf(&Mdl::Score::Construct)}, 0, 0x3440, [](Regs& r) { return r.ecx; });
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x00561290u, AddressOf(&Mdl::Score::SetSkaterId)}, {0x00564270u, AddressOf(&Mdl::Score::TweakTrick)}, {0x00561A80u, AddressOf(&Mdl::Score::SetTotalScore)}})
        Rec(s, {address, ours_}, 1, address & 0xFFFF);
    Rec(s, {0x005843A0, AddressOf(&Obj::CSkaterCareer::SetGlobalFlag)}, 1, 0x43A0);
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x00564B80u, AddressOf(&Mdl::Score::DestroyBody)}, {0x00563580u, AddressOf(&Mdl::Score::Update)}, {0x00562DF0u, AddressOf(&Mdl::Score::FillSpecial)}, {0x00564A10u, AddressOf(&Mdl::Score::Reset)}})
        Rec(s, {address, ours_}, 0, address & 0xFFFF);
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x00563370u, AddressOf(&Mdl::Score::SetBalanceMeter)}, {0x00563390u, AddressOf(&Mdl::Score::SetManualMeter)}})
        Rec(s, {address, ours_}, 2, address & 0xFFFF);
    Rec(s, {0x00584CC0, AddressOf(&Obj::CSkaterCareer::GetCheat)}, 1, 0x4CC0, [](Regs&) { return g_a.alwaysSpecial ? 1u : 0u; });
    Rec(s, {0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 1, 0x8F60,
        [](Regs& r) { return r.args[0] ^ 0x1234u; }, false, true);
    Rec(s, {0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, 0x1B40, [](Regs&) { return static_cast<uint32_t>(g_a.total); });
    Rec(s, {0x005633B0, AddressOf(&Mdl::Score::CountNonGapTricks)}, 0, 0x33B0, [](Regs&) { return static_cast<uint32_t>(g_a.nonGap); });
    Rec(s, {0x00562410, AddressOf(&Mdl::Score::LastTrickIsKeyCombo)}, 4, 0x2410, [](Regs&) { return g_a.latest ? 1u : 0u; });
    Rec(s, {0x005623B0, AddressOf(&Mdl::Score::LastTrickIs)}, 4, 0x23B0, [](Regs&) { return g_a.latest ? 0x101u : 0x100u; });
    s.At({0x00472E60, AddressOf(&Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        uint32_t h = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x2E60, h});
        return h;
    });
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_a.heap = kHeap;
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kScript + 0x14) = Addr(kResults);
    F<uint32_t>(kCore + 0x0C) = Addr(kObject);
}

using Cmf = int(__thiscall*)(void*, uint32_t, void*, void*);

// Rotate: made, destroyed, its turns over time (by axis, duration units, a node) stepped to their end, and at once.
void TestSkaterRotate()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x00517D40, 0x00517CC0, 0x00517CF0, 0x00517CD0, 0x00517D20, 0x00517DC0,
                                       0x00517EA0});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00517D40, 0x00517CC0, 0x00517CF0, kComponent, 0x58, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00517CD0))(c, At(kParams));
            trace.push_back(F<uint32_t>(kComponent + 0x28));
            trace.push_back(F<uint32_t>(kComponent + 0x3C));
            trace.push_back(F<uint32_t>(kComponent + 0x50));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517D20))(c);
            Lcg random(seed * 5);
            for (int k = 0; k < 24; ++k) {
                F<uint32_t>(kComponent) = reinterpret_cast<uint32_t>(g_vtable);
                g_structs.clear();
                void* p = At(kParams);
                // Over time (k < 16): a duration in ms / seconds / frames, some of x / y / z, a Node; else at once.
                if (k < 16) {
                    SetComponent(p, 0x79A07F3F, Bits(static_cast<float>(100 + random.Below(900))));
                    const uint32_t units[] = {0, 0xD029F619, 0x49E0EE96, 0x019176C5, 0x4A07C332};
                    if (uint32_t unit = units[random.Below(5)])
                        SetComponent(p, unit, 0);
                }
                for (uint32_t axis : {0x7323E97Cu, 0x0424D9EAu, 0x9D2D8850u}) {
                    if (random.Below(2))
                        SetComponent(p, axis, Bits(static_cast<float>(static_cast<int>(random.Below(721)) - 360)));
                }
                if (k < 16 && random.Below(3) == 0)
                    SetComponent(p, 0x7A8017BA, 0x40DE0000 + k);
                F<float>(kObject + 0x4C) = static_cast<float>(random.Below(100));
                F<uint8_t>(kCore + 0x34C) = static_cast<uint8_t>(random.Below(2));
                g_a.angleInPlane = static_cast<float>(static_cast<int>(random.Below(629)) - 314) / 100.0f;
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<Cmf>(Code(original, 0x00517EA0))(
                    c, k == 23 ? 0x12345678u : 0xC876ADF3u, p, At(kScript))));
                for (uint32_t i = 0x18; i < 0x54; i += 4)
                    trace.push_back(F<uint32_t>(kComponent + i));
                trace.push_back(F<uint8_t>(kCore + 0x34C));
                // A few frames (the slow motion varied: some frames end a turn).
                for (int frame = 0; frame < 6; ++frame) {
                    g_a.slomo = static_cast<float>(1 + random.Below(40)) / 60.0f;
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517DC0))(c);
                    for (uint32_t i = 0x18; i < 0x54; i += 4)
                        trace.push_back(F<uint32_t>(kComponent + i));
                }
            }
            // A turn reaching its end exactly, and one with no rate past its end.
            for (int k = 0; k < 2; ++k) {
                auto* turn = reinterpret_cast<Obj::CSkaterRotateComponent::Turn*>(At(kComponent + 0x18 + 0x14 * k));
                turn->active = true;
                turn->remaining = 1.0f;
                turn->turned = k == 0 ? 0.5f : 2.0f;
                turn->rate = k == 0 ? 1.0f : 0.0f;
                g_a.slomo = 0.5f;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517DC0))(c);
                for (uint32_t i = 0x18; i < 0x54; i += 4)
                    trace.push_back(F<uint32_t>(kComponent + i));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater rotate: made, destroyed, turned over time and at once");
    }
    delete &s;
}

// The run timer: made, destroyed, registered, finalized; its debug info; updates in each state (hanging or not);
// stopped, resumed (the cap by resumes), the member functions.
void TestSkaterRunTimer()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x005185D0, 0x00518240, 0x00518260, 0x005182B0, 0x005182E0, 0x00518270,
                                       0x00518300, 0x00518390, 0x005183F0, 0x005184A0, 0x005184F0, 0x00518500,
                                       0x00518540, 0x00518570, 0x005185A0});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x005185D0, 0x00518260, 0x005182B0, kComponent, 0x28, g_a.calloc, trace);
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00518240))();
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005182E0))(c);
            Lcg random(seed * 11);
            for (int k = 0; k < 60; ++k) {
                F<int>(kComponent + 0x18) = static_cast<int>(random.Below(4));
                F<float>(kComponent + 0x1C) = static_cast<float>(static_cast<int>(random.Below(400)) - 100) / 64.0f;
                F<int16_t>(kComponent + 0x20) = static_cast<int16_t>(static_cast<int>(random.Below(12)) - 2);
                F<int>(kWalk + 0x18) = static_cast<int>(random.Below(6));
                F<uint32_t>(kObject + 0x2BC) = random.Below(4);
                g_a.slomo = static_cast<float>(1 + random.Below(60)) / 60.0f;
                g_a.scriptFloat = static_cast<float>(random.Below(4)) / 2.0f;
                g_a.physicsValue = 1.0f + static_cast<float>(random.Below(30)) / 4.0f;
                g_a.spawned = random.Below(2) != 0;
                if (k % 13 == 7) { // (exactly 0 left after the frame / at a resume; NaN at a resume)
                    F<float>(kComponent + 0x1C) = 0.5f;
                    F<int>(kWalk + 0x18) = 0;
                    g_a.slomo = 0.5f;
                }
                if (k % 11 == 4 && (k / 11) % 3 != 2) // (a resume with exactly 0 left, or NaN)
                    F<float>(kComponent + 0x1C) = (k / 11) % 3 == 0 ? 0.0f : std::numeric_limits<float>::quiet_NaN();
                switch (k % 11) {
                case 0: reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00518270))(c, At(kResults)); break;
                case 1:
                case 2: reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00518300))(c); break;
                case 3: reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00518390))(c); break;
                case 4: reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005183F0))(c); break;
                default: {
                    const uint32_t members[] = {0x005184A0, 0x005184F0, 0x00518500, 0x00518540, 0x00518570, 0x005185A0};
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*)>(
                                        Code(original, members[k % 11 - 5]))(c, At(kParams), At(kScript))
                                        ? 1u
                                        : 0u);
                    break;
                }
                }
                for (uint32_t i = 0x18; i < 0x24; i += 4)
                    trace.push_back(F<uint32_t>(kComponent + i));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater run timer: made, destroyed, updated, stopped, resumed, member functions");
    }
    delete &s;
}

// The score: made, its score made (once), destroyed (with and without one), updated (local or not, special or not),
// reset, every member function (values given or not).
void TestSkaterScore()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory globals({{0x007CE478, 4}, {0x007CE4FD, 1}});
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CE478) = Addr(kParams + 0x300);
            F<uint32_t>(kParams + 0x300 + 0x20) = Addr(kParams + 0x340);
            GameVariable<uint8_t>(0x007CE4FD) = 0;
            Originals guard(original, {0x00518B60, 0x00518BE0, 0x00518C50, 0x00518660, 0x005186E0, 0x00518630,
                                       0x00518760});
            std::vector<uint32_t> trace;
            F<uint32_t>(kComponent + 0x18) = 0;
            TestLifecycle(original, 0x00518B60, 0x00518BE0, 0x00518C50, kComponent, 0x1C, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x18) = Addr(kScore);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00518BE0))(At(kComponent));
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            F<uint32_t>(kComponent + 0x18) = 0;
            F<uint32_t>(kObject + 0x14) = 0xABCD0000 + seed;
            void* c = At(kComponent);
            g_a.calloc = Addr(kScore + 0x200); // (the score's memory)
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00518660))(c, At(kParams));
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00518660))(c, At(kParams));
            trace.push_back(Rel(F<uint32_t>(kComponent + 0x18)));
            F<uint32_t>(kComponent + 0x18) = Addr(kScore);
            for (int k = 0; k < 8; ++k) {
                F<uint32_t>(kComponent) = reinterpret_cast<uint32_t>(g_vtable);
                F<uint8_t>(kObject + 0xBC) = k & 1 ? 1 : 0;
                F<uint8_t>(kObject + 0x168) = k & 2 ? 1 : 0;
                g_a.alwaysSpecial = (k & 4) != 0;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005186E0))(c);
            }
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00518630))(c);
            Lcg random(seed * 13);
            const uint32_t names[] = {0x3C445A82, 0x3AF0AEDC, 0x2877B61A, 0x5589B902, 0x56645FC6, 0x5E373400,
                                      0x8BF43831, 0x8F2FE3D3, 0xC068EA59, 0xBE3E5463, 0xBC30CA4E, 0xD19BDB50,
                                      0xF4EE54F0, 0xE0112AAB, 0xDF14BD98, 0x12345678};
            for (int k = 0; k < 96; ++k) {
                g_structs.clear();
                void* p = At(kParams);
                if (random.Below(4))
                    SetComponent(p, 0, static_cast<uint32_t>(static_cast<int>(random.Below(2000)) - 500));
                if (random.Below(3))
                    SetComponent(p, 0xE288A7CB, random.Below(100));
                uint32_t trick = random.Below(3);
                if (trick == 1)
                    SetComponent(p, 0x95E16467, 0x00C0B000 + random.Below(4));
                if (trick == 2)
                    SetComponent(p, 0x3EAFA520, reinterpret_cast<uint32_t>(k & 1 ? "Kickflip" : "Heelflip"));
                if (random.Below(2))
                    SetComponent(p, 0xEDF5DB70, static_cast<uint32_t>(static_cast<int>(random.Below(1081)) - 540));
                if (random.Below(2))
                    SetComponent(p, 0xC82CF71B, random.Below(4));
                F<int>(kScore + 0x14) = static_cast<int>(random.Below(3000));
                F<float>(kScore + 0x60) = static_cast<float>(random.Below(40)) / 4.0f;
                F<int>(kScore + 0x20) = static_cast<int>(random.Below(1000));
                F<uint8_t>(kScore + 0x74) = static_cast<uint8_t>(random.Below(2));
                g_a.total = static_cast<int>(random.Below(1500));
                g_a.nonGap = static_cast<int>(random.Below(20));
                g_a.latest = random.Below(2) != 0;
                if (k % 16 == 4 || k % 16 == 11) { // (the pot 252.5: its integer part, not rounded)
                    F<int>(kScore + 0x14) = 101;
                    F<float>(kScore + 0x60) = 2.5f;
                    SetComponent(p, 0, k % 32 < 16 ? 253u : 252u);
                } else if (k % 5 == 2) { // (the boundaries: equal to the last score / the total / the pot's integer part + 1)
                    int pot = static_cast<int>(static_cast<double>(F<int>(kScore + 0x14)) * F<float>(kScore + 0x60));
                    const int values[] = {F<int>(kScore + 0x20), g_a.total, pot + 1, pot};
                    SetComponent(p, 0, static_cast<uint32_t>(values[random.Below(4)]));
                }
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<Cmf>(Code(original, 0x00518760))(
                    c, names[k % 16], p, At(kScript))));
                trace.push_back(F<uint32_t>(kScore + 0x70));
            }
            trace.push_back(GameVariable<uint8_t>(0x007CE4FD));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater score: made, destroyed, updated, reset, every member function");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterRotate, RemoveStandIns);
SELFTEST(Early, TestSkaterRunTimer, RemoveStandIns);
SELFTEST(Early, TestSkaterScore, RemoveStandIns);
