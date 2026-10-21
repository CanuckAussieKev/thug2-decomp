// Self-tests for the motion component's update, moving to points, paths and following a leader (motion_path.cpp):
// original vs ours on a stand-in component, every callee stood in for (motion_testkit.h) - but Mth::CrossProduct
// and Vector::Normalize, which run as they are (the original's and ours, each tested on its own).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/obj/emitter.h"
#include "game/obj/motion.h"
#include "game/obj/path_follower.h"
#include "game/obj/path_manager.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/motion_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace MotionKit;
using Obj::CMotionComponent;
using Script::CScript;
using Script::CStruct;

// --- stand-ins ---
// (g_frameHalf: always 0.5 - set by a variant's setup, cleared before the next)
inline bool g_frameHalf = false;
StandIn FrameSeconds()
{
    return {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)}, [](Frame&, Result& r) {
                static const float frames[] = {1.0f / 60, 1.0f / 30, 0.0f, 0.5f, 0.0123f};
                r.st0 = g_frameHalf ? 0.5f : frames[Next() % 5];
            }};
}
StandIn Own(uint32_t address, uint32_t ours, uint8_t pop, uint8_t args, Ret ret = Ret::None)
{
    return {address, pop, args, ret, true, 0, {ours}};
}
StandIn Game(uint32_t address, uint8_t pop, uint8_t args, Ret ret = Ret::None)
{
    return {address, pop, args, ret, true};
}
StandIn RndCall() { return {0x00403DE0, 0, 1, Ret::Int, false, 0, {Ours(&Mth::Rnd)}}; }
StandIn CallocCall() { return Calloc(Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))); }
StandIn ManagerFree() { return {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}}; }
// A rich vector written to the address of argument `index`.
void WriteRich(Frame& f, int index)
{
    auto* out = reinterpret_cast<float*>(f.args[index]);
    for (int i = 0; i < 4; ++i)
        out[i] = RichFloat(2.0f);
}

template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    g_allocNext = kAlloc;
    return RunOnArena(seed, call);
}

// For each variant: the arena filled and `setup` run, then the original and ours over it, compared.
template <typename Setup, typename Original, typename Ours>
bool CompareRuns(const char* what, int variants, Setup setup, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0x2545F491u * static_cast<uint32_t>(variant + 7);
        g_params.clear();
        g_components.clear();
        g_allocFails = false;
        g_frameHalf = false;
        FillMotionArena();
        setup(variant);
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, [&](uint8_t*) { return original(); });
        auto actual = Run(seed, [&](uint8_t*) { return ours(); });
        if (!Compare(expected, actual, what, variant))
            return false;
    }
    return true;
}
template <typename Setup>
bool CompareMember(const char* what, uint32_t address, bool (CMotionComponent::*member)(CStruct*, CScript*),
                   int variants, Setup setup)
{
    return CompareRuns(
        what, variants, setup,
        [&] {
            return static_cast<uint32_t>(
                Original<bool(__thiscall*)(void*, void*, void*)>(address)(Motion(), Params(), TheScript()));
        },
        [&] { return static_cast<uint32_t>((Motion()->*member)(Params(), TheScript())); });
}
template <typename Setup>
bool CompareVoid(const char* what, uint32_t address, void (CMotionComponent::*member)(), int variants, Setup setup)
{
    return CompareRuns(
        what, variants, setup,
        [&] {
            Original<void(__thiscall*)(void*)>(address)(Motion());
            return 0u;
        },
        [&] {
            (Motion()->*member)();
            return 0u;
        });
}

