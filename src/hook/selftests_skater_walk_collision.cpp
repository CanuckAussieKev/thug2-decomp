// Self-tests for the Walk component's collision checks and the rest of its movement (game/skater/walk_collision.cpp,
// walk_movement.cpp): the original against ours on the same stand-in component, objects and callees (sk5_testkit.h).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/node_components.h"
#include "game/obj/shadow_component.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/trick_component.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/sk5_testkit.h"

namespace {

using namespace Sk5;
using Obj::CWalkComponent;
using Script::CStruct;

constexpr uint32_t kComponent = 0x0000;  // (0x1960)
constexpr uint32_t kObject = 0x2000;     // the skater (0x110)
constexpr uint32_t kInput = 0x2200;      // (+0x20 the pad)
constexpr uint32_t kModel = 0x2700;      // (+0x1F8)
constexpr uint32_t kCore = 0x2800;       // the core physics (+0x4E0)
constexpr uint32_t kState = 0x2900;      // the skater state (+0x11C)
constexpr uint32_t kControl = 0x2A00;    // the physics control (+0x24, +0x26)
constexpr uint32_t kCamera = 0x3000;
constexpr uint32_t kSkate = 0x3200;      // the skate module (+0x24 its rail manager)
constexpr uint32_t kRails = 0x3300;      // the level's rail manager
constexpr uint32_t kRailComps = 0x3400;  // two RailManager components (0x40 each: vtable, +0x0C object, +0x14 next)
constexpr uint32_t kHitObject = 0x3600;  // an object a contact feeler hit (+0xAC velocity)
constexpr uint32_t kTokens = 0x4000;
constexpr uint32_t kOther = 0x5000;      // components of no interest

float SharedFloat()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.0f};
    return values[Next() % 4];
}

void* g_railsOfThunk; // the RailManager components' vtable +0x34
void* g_callbackThunk; // the transition's member function

