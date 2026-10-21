// Self-tests for the Walk component's pieces (game/skater/walk_physics.cpp): the original against ours on the same
// stand-in component, objects and callees (sk5_testkit.h).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/objman.h"
#include "game/obj/rail_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
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
constexpr uint32_t kInput = 0x2200;      // (+0x20 the pad: +0x250 / +0x254; +0x228 / +0x22C)
constexpr uint32_t kScript = 0x2600;     // (+0x14 its params)
constexpr uint32_t kModel = 0x2700;      // (+0x1F8)
constexpr uint32_t kBlender = 0x2800;
constexpr uint32_t kScore = 0x2900;      // (+0x18 the score: kScoreData)
constexpr uint32_t kScoreData = 0x2A00;  // (+0x14, +0x60)
constexpr uint32_t kRails = 0x2B00;
constexpr uint32_t kCamera = 0x3000;
constexpr uint32_t kFeeler = 0x3100;     // (0xB0)
constexpr uint32_t kSkate = 0x3200;      // the skate module (+0x20 its career)
constexpr uint32_t kCareer = 0x3300;
constexpr uint32_t kTokens = 0x4000;
constexpr uint32_t kOther = 0x5000;      // components of no interest

uint32_t Token(uint32_t i) { return reinterpret_cast<uint32_t>(Arena() + kTokens + (i % 32) * 0x20); }

// A float from a short list (the stand-in parameters and the component's values both draw from it: equal compares).
float SharedFloat()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.0f};
    return values[Next() % 4];
}

void StandIns(Kit& kit)
{
    kit.Add({0x0045DB90}, 4, [](Frame& f) {
        Record(0x45DB90);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        switch (f.args[0]) {
        case 0x27D7CD28:
            return Int(reinterpret_cast<uint32_t>(Arena() + kInput));
        case 0x7149B753:
            return Int(reinterpret_cast<uint32_t>(Arena() + kBlender));
        case 0x286A8D26:
            return Int(reinterpret_cast<uint32_t>(Arena() + kModel));
        case 0xD262802F:
            return Int(reinterpret_cast<uint32_t>(Arena() + kScore));
        default:
            return Int(reinterpret_cast<uint32_t>(Arena() + kOther + (f.args[0] & 0xF0)));
        }
    }, {Code(&Obj::CCompositeObject::GetComponent)});
    kit.Add({0x004619F0}, 4, [](Frame& f) {
        Record(0x4619F0);
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Arena() + kCamera));
    }, {Code(&Obj::CBaseManager::GetObjectByID)});
    kit.Add({0x00476950}, 12, [](Frame& f) {
        Record(0x476950);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        bool found = Chance(60);
        Record(found);
        if (found)
            *reinterpret_cast<uint32_t*>(f.args[1]) = Next();
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))});
    // (half the time a value from a short list the component's own values are also drawn from: equal compares)
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
    kit.Recorder(0x00476B40, {4, 1, Ret::Bool, true},
                 {Code(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))});
    kit.Recorder(0x00477B80, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))});
    kit.Recorder(0x00477D40, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))});
    kit.Recorder(0x00477C60, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))});
    kit.Add({0x00479020}, 0, [](Frame& f) {
        Record(0x479020);
        Record(f.args[0]);
        return Int(Token(f.args[0]));
    }, {Code(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))});
    kit.Recorder(0x00478F60, {0, 1, Ret::Int}, {Code(static_cast<int (*)(uint32_t)>(&Script::GetInteger))});
    kit.Recorder(0x0046F060, {0, 3}, {Code(&Script::RegisterMemberFunctionTable)});
    kit.Add({0x0044ECE0}, 0, [](Frame&) {
        Record(0x44ECE0);
        static const uint32_t times[] = {0, 99, 100, 149, 150, 151, 1000, 0xFFFFFFF0u};
        uint32_t t = times[Next() % 8];
        Record(t);
        return Int(t);
    }, {Code(&Tmr::GameClockElapsed)});
    kit.Recorder(0x005BE970, {0, 0, Ret::Int}, {Code(&Tmr::GetTime)});
    kit.Recorder(0x005BEAA0, {0, 0, Ret::Float}, {Code(&Tmr::GetSlomoFrameSeconds)});
    kit.Recorder(0x00584CC0, {4, 1, Ret::Bool, true}, {Code(&Obj::CSkaterCareer::GetCheat)});
    kit.Recorder(0x005843A0, {4, 1, Ret::None, true}, {Code(&Obj::CSkaterCareer::SetGlobalFlag)});
    kit.Add({0x0057CB60}, 8, [](Frame& f) {
        Record(0x57CB60);
        Record(Relative(f.ecx));
        Record(f.args[1]);
        float* out = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 4; ++i)
            out[i] = Chance(10) ? 0.0f : RandomFloat(50.0f);
        return Int(f.args[0]);
    }, {Code(&Obj::CRailManager::GetWorldPos)});
    kit.Add({0x00623560}, 0, [](Frame&) {
        double argument = St0Argument();
        Record(0x623560);
        uint32_t halves[2];
        std::memcpy(halves, &argument, 8);
        Record(halves[0]);
        Record(halves[1]);
        return Float(std::asin(argument));
    }, {}, true);
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
        uint32_t hit = Next() & 1;
        Record(hit);
        feeler->m_distance = RandomFloat(1.0f);
        for (float* v : {&feeler->m_point.x, &feeler->m_normal.x})
            for (int i = 0; i < 4; ++i)
                v[i] = RandomFloat(10.0f);
        return Int(hit);
    }, {Code(&Nx::CFeeler::GetCollision)});
    kit.Recorder(0x0040AB40, {0, 0, Ret::None, true}, {Code(&Nx::CCollCache::Destruct)});
    kit.Recorder(0x004637E0, {4, 1, Ret::None, true}, {Code(&Obj::CRefCounted::RemoveSmartPointer)});
    kit.Recorder(0x0045D740, {0, 0, Ret::None, true}, {Code(&Obj::CBaseComponent::DestroyBody)});
    kit.Recorder(0x0043DB90, {0, 0, Ret::None, true});
    kit.Recorder(0x00419B90, {16, 4, Ret::None, true}, {Code(&Obj::CAnimBlenderComponent::FirstChannel47E850)});
}

