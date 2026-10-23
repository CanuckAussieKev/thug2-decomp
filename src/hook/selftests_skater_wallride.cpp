// Self-tests for the skater core physics' wall ride update (core_wallride.cpp): original vs ours on a stand-in
// component, every callee stood in for (skater_core_testkit.h); the vector functions the original calls pointed at
// ours.

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/trigger_component.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
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

constexpr uint32_t kQueries = kFree; // the SkaterMatrixQueries component (+0x0C its object, +0x18 a matrix)

// The feelers' stand-in: a hit or not, writing a hit normal (facing up, sideways, along the wall) and point.
void FeelStandIn(Frame& f, Result& r)
{
    float* c = reinterpret_cast<float*>(f.ecx);
    for (int i = 0; i < 8; ++i)
        RecordFloat(reinterpret_cast<float*>(f.ecx + 0x208)[i]);
    r.eax = Next() % 3 != 0;
    if (!r.eax)
        return;
    float* normal = c + 0x130 / 4;
    switch (Next() % 6) {
    case 5: // just round a corner (0.01 along the wall (1, 0, 0))
        normal[0] = 0.01f, normal[1] = 0.0f, normal[2] = 0.0f;
        break;
    case 0: // the top: facing up
        normal[0] = 0.1f, normal[1] = 0.95f, normal[2] = 0.0f;
        break;
    case 1: // exactly 0.9 up
        normal[0] = 0.0f, normal[1] = 0.9f, normal[2] = 0.0f;
        break;
    case 2: // the same wall (along it)
        std::memcpy(normal, c + 0x38C / 4, 16);
        break;
    default:
        for (int i = 0; i < 3; ++i)
            normal[i] = RandomFloat(1.0f);
        break;
    }
    normal[3] = RandomFloat(1.0f);
    for (int i = 0; i < 4; ++i)
        c[0x120 / 4 + i] = RandomFloat(5.0f);
    reinterpret_cast<uint8_t*>(f.ecx)[0x22A] = RandomBool(60);
}

