#pragma once

#include <string_view>

// NOT ORIGINAL - the water's GLSL ([Graphics] rt_water; vulkan_water.h has what it does and why, docs/design/vulkan.md
// "Water"). ff_shaders.cpp puts the declarations into a fragment shader whose key has kSurfaceWater (after the stages'
// textures, the vertex colour and the per-pixel inputs - v_surface_eye - are declared), and calls
//   vec4 water_shade(vec4 colour, vec4 texel, vec2 uv, inout float alpha)
// after the program's colour is clamped: `colour` the program's result, `texel` stage 0's texel, `uv` stage 0's
// coordinates. It returns the colour shaded as water where the surface faces up (the program's colour elsewhere, and
// its alpha always - the alpha test sees the game's alpha); `alpha` becomes the alpha to write.
namespace render::vulkan {

[[nodiscard]] std::string_view water_declarations_glsl();

} // namespace render::vulkan