void RandomFlags()
{
    Motion()->m_motionFlags = Next() & 0x3FF;
    Motion()->m_motionFlags2 = Next() & 0x7FF;
}
// --- the update ---
void TestMotionUpdate()
{
    StandIns standIns({FrameSeconds(), Own(0x00425C70, Ours(&CMotionComponent::UpdateRotations), 0, 0),
                       Own(0x00424440, Ours(&CMotionComponent::UpdateTurn), 0, 0), Own(0x00424870, Ours(&CMotionComponent::UpdateJump), 0, 0),
                       Own(0x00423740, Ours(&CMotionComponent::UpdateHover), 0, 0),
                       Own(0x00424200, Ours(&CMotionComponent::UpdateMoveTo), 0, 0, Ret::Bool),
                       Own(0x00426E70, Ours(&CMotionComponent::UpdatePath), 0, 0),
                       Own(0x004246A0, Ours(&CMotionComponent::UpdateFollowLeader), 0, 0)});
    bool ok = CompareVoid("motion Update", 0x004270E0, &CMotionComponent::Update, 6000, [](int) {
        RandomFlags();
        if (Next() % 4 == 0)
            Motion()->m_motionFlags = 0;
        static const float offsets[] = {0.0f, -0.0f, 5.0f, -5.0f, 0.001f, -0.001f, 1000.0f, -1000.0f};
        Motion()->m_groundOffset = Next() % 3 ? offsets[Next() % 8] : RichFloat(20.0f);
        if (Next() % 30 == 0)
            ArenaAt<uint32_t>(kMotion + 0x1A0) = 0x7FC00000;
        Motion()->m_groundSpeed = Next() % 2 ? RichFloat(400.0f) : 0.0f;
        ArenaAt<uint32_t>(kObj + 0x14) = Next() % 32; // (the object's id: 15 and under, the skaters)
        RichVector(kObj + 0x4C, 500.0f);
        if (Next() % 6 == 0) {
            // (a step exactly the offset: 192 * 0.5 * 0.5 from rest; the frame time is the stand-in's 0.5)
            Motion()->m_groundSpeed = 0.0f;
            Motion()->m_groundOffset = Next() % 2 ? 48.0f : -48.0f;
            g_frameHalf = true;
        }
    });
    Check(ok, "motion component: Update (flags, ground offsets, ids)");
}

// --- moving to a point ---
void TestMotionMoveTo()
{
    StandIns standIns({FrameSeconds(), Own(0x00424020, Ours(&CMotionComponent::ArriveAtNode), 4, 1)});
    int arrived = 0;
    bool ok = CompareRuns(
        "motion UpdateMoveTo", 6000,
        [](int) {
            RichVector(kMotion + 0x108, 500.0f);
            RichVector(kMotion + 0x18C, 500.0f);
            RichVector(kObj + 0x4C, 500.0f);
            CMotionComponent* m = Motion();
            m->m_moveDistance = std::fabs(RichFloat(300.0f)) + 0.01f;
            m->m_moveDone = m->m_moveDistance * static_cast<float>(Next() % 110) / 100.0f;
            m->m_moveSpeed = RichFloat(100.0f);
            m->m_moveMaxSpeed = RichFloat(200.0f);
            m->m_moveAcceleration = Next() % 3 ? 0.0f : RichFloat(10.0f);
            m->m_frameTime = Next() % 2 ? 1.0f / 60 : RichFloat(0.1f);
            m->m_moveArriveNode = Next() % 2 ? Next() : 0;
            switch (Next() % 8) {
            case 0: // (arriving exactly: no speed, the distance done)
                m->m_moveSpeed = 0.0f;
                m->m_moveAcceleration = 0.0f;
                m->m_moveDone = m->m_moveDistance;
                break;
            case 1: // (accelerating exactly to the maximum: 0.5 * 60 * 1 from 0)
                g_frameHalf = true;
                m->m_moveSpeed = 0.0f;
                m->m_moveAcceleration = 1.0f;
                m->m_moveMaxSpeed = 30.0f;
                break;
            case 2: // (NaNs)
                ArenaAt<uint32_t>(kMotion + (Next() % 2 ? 0x124 : 0x120)) = 0x7FC00000;
                break;
            default:
                break;
            }
        },
        [&] {
            uint32_t r = Original<bool(__thiscall*)(void*)>(0x00424200)(Motion());
            arrived += r;
            return r;
        },
        [] { return static_cast<uint32_t>(Motion()->UpdateMoveTo()); });
    Check(ok && arrived > 300, "motion component: UpdateMoveTo (accelerating, arriving, on the way)");
}

