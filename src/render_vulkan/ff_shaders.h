#pragma once

#include <string>

#include "render_vulkan/ff_key.h"

// NOT ORIGINAL - the fixed-function emulation's shader generator (docs/design/vulkan.md "Fixed-function emulation in
// Vulkan"): GLSL 4.50 for Vulkan from a key (ff_key.h), with the uniform blocks, bindings and attribute locations of
// ff_uniforms.h. Compiled to SPIR-V by glsl_compiler.h. Each generated shader starts with a comment naming its key.
//
// What each part computes, and where Direct3D 9's exact behaviour is uncertain, is written beside the code in
// ff_shaders.cpp. In short:
//   - the vertex shader: the position through world x view and the projection (Direct3D's y up turned into Vulkan's y
//     down, and the half-pixel offset between the two APIs' pixel centres added), or a pre-transformed position from
//     the render target's pixels; fixed-function lighting (ambient and directional lights, the material sources,
//     specular with or without the local viewer); vertex fog (linear / exp / exp2, by depth or by range); the stages'
//     texture coordinates (a coordinate set or camera-space normal / position / reflection, through the texture matrix
//     with COUNTn and PROJECTED); the point size;
//   - the fragment shader: the texture stages (every D3DTOP but premodulation and bump mapping, each stage clamped to
//     0-1) and the specular add - or one of the game's ps.1.1 shaders ported to GLSL (the six fixed ones and the
//     per-material generator, render_pc/pixel_shaders.cpp) - then fog (per vertex, or per pixel for table fog), then
//     the alpha test against the 0-255 reference.
namespace render::vulkan {

// The vertex shader for a normalised key (normalised(ShaderKey).vertex).
[[nodiscard]] std::string vertex_shader_glsl(const VertexKey& key);
// The fragment shader for a normalised key (normalised(ShaderKey).fragment).
[[nodiscard]] std::string fragment_shader_glsl(const FragmentKey& key);

} // namespace render::vulkan
