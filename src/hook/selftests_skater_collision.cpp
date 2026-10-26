// Self-tests for the skater core physics' wall checks (core_collision.cpp): original vs ours on a stand-in component,
// every callee stood in for (skater_core_testkit.h); the vector functions the original calls pointed at ours (as
// with the hooks installed).

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
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

constexpr uint32_t kFrom = kFree;             // the last position (a vector)
constexpr uint32_t kHitObject = kFree + 0x40; // an object a feeler hit (+0xAC its velocity)

// The vector functions the original calls, pointed at ours.
UseOurs* OurVectorFunctions()
{
    return new UseOurs({
        {0x00406080, Ours(&Vector::ProjectToPlane)},
        {0x004060F0, Ours(&Vector::RotateToPlane)},
        {0x004023F0, Ours(&Vector::Normalize)},
        {0x00402550, Ours(&Mth::CrossProduct)},
        {0x004024A0, Ours(static_cast<Vector (*)(const Vector&, float)>(&Mth::operator*))},
        {0x004024D0, Ours(static_cast<Vector (*)(float, const Vector&)>(&Mth::operator*))},
        {0x00402440, Ours(static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator+))},
        {0x00402470, Ours(static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator-))},
        {0x00402340, Ours(&Vector::operator+=)},
        {0x00406050, Ours(&Vector::operator-=)},
        {0x00402320, Ours(&Vector::operator=)},
        {0x0040AF20, Ours(static_cast<Vector (*)(const Vector&, float)>(&Mth::operator/))},
    });
}

void RecordVector(uint32_t address)
{
    const float* v = reinterpret_cast<const float*>(address);
    for (int i = 0; i < 4; ++i)
        RecordFloat(v[i]);
}

// The stand-ins both functions meet.
std::vector<StandIn> CollisionStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}, [](Frame&, Result& r) {
             static const float values[] = {0.0f, 1.0f, 2.5f, 3.0f, 4.0f, 6.0f, 9.0f, 12.0f, 0.5f};
             r.st0 = values[Next() % 9];
         }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) {
             static const float values[] = {0.0f, 1.0f, 2.5f, 6.0f, 12.0f, 0.5f, 30.0f};
             r.st0 = values[Next() % 7];
         }},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}, [](Frame&, Result& r) { r.eax = Next() % 5 != 0; }},
        {0x004F8200, 4, 1, Ret::Int, true, 0, {}, [](Frame& f, Result& r) {
             std::memcpy(reinterpret_cast<void*>(f.ecx), reinterpret_cast<void*>(f.args[0]), 0xB0);
             r.eax = f.ecx;
         }},
        {0x0043A930, 4, 1, Ret::Int, true, 0, {Ours(&Nx::CFeeler::Assign)}, [](Frame& f, Result& r) {
             std::memcpy(reinterpret_cast<void*>(f.ecx), reinterpret_cast<void*>(f.args[0]), 0xB0);
             r.eax = f.ecx;
         }},
        {0x004FFDB0, 4, 1, Ret::Bool, true},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x004172F0, 8, 2, Ret::Int, true, 3, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result& r) {
             const float* line = reinterpret_cast<const float*>(f.ecx);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(line[i]);
             r.eax = Next() % 3 == 0;
         }},
        {0x00417250, 8, 2, Ret::Int, true, 0,
         {Ours(static_cast<Nx::CFeeler* (Nx::CFeeler::*)(const Vector*, const Vector*)>(&Nx::CFeeler::Construct))},
         [](Frame& f, Result& r) {
             std::memcpy(reinterpret_cast<void*>(f.ecx), reinterpret_cast<void*>(f.args[0]), 16);
             std::memcpy(reinterpret_cast<uint8_t*>(f.ecx) + 16, reinterpret_cast<void*>(f.args[1]), 16);
             RecordVector(f.args[0]);
             RecordVector(f.args[1]);
             r.eax = f.ecx;
         }},
        {0x0041C350, 0, 0, Ret::None, true, 0, {Ours(&Nx::CFeeler::Destruct)}},
        {0x00507500, 12, 3, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::WallPlant)}},
        {0x00508140, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::StartWallRide)}},
        {0x00434BF0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CTriggerComponent::CheckFeeler)}},
        {0x00580790, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledPhysics)}},
        {0x00518DD0, 8, 2, Ret::None, true, 2, {Ours(&Obj::CSkaterSoundComponent::PlayBonk)}},
        {0x00475790, 0, 5, Ret::None, false, 8,
         {Ours(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))}},
        {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
        {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kHitObject)); }},
        {0x0042BBF0, 4, 1, Ret::None, true, 0, {Ours(&Mth::Vector::SetLength)}, [](Frame& f, Result&) { RecordVector(f.ecx); }},
        {0x005039F0, 12, 3, Ret::Float, true, 0, {Ours(&Skater::CorePhysicsComponent::BounceOffWall)}, [](Frame& f, Result&) { RecordVector(f.args[0]); }},
    };
}

