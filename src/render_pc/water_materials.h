#pragma once

#include <cstdint>

// NOT ORIGINAL - which of the game's materials are water (engine/gfx/water_surfaces.h; the Vulkan Gpu's [Graphics]
// rt_water). Called from MaterialRef::hand_in while a backend wants water marked: nothing happens otherwise.
namespace render {

struct MaterialRef;

namespace water_materials {

// Whether `material` is water: its passes move (a UV wibble or a texture animation) and its first pass has a texture
// and is drawn as itself or blended by alpha over what is behind it (blend modes 0, 5, 6, 11) - not added, subtracted
// or multiplied (glows, shadows, light pools).
[[nodiscard]] bool is_water(const MaterialRef* material);

// The material whose states are being set (hand_in): water_surfaces' current material set from it - its first pass's
// texture when it is water, else none. [debug] rt_water_materials=1 logs each moving material once, water or not.
void note_material(const MaterialRef* material);

} // namespace water_materials
} // namespace render
