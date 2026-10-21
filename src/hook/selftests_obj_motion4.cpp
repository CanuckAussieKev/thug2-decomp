// Self-tests for the motion component's jumping, ground sticking and moving to points (motion_ground.cpp): original
// vs ours on a stand-in component, every callee stood in for (motion_testkit.h) - but the feeler's constructor, the
// collision object's vertex lookup and the vector maths, which run as they are (the original's and ours).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tests.h"
#include "game/nx/feeler.h"
#include "game/obj/motion.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/motion_testkit.h"
#include "hook/selftest.h"

namespace Obj {
bool __cdecl GroundHit(Mth::Vector* triangle, const Mth::Vector* end, const Mth::Vector* start, Mth::Vector* out);
}

namespace {

using namespace MotionKit;
using Obj::CMotionComponent;
using Script::CScript;
using Script::CStruct;

StandIn Own(uint32_t address, uint32_t ours, uint8_t pop, uint8_t args, Ret ret = Ret::None)
{
    return {address, pop, args, ret, true, 0, {ours}};
}
StandIn RndCall() { return {0x00403DE0, 0, 1, Ret::Int, false, 0, {Ours(&Mth::Rnd)}}; }
// A position written for SkateScript's node getters (cdecl: node or name, out).
StandIn NodePosition(uint32_t address, uint32_t ours)
{
    return {address, 0, 2, Ret::Bool, false, 0, {ours}, [](Frame& f, Result&) {
                auto* out = reinterpret_cast<float*>(f.args[1]);
                for (int i = 0; i < 4; ++i)
                    out[i] = RichFloat(100.0f);
            }};
}
// Records a feeler's line (start, end) as the stand-in sees it.
void RecordLine(uint32_t feeler)
{
    for (int i = 0; i < 8; ++i)
        Record(reinterpret_cast<const uint32_t*>(feeler)[i]);
}

template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    g_allocNext = kAlloc;
    return RunOnArena(seed, call);
}
template <typename Setup, typename Original, typename Ours>
bool CompareRuns(const char* what, int variants, Setup setup, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0xBB67AE85u * static_cast<uint32_t>(variant + 5);
        g_params.clear();
        g_components.clear();
        g_allocFails = false;
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
void RandomFlags()
{
    Motion()->m_motionFlags = Next() & 0x3FF;
    Motion()->m_motionFlags2 = Next() & 0x7FF;
}
float Pick(std::initializer_list<float> values) { return values.begin()[Next() % values.size()]; }

// --- jumping ---
void TestMotionJump()
{
    StandIns standIns({{0x004172F0, 8, 2, Ret::Bool, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
                        [](Frame& f, Result& r) {
                            RecordLine(f.ecx);
                            r.eax = Next() % 2;
                        }}});
    bool ok = CompareRuns(
        "motion UpdateJump", 8000,
        [](int v) {
            RandomFlags();
            CMotionComponent* m = Motion();
            m->m_frameTime = Pick({1.0f / 60, 0.1f, 0.0f, 0.5f});
            m->m_jumpSpeed = RichFloat(400.0f);
            m->m_jumpGravity = Pick({-360.0f, -1000.0f, 0.0f, 50.0f});
            m->m_jumpStartSpeed = RichFloat(300.0f);
            m->m_jumpHasHeading = Next() % 2 != 0;
            m->m_jumpHasLandHeight = Next() % 3 == 0;
            m->m_jumpLandHeight = RichFloat(100.0f);
            RichVector(kMotion + 0x200, 1.0f);
            RichVector(kObj + 0x4C, 100.0f);
            m->m_jumpColAbove = RichFloat(10.0f);
            m->m_jumpColBelow = RichFloat(10.0f);
            if (v % 7 == 0) { // (landing exactly on land_height: 100 - 125 = -25)
                m->m_frameTime = 0.5f;
                m->m_jumpSpeed = 0.0f;
                m->m_jumpGravity = -1000.0f;
                m->m_jumpHasLandHeight = true;
                m->m_jumpLandHeight = -25.0f;
                ArenaAt<float>(kObj + 0x50) = 100.0f;
            }
        },
        [] {
            Original<void(__thiscall*)(void*)>(0x00424870)(Motion());
            return 0u;
        },
        [] {
            Motion()->UpdateJump();
            return 0u;
        });
    Check(ok, "motion component: UpdateJump (headings, land heights, the feeler)");
}

// --- the ground ---
// A collision object for the feeler's hit (vtable 0x00645C18: a sector's, whose slot 9 reads the geometry), its
// geometry, faces and vertices.
constexpr uint32_t kCollObj = kMine;          // 0x10
constexpr uint32_t kGeometry = kMine + 0x20;  // 0x40
constexpr uint32_t kFaces = kMine + 0x80;     // 8 faces of 10 bytes
constexpr uint32_t kVertices = kMine + 0x100; // 32 vertices of 3 floats
void BuildGround()
{
    ArenaAt<uint32_t>(kCollObj) = 0x00645C18;
    ArenaAt<uint8_t*>(kCollObj + 0x0C) = ArenaPtr(kGeometry);
    ArenaAt<uint8_t>(kGeometry + 0x0A) = static_cast<uint8_t>(Next() % 2); // (8- or 10-byte faces)
    ArenaAt<uint8_t>(kGeometry + 0x0B) = 0;                                // (float vertices)
    ArenaAt<uint8_t*>(kGeometry + 0x0C) = ArenaPtr(kFaces);
    ArenaAt<uint8_t*>(kGeometry + 0x30) = ArenaPtr(kVertices);
    for (uint32_t i = 0; i < 80; ++i)
        ArenaAt<uint8_t>(kFaces + i) = static_cast<uint8_t>(Next() % 32);
    for (uint32_t i = 0; i < 96; ++i)
        ArenaAt<float>(kVertices + 4 * i) = RichFloat(500.0f);
}

// The original GroundHit (ECX the triangle, EDX the end; the start and `out` on the stack, the caller pops).
uint32_t OriginalGroundHit()
{
    void* code = OriginalFunction(0x00425CB0);
    void* triangle = ArenaPtr(kMotion + 0x40);
    void* end = ArenaPtr(kMine + 0x300);
    void* start = ArenaPtr(kMine + 0x310);
    void* hitOut = ArenaPtr(kMine + 0x320);
    uint32_t result;
    __asm {
        mov ecx, triangle
        mov edx, end
        push hitOut
        push start
        call code
        add esp, 8
        movzx eax, al
        mov result, eax
    }
    return result;
}

void TestMotionGroundHit()
{
    StandIns standIns(
        {{0x0040B420, 0, 6, Ret::Bool, false, 0, {Ours(&Nx::RayHitsTriangle)},
          [](Frame& f, Result& r) {
              r.eax = Next() % 2;
              *reinterpret_cast<float*>(f.args[5]) = RichFloat(1.0f);
          }},
         {0x00417710, 12, 3, Ret::Bool, true, 4, {Ours(&Nx::CFeeler::GetCollisionAlong)}, [](Frame& f, Result& r) {
              r.eax = Next() % 2;
              auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
              feeler->m_hit.object = reinterpret_cast<Nx::CCollObj*>(ArenaPtr(kCollObj));
              feeler->m_hit.face = Next() % 8;
              feeler->m_point = {RichFloat(9.0f), RichFloat(9.0f), RichFloat(9.0f), 1.0f};
          }}});
    bool ok = CompareRuns(
        "motion GroundHit", 4000,
        [](int) {
            BuildGround();
            RichVector(kMine + 0x300, 300.0f); // (end)
            RichVector(kMine + 0x310, 300.0f); // (start)
        },
        [] { return OriginalGroundHit(); },
        [] {
            return static_cast<uint32_t>(Obj::GroundHit(reinterpret_cast<Mth::Vector*>(ArenaPtr(kMotion + 0x40)),
                                                        reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x300)),
                                                        reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x310)),
                                                        reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x320))));
        });
    Check(ok, "motion component: GroundHit (the cached triangle, the feeler, 8- and 10-byte faces)");
}

