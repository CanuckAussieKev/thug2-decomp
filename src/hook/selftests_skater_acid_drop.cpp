// Self-tests for the skater core physics component's acid drop (core_physics_acid_drop.cpp): original vs ours on a
// stand-in component, every callee into game code replaced by a recording stand-in for the length of the test (the
// math functions pointed at ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h"

using namespace SkaterPhysicsTest;
using Skater::AcidDrop;
using Skater::CorePhysicsComponent;

namespace {

CorePhysicsComponent* Ours(uint8_t* component) { return reinterpret_cast<CorePhysicsComponent*>(component); }

uint32_t g_pointer; // what Ret::Fixed stand-ins return

// (the copy of the drop's horizontal velocity kept at 0x007CCDBC)
void RecordCopy()
{
    for (int i = 0; i < 4; ++i)
        RecordValue((&GameVariable<uint32_t>(0x007CCDBC))[i]);
}

std::vector<StandIn> AcidDropStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0}, // the clock
        {0x00478F00, 0, 1, Ret::Float, false, 0, -1, -1, 0,
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0}, // PhysicsFloat
        {0x004FF470, 0, 0, Ret::Double, true, 0, -1, -1, 0}, // the gravity
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 18, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x50, 0, 8}, // (the line recorded; the hit's flags, distance, point and normal filled in)
        {0x00417210, 0, 0, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&Nx::CFeeler::HitObjectAlive)},
        {0x0040AB50, 0, 0, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Nx::CCollCache::Construct)},
        {0x0040ABA0, 4, 1, Ret::None, true, 0, 0, -1, 8, CodeAddress(&Nx::CCollCache::Update)}, // (the box recorded)
        {0x0040AB40, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Nx::CCollCache::Destruct)},
        {0x0043DAA0, 0, 0, Ret::Double, true, 0, -1, -1, 0}, // (the walk's speed)
        {0x0045DB90, 4, 1, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Obj::CCompositeObject::GetComponent),
         &g_pointer},
        {0x005BA3D0, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Pool::Allocate), &g_pointer},
        {0x005BAFC0, 12, 3, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Manager::Allocate), &g_pointer},
        {0x00476410, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Init), &g_pointer},
        {0x00477C60, 8, 2, Ret::None, true, 0, -1, -1, 0,
         CodeAddress(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat)), nullptr, 2},
        {0x00461480, 20, 5, Ret::Int, true, 12, -1, -1, 0, CodeAddress(&Obj::CObject::SpawnAndRunScript)},
        {0x00477130, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Clear)},
        {0x00409070, 0, 1, Ret::None, false, 0, -1, -1, 0, CodeAddress(&Script::DeleteStruct)},
        {0x004607A0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::ClearEventHandler)},
    };
}