// The globals: the skate module pointer (its career) and the moon flag.
struct StandInGlobals {
    uint32_t skate;
    uint8_t moon;
    StandInGlobals()
    {
        skate = GameVariable<uint32_t>(0x007CE478);
        moon = GameVariable<uint8_t>(0x007CE4FD);
    }
    ~StandInGlobals()
    {
        GameVariable<uint32_t>(0x007CE478) = skate;
        GameVariable<uint8_t>(0x007CE4FD) = moon;
    }
};

void FillVariant(int variant)
{
    FillArena();
    Pointer(kComponent + 0x0C, kObject);
    Pointer(kComponent + 0x1770, kInput);
    Pointer(kComponent + 0x1774, kBlender);
    Pointer(kComponent + 0x1778, kModel);
    Pointer(kComponent + 0x176C, kRails);
    Pointer(kScore + 0x18, kScoreData);
    Pointer(kSkate + 0x20, kCareer);
    At<int>(kComponent + 0x18) = static_cast<int>(Next() % 7) - 1;
    At<int>(kComponent + 0x20) = static_cast<int>(Next() % 6);
    At<int>(kComponent + 0x1C) = Chance(30) ? GameVariable<int>(0x00696D60) : static_cast<int>(Next() % 400) - 200;
    At<uint8_t>(kComponent + 0x98) = Chance(50) ? 1 : 0;
    if (Chance(50))
        At<float>(kInput + 0x228) = 0.0f;
    if (Chance(50))
        At<float>(kInput + 0x22C) = 0.0f;
    At<int>(kComponent + 0x700) = static_cast<int>(Next() % 4);
    At<uint32_t>(kComponent + 0x78) = Chance(50) ? 0xAF895B3Fu : Next();
    if (Chance(30))
        At<float>(kComponent + 0x704) = 0.0f;
    else if (Chance(30))
        At<float>(kComponent + 0x704) = SharedFloat();
    if (Chance(20))
        At<float>(kComponent + 0x708) = At<float>(kComponent + 0x704);
    if (Chance(10)) {
        At<float>(kComponent + 0x704) = -0.0f; // (the lower of -0 and +0: the original keeps the last frame's)
        At<float>(kComponent + 0x708) = 0.0f;
    }
    At<int>(kScoreData + 0x14) = Chance(50) ? 0 : static_cast<int>(Next() % 100) - 20;
    At<uint32_t>(kComponent + 0x184C) = Chance(50) ? 0u : reinterpret_cast<uint32_t>(Arena() + kOther);
    for (int i = 0; i < 8; ++i)
        At<uint32_t>(kComponent + 0xA0 + i * 0xCC + 0xB4) = Chance(50) ? 0u : reinterpret_cast<uint32_t>(Arena() + kOther + 0x100);
    (void)variant;
}

