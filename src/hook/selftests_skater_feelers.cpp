// Self-tests for the skater core physics component's feelers, member functions and skitching
// (core_physics_feelers.cpp, skitch.cpp): original vs ours on a stand-in component, every callee into game code
// replaced by a recording stand-in for the length of the test (the math functions pointed at ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/shadow_component.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/skater.h"
#include "game/skater/skitch.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h"

using namespace SkaterPhysicsTest;
using Skater::CorePhysicsComponent;

namespace {

CorePhysicsComponent* Ours(uint8_t* component) { return reinterpret_cast<CorePhysicsComponent*>(component); }

uint32_t g_pointerA; // what Ret::Fixed stand-ins return
uint32_t g_pointerB;
uint32_t g_skitchIndex; // what the nearest skitch point stand-in returns

float NotANumber()
{
    uint32_t bits = 0x7FC00000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::vector<StandIn> FeelerStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0, CodeAddress(&Tmr::GameClockElapsed)}, // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00478F60, 0, 1, Ret::Int, false, 0, -1, -1, 0,
         CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
        {0x004171D0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetLine)},
        {0x004170F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::SetIgnore), nullptr, 0, 3},
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 1, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x74, 0, 8}, // (the line recorded, the hit's distance filled in)
        {0x0057CB60, 8, 2, Ret::FirstArg, true, 0, -1, 0, 4, CodeAddress(&Obj::CRailManager::GetWorldPos), nullptr, 0, 0, -1, -1, 0, 8.0f}, // a rail node's position
        {0x0042BBF0, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Mth::Vector::SetLength), nullptr, 1}, // SetLength (a float)
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0, CodeAddress(&Obj::GetPhysicsFloat)},  // PhysicsFloat
        // (members)
        {0x00476A90, 4, 1, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::FindComponent)},
        {0x0045DB90, 4, 1, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::GetComponent),
         &g_pointerA},
        {0x004317C0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CShadowComponent::SetPosition)},    // (the shadow placed)
        {0x004317E0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CShadowComponent::SetNormal)},
        {0x0045E4C0, 4, 1, Ret::None, true, 1, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::SetTeleported)},
        {0x00580880, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkater::GetSkaterCamera), &g_pointerB}, // (the skater's camera)
        {0x0045D920, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::Update)},
        {0x00476B40, 4, 1, Ret::Bool, true, 0, -1, -1, 0,
         CodeAddress(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))},
        {0x004FDED0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Skater::CorePhysicsComponent::SetPhysicsState)},   // SetState
        {0x00511420, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkaterFlipAndRotateComponent::ApplyFlipToAnims)},
        {0x004215F0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CModelComponent::GetDisplayMatrix)},   // (the model's rotations onto a matrix)
        {0x004213E0, 0, 0, Ret::Float, true, 0, -1, -1, 0, CodeAddress(&Obj::CModelSpinAxis::Angle)},
        {0x00420AC0, 8, 2, Ret::Int, true, 0, 0, -1, 4, CodeAddress(&Mth::Quat::InitFromAxisAngle), nullptr, 2},
        {0x00420B50, 8, 2, Ret::FirstArg, true, 0, 1, 0, 4, CodeAddress(&Mth::Quat::Rotate)},
        {0x004204F0, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CModelSpinAxis::Clear)},
        // (skitching)
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CControlButton::GetPressedTime)},    // TimeSincePress
        {0x0046B7E0, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CControlButton::ClearTrigger)},   // TakePress
        {0x0045F2D0, 4, 1, Ret::Fixed, true, 0, -1, -1, 0,
         CodeAddress(&Obj::CCompositeObjectManager::GetFirstComponentByType), &g_pointerB},
        {0x0051BA70, 8, 2, Ret::Fixed, true, 0, 1, 0, 4, CodeAddress(&Obj::CSkitchComponent::GetNearestHookPoint), &g_skitchIndex}, // the nearest skitch point
        {0x0041D1D0, 4, 1, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CSmtPtr<Obj::CRefCounted>::AssignObject)},    // (a smart pointer set)
        {0x004609F0, 8, 2, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::SelfEvent)},
        {0x0051BC70, 8, 2, Ret::Bool, true, 0, -1, 0, 4, CodeAddress(&Obj::CSkitchComponent::GetHookPoint)},    // a skitch point's position
    };
}

