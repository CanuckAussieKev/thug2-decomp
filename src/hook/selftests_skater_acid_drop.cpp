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

// Variants set up at a threshold: the facing exactly at its limit (walking or not), the hit at the skater's own height
// (the arc's peak equal to it), the search reach exactly one of the search's steps.
enum class Edge { None, Facing, WalkFacing, Level, Reach };
Edge g_edge = Edge::None;
float g_reach;
std::vector<float> g_exactSteps; // the search's steps that are floats exactly

// GetCollision's extra: whether the feeler goes through the cache; at an edge, a vert wall (normal (1, 0.5, 0)) - at the
// skater's height for Level.
void AfterGetCollision(uint8_t* feeler, uint32_t*)
{
    RecordValue(*reinterpret_cast<uint32_t*>(feeler + 0xAC) != 0);
    if (g_edge == Edge::None || g_edge == Edge::Reach)
        return;
    *reinterpret_cast<uint16_t*>(feeler + 0x50) |= 8;
    float* normal = reinterpret_cast<float*>(feeler + 0x88);
    normal[0] = 1.0f;
    normal[1] = 0.5f;
    normal[2] = 0.0f;
    normal[3] = 0.0f;
    if (g_edge == Edge::Level)
        *reinterpret_cast<float*>(feeler + 0x7C) = ArenaAt<float>(kObject + 0x50);
}

float PickScriptFloat() { return g_edge == Edge::Reach ? g_reach : PickFloat(); }

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
         CodeAddress(static_cast<float (*)(uint32_t)>(&Script::GetFloat)), nullptr, 0, 0, -1, -1, 0, 4.0f,
         &PickScriptFloat},
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0}, // PhysicsFloat
        {0x004FF470, 0, 0, Ret::Double, true, 0, -1, -1, 0}, // the gravity
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 18, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x50, 0, 8, 4.0f, nullptr, &AfterGetCollision}, // (the line recorded; the hit's flags, distance, point and normal
                                                         // filled in)
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
    int found = 0, searched = 0, started = 0, edges = 0;
    if (g_exactSteps.empty()) { // (the search's steps, as FindAcidDrop takes them)
        float step = 0.01f;
        for (double next = step; next < 3000.0; step = static_cast<float>(next)) {
            next = static_cast<double>(step) + (step > 100.0f ? 24.0 : 0.0) + 3.0;
            if (static_cast<double>(static_cast<float>(next)) == next)
                g_exactSteps.push_back(static_cast<float>(next));
        }
    }
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
        g_edge = start ? Edge::None : static_cast<Edge>(Next() % 8 < 4 ? Next() % 5 : 0);
        if (g_edge == Edge::Facing || g_edge == Edge::WalkFacing) { // (looking along the walk's direction)
            ArenaAt<uint8_t>(kWalk + 0x118) = g_edge == Edge::Facing;
            ArenaAt<int>(kWalk + 0x1C) = 1;
            ArenaAt<uint32_t>(kMisc2 + 0x20 + 0x18) = 1;
            ArenaAt<Mth::Vector>(kMisc2 + 0x20 + 0x48) = {g_edge == Edge::Facing ? 0.05f : -0.25f, 0.0f, 0.0f, 0.0f};
        } else if (g_edge == Edge::Reach && !g_exactSteps.empty()) { // (walking on the ground: the reach a script value)
            ArenaAt<uint8_t>(kWalk + 0x118) = 0;
            ArenaAt<int>(kWalk + 0x1C) = 1;
            ArenaAt<uint32_t>(kMisc2 + 0x20 + 0x18) = 0;
            g_reach = g_exactSteps[Next() % (g_exactSteps.size() < 12 ? g_exactSteps.size() : 12)];
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
            edges += g_edge != Edge::None && actual[0] != 0;
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
    g_edge = Edge::None;
    bool covered = searched > 5000 && found > 30 && started > 3000 && edges > 30;
    if (!covered)
        Log("  physics acid drop coverage: searched %d, found %d, started %d, at an edge %d", searched,
            found, started, edges);
    Check(same && covered, "skater physics: FindAcidDrop, StartAcidDrop");
}

// --- the spine transfer ---

// Variants at a threshold: the target exactly 24 / 23 from the wall (the distance's and the reach's checks), straight
// across from the skater with a normal along x (a hip's side exactly 0), facing the wall's way out at exactly 0.9 (the
// skater's up flattened to (0.9, 0, 0.436), the target's normal along x).
enum class Spine { None, Far24, Far23, Across, Flat };
Spine g_spine = Spine::None;
float g_wallX, g_wallZ; // (where the wall feeler hit)
int g_finds;             // (0x004FEE10's calls in a run)
bool g_hip;              // (the last one's answer)

void AfterSpineFeeler(uint8_t* feeler, uint32_t*)
{
    g_wallX = *reinterpret_cast<float*>(feeler + 0x78);
    g_wallZ = *reinterpret_cast<float*>(feeler + 0x80);
}

