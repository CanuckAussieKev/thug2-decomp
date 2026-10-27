// Self-tests for the skater core physics component's gravity, vert target, speed override, wall push-off, turning
// round, flip spins and stepping up (core_physics_moves.cpp): original vs ours on a stand-in component, the game
// calls stood in for (skater_core_testkit.h); the vector maths the original calls pointed at ours.

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
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
using Skater::CorePhysicsComponent;

constexpr uint32_t kSkateModule = kFree + 0x000;  // *0x007CE478 (+0x20: the career, a stand-in pointer)
constexpr uint32_t kBalance = kFree + 0x040;      // the balance trick component (+0x18 its trick)
constexpr uint32_t kSpins = kFree + 0x080;        // the flip spins' array
constexpr uint32_t kArgs = kFree + 0x100;         // vectors passed in and out

float PickMove()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 0.001f, -0.25f, 7.0f, 0.05f, 1.25f,
                                   200.0f, 0.3f, 0.9f, 2.0f, 0.7f, -0.7f, 0.0005f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}

std::vector<StandIn> MoveStandIns()
{
    using Script::CStruct;
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)},
         [](Frame&, Result& r) { r.st0 = PickMove(); }},
        {0x00580790, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledPhysics)},
         [](Frame&, Result& r) { r.st0 = PickMove(); }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) { r.st0 = PickMove(); }},
        {0x00478F60, 0, 1, Ret::Bool, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x00584CC0, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CSkaterCareer::GetCheat)}},
        {0x005843A0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSkaterCareer::SetGlobalFlag)}},
        {0x00401C30, 0, 3, Ret::Int, false, 0, {Ours(&Dbg::Printf)}},
        {0x0040AB50, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CCollCache::Construct)},
         [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x0040ABA0, 4, 1, Ret::None, true, 0, {Ours(&Nx::CCollCache::Update)},
         [](Frame& f, Result&) {
             const float* box = reinterpret_cast<const float*>(f.args[0]);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(box[i]);
         }},
        {0x0040AB40, 0, 0, Ret::None, true, 0, {Ours(&Nx::CCollCache::Destruct)}},
        {0x004172F0, 8, 2, Ret::Int, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
         [](Frame& f, Result& r) {
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
             for (const Mth::Vector* v : {&feeler->m_line.start, &feeler->m_line.end}) {
                 RecordFloat(v->x);
                 RecordFloat(v->y);
                 RecordFloat(v->z);
                 RecordFloat(v->w);
             }
             r.eax = Next() % 3 ? 1 : 0;
             feeler->m_hit.faceFlags = static_cast<uint16_t>(Next() & 1 ? 8 : 0x10);
             static const float ys[] = {0.0f, 0.3f, 0.6f, 0.71f, 0.9f, -0.2f, 0.5f, 0.68f, -0.68f};
             feeler->m_normal = Mth::Vector(PickMove(), ys[Next() % 9], PickMove(), 0.0f);
             feeler->m_point = Mth::Vector(PickMove(), PickMove(), PickMove(), 1.0f);
             feeler->m_object.mp_object = Next() % 3 ? nullptr : reinterpret_cast<Obj::CCompositeObject*>(0x5D5D0000);
         }},
        {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x00475790, 0, 5, Ret::None, false, 0,
         {Ours(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))}},
        {0x00401B90, 0, 1, Ret::Int, false, 0, {Ours(&Crc::GenerateCRCFromString)},
         [](Frame&, Result& r) {
             static const uint32_t boards[] = {0xEF24413B, 0x89B7DBE4, 0x12345678, 0xC692E824, 0x0BADF00D};
             r.eax = boards[Next() % 5];
         }},
        {0x00479070, 0, 1, Ret::Int, false, 0, {Ours(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kSpins)); }},
        {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)},
         [](Frame& f, Result& r) { r.eax = 0x5A5A0000u + f.args[0] * 0x100; }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         [](Frame& f, Result&) { // (written either way: missing, the flip spin's sequence is left as garbage)
             *reinterpret_cast<uint32_t*>(f.args[1]) = 0x1000u + Next() % 4;
         }},
        {0x00476630, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         [](Frame& f, Result& r) {
             static const int angles[] = {0, 90, 180, -180, 360, -90};
             if (r.eax)
                 *reinterpret_cast<int*>(f.args[1]) = angles[Next() % 6];
         }},
        {0x0051F9B0, 8, 2, Ret::Bool, true, 2, {Ours(&Obj::CTrickComponent::CheckSpinSequence)},
         [](Frame& f, Result& r) {
             const uint32_t* sequence = reinterpret_cast<const uint32_t*>(f.args[0]);
             Record(sequence[0]);
             Record(sequence[1]);
             Record(sequence[2]);
             r.eax = Next() % 3 == 0;
         }},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame& f, Result& r) { r.eax = 0x51000000u + (f.args[0] & 0xFFF0); }},
        {0x00420730, 0x20, 8, Ret::None, true, 0xF0, {Ours(&Obj::CModelComponent::RotateDisplay)}},
        {0x0046B7F0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CControlButton::ClearRelease)}},
        {0x00487AD0, 0, 3, Ret::Float, false, 0, {}, [](Frame&, Result& r) { r.st0 = PickMove(); }},
        {0x004196B0, 0, 2, Ret::Float, false, 0, {Ours(&Obj::LargerOf)}, [](Frame&, Result& r) { r.st0 = PickMove(); }},
    };
}

