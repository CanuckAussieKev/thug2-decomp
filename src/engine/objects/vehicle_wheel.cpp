// The vehicle component's member function registration and wheel settings (engine/objects/vehicle_wheel.h).

#include "engine/objects/vehicle_wheel.h"

#include <cstdint>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/core/pointer_cell.h"

namespace objects {

uint32_t g_vehicle_wheel_member_functions[60]{
    0x93B713A6, 0xE47F1B79, 0, 0, 0, 0, 0xA80A0D36, 0xE47F1B79, 0, 0, 0, 0, 0x4B0B27DD, 0xE47F1B79, 0, 0, 0, 0,
    0xC33608E4, 0xE47F1B79, 0, 0, 0, 0, 0xDB35AAD8, 0xE47F1B79, 0, 0, 0, 0, 0x5008B253, 0xE47F1B79, 0, 0, 0, 0,
    0x279602ED, 0xE47F1B79, 0, 0, 0, 0, 0x4C73526B, 0xE47F1B79, 0, 0, 0, 0, 0x81FAAC21, 0xE47F1B79, 0, 0, 0, 0,
    0,          0xE47F1B79, 0, 0, 0, 0};

namespace {

constexpr uint32_t kVehicle = 0xE47F1B79;

constexpr uint32_t kSteering = 0x07560E63, kRight = 0x4B358AEB, kFixed = 0x613631CD, kLeft = 0x85981897;
constexpr uint32_t kDrive = 0x97E20A70, kYes = 0x8A18CA56, kNo = 0x9855D7E0;

constexpr float kGravity = 386.3999938964844f;             // 0x006466A4 (inches a second squared)
constexpr float kInchesPerMile = 63360.0f;                 // 0x006461E4
constexpr float kHoursPerSecond = 0.00027777778450399637f; // 0x006461E0 (1 / 3600)
constexpr float kInchesPerFoot = 12.0f;                    // 0x0064546C

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
void register_vehicle_member_functions()
{
    script::register_member_function_table(cell::pointer<void>(kVehicle), g_vehicle_wheel_member_functions, 0x18);
}

// 0x00434F20
void read_wheel_settings(DrivableModule::Wheel* wheel, script::Struct* params)
{
    // (each checksum is read into the parameters' own argument slot: a steering= that isn't a name leaves the
    // pointer's value there)
    auto choice = cell::of(params);
    if (params->has_entry_named(kSteering)) {
        params->read_hash_value(kSteering, &choice, 0);
        if (choice == kRight)
            wheel->steering_side_ = 2;
        else if (choice == kFixed)
            wheel->steering_side_ = 0;
        else if (choice == kLeft)
            wheel->steering_side_ = 1;
    }
    if (params->has_entry_named(kDrive)) {
        params->read_hash_value(kDrive, &choice, 0);
        if (choice == kYes)
            wheel->drive = true;
        else if (choice == kNo)
            wheel->drive = false;
    }
    params->fetch_float(0x9E931FDF, &wheel->spring_rate_, 0);  // spring_rate
    params->fetch_float(0xAFA960FF, &wheel->damping_rate_, 0); // damping_rate
    params->fetch_float(0xC48391A5, &wheel->radius, 0);        // radius
    if (params->has_entry_named(0xCA73775D)) {                 // moment
        float& moment = wheel->inv_moment_;
        params->fetch_float(0xCA73775D, &moment, 0);
        moment = kGravity / moment;
    }
    params->fetch_float(0xD87D0183, &wheel->max_drawn_y_, 0);
    params->fetch_float(0xF36B97C8, &wheel->static_friction_, 0);          // static_friction
    params->fetch_float(0x674852F6, &wheel->dynamic_friction_, 0);         // dynamic_friction
    params->fetch_float(0x52242919, &wheel->parking_grip_powered_, 0);     // handbrake_throttle_friction
    params->fetch_float(0x6D615F67, &wheel->parking_grip_locked_, 0);      // handbrake_locked_friction
    read_miles_per_hour(params, 0x60B2C097, wheel->min_static_grip_vel_);  // min_static_grip_velocity
    read_miles_per_hour(params, 0xBB0D9370, wheel->max_static_grip_vel_);  // max_static_grip_velocity
    read_miles_per_hour(params, 0xB71803AD, wheel->min_dynamic_grip_vel_); // min_dynamic_grip_velocity
    read_feet(params, 0x3BCCBADC, wheel->brake_torque_);
    read_feet(params, 0x9439D144, wheel->handbrake_torque_);
}

} // namespace objects
