// Self-tests for the WalkCamera component (game/skater/walk_camera.cpp) and the Walk component's pieces it uses
// (game/skater/walk_physics.cpp): the original against ours on the same stand-in objects and callees
// (sk5_testkit.h).

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/obj/camera_util.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/skater/walk_camera.h"
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
using Obj::CWalkCameraComponent;
using Obj::CWalkComponent;

constexpr uint32_t kComponent = 0x0000;     // (0xA8)
constexpr uint32_t kObject = 0x0100;        // the camera object (0x110)
constexpr uint32_t kSkater = 0x0300;        // (0x110)
constexpr uint32_t kLookAround = 0x0480;    // (0x44)
constexpr uint32_t kCameraComponent = 0x04D0;
constexpr uint32_t kCamera = 0x0520;
constexpr uint32_t kSkaterCamera = 0x0600;  // (0x140)
constexpr uint32_t kPhysicsControl = 0x0780;
constexpr uint32_t kPhysicsState = 0x0800;  // (+0x118)
constexpr uint32_t kVtables = 0x0A00;
constexpr uint32_t kState = 0x0B00;         // a WalkCameraState (0x58)
constexpr uint32_t kFresh = 0x1000;
constexpr uint32_t kParams = 0x1F00;
constexpr uint32_t kWalk = 0x2000;          // (0x1800)
constexpr uint32_t kInput = 0x4000;         // (0x240)

bool g_identityMultiply = false;
bool g_edgeParams = false;

// A flat velocity (x, z) whose length is just below 1 in double precision but 1 as a float.
void SpeedJustBelowOne(float* x, float* z)
{
    float vx = 0.6f;
    float vz = static_cast<float>(std::sqrt(1.0 - static_cast<double>(vx) * vx));
    for (int i = 0; i < 200; ++i) {
        double speed = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vz) * vz);
        if (speed < 1.0 && static_cast<float>(speed) == 1.0f)
            break;
        uint32_t bits;
        std::memcpy(&bits, &vz, 4);
        bits += speed >= 1.0 ? -1 : 1;
        std::memcpy(&vz, &bits, 4);
    }
    *x = vx;
    *z = vz;
}

void RecordMatrix(uint32_t address)
{
    for (int i = 0; i < 16; ++i)
        Record(reinterpret_cast<const uint32_t*>(address)[i]);
}
void RandomMatrix(uint32_t address)
{
    float* m = reinterpret_cast<float*>(address);
    for (int i = 0; i < 16; ++i)
        m[i] = RandomFloat(2.0f);
}

