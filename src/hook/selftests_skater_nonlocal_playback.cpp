// Self-tests for the non-local skater's playback (src/game/skater/nonlocal_playback.cpp): the original and ours on
// the same stand-in skater, history and rails, every call into other game code recorded - interpolation, the
// extrapolation on the ground and along rails, the history's messages.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/model_component.h"
#include "game/obj/rail_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/nonlocal_net_logic.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/special_item.h"
#include "game/nx/feeler.h"
#include "game/sys/game_clock.h"
#include "game/gfx/anim_blend.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CSkaterNonLocalNetLogicComponent;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0400, kState = 0x0600, kHistory = 0x0800, kModel = 0x2000,
                   kEndRun = 0x2400, kBlender = 0x2500, kFlip = 0x2600, kApp = 0x2700, kPlayer = 0x2800,
                   kLocal = 0x2900, kLocalSkater = 0x2A00, kSkate = 0x2B00, kRails = 0x2C00, kNodes = 0x2D00,
                   kItems = 0x3000, kItem = 0x3100, kItemComponent = 0x3200, kOthers = 0x3300, kHeap = 0x4000,
                   kConn = 0x8000;
constexpr uint32_t kNodeSize = 0x60;
constexpr int kNodeCount = 8;

struct Answers {
    uint32_t calloc, heap;
    uint32_t bits; // used up one bit a call by the yes / no stand-ins
    bool noApp, onServer, localConn, noLocal, hidden;
    float physics, cornerAngle;
    int found;   // MoveAlongRail's stand-in: which call returns 0 (0: none)
    int calls;
    uint32_t clock;
};
Answers g_a;
void* g_vtable[16];

bool NextBit()
{
    bool bit = (g_a.bits & 1) != 0;
    g_a.bits = (g_a.bits >> 1) | (g_a.bits << 31);
    return bit;
}

void RecordVector(const void* v)
{
    const auto* w = static_cast<const uint32_t*>(v);
    Record({w[0], w[1], w[2], w[3]});
}

uint32_t Allocate(uint32_t size)
{
    uint32_t at = g_a.heap;
    g_a.heap += (size + 15) & ~15u;
    std::memset(At(at), 0xCD, size);
    return Addr(at);
}

