// Self-tests for the skater core physics' rail update (core_rail.cpp): original vs ours on a stand-in component and
// a stand-in rail, every callee stood in for (skater_core_testkit.h); the vector functions the original calls pointed
// at ours (as with the hooks installed).

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Skater::CorePhysicsComponent;

constexpr uint32_t kNodes = kFree;              // 6 rail nodes (0x54 each)
constexpr uint32_t kManager = kFree + 0x200;    // the rail manager (+0x44 its node array)
constexpr uint32_t kBalanceTrick = kFree + 0x260; // (+0x18 the kind, +0x7C a balance, +0xAC / +0xB0)
constexpr uint32_t kSkate = kFree + 0x320;      // the skate module (+0x20 the career)
constexpr uint32_t kSkate2 = kFree + 0x340;     // 0x007CE5D8 (+0x40 the level)
constexpr uint32_t kOther1520 = kFree + 0x3A0;  // +0x1520 (+0x18 the score)
constexpr uint32_t kScore = 0x0800;             // the score (+0x154C a float)

int g_findMode; // TransferOrLeaveRail's FindGrindRail: what it finds

UseOurs* RailVectorFunctions()
{
    return new UseOurs({
        {0x00406200, Ours(&Vector::ProjectOnto)},
        {0x004061A0, Ours(&Vector::SetDirection)},
        {0x004023F0, Ours(&Vector::Normalize)},
        {0x00402550, Ours(&Mth::CrossProduct)},
        {0x00402470, Ours(static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator-))},
        {0x00402440, Ours(static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator+))},
        {0x0040AF20, Ours(static_cast<Vector (*)(const Vector&, float)>(&Mth::operator/))},
        {0x00402320, Ours(&Vector::operator=)},
        {0x00406080, Ours(&Vector::ProjectToPlane)},
        {0x00402370, Ours(&Vector::operator*=)},
        {0x00402340, Ours(&Vector::operator+=)},
    });
}

std::vector<StandIn> RailStandIns(bool transferStoodIn)
{
    std::vector<StandIn> list = {
        {0x0044ECE0, 0, 0, Ret::Int, false},
        {0x00580400, 0, 3, Ret::Float, false},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame&, Result& r) {
             static const float values[] = {0.0f, 20.0f, 45.0f, 60.0f, 89.0f, 200.0f, 1000.0f, 2.5f};
             r.st0 = values[Next() % 8];
         }},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x0057CB60, 8, 2, Ret::Int, true, 0, {Ours(&Obj::CRailManager::GetWorldPos)}, [](Frame& f, Result& r) {
             std::memcpy(reinterpret_cast<void*>(f.args[0]), reinterpret_cast<void*>(f.args[1] + 0x20), 16);
             r.eax = f.args[0];
         }},
        {0x0057C600, 0, 8, Ret::Bool, false, 0, {Ours(&Obj::RailClear)}},
        {0x0057CC30, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CRailNode::GetWallSide)},
         [](Frame&, Result& r) { r.eax = static_cast<uint32_t>(static_cast<int>(Next() % 3) - 1); }},
        {0x0057E2D0, 0x1C, 7, Ret::Bool, true, 0, {Ours(&Obj::CRailManager::FindGrindRail)},
         [](Frame& f, Result& r) {
             for (int i = 0; i < 4; ++i) {
                 RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
                 RecordFloat(reinterpret_cast<float*>(f.args[1])[i]);
             }
             float* point = reinterpret_cast<float*>(f.args[2]);
             for (int i = 0; i < 4; ++i)
                 point[i] = RandomFloat(3.0f);
             // what's found: none, a node with a next, the current node, the last node, the node after or before it
             static const int pick[] = {-1, 1, 2, 3, 5, 0, 4};
             int which = pick[g_findMode % 7];
             *reinterpret_cast<uint8_t**>(f.args[3]) = which < 0 ? nullptr : ArenaPtr(kNodes + which * 0x54);
             r.eax = g_findMode % 9 != 8;
         }},
        {0x00427C70, 0, 0, Ret::None, true},
        {0x00416F00, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CRideTracker::Update)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = 0x0C0C0000; }},
        {0x0042BA10, 4, 1, Ret::None, true, 0, {}, [](Frame& f, Result&) {
             std::memset(reinterpret_cast<void*>(f.args[0]), 0, 64);
         }},
        {0x005BFA8C, 12, 3, Ret::Int, false},
        {0x004FFB10, 0, 0, Ret::Bool, true, 0, {}, [](Frame&, Result& r) { r.eax = Next() % 8 == 0; }},
        {0x005092E0, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TriggerRailScript)}},
        {0x00579CC0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBalance::Update)}},
        {0x00584CC0, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CSkaterCareer::GetCheat)}},
        {0x005843A0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSkaterCareer::SetGlobalFlag)}},
        {0x00504730, 0, 0, Ret::None, true},
        {0x0043D990, 0, 4, Ret::Int, false, 0, {}, [](Frame& f, Result& r) {
             const float* a = reinterpret_cast<const float*>(f.args[1]);
             const float* b = reinterpret_cast<const float*>(f.args[2]);
             float* out = reinterpret_cast<float*>(f.args[0]);
             for (int i = 0; i < 4; ++i) {
                 RecordFloat(a[i]);
                 RecordFloat(b[i]);
                 out[i] = a[i] * 0.5f + b[i];
             }
             r.eax = f.args[0];
         }},
        {0x00564270, 4, 1, Ret::None, true},
        {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)},
         [](Frame&, Result& r) { r.eax = 0x51000000u; }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             r.eax = 1;
             *reinterpret_cast<uint32_t*>(f.args[1]) = 0x6000 + Next() % 4;
         }},
        {0x00522370, 4, 1, Ret::None, true},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {}, [](Frame&, Result& r) { r.eax = Next() % 3 == 0; }},
        {0x004FDED0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x004FC470, 4, 1, Ret::None, true},
    };
    if (transferStoodIn)
        list.push_back({0x0050C200, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TransferOrLeaveRail)},
                        [](Frame& f, Result&) {
                            for (int i = 0; i < 4; ++i)
                                RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
                            if (Next() % 2) // (left the rail - or anything else)
                                ArenaAt<int>(kFlags + 0x18) = Next() % 2 ? 1 : 3;
                        }});
    return list;
}