// The stand-ins of the WalkCamera's callees. `walkMethods`: the Walk component's two functions stood in too
// (they're compared on their own).
void StandIns(Kit& kit, bool walkMethods, bool param)
{
    using Script::CStruct;
    kit.Add({0x0045DB90}, 4, [](Frame& f) {
        Record(0x0045DB90);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        switch (f.args[0]) {
        case 0x726E85AA:
            return Int(reinterpret_cast<uint32_t>(Arena() + kWalk));
        case 0xD9151D4D:
            return Int(Chance(80) ? reinterpret_cast<uint32_t>(Arena() + kPhysicsControl) : 0u);
        case 0x81C7122D:
            return Int(reinterpret_cast<uint32_t>(Arena() + kLookAround));
        case 0xC4E311FA:
            return Int(reinterpret_cast<uint32_t>(Arena() + kCameraComponent));
        case 0x5E43A604:
            return Int(reinterpret_cast<uint32_t>(Arena() + kSkaterCamera));
        case 0x27D7CD28:
            return Int(reinterpret_cast<uint32_t>(Arena() + kInput));
        default:
            return Int(0);
        }
    }, {Code(&Obj::CCompositeObject::GetComponent)});
    kit.Add({0x004619F0}, 4, [](Frame& f) {
        Record(0x004619F0);
        Record(f.args[0]);
        return Int(Chance(85) ? reinterpret_cast<uint32_t>(Arena() + kSkater) : 0u);
    }, {Code(&Obj::CBaseManager::GetObjectByID)});
    kit.Recorder(0x00485A50, {0, 0, Ret::None, true}, {Code(&Gfx::Camera::StoreOldPos)});
    kit.Recorder(0x005BEAA0, {0, 0, Ret::Float}, {Code(&Tmr::GetSlomoFrameSeconds)});
    kit.Recorder(0x0044ECE0, {0, 0, Ret::Int}, {Code(&Tmr::GameClockElapsed)});
    kit.Recorder(0x004E4340, {0, 2, Ret::Float}, {Code(&Obj::FrameBlend)});
    kit.Add({0x00479020}, 0, [](Frame& f) {
        Record(0x00479020);
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Arena() + kParams));
    }, {Code(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))});
    kit.Recorder(0x004766D0, {12, 3, Ret::Bool, true, -1, 1, 1},
                 {Code(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))});
    kit.Add({0x00476950}, 12, [](Frame& f) {
        Record(0x00476950);
        Record(f.args[0]);
        Record(f.args[2]);
        *reinterpret_cast<uint32_t*>(f.args[1]) = Next();
        return Int(1);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))});
    kit.Add({0x00404DD0}, 0, [](Frame& f) {
        Record(0x00404DD0);
        Record(Relative(f.args[0]));
        Record(f.args[1]);
        RandomMatrix(f.args[0]);
        return Int(f.args[0]);
    }, {Code(static_cast<Mth::Matrix& (*)(Mth::Matrix&, float)>(&Mth::CreateRotateXMatrix))});
    kit.Add({0x005BFA8C}, 12, [](Frame& f) {
        Record(0x005BFA8C);
        for (int i = 0; i < 3; ++i)
            Record(Relative(f.args[i]));
        RecordMatrix(f.args[1]);
        RecordMatrix(f.args[2]);
        if (g_identityMultiply)
            std::memmove(reinterpret_cast<void*>(f.args[0]), reinterpret_cast<void*>(f.args[2]), 64);
        else
            RandomMatrix(f.args[0]);
        return Int(f.args[0]);
    });
    kit.Add({0x00405990}, 8, [](Frame& f) {
        Record(0x00405990);
        RecordMatrix(f.args[0]);
        RecordMatrix(f.args[1]);
        return Int(1);
    }, {Code(&Mth::SlerpInterpolator::SetMatrices)});
    kit.Add({0x00405A20}, 8, [](Frame& f) {
        Record(0x00405A20);
        Record(Relative(f.args[0]));
        Record(f.args[1]);
        RandomMatrix(f.args[0]);
        return Result();
    }, {Code(&Mth::SlerpInterpolator::GetMatrix)});
    kit.Add({0x004023F0}, 0, [](Frame& f) {
        float* v = reinterpret_cast<float*>(f.ecx);
        Record(0x004023F0);
        for (int i = 0; i < 4; ++i)
            RecordFloat(v[i]);
        double length = std::sqrt((static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1]) +
                                  static_cast<double>(v[2]) * v[2]);
        if (length > 0.0) {
            double scale = 1.0 / length;
            for (int i = 0; i < 3; ++i)
                v[i] = static_cast<float>(v[i] * scale);
        }
        return Int(f.ecx);
    }, {Code(&Mth::Vector::Normalize)});
    kit.Add({0x004E4360}, 0, [](Frame& f) {
        Record(0x004E4360);
        for (int i = 0; i < 6; ++i)
            Record(Relative(f.args[i]));
        for (int a : {0, 2, 3})
            for (int i = 0; i < 4; ++i)
                Record(reinterpret_cast<const uint32_t*>(f.args[a])[i]);
        RecordMatrix(f.args[1]);
        float* position = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 4; ++i)
            position[i] = RandomFloat(100.0f);
        RandomMatrix(f.args[1]);
        return Result();
    }, {Code(&Obj::AvoidCameraCollisions)});
    if (walkMethods) {
        kit.Recorder(0x00524E10, {0, 0, Ret::Bool, true}, {Code(&CWalkComponent::UsingDpad)});
        kit.Add({0x00524E60}, 4, [](Frame& f) {
            Record(0x00524E60);
            Record(Relative(f.ecx));
            float* out = reinterpret_cast<float*>(f.args[0]);
            for (int i = 0; i < 4; ++i)
                out[i] = RandomFloat(20.0f);
            return Int(f.args[0]);
        }, {Code(&CWalkComponent::GetCameraOffset)});
    }
    if (param) {
        kit.Add({0x00524EF0}, 4, [](Frame& f) {
            Record(0x00524EF0);
            Record(f.args[0]);
            float value = PickFloat();
            if (g_edgeParams) {
                // (the speed edge cases: full speed 1, least 0.5, any angle within the lock)
                switch (f.args[0]) {
                case 0xBCEF6DDA:
                case 0x73DA1EC0:
                    value = 1.0f;
                    break;
                case 0x824349E5:
                case 0x0DA5CAD3:
                    value = 0.5f;
                    break;
                case 0xB8DA8A73:
                    value = 180.0f;
                    break;
                default:
                    break;
                }
            }
            RecordFloat(value);
            return Float(value);
        }, {Code(&CWalkCameraComponent::Param)});
    }
}

