// Self-tests for the skater core physics component's friction, braking, pushing and ground normal functions
// (core_physics_ground.cpp): original vs ours on a stand-in component, the game calls stood in for
// (skater_core_testkit.h); pure maths (vector lengths, normalizing, the matrix) runs for real.

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/vector.h"
#include "game/mth/matrix.h"
#include "game/nx/feeler.h"
#include "game/obj/walk_hang.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Skater::CorePhysicsComponent;

constexpr uint32_t kSettings = kFree + 0x40; // what *0x007CE5D8 points at (+0x40: Off or not)

float PickGround()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 0.001f, -0.25f, 7.0f, 0.05f, 49.0f,
                                   51.0f, 0.3f, 0.9f, 0.016f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}

std::vector<StandIn> GroundStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)},
         [](Frame&, Result& r) { r.st0 = PickGround(); }},
        {0x00580790, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledPhysics)},
         [](Frame&, Result& r) { r.st0 = PickGround(); }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) { r.st0 = PickGround(); }},
        {0x00478F60, 0, 1, Ret::Bool, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x00401C30, 0, 14, Ret::Int, false, 0, {Ours(&Dbg::Printf)}},
        {0x004172F0, 8, 2, Ret::Int, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
         [](Frame& f, Result& r) {
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
             for (const Mth::Vector* v : {&feeler->m_line.start, &feeler->m_line.end}) {
                 RecordFloat(v->x);
                 RecordFloat(v->y);
                 RecordFloat(v->z);
                 RecordFloat(v->w);
             }
             r.eax = Next() % 2;
             feeler->m_object.mp_object = Next() % 3 ? nullptr : reinterpret_cast<Obj::CCompositeObject*>(0x5D5D0000);
         }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
    };
}

void SetVector(uint32_t at, float x, float y, float z, float w)
{
    ArenaAt<Mth::Vector>(at) = Mth::Vector(x, y, z, w);
}

void FillGround()
{
    FillArena();
    uint8_t* input = ArenaPtr(kInput);
    for (uint32_t button : {0x40u, 0x60u, 0x80u, 0x180u})
        input[button] = RandomBool(50);
    for (uint32_t flag : {0x20u, 0x30u, 0x38u, 0x40u, 0x68u})
        ArenaAt<uint8_t>(kFlags + flag) = RandomBool(50);
    for (uint32_t offset : {0x2D9u, 0x303u, 0x310u, 0x311u, 0x388u})
        ArenaAt<uint8_t>(kComponent + offset) = RandomBool(50);
    static const float frames[] = {0.016f, 0.05f, 0.03125f};
    ArenaAt<float>(kComponent + 0x8C) = frames[Next() % 3];
    static const float rolling[] = {0.02f, 2.5f, 0.0f};
    ArenaAt<float>(kComponent + 0x314) = rolling[Next() % 3];
    if (Next() % 2)
        ArenaAt<float>(kComponent + 0x350) = 0.0f;
    // The velocity: fast, slow, barely moving or still.
    static const float scales[] = {40.0f, 20.0f, 1.0f, 0.1f, 0.0001f, 0.0f, 0.002f};
    float scale = scales[Next() % 7];
    for (uint32_t i = 0; i < 4; ++i)
        ArenaAt<float>(kObject + 0xAC + 4 * i) *= scale;
    // Normals: the ground's (+0x68) up-ish or flat, the old one (+0x78) the same at times, the drawn one (+0x58) and
    // the display matrix's up (+0x28) the same at times; the blend (+0x88).
    switch (Next() % 4) {
    case 0: SetVector(kComponent + 0x68, 0.0f, 1.0f, 0.0f, 0.0f); break;
    case 1: SetVector(kComponent + 0x68, 0.6f, 0.0f, 0.8f, 0.0f); break;
    case 2: SetVector(kComponent + 0x68, 0.0f, 0.0f, 0.0f, 0.0f); break;
    default: break;
    }
    if (Next() % 3 == 0)
        ArenaAt<Mth::Vector>(kComponent + 0x78) = ArenaAt<Mth::Vector>(kComponent + 0x68);
    if (Next() % 3 == 0)
        ArenaAt<Mth::Vector>(kComponent + 0x28) = ArenaAt<Mth::Vector>(kComponent + 0x58);
    static const float blends[] = {0.0f, 1.0f, 0.5f, 0.01f, 0.1f, 0.9375f};
    ArenaAt<float>(kComponent + 0x88) = blends[Next() % 6];
    if (Next() % 3 == 0) { // (a blend that runs out exactly at a rate of 0.5)
        ArenaAt<float>(kComponent + 0x8C) = 0.03125f;
        ArenaAt<float>(kComponent + 0x88) = 0.9375f;
    }
    if (Next() % 4 == 0) { // (moving square to the facing)
        SetVector(kObject + 0x8C, 1.0f, 0.0f, 0.0f, 0.0f);
        SetVector(kObject + 0xAC, 0.0f, 0.0f, 60.0f, 0.0f);
    }
    ArenaAt<uint32_t>(kSettings + 0x40) = Next() % 3 ? 0x12345678u : 0xD443A2BCu;
}