void FillCollisionArena()
{
    FillArena();
    for (uint32_t f : {0x30u, 0x70u, 0x78u, 0xB0u, 0xE0u})
        ArenaAt<uint8_t>(kFlags + f) = RandomBool(f == 0x30 ? 30 : 25);
    static const int states[] = {0, 1, 1, 2, 3, 4};
    ArenaAt<int>(kFlags + 0x18) = states[Next() % 6];
    ArenaAt<uint8_t>(kComponent + 0x228) = RandomBool(50);
    ArenaAt<uint8_t>(kComponent + 0x2ED) = RandomBool(30);
    ArenaAt<uint8_t>(kComponent + 0x302) = RandomBool(30);
    ArenaAt<uint8_t>(kComponent + 0xFA) = static_cast<uint8_t>(Next());
    ArenaAt<uint8_t>(kTriggerState + 0x26) = RandomBool(20);
    ArenaAt<uint32_t>(kComponent + 0xA8 + 0x98) = Next() % 2 ? 0 : 0x0ABC0000; // (the feeler's object)
    for (uint32_t b : {0x60u, 0x80u, 0xA0u, 0x100u})
        ArenaAt<uint8_t>(kInput + b) = RandomBool(30);
    // Some values on the edges of the comparisons.
    static const float edges[] = {0.5f, 0.1f, 0.01f, 0.05f, -0.1f, 0.0f, 10.0f, 0.25f, -0.25f, 0.8f};
    if (Next() % 3 == 0)
        ArenaAt<float>(kComponent + 0x134) = edges[Next() % 10];
    if (Next() % 4 == 0)
        ArenaAt<float>(kObject + 0x80) = edges[Next() % 10];
    if (Next() % 4 == 0)
        ArenaAt<float>(kObject + 0xB0) = edges[Next() % 10] * 2.0f;
    if (Next() % 5 == 0)
        ArenaAt<float>(kObject + 0x4C + 4 * (Next() % 4)) = ArenaAt<float>(kFrom + 4 * (Next() % 4));
    if (Next() % 7 == 0)
        std::memcpy(ArenaPtr(kObject + 0x4C), ArenaPtr(kFrom), 16); // (no move at all)
    if (Next() % 3 != 0) {
        // Full-precision values (roundings show), of different sizes.
        auto full = [](float range) { return static_cast<float>((static_cast<double>(Next()) / 4294967296.0 - 0.5) * range); };
        for (uint32_t at : {kObject + 0x4Cu, kFrom, kObject + 0x6Cu, kObject + 0x7Cu, kObject + 0x8Cu, kObject + 0xACu,
                            kComponent + 0x120u, kComponent + 0x130u, kComponent + 0x208u})
            for (uint32_t i = 0; i < 16; i += 4)
                ArenaAt<float>(at + i) = full(Next() % 2 ? 7.0f : 3000.0f);
    }
    if (Next() % 6 == 0) {
        // Axis-aligned up and normal: the dot products meet their limits exactly.
        static const float ys[] = {0.25f, -0.25f, 0.8f, 0.5f, 1.0f};
        ArenaAt<float>(kObject + 0x7C) = 0.0f, ArenaAt<float>(kObject + 0x80) = 1.0f, ArenaAt<float>(kObject + 0x84) = 0.0f;
        ArenaAt<float>(kComponent + 0x68) = 0.0f, ArenaAt<float>(kComponent + 0x6C) = 1.0f, ArenaAt<float>(kComponent + 0x70) = 0.0f;
        ArenaAt<float>(kComponent + 0x130) = 0.0f, ArenaAt<float>(kComponent + 0x134) = ys[Next() % 5];
        ArenaAt<float>(kComponent + 0x138) = 0.0f;
        ArenaAt<float>(kHitObject + 0xAC) = 2.0f, ArenaAt<float>(kHitObject + 0xB0) = 0.0f; // (along the wall)
        ArenaAt<float>(kHitObject + 0xB4) = -3.0f;
    }
    if (Next() % 5 == 0)
        ArenaAt<float>(kObject + 0xB0) = 10.0f;
    if (Next() % 4 == 0)
        std::memset(ArenaPtr(kHitObject + 0xAC), 0, 16); // (a hit object standing still)
}