// Every call into the rest of the game recorded and answered from the test's answers.
void StandIn(StandIns& s, bool railMoves)
{
    StandInComponentBase(s, &g_a.calloc);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        switch (r.args[0]) {
        case 0xF27A294D: return Addr(kItems);
        case 0x5CB5CB7E: return Addr(kItemComponent);
        default: return Addr(kOthers) + (r.args[0] & 0xF0);
        }
    });
    Rec(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, 0xECE0, [](Regs&) { return 0x1000u + g_a.clock++; },
        false);
    Rec(s, {0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 1, 0x2EA0, [](Regs&) { return g_a.noApp ? 0u : Addr(kApp); });
    Rec(s, {0x00533F90, AddressOf(&GameNet::Manager::OnServer)}, 0, 0x3F90, [](Regs&) { return g_a.onServer ? 0xABCD01u : 0xABCD00u; });
    // (the object id: only its low 16 bits are read - the original passes the skater's whole id)
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        g_trace.insert(g_trace.end(), {0xAEA0u, Rel(r.ecx), r.args[0] & 0xFFFF});
        return Addr(kPlayer);
    });
    Rec(s, {0x00543380, AddressOf(&GameNet::PlayerInfo::IsFullyIn)}, 0, 0x3380, [](Regs&) { return NextBit() ? 1u : 0u; });
    Rec(s, {0x005439D0, AddressOf(&GameNet::PlayerInfo::MarkAsFullyIn)}, 0, 0x39D0);
    Rec(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_a.noLocal ? 0u : Addr(kLocal); });
    Rec(s, {0x00455A20, AddressOf(&Net::Conn::IsLocal)}, 0, 0x5A20, [](Regs&) { return g_a.localConn ? 1u : 0u; });
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) {
        const auto* search = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x8BE0, Rel(r.ecx), search[0], search[1]});
        return Addr(kConn);
    });
    // The skater's own functions.
    Rec(s, {0x0045E4C0, AddressOf(&Obj::CCompositeObject::SetTeleported)}, 1, 0xE4C0);
    Rec(s, {0x00580050, AddressOf(&Obj::CSkater::GetFlag2C4)}, 0, 0x0050, [](Regs&) { return g_a.hidden ? 1u : 0u; });
    Rec(s, {0x00581550, AddressOf(&Obj::CSkater::ShowSkater)}, 0, 0x1550);
    Rec(s, {0x005814D0, AddressOf(&Obj::CSkater::Reset27C)}, 0, 0x14D0);
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        5, 0x5790, nullptr, false, true);
    Rec(s, {0x00514610, AddressOf(&CSkaterNonLocalNetLogicComponent::UpdateGroundLighting)}, 0, 0x4610);
    s.At({0x00514E20, AddressOf(&CSkaterNonLocalNetLogicComponent::BounceOffWall)}, 8, [](Regs& r) {
        Record({0x4E20, Rel(r.ecx)});
        RecordVector(reinterpret_cast<const void*>(r.args[0]));
        RecordVector(reinterpret_cast<const void*>(r.args[1]));
        return 0u;
    });
    Rec(s, {0x00519660, AddressOf(&Obj::CSkaterStateComponent::SetState)}, 1, 0x9660);
    Rec(s, {0x00519680, AddressOf(&Obj::CSkaterStateComponent::SetPhysicsMode)}, 1, 0x9680);
    Rec(s, {0x005196A0, AddressOf(&Obj::CSkaterStateComponent::SetTerrain)}, 1, 0x96A0);
    RecFloat(s, {0x00580400, AddressOf(&Obj::GetPhysicsFloat)}, 3, 0x0400,
             [](Regs& r) { return r.args[0] == 0x76C1DA15 ? g_a.cornerAngle : g_a.physics; }, false, true);
    // The feeler's query: the line and the filters recorded, a hit made up.
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        Record({0x4172, r.args[0] & 0xFF, r.args[1] & 0xFF, feeler->m_hit.ignoreFlags, feeler->m_hit.requireFlags});
        RecordVector(&feeler->m_line.start);
        RecordVector(&feeler->m_line.end);
        bool hit = NextBit();
        if (hit) {
            feeler->m_point = {1.5f, -2.25f, 3.0f, 1.0f};
            feeler->m_normal = {0.6f, 0.0f, -0.8f, 0.0f};
        }
        return static_cast<uint32_t>(hit);
    });
    // The rails: a node's world position its own, RailClear by the pattern.
    s.At({0x0057CB60, AddressOf(&Obj::CRailManager::GetWorldPos)}, 8, [](Regs& r) {
        Record({0xCB60, Rel(r.ecx), Rel(r.args[1])});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), reinterpret_cast<const uint8_t*>(r.args[1]) + 0x20, 16);
        return r.args[0];
    });
    s.At({0x0057C600, AddressOf(&Obj::RailClear)}, 0, [](Regs& r) {
        Record({0xC600, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7]});
        return NextBit() ? 1u : 0u;
    });
    s.At({0x0057CBD0, AddressOf(&Obj::CRailManager::FindNode)}, 4, [](Regs& r) {
        Record({0xCBD0, Rel(r.ecx), r.args[0]});
        return r.args[0] % 3 == 0 ? 0u : Addr(kNodes + (r.args[0] % kNodeCount) * kNodeSize);
    });
    if (railMoves) {
        s.At({0x00516640, AddressOf(&CSkaterNonLocalNetLogicComponent::MoveAlongRail)}, 8, [](Regs& r) {
            Record({0x6640, Rel(r.ecx), Rel(r.args[0]), r.args[1]});
            if (++g_a.calls == g_a.found)
                return 0u;
            F<float>(kObject + 0x4C) += 1.0f;
            return Addr(kNodes + (g_a.calls % kNodeCount) * kNodeSize);
        });
        Rec(s, {0x00515360, AddressOf(&CSkaterNonLocalNetLogicComponent::Extrapolate)}, 0, 0x5360);
    }
    // The history's messages' calls.
    s.At({0x00419D40, AddressOf(&Obj::CAnimBlenderComponent::FindChannel)}, 4, [](Regs& r) {
        Record({0x9D40, Rel(r.ecx), r.args[0]});
        return NextBit() ? 0x0C4A0000u : 0u;
    });
    Rec(s, {0x0047F260, AddressOf(&Gfx::CBlendChannel::PlayAnim)}, 8, 0xF260);
    Rec(s, {0x0047E280, AddressOf(&Gfx::CBlendChannel::SetWobbleTarget)}, 2, 0xE280);
    Rec(s, {0x0047DC40, AddressOf(&Gfx::CBlendChannel::SetFlipped)}, 4, 0xDC40, [](Regs&) { return 1u; });
    s.At({0x0047E450, AddressOf(&Gfx::CBlendChannel::SetWobbleDetails)}, 8, [](Regs& r) {
        Record({0xE450, r.ecx, r.args[1]});
        const auto* data = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({data[0], data[1], data[2], data[3], data[4]});
        return 0u;
    });
    Rec(s, {0x0047E740, AddressOf(&Gfx::CBlendChannel::SetLoopType)}, 2, 0xE740);
    Rec(s, {0x0047E850, AddressOf(&Gfx::CBlendChannel::SetSpeed)}, 4, 0xE850);
    Rec(s, {0x00511390, AddressOf(&Obj::CSkaterFlipAndRotateComponent::SetBoardRotated)}, 4, 0x1390,
        [](Regs& r) { return r.args[1] & 0xFF; });
    // (their bool arguments - HideGeom's hide, Add's hold - as truth values: the original passes the message's byte,
    // ours that byte != 0)
    s.At({0x004202A0, AddressOf(&Obj::CModelComponent::HideGeom)}, 12, [](Regs& r) {
        g_trace.insert(g_trace.end(), {0x02A0u, Rel(r.ecx), r.args[0], (r.args[1] & 0xFF) != 0 ? 1u : 0u, r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x00420520, AddressOf(&Obj::CModelSpinAxis::Add)}, 28, [](Regs& r) {
        g_trace.insert(g_trace.end(), {0x0520u, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4],
                                       (r.args[5] & 0xFF) != 0 ? 1u : 0u, r.args[6] & 0xFF});
        return 0u;
    });
    Rec(s, {0x004204F0, AddressOf(&Obj::CModelSpinAxis::Clear)}, 0, 0x04F0);
    Rec(s, {0x00479020, AddressOf(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 1,
        0x9020, [](Regs& r) { return 0x5700000u + (r.args[0] & 0xFFFF); }, false, true);
    Rec(s, {0x0051BF80, AddressOf(&Obj::CSpecialItemComponent::CreateItem)}, 2, 0xBF80,
        [](Regs&) { return Addr(kItem); });
    Rec(s, {0x0051BE00, AddressOf(&Obj::CSpecialItemComponent::DestroyItem)}, 1, 0xBE00);
    Rec(s, {0x00409030, AddressOf(&Script::NewStruct)}, 0, 0x9030, [](Regs&) { return Allocate(0x20); }, false, true);
    Rec(s, {0x005BA3D0}, 0, 0x9030, [](Regs&) { return Allocate(0x20); }, false);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0x6410, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(
                            &Script::CStruct::AddChecksum))},
        2, 0x7D40);
    Rec(s, {0x00477090, 0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0x7130);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1, 0x9070, nullptr, false, true);
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
    // The maths the original reaches where ours runs our own (tested on its own; not bit-identical to the x87
    // originals): the original pointed at ours.
    const std::pair<uint32_t, uint32_t> ours[] = {
        {0x00405990, AddressOf(&Mth::SlerpInterpolator::SetMatrices)},
        {0x00405A20, AddressOf(&Mth::SlerpInterpolator::GetMatrix)},
        {0x004023F0, AddressOf(&Mth::Vector::Normalize)},
        {0x004061A0, AddressOf(&Mth::Vector::SetDirection)},
        {0x00406200, AddressOf(&Mth::Vector::ProjectOnto)},
    };
    for (auto [game, mine] : ours)
        s.Bytes(game, Branch(0xE9, game, reinterpret_cast<void*>(mine)));
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_a.heap = kHeap;
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kComponent + 0x2A4) = Addr(kHistory);
    F<uint32_t>(kComponent + 0x2A8) = Addr(kBlender);
    F<uint32_t>(kComponent + 0x2AC) = Addr(kModel);
    F<uint32_t>(kComponent + 0x2B0) = Addr(kState);
    F<uint32_t>(kComponent + 0x2B4) = Addr(kEndRun);
    F<uint32_t>(kComponent + 0x2B8) = Addr(kFlip);
    F<uint32_t>(kObject + 0x14) = 0x0B1E0000 + seed;
    F<uint32_t>(kItemComponent) = reinterpret_cast<uint32_t>(g_vtable);
    F<uint32_t>(kPlayer + 0x18) = Addr(kConn);
    F<uint32_t>(kLocal + 0x14) = Addr(kLocalSkater);
    F<uint32_t>(kSkate + 0x24) = Addr(kRails);
    F<int>(kConn + 0x1CA4) = 40 + static_cast<int>(seed);
}