void TestDoWallRidePhysics()
{
    StandIns standIns({
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x00416F00, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CRideTracker::Update)}},
        {0x005BFA8C, 12, 3, Ret::Int, false},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) {
             static const float values[] = {0.0f, 0.05f, 1.5f, -30.0f, 6.0f, 200.0f, 0.5f};
             r.st0 = values[Next() % 7];
         }},
        {0x00421440, 8, 2, Ret::None, true, 0, {Ours(&Mth::Vector::RotateAboutAxis)}, [](Frame& f, Result&) {
             float* v = reinterpret_cast<float*>(f.ecx);
             for (int i = 0; i < 4; ++i)
                 RecordFloat(v[i]);
             RecordFloat(*reinterpret_cast<float*>(&f.args[1]));
             v[0] += 0.5f, v[2] -= 0.25f; // (turned)
         }},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}, FeelStandIn},
        {0x004FE860, 16, 4, Ret::None, true},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kQueries)); }},
        {0x0050DAE0, 4, 1, Ret::None, true, 1, {Ours(&CorePhysicsComponent::TryGrind)}, [](Frame&, Result&) {
             if (Next() % 3 == 0)
                 ArenaAt<int>(kFlags + 0x18) = 4; // (onto a rail)
         }},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x00460A20, 12, 3, Ret::None, true, 0, {Ours(&Obj::CObject::BroadcastEvent)}},
        {0x005051B0, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::KeepOffCeilingAtWall)}, [](Frame&, Result& r) { r.eax = Next() % 6 == 0; }},
        {0x00434BF0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CTriggerComponent::CheckFeeler)}},
        {0x005232A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CTrickComponent::SetTrickObjects)}},
        {0x0043A930, 4, 1, Ret::Int, true, 0, {Ours(&Nx::CFeeler::Assign)}, [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x004FFB10, 0, 0, Ret::Bool, true},
        {0x00522370, 4, 1, Ret::None, true, 0, {Ours(&Obj::CTrickComponent::PassToTrickObjects)}},
    });
    UseOurs vectors({
        {0x004060F0, Ours(&Vector::RotateToPlane)},
        {0x00406080, Ours(&Vector::ProjectToPlane)},
        {0x004023F0, Ours(&Vector::Normalize)},
        {0x00402550, Ours(&Mth::CrossProduct)},
        {0x00404090, Ours(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(&Mth::Matrix::operator=))},
        {0x00405170, Ours(&Mth::Matrix::OrthoNormalizeAbout)},
    });
    ClockOffsetZero clock;
    bool same = true;
    int turned = 0, landed = 0, offEdge = 0, jumped = 0, stillOn = 0;
    for (int variant = 0; variant < 16000 && same; ++variant) {
        g_rng = 0x5BE0CD19u * (variant + 1);
        FillArena();
        ArenaAt<int>(kFlags + 0x18) = 2;
        for (uint32_t f : {0x20u, 0x78u, 0x80u, 0xA8u, 0xB0u, 0xD8u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(50);
        ArenaAt<uint8_t>(kInput + 0x1C0) = RandomBool(50);
        ArenaAt<uint8_t>(kTriggerState + 0x26) = RandomBool(15);
        ArenaAt<uint8_t>(kSkater + 0x13B0) = RandomBool(50);
        if (Next() % 2)
            ArenaAt<int>(kComponent + 0xFC) = ArenaAt<int>(kComponent + 0x1AC);
        ArenaAt<uint32_t>(kQueries + 0xC) = reinterpret_cast<uint32_t>(ArenaPtr(kObject));
        uint32_t ride = Next() % 4;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kTracker) = ride >= 2 ? 0x0BEEF000 : 0;
        ArenaAt<uint8_t>(kTracker + 0xAC) = RandomBool(50);
        ArenaAt<float>(kComponent + 0x8C) = Next() % 2 ? 1.0f / 60.0f : RandomFloat(0.5f);
        // The wall: level normals mostly (a vertical wall), sometimes straight up (no level tangent).
        float* wall = &ArenaAt<float>(kComponent + 0x38C);
        switch (Next() % 5) {
        case 0:
            wall[0] = 0.0f, wall[1] = 1.0f, wall[2] = 0.0f;
            break;
        case 1:
            wall[0] = 0.6f, wall[1] = 0.0f, wall[2] = -0.8f;
            break;
        case 2: // (up it: straight down)
            wall[0] = 1.0f, wall[1] = 0.0f, wall[2] = 0.0f;
            if (Next() % 2) { // heading up it exactly the limit
                float* facing = &ArenaAt<float>(kObject + 0x8C);
                facing[0] = 0.0f, facing[1] = -0.68f, facing[2] = 0.0f;
            }
            break;
        default:
            break;
        }
        // The facing: up the wall (no turn), across it, down it.
        if (Next() % 3 == 0) {
            float* facing = &ArenaAt<float>(kObject + 0x8C);
            facing[0] = 0.0f, facing[1] = Next() % 2 ? 1.0f : -1.0f, facing[2] = 0.0f;
        }
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x0050F830)(c);
            return 0u;
        });
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoWallRidePhysics();
            return 0u;
        });
        turned += Called(actual, 0x00421440);
        landed += Called(actual, 0x0050DAE0);
        offEdge += Called(actual, 0x00460A20);
        for (size_t i = 1; i + 2 < actual.size() - kArenaSize / 4; ++i)
            if (actual[i] == 0x004FFB10 && actual[i + 2] == 1)
                ++jumped;
        stillOn += Called(actual, 0x0043A930);
        same = Compare(expected, actual, "wall ride physics", variant);
    }
    bool covered = turned > 1000 && landed > 300 && offEdge > 1000 && jumped > 300 && stillOn > 500;
    if (!covered)
        Log("  wall ride coverage: turned %d, landed %d, off the edge %d, jumped %d, still on %d", turned, landed,
            offEdge, jumped, stillOn);
    Check(same && covered, "skater: DoWallRidePhysics");
}

} // namespace

SELFTEST(Early, TestDoWallRidePhysics);