// A member function of a stand-in component (vtable slot 12): recorded.
int __fastcall FakeCallMember(void*, void*, uint32_t name, void*, void*)
{
    RecordValue(0xCA11CA11u);
    RecordValue(name);
    return static_cast<int>(Next() % 3);
}

using Method = void (CorePhysicsComponent::*)();

// Random face flags for the feeler's hit (the bits CheckFeeler looks at, alone and together).
uint16_t FaceFlags()
{
    static const uint16_t flags[] = {0, 0, 1, 2, 4, 8, 3, 5, 6, 0xC, 0x10, 0x14, 0x12, 0xA, 0x9, 0xE};
    return flags[Next() % (sizeof(flags) / sizeof(flags[0]))];
}

// The component's feeler results and lines randomised.
void FeelerState()
{
    ArenaAt<uint16_t>(kComponent + 0xF8) = FaceFlags();
    ArenaAt<uint32_t>(kComponent + 0x100) = RandomBool(50) ? Next() : 0;
    ArenaAt<uint8_t>(kComponent + 0x115) = RandomBool(50);
    if (RandomBool(10))
        ArenaAt<float>(kComponent + 0x134) = NotANumber();
}

// CheckFeeler, NearGround's feeler, the side collisions, the nearest wall side, CanGrindRail.
void TestSkaterPhysicsFeelers()
{
    g_rng = 3;
    StandIns standIns(FeelerStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    uint8_t* savedPark = GameVariable<uint8_t*>(0x007CE5D8);
    bool same = true;
    int deep[6] = {};
    for (int variant = 0; variant < 9000 && same; ++variant) {
        int which = variant % 6;
        g_rng = 0x2545F491u * (variant + 1);
        FillArena();
        FeelerState();
        uint8_t* c = g_physicsArena + kComponent;
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(40);
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 5);
        g_pointerA = ArenaAddress(kScratch);
        std::vector<uint32_t> expected, actual;
        uint32_t seed = Next() | 1;
        switch (which) {
        case 0: {
            uint32_t ignore = Next(), require = Next(); // (only the low halves count)
            SaveStart();
            expected = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x005003A0)(
                    component, ignore, require));
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Ours(component)->CheckFeeler(ignore, require));
            });
            deep[0] += Called(actual, 0x00478F00);
            break;
        }
        case 1: {
            auto* params = reinterpret_cast<Script::CStruct*>(g_physicsArena + kScratch);
            SaveStart();
            expected = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Original<int(__thiscall*)(void*, void*, void*)>(0x005012B0)(
                    component, params, reinterpret_cast<void*>(0x1234)));
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(
                    Ours(component)->NearGround(params, reinterpret_cast<Script::CScript*>(0x1234)));
            });
            deep[1] += Called(actual, 0x004172F0);
            break;
        }
        case 2: {
            // One side: its own and the opposite side's times (the clock stand-in's values are around 0..1000).
            uint32_t side = Next() % 4;
            for (int i = 0; i < 4; ++i)
                ArenaAt<int>(kComponent + 0x328 + 4 * i) = PickInt();
            ArenaAt<float>(kComponent + 0x2BC) = RandomFloat(3.0f);
            Mth::Vector* start = &ArenaAt<Mth::Vector>(kScratch + 0x100);
            Mth::Vector* end = &ArenaAt<Mth::Vector>(kScratch + 0x110);
            SaveStart();
            expected = RunOnArena(seed, [=](uint8_t* component) {
                Original<void(__thiscall*)(void*, void*, void*, uint32_t)>(0x00501C90)(component, start, end, side);
                return 0u;
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                Ours(component)->CheckSideFeeler(start, end, side);
                return 0u;
            });
            deep[2] += Called(actual, 0x004172F0);
            break;
        }
        case 3: {
            if (RandomBool(10))
                ArenaAt<float>(kComponent + 0x2C0) = 0.0f;
            if (RandomBool(10))
                ArenaAt<float>(kComponent + 0x2C4) = 0.0f;
            for (int i = 0; i < 4; ++i)
                ArenaAt<int>(kComponent + 0x328 + 4 * i) = RandomBool(70) ? 0 : PickInt();
            if (RandomBool(20)) // (side reaches with bits a rounding can lose)
                ArenaAt<uint32_t>(kComponent + 0x2C0) = 0x3F800000u | (Next() & 0x7FFFFF);
            // A matrix with full-precision rows sometimes.
            if (RandomBool(30)) {
                for (uint32_t i = 0x6C; i < 0xAC; i += 4)
                    ArenaAt<uint32_t>(kObject + i) = 0x3E000000u | (Next() & 0x00FFFFFF);
            }
            SaveStart();
            uint32_t display[16];
            std::memcpy(display, g_physicsArena + kComponent + 0x18, sizeof(display));
            expected = RunOnArena(seed, [](uint8_t* component) {
                Original<void(__thiscall*)(void*)>(0x005055B0)(component);
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t* component) {
                Ours(component)->CheckSideFeelers();
                return 0u;
            });
            {
                // (turned: the display matrix changed)
                size_t at = actual.size() - kArenaSize / 4 + 0x18 / 4;
                bool turned = false;
                for (uint32_t i = 0; i < 16; ++i)
                    turned |= actual[at + i] != display[i];
                deep[3] += turned;
            }
            break;
        }
        case 4:
            if (RandomBool(30)) {
                for (uint32_t i = 0x4C; i < 0xAC; i += 4)
                    ArenaAt<uint32_t>(kObject + i) = 0x3F000000u | (Next() & 0x00FFFFFF);
            }
            SaveStart();
            expected = RunOnArena(seed, [](uint8_t* component) {
                Original<void(__thiscall*)(void*)>(0x005009C0)(component);
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t* component) {
                Ours(component)->FindNearestWallSide();
                return 0u;
            });
            deep[4] += actual[actual.size() - kArenaSize / 4 + 0x2D4 / 4] != 0; // (a side found)
            break;
        case 5: {
            // Rail nodes in the scratch area: the rail, its neighbours, the last one and its previous one.
            uint32_t rail = kScratch, previous = kScratch + 0x60, next = kScratch + 0xC0, last = kScratch + 0x120,
                     lastPrevious = kScratch + 0x180, park = kScratch + 0x200;
            ArenaAt<uint32_t>(rail + 0x40) = RandomBool(85) ? ArenaAddress(previous) : 0;
            ArenaAt<uint32_t>(rail + 0x44) = ArenaAddress(next);
            ArenaAt<uint32_t>(last + 0x40) = RandomBool(85) ? ArenaAddress(lastPrevious) : 0;
            static const uint32_t lasts[] = {0, kScratch, kScratch + 0x60, kScratch + 0xC0, kScratch + 0x120,
                                             kScratch + 0x120, kScratch + 0x120};
            uint32_t chosen = lasts[Next() % 7];
            ArenaAt<uint32_t>(kComponent + 0x22C) = chosen ? ArenaAddress(chosen) : 0;
            ArenaAt<uint32_t>(park + 0x40) = RandomBool(25) ? 0xD443A2BCu : Next();
            GameVariable<uint8_t*>(0x007CE5D8) = g_physicsArena + park;
            ArenaAt<int>(kFlags + 0x18) = RandomBool(40) ? 1 : static_cast<int>(Next() % 5);
            ArenaAt<int>(kComponent + 0x234) = PickInt();
            ArenaAt<int>(kComponent + 0x23C) = PickInt();
            if (RandomBool(20))
                ArenaAt<float>(kObject + 0xB0) = 0.0f;
            bool anyTime = RandomBool(30);
            const void* node = g_physicsArena + rail;
            SaveStart();
            expected = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, const void*, const void*, bool)>(0x00500020)(
                        component, node, reinterpret_cast<void*>(0x5678), anyTime));
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(
                    Ours(component)->CanGrindRail(node, reinterpret_cast<void*>(0x5678), anyTime));
            });
            deep[5] += Called(actual, 0x0057CB60);
            break;
        }
        }
        (void)c;
        static const char* const names[] = {"CheckFeeler", "NearGround", "CheckSideFeeler", "CheckSideFeelers",
                                            "FindNearestWallSide", "CanGrindRail"};
        same = Compare(expected, actual, names[which], variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    GameVariable<uint8_t*>(0x007CE5D8) = savedPark;
    bool covered = true;
    for (int i = 0; i < 6; ++i) {
        if (deep[i] < 60) {
            Log("  physics feelers coverage: case %d reached its deep call %d times", i, deep[i]);
            covered = false;
        }
    }
    Check(same && covered, "skater physics: feelers (CheckFeeler, NearGround, side collisions, wall side, rails)");
}

