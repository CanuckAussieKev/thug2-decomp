// Self-tests for the skater's adjust-physics component (adjust_physics.cpp): original vs ours on stand-in components
// (skater_core_testkit.h), every callee into game code stood in for; the feeler on the stack constructed for real.
// Also the bike's branch (THAW 0x005C2980, thaw_bmx_physics.cpp StuckCheckRaise): the feelers raised on the bike only.

#include <cstdint>
#include <cstring>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/shadow_component.h"
#include "game/script/struct.h"
#include "game/skater/adjust_physics.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Obj::CSkaterAdjustPhysicsComponent;

constexpr uint32_t kAdjust = kFree;          // the adjust-physics component (0x34 bytes)
constexpr uint32_t kShadow = kFree + 0x40;   // its shadow component
constexpr uint32_t kPark = kFree + 0x80;     // the park editor (+0x40)
constexpr uint32_t kRidden = kFree + 0x100;  // the object ridden (+0xAC its velocity)

// GetCollision's stand-in: the floor found near the skater or further up (or nothing: its own result).
void FloorStandIn(Frame& f, Result&)
{
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
    for (int i = 0; i < 8; ++i)
        RecordFloat(reinterpret_cast<float*>(&feeler->m_line)[i]);
    Record(feeler->m_hit.ignoreFlags);
    Record(feeler->m_hit.requireFlags);
    const Vector& p = ArenaAt<Vector>(kObject + 0x4C);
    float up = Next() % 2 ? RandomFloat(6.5f) : 6.0f + static_cast<float>(Next() % 40);
    feeler->m_point = {p.x + RandomFloat(1.0f), p.y + up, p.z + RandomFloat(1.0f), p.w};
}

void TestAdjustPhysicsUpdate()
{
    StandIns standIns({
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}, [](Frame& f, Result&) {
             for (int i = 0; i < 8; ++i) // (the core's feeler line: 0x208 start, 0x218 end)
                 RecordFloat(reinterpret_cast<float*>(f.ecx + 0x208)[i]);
         }},
        {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
        {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)}, [](Frame&, Result& r) {
             if (Next() % 2)
                 r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kRidden));
         }},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x004317C0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CShadowComponent::SetPosition)}},
        {0x004317E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CShadowComponent::SetNormal)}},
        {0x004F82F0, 4, 1, Ret::None, true},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, FloorStandIn},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
    });
    void* savedPark = GameVariable<void*>(0x007CE5D8);
    bool same = true;
    int frigs = 0, tries5 = 0, leaves = 0, floors = 0, lips = 0, still = 0;
    for (int variant = 0; variant < 12000 && same; ++variant) {
        g_rng = 0x2545F491u * (variant + 1);
        FillArena();
        GameVariable<void*>(0x007CE5D8) = ArenaPtr(kPark);
        ArenaAt<uint32_t>(kPark + 0x40) = Next() % 3 ? 0x12345678u : 0xD443A2BCu;
        // the adjust component
        ArenaAt<uint8_t*>(kAdjust + 0x0C) = ArenaPtr(kObject);
        ArenaAt<int>(kAdjust + 0x18) = static_cast<int>(Next() % 7);
        ArenaAt<uint8_t>(kAdjust + 0x1C) = RandomBool(50);
        ArenaAt<uint8_t*>(kAdjust + 0x20) = ArenaPtr(kComponent);
        ArenaAt<uint8_t*>(kAdjust + 0x24) = ArenaPtr(kFree + 0x200);
        ArenaAt<uint8_t*>(kAdjust + 0x28) = ArenaPtr(kShadow);
        ArenaAt<uint8_t*>(kAdjust + 0x2C) = ArenaPtr(kRide);
        ArenaAt<uint8_t*>(kAdjust + 0x30) = ArenaPtr(kFlags);
        // the ride: none, a tracker on nothing, or on an object
        uint32_t ride = Next() % 4;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint8_t*>(kTracker) = ride >= 2 ? ArenaPtr(kRidden) : nullptr;
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
        for (uint32_t f : {0x78u, 0x88u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(25);
        // the frame's start: the position itself (not moved), near it, or further
        Vector& pos = ArenaAt<Vector>(kObject + 0x4C);
        Vector& began = ArenaAt<Vector>(kComponent + 0x98);
        switch (Next() % 5) {
        case 0:
            began = pos;
            break;
        case 4: // (only w differs: moved)
            began = pos;
            began.w = pos.w + 1.0f;
            break;
        case 1:
            began = {pos.x + RandomFloat(0.4f), pos.y + RandomFloat(0.4f), pos.z, pos.w};
            break;
        default:
            break;
        }
        // the ground hit just under the position, at it, or over it
        ArenaAt<float>(kComponent + 0x124) = pos.y - (Next() % 3 == 0 ? 0.0005f : RandomFloat(1.0f));
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x004F8420)(c + (kAdjust - kComponent));
            return 0u;
        });
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CSkaterAdjustPhysicsComponent*>(c + (kAdjust - kComponent))->Update();
            return 0u;
        });
        bool checked = Called(actual, 0x005003A0);
        still += !checked;
        if (checked && !Called(actual, 0x004317C0)) {
            ++frigs;
            tries5 += g_arenaStart[kAdjust + 0x18] >= 5;
        }
        leaves += Called(actual, 0x00427C70);
        floors += Called(actual, 0x004172F0);
        lips += Called(actual, 0x004609F0);
        same = Compare(expected, actual, "adjust physics", variant);
    }
    GameVariable<void*>(0x007CE5D8) = savedPark;
    bool covered = frigs > 300 && tries5 > 50 && leaves > 300 && floors > 1000 && lips > 30 && still > 300;
    if (!covered)
        Log("  adjust physics coverage: frigs %d (at 5: %d), rides left %d, floor probes %d, lips %d, not moved %d",
            frigs, tries5, leaves, floors, lips, still);
    Check(same && covered, "skater: SkaterAdjustPhysics::Update");
}

