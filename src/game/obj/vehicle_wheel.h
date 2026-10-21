#pragma once

#include <cstdint>

namespace Script {
class CStruct;
} // namespace Script

// Pieces of the vehicle component (type 0xE47F1B79, "vehicle"; its class is decompiled with the rest of its code):
// the registration of its script member functions, and the reading of one wheel's settings.
namespace Obj {

// 0x00434F00 (not a Ghidra function; registered with the vehicle component's Create 0x0043AA30 by 0x004E4860): its
// script member functions (the table at 0x00684060) registered.
void __cdecl RegisterVehicleMemberFunctions();

// 0x00434F20 (stdcall): a wheel's settings from `params`, each only if given: steering (left 1 / right 2 / fixed 0,
// +0xB0), drive (yes / no, +0xB4), spring_rate (+0xC8), damping_rate (+0xCC), radius (+0xC0), moment (kept as
// 386.4 / moment - gravity in inches per second squared over it, +0xC4), 0xD87D0183 (+0xBC), static_friction
// (+0xDC), dynamic_friction (+0xE0), handbrake_throttle_friction (+0xE4), handbrake_locked_friction (+0xE8), the
// grip velocities min_static / max_static / min_dynamic_grip_velocity (miles an hour, kept in inches a second:
// +0xD0 / +0xD4 / +0xD8), and 0x3BCCBADC / 0x9439D144 (feet, kept in inches: +0xF0 / +0xF4).
void __stdcall ReadWheelSettings(uint8_t* wheel, Script::CStruct* params);

} // namespace Obj