void FillVariant(int variant)
{
    FillArena();
    Pointer(kComponent, kVtables);
    Pointer(kComponent + 0x0C, kObject);
    Pointer(kComponent + 0x90, kLookAround);
    Pointer(kComponent + 0x94, kCameraComponent);
    Pointer(kComponent + 0x98, kSkaterCamera);
    Pointer(kComponent + 0x9C, kSkater);
    Pointer(kComponent + 0xA0, kWalk);
    Pointer(kComponent + 0xA4, kPhysicsControl);
    Pointer(kCameraComponent + 0x18, kCamera);
    Pointer(kPhysicsControl + 0x2C, kPhysicsState);
    Pointer(kWalk + 0x1770, kInput);
    At<uint8_t>(kPhysicsState + 0x118) = Chance(5) ? 1 : 0;
    if (Chance(5))
        At<uint32_t>(kComponent + 0x9C) = 0;
    if (Chance(10))
        At<uint32_t>(kComponent + 0xA4) = 0;
    At<int>(kComponent + 0x18) = static_cast<int>(Next() % 6) - 1;
    At<uint8_t>(kComponent + 0x1C) = Chance(25) ? 1 : 0;
    At<uint8_t>(kComponent + 0x1D) = Chance(20) ? 1 : 0;
    At<uint8_t>(kComponent + 0x78) = Chance(25) ? 1 : 0;
    At<uint32_t>(kSkater + 0x104) = Chance(15) ? 8u : 0u;
    At<int>(kWalk + 0x18) = static_cast<int>(Next() % 7) - 1;
    At<int>(kWalk + 0x20) = static_cast<int>(Next() % 6);
    At<int>(kWalk + 0x1C) = static_cast<int>(Next() % 400) - 200;
    At<uint8_t>(kWalk + 0x98) = Chance(50) ? 1 : 0;
    At<uint8_t>(kWalk + 0x17A4) = Chance(40) ? 1 : 0;
    At<uint8_t>(kWalk + 0x70C) = Chance(50) ? 1 : 0;
    At<uint8_t>(kLookAround + 0x18) = Chance(50) ? 1 : 0;
    At<uint8_t>(kLookAround + 0x19) = Chance(50) ? 1 : 0;
    if (Chance(30))
        At<float>(kLookAround + 0x1C) = 1.0f;
    if (Chance(50))
        At<float>(kInput + 0x230) = 0.0f;
    if (Chance(50))
        At<float>(kInput + 0x228) = 0.0f;
    if (Chance(50))
        At<float>(kInput + 0x22C) = 0.0f;
    if (Chance(25))
        At<float>(kComponent + 0x20) = 1.0f + RandomFloat(0.1f);
    // The skater's velocity sometimes fast (past the full slerp speeds the stand-in gives: up to 200).
    if (Chance(30)) {
        At<float>(kSkater + 0xAC) = RandomFloat(300.0f);
        At<float>(kSkater + 0xB4) = RandomFloat(300.0f);
    }
    // Aligned: the orientation already the look's (with the multiply standing for no tilt), so the slerp is skipped.
    g_identityMultiply = variant % 4 == 0;
    if (g_identityMultiply) {
        double angle = RandomFloat(3.14f);
        float fx = static_cast<float>(std::sin(angle)), fz = static_cast<float>(std::cos(angle));
        float* m = &At<float>(kComponent + 0x34);
        const float rows[12] = {-fz, 0.0f, fx, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, -fx, 0.0f, -fz, RandomFloat(1.0f)};
        std::memcpy(m, rows, sizeof(rows));
        At<float>(kInput + 0x230) = 0.0f;
    }
    // The edges: a speed just under the full speed (1 as a float), exactly the least speed; the zoom just under 1
    // (1 as a float); a walk state changed exactly 150 ms ago (the clock stand-in gives 150 now and then).
    g_edgeParams = variant % 5 == 1;
    if (g_edgeParams) {
        At<float>(kInput + 0x230) = 0.0f;
        At<uint8_t>(kComponent + 0x1C) = 0;
        At<uint8_t>(kComponent + 0x1D) = 0;
        if (Chance(50)) {
            SpeedJustBelowOne(&At<float>(kSkater + 0xAC), &At<float>(kSkater + 0xB4));
        } else {
            At<float>(kSkater + 0xAC) = 0.5f;
            At<float>(kSkater + 0xB4) = 0.0f;
        }
    }
    if (variant % 7 == 3) {
        At<float>(kComponent + 0x20) = 1.0f;
        At<uint32_t>(kLookAround + 0x1C) = 0x3F7FFFFF;
        At<uint8_t>(kLookAround + 0x19) = 1;
        At<float>(kSkaterCamera + 0x110) = 0.25f;
        At<float>(kSkaterCamera + 0xD0) = 1e-10f; // ((h - 3) * 1 + 3 isn't h)
    }
    if (Chance(20))
        At<int>(kWalk + 0x1C) = GameVariable<int>(0x00696D60);
}

