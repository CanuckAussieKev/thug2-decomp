#pragma once

#include <cstdint>

#include "engine/objects/vehicle_component.h"

namespace script {
class Struct;
} // namespace script

// Pieces of the vehicle component (type 0xE47F1B79, "vehicle"; its class is decompiled with the rest of its code):
// the registration of its script member functions, and the reading of one wheel's settings.
namespace objects {

// 0x00434F00 (not a Ghidra function; registered with the vehicle component's Create 0x0043AA30 by 0x004E4860): its
// script member functions (the table at 0x00684060) registered.
void register_vehicle_member_functions();

// 0x00434F20 (stdcall): a wheel's settings from `params`, each only if given: steering (left 1 / right 2 / fixed 0),
// drive (yes / no), spring_rate, damping_rate, radius, moment (kept as 386.4 / moment - gravity in inches per second
// squared over it), 0xD87D0183 (the lowest drawn), static_friction, dynamic_friction, handbrake_throttle_friction,
// handbrake_locked_friction, the grip velocities min_static / max_static / min_dynamic_grip_velocity (miles an
// hour, kept in inches a second), and 0x3BCCBADC / 0x9439D144 (the brake / handbrake torques: feet, kept in inches).
void read_wheel_settings(DrivableModule::Wheel* wheel, script::Struct* params);

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_vehicle_wheel_member_functions[60];
} // namespace objects