// CheckAirForwardCollision 0x00508780.
void TestCheckAirForwardCollision()
{
    StandIns standIns(CollisionStandIns());
    std::unique_ptr<UseOurs> vectors(OurVectorFunctions());
    ClockOffsetZero clock;
    bool same = true;
    int stoppedThere = 0, snapped = 0, vert = 0, bonked = 0, sounds = 0, pushed = 0;
    for (int variant = 0; variant < 12000 && same; ++variant) {
        g_rng = 0x9E3779B9u * (variant + 7);
        FillCollisionArena();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto from = reinterpret_cast<const Vector*>(ArenaPtr(kFrom));
        auto expected = RunOnArena(seed, [from](uint8_t* c) {
            return static_cast<uint32_t>(Original<bool(__thiscall*)(void*, const Vector*)>(0x00508780)(c, from));
        });
        auto actual = RunOnArena(seed, [from](uint8_t* c) {
            return static_cast<uint32_t>(reinterpret_cast<CorePhysicsComponent*>(c)->CheckAirForwardCollision(from));
        });
        bool returned = actual[0] != 0;
        stoppedThere += returned && !Called(actual, 0x0041C350);
        snapped += returned && Called(actual, 0x0041C350);
        vert += !returned && CalledWith(actual, 0x00580400, 0x23410C14);
        bonked += Called(actual, 0x00434BF0) && !returned;
        sounds += Called(actual, 0x00518DD0) || Called(actual, 0x00475790);
        pushed += Called(actual, 0x00417240) && ArenaAt<uint32_t>(kObject + 0xAC) != *reinterpret_cast<uint32_t*>(g_arenaStart + kObject + 0xAC);
        same = Compare(expected, actual, "air forward collision", variant);
    }
    bool covered = stoppedThere > 100 && snapped > 100 && vert > 100 && bonked > 300 && sounds > 200 && pushed > 50;
    if (!covered)
        Log("  air forward collision coverage: stopped there %d, snapped %d, vert %d, bonked %d, sounds %d, pushed %d",
            stoppedThere, snapped, vert, bonked, sounds, pushed);
    Check(same && covered, "skater: CheckAirForwardCollision");
}

// CheckSideCollision 0x0050A2C0: each side, lengths, each state (on a rail: nothing), in the air (taken over or
// pushed out by some overlap or none), on the ground (walls facing up / down / across, something between, a
// trigger taking over).
void TestCheckSideCollision()
{
    StandIns standIns(CollisionStandIns());
    std::unique_ptr<UseOurs> vectors(OurVectorFunctions());
    ClockOffsetZero clock;
    bool same = true;
    int pushedOut = 0, onWall = 0;
    for (int variant = 0; variant < 16000 && same; ++variant) {
        g_rng = 0x7F4A7C15u * (variant + 3);
        FillCollisionArena();
        SaveStart();
        uint32_t seed = Next() | 1;
        float side = variant & 1 ? 1.0f : -1.0f;
        if (variant % 4 == 3) // (any side length, though the game only uses 1 and -1)
            side = static_cast<float>((static_cast<double>(Next()) / 4294967296.0 - 0.5) * 3.0);
        float length = RandomFloat(20.0f);
        auto expected = RunOnArena(seed, [side, length](uint8_t* c) {
            return static_cast<uint32_t>(
                Original<bool(__thiscall*)(void*, float, float)>(0x0050A2C0)(c, side, length));
        });
        auto actual = RunOnArena(seed, [side, length](uint8_t* c) {
            return static_cast<uint32_t>(reinterpret_cast<CorePhysicsComponent*>(c)->CheckSideCollision(side, length));
        });
        pushedOut += Called(actual, 0x0042BBF0);
        onWall += actual[0] && Called(actual, 0x00417250);
        same = Compare(expected, actual, "side collision", variant);
    }
    bool covered = pushedOut > 300 && onWall > 30;
    if (!covered)
        Log("  side collision coverage: pushed out %d, onto the wall %d", pushedOut, onWall);
    Check(same && covered, "skater: CheckSideCollision");
}

// CheckSideCollisions 0x0050C930: the four orders of the two sides' results.
void TestCheckSideCollisions()
{
    StandIns standIns({{0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
                       {0x0050A2C0, 8, 2, Ret::Bool, true, 0, {Ours(&CorePhysicsComponent::CheckSideCollision)},
                        [](Frame&, Result&) {
                            // (each side's check moves the skater a little)
                            ArenaAt<float>(kObject + 0x4C) += 1.0f;
                        }}});
    bool same = true;
    for (int variant = 0; variant < 400 && same; ++variant) {
        g_rng = 0x1B873593u * (variant + 1);
        FillArena();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x0050C930)(c);
            return 0u;
        });
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->CheckSideCollisions();
            return 0u;
        });
        same = Compare(expected, actual, "side collisions", variant);
    }
    Check(same, "skater: CheckSideCollisions");
}

} // namespace

SELFTEST(Early, TestCheckAirForwardCollision);
SELFTEST(Early, TestCheckSideCollision);
SELFTEST(Early, TestCheckSideCollisions);