struct RailGlobals {
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    uint8_t* skate2 = GameVariable<uint8_t*>(0x007CE5D8);
    uint8_t flag = GameVariable<uint8_t>(0x007CE4FD);
    ~RailGlobals()
    {
        GameVariable<uint8_t*>(0x007CE478) = skate;
        GameVariable<uint8_t*>(0x007CE5D8) = skate2;
        GameVariable<uint8_t>(0x007CE4FD) = flag;
    }
};

// A rail of 6 nodes in a line (each one's position near the last; flags 4 on some), the component on it.
void FillRailArena()
{
    FillArena();
    GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
    GameVariable<uint8_t*>(0x007CE5D8) = ArenaPtr(kSkate2);
    GameVariable<uint8_t>(0x007CE4FD) = 0;
    ArenaAt<uint32_t>(kSkate + 0x20) = 0xCA000000;
    ArenaAt<uint32_t>(kSkate2 + 0x40) = Next() % 5 ? 0x1234 : 0xD443A2BC;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool climbs = Next() % 3 == 0; // (a straight rail: the next segment climbs as this one)
    for (int i = 0; i < 6; ++i) {
        uint8_t* n = ArenaPtr(kNodes + i * 0x54);
        *reinterpret_cast<uint8_t**>(n + 0x40) = i < 5 ? n + 0x54 : nullptr;
        *reinterpret_cast<uint8_t**>(n + 0x44) = i > 0 ? n - 0x54 : nullptr;
        *reinterpret_cast<uint32_t*>(n + 0x48) = Next() % 3 ? 4 | (Next() & 1) : Next() % 4;
        if (climbs) {
            x += 2.0f, y += 0.5f;
        } else {
            x += RandomFloat(3.0f), y += RandomFloat(1.0f), z += RandomFloat(3.0f);
        }
        float* pos = reinterpret_cast<float*>(n + 0x20);
        pos[0] = x, pos[1] = y, pos[2] = z, pos[3] = 1.0f;
    }
    int which = static_cast<int>(Next() % 5);
    if (Next() % 12 == 0)
        which = 5; // (the last node)
    ArenaAt<uint8_t*>(kComponent + 0x22C) = ArenaPtr(kNodes + which * 0x54);
    ArenaAt<uint8_t*>(kComponent + 0x230) = ArenaPtr(kManager);
    ArenaAt<uint32_t>(kManager + 0x44) = Next() % 4 ? 0x0A000000 : 0;
    ArenaAt<uint8_t*>(kComponent + 0x1524) = ArenaPtr(kBalanceTrick);
    static const uint32_t kinds[] = {0x255ED86F, 0x8D10119D, 0xA549B57B};
    ArenaAt<uint32_t>(kBalanceTrick + 0x18) = kinds[Next() % 3];
    ArenaAt<uint8_t*>(kComponent + 0x1520) = ArenaPtr(kOther1520);
    ArenaAt<uint8_t*>(kOther1520 + 0x18) = ArenaPtr(kScore);
    ArenaAt<int>(kComponent + 0x3A8) = PickInt();
    ArenaAt<int>(kFlags + 0x18) = 4;
    ArenaAt<uint8_t>(kComponent + 0x34C) = RandomBool(50);
    ArenaAt<uint8_t>(kComponent + 0xF9) = static_cast<uint8_t>(Next());
    for (uint32_t f : {0x50u, 0x78u, 0x80u, 0xA8u, 0xB0u, 0xD0u, 0xD8u})
        ArenaAt<uint8_t>(kFlags + f) = RandomBool(50);
    uint32_t ride = Next() % 4;
    ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
    ArenaAt<uint32_t>(kTracker) = ride >= 2 ? 0x0BEEF000 : 0;
    ArenaAt<uint8_t>(kTracker + 0xAC) = RandomBool(50);
    // The skater near its node, moving along the rail or against it, fast or slow; sometimes still.
    const float* at = reinterpret_cast<const float*>(ArenaPtr(kNodes + which * 0x54 + 0x20));
    float* p = &ArenaAt<float>(kObject + 0x4C);
    for (int i = 0; i < 3; ++i)
        p[i] = at[i] + RandomFloat(Next() % 2 ? 1.0f : 8.0f);
    float* v = &ArenaAt<float>(kObject + 0xAC);
    if (Next() % 6 == 0)
        v[0] = v[2] = 0.0f;
    else if (Next() % 6 == 0)
        v[0] = 0.0f;
    ArenaAt<float>(kComponent + 0x8C) = Next() % 2 ? 1.0f / 60.0f : RandomFloat(0.5f);
}

