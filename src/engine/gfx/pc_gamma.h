#pragma once

#include <array>

// The PC gamma (Nx engine): the red, green and blue gammas (0x0067E9C8-D0, 0.5-2.5 as 0-1 is given) and the device's
// gamma ramp from them (0x00781BF8).
namespace render {

// 0x004D06A0: the gammas as they were given (0-1).
// (red, green, blue)
std::array<float, 3> get_device_gamma();
// 0x004D06F0: the ramp: each of 256 levels to the power 1 / gamma, in 65536ths.
void build_gamma_ramp();
// 0x004D07B0: the gammas set (each kept within 0-1 first) and the ramp sent to the device.
void set_device_gamma(float r, float g, float b);

} // namespace render