void TestSkaterPhysicsAcidDrop()
{
    g_rng = 19;
    StandIns standIns(AcidDropStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    uint8_t savedPools[16];
    std::memcpy(savedPools, &GameVariable<uint8_t>(0x006F3614), sizeof(savedPools));
    bool savedReady = GameVariable<bool>(0x007CC979);
    GameVariable<bool>(0x007CC979) = true;
    Mth::Vector savedCopy = GameVariable<Mth::Vector>(0x007CCDBC);
    bool same = true;
    int found = 0, searched = 0, started = 0;
    for (int variant = 0; variant < 20000 && same; ++variant) {
        bool start = variant % 4 == 3;
        g_rng = 0x1F123BB5u * (variant + 1);
        FillArena();
        g_fixedOut = false;
        g_pointer = ArenaAddress(kScratch + 0x400);
        for (uint32_t flag = 0x20; flag < 0xF8; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(50);
        // The walking component (+0x1530's +0x2C) and walking state (+0x1534).
        ArenaAt<uint8_t>(kWalk + 0x118) = RandomBool(20);
        ArenaAt<int>(kWalk + 0x1C) = static_cast<int>(Next() % 3);
        ArenaAt<uint32_t>(kMisc2 + 0x20 + 0x18) = Next() % 2;
        // Velocities mostly fast and horizontal, sometimes still; positions apart.
        float* velocity = &ArenaAt<float>(kObject + 0xAC);
        velocity[0] = RandomBool(10) ? 0.0f : RandomFloat(200.0f);
        velocity[1] = RandomFloat(100.0f);
        velocity[2] = RandomFloat(200.0f);
        ArenaAt<float>(kObject + 0x5C + 4) = RandomFloat(10.0f);
        // (the script pools for the parameters: the current one's pool there or not)
        GameVariable<int>(0x006F3614) = static_cast<int>(Next() % 2);
        GameVariable<uint32_t>(0x006F361C) = RandomBool(50) ? ArenaAddress(kScratch + 0x300) : 0;
        GameVariable<uint32_t>(0x006F3620) = RandomBool(50) ? ArenaAddress(kScratch + 0x340) : 0;
        if (RandomBool(30)) { // (full-mantissa vectors)
            for (uint32_t i = 0x4C; i < 0xBC; i += 4)
                ArenaAt<uint32_t>(kObject + i) = (Next() & 0x80000000u) | 0x3F000000u | (Next() & 0x00FFFFFF);
        }
        switch (Next() % 10) { // (values at the thresholds)
        case 0: // (moving at exactly the least speed)
            velocity[0] = 0.01f;
            velocity[2] = 0.0f;
            break;
        case 1: // (no horizontal speed)
            velocity[0] = 0.0f;
            velocity[2] = 0.0f;
            break;
        default:
            break;
        }
        AcidDrop* drop = &ArenaAt<AcidDrop>(kScratch + 0x200);
        if (start) {
            ArenaAt<float>(kScratch + 0x214) = RandomBool(50) ? RandomFloat(1.0f) : 0.5f + RandomFloat(0.4f);
            if (RandomBool(10)) { // (a floor: the drop's direction flat)
                ArenaAt<float>(kScratch + 0x210) = 0.0f;
                ArenaAt<float>(kScratch + 0x214) = 1.0f;
                ArenaAt<float>(kScratch + 0x218) = 0.0f;
            }
        }
        bool pop = RandomBool(50);
        SaveStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        if (!start) {
            const Mth::Vector* position = &ArenaAt<Mth::Vector>(kObject + 0x4C);
            const Mth::Vector* last = &ArenaAt<Mth::Vector>(kObject + 0x5C);
            Mth::Vector* v = &ArenaAt<Mth::Vector>(kObject + 0xAC);
            expected = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, bool, const void*, const void*, void*, void*)>(0x00502240)(
                        component, pop, position, last, v, drop));
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                return static_cast<uint32_t>(Ours(component)->FindAcidDrop(pop, position, last, v, drop));
            });
            searched += Called(actual, 0x0040ABA0);
            found += actual[0] != 0;
        } else {
            expected = RunOnArena(seed, [=](uint8_t* component) {
                GameVariable<Mth::Vector>(0x007CCDBC) = {};
                Original<void(__thiscall*)(void*, const void*)>(0x00502F20)(component, drop);
                RecordCopy();
                return 0u;
            });
            actual = RunOnArena(seed, [=](uint8_t* component) {
                GameVariable<Mth::Vector>(0x007CCDBC) = {};
                Ours(component)->StartAcidDrop(drop);
                RecordCopy();
                return 0u;
            });
            started += Called(actual, 0x00461480);
        }
        same = Compare(expected, actual, start ? "StartAcidDrop" : "FindAcidDrop", variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    std::memcpy(&GameVariable<uint8_t>(0x006F3614), savedPools, sizeof(savedPools));
    GameVariable<bool>(0x007CC979) = savedReady;
    GameVariable<Mth::Vector>(0x007CCDBC) = savedCopy;
    g_fixedOut = false;
    bool covered = searched > 5000 && found > 30 && started > 3000;
    if (!covered)
        Log("  physics acid drop coverage: searched %d, found %d, started %d", searched, found, started);
    Check(same && covered, "skater physics: FindAcidDrop, StartAcidDrop");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsAcidDrop);