void StandIns(Kit& kit)
{
    kit.Add({0x0045DB90}, 4, [](Frame& f) {
        Record(0x45DB90);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Arena() + kOther + (f.args[0] & 0xF0)));
    }, {Code(&Obj::CCompositeObject::GetComponent)});
    kit.Add({0x004766D0}, 12, [](Frame& f) {
        Record(0x4766D0);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        Record(Relative(f.args[1]));
        Record(f.args[2]);
        *reinterpret_cast<float*>(f.args[1]) = Chance(50) ? RandomFloat(4.0f) : SharedFloat();
        uint32_t found = Next() & 1;
        Record(found);
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))});
    kit.Add({0x00479020}, 0, [](Frame& f) {
        Record(0x479020);
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Arena() + kTokens));
    }, {Code(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))});
    kit.Add({0x00478F00}, 0, [](Frame& f) {
        Record(0x478F00);
        Record(f.args[0]);
        float value = Chance(50) ? SharedFloat() * 100.0f : RandomFloat(300.0f);
        RecordFloat(value);
        return Float(value);
    }, {Code(static_cast<float (*)(uint32_t)>(&Script::GetFloat))});
    kit.Recorder(0x00478F60, {0, 1, Ret::Int}, {Code(static_cast<int (*)(uint32_t)>(&Script::GetInteger))});
    kit.Add({0x0044ECE0}, 0, [](Frame&) {
        Record(0x44ECE0);
        static const uint32_t times[] = {0, 99, 100, 150, 1000, 5000, 0xFFFFFFF0u, 200};
        uint32_t t = times[Next() % 8];
        Record(t);
        return Int(t);
    }, {Code(&Tmr::GameClockElapsed)});
    kit.Recorder(0x005BEAA0, {0, 0, Ret::Float}, {Code(&Tmr::GetSlomoFrameSeconds)});
    kit.Add({0x004172F0}, 8, [](Frame& f) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
        Record(0x4172F0);
        Record(Relative(f.ecx));
        Record(f.args[0] & 0xFF);
        Record(f.args[1] & 0xFF);
        const uint32_t* line = reinterpret_cast<const uint32_t*>(&feeler->m_line);
        for (int i = 0; i < 8; ++i)
            Record(line[i]);
        Record(feeler->m_hit.ignoreFlags | feeler->m_hit.requireFlags << 16);
        uint32_t hit = Next() % 3 != 0 ? 1u : 0u;
        Record(hit);
        feeler->m_distance = Chance(20) ? SharedFloat() : RandomFloat(1.0f);
        for (float* v : {&feeler->m_point.x, &feeler->m_normal.x})
            for (int i = 0; i < 4; ++i)
                v[i] = Chance(20) ? SharedFloat() : RandomFloat(10.0f);
        if (Chance(10)) {
            // hit exactly 6 along x from the line's end (the grind's clearance)
            feeler->m_point = feeler->m_line.end;
            feeler->m_point.x = feeler->m_point.x + 6.0f;
        }
        if (Chance(15)) // something hit that the feeler holds on to (let go by its destructor)
            *reinterpret_cast<uint8_t**>(&feeler->m_object) = Arena() + kOther + 0x200;
        return Int(hit);
    }, {Code(&Nx::CFeeler::GetCollision)});
    kit.Recorder(0x004637E0, {4, 1, Ret::None, true}, {Code(&Obj::CRefCounted::RemoveSmartPointer)});
    kit.Add({0x004609F0}, 8, [](Frame& f) {
        Record(0x4609F0);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        Record(f.args[1]);
        return Result();
    }, {Code(&Obj::CObject::SelfEvent)});
    kit.Add({0x00401C30}, 0, [](Frame& f) {
        Record(0x401C30);
        // (the arguments each format takes: the rest is the caller's stack)
        int words;
        switch (f.args[0]) {
        case 0x0064C66C:
            words = 1;
            break;
        case 0x006466B0:
            words = 4;
            break;
        case 0x0064B998:
            words = 14;
            break;
        default:
            words = 9;
            break;
        }
        for (int i = 0; i < words; ++i)
            Record(f.args[i]);
        return Int(0xFFFFFFFFu);
    }, {Code(&Dbg::Printf)});
    kit.Recorder(0x00507500, {12, 3, Ret::Bool, true}, {Code(&Skater::CorePhysicsComponent::WallPlant)});
    kit.Recorder(0x00500020, {12, 3, Ret::Bool, true}, {Code(&Skater::CorePhysicsComponent::CanGrindRail)});
    kit.Recorder(0x00475790, {0, 5, Ret::None},
                 {Code(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))});
    kit.Add({0x0046B920}, 0, [](Frame& f) {
        Record(0x46B920);
        Record(Relative(f.ecx));
        static const int times[] = {0, 30, 31, 100, 500, -5, 1000};
        int t = times[Next() % 7];
        Record(static_cast<uint32_t>(t));
        return Int(static_cast<uint32_t>(t));
    }, {Code(&Obj::CControlButton::GetPressedTime)});
    kit.Recorder(0x00420670, {12, 3, Ret::None, true, -1, -1, 0, 2}, {Code(&Obj::CModelComponent::ApplyBrightness)});
    kit.Recorder(0x004317C0, {4, 1, Ret::None, true, 0, -1, 4}, {Code(&Obj::CShadowComponent::SetPosition)});
    kit.Recorder(0x004317E0, {4, 1, Ret::None, true, 0, -1, 4}, {Code(&Obj::CShadowComponent::SetNormal)});
    kit.Recorder(0x00434BF0, {8, 2, Ret::None, true}, {Code(&Obj::CTriggerComponent::CheckFeeler)});
    kit.Recorder(0x00522370, {4, 1, Ret::None, true}, {Code(&Obj::CTrickComponent::PassToTrickObjects)});
    kit.Add({0x005284C0}, 0, [](Frame&) {
        Record(0x5284C0);
        static const double values[] = {1.0, 0.5, -0.25, 2.0 / 3.0, 0.0};
        return Float(values[Next() % 5]);
    }, {Code(&Obj::CWalkComponent::GetGravity)});
    kit.Add({0x0057E2D0}, 28, [](Frame& f) {
        Record(0x57E2D0);
        Record(Relative(f.ecx));
        for (int i = 0; i < 2; ++i) {
            const uint32_t* v = reinterpret_cast<const uint32_t*>(f.args[i]);
            for (int j = 0; j < 4; ++j)
                Record(v[j]);
        }
        for (int i = 2; i < 7; ++i)
            Record(Relative(f.args[i]));
        bool found = Chance(40);
        Record(found);
        if (found) {
            float* point = reinterpret_cast<float*>(f.args[2]);
            for (int j = 0; j < 4; ++j)
                point[j] = Chance(30) ? SharedFloat() : RandomFloat(10.0f);
            *reinterpret_cast<uint32_t*>(f.args[3]) = reinterpret_cast<uint32_t>(Arena() + kOther + 0x300);
        }
        return Int(found);
    }, {Code(&Obj::CRailManager::FindGrindRail)});
    kit.Add({0x0045F2D0}, 4, [](Frame& f) {
        Record(0x45F2D0);
        Record(f.args[0]);
        return Int(Chance(80) ? reinterpret_cast<uint32_t>(Arena() + kRailComps) : 0u);
    }, {Code(&Obj::CCompositeObjectManager::GetFirstComponentByType)});
    kit.Add({0x0042BA10}, 4, [](Frame& f) {
        Record(0x42BA10);
        Record(Relative(f.ecx));
        float* m = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 1.0f + RandomFloat(0.1f) : RandomFloat(0.2f);
        m[12] = RandomFloat(20.0f);
        m[13] = RandomFloat(20.0f);
        m[14] = RandomFloat(20.0f);
        return Int(f.args[0]);
    }, {Code(&Obj::CRailManagerComponent::UpdateRailMatrix)});
    // Matrix::Invert (ours isn't bit-exact with the original's x87 adjugate and determinant - outside this range):
    // a stand-in that records the matrix and changes it in a fixed way.
    kit.Add({0x00405600}, 0, [](Frame& f) {
        Record(0x405600);
        Record(Relative(f.ecx));
        float* m = reinterpret_cast<float*>(f.ecx);
        for (int i = 0; i < 16; ++i)
            RecordFloat(m[i]);
        for (int i = 0; i < 16; ++i)
            m[i] = -m[i] * 0.5f;
        return Int(f.ecx);
    }, {Code(&Mth::Matrix::Invert)});
    g_railsOfThunk = kit.Add({}, 0, [](Frame& f) {
        Record(0xCA000034);
        Record(Relative(f.ecx));
        return Int(reinterpret_cast<uint32_t>(Arena() + kOther + 0x400));
    });
    g_callbackThunk = kit.Add({}, 0, [](Frame& f) {
        Record(0xCA001948);
        Record(Relative(f.ecx));
        return Result();
    });
    kit.Add({0x00623630}, 0, [](Frame&) {
        double argument = St0Argument();
        Record(0x623630);
        uint32_t halves[2];
        std::memcpy(halves, &argument, 8);
        Record(halves[0]);
        Record(halves[1]);
        return Float(std::acos(argument));
    }, {}, true);
    kit.Add({0x0043D990}, 0, [](Frame& f) {
        Record(0x43D990);
        Record(Relative(f.args[0]));
        for (int i = 1; i <= 2; ++i) {
            Record(Relative(f.args[i]));
            const uint32_t* v = reinterpret_cast<const uint32_t*>(f.args[i]);
            for (int j = 0; j < 4; ++j)
                Record(v[j]);
        }
        Record(f.args[3]);
        float* out = reinterpret_cast<float*>(f.args[0]);
        for (int j = 0; j < 4; ++j)
            out[j] = RandomFloat(2.0f);
        return Int(f.args[0]);
    }, {Code(&Walk::Lerp)});
    kit.Add({0x0043DAA0}, 0, [](Frame& f) {
        Record(0x43DAA0);
        Record(Relative(f.ecx));
        static const double values[] = {0.0, 1.0, 2.0 / 3.0, 300.5, 12.000000000000002};
        return Float(values[Next() % 5]);
    }, {Code(&Obj::CWalkComponent::RunSpeed)});
}

