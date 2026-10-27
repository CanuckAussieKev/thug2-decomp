// Self-tests for the skater's adjust-physics component's life and frame (adjust_physics_component.cpp), the feeler's
// copy constructor and the hit record's copy: original vs ours on stand-in components (skater_core_testkit.h), every
// callee into game code stood in for.

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/nx/coll_line.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/walk_hang.h"
#include "game/skater/adjust_physics.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Obj::CSkaterAdjustPhysicsComponent;

constexpr uint32_t kAdjust = kFree;              // the component (0x34 bytes)
constexpr uint32_t kGround = kFree + 0x40;       // a feeler (0xB0 bytes)
constexpr uint32_t kCopy = kFree + 0x100;        // a feeler copied into
constexpr uint32_t kModelArea = kFree + 0x1C0;
constexpr uint32_t kHitObject = kFree + 0x200;   // an object hit (smart pointers to it: stand-ins)
constexpr uint32_t kHits = kFree + 0x240;        // two hit records (0x50 bytes each)

std::vector<StandIn> AdjustStandIns(bool frame)
{
    std::vector<StandIn> list = {
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kAdjust)) : 0; }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kFree + 0x300 + (f.args[0] & 0x3C))); }},
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(Tmr::GameClockElapsed)}},
        {0x00427B30, 8, 2, Ret::Int, true, 0, {Ours(&Obj::CMovableContactComponent::FindContactAhead)},
         [](Frame&, Result& r) { r.eax = Next() % 3 ? reinterpret_cast<uint32_t>(ArenaPtr(kHitObject)) : 0; }},
        {0x00401C30, 0, 4, Ret::Int, false, 0, {Ours(Dbg::Printf)}},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result&) {
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(reinterpret_cast<const float*>(&feeler->m_line)[i]);
             Record(feeler->m_hit.ignoreFlags);
             Record(feeler->m_hit.requireFlags);
             feeler->m_point = {RandomFloat(50.0f), RandomFloat(50.0f), RandomFloat(50.0f), 1.0f};
         }},
        {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
        {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kHitObject)); }},
        {0x0043EB80, 4, 1, Ret::None, true, 0, {Ours(&Walk::MovableContact::StartRiding)}},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x004637C0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::AddSmartPointer)}},
        {0x00420670, 12, 3, Ret::None, true, 2, {Ours(&Obj::CModelComponent::ApplyBrightness)},
         [](Frame& f, Result&) {
             // (which feeler, and its line)
             const float* line = reinterpret_cast<const float*>(f.args[0]);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(line[i]);
         }},
    };
    if (frame) { // (FrameUpdate's two callees: tested on their own)
        list.push_back({0x004F8AA0, 0, 0, Ret::None, true, 0, {Ours(&CSkaterAdjustPhysicsComponent::CheckInsideMovingObject)}});
        list.push_back({0x004F8420, 0, 0, Ret::None, true, 0, {Ours(&CSkaterAdjustPhysicsComponent::Update)}});
    }
    return list;
}