template <typename Original, typename Ours>
bool Compare(const char* what, int variants, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        Seed(0x9E3779B9u * (variant + 1));
        FillVariant(variant);
        KeepStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = Run(seed, [&] {
            GameVariable<void*>(0x007CE478) = Arena() + kSkate;
            GameVariable<uint8_t>(0x007CE4FD) = 0;
            uint32_t result = original(Arena() + kComponent);
            Record(GameVariable<uint8_t>(0x007CE4FD));
            return result;
        });
        std::vector<uint32_t> actual = Run(seed, [&] {
            GameVariable<void*>(0x007CE478) = Arena() + kSkate;
            GameVariable<uint8_t>(0x007CE4FD) = 0;
            uint32_t result = ours(reinterpret_cast<CWalkComponent*>(Arena() + kComponent));
            Record(GameVariable<uint8_t>(0x007CE4FD));
            return result;
        });
        if (!Same(expected, actual, what, variant))
            return false;
    }
    return true;
}

template <typename R>
uint32_t U(R r)
{
    return static_cast<uint32_t>(r);
}
uint32_t D(double d)
{
    uint32_t bits[2];
    std::memcpy(bits, &d, 8);
    return bits[0] ^ bits[1] * 3;
}

void TestWalkPieces()
{
    Kit kit;
    StandIns(kit);
    StandInGlobals globals;
    bool ok = true;
    auto check = [&](const char* what, int variants, auto original, auto ours) {
        ok = ok && Compare(what, variants, original, ours);
    };
    using Self = void*;
    using Member = bool(__thiscall*)(Self, uint32_t, uint32_t);
    struct MemberFunction {
        uint32_t address;
        bool (CWalkComponent::*ours)(CStruct*, Script::CScript*);
        const char* name;
    };
    static const MemberFunction members[] = {
        {0x00525E90, &CWalkComponent::Walk_Ground, "Walk_Ground"},
        {0x00525EA0, &CWalkComponent::Walk_ScaleAnimSpeed, "Walk_ScaleAnimSpeed"},
        {0x00525F60, &CWalkComponent::Walk_ReleaseHang, "Walk_ReleaseHang"},
        {0x00528580, &CWalkComponent::Walk_GetStateTime, "Walk_GetStateTime"},
        {0x005285C0, &CWalkComponent::Walk_GetSpeedScale, "Walk_GetSpeedScale"},
        {0x005286A0, &CWalkComponent::Walk_GetHangInitAnimType, "Walk_GetHangInitAnimType"},
        {0x005286C0, &CWalkComponent::Walk_GetStateDuration, "Walk_GetStateDuration"},
        {0x00528710, &CWalkComponent::Walk_GetPreviousState, "Walk_GetPreviousState"},
        {0x00528740, &CWalkComponent::Walk_GetState, "Walk_GetState"},
        {0x00528770, &CWalkComponent::Walk_GetHangAngle, "Walk_GetHangAngle"},
        {0x00528860, &CWalkComponent::Walk_SetForwardControlLock, "Walk_SetForwardControlLock"},
    };
    for (const MemberFunction& m : members) {
        static const MemberFunction* current;
        current = &m;
        char what[80];
        std::snprintf(what, sizeof(what), "walk: %s", m.name);
        check(what, m.address == 0x00528770 || m.address == 0x005285C0 ? 2000 : 300,
              [](uint8_t* c) {
                  return U(Original<Member>(current->address)(c, Token(1), reinterpret_cast<uint32_t>(Arena() + kScript)));
              },
              [](CWalkComponent* c) {
                  return U((c->*(current->ours))(reinterpret_cast<CStruct*>(Token(1)),
                                                 reinterpret_cast<Script::CScript*>(Arena() + kScript)));
              });
    }
    check("walk: InitFromStructure", 300,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, uint32_t)>(0x00526060)(c, Token(1)); return 0u; },
          [](CWalkComponent* c) { c->InitFromStructure(reinterpret_cast<CStruct*>(Token(1))); return 0u; });
    check("walk: Finalize", 50, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00525FB0)(c); return 0u; },
          [](CWalkComponent* c) { c->Finalize(); return 0u; });
    check("walk: SetCamera", 50,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, Self)>(0x00525E60)(c, Arena() + kCamera); return 0u; },
          [](CWalkComponent* c) { c->SetCamera(reinterpret_cast<Obj::CCompositeObject*>(Arena() + kCamera)); return 0u; });
    check("walk: SetState", 3000,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, int)>(0x005260E0)(c, static_cast<int>(Next() % 6) - 1); return 0u; },
          [](CWalkComponent* c) { c->SetState(static_cast<int>(Next() % 6) - 1); return 0u; });
    check("walk: ClearModelOffset", 30, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x005260C0)(c); return 0u; },
          [](CWalkComponent* c) { c->ClearModelOffset(); return 0u; });
    check("walk: GetFacing", 30,
          [](uint8_t* c) { return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(Self, Self)>(0x00528560)(c, Arena() + 0x5800)); },
          [](CWalkComponent* c) { return reinterpret_cast<uint32_t>(c->GetFacing(reinterpret_cast<Mth::Vector*>(Arena() + 0x5800))); });
    check("walk: GetTurnedFacing", 2000,
          [](uint8_t* c) {
              return reinterpret_cast<uint32_t>(
                  Original<void*(__thiscall*)(Self, Self, int)>(0x00528220)(c, Arena() + 0x5800, static_cast<int>(Next() % 9) - 1));
          },
          [](CWalkComponent* c) {
              return reinterpret_cast<uint32_t>(
                  c->GetTurnedFacing(reinterpret_cast<Mth::Vector*>(Arena() + 0x5800), static_cast<int>(Next() % 9) - 1));
          });
    check("walk: UpdateRunAdjustment", 1000,
          [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00528370)(c); return 0u; },
          [](CWalkComponent* c) { c->UpdateRunAdjustment(); return 0u; });
    check("walk: LimitSpeed", 2000,
          [](uint8_t* c) {
              Original<void(__thiscall*)(Self, uint32_t)>(0x00528400)(c, Chance(50) ? 0x1D537EFFu : Next());
              return 0u;
          },
          [](CWalkComponent* c) { c->LimitSpeed(Chance(50) ? 0x1D537EFFu : Next()); return 0u; });
    check("walk: GetGravity", 300, [](uint8_t*) { return D(Original<double(__cdecl*)()>(0x005284C0)()); },
          [](CWalkComponent*) { return D(CWalkComponent::GetGravity()); });
    check("walk: FindStandPosition", 500,
          [](uint8_t*) {
              return U(Original<bool(__stdcall*)(Self, Self, Self)>(0x00528280)(Arena() + 0x5900, Arena() + 0x5800, Arena() + kFeeler));
          },
          [](CWalkComponent*) {
              return U(CWalkComponent::FindStandPosition(reinterpret_cast<Mth::Vector*>(Arena() + 0x5900),
                                                         reinterpret_cast<Mth::Vector*>(Arena() + 0x5800),
                                                         reinterpret_cast<Nx::CFeeler*>(Arena() + kFeeler)));
          });
    check("walk: UsingDpad", 300, [](uint8_t* c) { return U(Original<bool(__thiscall*)(Self)>(0x00524E10)(c)); },
          [](CWalkComponent* c) { return U(c->UsingDpad()); });
    check("walk: GetCameraOffset", 3000,
          [](uint8_t* c) { return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(Self, Self)>(0x00524E60)(c, Arena() + 0x5800)); },
          [](CWalkComponent* c) { return reinterpret_cast<uint32_t>(c->GetCameraOffset(reinterpret_cast<Mth::Vector*>(Arena() + 0x5800))); });
    check("walk: DestroyBody", 200, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x005288B0)(c); return 0u; },
          [](CWalkComponent* c) { c->DestroyBody(); return 0u; });
    check("walk: contact", 100,
          [](uint8_t*) {
              Original<void(__thiscall*)(Self)>(0x00528890)(Arena() + 0xA0);
              return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(Self)>(0x00528880)(Arena() + 0xA0 + 0xCC));
          },
          [](CWalkComponent*) {
              reinterpret_cast<Obj::WalkContact*>(Arena() + 0xA0)->Destroy();
              return reinterpret_cast<uint32_t>(reinterpret_cast<Obj::WalkContact*>(Arena() + 0xA0 + 0xCC)->Construct());
          });
    check("walk: RegisterMemberFunctions", 20, [](uint8_t*) { Original<void(__cdecl*)()>(0x00525E40)(); return 0u; },
          [](CWalkComponent*) { CWalkComponent::RegisterMemberFunctions(); return 0u; });
    Check(ok, "walk: the Walk component's small pieces (original vs ours)");
}

