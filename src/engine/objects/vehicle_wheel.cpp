// The vehicle component's member function registration and wheel settings (engine/objects/vehicle_wheel.h).

#include "engine/objects/vehicle_wheel.h"

#include <cstdint>
#include <array>

#include "engine/objects/vehicle_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
namespace objects {
namespace {
constexpr uint32_t kVehicleKick = 0x93B713A6; // Vehicle_Kick
constexpr uint32_t kVehicleName = 0xE47F1B79; // Vehicle
constexpr uint32_t kVehicleWake = 0xA80A0D36; // Vehicle_Wake
constexpr uint32_t kVehicleMoveToRestart = 0x4B0B27DD; // Vehicle_MoveToRestart
constexpr uint32_t kVehiclePlaceBeforeCamera = 0xC33608E4; // Vehicle_PlaceBeforeCamera
constexpr uint32_t kVehicleAdjustGravity = 0xDB35AAD8; // Vehicle_AdjustGravity
constexpr uint32_t kVehicleHandbrakeActive = 0x5008B253; // Vehicle_HandbrakeActive
constexpr uint32_t kVehicleAllWheelsAreInContact = 0x279602ED; // Vehicle_AllWheelsAreInContact
constexpr uint32_t kHash4C73526B = 0x4C73526B; // (text unknown)
constexpr uint32_t kVehicleIsSkaterVisible = 0x81FAAC21; // Vehicle_IsSkaterVisible
constexpr uint32_t kSpringRate = 0x9E931FDF; // spring_rate
constexpr uint32_t kDampingRate = 0xAFA960FF; // damping_rate
constexpr uint32_t kRadius = 0xC48391A5; // Radius
constexpr uint32_t kMoment = 0xCA73775D; // Moment
constexpr uint32_t kHashD87D0183 = 0xD87D0183; // (text unknown)
constexpr uint32_t kStaticFriction = 0xF36B97C8; // static_friction
constexpr uint32_t kDynamicFriction = 0x674852F6; // dynamic_friction
constexpr uint32_t kHandbrakeThrottleFriction = 0x52242919; // handbrake_throttle_friction
constexpr uint32_t kHandbrakeLockedFriction = 0x6D615F67; // handbrake_locked_friction
constexpr uint32_t kMinStaticGripVelocity = 0x60B2C097; // min_static_grip_velocity
constexpr uint32_t kMaxStaticGripVelocity = 0xBB0D9370; // max_static_grip_velocity
constexpr uint32_t kMinDynamicGripVelocity = 0xB71803AD; // min_dynamic_grip_velocity
constexpr uint32_t kBrakeTorque = 0x3BCCBADC; // brake_torque
constexpr uint32_t kHandbrakeTorque = 0x9439D144; // handbrake_torque
} // namespace

std::array<script::MemberFunctionEntry, kVehicleWheelMemberFunctionCount>& vehicle_wheel_member_functions()
{
    static std::array<script::MemberFunctionEntry, kVehicleWheelMemberFunctionCount> vehicle_wheel_member_functions{{
        {.callsign_ = kVehicleKick, .owner_kind_ = kVehicleName}, {.callsign_ = kVehicleWake, .owner_kind_ = kVehicleName}, {.callsign_ = kVehicleMoveToRestart, .owner_kind_ = kVehicleName}, {.callsign_ = kVehiclePlaceBeforeCamera, .owner_kind_ = kVehicleName},
        {.callsign_ = kVehicleAdjustGravity, .owner_kind_ = kVehicleName}, {.callsign_ = kVehicleHandbrakeActive, .owner_kind_ = kVehicleName}, {.callsign_ = kVehicleAllWheelsAreInContact, .owner_kind_ = kVehicleName}, {.callsign_ = kHash4C73526B, .owner_kind_ = kVehicleName},
        {.callsign_ = kVehicleIsSkaterVisible, .owner_kind_ = kVehicleName}, {.callsign_ = 0, .owner_kind_ = kVehicleName}}};
    return vehicle_wheel_member_functions;
}

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
    script::register_member_function_table(vehicle_wheel_member_functions());
}

// 0x00434F20
void read_wheel_settings(DrivableModule::Wheel* wheel, script::Struct* params)
{
    // (each checksum is read into the parameters' own argument slot: a steering= that isn't a name leaves the
    // pointer's value there)
    uint32_t choice = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
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
    params->fetch_float(kSpringRate, &wheel->spring_rate_, 0);  // spring_rate
    params->fetch_float(kDampingRate, &wheel->damping_rate_, 0); // damping_rate
    params->fetch_float(kRadius, &wheel->radius, 0);        // radius
    if (params->has_entry_named(kMoment)) {                 // moment
        float& moment = wheel->inv_moment_;
        params->fetch_float(kMoment, &moment, 0);
        moment = kGravity / moment;
    }
    params->fetch_float(kHashD87D0183, &wheel->max_drawn_y_, 0);
    params->fetch_float(kStaticFriction, &wheel->static_friction_, 0);          // static_friction
    params->fetch_float(kDynamicFriction, &wheel->dynamic_friction_, 0);         // dynamic_friction
    params->fetch_float(kHandbrakeThrottleFriction, &wheel->parking_grip_powered_, 0);     // handbrake_throttle_friction
    params->fetch_float(kHandbrakeLockedFriction, &wheel->parking_grip_locked_, 0);      // handbrake_locked_friction
    read_miles_per_hour(params, kMinStaticGripVelocity, wheel->min_static_grip_vel_);  // min_static_grip_velocity
    read_miles_per_hour(params, kMaxStaticGripVelocity, wheel->max_static_grip_vel_);  // max_static_grip_velocity
    read_miles_per_hour(params, kMinDynamicGripVelocity, wheel->min_dynamic_grip_vel_); // min_dynamic_grip_velocity
    read_feet(params, kBrakeTorque, wheel->brake_torque_);
    read_feet(params, kHandbrakeTorque, wheel->handbrake_torque_);
}

} // namespace objects