// Small integers as floats (a few bits: sums stay exact-ish but varied).
float Small(Lcg& random, int range) { return static_cast<float>(static_cast<int>(random.Below(2 * range + 1)) - range) / 4.0f; }

// What the playback changes: the component's fields, the object's position, matrices and velocity, the state's.
void RecordAll(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0x18; i < 0x2A4; i += 4)
        trace.push_back(Rel(F<uint32_t>(kComponent + i)));
    for (uint32_t i = 0x4C; i < 0x100; i += 4)
        trace.push_back(Rel(F<uint32_t>(kObject + i)));
    for (uint32_t i = 0x18; i < 0x120; i += 4)
        trace.push_back(Rel(F<uint32_t>(kState + i)));
    trace.push_back(F<uint32_t>(kEndRun + 0x18));
}

// A history of 20 states, a frame apart from `start` (some times out of step), each with a matrix, a position near
// the last (now and then far), and its flags.
void FillHistory(Lcg& random, uint32_t start, int count)
{
    F<int>(kHistory + 0x18) = count;
    uint32_t time = start;
    float x = Small(random, 400), y = Small(random, 40), z = Small(random, 400);
    for (int i = 0; i < 20; ++i) {
        uint8_t* frame = At(kHistory + 0x1C + i * 0x84);
        auto* m = reinterpret_cast<float*>(frame + 8);
        float angle = static_cast<float>(random.Below(64)) / 32.0f * (random.Below(3) ? 0.1f : 1.0f);
        float c = std::cos(angle), sn = std::sin(angle);
        const float rows[16] = {c, 0, -sn, 0, 0, 1, 0, 0, sn, 0, c, 0, x, y, z, 1};
        std::memcpy(m, rows, sizeof(rows));
        x += Small(random, 40) * (random.Below(12) == 0 ? 100.0f : 1.0f);
        y += Small(random, 8);
        z += Small(random, 40);
        const float pos[4] = {x, y, z, 1.0f};
        std::memcpy(frame + 0x48, pos, 16);
        *reinterpret_cast<uint32_t*>(frame + 0x68) = random.Next();
        *reinterpret_cast<uint32_t*>(frame + 0x6C) = random.Below(64);
        *reinterpret_cast<int*>(frame + 0x70) = static_cast<int>(random.Below(6));
        frame[0x74] = static_cast<uint8_t>(random.Below(3));
        frame[0x75] = static_cast<uint8_t>(random.Below(3));
        frame[0x76] = static_cast<uint8_t>(random.Below(3));
        time += 16 + random.Below(3) - (random.Below(10) == 0 ? 30u : 0u);
        *reinterpret_cast<uint16_t*>(frame + 0x78) = static_cast<uint16_t>(time);
        *reinterpret_cast<uint16_t*>(frame + 0x7A) = static_cast<uint16_t>(time >> 16);
        frame[0x7C] = static_cast<uint8_t>(random.Below(20));
        *reinterpret_cast<uint32_t*>(frame + 0x80) = random.Next();
    }
}