// --- the movement (walk_movement.cpp) ---

// More stand-ins: Lerp (0x0043D990), the collision cache's Update, StickLength, the run speed (0x0043DAA0).
void MovementStandIns(Kit& kit)
{
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
    });
    kit.Add({0x0040ABA0}, 4, [](Frame& f) {
        Record(0x40ABA0);
        Record(Relative(f.ecx));
        const uint32_t* box = reinterpret_cast<const uint32_t*>(f.args[0]);
        for (int j = 0; j < 8; ++j)
            Record(box[j]);
        return Result();
    }, {Code(&Nx::CCollCache::Update)});
    kit.Add({0x0046C370}, 12, [](Frame& f) {
        Record(0x46C370);
        for (int j = 0; j < 3; ++j)
            Record(f.args[j]);
        // (unrounded values, and around 0.85 - the full push after x 1/0.85)
        static const double values[] = {0.0, 0.5, 0.85, 0.85000002384185791, 0.8499999, 1.0, 1.0 / 3.0, 0.7071067811865476};
        double value = values[Next() % 8];
        if (Chance(15)) {
            // exactly a full push once scaled by 1/0.85 (0x00647B28)
            const double scale = 1.1764705181121826;
            value = 1.0 / scale;
            while (value * scale < 1.0)
                value = std::nextafter(value, 2.0);
            while (value * scale > 1.0)
                value = std::nextafter(value, 0.0);
        }
        uint32_t bits[2];
        std::memcpy(bits, &value, 8);
        Record(bits[0]);
        return Float(value);
    }, {Code(&Obj::StickLength)});
    kit.Add({0x0043DAA0}, 0, [](Frame& f) {
        Record(0x43DAA0);
        Record(Relative(f.ecx));
        static const double values[] = {0.0, 1.0, 2.0 / 3.0, 300.5, 12.000000000000002};
        return Float(values[Next() % 5]);
    });
}