// GroundHit as each version calls it: the original's (ECX, EDX, two on the stack) and ours (cdecl).
void GroundHitAnswer(uint32_t triangle, uint32_t end, uint32_t start, uint32_t out, Result& r)
{
    Record(triangle); // (the start, end and out are on each version's own stack: their contents are recorded)
    for (int i = 0; i < 4; ++i)
        Record(reinterpret_cast<const uint32_t*>(start)[i]);
    for (int i = 0; i < 4; ++i)
        Record(reinterpret_cast<const uint32_t*>(end)[i]);
    r.eax = Next() % 5 != 0;
    for (int i = 0; i < 4; ++i)
        reinterpret_cast<float*>(out)[i] = RichFloat(50.0f);
}

void TestMotionStick()
{
    StandIns standIns({{0x00425CB0, 0, 0, Ret::Bool, false, 0, {},
                        [](Frame& f, Result& r) { GroundHitAnswer(f.ecx, f.edx, f.args[0], f.args[1], r); }},
                       {Ours(&Obj::GroundHit), 0, 0, Ret::Bool, false, 0, {},
                        [](Frame& f, Result& r) {
                            g_calls.back() = 0x00425CB0; // (recorded as the original's address)
                            GroundHitAnswer(f.args[0], f.args[1], f.args[2], f.args[3], r);
                        }}});
    bool ok = CompareRuns(
        "motion StickToGround", 8000,
        [](int) {
            CMotionComponent* m = Motion();
            m->m_groundAbove = Next() % 5 == 0 ? 0.0f : RichFloat(10.0f);
            m->m_groundBelow = Next() % 5 == 0 ? 0.0f : RichFloat(10.0f);
            m->m_groundHeightOnly = Next() % 2 != 0;
            RichVector(kObj + 0x4C, 300.0f);
            for (uint32_t row = 0; row < 4; ++row)
                RichVector(kObj + 0x6C + row * 16, 1.0f);
        },
        [] { return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00425F40)(Motion())); },
        [] { return static_cast<uint32_t>(Motion()->StickToGround()); });
    Check(ok, "motion component: StickToGround (height only, three feelers, hits and misses)");
}