// Interpolate 0x00515940: no client app, a short history, the first sync (hidden or not), then the playback in
// every case: ahead of the history (by little or much), behind by ten or more, at the newest, past every state.
void TestSkaterNonLocalInterpolate()
{
    StandIns& s = *new StandIns();
    StandIn(s, false);
    GameMemory globals({{0x007CCE14, 4}, {0x007CCDE4, 4}, {0x00696D60, 4}});
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000;
            GameVariable<int>(0x00696D60) = 0;
            GameVariable<int>(0x007CCDE4) = 0;
            Originals guard(original, {0x00515940});
            std::vector<uint32_t> trace;
            void* c = At(kComponent);
            Lcg random(seed * 6151);
            for (int k = 0; k < 64; ++k) {
                g_a.bits = random.Next();
                g_a.noApp = k % 23 == 7;
                g_a.hidden = random.Below(3) == 0;
                uint32_t start = 0x10000 + random.Below(0x10000);
                FillHistory(random, start, k % 19 == 3 ? 12 : 20 + static_cast<int>(random.Below(50)));
                F<uint32_t>(kApp + 8) = 5000 + random.Below(400);
                F<float>(kComponent + 0x29C) = 1.0f / 60.0f * static_cast<float>(1 + random.Below(2));
                F<int>(kComponent + 0x298) = static_cast<int>(random.Below(30));
                for (int i = 0; i < 20; ++i)
                    for (int j = 0; j < 4; ++j)
                        F<float>(kComponent + 0x158 + i * 16 + j * 4) = Small(random, 20);
                if (k % 8 == 0) {
                    F<uint32_t>(kComponent + 0x18) = 0; // (the first playback)
                } else {
                    F<uint32_t>(kComponent + 0x18) = 4000 + random.Below(400);
                    // The playback time: within the history, past its newest state (by a little, by a lot), before it.
                    int newest = (F<int>(kHistory + 0x18) - 1) % 20;
                    uint8_t* frame = At(kHistory + 0x1C + newest * 0x84);
                    uint32_t last = *reinterpret_cast<uint16_t*>(frame + 0x78) |
                                    (static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(frame + 0x7A)) << 16);
                    uint32_t now = F<uint32_t>(kApp + 8) - F<uint32_t>(kComponent + 0x18);
                    int ahead[] = {-330, -200, -176, -168, -160, -150, -40, -10, -3, 0, 5, 30, 400, 999, 2000};
                    F<int>(kComponent + 0x1C) = static_cast<int>(last - now) + ahead[random.Below(15)];
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00515940))(c);
                RecordAll(trace);
                trace.push_back(GameVariable<uint32_t>(0x007CCDE4));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local playback: interpolate");
    }
    delete &s;
}