void FillMovement()
{
    FillVariant(0);
    Pointer(kComponent + 0x175C, kCamera);
    // the pad (the input's +0x20)
    uint32_t pad = kInput + 0x20;
    if (Chance(40))
        At<float>(pad + 0x208) = 0.0f;
    if (Chance(40))
        At<float>(pad + 0x20C) = 0.0f;
    if (Chance(30))
        At<float>(pad + 0x22C) = 0.0f;
    if (Chance(20))
        At<float>(pad + 0x228) = Chance(50) ? 0.0f : -0.0f;
    At<uint8_t>(pad + 0x1A0) = Chance(50) ? 1 : 0;
    At<uint8_t>(kComponent + 0x70C) = Chance(50) ? 1 : 0;
    At<uint8_t>(kComponent + 0x18B0) = Chance(70) ? 1 : 0;
    // the camera's at-row flat or not
    if (Chance(20)) {
        At<float>(kCamera + 0x8C) = 0.0f;
        At<float>(kCamera + 0x94) = 0.0f;
    }
    // the velocity: zero, along x by a shared value, or random
    if (Chance(20))
        for (int i = 0; i < 4; ++i)
            At<float>(kComponent + 0x34 + 4 * i) = 0.0f;
    else if (Chance(30)) {
        At<float>(kComponent + 0x34) = SharedFloat();
        At<float>(kComponent + 0x38) = 0.0f;
        At<float>(kComponent + 0x3C) = 0.0f;
    } else if (Chance(20)) {
        // (negative zeros: taking away nothing would turn them positive)
        At<float>(kComponent + 0x34) = -0.0f;
        At<float>(kComponent + 0x38) = 0.0f;
        At<float>(kComponent + 0x3C) = -0.0f;
        At<float>(kComponent + 0x18B4) = -1.0f;
        At<float>(kComponent + 0x18BC) = -1.0f;
    }
    if (Chance(30))
        At<float>(kComponent + 0x80) = SharedFloat();
    // the up vector: exactly upright, nearly, or not
    if (Chance(20))
        At<float>(kComponent + 0x5C) = 1.0f;
    else if (Chance(20))
        At<float>(kComponent + 0x5C) = Chance(50) ? 0.999f : 0.9995f;
    if (Chance(50))
        At<float>(kComponent + 0x72C) = Chance(50) ? 0.0f : -0.0f;
    // the ground normal's y at the steep slope's limit
    if (Chance(30))
        At<float>(kComponent + 0x1798) = Chance(50) ? 0.95f : 0.5f;
    // the push plane: level, its distance a shared value; the height at the position's
    if (Chance(50)) {
        At<float>(kComponent + 0x18B4) = 0.0f;
        At<float>(kComponent + 0x18B8) = 1.0f;
        At<float>(kComponent + 0x18BC) = 0.0f;
        At<float>(kComponent + 0x28) = 0.0f;
        At<float>(kComponent + 0x18C4) = SharedFloat();
    }
    if (Chance(20))
        At<float>(kComponent + 0x18C8) = At<float>(kComponent + 0x28);
    // the object's at-row nearly vertical
    if (Chance(30)) {
        At<float>(kObject + 0x8C) = Chance(50) ? 0.0005f : 0.0f;
        At<float>(kObject + 0x94) = Chance(50) ? 0.0005f : 0.0f;
    }
    // the push asked for
    if (Chance(40))
        At<float>(kComponent + 0x94) = SharedFloat();
    else if (Chance(20))
        At<float>(kComponent + 0x94) = 0.3f;
}

