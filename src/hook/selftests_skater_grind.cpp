// Self-tests for the skater core physics' StartGrind (core_grind.cpp): original vs ours on a stand-in component
// with a stand-in rail, every callee stood in for (skater_core_testkit.h); the vector functions the original calls
// pointed at ours (as with the hooks installed).

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/gap.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Skater::CorePhysicsComponent;

constexpr uint32_t kNodes = kFree;               // 6 rail nodes (0x54 bytes each)
constexpr uint32_t kManager = kFree + 0x200;     // the rail manager (+0x40 its nodes)
constexpr uint32_t kPoint = kFree + 0x260;       // the point on the rail
constexpr uint32_t kComponents = kFree + 0x280;  // what GetComponent returns
constexpr uint32_t kOther1520 = kFree + 0x2C0;   // what +0x1520 points at (+0x18)
constexpr uint32_t kScriptMemory = kFree + 0x300; // a new script

std::vector<StandIn> GrindStandIns()
{
    auto vectorOut = [](Frame& f, Result& r) {
        // GetWorldPos: the node's own position (a zero node: the point), recorded.
        uint32_t node = f.args[1];
        const void* from = node ? reinterpret_cast<const void*>(node + 0x20) : ArenaPtr(kPoint);
        std::memcpy(reinterpret_cast<void*>(f.args[0]), from, 16);
        r.eax = f.args[0];
    };
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kComponents)); }},
        {0x00511570, 0, 0, Ret::None, true, 0, {Ours(&Obj::CSkaterFlipAndRotateComponent::DoAfterAnim)}},
        {0x005627B0, 4, 1, Ret::None, true},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) {
             static const float values[] = {0.0f, 30.0f, 45.0f, 60.0f, 80.0f, 89.0f, 2.5f, 0.5f, 12.0f, 6.0f, 0.25f};
             r.st0 = values[Next() % 11];
         }},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}, [](Frame&, Result& r) { r.eax = Next() % 3 == 0; }},
        {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)},
         [](Frame&, Result& r) { r.eax = 0x51000000u; }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             if (f.args[0] == 0xA1DC81F9) {
                 r.eax = 1;
                 *reinterpret_cast<uint32_t*>(f.args[1]) = 0x6000 + Next() % 4;
             } else if (r.eax) {
                 *reinterpret_cast<uint32_t*>(f.args[1]) = 0x9000 + Next() % 4;
             }
         }},
        {0x00522370, 4, 1, Ret::None, true, 0, {Ours(&Obj::CTrickComponent::PassToTrickObjects)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x00434A60, 16, 4, Ret::Bool, true, 0, {Ours(&Obj::CTriggerComponent::TriggerNode)}},
        {0x004FE860, 16, 4, Ret::None, true},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x005196A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CSkaterStateComponent::SetTerrain)}},
        {0x00580790, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledPhysics)}},
        {0x00518CF0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CSkaterSoundComponent::PlayLand)}},
        {0x0043DA70, 0, 1, Ret::Float, false, 0, {Ours(&Obj::CWalkComponent::Param)}, [](Frame&, Result& r) {
             static const float values[] = {0.0f, 1.0f, 3.0f, 10.0f, 30.0f};
             r.st0 = values[Next() % 5];
         }},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x005092E0, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TriggerRailScript)}},
        {0x004FC470, 4, 1, Ret::None, true},
        {0x004F8EE0, 0, 0, Ret::None, true},
        {0x00511F60, 8, 2, Ret::None, true, 0, {Ours(&Obj::CSkaterGapComponent::CheckGaps)}},
        {0x00449970, 0, 1, Ret::Int, false, 0, {Ours(&Script::CScript::operator new)},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kScriptMemory)) : 0; }},
        {0x004738E0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CScript::Construct)},
         [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x004747A0, 12, 3, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CScript::*)(uint32_t, Script::CStruct*, Obj::CObject*)>(&Script::CScript::SetScript))}},
        {0x004750B0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CScript::Update)}},
        {0x0057CB60, 8, 2, Ret::Int, true, 0, {Ours(&Obj::CRailManager::GetWorldPos)}, vectorOut},
        {0x00401C30, 0, 3, Ret::Int, false, 0, {Ours(&Dbg::Printf)}, [](Frame& f, Result&) {
             if (f.args[0] == 0x0064BD6C)
                 for (int i = 3; i < 9; ++i)
                     Record(f.args[i]);
             else
                 Record(f.args[3]);
         }},
        {0x00403DE0, 0, 1, Ret::Int, false, 0, {Ours(&Mth::Rnd)},
         [](Frame&, Result& r) { r.eax = Next() & 1; }},
        {0x00522E80, 16, 4, Ret::Bool, true, 0xF, {Ours(&Obj::CTrickComponent::DoExtraGrindTrick)}},
        {0x0046BA50, 0, 4, Ret::Int, false, 0xF, {Ours(&Obj::DPadDirection)}},
        {0x005004B0, 0x14, 5, Ret::None, true, 0x16, {Ours(&Skater::CorePhysicsComponent::StartGrindTrick)}},
    };
}

