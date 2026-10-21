// Self-tests for the motion component's turning and looking (motion_rotate.cpp): original vs ours on a stand-in
// component, every callee stood in for (motion_testkit.h) - but the per-axis direction fix (0x00423F10),
// Matrix::operator= and Vector::NegateXRotateX90, which run as they are (the original's and ours).

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/obj/motion.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
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
StandIn Own(uint32_t address, uint32_t ours, uint8_t pop, uint8_t args, Ret ret = Ret::None)
{
    return {address, pop, args, ret, true, 0, {ours}};
}
// Mth::CreateRotateX/Y/ZMatrix (cdecl: the matrix, the angle; returns the matrix): a recognisable matrix written.
StandIn CreateRotate(uint32_t address, Mth::Matrix& (*ours)(Mth::Matrix&, float))
{
    return {address, 0, 2, Ret::Int, false, 0, {Ours(ours)}, [](Frame& f, Result& r) {
                auto* m = reinterpret_cast<float*>(f.args[0]);
                for (int i = 0; i < 16; ++i)
                    m[i] = std::bit_cast<float>(f.args[1]) + static_cast<float>(i) * 0.25f;
                r.eax = f.args[0];
            }};
}
// D3DXMatrixMultiply (stdcall: out, a, b; returns out): out's first value bumped.
StandIn MatrixMultiply()
{
    return {0x005BFA8C, 12, 3, Ret::Int, false, 0, {}, [](Frame& f, Result& r) {
                auto* out = reinterpret_cast<float*>(f.args[0]);
                out[0] += 1.5f;
                out[5] -= 0.5f;
                r.eax = f.args[0];
            }};
}
StandIn OrthoNormalize()
{
    return {0x00405170, 4, 1, Ret::Int, true, 0, {Ours(&Mth::Matrix::OrthoNormalizeAbout)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn AngleInPlane()
{
    return {0x004062D0, 0, 4, Ret::Float, false, 0, {Ours(&Mth::GetAngleInPlane)}, [](Frame&, Result& r) {
                static const float angles[] = {0.0f, 0.5f, -0.5f, 3.0f, -3.0f, 0.001f, 1.25f, -0.0f};
                r.st0 = angles[Next() % 8];
            }};
}
// A position written for SkateScript's node getters (cdecl: node or name, out).
StandIn NodePosition(uint32_t address, uint32_t ours)
{
    return {address, 0, 2, Ret::Bool, false, 0, {ours}, [](Frame& f, Result&) {
                auto* out = reinterpret_cast<float*>(f.args[1]);
                for (int i = 0; i < 4; ++i)
                    out[i] = RichFloat(100.0f);
            }};
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
        g_rng = 0x6A09E667u * static_cast<uint32_t>(variant + 3);
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
float Pick(std::initializer_list<float> values)
{
    return values.begin()[Next() % values.size()];
}
// The six per-axis vectors at random (some values 0, some signs flipped).
void RandomRotationState()
{
    for (uint32_t offset = 0x12C; offset < 0x18C; offset += 4)
        ArenaAt<float>(kMotion + offset) = Next() % 5 == 0 ? 0.0f : RichFloat(4.0f);
    Motion()->m_frameTime = Pick({1.0f / 60, 0.5f, 0.0f, 0.02f});
}

// --- about the axes ---
void TestMotionRotAxes()
{
    StandIns standIns({GetFloat(), ContainsFlag()});
    bool ok = true;
    for (uint32_t which = 0; which < 3 && ok; ++which) {
        static const uint32_t addresses[] = {0x00424B90, 0x00424D50, 0x00424F50};
        static const char* names[] = {"Obj_RotX", "Obj_RotY", "Obj_RotZ"};
        static bool (CMotionComponent::*members[])(CStruct*, CScript*) = {
            &CMotionComponent::Obj_RotX, &CMotionComponent::Obj_RotY, &CMotionComponent::Obj_RotZ};
        ok = CompareMember(names[which], addresses[which], members[which], 4000, [](int) {
            RandomFlags();
            RandomRotationState();
            if (Next() % 2)
                GiveFlag(0x26AF9DC9);
            if (Next() % 3)
                Give(0xFF7EBAF6, Next() % 4 == 0 ? 0.0f : RichFloat(400.0f));
            if (Next() % 3 == 0)
                Give(0xF59FF7D7, Next() % 4 == 0 ? 0.0f : RichFloat(100.0f));
            if (Next() % 3 == 0)
                Give(0x85894A84, RichFloat(100.0f));
            if (Next() % 2)
                Give(0xF0D90109, RichFloat(300.0f));
        });
    }
    ok = ok && CompareRuns(
                   "motion SetRotationDirection", 600, [](int) { RandomRotationState(); },
                   [] {
                       Original<void(__thiscall*)(void*, int)>(0x00423F10)(Motion(), static_cast<int>(g_rng % 3));
                       return 0u;
                   },
                   [] {
                       Motion()->SetRotationDirection(static_cast<int>(g_rng % 3));
                       return 0u;
                   });
    Check(ok, "motion component: Obj_RotX / RotY / RotZ (Angle, deceleration, Acceleration, speed, max coords), "
              "SetRotationDirection");
}

void TestMotionAxisUpdate()
{
    bool ok;
    {
        StandIns standIns({Own(0x004258B0, Ours(&CMotionComponent::RotateAboutAxis), 8, 2)});
        ok = CompareRuns(
            "motion UpdateAxisRotation", 8000,
            [](int) {
                RandomFlags();
                RandomRotationState();
            },
            [] {
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, int)>(0x00425A80)(Motion(), static_cast<int>(g_rng % 3)));
            },
            [] { return static_cast<uint32_t>(Motion()->UpdateAxisRotation(static_cast<int>(g_rng % 3))); });
    }
    {
        StandIns standIns({Own(0x00425A80, Ours(&CMotionComponent::UpdateAxisRotation), 4, 1, Ret::Bool)});
        ok = ok && CompareRuns(
                       "motion UpdateRotations", 300, [](int) { RandomFlags(); },
                       [] {
                           Original<void(__thiscall*)(void*)>(0x00425C70)(Motion());
                           return 0u;
                       },
                       [] {
                           Motion()->UpdateRotations();
                           return 0u;
                       });
    }
    {
        StandIns standIns({CreateRotate(0x00404DD0, &Mth::CreateRotateXMatrix),
                           CreateRotate(0x00404E20, &Mth::CreateRotateYMatrix),
                           CreateRotate(0x00404E70, &Mth::CreateRotateZMatrix), MatrixMultiply(), OrthoNormalize()});
        ok = ok && CompareRuns(
                       "motion RotateAboutAxis", 400, [](int) {},
                       [] {
                           int axis = static_cast<int>(g_rng % 5) - 1;
                           float angle = RichFloat(3.0f);
                           Original<void(__thiscall*)(void*, int, float)>(0x004258B0)(Motion(), axis, angle);
                           return 0u;
                       },
                       [] {
                           int axis = static_cast<int>(g_rng % 5) - 1;
                           float angle = RichFloat(3.0f);
                           Motion()->RotateAboutAxis(axis, angle);
                           return 0u;
                       });
    }
    Check(ok, "motion component: UpdateAxisRotation (speeds, decelerations, targets, forever), UpdateRotations, "
              "RotateAboutAxis");
}

// --- Obj_Rotate's turn ---
void TestMotionTurn()
{
    bool savedReady = GameVariable<bool>(0x007CC979);
    StandIns standIns(
        {GetFloat(), {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(&Mem::EnsureManager)}},
         {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kAlloc)); }},
         {0x00405990, 8, 2, Ret::Int, true, 0, {Ours(&Mth::SlerpInterpolator::SetMatrices)}},
         {0x00405A20, 8, 2, Ret::None, true, 0, {Ours(&Mth::SlerpInterpolator::GetMatrix)}},
         {0x00425600, 4, 1, Ret::None, true, 0, {Ours(&CMotionComponent::RotateTo)}}});
    bool ok = CompareRuns(
        "motion StartRotation", 400,
        [](int) {
            ArenaAt<uint8_t*>(kMotion + 0x1CC) = Next() % 2 ? ArenaPtr(kMine) : nullptr;
            GameVariable<bool>(0x007CC979) = Next() % 2 != 0;
            RandomFlags();
            if (Next() % 2)
                Give(0x906B67BA, RichFloat(3.0f));
        },
        [] {
            Original<void(__thiscall*)(void*, void*, void*)>(0x00425520)(Motion(), Params(), ArenaPtr(kMine + 0x200));
            return 0u;
        },
        [] {
            Motion()->StartRotation(Params(), reinterpret_cast<Mth::Matrix*>(ArenaPtr(kMine + 0x200)));
            return 0u;
        });
    GameVariable<bool>(0x007CC979) = savedReady;
    ok = ok && CompareRuns(
                   "motion UpdateTurn", 3000,
                   [](int) {
                       RandomFlags();
                       ArenaAt<uint8_t*>(kMotion + 0x1CC) = ArenaPtr(kMine);
                       Motion()->m_frameTime = Pick({1.0f / 60, 0.0f, 0.25f});
                       Motion()->m_turnTime = Pick({0.0f, 1.0f, 0.5f, 0.01f, -1.0f});
                       Motion()->m_turnElapsed = Pick({0.0f, 0.4f, 0.99f, 1.0f, 2.0f, -1.0f});
                   },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x00424440)(Motion());
                       return 0u;
                   },
                   [] {
                       Motion()->UpdateTurn();
                       return 0u;
                   });
    ok = ok && CompareMember("Obj_Rotate", 0x00426770, &CMotionComponent::Obj_Rotate, 20, [](int) {});
    Check(ok, "motion component: StartRotation (an interpolator or not, the manager ready or not), UpdateTurn, "
              "Obj_Rotate");
}

