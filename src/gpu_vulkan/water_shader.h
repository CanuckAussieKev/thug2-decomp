#pragma once

#include <string>
#include <string_view>

// NOT ORIGINAL - the water's GLSL ([Graphics] rt_water; vulkan_water.h has what it does and why, docs/design/vulkan.md
// "Water"). ff_shaders.cpp puts the declarations into a fragment shader whose key has FragmentKey::water (after the stages'
// textures, the vertex colour and the per-pixel inputs - v_surface_eye - are declared), and calls
//   vec4 water_shade(vec4 colour, vec4 texel, vec2 uv, inout float alpha)
// after the program's colour is clamped: `colour` the program's result, `texel` stage 0's texel, `uv` stage 0's
// coordinates. It returns the colour shaded as water where the surface faces up (the program's colour elsewhere, and
// its alpha always - the alpha test sees the game's alpha); `alpha` becomes the alpha to write.
//
// The moving surface ([Graphics] rt_water_geometry; FragmentKey::water kWaterMoving, VertexKey::surface
// kVertexWaterMoving): the vertex shader declares water_vertex_declarations_glsl (after v_fog and v_surface_eye) and puts
// water_vertex_move_glsl right after its camera-space position `eye` is made - the point moved by the swell, the move
// handed on -; the fragment shader's declarations are then water_declarations_glsl(true): it shades the rest point.
namespace render::vulkan {

[[nodiscard]] std::string water_declarations_glsl(bool moving);
[[nodiscard]] std::string water_vertex_declarations_glsl();
[[nodiscard]] std::string_view water_vertex_move_glsl();

} // namespace render::vulkan