// The globals: the skate module pointer.
struct StandInGlobals {
    uint32_t skate;
    StandInGlobals() { skate = GameVariable<uint32_t>(0x007CE478); }
    ~StandInGlobals() { GameVariable<uint32_t>(0x007CE478) = skate; }
};

void Fill()
{
    FillArena();
    Pointer(kComponent + 0x0C, kObject);
    Pointer(kComponent + 0x1770, kInput);
    Pointer(kComponent + 0x1778, kModel);
    Pointer(kComponent + 0x177C, kOther + 0x500);
    Pointer(kComponent + 0x1780, kControl);
    Pointer(kComponent + 0x1788, kState);
    Pointer(kComponent + 0x178C, kCore);
    Pointer(kComponent + 0x175C, kCamera);
    Pointer(kSkate + 0x24, kRails);
    // two RailManager components
    static void* vtable[16];
    vtable[13] = g_railsOfThunk;
    for (int i = 0; i < 2; ++i) {
        uint32_t c = kRailComps + i * 0x40;
        At<void**>(c) = vtable;
        Pointer(c + 0x0C, kOther + 0x600 + i * 0x10);
        if (i == 0)
            Pointer(c + 0x14, kRailComps + 0x40);
        else
            At<uint32_t>(c + 0x14) = 0;
    }
    At<int>(kComponent + 0x18) = static_cast<int>(Next() % 7) - 1;
    At<int>(kComponent + 0x1C) = static_cast<int>(Next() % 400) - 200;
    At<int>(kCore + 0x4E0) = static_cast<int>(Next() % 400) - 200;
    uint32_t pad = kInput + 0x20;
    At<uint8_t>(pad + 0x1A0) = Chance(70) ? 1 : 0; // X
    At<uint8_t>(pad + 0x20) = Chance(70) ? 1 : 0;  // the +0x20 button
    At<uint8_t>(kControl + 0x24) = Chance(20) ? 1 : 0;
    At<uint8_t>(kControl + 0x26) = Chance(30) ? 1 : 0;
    At<uint8_t>(kComponent + 0x18B0) = Chance(50) ? 1 : 0;
    At<uint8_t>(kComponent + 0x17A4) = Chance(50) ? 1 : 0;
    At<uint8_t>(kComponent + 0x186C) = Chance(30) ? 1 : 0;
    At<uint8_t>(kComponent + 0x1958) = Chance(30) ? 1 : 0;
    if (Chance(30))
        At<float>(kComponent + 0x44) = Chance(50) ? 0.0f : -SharedFloat();
    if (Chance(40))
        At<float>(kComponent + 0x94) = SharedFloat();
    if (Chance(30))
        At<float>(kComponent + 0x714) = Chance(50) ? 0.0f : (Chance(50) ? 0.01f : -0.005f);
    if (Chance(30))
        At<float>(kComponent + 0x1898) = 0.0f;
    else if (Chance(30))
        At<float>(kComponent + 0x1898) = At<float>(kComponent + 0x80);
    if (Chance(30))
        At<float>(kComponent + 0x72C) = Chance(50) ? 0.0f : At<float>(kComponent + 0x80);
    if (Chance(30))
        for (int i = 0; i < 3; ++i)
            At<float>(kComponent + 0x71C + 4 * i) = 0.0f;
    if (Chance(20))
        At<float>(kComponent + 0x744) = Chance(50) ? 1.0f : -1.0f;
    // the velocities: zero, along x by a shared value, or random
    if (Chance(20))
        for (int i = 0; i < 4; ++i)
            At<float>(kComponent + 0x34 + 4 * i) = 0.0f;
    else if (Chance(30)) {
        At<float>(kComponent + 0x34) = SharedFloat();
        At<float>(kComponent + 0x38) = 0.0f;
        At<float>(kComponent + 0x3C) = 0.0f;
    }
    // the contacts: some touching, their feelers' objects held or not
    for (int i = 0; i < 8; ++i) {
        uint32_t contact = kComponent + 0xA0 + i * 0xCC;
        At<uint8_t>(contact) = Chance(25) ? 1 : 0;
        At<uint32_t>(contact + 0xB4) = Chance(50) ? 0u : reinterpret_cast<uint32_t>(Arena() + kHitObject);
    }
    // the facing, control and direction: the same, opposite, or apart
    if (Chance(30)) {
        At<Mth::Vector>(kComponent + 0x84) = At<Mth::Vector>(kComponent + 0x48);
    } else if (Chance(20)) {
        At<float>(kComponent + 0x84) = -At<float>(kComponent + 0x48);
        At<float>(kComponent + 0x8C) = -At<float>(kComponent + 0x50);
    }
    // equal compares: the height above the ground against the script values, the critical point's offset
    // eased by exactly its length, the push plane through it, a move of exactly 0.01, nothing carried by the jump
    if (Chance(30))
        At<float>(kState + 0x11C) = SharedFloat() * 100.0f;
    if (Chance(20)) {
        At<float>(kComponent + 0x80) = 0.5f;
        At<Mth::Vector>(kComponent + 0x71C) = {500.0f, 0.0f, 0.0f, 1.0f};
        At<Mth::Vector>(kComponent + 0x18B4) = {0.0f, 1.0f, 0.0f, 1.0f};
        At<float>(kComponent + 0x18C4) = 0.0f;
        At<float>(kComponent + 0x28) = 0.0f;
    }
    if (Chance(15)) {
        At<Mth::Vector>(0x5900) = {0.01f, 0.0f, 0.0f, 1.0f};
        At<Mth::Vector>(kComponent + 0x34) = {0.01f, 0.0f, 0.0f, 1.0f};
    }
    if (Chance(30))
        At<Mth::Vector>(kComponent + 0x1888) = {0.0f, 0.0f, 0.0f, 0.0f};
    // the transition's member function
    if (Chance(50)) {
        At<void*>(kComponent + 0x1948) = g_callbackThunk;
        At<int>(kComponent + 0x194C) = Chance(50) ? 0 : 0x20;
    } else {
        At<uint32_t>(kComponent + 0x1948) = 0;
    }
}