// --- looking ---
// The manager of the object (+0x0C) and its list of objects (kMine + 0x400 on): `count` objects with ids.
void BuildObjectList(uint32_t count)
{
    uint32_t manager = kMine + 0x400, vtable = kMine + 0x440, nodes = kMine + 0x480;
    ArenaAt<uint8_t*>(kObj + 0x0C) = ArenaPtr(manager);
    ArenaAt<uint8_t*>(manager) = ArenaPtr(vtable);
    ArenaAt<uint32_t>(vtable + 6 * 4) = 0x004AAD40; // (lea eax, [ecx + 4]: the head)
    uint32_t head = manager + 4;
    ArenaAt<uint32_t>(head + 4) = 0xFFFFFFFF;
    uint32_t previous = head;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t node = nodes + i * 0x14;
        uint32_t choice = Next() % 4;
        uint32_t object = choice == 0 ? kObj : choice == 1 ? kOtherObj : kMine + 0x600 + 0x20 * i;
        ArenaAt<uint8_t*>(node + 4) = ArenaPtr(object);
        if (object != kObj && object != kOtherObj)
            ArenaAt<uint32_t>(object + 0x14) = 0x100 + Next() % 4;
        ArenaAt<uint8_t*>(previous + 0x0C) = ArenaPtr(node);
        previous = node;
    }
    ArenaAt<uint8_t*>(previous + 0x0C) = ArenaPtr(head);
    ArenaAt<uint32_t>(kObj + 0x14) = 0x100 + Next() % 4;
    ArenaAt<uint32_t>(kOtherObj + 0x14) = 0x100 + Next() % 4;
}