UseOurs* GrindVectorFunctions()
{
    return new UseOurs({
        {0x004060F0, Ours(&Vector::RotateToPlane)},
        {0x004023F0, Ours(&Vector::Normalize)},
        {0x00406200, Ours(&Vector::ProjectOnto)},
        {0x004061A0, Ours(&Vector::SetDirection)},
        {0x00402470, Ours(static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator-))},
        {0x00402320, Ours(&Vector::operator=)},
        {0x00404090, Ours(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(&Mth::Matrix::operator=))},
    });
}

// StartGrind 0x0050A770: point rails (spin held or not, the wait over or not), a lip trick's every condition, grinds
// both ways (a tie: the coin), at the rail's ends, from an object or not, the feelers hitting near or far, the acid
// hold, the trick from the skater component or the pad.
void TestStartGrind()
{
    StandIns standIns(GrindStandIns());
    std::unique_ptr<UseOurs> vectors(GrindVectorFunctions());
    ClockOffsetZero clock;
    Vector savedHold = GameVariable<Vector>(0x007CCDBC);
    bool same = true;
    int kicks = 0, spins = 0, lips = 0, grinds = 0, ends = 0, tricks = 0, acid = 0, slow = 0;
    for (int variant = 0; variant < 16000 && same; ++variant) {
        g_rng = 0x2F6B3A1Du * (variant + 5);
        FillArena();
        GameVariable<Vector>(0x007CCDBC) = {RandomFloat(5.0f), RandomFloat(5.0f), RandomFloat(5.0f), RandomFloat(1.0f)};
        // The rail: nodes 0-5 linked in a line (prev / next), or a node on its own.
        uint8_t* nodes = ArenaPtr(kNodes);
        int count = 2 + static_cast<int>(Next() % 4);
        for (int i = 0; i < 6; ++i) {
            uint8_t* n = nodes + i * 0x54;
            *reinterpret_cast<uint8_t**>(n + 0x40) = i + 1 < count ? n + 0x54 : nullptr;
            *reinterpret_cast<uint8_t**>(n + 0x44) = i > 0 && i < count ? n - 0x54 : nullptr;
            *reinterpret_cast<uint32_t*>(n + 0x48) = Next() % 4;
            *reinterpret_cast<uint32_t*>(n + 0x4C) = 0x7000 + i;
            n[0x50] = static_cast<uint8_t>(Next());
        }
        int which = static_cast<int>(Next() % (count - 1)); // (a node with a next)
        bool point = Next() % 4 == 0;
        uint8_t* node = nodes + which * 0x54;
        if (point) {
            node = nodes + 5 * 0x54;
            *reinterpret_cast<uint8_t**>(node + 0x40) = nullptr;
            *reinterpret_cast<uint8_t**>(node + 0x44) = nullptr;
        }
        ArenaAt<uint8_t*>(kManager + 0x40) = nodes - 40 * 0x54; // (an array starting well before: the index shows)
        if (Next() % 4 == 0) // (the point at a node: the rail's end)
            std::memcpy(ArenaPtr(kPoint), (Next() % 2 ? node : nodes + (count - 1) * 0x54) + 0x20, 16);
        if (Next() % 6 == 0) // (the point at the first node)
            std::memcpy(ArenaPtr(kPoint), nodes + 0x20, 16);
        ArenaAt<uint8_t*>(kComponent + 0x22C) = Next() % 3 ? node : nullptr;
        ArenaAt<uint8_t*>(kComponent + 0x1520) = ArenaPtr(kOther1520);
        ArenaAt<uint32_t>(kOther1520 + 0x18) = 0x15200018;
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
        for (uint32_t f : {0x28u, 0x30u, 0x38u, 0x40u, 0x48u, 0x50u, 0x78u, 0x80u, 0xA8u, 0xB0u, 0xE0u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(f == 0xA8 ? 15 : 40);
        ArenaAt<uint8_t>(kFlags + 0xE1) = static_cast<uint8_t>(Next());
        ArenaAt<uint8_t>(kComponent + 0x2ED) = RandomBool(25);
        ArenaAt<uint8_t>(kComponent + 0x375) = RandomBool(20);
        ArenaAt<int>(kComponent + 0x1504) = PickInt();
        ArenaAt<uint32_t>(kTriggerState + 0x1C) = Next() % 2 ? 0 : 0x55;
        uint32_t ride = Next() % 3;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kTracker) = ride == 2 ? 0x0BEEF000 : 0;
        ArenaAt<uint32_t>(kObject + 0x10) = Next() % 2 ? 0 : 0x5C000000;
        for (uint32_t b : {0x20u, 0x40u, 0x60u, 0x80u, 0xA0u, 0x100u})
            ArenaAt<uint8_t>(kInput + b) = RandomBool(30);
        // The velocity: still, barely moving, level, along the facing or not; the lip's conditions often met.
        float* v = &ArenaAt<float>(kObject + 0xAC);
        switch (Next() % 6) {
        case 0:
            v[0] = v[2] = 0.0f;
            break;
        case 1:
            v[0] = 0.005f, v[2] = -0.005f;
            break;
        case 2:
            v[0] = RandomFloat(20.0f), v[2] = RandomFloat(20.0f);
            break;
        default:
            break;
        }
        if (Next() % 3 == 0) {
            v[1] = 5.0f;
            ArenaAt<float>(kObject + 0x80) = 0.1f;  // (up near level)
            ArenaAt<float>(kObject + 0x90) = 0.9f;  // (facing up)
            ArenaAt<float>(kComponent + 0x6C) = 0.05f;
            ArenaAt<float>(kObject + 0x70) = 0.05f;
        }
        if (Next() % 5 == 0) // (moving straight across the rail: a tie)
            std::memset(v, 0, 12);
        auto full = [](float range) {
            return static_cast<float>((static_cast<double>(Next()) / 4294967296.0 - 0.5) * range);
        };
        if (Next() % 3 == 0) // (full-precision values: the roundings show)
            for (uint32_t at : {kPoint, kComponent + 0x120u, kObject + 0xACu, kObject + 0x4Cu})
                for (uint32_t i = 0; i < 16; i += 4)
                    ArenaAt<float>(at + i) = full(Next() % 2 ? 9.0f : 900.0f);
        switch (Next() % 8) { // (values at the limits)
        case 0: // a hit exactly 6 from the point
            std::memcpy(ArenaPtr(kComponent + 0x120), ArenaPtr(kPoint), 16);
            ArenaAt<float>(kComponent + 0x120) += 6.0f;
            break;
        case 1: // level speed exactly 10
            v[0] = 6.0f, v[2] = 8.0f;
            break;
        case 2: // no facing (nothing against it; a still skater stays still)
            std::memset(ArenaPtr(kObject + 0x8C), 0, 16);
            break;
        case 3: // the jump started at the point (straight on)
            std::memcpy(ArenaPtr(kComponent + 0x338), ArenaPtr(kPoint), 16);
            break;
        case 4: // no right (along the rail exactly at the tolerance 0)
            std::memset(ArenaPtr(kObject + 0x6C), 0, 16);
            break;
        default:
            break;
        }
        bool noLip = Next() % 4 == 0;
        bool fromObject = Next() % 3 == 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto p = reinterpret_cast<const Vector*>(ArenaPtr(kPoint));
        auto n = reinterpret_cast<Obj::CRailNode*>(node);
        auto m = reinterpret_cast<Obj::CRailManager*>(ArenaPtr(kManager));
        auto expected = RunOnArena(seed, [&](uint8_t* c) {
            Original<void(__thiscall*)(void*, const Vector*, void*, void*, bool, bool)>(0x0050A770)(c, p, n, m, noLip,
                                                                                                    fromObject);
            return 0u;
        });
        auto actual = RunOnArena(seed, [&](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->StartGrind(p, n, m, noLip, fromObject);
            return 0u;
        });
        kicks += point && Called(actual, 0x00522370);
        spins += point && Called(actual, 0x005092E0);
        lips += Called(actual, 0x004F8EE0);
        bool grind = Called(actual, 0x00522E80);
        grinds += grind;
        tricks += Called(actual, 0x005004B0);
        ends += !point && !grind && !Called(actual, 0x004F8EE0) && Called(actual, 0x0057CB60);
        acid += Called(actual, 0x00401C30);
        slow += fromObject && grind && Called(actual, 0x0043DA70);
        same = Compare(expected, actual, "start grind", variant);
    }
    GameVariable<Vector>(0x007CCDBC) = savedHold;
    bool covered = kicks > 300 && spins > 100 && lips > 300 && grinds > 2000 && tricks > 800 && ends > 300 &&
                   acid > 100 && slow > 200;
    if (!covered)
        Log("  start grind coverage: kicks %d, spins %d, lips %d, grinds %d (tricks from the pad %d), ends %d, acid %d, "
            "slow from an object %d",
            kicks, spins, lips, grinds, tricks, ends, acid, slow);
    Check(same && covered, "skater: StartGrind");
}

} // namespace

SELFTEST(Early, TestStartGrind);
