// The vehicle component's member function registration and wheel settings (engine/objects/vehicle_wheel.h).

#include "engine/objects/vehicle_wheel.h"

#include <cstdint>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"

namespace objects {

uint32_t g_vehicle_wheel_member_functions[60]{0x93B713A6, 0xE47F1B79, 0, 0, 0, 0, 0xA80A0D36, 0xE47F1B79, 0, 0, 0, 0,
                                              0x4B0B27DD, 0xE47F1B79, 0, 0, 0, 0, 0xC33608E4, 0xE47F1B79, 0, 0, 0, 0,
                                              0xDB35AAD8, 0xE47F1B79, 0, 0, 0, 0, 0x5008B253, 0xE47F1B79, 0, 0, 0, 0,
                                              0x279602ED, 0xE47F1B79, 0, 0, 0, 0, 0x4C73526B, 0xE47F1B79, 0, 0, 0, 0,
                                              0x81FAAC21, 0xE47F1B79, 0, 0, 0, 0, 0,          0xE47F1B79, 0, 0, 0, 0};

namespace {

constexpr uint32_t kVehicle = 0xE47F1B79;

constexpr uint32_t kSteering = 0x07560E63, kRight = 0x4B358AEB, kFixed = 0x613631CD, kLeft = 0x85981897;
constexpr uint32_t kDrive = 0x97E20A70, kYes = 0x8A18CA56, kNo = 0x9855D7E0;

constexpr float kGravity = 386.3999938964844f;             // 0x006466A4 (inches a second squared)
constexpr float kInchesPerMile = 63360.0f;                 // 0x006461E4
constexpr float kHoursPerSecond = 0.00027777778450399637f; // 0x006461E0 (1 / 3600)
constexpr float kInchesPerFoot = 12.0f;                    // 0x0064546C

template <typename T>
T& get_at(uint8_t* wheel, uint32_t offset)
{
    return *reinterpret_cast<T*>(wheel + offset);
}

// A grip velocity in miles an hour, kept in inches a second (rounded once, as the x87 does).
void read_miles_per_hour(script::Struct* params, uint32_t name, float& value)
{
    if (!params->has_entry_named(name))
        return;
    params->fetch_float(name, &value, 0);
    value = static_cast<float>(static_cast<double>(value) * kInchesPerMile * kHoursPerSecond);
}

void read_feet(script::Struct* params, uint32_t name, float& value)
{
    if (!params->has_entry_named(name))
        return;
    params->fetch_float(name, &value, 0);
    value = value * kInchesPerFoot;
}

} // namespace

// 0x00434F00
void __cdecl register_vehicle_member_functions()
{
    script::register_member_function_table(reinterpret_cast<void*>(kVehicle), g_vehicle_wheel_member_functions, 0x18);
}

// 0x00434F20
void __stdcall read_wheel_settings(uint8_t* wheel, script::Struct* params)
{
    // (each checksum is read into the parameters' own argument slot: a steering= that isn't a name leaves the
    // pointer's value there)
    uint32_t choice = reinterpret_cast<uint32_t>(params);
    if (params->has_entry_named(kSteering)) {
        params->read_hash_value(kSteering, &choice, 0);
        if (choice == kRight)
            get_at<int>(wheel, 0xB0) = 2;
        else if (choice == kFixed)
            get_at<int>(wheel, 0xB0) = 0;
        else if (choice == kLeft)
            get_at<int>(wheel, 0xB0) = 1;
    }
    if (params->has_entry_named(kDrive)) {
        params->read_hash_value(kDrive, &choice, 0);
        if (choice == kYes)
            get_at<bool>(wheel, 0xB4) = true;
        else if (choice == kNo)
            get_at<bool>(wheel, 0xB4) = false;
    }
    params->fetch_float(0x9E931FDF, &get_at<float>(wheel, 0xC8), 0); // spring_rate
    params->fetch_float(0xAFA960FF, &get_at<float>(wheel, 0xCC), 0); // damping_rate
    params->fetch_float(0xC48391A5, &get_at<float>(wheel, 0xC0), 0); // radius
    if (params->has_entry_named(0xCA73775D)) {                       // moment
        float& moment = get_at<float>(wheel, 0xC4);
        params->fetch_float(0xCA73775D, &moment, 0);
        moment = kGravity / moment;
    }
    params->fetch_float(0xD87D0183, &get_at<float>(wheel, 0xBC), 0);
    params->fetch_float(0xF36B97C8, &get_at<float>(wheel, 0xDC), 0);     // static_friction
    params->fetch_float(0x674852F6, &get_at<float>(wheel, 0xE0), 0);     // dynamic_friction
    params->fetch_float(0x52242919, &get_at<float>(wheel, 0xE4), 0);     // handbrake_throttle_friction
    params->fetch_float(0x6D615F67, &get_at<float>(wheel, 0xE8), 0);     // handbrake_locked_friction
    read_miles_per_hour(params, 0x60B2C097, get_at<float>(wheel, 0xD0)); // min_static_grip_velocity
    read_miles_per_hour(params, 0xBB0D9370, get_at<float>(wheel, 0xD4)); // max_static_grip_velocity
    read_miles_per_hour(params, 0xB71803AD, get_at<float>(wheel, 0xD8)); // min_dynamic_grip_velocity
    read_feet(params, 0x3BCCBADC, get_at<float>(wheel, 0xF0));
    read_feet(params, 0x9439D144, get_at<float>(wheel, 0xF4));
}

} // namespace objects