// CallMemberFunction (Ledge, Obj_MoveToNode with NoReset / orient, others), ApplyModelRotations.
void TestSkaterPhysicsMembers()
{
    g_rng = 5;
    StandIns standIns(FeelerStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    bool same = true;
    int moved = 0, rotated = 0;
    for (int variant = 0; variant < 4000 && same; ++variant) {
        int which = variant % 2;
        g_rng = 0x6C8E9CF5u * (variant + 1);
        FillArena();
        // A stand-in component (the motion, shadow, model... - one for all) with a vtable whose slot 12 is recorded.
        uint32_t component = kScratch, vtable = kScratch + 0x300;
        ArenaAt<uint32_t>(component) = ArenaAddress(vtable);
        ArenaAt<const void*>(vtable + 12 * 4) = reinterpret_cast<const void*>(&FakeCallMember);
        g_pointerA = ArenaAddress(component);
        g_pointerB = RandomBool(50) ? ArenaAddress(kScratch + 0x400) : 0;
        ArenaAt<uint8_t>(kObject + 0xBC) = RandomBool(60);
        ArenaAt<uint8_t>(kObject + 0x110) = RandomBool(50);
        std::vector<uint32_t> expected, actual;
        uint32_t seed = Next() | 1;
        if (which == 0) {
            static const uint32_t names[] = {0x315D9ED4, 0x8819DD8B, 0x8819DD8B, 0x8819DD8B, 0x12345678};
            uint32_t name = names[Next() % 5];
            auto* params = reinterpret_cast<Script::CStruct*>(g_physicsArena + kScratch + 0x500);
            SaveStart();
            expected = RunOnArena(seed, [=](uint8_t* c) {
                return static_cast<uint32_t>(Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x00501600)(
                    c, name, params, reinterpret_cast<void*>(0x1234)));
            });
            actual = RunOnArena(seed, [=](uint8_t* c) {
                return static_cast<uint32_t>(
                    Ours(c)->CallMemberFunction(name, params, reinterpret_cast<Script::CScript*>(0x1234)));
            });
            moved += Called(actual, 0x0045E4C0);
        } else {
            for (uint32_t flag : {0xC8u, 0x15Cu, 0x1F0u})
                ArenaAt<uint8_t>(component + flag) = RandomBool(30);
            SaveStart();
            expected = RunOnArena(seed, [](uint8_t* c) {
                return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x005054A0)(c));
            });
            actual = RunOnArena(seed, [](uint8_t* c) { return static_cast<uint32_t>(Ours(c)->ApplyModelRotations()); });
            rotated += Called(actual, 0x00420B50);
        }
        same = Compare(expected, actual, which ? "ApplyModelRotations" : "CallMemberFunction", variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    bool covered = moved > 300 && rotated > 200;
    if (!covered)
        Log("  physics members coverage: moved %d, rotated %d", moved, rotated);
    Check(same && covered, "skater physics: CallMemberFunction, ApplyModelRotations");
}