// --- along a path ---
void TestMotionUpdatePath()
{
    StandIns standIns({GetComponent(), Game(0x00423530, 0, 0),
                       {0x0046A640, 8, 2, Ret::Bool, true, 0, {Ours(&Obj::CPathFollower::Update)}},
                       {0x0046A1B0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CPathFollower::GetHeading)},
                        [](Frame& f, Result&) {
                            WriteRich(f, 0);
                            if (Next() % 4 == 0)
                                reinterpret_cast<float*>(f.args[0])[1] = 0.0f;
                        }},
                       Own(0x00425F40, Ours(&CMotionComponent::StickToGround), 0, 0, Ret::Bool)});
    bool ok = CompareVoid("motion UpdatePath", 0x00426E70, &CMotionComponent::UpdatePath, 6000, [](int) {
        RandomFlags();
        if (Next() % 5 == 0)
            g_components.push_back({kObj, 0x6EFDDFB8, kMine});
        Motion()->m_pathSpeed = Next() % 4 ? RichFloat(100.0f) : 0.0f;
        Motion()->m_frameTime = Next() % 2 ? 1.0f / 60 : RichFloat(0.1f);
        ArenaAt<uint8_t*>(kMotion + 0x1EC) = ArenaPtr(kFollower);
        RichVector(kFollower + 0x04, 500.0f);
        for (uint32_t row = 0; row < 4; ++row)
            RichVector(kObj + 0x6C + row * 16, 1.0f);
        RichVector(kObj + 0x4C, 500.0f);
    });
    Check(ok, "motion component: UpdatePath (peds skipped, speeds, headings, ground, constant height)");
}

// --- after a leader ---
void TestMotionLeader()
{
    StandIns standIns({{0x00468610, 0, 0, Ret::None, true, 0, {Ours(&Obj::CFollowTrail::Update)}},
                       {0x00468730, 8, 2, Ret::None, true, 0, {Ours(&Obj::CFollowTrail::GetPosition)},
                        [](Frame& f, Result&) {
                            WriteRich(f, 0);
                            WriteRich(f, 1);
                        }},
                       {0x00468AC0, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CFollowTrail::Init)},
                        [](Frame& f, Result& r) { r.eax = f.ecx; }},
                       {0x004685D0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CFollowTrail::Reset)}}, CallocCall(),
                       ManagerFree(), GetComponent(), GetFloat(), GetChecksum(), ContainsFlag(), ContainsFlagNamed()});
    bool ok = CompareVoid("motion UpdateFollowLeader", 0x004246A0, &CMotionComponent::UpdateFollowLeader, 200, [](int) {
        ArenaAt<uint8_t*>(kMotion + 0x1F0) = ArenaPtr(kTrail);
        ArenaAt<uint8_t>(kMotion + 0x1F4) = static_cast<uint8_t>(Next() % 2);
    });
    ok = ok && CompareRuns(
                   "motion FollowLeader", 3000,
                   [](int) {
                       RandomFlags();
                       ArenaAt<uint8_t*>(kMotion + 0x1F0) = Next() % 2 ? ArenaPtr(kTrail) : nullptr;
                       g_components.push_back({kObj, 0x5CB5CB7E, kLock});
                       if (Next() % 3 == 0)
                           GiveFlag(0xD443A2BC);
                       if (Next() % 2)
                           GiveFlag(Crc::GenerateCRCFromString("LeaveYUnaffected"));
                       if (Next() % 2)
                           Give(0xE36D657E, RichFloat(100.0f));
                       if (Next() % 2)
                           GiveChecksum(0xA1DC81F9, Next());
                       if (Next() % 2)
                           GiveFlag(0xE19E310A);
                   },
                   [] {
                       Original<void(__thiscall*)(void*, void*)>(0x004246F0)(Motion(), Params());
                       return 0u;
                   },
                   [] {
                       Motion()->FollowLeader(Params());
                       return 0u;
                   });
    Check(ok, "motion component: UpdateFollowLeader, FollowLeader (Off, a trail or not, LeaveYUnaffected, OrientY)");
}