template <typename Original, typename Ours>
bool Compare(const char* what, int variants, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        Seed(0xC2B2AE35u * (variant + 1));
        Fill();
        KeepStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = Run(seed, [&] {
            GameVariable<void*>(0x007CE478) = Arena() + kSkate;
            return original(Arena() + kComponent);
        });
        std::vector<uint32_t> actual = Run(seed, [&] {
            GameVariable<void*>(0x007CE478) = Arena() + kSkate;
            return ours(reinterpret_cast<CWalkComponent*>(Arena() + kComponent));
        });
        if (!Same(expected, actual, what, variant))
            return false;
    }
    return true;
}

// (in three parts, each well under the watchdog's 45 s)
void RunWalkCollision(int part)
{
    Kit kit;
    StandIns(kit);
    StandInGlobals globals;
    bool ok = true;
    auto check = [&](const char* what, int variants, auto original, auto ours) {
        ok = ok && Compare(what, variants, original, ours);
    };
    using Self = void*;
    using Void = void(__thiscall*)(Self);
    struct Plain {
        uint32_t address;
        void (CWalkComponent::*ours)();
        const char* name;
        int variants;
        int part;
    };
    static const Plain plain[] = {
        {0x005264F0, &CWalkComponent::UpdateCurbFloat, "UpdateCurbFloat", 3000, 0},
        {0x00526BA0, &CWalkComponent::CheckCeiling, "CheckCeiling", 2000, 0},
        {0x00526E00, &CWalkComponent::CheckStickerSlap, "CheckStickerSlap", 3000, 0},
        {0x00527080, &CWalkComponent::UpdateGroundBelow, "UpdateGroundBelow", 2000, 0},
        {0x005278D0, &CWalkComponent::EaseCriticalPointOffset, "EaseCriticalPointOffset", 2000, 1},
        {0x00529160, &CWalkComponent::PlanJumpOverObstruction, "PlanJumpOverObstruction", 2000, 1},
        {0x005293F0, &CWalkComponent::TryWallplant, "TryWallplant", 4000, 1},
        {0x00526850, &CWalkComponent::SteerJump, "SteerJump", 3000, 1},
        {0x00529800, &CWalkComponent::UpdateRotateUpright, "UpdateRotateUpright", 500, 2},
        {0x005298D0, &CWalkComponent::ApplyTransition, "ApplyTransition", 300, 2},
        {0x00529CE0, &CWalkComponent::CopyToObject, "CopyToObject", 1000, 2},
    };
    for (const Plain& p : plain) {
        if (p.part != part)
            continue;
        static const Plain* current;
        current = &p;
        char what[80];
        std::snprintf(what, sizeof(what), "walk: %s", p.name);
        check(what, p.variants, [](uint8_t* c) { Original<Void>(current->address)(c); return 0u; },
              [](CWalkComponent* c) { (c->*(current->ours))(); return 0u; });
    }
    if (part == 1)
    check("walk: TryGrind", 3000, [](uint8_t* c) { return static_cast<uint32_t>(Original<bool(__thiscall*)(Self)>(0x005299F0)(c)); },
          [](CWalkComponent* c) { return static_cast<uint32_t>(c->TryGrind()); });
    if (part == 2)
    check("walk: UpdateGroundVelocity", 4000,
          [](uint8_t* c) {
              // (the game passes the component's own velocity, +0x34)
              Original<void(__thiscall*)(Self, Self)>(0x00528950)(c, Arena() + (Chance(50) ? 0x34 : 0x5900));
              return 0u;
          },
          [](CWalkComponent* c) {
              c->UpdateGroundVelocity(reinterpret_cast<Mth::Vector*>(Arena() + (Chance(50) ? 0x34 : 0x5900)));
              return 0u;
          });
    Check(ok, part == 0   ? "walk: the Walk component's collision checks (original vs ours)"
              : part == 1 ? "walk: the Walk component's critical point, jumps, wallplant and grind (original vs ours)"
                          : "walk: the Walk component's ground velocity and its object (original vs ours)");
}

void TestWalkCollision() { RunWalkCollision(0); }
void TestWalkJumps() { RunWalkCollision(1); }
void TestWalkObject() { RunWalkCollision(2); }

} // namespace

SELFTEST(Early, TestWalkCollision, Sk5::Cleanup);
SELFTEST(Early, TestWalkJumps, Sk5::Cleanup);
SELFTEST(Early, TestWalkObject, Sk5::Cleanup);