// --- moving to points ---
void MoveParams(int)
{
    RandomFlags();
    RichVector(kObj + 0x4C, 300.0f);
    for (uint32_t row = 0; row < 3; ++row)
        RichVector(kObj + 0x6C + row * 16, 1.0f);
    if (Next() % 4 == 0)
        Give(0x906B67BA, Pick({0.0f, 1.0f, 2.5f, 0.001f}));
    if (Next() % 3)
        Give(0xF0D90109, RichFloat(100.0f));
    static const uint32_t units[] = {0x2CE7EE02, 0xAAD7C80F, 0xA18B8F32};
    if (Next() % 2)
        GiveFlag(units[Next() % 3]);
    if (Next() % 2)
        Give(0x85894A84, Next() % 3 ? 0.0f : RichFloat(50.0f));
    if (Next() % 2)
        GiveFlag(0x90A91232);
    if (Next() % 3)
        GiveChecksum(0xA1DC81F9, Next() % 4 ? Next() : 0);
    if (Next() % 2)
        GiveChecksum(0, Next());
    if (Next() % 2)
        GiveVector(0, RichFloat(100.0f), RichFloat(100.0f), RichFloat(100.0f));
    if (Next() % 2)
        GiveFlag(0x26AF9DC9);
    if (Next() % 2)
        GiveInteger(0xE85997D0, static_cast<int>(Next() % 5));
    if (Next() % 2)
        GiveFlag(0x90A1C52A);
    if (Next() % 2)
        Give(0x7E832F08, RichFloat(200.0f));
    if (Next() % 2)
        Give(0xA8A5A3C7, Next() % 3 ? 0.0f : RichFloat(10.0f));
    if (Next() % 2)
        Give(0x182D87A7, Next() % 3 ? 0.0f : RichFloat(10.0f));
    if (Next() % 4 == 0)
        GiveFlag(0xD443A2BC);
    // (a model component whose update is the empty 0x005480A0)
    ArenaAt<uint32_t>(kMine + 0x400) = 0x005480A0;
    ArenaAt<uint8_t*>(kMine + 0x440) = ArenaPtr(kMine + 0x3FC);
    g_components.push_back({kObj, 0x286A8D26, kMine + 0x440});
}