// TryStartSkitch (the nearest skitch point of a list of components), Skitch (moving along, held, pulled, let go).
void TestSkaterPhysicsSkitch()
{
    g_rng = 9;
    StandIns standIns(FeelerStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    bool same = true;
    int started = 0, moving = 0, held = 0;
    for (int variant = 0; variant < 9000 && same; ++variant) {
        int which = variant % 2;
        g_rng = 0x1B873593u * (variant + 1);
        FillArena();
        g_fixedOut = RandomBool(30);
        static const int indexes[] = {-1, 0, 1, 2};
        g_skitchIndex = static_cast<uint32_t>(indexes[Next() % 4]);
        std::vector<uint32_t> expected, actual;
        uint32_t seed = Next() | 1;
        if (which == 0) {
            // Up to four skitch components (+0x14 the next, +0x18 on, +0xC the object - sometimes the skater).
            int count = static_cast<int>(Next() % 5);
            g_pointerB = count ? ArenaAddress(kScratch) : 0;
            for (int i = 0; i < count; ++i) {
                uint32_t at = kScratch + i * 0x40;
                ArenaAt<uint32_t>(at + 0x14) = i + 1 < count ? ArenaAddress(at + 0x40) : 0;
                ArenaAt<uint8_t>(at + 0x18) = RandomBool(80);
                ArenaAt<uint32_t>(at + 0xC) = RandomBool(15) ? ArenaAddress(kObject) : ArenaAddress(kScratch + 0x400 + i * 0x100);
            }
            ArenaAt<uint8_t>(kComponent + 0x2ED) = RandomBool(10);
            ArenaAt<uint8_t>(kFlags + 0x88) = RandomBool(10);
            ArenaAt<uint8_t>(kInput + 0x20) = RandomBool(85);
            ArenaAt<int>(kFlags + 0x18) = RandomBool(80) ? 0 : 1;
            if (g_fixedOut && RandomBool(50)) { // (5 from every point at 1, 2, 3: a tie)
                ArenaAt<float>(kObject + 0x4C) = 4.0f;
                ArenaAt<float>(kObject + 0x50) = 6.0f;
                ArenaAt<float>(kObject + 0x54) = 3.0f;
            }
            SaveStart();
            expected = RunOnArena(seed, [](uint8_t* c) {
                Original<void(__thiscall*)(void*)>(0x00503EC0)(c);
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t* c) {
                Ours(c)->TryStartSkitch();
                return 0u;
            });
            started += Called(actual, 0x0041D1D0);
        } else {
            uint32_t rider = kRider;
            if (RandomBool(10))
                ArenaAt<uint32_t>(kRide + 0x18) = 0;
            else if (RandomBool(10))
                ArenaAt<uint32_t>(rider) = 0;
            g_pointerA = ArenaAddress(kScratch);
            for (uint32_t button : {0x90u, 0xF0u, 0xB0u, 0x110u})
                ArenaAt<uint8_t>(kInput + 0x20 + button) = RandomBool(20);
            ArenaAt<uint8_t>(kComponent + 0x24A) = RandomBool(40);
            ArenaAt<float>(kComponent + 0x250) = RandomBool(50) ? 1.0f : -1.0f;
            ArenaAt<int>(kComponent + 0x24C) = RandomBool(30) ? static_cast<int>(g_skitchIndex) : static_cast<int>(Next() % 3) - 1;
            ArenaAt<uint8_t>(kFlags + 0x88) = RandomBool(85);
            ArenaAt<float>(kComponent + 0x8C) = (Next() % 2) ? 1.0f / 60.0f : RandomFloat(3.0f);
            // (the ride's move: big sometimes, so the skater ends up far from the point)
            for (uint32_t i = 0; i < 16; i += 4)
                ArenaAt<float>(kRider + 0x5C + i) = RandomBool(20) ? RandomFloat(300.0f) : RandomFloat(3.0f);
            if (g_fixedOut && RandomBool(60)) {
                // (the point at 1, 2, 3; no ride move: the skater 1 away - a speed of 1 a frame arrives exactly - or
                // just over 180 away)
                for (uint32_t i = 0; i < 16; i += 4)
                    ArenaAt<float>(kRider + 0x5C + i) = 0.0f;
                ArenaAt<float>(kObject + 0x4C) = RandomBool(50) ? 0.0f : -179.001f;
                ArenaAt<float>(kObject + 0x50) = 2.0f;
                ArenaAt<float>(kObject + 0x54) = 3.0f;
                ArenaAt<float>(kComponent + 0x8C) = 1.0f;
            }
            SaveStart();
            expected = RunOnArena(seed, [](uint8_t* c) {
                Original<void(__thiscall*)(void*)>(0x00501DB0)(c);
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t* c) {
                Ours(c)->Skitch();
                return 0u;
            });
            moving += Called(actual, 0x0051BA70);
            held += Called(actual, 0x0051BC70);
        }
        same = Compare(expected, actual, which ? "Skitch" : "TryStartSkitch", variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    g_fixedOut = false;
    bool covered = started > 200 && moving > 200 && held > 600;
    if (!covered)
        Log("  physics skitch coverage: started %d, moving %d, held %d", started, moving, held);
    Check(same && covered, "skater physics: TryStartSkitch, Skitch");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsFeelers);
SELFTEST(Early, TestSkaterPhysicsMembers);
SELFTEST(Early, TestSkaterPhysicsSkitch);