template <typename Original, typename Ours>
bool CompareMovement(const char* what, int variants, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        Seed(0x85EBCA6Bu * (variant + 1));
        FillMovement();
        KeepStart();
        uint32_t seed = Next() | 1;
        // (the feelers' current collision cache, 0x00691974, recorded too)
        auto& cache = GameVariable<uint32_t>(0x00691974);
        uint32_t savedCache = cache;
        std::vector<uint32_t> expected = Run(seed, [&] {
            cache = 0;
            uint32_t result = original(Arena() + kComponent);
            Record(Relative(cache));
            return result;
        });
        std::vector<uint32_t> actual = Run(seed, [&] {
            cache = 0;
            uint32_t result = ours(reinterpret_cast<CWalkComponent*>(Arena() + kComponent));
            Record(Relative(cache));
            return result;
        });
        cache = savedCache;
        if (!Same(expected, actual, what, variant))
            return false;
    }
    return true;
}

void TestWalkMovement()
{
    Kit kit;
    StandIns(kit);
    MovementStandIns(kit);
    bool ok = true;
    auto check = [&](const char* what, int variants, auto original, auto ours) {
        ok = ok && CompareMovement(what, variants, original, ours);
    };
    using Self = void*;
    using Void = void(__thiscall*)(Self);
    struct Plain {
        uint32_t address;
        void (CWalkComponent::*ours)();
        const char* name;
        int variants;
    };
    static const Plain plain[] = {
        {0x00526260, &CWalkComponent::TurnFacingToVelocity, "TurnFacingToVelocity", 3000},
        {0x00527420, &CWalkComponent::UpdateUpright, "UpdateUpright", 1000},
        {0x005277B0, &CWalkComponent::UpdateCollisionCache, "UpdateCollisionCache", 300},
        {0x00527BE0, &CWalkComponent::RestoreDisplayOffset, "RestoreDisplayOffset", 500},
        {0x00527C50, &CWalkComponent::UpdatePush, "UpdatePush", 3000},
        {0x00527DB0, &CWalkComponent::CopyFromObject, "CopyFromObject", 1000},
        {0x00527F20, &CWalkComponent::UpdateControl, "UpdateControl", 3000},
    };
    for (const Plain& p : plain) {
        static const Plain* current;
        current = &p;
        char what[80];
        std::snprintf(what, sizeof(what), "walk: %s", p.name);
        check(what, p.variants, [](uint8_t* c) { Original<Void>(current->address)(c); return 0u; },
              [](CWalkComponent* c) { (c->*(current->ours))(); return 0u; });
    }
    check("walk: AdjustSpeedForSlope", 3000,
          [](uint8_t* c) {
              float speed = Chance(50) ? SharedFloat() : RandomFloat(3.0f);
              return D(Original<double(__thiscall*)(Self, float, Self)>(0x00526430)(c, speed, Arena() + 0x5900));
          },
          [](CWalkComponent* c) {
              float speed = Chance(50) ? SharedFloat() : RandomFloat(3.0f);
              return D(c->AdjustSpeedForSlope(speed, reinterpret_cast<Mth::Vector*>(Arena() + 0x5900)));
          });
    check("walk: SetTransitionFacing", 2000,
          [](uint8_t* c) {
              float rate = RandomFloat(2.0f);
              Original<void(__thiscall*)(Self, Self, float)>(0x00527720)(c, Arena() + 0x5900, rate);
              return 0u;
          },
          [](CWalkComponent* c) {
              float rate = RandomFloat(2.0f);
              c->SetTransitionFacing(reinterpret_cast<Mth::Vector*>(Arena() + 0x5900), rate);
              return 0u;
          });
    check("walk: GetControlSpeed", 3000, [](uint8_t* c) { return D(Original<double(__thiscall*)(Self)>(0x00528120)(c)); },
          [](CWalkComponent* c) { return D(c->GetControlSpeed()); });
    Check(ok, "walk: the Walk component's movement (original vs ours)");
}

} // namespace

SELFTEST(Early, TestWalkPieces, Sk5::Cleanup);
SELFTEST(Early, TestWalkMovement, Sk5::Cleanup);