template <typename Original, typename Ours>
bool Compare(const char* what, int variants, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        Seed(0x9E3779B9u * (variant + 1));
        FillVariant(variant);
        KeepStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = Run(seed, [&] { return original(Arena() + kComponent); });
        std::vector<uint32_t> actual =
            Run(seed, [&] { return ours(reinterpret_cast<CWalkCameraComponent*>(Arena() + kComponent)); });
        if (!Same(expected, actual, what, variant))
            return false;
    }
    return true;
}

void TestWalkCamera()
{
    bool ok = true;
    {
        Kit kit;
        StandIns(kit, true, true);
        void* update = kit.Add({}, 0, [](Frame& f) {
            Record(0xCA000001);
            Record(Relative(f.ecx));
            return Result();
        });
        auto vtable = [update] { At<void*>(kVtables + 4) = update; };
        auto check = [&](const char* what, int variants, auto original, auto ours) {
            ok = ok && Compare(what, variants, [&](uint8_t* c) { vtable(); return original(c); },
                               [&](CWalkCameraComponent* c) { vtable(); return ours(c); });
        };
        check("walk camera: Update", 8000,
              [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x005251D0)(c); return 0u; },
              [](CWalkCameraComponent* c) { c->Update(); return 0u; });
        check("walk camera: GetTarget", 600,
              [](uint8_t* c) {
                  Pointer(kComponent + 0x9C, kSkater); // (a skater: GetTarget's callers check)
                  Original<void(__thiscall*)(void*, void*, bool)>(0x005250E0)(c, Arena() + 0x1E00, Arena()[0x1E80] & 1);
                  return 0u;
              },
              [](CWalkCameraComponent* c) {
                  Pointer(kComponent + 0x9C, kSkater);
                  c->GetTarget(reinterpret_cast<Mth::Vector*>(Arena() + 0x1E00), Arena()[0x1E80] & 1);
                  return 0u;
              });
        check("walk camera: GetZoomedOffsets", 1500,
              [](uint8_t* c) {
                  Original<void(__thiscall*)(void*, void*, void*)>(0x00524CB0)(c, Arena() + 0x1E00, Arena() + 0x1E10);
                  return 0u;
              },
              [](CWalkCameraComponent* c) {
                  c->GetZoomedOffsets(reinterpret_cast<float*>(Arena() + 0x1E00), reinterpret_cast<float*>(Arena() + 0x1E10));
                  return 0u;
              });
        check("walk camera: InitFromStructure", 200,
              [](uint8_t* c) { Original<void(__thiscall*)(void*, void*)>(0x00524F50)(c, Arena() + kParams); return 0u; },
              [](CWalkCameraComponent* c) {
                  c->InitFromStructure(reinterpret_cast<Script::CStruct*>(Arena() + kParams));
                  return 0u;
              });
        check("walk camera: Finalize", 50,
              [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524FB0)(c); return 0u; },
              [](CWalkCameraComponent* c) { c->Finalize(); return 0u; });
        check("walk camera: SetSkater", 200,
              [](uint8_t* c) {
                  Original<void(__thiscall*)(void*, void*)>(0x00524C50)(c, Chance(80) ? Arena() + kSkater : nullptr);
                  return 0u;
              },
              [](CWalkCameraComponent* c) {
                  c->SetSkater(Chance(80) ? reinterpret_cast<Obj::CCompositeObject*>(Arena() + kSkater) : nullptr);
                  return 0u;
              });
        check("walk camera: ResetNow", 50,
              [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524C30)(c); return 0u; },
              [](CWalkCameraComponent* c) { c->ResetNow(); return 0u; });
        check("walk camera: FlushRequest", 50,
              [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x005250A0)(c); return 0u; },
              [](CWalkCameraComponent* c) { c->FlushRequest(); return 0u; });
        static const uint32_t names[] = {0x73FEBD0F, 0xD1A485D1, 0x12345678, 0};
        check("walk camera: CallMemberFunction", 200,
              [](uint8_t* c) {
                  return static_cast<uint32_t>(Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x00525D40)(
                      c, names[Next() % 4], Arena() + kParams, Arena() + 0x1F80));
              },
              [](CWalkCameraComponent* c) {
                  return static_cast<uint32_t>(c->CallMemberFunction(names[Next() % 4],
                                                                     reinterpret_cast<Script::CStruct*>(Arena() + kParams),
                                                                     reinterpret_cast<Script::CScript*>(Arena() + 0x1F80)));
              });
        check("walk camera: SetOverrideFacing", 50,
              [](uint8_t* c) {
                  Original<void(__thiscall*)(void*, void*, float)>(0x00525F70)(c, Arena() + 0x1E00, At<float>(0x1E40));
                  return 0u;
              },
              [](CWalkCameraComponent* c) {
                  c->SetOverrideFacing(reinterpret_cast<Mth::Vector*>(Arena() + 0x1E00), At<float>(0x1E40));
                  return 0u;
              });
        check("walk camera: SaveState", 50,
              [](uint8_t* c) { Original<void(__thiscall*)(void*, void*)>(0x00525060)(c, Arena() + kState); return 0u; },
              [](CWalkCameraComponent* c) {
                  c->SaveState(reinterpret_cast<Obj::WalkCameraState*>(Arena() + kState));
                  return 0u;
              });
        check("walk camera: RestoreState", 50,
              [](uint8_t* c) { Original<void(__thiscall*)(void*, void*)>(0x00524FF0)(c, Arena() + kState); return 0u; },
              [](CWalkCameraComponent* c) {
                  c->RestoreState(reinterpret_cast<Obj::WalkCameraState*>(Arena() + kState));
                  return 0u;
              });
        check("walk camera: LerpByRange", 1500,
              [](uint8_t*) {
                  float t = Chance(20) ? At<float>(0x1E44) : RandomFloat(10.0f);
                  return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)(void*, void*, void*, float, float, float)>(0x00524D70)(
                      Arena() + 0x1E00, Arena() + 0x1E10, Arena() + 0x1E20, t, At<float>(0x1E30), At<float>(0x1E34)));
              },
              [](CWalkCameraComponent*) {
                  float t = Chance(20) ? At<float>(0x1E44) : RandomFloat(10.0f);
                  return reinterpret_cast<uint32_t>(Obj::LerpByRange(
                      reinterpret_cast<Mth::Vector*>(Arena() + 0x1E00), reinterpret_cast<Mth::Vector*>(Arena() + 0x1E10),
                      reinterpret_cast<Mth::Vector*>(Arena() + 0x1E20), t, At<float>(0x1E30), At<float>(0x1E34)));
              });
    }
    {
        // The parameter getter and the Walk component's pieces, with their own callees stood in.
        Kit kit;
        StandIns(kit, false, false);
        auto check = [&](const char* what, int variants, auto original, auto ours) {
            ok = ok && Compare(what, variants, original, ours);
        };
        check("walk camera: Param", 100,
              [](uint8_t*) {
                  float value = Original<float(__stdcall*)(uint32_t)>(0x00524EF0)(0xB8DA8A73);
                  uint32_t bits;
                  std::memcpy(&bits, &value, 4);
                  return bits;
              },
              [](CWalkCameraComponent*) {
                  float value = CWalkCameraComponent::Param(0xB8DA8A73);
                  uint32_t bits;
                  std::memcpy(&bits, &value, 4);
                  return bits;
              });
        check("walk: UsingDpad", 300,
              [](uint8_t*) { return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00524E10)(Arena() + kWalk)); },
              [](CWalkCameraComponent*) {
                  return static_cast<uint32_t>(reinterpret_cast<CWalkComponent*>(Arena() + kWalk)->UsingDpad());
              });
        check("walk: GetCameraOffset", 8000,
              [](uint8_t*) {
                  return reinterpret_cast<uint32_t>(
                      Original<void*(__thiscall*)(void*, void*)>(0x00524E60)(Arena() + kWalk, Arena() + 0x1E00));
              },
              [](CWalkCameraComponent*) {
                  return reinterpret_cast<uint32_t>(reinterpret_cast<CWalkComponent*>(Arena() + kWalk)->GetCameraOffset(
                      reinterpret_cast<Mth::Vector*>(Arena() + 0x1E00)));
              });
    }
    Check(ok, "walk camera: the component's functions, the Walk component's camera pieces (original vs ours)");
}

