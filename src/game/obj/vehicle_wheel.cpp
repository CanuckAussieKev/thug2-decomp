// The vehicle component's member function registration and wheel settings (game/obj/vehicle_wheel.h).

#include "game/obj/vehicle_wheel.h"

#include <cstdint>

#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(uint32_t[60], g_vehicleWheelMemberFunctions, 0x00684060,
            0x93B713A6, 0xE47F1B79, 0, 0, 0, 0,
            0xA80A0D36, 0xE47F1B79, 0, 0, 0, 0,
            0x4B0B27DD, 0xE47F1B79, 0, 0, 0, 0,
            0xC33608E4, 0xE47F1B79, 0, 0, 0, 0,
            0xDB35AAD8, 0xE47F1B79, 0, 0, 0, 0,
            0x5008B253, 0xE47F1B79, 0, 0, 0, 0,
            0x279602ED, 0xE47F1B79, 0, 0, 0, 0,
            0x4C73526B, 0xE47F1B79, 0, 0, 0, 0,
            0x81FAAC21, 0xE47F1B79, 0, 0, 0, 0,
            0, 0xE47F1B79, 0, 0, 0, 0);

namespace {

constexpr uint32_t kVehicle = 0xE47F1B79;

constexpr uint32_t kSteering = 0x07560E63, kRight = 0x4B358AEB, kFixed = 0x613631CD, kLeft = 0x85981897;
constexpr uint32_t kDrive = 0x97E20A70, kYes = 0x8A18CA56, kNo = 0x9855D7E0;

constexpr float kGravity = 386.3999938964844f;           // 0x006466A4 (inches a second squared)
constexpr float kInchesPerMile = 63360.0f;               // 0x006461E4
constexpr float kHoursPerSecond = 0.00027777778450399637f; // 0x006461E0 (1 / 3600)
constexpr float kInchesPerFoot = 12.0f;                  // 0x0064546C

template <typename T>
T& At(uint8_t* wheel, uint32_t offset)
{
    return *reinterpret_cast<T*>(wheel + offset);
}

// A grip velocity in miles an hour, kept in inches a second (rounded once, as the x87 does).
void ReadMilesPerHour(Script::CStruct* params, uint32_t name, float& value)
{
    if (!params->ContainsComponentNamed(name))
        return;
    params->GetFloat(name, &value, 0);
    value = static_cast<float>(static_cast<double>(value) * kInchesPerMile * kHoursPerSecond);
}

void ReadFeet(Script::CStruct* params, uint32_t name, float& value)
{
    if (!params->ContainsComponentNamed(name))
        return;
    params->GetFloat(name, &value, 0);
    value = value * kInchesPerFoot;
}

} // namespace

// 0x00434F00
void __cdecl RegisterVehicleMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kVehicle), g_vehicleWheelMemberFunctions, 0x18);
}

// 0x00434F20
void __stdcall ReadWheelSettings(uint8_t* wheel, Script::CStruct* params)
{
    // (each checksum is read into the parameters' own argument slot: a steering= that isn't a name leaves the
    // pointer's value there)
    uint32_t choice = reinterpret_cast<uint32_t>(params);
    if (params->ContainsComponentNamed(kSteering)) {
        params->GetChecksum(kSteering, &choice, 0);
        if (choice == kRight)
            At<int>(wheel, 0xB0) = 2;
        else if (choice == kFixed)
            At<int>(wheel, 0xB0) = 0;
        else if (choice == kLeft)
            At<int>(wheel, 0xB0) = 1;
    }
    if (params->ContainsComponentNamed(kDrive)) {
        params->GetChecksum(kDrive, &choice, 0);
        if (choice == kYes)
            At<bool>(wheel, 0xB4) = true;
        else if (choice == kNo)
            At<bool>(wheel, 0xB4) = false;
    }
    params->GetFloat(0x9E931FDF, &At<float>(wheel, 0xC8), 0); // spring_rate
    params->GetFloat(0xAFA960FF, &At<float>(wheel, 0xCC), 0); // damping_rate
    params->GetFloat(0xC48391A5, &At<float>(wheel, 0xC0), 0); // radius
    if (params->ContainsComponentNamed(0xCA73775D)) {         // moment
        float& moment = At<float>(wheel, 0xC4);
        params->GetFloat(0xCA73775D, &moment, 0);
        moment = kGravity / moment;
    }
    params->GetFloat(0xD87D0183, &At<float>(wheel, 0xBC), 0);
    params->GetFloat(0xF36B97C8, &At<float>(wheel, 0xDC), 0); // static_friction
    params->GetFloat(0x674852F6, &At<float>(wheel, 0xE0), 0); // dynamic_friction
    params->GetFloat(0x52242919, &At<float>(wheel, 0xE4), 0); // handbrake_throttle_friction
    params->GetFloat(0x6D615F67, &At<float>(wheel, 0xE8), 0); // handbrake_locked_friction
    ReadMilesPerHour(params, 0x60B2C097, At<float>(wheel, 0xD0)); // min_static_grip_velocity
    ReadMilesPerHour(params, 0xBB0D9370, At<float>(wheel, 0xD4)); // max_static_grip_velocity
    ReadMilesPerHour(params, 0xB71803AD, At<float>(wheel, 0xD8)); // min_dynamic_grip_velocity
    ReadFeet(params, 0x3BCCBADC, At<float>(wheel, 0xF0));
    ReadFeet(params, 0x9439D144, At<float>(wheel, 0xF4));
}

} // namespace Obj

REPLACE(0x00434F00, Obj::RegisterVehicleMemberFunctions);
REPLACE(0x00434F20, Obj::ReadWheelSettings);
