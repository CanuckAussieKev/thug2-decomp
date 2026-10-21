// Self-tests for the VehicleCamera component (game/skater/vehicle_camera.cpp): the original against ours on the
// same stand-in component, objects and callees (sk5_testkit.h).

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/camera_util.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/vehicle_camera.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/sk5_testkit.h"

namespace {

using namespace Sk5;
using Obj::CVehicleCameraComponent;

// The arena: the component, its camera object, the vehicle and its Vehicle component, the look-around and Camera
// components, the camera, stand-in vtables, the memory the stand-in Calloc gives.
constexpr uint32_t kComponent = 0x0000;
constexpr uint32_t kObject = 0x0200;   // (0x110)
constexpr uint32_t kSubject = 0x0400;
constexpr uint32_t kVehicle = 0x0600;  // (+0x114)
constexpr uint32_t kLookAround = 0x0800;
constexpr uint32_t kCameraComponent = 0x0900;
constexpr uint32_t kCamera = 0x0A00;
constexpr uint32_t kVtables = 0x0B00;  // the camera component's, then the component's
constexpr uint32_t kFresh = 0x1000;

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The stand-ins every VehicleCamera function's callees get.
void StandIns(Kit& kit)
{
    using Script::CStruct;
    // GetComponent: the arena's components by type.
    kit.Add({0x0045DB90}, 4, [](Frame& f) {
        Record(0x0045DB90);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        switch (f.args[0]) {
        case 0xC4E311FA:
            return Int(reinterpret_cast<uint32_t>(Arena() + kCameraComponent));
        case 0x81C7122D:
            return Int(reinterpret_cast<uint32_t>(Arena() + kLookAround));
        case 0xE47F1B79:
            return Int(reinterpret_cast<uint32_t>(Arena() + kVehicle));
        default:
            return Int(0);
        }
    }, {Code(&Obj::CCompositeObject::GetComponent)});
    kit.Add({0x004619F0}, 4, [](Frame& f) {
        Record(0x004619F0);
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Arena() + kSubject));
    }, {Code(&Obj::CBaseManager::GetObjectByID)});
    kit.Recorder(0x00485A50, {0, 0, Ret::None, true}, {Code(&Gfx::Camera::StoreOldPos)});
    kit.Recorder(0x005BEAA0, {0, 0, Ret::Float}, {Code(&Tmr::GetSlomoFrameSeconds)});
    // acos (ST0 in and out): the argument recorded, its true acos given back.
    kit.Add({0x00623630}, 0, [](Frame&) {
        double argument = St0Argument();
        Record(0x00623630);
        RecordFloat(static_cast<float>(argument));
        uint32_t halves[2];
        std::memcpy(halves, &argument, 8);
        Record(halves[0]);
        Record(halves[1]);
        return Float(std::acos(argument));
    }, {}, true);
    // CreateRotateZMatrix (cdecl; returns the matrix): the angle recorded, a random matrix.
    kit.Add({0x00404E70}, 0, [](Frame& f) {
        Record(0x00404E70);
        Record(Relative(f.args[0]));
        Record(f.args[1]);
        float* m = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = RandomFloat(2.0f);
        return Int(f.args[0]);
    }, {Code(static_cast<Mth::Matrix& (*)(Mth::Matrix&, float)>(&Mth::CreateRotateZMatrix))});
    // D3DXMatrixMultiply: the inputs recorded, a random product.
    kit.Add({0x005BFA8C}, 12, [](Frame& f) {
        Record(0x005BFA8C);
        for (int i = 0; i < 3; ++i)
            Record(Relative(f.args[i]));
        for (int a = 1; a <= 2; ++a)
            for (int i = 0; i < 16; ++i)
                Record(reinterpret_cast<const uint32_t*>(f.args[a])[i]);
        float* m = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = RandomFloat(2.0f);
        return Int(f.args[0]);
    });
    // AvoidCameraCollisions: its arguments and what they point at recorded, the position moved.
    kit.Add({0x004E4360}, 0, [](Frame& f) {
        Record(0x004E4360);
        for (int i = 0; i < 6; ++i)
            Record(Relative(f.args[i]));
        for (int a : {0, 2, 3})
            for (int i = 0; i < 4; ++i)
                Record(reinterpret_cast<const uint32_t*>(f.args[a])[i]);
        for (int i = 0; i < 16; ++i)
            Record(reinterpret_cast<const uint32_t*>(f.args[1])[i]);
        float* position = reinterpret_cast<float*>(f.args[0]);
        for (int i = 0; i < 4; ++i)
            position[i] = RandomFloat(100.0f);
        return Result();
    }, {Code(&Obj::AvoidCameraCollisions)});
    kit.Recorder(0x00476AF0, {4, 1, Ret::Bool, true},
                 {Code(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))});
    kit.Add({0x00476950}, 12, [](Frame& f) {
        Record(0x00476950);
        Record(f.args[0]);
        Record(Relative(f.args[1]));
        Record(f.args[2]);
        *reinterpret_cast<uint32_t*>(f.args[1]) = Next();
        uint32_t found = Next() & 1;
        Record(found);
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))});
    kit.Recorder(0x004766D0, {12, 3, Ret::Bool, true, -1, 1, 1},
                 {Code(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))});
    // Normalize: the vector recorded, then normalised as ours does.
    kit.Add({0x004023F0}, 0, [](Frame& f) {
        float* v = reinterpret_cast<float*>(f.ecx);
        Record(0x004023F0);
        Record(Relative(f.ecx));
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
}

// Fill the arena for a variant: random floats, then the pointers, the counts and the flags.
void FillVariant(int variant)
{
    FillArena();
    Pointer(kComponent + 0x0C, kObject);
    Pointer(kComponent + 0x18, kSubject);
    Pointer(kComponent + 0x1C, kVehicle);
    Pointer(kComponent + 0xA0, kLookAround);
    Pointer(kCameraComponent + 0x18, kCamera);
    Pointer(kCameraComponent, kVtables);
    Pointer(kComponent, kVtables + 0x40);
    At<int>(kVehicle + 0x114) = static_cast<int>(Next() % 4);
    At<uint8_t>(kLookAround + 0x18) = static_cast<uint8_t>(Next());
    // Unit-length directions now and then (the angle between them inside acos's range), the velocity's speed
    // sometimes past the 1 / 0.0037879 at which it counts fully, and the huge one the fewer-wheels case needs.
    if (variant % 3 == 0) {
        for (uint32_t at : {kComponent + 0x20, kComponent + 0x30}) {
            double angle = RandomFloat(3.14f);
            At<float>(at) = static_cast<float>(std::sin(angle));
            At<float>(at + 4) = 0.0f;
            At<float>(at + 8) = static_cast<float>(std::cos(angle));
        }
    }
    if (variant % 5 == 1)
        At<float>(kVehicle + 0xA4) = RandomFloat(1000.0f);
    if (variant % 7 == 2)
        At<float>(kVehicle + 0xAC) = 9.0e7f;
    if (variant % 11 == 3) {
        At<float>(kVehicle + 0xA4) = 0.0f;
        At<float>(kVehicle + 0xAC) = 0.0f;
    }
    if (variant % 13 == 4)
        At<uint32_t>(kVehicle + 0xA4) = 0x7FC00000u; // a NaN speed
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
        std::vector<uint32_t> actual = Run(seed, [&] { return ours(reinterpret_cast<CVehicleCameraComponent*>(Arena() + kComponent)); });
        if (!Same(expected, actual, what, variant)) {
            Run(seed, [] { return 0u; }); // (the start again, for the log)
            for (uint32_t at : {kVehicle + 0xA4, kVehicle + 0xA8, kVehicle + 0xAC, kVehicle + 0xB0, kVehicle + 0x114,
                                kSubject + 0x8C, kSubject + 0x90, kSubject + 0x94, kSubject + 0x98})
                Log("    start +%04X: %08X", at, At<uint32_t>(at));
            return false;
        }
    }
    return true;
}