// DoRailPhysics 0x0050DDF0.
void TestDoRailPhysics()
{
    StandIns standIns(RailStandIns(true));
    std::unique_ptr<UseOurs> vectors(RailVectorFunctions());
    ClockOffsetZero clock;
    RailGlobals globals;
    bool same = true;
    int off = 0, last = 0, flipped = 0, onward = 0, transfers = 0, aheadHit = 0, along = 0;
    for (int variant = 0; variant < 16000 && same; ++variant) {
        g_rng = 0x510E527Fu * (variant + 3);
        FillRailArena();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x0050DDF0)(c);
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        GameVariable<uint8_t>(0x007CE4FD) = 0;
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoRailPhysics();
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        off += CalledWith(actual, 0x005092E0, reinterpret_cast<uint32_t>(ArenaPtr(kComponent))) &&
               !Called(actual, 0x0057CB60);
        last += Called(actual, 0x00504730);
        flipped += ArenaAt<uint8_t>(kComponent + 0x34C) != g_arenaStart[kComponent + 0x34C];
        onward += Called(actual, 0x0057C600) && Called(actual, 0x004FC470);
        transfers += Called(actual, 0x0050C200);
        aheadHit += Called(actual, 0x005003A0) && Called(actual, 0x004FDED0);
        along += Called(actual, 0x0043D990);
        same = Compare(expected, actual, "rail physics", variant);
    }
    bool covered = off > 500 && last > 300 && flipped > 300 && onward > 200 && transfers > 500 && aheadHit > 200 &&
                   along > 2000;
    if (!covered)
        Log("  rail physics coverage: off %d, last node %d, flipped %d, onward %d, transfers %d, ahead hit %d, "
            "along %d",
            off, last, flipped, onward, transfers, aheadHit, along);
    Check(same && covered, "skater: DoRailPhysics");
}

// TransferOrLeaveRail 0x0050C200: the park editor or not, what FindGrindRail finds (nothing, a node with a next, the
// current one, the last one, this rail's next or previous), the move toward its segment or not, long enough or not.
void TestTransferOrLeaveRail()
{
    StandIns standIns(RailStandIns(false));
    std::unique_ptr<UseOurs> vectors(RailVectorFunctions());
    ClockOffsetZero clock;
    RailGlobals globals;
    bool same = true;
    int transferred = 0, left = 0;
    for (int variant = 0; variant < 12000 && same; ++variant) {
        g_rng = 0x9B05688Cu * (variant + 3);
        FillRailArena();
        ArenaAt<uint8_t*>(kComponent + 0x22C) = ArenaPtr(kNodes + 2 * 0x54);
        g_findMode = static_cast<int>(Next() % 63);
        if (Next() % 10 == 0) // (a rail at no position: the comparisons with NaN)
            ArenaAt<uint32_t>(kNodes + (1 + Next() % 4) * 0x54 + 0x20 + 4 * (Next() % 3)) = 0x7FC00000;
        float* end = &ArenaAt<float>(kFree + 0x3C0);
        for (int i = 0; i < 4; ++i)
            end[i] = RandomFloat(6.0f);
        SaveStart();
        uint32_t seed = Next() | 1;
        auto e = reinterpret_cast<const Vector*>(end);
        auto expected = RunOnArena(seed, [e](uint8_t* c) {
            Original<void(__thiscall*)(void*, const Vector*)>(0x0050C200)(c, e);
            return 0u;
        });
        auto actual = RunOnArena(seed, [e](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->TransferOrLeaveRail(e);
            return 0u;
        });
        transferred += Called(actual, 0x004FC470);
        left += Called(actual, 0x004FDED0);
        same = Compare(expected, actual, "transfer or leave rail", variant);
    }
    bool covered = transferred > 1000 && left > 2000;
    if (!covered)
        Log("  transfer coverage: transferred %d, left %d", transferred, left);
    Check(same && covered, "skater: TransferOrLeaveRail");
}

} // namespace

SELFTEST(Early, TestDoRailPhysics);
SELFTEST(Early, TestTransferOrLeaveRail);