// The bike's branch: off the bike nothing is raised (the original's lines exactly); on it, both feelers by
// Skater_First_Forward_Collision_Height (the physics float's stand-in value).
void TestAdjustPhysicsBike()
{
    float raise = 0.0f;
    StandIns standIns({
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) { r.st0 = 400.0f; }},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}, [](Frame&, Result& r) { r.st0 = 32.1f; }},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)},
         [](Frame&, Result& r) { r.eax = 1; }},
        {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
        {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x004317C0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CShadowComponent::SetPosition)}},
        {0x004317E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CShadowComponent::SetNormal)}},
        {0x004F82F0, 4, 1, Ret::None, true},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result& r) {
             const float* line = reinterpret_cast<const float*>(f.ecx);
             RecordFloat(line[5]); // (the end's y)
             r.eax = 0;
         }},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
    });
    void* savedPark = GameVariable<void*>(0x007CE5D8);
    bool savedEnabled = Skater::ThawBmx::Enabled();
    bool ok = true;
    for (int bike = 0; bike < 2; ++bike) {
        g_rng = 77;
        FillArena();
        GameVariable<void*>(0x007CE5D8) = ArenaPtr(kPark);
        ArenaAt<uint32_t>(kPark + 0x40) = 0;
        ArenaAt<uint8_t*>(kAdjust + 0x0C) = ArenaPtr(kObject);
        ArenaAt<uint8_t*>(kAdjust + 0x20) = ArenaPtr(kComponent);
        ArenaAt<uint8_t*>(kAdjust + 0x28) = ArenaPtr(kShadow);
        ArenaAt<uint8_t*>(kAdjust + 0x2C) = ArenaPtr(kRide);
        ArenaAt<uint8_t*>(kAdjust + 0x30) = ArenaPtr(kFlags);
        ArenaAt<uint8_t*>(kRide + 0x18) = nullptr;
        ArenaAt<int>(kFlags + 0x18) = 0;
        Vector& pos = ArenaAt<Vector>(kObject + 0x4C);
        pos = {10.0f, 20.0f, 30.0f, 1.0f};
        ArenaAt<Vector>(kObject + 0x7C) = {0.0f, 1.0f, 0.0f, 0.0f};
        ArenaAt<Vector>(kComponent + 0x98) = {0.0f, 20.0f, 30.0f, 1.0f};
        ArenaAt<float>(kComponent + 0x124) = 19.0f;
        void* core = ArenaPtr(kComponent);
        Skater::ThawBmx::Enabled() = bike != 0;
        Skater::ThawBmx::StateOf(core, true)->on = bike != 0;
        g_calls.clear();
        reinterpret_cast<CSkaterAdjustPhysicsComponent*>(ArenaPtr(kAdjust))->Update();
        Skater::ThawBmx::Forget(core);
        float startY = ArenaAt<float>(kComponent + 0x20C);
        float endY = 0.0f;
        for (size_t i = 0; i + 1 < g_calls.size(); ++i) {
            if (g_calls[i] == 0x004172F0)
                std::memcpy(&endY, &g_calls[i + 4], 4); // (address, this, two arguments, then the end's y)
        }
        raise = bike ? 32.1f : 0.0f;
        float wantStart = static_cast<float>(static_cast<double>(static_cast<float>(1.0 * 0.01f + 20.0)) + 8.0f) + raise;
        ok = ok && startY == wantStart && endY == 21.0f + raise;
        if (startY != wantStart || endY != 21.0f + raise)
            Log("  adjust physics bike %d: start y %g (want %g), floor probe end y %g (want %g)", bike, startY, wantStart,
                endY, 21.0f + raise);
    }
    Skater::ThawBmx::Enabled() = savedEnabled;
    GameVariable<void*>(0x007CE5D8) = savedPark;
    Check(ok, "skater: SkaterAdjustPhysics on the bike (THAW 0x005C2980): the feelers raised, off the bike not");
}

} // namespace

SELFTEST(Early, TestAdjustPhysicsUpdate);
SELFTEST(Early, TestAdjustPhysicsBike);