void TestCoreGround()
{
    StandIns standIns(GroundStandIns());
    // The vector and matrix maths the original calls (each tested on its own) pointed at ours: their x87 and SSE
    // results may differ in the last bit.
    UseOurs maths({{0x004023F0, Ours(&Mth::Vector::Normalize)},
                   {0x004060F0, Ours(&Mth::Vector::RotateToPlane)},
                   {0x00405170, Ours(&Mth::Matrix::OrthoNormalizeAbout)},
                   {0x0043D990, Ours(&Walk::Lerp)},
                   {0x0042BBF0, Ours(&Mth::Vector::SetLength)}});
    ClockOffsetZero clock;
    uint8_t* savedSettings = GameVariable<uint8_t*>(0x007CE5D8);
    GameVariable<uint8_t*>(0x007CE5D8) = ArenaPtr(kSettings);
    using Run = std::function<uint32_t(uint8_t*)>;
    using C = CorePhysicsComponent;
    struct Case {
        const char* name;
        Run original;
        Run ours;
    };
    auto voidCase = [](const char* name, uint32_t address, void (C::*ours)()) {
        return Case{name,
                    [=](uint8_t* c) {
                        Original<void(__thiscall*)(void*)>(address)(c);
                        return 0u;
                    },
                    [=](uint8_t* c) {
                        (reinterpret_cast<C*>(c)->*ours)();
                        return 0u;
                    }};
    };
    auto boolCase = [](const char* name, uint32_t address, bool (C::*ours)()) {
        return Case{name, [=](uint8_t* c) { return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(address)(c)); },
                    [=](uint8_t* c) { return static_cast<uint32_t>((reinterpret_cast<C*>(c)->*ours)()); }};
    };
    static float friction;
    static Mth::Vector normal;
    const std::vector<Case> cases = {
        {"ApplyFriction",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*, float)>(0x004FE1F0)(c, friction);
             return 0u;
         },
         [](uint8_t* c) {
             reinterpret_cast<C*>(c)->ApplyFriction(friction);
             return 0u;
         }},
        boolCase("ShouldBrake", 0x004FE320, &C::ShouldBrake),
        boolCase("MayTurnOnSlope", 0x004FE420, &C::MayTurnOnSlope),
        boolCase("CanPushOff", 0x004FE490, &C::CanPushOff),
        voidCase("PushOff", 0x004FE530, &C::PushOff),
        voidCase("ApplyStanceFriction", 0x004FE6A0, &C::ApplyStanceFriction),
        voidCase("ApplyRollingFriction", 0x004FE730, &C::ApplyRollingFriction),
        {"TurnToNormal",
         [](uint8_t* c) {
             Original<void(__thiscall*)(void*, Mth::Vector)>(0x004FE860)(c, normal);
             return 0u;
         },
         [](uint8_t* c) {
             reinterpret_cast<C*>(c)->TurnToNormal(normal);
             return 0u;
         }},
        voidCase("UpdateGroundNormal", 0x004FEA50, &C::UpdateGroundNormal),
        voidCase("StartVertAir", 0x004FEBA0, &C::StartVertAir),
    };
    const int kCases = static_cast<int>(cases.size());
    bool same = true;
    for (int variant = 0; variant < kCases * 1500 && same; ++variant) {
        g_rng = 0x85EBCA6Bu * (variant + 11);
        FillGround();
        static const float frictions[] = {0.0f, 0.02f, 0.5f, 3.0f, 40.0f};
        friction = frictions[Next() % 5];
        switch (Next() % 4) {
        case 0: normal = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f); break;
        case 1: normal = ArenaAt<Mth::Vector>(kComponent + 0x68); break;
        case 2: normal = Mth::Vector(0.6f, -0.8f, 0.0f, 0.0f); break;
        default: normal = Mth::Vector(0.28f, 0.96f, 0.0f, 0.5f); break;
        }
        SaveStart();
        uint32_t seed = Next() | 1;
        const Case& test = cases[variant % kCases];
        auto expected = RunOnArena(seed, test.original);
        auto actual = RunOnArena(seed, test.ours);
        same = Compare(expected, actual, test.name, variant);
    }
    GameVariable<uint8_t*>(0x007CE5D8) = savedSettings;
    Check(same, "skater: the core physics' friction, brakes, push and ground normal (ApplyFriction ... StartVertAir)");
}

} // namespace

SELFTEST(Early, TestCoreGround);