void RunAdjustPhysicsTest(bool frame)
{
    StandIns standIns(AdjustStandIns(frame));
    ClockOffsetZero clock;
    GameVariable<int>(0x00696D60) = 123457; // (the clock's offset counts: restored by `clock`)
    bool same = true;
    int counts[6] = {};
    for (int variant = 0; variant < 6000 && same; ++variant) {
        g_rng = 0x1F123BB5u * (variant + 1);
        FillArena();
        auto* adjust = reinterpret_cast<CSkaterAdjustPhysicsComponent*>(ArenaPtr(kAdjust));
        ArenaAt<uint8_t*>(kAdjust + 0x0C) = ArenaPtr(kObject);
        ArenaAt<uint8_t*>(kAdjust + 0x20) = ArenaPtr(kComponent);
        ArenaAt<uint8_t*>(kAdjust + 0x24) = ArenaPtr(kModelArea);
        ArenaAt<uint8_t*>(kAdjust + 0x2C) = ArenaPtr(kRide);
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
        ArenaAt<uint8_t>(kFlags + 0x60) = RandomBool(50);
        ArenaAt<uint8_t>(kFlags + 0x88) = RandomBool(30);
        Vector& pos = ArenaAt<Vector>(kObject + 0x4C);
        if (Next() % 3 == 0)
            ArenaAt<Vector>(kComponent + 0x98) = pos;
        if (Next() % 3 == 0)
            ArenaAt<float>(kObject + 0xB0) = Next() % 2 ? 0.0f : -0.0f;
        // the ground feeler: a line, a hit record, an object hit or none
        auto* ground = reinterpret_cast<Nx::CFeeler*>(ArenaPtr(kGround));
        ArenaAt<uint8_t*>(kGround + 0x98) = Next() % 2 ? ArenaPtr(kHitObject) : nullptr;
        for (uint32_t i = 0x48; i < 0x50; ++i)
            ArenaAt<uint8_t>(kGround + 0x24 + i) = static_cast<uint8_t>(Next());
        int op = frame ? 1 : static_cast<int>(Next() % 5);
        if (!frame && op == 1)
            op = 5;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto runOp = [&](bool o) -> uint32_t {
            switch (op) {
            case 0: {
                uint32_t made = o ? Original<uint32_t(__cdecl*)()>(0x004F80E0)()
                                  : reinterpret_cast<uint32_t>(CSkaterAdjustPhysicsComponent::Create());
                Record(made);
                if (made) {
                    ArenaAt<uint8_t*>(kAdjust + 0x0C) = ArenaPtr(kObject);
                    if (o)
                        Original<void(__thiscall*)(void*)>(0x004F8080)(adjust);
                    else
                        adjust->Finalize();
                }
                uint32_t flags = Next() % 2;
                return reinterpret_cast<uint32_t>(
                    o ? Original<void*(__thiscall*)(void*, unsigned int)>(0x004F8050)(adjust, flags)
                      : adjust->Destroy(flags));
            }
            case 1:
                if (o)
                    Original<void(__thiscall*)(void*)>(0x004F8C40)(adjust);
                else
                    adjust->FrameUpdate();
                return 1;
            case 2:
                if (o)
                    Original<void(__thiscall*)(void*)>(0x004F8AA0)(adjust);
                else
                    adjust->CheckInsideMovingObject();
                return 1;
            case 3:
                if (o)
                    Original<void(__thiscall*)(void*, void*)>(0x004F82F0)(adjust, ground);
                else
                    adjust->UpdateBrightness(ground);
                return 1;
            case 4:
                return reinterpret_cast<uint32_t>(
                    o ? Original<void*(__thiscall*)(void*, const void*)>(0x004F8200)(ArenaPtr(kCopy), ground)
                      : reinterpret_cast<Nx::CFeeler*>(ArenaPtr(kCopy))->CopyConstruct(*ground));
            default: {
                auto* to = reinterpret_cast<Nx::CollisionHit*>(ArenaPtr(kHits));
                auto* from = reinterpret_cast<const Nx::CollisionHit*>(ArenaPtr(kHits + 0x50));
                return reinterpret_cast<uint32_t>(
                    o ? Original<void*(__thiscall*)(void*, const void*)>(0x004F8160)(to, from) : to->CopyFrom(*from));
            }
            }
        };
        auto expected = RunOnArena(seed, [&](uint8_t*) { return runOp(true); });
        auto actual = RunOnArena(seed, [&](uint8_t*) { return runOp(false); });
        same = Compare(expected, actual, "adjust physics frame", variant);
        ++counts[op];
    }
    if (frame)
        Check(same, "skater: SkaterAdjustPhysics' frame (0x004F8C40)");
    else
        Check(same, "skater: SkaterAdjustPhysics' create / finalize / destroy, moving-object check and brightness; the "
                    "feeler's copy, the hit record's copy");
}

void TestAdjustPhysicsFrame() { RunAdjustPhysicsTest(true); }
void TestAdjustPhysicsParts() { RunAdjustPhysicsTest(false); }

} // namespace

SELFTEST(Early, TestAdjustPhysicsFrame);
SELFTEST(Early, TestAdjustPhysicsParts);