void TestMotionMoves()
{
    using LinkCountFn = uint32_t (*)(uint32_t);
    using LinkedNodeFn = Script::CStruct* (*)(uint32_t, int);
    using PositionStructFn = bool (*)(Script::CStruct*, Mth::Vector*);
    using PositionNameFn = bool (*)(uint32_t, Mth::Vector*);
    using NodeNameFn = uint32_t (*)(Script::CStruct*);
    std::vector<StandIn> base = {
        GetFloat(), GetInteger(), GetChecksum(), GetVector(), ContainsFlag(), GetComponent(), RndCall(),
        Own(0x00425970, Ours(&CMotionComponent::OrientToNode), 4, 1),
        Own(0x00424020, Ours(&CMotionComponent::ArriveAtNode), 4, 1),
        Own(0x00425F40, Ours(&CMotionComponent::StickToGround), 0, 0, Ret::Bool),
        {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)}},
        {0x0045BE40, 0, 1, Ret::Int, false, 0, {Ours(static_cast<NodeNameFn>(&SkateScript::GetNodeName))}},
        {0x0045C3A0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<LinkCountFn>(&SkateScript::GetLinkCount))},
         [](Frame&, Result& r) { r.eax = Next() % 4; }},
        {0x0045C460, 0, 2, Ret::Int, false, 0, {Ours(static_cast<LinkedNodeFn>(&SkateScript::GetLinkedNode))}},
        NodePosition(0x0045C050, Ours(static_cast<PositionStructFn>(&SkateScript::GetPosition))),
        NodePosition(0x0045C560, Ours(static_cast<PositionNameFn>(&SkateScript::GetPosition)))};
    bool ok;
    {
        StandIns standIns(base);
        ok = CompareRuns(
            "motion MoveTo", 8000, MoveParams,
            [] {
                Mth::Vector target = {RichFloat(300.0f), RichFloat(300.0f), RichFloat(300.0f), RichFloat(2.0f)};
                uint32_t node = Next() % 2 ? Next() : 0;
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, void*, void*, Mth::Vector, uint32_t)>(0x004269F0)(
                        Motion(), Params(), TheScript(), target, node));
            },
            [] {
                Mth::Vector target = {RichFloat(300.0f), RichFloat(300.0f), RichFloat(300.0f), RichFloat(2.0f)};
                uint32_t node = Next() % 2 ? Next() : 0;
                return static_cast<uint32_t>(Motion()->MoveTo(Params(), TheScript(), target, node));
            });
        ok = ok && CompareMember("Obj_StickToGround", 0x004268C0, &CMotionComponent::Obj_StickToGround, 400,
                                 MoveParams);
        ok = ok && CompareMember("Obj_SkipToRestart", 0x00426960, &CMotionComponent::Obj_SkipToRestart, 100,
                                 MoveParams);
    }
    {
        std::vector<StandIn> list = base;
        list.push_back(Own(0x004269F0, Ours(&CMotionComponent::MoveTo), 0x1C, 7, Ret::Bool));
        StandIns standIns(list);
        ok = ok && CompareRuns(
                       "motion MoveToNode", 600, MoveParams,
                       [] {
                           Original<void(__thiscall*)(void*, void*, void*)>(0x00426C10)(Motion(), Params(), TheScript());
                           return 0u;
                       },
                       [] {
                           Motion()->MoveToNode(Params(), TheScript());
                           return 0u;
                       });
        ok = ok && CompareRuns(
                       "motion MoveToLink", 1000, MoveParams,
                       [] {
                           Original<void(__thiscall*)(void*, void*, void*)>(0x00426D80)(Motion(), Params(), TheScript());
                           return 0u;
                       },
                       [] {
                           Motion()->MoveToLink(Params(), TheScript());
                           return 0u;
                       });
        ok = ok && CompareMember("Obj_MoveToPosStored", 0x004272D0, &CMotionComponent::Obj_MoveToPosStored, 50,
                                 MoveParams);
        ok = ok && CompareMember("Obj_MoveForward", 0x00427320, &CMotionComponent::Obj_MoveForward, 600, MoveParams);
        ok = ok && CompareMember("Obj_MoveLeft", 0x004273D0, &CMotionComponent::Obj_MoveLeft, 600, MoveParams);
        ok = ok && CompareMember("Obj_MoveToPos", 0x00427470, &CMotionComponent::Obj_MoveToPos, 600, MoveParams);
        ok = ok && CompareMember("Obj_MoveToRelPos", 0x00427490, &CMotionComponent::Obj_MoveToRelPos, 600,
                                 MoveParams);
    }
    {
        std::vector<StandIn> list = base;
        list.push_back(Own(0x00426D80, Ours(&CMotionComponent::MoveToLink), 8, 2));
        StandIns standIns(list);
        ok = ok && CompareMember("Obj_MoveToLink", 0x004274B0, &CMotionComponent::Obj_MoveToLink, 20, MoveParams);
    }
    Check(ok, "motion component: MoveTo (time, speed, units, acceleration, at once), MoveToNode, MoveToLink and the "
              "move member functions, Obj_StickToGround, Obj_SkipToRestart");
}

} // namespace

SELFTEST(Early, TestMotionJump);
SELFTEST(Early, TestMotionGroundHit);
SELFTEST(Early, TestMotionStick);
SELFTEST(Early, TestMotionMoves);