// --- paths ---
constexpr uint32_t kGroup = kMine + 0x1100;
constexpr uint32_t kSlots = kMine + 0x1000;

void TestMotionPaths()
{
    using RemoveFn = void (Script::CStruct::*)(uint32_t);
    using AddChecksumFn = void (Script::CStruct::*)(uint32_t, uint32_t);
    using LinkCountFn = uint32_t (*)(uint32_t);
    using LinkFn = uint32_t (*)(uint32_t, int);
    std::vector<StandIn> base(
        {{0x00469580, 4, 1, Ret::None, true, 0, {Ours(&Obj::CObjectGroup::Remove)}},
         {0x004694E0, 0, 0, Ret::Int, false, 0, {Ours(&Obj::CPathManager::Instance)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x1200)); }},
         {0x00469620, 8, 2, Ret::Int, true, 0, {Ours(&Obj::CPathManager::AddToPath)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kGroup)); }},
         CallocCall(),
         {0x0046A1D0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CPathFollower::Construct)},
          [](Frame& f, Result& r) { r.eax = f.ecx; }},
         {0x0046A3F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CPathFollower::Start)}},
         {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParams)); }},
         {0x00461410, 16, 4, Ret::Int, true, 1 << 3, {Ours(&Obj::CObject::SpawnScript)}},
         {0x00461350, 8, 2, Ret::Int, true, 0, {Ours(&Obj::CObject::SwitchScript)}},
         {0x0045C3A0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<LinkCountFn>(&SkateScript::GetLinkCount))},
          [](Frame&, Result& r) {
              static const uint32_t counts[] = {0, 1, 2, 3, 7};
              r.eax = counts[Next() % 5];
          }},
         {0x0045C3E0, 0, 2, Ret::Int, false, 0, {Ours(static_cast<LinkFn>(&SkateScript::GetLink))}}, RndCall(),
         {0x004776B0, 4, 1, Ret::None, true, 0, {Ours(static_cast<RemoveFn>(&Script::CStruct::RemoveComponent))}},
         {0x00477D40, 8, 2, Ret::None, true, 0, {Ours(static_cast<AddChecksumFn>(&Script::CStruct::AddChecksum))}},
         GetFloat(), GetChecksum(), GetStructure(), ContainsFlag(), GetComponent()});
    auto with = [&](std::vector<StandIn> extra) {
        std::vector<StandIn> list = base;
        list.insert(list.end(), extra.begin(), extra.end());
        return list;
    };
    bool ok = true;
    std::vector<StandIn> plain = base;
    auto* standIns = new StandIns(plain);
    ok = ok && CompareRuns(
                   "motion StartPath", 400,
                   [](int) {
                       ArenaAt<uint8_t*>(kMotion + 0x104) = Next() % 2 ? ArenaPtr(kGroup) : nullptr;
                       ArenaAt<uint8_t*>(kMotion + 0x1EC) = Next() % 3 ? ArenaPtr(kFollower) : nullptr;
                   },
                   [] {
                       Original<void(__thiscall*)(void*, uint32_t)>(0x00423FC0)(Motion(), 0x1234);
                       return 0u;
                   },
                   [] {
                       Motion()->StartPath(0x1234);
                       return 0u;
                   });
    ok = ok && CompareRuns(
                   "motion ArriveAtNode", 400,
                   [](int) {
                       if (Next() % 2)
                           GiveChecksum(0x3AEFE377, Next());
                       if (Next() % 2)
                           GiveChecksum(0x86125749, Next());
                       if (Next() % 2)
                           GiveStructure(0x7031F10C, Next());
                   },
                   [] {
                       Original<void(__thiscall*)(void*, uint32_t)>(0x00424020)(Motion(), 0x4321);
                       return 0u;
                   },
                   [] {
                       Motion()->ArriveAtNode(0x4321);
                       return 0u;
                   });
    delete standIns;
    {
        StandIns startPath(with({Own(0x00423FC0, Ours(&CMotionComponent::StartPath), 4, 1)}));
        ok = ok && CompareRuns(
                       "motion FollowPathLinked", 1000,
                       [](int) {
                           RandomFlags();
                           if (Next() % 2)
                               GiveFlag(0x422E1FCE);
                       },
                       [] {
                           Original<void(__thiscall*)(void*, void*, void*)>(0x00424180)(Motion(), Params(), TheScript());
                           return 0u;
                       },
                       [] {
                           Motion()->FollowPathLinked(Params(), TheScript());
                           return 0u;
                       });
        ok = ok && CompareMember("Obj_FollowPath", 0x00425220, &CMotionComponent::Obj_FollowPath, 100, [](int) {
            RandomFlags();
            if (Next() % 2)
                GiveChecksum(0xA1DC81F9, Next());
        });
        ok = ok && CompareMember("Obj_FollowPathStored", 0x00425290, &CMotionComponent::Obj_FollowPathStored, 50,
                                 [](int) { RandomFlags(); });
    }
    {
        StandIns parts(with({Own(0x00424180, Ours(&CMotionComponent::FollowPathLinked), 8, 2),
                        Own(0x004246F0, Ours(&CMotionComponent::FollowLeader), 4, 1),
                        Own(0x00423C90, Ours(&CMotionComponent::InitPathFollower), 4, 1),
                        {0x004244C0, 4, 1, Ret::Int, true, 0, {Ours(&CMotionComponent::GetNextObjOnPath)},
                         [](Frame&, Result& r) {
                             r.eax = Next() % 2 ? reinterpret_cast<uint32_t>(ArenaPtr(kOtherObj)) : 0;
                         }}}));
        ok = ok && CompareMember("Obj_FollowPathLinked", 0x00425270, &CMotionComponent::Obj_FollowPathLinked, 50,
                                 [](int) { RandomFlags(); });
        ok = ok && CompareMember("Obj_FollowLeader", 0x004252C0, &CMotionComponent::Obj_FollowLeader, 20, [](int) {});
        ok = ok && CompareMember("Obj_GetNextObjOnPath", 0x00425150, &CMotionComponent::Obj_GetNextObjOnPath, 200,
                                 [](int) {
                                     if (Next() % 2)
                                         Give(0x6C78A5B6, RichFloat(300.0f));
                                 });
        ok = ok && CompareMember("Obj_SetPathTurnDist", 0x004251C0, &CMotionComponent::Obj_SetPathTurnDist, 200,
                                 [](int) {
                                     ArenaAt<uint8_t*>(kMotion + 0x1EC) = ArenaPtr(kFollower);
                                     if (Next() % 3)
                                         Give(0, RichFloat(30.0f));
                                     if (Next() % 2)
                                         GiveFlag(0xF915B4F3);
                                 });
    }
    standIns = new StandIns(plain);
    // GetNextObjOnPath: up to 7 objects in the group (empty slots, itself), facing and placed at random, some on paths.
    int found = 0;
    ok = ok && CompareRuns(
                   "motion GetNextObjOnPath", 6000,
                   [](int) {
                       RandomFlags();
                       ArenaAt<uint8_t*>(kMotion + 0x104) = Next() % 10 ? ArenaPtr(kGroup) : nullptr;
                       ArenaAt<uint8_t*>(kMotion + 0x1EC) = Next() % 4 ? ArenaPtr(kFollower) : nullptr;
                       ArenaAt<uint32_t>(kFollower + 0x58) = Next() % 3;
                       ArenaAt<uint32_t>(kFollower + 0x5C) = Next() % 3;
                       int count = static_cast<int>(Next() % 8);
                       ArenaAt<uint8_t*>(kGroup) = ArenaPtr(kSlots);
                       ArenaAt<int>(kGroup + 4) = Next() % 10 == 0 ? -1 : count;
                       RichVector(kObj + 0x4C, 300.0f);
                       for (uint32_t row = 0; row < 3; ++row)
                           RichVector(kObj + 0x6C + row * 16, 1.0f);
                       bool exact = Next() % 3 == 0;
                       if (exact) {
                           ArenaAt<Mth::Vector>(kObj + 0x4C) = {10.0f, 20.0f, 30.0f, 1.0f};
                           ArenaAt<Mth::Vector>(kObj + 0x8C) = {1.0f, 0.0f, 0.0f, 0.0f};
                       }
                       for (int i = 0; i < count; ++i) {
                           uint32_t object = kMine + 0x120 * static_cast<uint32_t>(i);
                           uint32_t choice = Next() % 10;
                           uint32_t slotObject = choice == 0 ? 0 : choice == 1 ? kObj : object;
                           ArenaAt<uint8_t*>(kSlots + 12 * i) = slotObject ? ArenaPtr(slotObject) : nullptr;
                           RichVector(object + 0x4C, 300.0f);
                           for (uint32_t row = 0; row < 3; ++row)
                               RichVector(object + 0x6C + row * 16, 1.0f);
                           if (Next() % 3 == 0) // (facing the same way, ahead)
                               std::memcpy(ArenaPtr(object + 0x8C), ArenaPtr(kObj + 0x8C), 16);
                           if (Next() % 12 == 0) // (a NaN facing)
                               ArenaAt<uint32_t>(object + 0x8C + 4 * (Next() % 3)) = 0x7FC00000;
                           if (exact && Next() % 3) {
                               // (exactly 100 or 50 ahead: distances that tie with the range and each other)
                               static const float ahead[] = {110.0f, 60.0f};
                               ArenaAt<float>(object + 0x4C) = ahead[Next() % 2];
                               ArenaAt<float>(object + 0x50) = 20.0f;
                               ArenaAt<float>(object + 0x54) = 30.0f;
                               std::memcpy(ArenaPtr(object + 0x8C), ArenaPtr(kObj + 0x8C), 16);
                           }
                           uint32_t motion = 0x2800 + 8 * static_cast<uint32_t>(i);
                           uint32_t follower = 0x2C00 + 8 * static_cast<uint32_t>(i);
                           g_components.push_back({object, 0x0A015E17, motion});
                           ArenaAt<uint32_t>(motion + 0x18) = Next() & 0x10;
                           ArenaAt<uint8_t*>(motion + 0x1EC) = Next() % 4 ? ArenaPtr(follower) : nullptr;
                           ArenaAt<uint32_t>(follower + 0x58) = Next() % 3;
                           ArenaAt<uint32_t>(follower + 0x5C) = Next() % 3;
                       }
                   },
                   [&] {
                       static const float ranges[] = {100.0f, 1000.0f, 50.0f, 0.0f, 1200.0f};
                       float range = ranges[g_rng % 5];
                       uint32_t r = reinterpret_cast<uint32_t>(
                           Original<void*(__thiscall*)(void*, float)>(0x004244C0)(Motion(), range));
                       found += r != 0;
                       return r;
                   },
                   [] {
                       static const float ranges[] = {100.0f, 1000.0f, 50.0f, 0.0f, 1200.0f};
                       float range = ranges[g_rng % 5];
                       return reinterpret_cast<uint32_t>(Motion()->GetNextObjOnPath(range));
                   });
    delete standIns;
    Check(ok && found > 200, "motion component: StartPath, ArriveAtNode, FollowPathLinked, GetNextObjOnPath and the path "
                             "member functions (original vs ours)");
}

} // namespace

SELFTEST(Early, TestMotionUpdate);
SELFTEST(Early, TestMotionMoveTo);
SELFTEST(Early, TestMotionUpdatePath);
SELFTEST(Early, TestMotionLeader);
SELFTEST(Early, TestMotionPaths);