void FillMoves()
{
    FillArena();
    for (uint32_t flag : {0x20u, 0x30u, 0x70u, 0x78u, 0x88u, 0xB8u})
        ArenaAt<uint8_t>(kFlags + flag) = RandomBool(40);
    for (uint32_t offset : {0x255u, 0x2DBu, 0x2EDu, 0x304u, 0x324u, 0x2E4u})
        ArenaAt<uint8_t>(kComponent + offset) = RandomBool(40);
    ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 3);
    ArenaAt<int>(kFlags + 0x24) = PickInt();
    ArenaAt<float>(kComponent + 0x3AC) = Next() % 3 ? PickMove() : 1.0f;
    ArenaAt<float>(kComponent + 0x354) = PickMove();
    ArenaAt<uint32_t>(kComponent + 0x350) = Next() % 4 ? Next() : 0xC0000000u;
    ArenaAt<int>(kComponent + 0x4E0) = PickInt();
    ArenaAt<int>(kComponent + 0x4E4) = PickInt();
    ArenaAt<float>(kComponent + 0x8C) = 0.016f;
    ArenaAt<uint8_t*>(kComponent + 0x1524) = ArenaPtr(kBalance);
    static const uint32_t tricks[] = {0xEF24413B, 0x0AC90769, 0x11111111};
    ArenaAt<uint32_t>(kBalance + 0x18) = tricks[Next() % 3];
    std::memcpy(ArenaPtr(kSkater + 0x1331), "board", 6);
    uint8_t* input = ArenaPtr(kInput);
    for (uint32_t button : {0x160u, 0x180u, 0x1A0u, 0x1C0u})
        input[button] = RandomBool(50);
    ArenaAt<uint32_t>(kSkateModule + 0x20) = 0x5C000000u;
    ArenaAt<uint32_t>(kSpins + 4) = Next() % 4;
    // The velocity: fast, slow or nearly still; the facing at times along it.
    static const float scales[] = {40.0f, 3.0f, 0.5f, 0.000001f, 0.0f, 0.001f};
    float scale = scales[Next() % 6];
    for (uint32_t i = 0; i < 4; ++i)
        ArenaAt<float>(kObject + 0xAC + 4 * i) *= scale;
    for (uint32_t i = 0; i < 4; ++i) {
        ArenaAt<float>(kArgs + 4 * i) *= scale;
        ArenaAt<float>(kArgs + 0x40 + 4 * i) *= scale; // (AlongFacing's)
    }
    if (Next() % 4 == 0) { // (the argument square to the facing)
        ArenaAt<Mth::Vector>(kArgs + 0x40) = Mth::Vector(0.0f, 0.0f, 5.0f, 0.0f);
        ArenaAt<Mth::Vector>(kObject + 0x8C) = Mth::Vector(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (Next() % 3 == 0) // (slow: under 1)
        ArenaAt<Mth::Vector>(kObject + 0xAC) = Mth::Vector(0.3f, 0.0f, 0.2f, 0.0f);
}

void TestCoreMoves()
{
    StandIns standIns(MoveStandIns());
    UseOurs maths({{0x004023F0, Ours(&Mth::Vector::Normalize)},
                   {0x004060F0, Ours(&Mth::Vector::RotateToPlane)},
                   {0x00402530, Ours(&Mth::DotProduct)},
                   {0x00402550, Ours(&Mth::CrossProduct)},
                   {0x004024D0, Ours(static_cast<Mth::Vector (*)(float, const Mth::Vector&)>(&Mth::operator*))},
                   {0x00402500, Ours(static_cast<Mth::Vector (*)(const Mth::Vector&)>(&Mth::operator-))},
                   {0x00406050, Ours(&Mth::Vector::operator-=)},
                   {0x00402370, Ours(&Mth::Vector::operator*=)}});
    ClockOffsetZero clock;
    uint8_t* savedModule = GameVariable<uint8_t*>(0x007CE478);
    int savedCounter = GameVariable<int>(0x007CCDCC);
    uint8_t savedMoon = GameVariable<uint8_t>(0x007CE4FD);
    GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkateModule);
    static int counter;
    // The globals the functions change: set before each run, recorded after.
    auto globals = [](uint32_t result) {
        Record(static_cast<uint32_t>(GameVariable<int>(0x007CCDCC)));
        Record(GameVariable<uint8_t>(0x007CE4FD));
        return result;
    };
    auto start = [] {
        GameVariable<int>(0x007CCDCC) = counter;
        GameVariable<uint8_t>(0x007CE4FD) = 0;
    };
    using Run = std::function<uint32_t(uint8_t*)>;
    using C = CorePhysicsComponent;
    struct Case {
        const char* name;
        Run original;
        Run ours;
    };
    auto vec = [](uint32_t i) { return &ArenaAt<Mth::Vector>(kArgs + 0x10 * i); };
    auto boolCase = [&](const char* name, uint32_t address, bool (C::*ours)()) {
        return Case{name,
                    [=](uint8_t* c) {
                        start();
                        return globals(static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(address)(c)));
                    },
                    [=](uint8_t* c) {
                        start();
                        return globals(static_cast<uint32_t>((reinterpret_cast<C*>(c)->*ours)()));
                    }};
    };
    auto voidCase = [&](const char* name, uint32_t address, void (C::*ours)()) {
        return Case{name,
                    [=](uint8_t* c) {
                        start();
                        Original<void(__thiscall*)(void*)>(address)(c);
                        return globals(0);
                    },
                    [=](uint8_t* c) {
                        start();
                        (reinterpret_cast<C*>(c)->*ours)();
                        return globals(0);
                    }};
    };
    const std::vector<Case> cases = {
        {"FindVertTarget",
         [=](uint8_t* c) {
             start();
             auto* out = ArenaPtr(kArgs + 0x80);
             return globals(static_cast<uint32_t>(
                 Original<bool(__thiscall*)(void*, void*, void*, void*, void*, void*, void*)>(0x004FEE10)(
                     c, vec(0), vec(1), out, vec(2), vec(3), out + 1)));
         },
         [=](uint8_t* c) {
             start();
             auto* out = reinterpret_cast<bool*>(ArenaPtr(kArgs + 0x80));
             return globals(static_cast<uint32_t>(
                 reinterpret_cast<C*>(c)->FindVertTarget(vec(0), vec(1), out, vec(2), vec(3), out + 1)));
         }},
        voidCase("UpdateSpeedOverride", 0x004FF200, &C::UpdateSpeedOverride),
        {"GetGravity",
         [=](uint8_t* c) {
             start();
             double g = Original<double(__thiscall*)(void*)>(0x004FF470)(c);
             uint64_t bits;
             std::memcpy(&bits, &g, 8);
             Record(static_cast<uint32_t>(bits >> 32));
             return globals(static_cast<uint32_t>(bits));
         },
         [=](uint8_t* c) {
             start();
             double g = reinterpret_cast<C*>(c)->GetGravity();
             uint64_t bits;
             std::memcpy(&bits, &g, 8);
             Record(static_cast<uint32_t>(bits >> 32));
             return globals(static_cast<uint32_t>(bits));
         }},
        {"AlongFacing",
         [=](uint8_t* c) {
             start();
             Original<void(__thiscall*)(void*, void*)>(0x004FF590)(c, vec(4));
             return globals(0);
         },
         [=](uint8_t* c) {
             start();
             reinterpret_cast<C*>(c)->AlongFacing(vec(4));
             return globals(0);
         }},
        boolCase("WallPushOff", 0x004FF670, &C::WallPushOff),
        voidCase("TurnRoundIfBackwards", 0x004FF920, &C::TurnRoundIfBackwards),
        boolCase("JumpReleased", 0x004FFB10, &C::JumpReleased),
        voidCase("CheckFlipSpin", 0x004FFB90, &C::CheckFlipSpin),
        {"StepUp",
         [=](uint8_t* c) {
             start();
             return globals(static_cast<uint32_t>(Original<bool(__thiscall*)(void*, void*)>(0x004FFDB0)(c, vec(5))));
         },
         [=](uint8_t* c) {
             start();
             return globals(static_cast<uint32_t>(reinterpret_cast<C*>(c)->StepUp(vec(5))));
         }},
    };
    const int kCases = static_cast<int>(cases.size());
    bool same = true;
    for (int variant = 0; variant < kCases * 1200 && same; ++variant) {
        g_rng = 0xC2B2AE35u * (variant + 5);
        FillMoves();
        counter = static_cast<int>(Next() % 10);
        SaveStart();
        uint32_t seed = Next() | 1;
        const Case& test = cases[variant % kCases];
        auto expected = RunOnArena(seed, test.original);
        auto actual = RunOnArena(seed, test.ours);
        same = Compare(expected, actual, test.name, variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedModule;
    GameVariable<int>(0x007CCDCC) = savedCounter;
    GameVariable<uint8_t>(0x007CE4FD) = savedMoon;
    Check(same, "skater: the core physics' gravity, vert target, speed override, push-off, turn round, flip spin, step up");
}

} // namespace

SELFTEST(Early, TestCoreMoves);
