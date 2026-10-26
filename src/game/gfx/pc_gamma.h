#pragma once

// The PC gamma (Nx engine): the red, green and blue gammas (0x0067E9C8-D0, 0.5-2.5 as 0-1 is given) and the device's
// gamma ramp from them (0x00781BF8).
namespace Nx {

// 0x004D06A0: the gammas as they were given (0-1).
void __cdecl GetDeviceGamma(float* r, float* g, float* b);
// 0x004D06F0: the ramp: each of 256 levels to the power 1 / gamma, in 65536ths.
void __cdecl BuildGammaRamp();
// 0x004D07B0: the gammas set (each kept within 0-1 first) and the ramp sent to the device.
void __cdecl SetDeviceGamma(float r, float g, float b);

} // namespace Nx
