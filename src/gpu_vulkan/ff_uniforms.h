#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "gpu_vulkan/ff_key.h"

// NOT ORIGINAL - what the fixed-function emulation's shaders read besides their key (ff_key.h, ff_shaders.h): two
// uniform blocks in std140 layout and the stages' textures, all in descriptor set 0, and the vertex attributes'
// locations. The structs below are the blocks byte for byte (the static_asserts hold them to std140's offsets), so the
// backend fills them and copies them into a uniform buffer as they are.
//
// Matrices are Direct3D 9's as they are (D3DMATRIX, row-major, row vectors: v * M). Uploaded unchanged, GLSL reads each
// as its transpose and computes M * v - the same product - so nothing is transposed on either side.
namespace render::vulkan {

using Float4 = std::array<float, 4>;
constexpr std::size_t kMatrixFloats = 16;
using Float4x4 = std::array<float, kMatrixFloats>; // a D3DMATRIX's 16 floats, row by row

// Descriptor set 0's bindings.
constexpr uint32_t kVertexUniformsBinding = 0;
constexpr uint32_t kFragmentUniformsBinding = 1;
constexpr uint32_t kFirstTextureBinding = 2; // stage n's texture (combined image and sampler) at 2 + n

// The vertex attributes' locations (a vertex format's parts in Direct3D 9's order). D3DCOLOR is
// VK_FORMAT_B8G8R8A8_UNORM: its bytes are blue, green, red, alpha, and the shader reads it as rgba.
constexpr uint32_t kPositionLocation = 0; // XYZ: three floats; XYZRHW: four
constexpr uint32_t kNormalLocation = 1;   // three floats
constexpr uint32_t kDiffuseLocation = 2;  // D3DCOLOR
constexpr uint32_t kSpecularLocation = 3; // D3DCOLOR
constexpr uint32_t kFirstTexSetLocation = 4; // set n at 4 + n: one to four floats (the vertex format's size bits)

// Binding 0, the vertex shader's.
struct VertexUniforms {
    Float4x4 world_view{};    // world x view (Direct3D's D3DTS_WORLD x D3DTS_VIEW): positions into camera space
    Float4x4 projection{};    // D3DTS_PROJECTION
    Float4x4 normal_matrix{}; // the inverse transpose of world_view (its 3x3 part is used): normals into camera space
    std::array<Float4x4, kStageCount> texture_matrix{}; // D3DTS_TEXTURE0-3
    // x, y: 1 / the viewport's width and height (Direct3D 9's pixel centres are half a pixel from Vulkan's); z, w: the
    // viewport's left and top (pre-transformed positions are the render target's pixels)
    Float4 pixel{};
    Float4 ambient{};          // D3DRS_AMBIENT
    Float4 material_diffuse{}; // the D3DMATERIAL9
    Float4 material_ambient{};
    Float4 material_specular{};
    Float4 material_emissive{};
    Float4 material_power{}; // x: the specular power
    Float4 fog{};            // vertex fog: start, end, density (D3DRS_FOGSTART / FOGEND / FOGDENSITY)
    Float4 point{};          // D3DRS_POINTSIZE, POINTSIZE_MIN, POINTSIZE_MAX, the viewport's height
    Float4 point_scale{};    // D3DRS_POINTSCALE_A, _B, _C
    std::array<Float4, kMaxLights> light_direction{}; // in camera space, as D3DLIGHT9's Direction (from the light)
    std::array<Float4, kMaxLights> light_diffuse{};
    std::array<Float4, kMaxLights> light_specular{};
    std::array<Float4, kMaxLights> light_ambient{};
};

constexpr std::size_t kPixelShaderConstants = 8;

// Binding 1, the fragment shader's.
struct FragmentUniforms {
    std::array<Float4, kPixelShaderConstants> constant{}; // the ps.1.1 constants c0-c7 (read clamped to -1 - 1)
    Float4 texture_factor{};          // D3DRS_TEXTUREFACTOR as 0-1 rgba
    Float4 fog_colour{};              // D3DRS_FOGCOLOR as 0-1 rgba
    Float4 fog{};                     // table fog: start, end, density
    Float4 alpha_reference{};         // x: D3DRS_ALPHAREF, 0-255
};

constexpr std::size_t kMatrixBytes = 64;
constexpr std::size_t kVectorBytes = 16;
static_assert(offsetof(VertexUniforms, texture_matrix) == 3 * kMatrixBytes, "std140: matrices are 64 bytes");
static_assert(offsetof(VertexUniforms, pixel) == (3 + kStageCount) * kMatrixBytes, "std140");
constexpr std::size_t kVectorsBeforeLights = 10; // pixel ... point_scale
constexpr std::size_t kFragmentVectors = kPixelShaderConstants + 4;
static_assert(offsetof(VertexUniforms, light_direction) == offsetof(VertexUniforms, pixel) + (kVectorsBeforeLights * kVectorBytes),
              "std140: vectors are 16 bytes");
static_assert(sizeof(VertexUniforms) == offsetof(VertexUniforms, light_direction) + (4 * kMaxLights * kVectorBytes),
              "std140: no padding");
static_assert(sizeof(FragmentUniforms) == kFragmentVectors * kVectorBytes, "std140: no padding");

} // namespace render::vulkan
