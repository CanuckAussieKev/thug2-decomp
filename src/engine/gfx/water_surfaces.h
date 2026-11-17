#pragma once

#include <cstdint>

#include "engine/gfx/water_waves.h"

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

// The level's water as render_pc's table gives it (render_pc/water_materials.cpp; set when the level changes): how clear
// it is - a canal murkier than the open sea. (Changed by the physical waves: its waves, swell length and wind heading
// are no longer the level's - they are each water body's, from the weather's wind, its fetch, depth and shelter below.)
inline constexpr float kLevelVisibility = 450.0F;  // (a level without an entry)
struct LevelWater {
    uint32_t level = 0;                       // the level's main scene's checksum (0: none loaded)
    float visibility = kLevelVisibility;      // how far one sees into the water (units)
};
void set_level_water(const LevelWater& level);
[[nodiscard]] LevelWater level_water();

// (added by the physical waves - water_waves.h, docs/design/vulkan.md "Water") The water body a draw shows: render_pc
// measures each water mesh of the level from the level's own geometry as it loads (render_pc/water_bodies.h: its fetch
// along the wind, the depth to its bed, how open it is to the sky) and hands the backend the mesh being drawn - its waves
// worked out for the weather's wind (engine/gfx/wind.h). `known` false: no body measured for the draw (not a level
// mesh, or not measured yet) - it is drawn still.
struct WaterBody {
    bool known = false;
    uint64_t id = 0;         // the body (its mesh) - with `version`, what the backend keys its block by
    uint32_t version = 0;    // changes when its waves are worked out again (the wind changed, the level was measured)
    float fetch = 0.0F;      // metres to the upwind shore (kOpenFetch: open to the horizon)
    float depth = 0.0F;      // metres to the bed (kDeepWater: none found)
    float shelter = 0.0F;    // (round W) the share of the weather's wind reaching it (air_volume.h: 0 in a closed room)
    float sky = 0.0F;        // (round W) the share of the rain falling on it (0 under a roof)
    float wind = 0.0F;       // the wind over it (m/s 10 m up: the weather's x its shelter)
    SeaWaves waves;          // what its waves are (water_waves.h)
};
void set_current_body(const WaterBody& body);
[[nodiscard]] WaterBody current_body();

// The moving surface ([Graphics] rt_water_geometry, docs/design/vulkan.md "Moving water"): a backend that moves the water's
// vertices by its swell wants the water's meshes split into a grid as a level loads (render_pc/water_geometry.h - only
// flat meshes of water materials; set with set_wanted, true while rt_water and rt_water_geometry are both on), and render_pc
// says, for each draw of a mesh it split, that the draw's vertices may be moved (set_current clears it: any other draw's
// vertices stay where they are - a waterfall, a mesh not split, a level loaded before the setting was on).
void set_geometry_wanted(bool wanted);
[[nodiscard]] bool geometry_wanted();
void set_current_waves(bool waves);
[[nodiscard]] bool current_waves();

// How many materials render_pc has found to be water since the start (the log, checks).
void note_water_material();
[[nodiscard]] uint32_t water_materials();

} // namespace render::water