// Extrapolate 0x00515360: on the server or not, a local connection or not, this machine's skater or not, no frame
// time, a move or none; the wall check near and far, hit or not.
void TestSkaterNonLocalExtrapolate()
{
    StandIns& s = *new StandIns();
    StandIn(s, false);
    GameMemory globals({{0x007CCE14, 4}});
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000;
            Originals guard(original, {0x00515360});
            std::vector<uint32_t> trace;
            void* c = At(kComponent);
            Lcg random(seed * 7417);
            for (int k = 0; k < 96; ++k) {
                g_a.bits = random.Next();
                g_a.onServer = random.Below(2) == 0;
                g_a.localConn = random.Below(5) == 0;
                g_a.noLocal = random.Below(4) == 0;
                F<uint32_t>(kLocal + 0x14) = random.Below(5) == 0 ? 0u : Addr(kLocalSkater);
                g_a.physics = static_cast<float>(random.Below(40)) / 2.0f;
                F<int>(kConn + 0x1CA4) = static_cast<int>(random.Below(300));
                const float frames[] = {0.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.05f, 0.0004f};
                F<float>(kComponent + 0x29C) = frames[random.Below(5)];
                F<int>(kComponent + 0x298) = k % 11 == 0 ? 0 : static_cast<int>(random.Below(60));
                for (int i = 0; i < 4; ++i) {
                    F<float>(kComponent + 0x28 + 4 * i) = Small(random, 400);
                    F<float>(kComponent + 0x38 + 4 * i) = F<float>(kComponent + 0x28 + 4 * i) -
                                                          (k % 9 == 4 ? 0.0f : Small(random, 60));
                    F<float>(kObject + 0x4C + 4 * i) = F<float>(kComponent + 0x28 + 4 * i) + Small(random, 20);
                    F<float>(kObject + 0x7C + 4 * i) = Small(random, 4);
                    F<float>(kLocalSkater + 0xAC + 4 * i) = Small(random, 40);
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00515360))(c);
                RecordAll(trace);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local playback: extrapolate");
    }
    delete &s;
}

