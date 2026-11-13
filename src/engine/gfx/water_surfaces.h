#pragma once

#include <cstdint>

// NOT ORIGINAL - water shading's contract (docs/design/vulkan.md "Water"; the Vulkan Gpu's [Graphics] rt_water):
// render_pc says which material it is drawing with when that material is water - one of the game's moving water
// surfaces - and a backend that shades water (gpu_vulkan/vulkan_water.h) asks for each draw. API-neutral: no Vulkan.
//
// Which materials are water (render_pc/water_materials.cpp; [debug] rt_water_materials=1 logs each one once): a
// material whose passes move - a UV wibble (pass flag 0x1, or a pass's oscillator) or a texture animation (0x800, or the
// material's animation) - and whose first pass has a texture and isn't added, subtracted or multiplied onto what is
// behind it (glows, shadows, light pools are drawn so). The backend narrows it per pixel: only surfaces that face up
// (the sea, a canal, a pool - not a waterfall or a scrolling sign) and whose own texture isn't warm (lava, fire).
//
// While no backend wants it (wanted() false - the default, Direct3D 9) render_pc never asks: nothing is classified.
namespace render {
struct GpuTexture;
} // namespace render

namespace render::water {

// A backend shades water (set when [Graphics] rt_water changes): render_pc marks its water materials' draws.
void set_wanted(bool wanted);
[[nodiscard]] bool wanted();

// The material being drawn with now (render_pc's MaterialRef::hand_in, each time a material's states are set): its
// first pass's texture when it is water, else null - every later draw with that texture on stage 0 is water, until the
// next material. `material` is the material's checksum (for the backend's log).
void set_current(const GpuTexture* texture, uint32_t material);
[[nodiscard]] const GpuTexture* current_texture();
[[nodiscard]] uint32_t current_material();

// The level's water as render_pc's table gives it (render_pc/water_materials.cpp; set when the level changes): a calm
// harbour or canal has lower, shorter waves and murkier water than the open sea. The backend's settings scale it.
struct LevelWater {
    uint32_t level = 0;          // the level's main scene's checksum (0: none loaded)
    float waves = 0.4F;          // the swell's steepness scale (1: the open sea)
    float swell_length = 500.0F; // the swell's longest wavelength (units)
    float visibility = 450.0F;   // how far one sees into the water (units)
    float heading = 30.0F;       // the wind's way, degrees from +x toward +z
};
void set_level_water(const LevelWater& level);
[[nodiscard]] LevelWater level_water();

// How many materials render_pc has found to be water since the start (the log, checks).
void note_water_material();
[[nodiscard]] uint32_t water_materials();

} // namespace render::water
