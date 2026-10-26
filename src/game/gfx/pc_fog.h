#pragma once

#include <cstdint>

// The fog on PC (Nx engine; what SetFogColor / SetFogDistance / EnableFog reach through 0x00490DE0-0x00490E20):
// the device's fog states and their cache (on 0x00787338, start / end 0x00787340 / 44, colour 0x0078733C). The
// device isn't told while the loading screen is up.
namespace Nx {

// 0x004B8E40: fog on or off.
void __cdecl SetFogEnabled(bool on);
// 0x004B8E80: where the fog starts; it ends 7200 further with distance clipping on, else at four times the start.
void __cdecl SetFogDistance(float start);
// 0x004B8EE0: the colour (RGB; the device gets XRGB); an alpha above 0 turns the fog on, and gives the pixel shaders'
// fog constant (0x00786F0C, three floats) alpha / 128, at most 1.
void __cdecl SetFogColor(uint32_t rgba);

} // namespace Nx