// A rail of kNodeCount nodes (some inactive, the last without a next), bending now and then.
void FillRail(Lcg& random)
{
    float x = Small(random, 200), y = Small(random, 40), z = Small(random, 200);
    for (int i = 0; i < kNodeCount; ++i) {
        uint32_t node = kNodes + i * kNodeSize;
        F<float>(node + 0x20) = x;
        F<float>(node + 0x24) = y;
        F<float>(node + 0x28) = z;
        F<float>(node + 0x2C) = 1.0f;
        x += 20.0f + Small(random, 40);
        y += Small(random, 120);
        z += random.Below(3) == 0 ? Small(random, 200) : Small(random, 8);
        F<uint32_t>(node + 0x40) = i + 1 < kNodeCount ? Addr(node + kNodeSize) : 0u;
        F<uint32_t>(node + 0x44) = i > 0 ? Addr(node - kNodeSize) : 0u;
        F<uint32_t>(node + 0x48) = random.Below(6) == 0 ? 0x10u : 0x14u;
    }
}

// MoveAlongRail 0x00516640: each way along, before / past either end, the next segment there or not (inactive),
// clear or not, round a sharp corner or not; and ExtrapolateOnRail 0x00516E00 over it (its moves stood in).
void TestSkaterNonLocalRail()
{
    {
        StandIns& s = *new StandIns();
        StandIn(s, false);
        GameMemory globals({{0x007CE478, 4}, {0x007CE5D8, 4}});
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            auto run = [&](bool original) {
                g_trace.clear();
                ResetWorld(seed);
                GameVariable<uint32_t>(0x007CE478) = Addr(kSkate);
                Originals guard(original, {0x00516640});
                std::vector<uint32_t> trace;
                void* c = At(kComponent);
                Lcg random(seed * 3571);
                for (int k = 0; k < 128; ++k) {
                    g_a.bits = random.Next();
                    g_a.physics = Small(random, 40);
                    g_a.cornerAngle = static_cast<float>(random.Below(90));
                    FillRail(random);
                    int index = static_cast<int>(random.Below(kNodeCount));
                    uint32_t node = kNodes + index * kNodeSize;
                    // On the segment, before its start or past its end (along it, a little off it).
                    float along = static_cast<float>(random.Below(15)) / 10.0f - 0.2f;
                    uint32_t next = index + 1 < kNodeCount ? node + kNodeSize : node;
                    for (int i = 0; i < 3; ++i)
                        F<float>(kObject + 0x4C + 4 * i) =
                            F<float>(node + 0x20 + 4 * i) +
                            (F<float>(next + 0x20 + 4 * i) - F<float>(node + 0x20 + 4 * i)) * along + Small(random, 2);
                    F<float>(kObject + 0x58) = 1.0f;
                    for (int i = 0; i < 4; ++i)
                        F<float>(kObject + 0xAC + 4 * i) = Small(random, k % 2 ? 200 : 6);
                    float seconds = static_cast<float>(1 + random.Below(4)) / 60.0f;
                    uint32_t result = reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, float)>(
                        Code(original, 0x00516640))(c, Addr(node), seconds);
                    trace.push_back(Rel(result));
                    trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                    g_trace.clear();
                    RecordAll(trace);
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                DumpArena(trace);
                return trace;
            };
            std::vector<uint32_t> expected = run(true);
            std::vector<uint32_t> actual = run(false);
            Compare(expected, actual, "skater non-local playback: along a rail");
        }
        delete &s;
    }
    StandIns& s = *new StandIns();
    StandIn(s, true);
    GameMemory globals({{0x007CCE14, 4}, {0x007CE478, 4}});
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000;
            GameVariable<uint32_t>(0x007CE478) = Addr(kSkate);
            Originals guard(original, {0x00516E00});
            std::vector<uint32_t> trace;
            void* c = At(kComponent);
            Lcg random(seed * 2903);
            for (int k = 0; k < 96; ++k) {
                g_a.bits = random.Next();
                g_a.onServer = random.Below(2) == 0;
                g_a.localConn = random.Below(5) == 0;
                g_a.noLocal = random.Below(4) == 0;
                g_a.calls = 0;
                g_a.found = static_cast<int>(random.Below(5));
                F<int>(kConn + 0x1CA4) = static_cast<int>(random.Below(300));
                const float frames[] = {0.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.05f, 0.25f, 0.75f, 1.5f};
                F<float>(kComponent + 0x29C) = frames[random.Below(7)];
                if (random.Below(2)) // (a latency of a frame or a little more: a lag of a few frames)
                    F<int>(kConn + 0x1CA4) = static_cast<int>(F<float>(kComponent + 0x29C) * 1000.0f * 2.0f) +
                                             static_cast<int>(random.Below(80));
                F<int>(kComponent + 0x298) = static_cast<int>(random.Below(60));
                F<int>(kComponent + 0x24) = static_cast<int>(random.Below(20));
                for (int i = 0; i < 20; ++i)
                    F<uint32_t>(kHistory + 0x1C + i * 0x84 + 0x80) = random.Next();
                for (int i = 0; i < 20; ++i)
                    F<float>(kComponent + 0x108 + 4 * i) = static_cast<float>(random.Below(9)) / 8.0f;
                for (int i = 0; i < 20; ++i)
                    F<float>(kComponent + 0xB8 + 4 * i) = static_cast<float>(random.Below(40)) / 8.0f;
                for (int i = 0; i < 4; ++i) {
                    F<float>(kComponent + 0x28 + 4 * i) = Small(random, 400);
                    F<float>(kComponent + 0x38 + 4 * i) = F<float>(kComponent + 0x28 + 4 * i) - Small(random, 60);
                    F<float>(kLocalSkater + 0xAC + 4 * i) = Small(random, 40);
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00516E00))(c);
                RecordAll(trace);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local playback: extrapolate on a rail");
    }
    delete &s;
}