void TestVehicleCamera()
{
    Kit kit;
    StandIns(kit);
    void* cameraUpdate = kit.Add({}, 0, [](Frame& f) {
        Record(0xCA000001);
        Record(Relative(f.ecx));
        return Result();
    });
    void* refresh = kit.Add({}, 4, [](Frame& f) {
        Record(0xCA000005);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        return Result();
    });
    void* finalize = kit.Add({}, 0, [](Frame& f) {
        Record(0xCA000003);
        Record(Relative(f.ecx));
        return Result();
    });
    auto vtables = [&] {
        At<void*>(kVtables + 4) = cameraUpdate;
        At<void*>(kVtables + 0x40 + 0x0C) = finalize;
        At<void*>(kVtables + 0x40 + 0x14) = refresh;
    };
    bool ok = true;
    auto check = [&](const char* what, int variants, auto original, auto ours) {
        ok = ok && Compare(what, variants, [&](uint8_t* c) { vtables(); return original(c); },
                           [&](CVehicleCameraComponent* c) { vtables(); return ours(c); });
    };
    check("vehicle camera: UpdateTargetDirection", 3000,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524550)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->UpdateTargetDirection(); return 0u; });
    check("vehicle camera: UpdateOrientation", 1000,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524780)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->UpdateOrientation(); return 0u; });
    check("vehicle camera: ApplyToCamera", 200,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524720)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->ApplyToCamera(); return 0u; });
    check("vehicle camera: Update", 4000,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524940)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->Update(); return 0u; });
    check("vehicle camera: Reset", 1000,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524B40)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->Reset(); return 0u; });
    check("vehicle camera: Finalize", 300,
          [](uint8_t* c) { Original<void(__thiscall*)(void*)>(0x00524C00)(c); return 0u; },
          [](CVehicleCameraComponent* c) { c->Finalize(); return 0u; });
    check("vehicle camera: InitFromStructure", 500,
          [](uint8_t* c) { Original<void(__thiscall*)(void*, void*)>(0x005244A0)(c, Arena() + 0x1F00); return 0u; },
          [](CVehicleCameraComponent* c) {
              c->InitFromStructure(reinterpret_cast<Script::CStruct*>(Arena() + 0x1F00));
              return 0u;
          });
    check("vehicle camera: VehicleCamera_Reset", 50,
          [](uint8_t* c) {
              return static_cast<uint32_t>(Original<int(__thiscall*)(void*, void*, void*)>(0x00524450)(
                  c, Arena() + 0x1F00, Arena() + 0x1F80));
          },
          [](CVehicleCameraComponent* c) {
              return static_cast<uint32_t>(c->VehicleCamera_Reset(reinterpret_cast<Script::CStruct*>(Arena() + 0x1F00),
                                                                  reinterpret_cast<Script::CScript*>(Arena() + 0x1F80)));
          });
    Check(ok, "vehicle camera: the component's functions (original vs ours)");
}