// 0x004FEE10's stand-in: `look` and `normal` recorded; the target, its normal and the two flags made up.
void AfterFindTarget(uint8_t*, uint32_t* args)
{
    for (int arg = 0; arg < 2; ++arg)
        for (int i = 0; i < 4; ++i)
            RecordValue(reinterpret_cast<const uint32_t*>(args[arg])[i]);
    ++g_finds;
    g_hip = RandomBool(50);
    *reinterpret_cast<uint8_t*>(args[2]) = g_hip;
    *reinterpret_cast<uint8_t*>(args[5]) = RandomBool(50);
    float* target = reinterpret_cast<float*>(args[3]);
    float* normal = reinterpret_cast<float*>(args[4]);
    for (int i = 0; i < 4; ++i) {
        target[i] = RandomFloat(RandomBool(50) ? 30.0f : 3.0f);
        normal[i] = RandomFloat(1.0f);
    }
    switch (g_spine) {
    case Spine::Far24:
    case Spine::Far23:
        target[0] = g_wallX + (g_spine == Spine::Far24 ? 24.0f : 23.0f);
        target[2] = g_wallZ;
        break;
    case Spine::Flat:
        normal[0] = 1.0f;
        normal[2] = 0.0f;
        break;
    case Spine::Across:
        target[0] = ArenaAt<float>(kObject + 0x4C);
        normal[0] = 1.0f;
        normal[1] = RandomBool(50) ? 0.5f : 1.0f;
        normal[2] = 0.0f;
        break;
    default:
        break;
    }
}

std::vector<StandIn> SpineStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0}, // the clock
        {0x004FF470, 0, 0, Ret::Double, true, 0, -1, -1, 0}, // the gravity
        {0x004172F0, 8, 2, Ret::Bool, true, 3, -1, -1, 18, CodeAddress(&Nx::CFeeler::GetCollision), nullptr, 0, 0,
         0x50, 0, 8, 6.0f, nullptr, &AfterSpineFeeler}, // (the line recorded; the hit filled in)
        {0x004FEE10, 0x18, 6, Ret::Bool, true, 0, -1, -1, 0, nullptr, nullptr, 0, 0, -1, -1, 0, 4.0f, nullptr,
         &AfterFindTarget},
        {0x00461480, 20, 5, Ret::Int, true, 12, -1, -1, 0, CodeAddress(&Obj::CObject::SpawnAndRunScript)},
        {0x004607A0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::ClearEventHandler)},
    };
}

void TestSkaterPhysicsSpineTransfer()
{
    StandIns standIns(SpineStandIns());
    OurMath math;
    int savedOffset = GameVariable<int>(0x00696D60);
    GameVariable<int>(0x00696D60) = 0;
    bool same = true;
    int transferred = 0, blocked = 0, hips = 0, tooSlow = 0, turned = 0;
    for (int variant = 0; variant < 30000 && same; ++variant) {
        g_rng = 0x2B5E61F3u * (variant + 1);
        FillArena();
        for (uint32_t flag = 0x20; flag < 0x100; flag += 8)
            ArenaAt<uint8_t>(kFlags + flag) = RandomBool(50);
        ArenaAt<uint8_t>(kInput + 0x60) = RandomBool(50);
        ArenaAt<uint8_t>(kInput + 0x80) = RandomBool(50);
        float* velocity = &ArenaAt<float>(kObject + 0xAC);
        for (int i = 0; i < 3; ++i)
            velocity[i] = RandomFloat(RandomBool(50) ? 2000.0f : 50.0f);
        switch (Next() % 8) { // (still; an up with no horizontal part)
        case 0:
            velocity[0] = velocity[1] = velocity[2] = 0.0f;
            break;
        case 1:
            ArenaAt<float>(kObject + 0x7C) = 0.0f;
            ArenaAt<float>(kObject + 0x84) = 0.0f;
            break;
        default:
            break;
        }
        if (RandomBool(30)) { // (full-mantissa vectors)
            for (uint32_t i = 0x4C; i < 0xBC; i += 4)
                ArenaAt<uint32_t>(kObject + i) = (Next() & 0x80000000u) | 0x3F000000u | (Next() & 0x00FFFFFF);
        }
        g_spine = static_cast<Spine>(Next() % 6 < 3 ? Next() % 5 : 0);
        if (g_spine == Spine::Flat) {
            ArenaAt<float>(kObject + 0x7C) = 0.9f;
            ArenaAt<float>(kObject + 0x84) = RandomBool(50) ? 0.43588989f : -0.43588989f;
        }
        SaveStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = RunOnArena(seed, [](uint8_t* component) {
            return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00505F70)(component));
        });
        std::vector<uint32_t> actual = RunOnArena(seed, [](uint8_t* component) {
            g_finds = 0;
            return static_cast<uint32_t>(Ours(component)->TrySpineTransfer());
        });
        bool spawned = Called(actual, 0x00461480);
        transferred += spawned;
        hips += spawned && g_hip;
        blocked += actual[0] && !spawned;
        tooSlow += !actual[0] && Called(actual, 0x004FF470);
        turned += spawned && g_finds > 1;
        same = Compare(expected, actual, "TrySpineTransfer", variant);
    }
    GameVariable<int>(0x00696D60) = savedOffset;
    g_spine = Spine::None;
    bool covered = transferred > 1000 && blocked > 500 && hips > 300 && tooSlow > 300 && turned > 100;
    if (!covered)
        Log("  physics spine transfer coverage: transferred %d (hips %d, along the wall %d), blocked %d, too slow %d",
            transferred, hips, turned, blocked, tooSlow);
    Check(same && covered, "skater physics: TrySpineTransfer");
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsAcidDrop);
SELFTEST(Early, TestSkaterPhysicsSpineTransfer);