void TestWalkCameraLife()
{
    Kit kit;
    kit.Add({0x00401CC0}, 0, [](Frame& f) {
        Record(0x00401CC0);
        Record(f.args[0]);
        bool fail = Chance(30);
        Record(fail);
        if (fail)
            return Int(0);
        std::memset(Arena() + kFresh, 0, 0x200);
        return Int(reinterpret_cast<uint32_t>(Arena() + kFresh));
    }, {Code(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))});
    kit.Recorder(0x0045D740, {0, 0, Ret::None, true}, {Code(&Obj::CBaseComponent::DestroyBody)});
    kit.Recorder(0x005BABA0, {4, 1, Ret::None, true}, {Code(&Mem::Manager::Free)});
    bool ok = true;
    for (int variant = 0; variant < 60 && ok; ++variant) {
        Seed(0x7F4A7C15u * (variant + 1));
        FillArena();
        KeepStart();
        uint32_t seed = Next() | 1;
        auto expected = Run(seed, [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00525DC0)()); });
        auto actual = Run(seed, [] { return reinterpret_cast<uint32_t>(CWalkCameraComponent::Create()); });
        ok = Same(expected, actual, "walk camera: Create", variant);
        if (!ok)
            break;
        uint32_t flags = Next() & 3;
        expected = Run(seed, [&] {
            return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, unsigned int)>(0x00524F20)(Arena(), flags));
        });
        actual = Run(seed, [&] {
            return reinterpret_cast<uint32_t>(reinterpret_cast<CWalkCameraComponent*>(Arena())->Destroy(flags));
        });
        ok = Same(expected, actual, "walk camera: Destroy", variant);
    }
    Check(ok, "walk camera: Create, Destroy");
}

} // namespace

SELFTEST(Early, TestWalkCamera, Sk5::Cleanup);
SELFTEST(Early, TestWalkCameraLife, Sk5::Cleanup);