// Create (out of memory or not), the destructor, the member function table.
void TestVehicleCameraLife()
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
    kit.Recorder(0x0046F060, {0, 3}, {Code(&Script::RegisterMemberFunctionTable)});
    bool ok = true;
    for (int variant = 0; variant < 60 && ok; ++variant) {
        Seed(0x7F4A7C15u * (variant + 1));
        FillArena();
        KeepStart();
        uint32_t seed = Next() | 1;
        auto expected = Run(seed, [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x005248C0)()); });
        auto actual = Run(seed, [] { return reinterpret_cast<uint32_t>(CVehicleCameraComponent::Create()); });
        ok = Same(expected, actual, "vehicle camera: Create", variant);
        if (!ok)
            break;
        uint32_t flags = Next() & 3;
        expected = Run(seed, [&] {
            return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, unsigned int)>(0x00524470)(Arena(), flags));
        });
        actual = Run(seed, [&] {
            return reinterpret_cast<uint32_t>(reinterpret_cast<CVehicleCameraComponent*>(Arena())->Destroy(flags));
        });
        ok = Same(expected, actual, "vehicle camera: Destroy", variant);
        if (!ok)
            break;
        expected = Run(seed, [] { Original<void(__cdecl*)()>(0x00524420)(); return 0u; });
        actual = Run(seed, [] { CVehicleCameraComponent::RegisterMemberFunctions(); return 0u; });
        ok = Same(expected, actual, "vehicle camera: RegisterMemberFunctions", variant);
    }
    Check(ok, "vehicle camera: Create, Destroy, RegisterMemberFunctions");
}

} // namespace

SELFTEST(Early, TestVehicleCamera, Sk5::Cleanup);
SELFTEST(Early, TestVehicleCameraLife, Sk5::Cleanup);