void TestMotionLookAt()
{
    bool ok;
    {
        StandIns standIns({AngleInPlane()});
        ok = CompareRuns(
            "motion SetupLookAxis", 3000,
            [](int) {
                RandomFlags();
                RandomRotationState();
            },
            [] {
                int reference = static_cast<int>(g_rng % 3), axis = static_cast<int>(g_rng / 3 % 3);
                float threshold = Pick({0.0f, 0.5f, 1.0f, -1.0f});
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, void*, void*, int, int, float)>(0x004240E0)(
                        Motion(), ArenaPtr(kMine), ArenaPtr(kMine + 0x10), reference, axis, threshold));
            },
            [] {
                int reference = static_cast<int>(g_rng % 3), axis = static_cast<int>(g_rng / 3 % 3);
                float threshold = Pick({0.0f, 0.5f, 1.0f, -1.0f});
                return static_cast<uint32_t>(Motion()->SetupLookAxis(
                    reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine)), reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x10)),
                    reference, axis, threshold));
            });
    }
    {
        StandIns standIns({GetFloat(), GetInteger(), ContainsFlag(), AngleInPlane(),
                           CreateRotate(0x00404E20, &Mth::CreateRotateYMatrix), MatrixMultiply(),
                           Own(0x004240E0, Ours(&CMotionComponent::SetupLookAxis), 20, 5, Ret::Bool)});
        ok = ok && CompareRuns(
                       "motion LookAt", 8000,
                       [](int) {
                           RandomFlags();
                           RandomRotationState();
                           if (Next() % 2)
                               Give(0xF73A4EFD, RichFloat(90.0f));
                           if (Next() % 4 == 0)
                               GiveFlag(0xF16FB852);
                           if (Next() % 2)
                               Give(0x906B67BA, Next() % 3 ? 0.0f : RichFloat(5.0f));
                           if (Next() % 2)
                               GiveInteger(0xF0D90109, static_cast<int>(Next() % 720) - 360);
                           if (Next() % 2)
                               GiveInteger(0x85894A84, Next() % 3 ? 0 : static_cast<int>(Next() % 200) - 100);
                           if (Next() % 2)
                               GiveInteger(0xA4AECB20, static_cast<int>(Next() % 8));
                       },
                       [] {
                           return static_cast<uint32_t>(Original<bool(__thiscall*)(void*, void*, void*)>(0x004252E0)(
                               Motion(), Params(), ArenaPtr(kMine)));
                       },
                       [] {
                           return static_cast<uint32_t>(
                               Motion()->LookAt(Params(), reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine))));
                       });
    }
    {
        using LinkCountFn = uint32_t (*)(uint32_t);
        using LinkedNodeFn = Script::CStruct* (*)(uint32_t, int);
        using PositionStructFn = bool (*)(Script::CStruct*, Mth::Vector*);
        using PositionNameFn = bool (*)(uint32_t, Mth::Vector*);
        StandIns standIns(
            {GetInteger(), GetChecksum(), GetVector(), ContainsFlag(),
             {0x004252E0, 8, 2, Ret::Bool, true, 0, {Ours(&CMotionComponent::LookAt)},
              [](Frame& f, Result&) {
                  for (int i = 0; i < 4; ++i) // (the position looked at)
                      Record(reinterpret_cast<const uint32_t*>(f.args[1])[i]);
              }},
             {0x0045C3A0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<LinkCountFn>(&SkateScript::GetLinkCount))},
              [](Frame&, Result& r) { r.eax = Next() % 5; }},
             {0x0045C460, 0, 2, Ret::Int, false, 0, {Ours(static_cast<LinkedNodeFn>(&SkateScript::GetLinkedNode))}},
             {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)}},
             NodePosition(0x0045C050, Ours(static_cast<PositionStructFn>(&SkateScript::GetPosition))),
             NodePosition(0x0045C560, Ours(static_cast<PositionNameFn>(&SkateScript::GetPosition))),
             {0x0045ED30, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::FindNearestObjectOfType)},
              [](Frame&, Result& r) { r.eax = Next() % 3 ? reinterpret_cast<uint32_t>(ArenaPtr(kOtherObj)) : 0; }}});
        auto lookParams = [](int) {
            RandomFlags();
            if (Next() % 2)
                GiveInteger(0xE85997D0, static_cast<int>(Next() % 7) - 1);
            if (Next() % 3)
                GiveChecksum(0xA1DC81F9, Next() % 3 ? 0x100 + Next() % 4 : 0);
            if (Next() % 2)
                GiveVector(0, RichFloat(100.0f), RichFloat(100.0f), RichFloat(100.0f));
            if (Next() % 2)
                GiveFlag(0x26AF9DC9);
            static const uint32_t types[] = {0x5B8AB877, 0x061A741E, 0x3B5737A6, 0x88C21962, 0x12345678};
            if (Next() % 3 == 0)
                GiveChecksum(0x7321A8D6, types[Next() % 5]);
            BuildObjectList(Next() % 6);
        };
        ok = ok && CompareMember("Obj_LookAtNode", 0x00426790, &CMotionComponent::Obj_LookAtNode, 400, lookParams);
        ok = ok && CompareMember("Obj_LookAtNodeLinked", 0x004267B0, &CMotionComponent::Obj_LookAtNodeLinked, 1000,
                                 lookParams);
        ok = ok && CompareMember("Obj_LookAtNodeStored", 0x004267D0, &CMotionComponent::Obj_LookAtNodeStored, 100,
                                 lookParams);
        ok = ok && CompareMember("Obj_LookAtPos", 0x00426810, &CMotionComponent::Obj_LookAtPos, 400, lookParams);
        ok = ok && CompareMember("Obj_LookAtPosStored", 0x00426880, &CMotionComponent::Obj_LookAtPosStored, 50,
                                 lookParams);
        ok = ok && CompareMember("Obj_LookAtRelPos", 0x004268A0, &CMotionComponent::Obj_LookAtRelPos, 400, lookParams);
        ok = ok && CompareMember("Obj_LookAtObject", 0x004276A0, &CMotionComponent::Obj_LookAtObject, 4000,
                                 lookParams);
    }
    {
        StandIns standIns({CreateRotate(0x00404DD0, &Mth::CreateRotateXMatrix),
                           CreateRotate(0x00404E20, &Mth::CreateRotateYMatrix),
                           CreateRotate(0x00404E70, &Mth::CreateRotateZMatrix), MatrixMultiply(),
                           {0x0045C180, 0, 2, Ret::Bool, false, 0,
                            {Ours(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetAngles))},
                            [](Frame& f, Result&) {
                                auto* out = reinterpret_cast<float*>(f.args[1]);
                                for (int i = 0; i < 3; ++i)
                                    out[i] = Next() % 2 ? 0.0f : RichFloat(3.0f);
                                out[3] = 1.0f;
                            }}});
        ok = ok && CompareRuns(
                       "motion OrientToNode", 400, [](int) {},
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x00425970)(Motion(), Params());
                           return 0u;
                       },
                       [] {
                           Motion()->OrientToNode(Params());
                           return 0u;
                       });
    }
    Check(ok, "motion component: SetupLookAxis, LookAt (instant, time, speed, acceleration, axes), the look-at member "
              "functions, OrientToNode");
}

} // namespace

SELFTEST(Early, TestMotionRotAxes);
SELFTEST(Early, TestMotionAxisUpdate);
SELFTEST(Early, TestMotionTurn);
SELFTEST(Early, TestMotionLookAt);