// PlayHistoryEvents 0x00516180: every message kind (and others), before / at / after the window, no app, a short
// history, the channel missing or not.
void TestSkaterNonLocalHistoryEvents()
{
    StandIns& s = *new StandIns();
    StandIn(s, false);
    GameMemory globals({{0x007CCE14, 4}, {0x00696D60, 4}});
    const uint8_t kinds[] = {0x29, 0x2A, 0x2B, 0x2C, 0x3C, 0x3D, 0x3E, 0x41, 0x76, 0x77, 0x78, 0x8C, 0x30, 0x75};
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000;
            GameVariable<int>(0x00696D60) = 0;
            Originals guard(original, {0x00516180});
            std::vector<uint32_t> trace;
            void* c = At(kComponent);
            Lcg random(seed * 1609);
            for (int k = 0; k < 48; ++k) {
                g_a.bits = random.Next();
                g_a.noApp = k % 17 == 5;
                F<int>(kHistory + 0x18) = k % 13 == 4 ? 7 : 20 + static_cast<int>(random.Below(9));
                F<int>(kHistory + 0xA6C) = static_cast<int>(random.Below(100));
                F<uint32_t>(kApp + 8) = 9000 + random.Below(100);
                F<uint32_t>(kComponent + 0x18) = 3000;
                F<int>(kComponent + 0x1C) = static_cast<int>(random.Below(200));
                uint32_t now = F<uint32_t>(kApp + 8) - 3000 + F<uint32_t>(kComponent + 0x1C);
                F<uint32_t>(kComponent + 0x20) = now - 20 - random.Below(40);
                for (int i = 0; i < 20; ++i) {
                    uint8_t* msg = At(kHistory + 0xA70 + i * 0x54);
                    msg[0] = kinds[random.Below(sizeof(kinds))];
                    uint32_t time = now - 70 + random.Below(90);
                    if (random.Below(8) == 0)
                        time = F<uint32_t>(kComponent + 0x20); // (exactly the last played)
                    if (random.Below(8) == 0)
                        time = now;
                    *reinterpret_cast<uint16_t*>(msg + 4) = static_cast<uint16_t>(time);
                    *reinterpret_cast<uint16_t*>(msg + 6) = static_cast<uint16_t>(time >> 16);
                    for (uint32_t j = 8; j < 0x54; j += 4)
                        *reinterpret_cast<uint32_t*>(msg + j) = random.Next();
                    msg[1] = static_cast<uint8_t>(random.Next());
                    msg[2] = static_cast<uint8_t>(random.Next());
                    msg[3] = static_cast<uint8_t>(random.Below(8));
                    *reinterpret_cast<int*>(msg + 0x3C) = static_cast<int>(random.Below(2000)) - 1000;
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00516180))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x20));
                for (uint32_t i = 0x28; i < 0x38; i += 4)
                    trace.push_back(F<uint32_t>(kModel + i));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local playback: the history's messages");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterNonLocalInterpolate, RemoveStandIns);
SELFTEST(Early, TestSkaterNonLocalExtrapolate, RemoveStandIns);
SELFTEST(Early, TestSkaterNonLocalRail, RemoveStandIns);
SELFTEST(Early, TestSkaterNonLocalHistoryEvents, RemoveStandIns);
