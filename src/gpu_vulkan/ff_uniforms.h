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
// (NOT ORIGINAL - [Graphics] rt_pbr, vulkan_surface.h) the per-pixel lighting's: read only by keys with
// FragmentKey::surface set (pushed only for their draws)
constexpr uint32_t kSurfaceUniformsBinding = 6; // SurfaceUniforms
constexpr uint32_t kSurfaceMapBinding = 7;      // stage 0's texture's surface map (kSurfaceMap)
// (NOT ORIGINAL - [Graphics] rt_water, vulkan_water.h) the water's: read only by keys with kSurfaceWater (pushed only for
// their draws)
constexpr uint32_t kWaterUniformsBinding = 8; // WaterUniforms
constexpr uint32_t kWaterSceneBinding = 9;    // what was drawn before the view's first water: its colour
constexpr uint32_t kWaterDepthBinding = 10;   // ... its linear depth (R32F: the distance along the view, 1e9 the sky)
constexpr uint32_t kWaterSkyBinding = 11;     // the level's sky captured (rt::SkyCapture: 3 x 2 faces), or blank
constexpr uint32_t kWaterWavesBinding = 12;   // the ripples' tiling texture (slope x, slope z, height, foam pattern)

// The vertex attributes' locations (a vertex format's parts in Direct3D 9's order). D3DCOLOR is
// VK_FORMAT_B8G8R8A8_UNORM: its bytes are blue, green, red, alpha, and the shader reads it as rgba.
constexpr uint32_t kPositionLocation = 0; // XYZ: three floats; XYZRHW: four
constexpr uint32_t kNormalLocation = 1;   // three floats
constexpr uint32_t kDiffuseLocation = 2;  // D3DCOLOR
constexpr uint32_t kSpecularLocation = 3; // D3DCOLOR
constexpr uint32_t kFirstTexSetLocation = 4; // set n at 4 + n: one to four floats (the vertex format's size bits)
// (added by the upscaler) The position the frame before (three floats; binding 1: the vertices the CPU wrote then, or
// the same ones) - read with VertexKey::motion.
constexpr uint32_t kPreviousPositionLocation = kFirstTexSetLocation + kMaxTexSets;

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
    // (added by the upscaler; read with VertexKey::motion) The previous position into the frame before's clip space
    // (its world x view x unjittered projection, Direct3D's row vectors), and this frame's jitter in Direct3D's clip
    // space (x, y: what was added to x / w and y / w) - taken out of this frame's position for the motion.
    Float4x4 previous_clip{};
    Float4 motion_jitter{};
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

// (NOT ORIGINAL - rt_pbr) Binding 6, the per-pixel lighting's (fragment shader). Directions are in camera space (the
// vertex shader's eye space), toward the light, normalised. vulkan_surface.cpp fills it.
struct SurfaceUniforms {
    std::array<Float4, kMaxLights> light_toward{}; // the draw's enabled lights (kSurfaceLit); a zero vector: none
    std::array<Float4, kMaxLights> light_colour{}; // their diffuse colours (rgb)
    Float4 sun{};        // toward the sun the bake was lit by (xyz); w: its weight 0 - 1 (0: no sun - the sky alone)
    Float4 sun_colour{}; // rgb: the sun's light; w: the ambient share a of a sunlit surface's baked light (> 0)
    Float4 sky{};        // xyz: up (the world's +y); w: how much of the normal map's detail shows in the shade (0 - 1)
    Float4 params{};     // x: specular strength, y: normal strength, z: roughness without a map, w: F0
    Float4 shade{};      // x, y: the bake's light level (luma of 2 x v0) from in shade to sunlit; z, w: the detail's range
    Float4 relit{};      // reserved for the relit mode (kSurfaceRelit; zeros)
};

// (NOT ORIGINAL - rt_water) Binding 8, the water's (fragment shader): one per view, vulkan_water.cpp fills it. Matrices
// as the vertex block's (Direct3D's, read as their transpose: M * v).
struct WaterUniforms {
    Float4x4 eye_to_world{}; // the view's inverse: camera space -> the world
    Float4x4 world_to_eye{}; // the view
    Float4x4 projection{};   // the view's projection (camera space -> Direct3D's clip space, y up)
    Float4 viewport{};       // the view's rectangle in the target: x, y, width, height (pixels)
    Float4 target{};         // x, y: 1 / the target's width and height; z: 1 when the copy behind the water is there; w: HDR
    Float4 time{};           // x: the game's seconds; y, z: the wind's direction (world x, z); w: the wind's strength
    Float4 waves{};          // x: the waves' height; y: how far one sees into the water (units); z: foam; w: the texture's
                             // detail kept
    Float4 tint{};           // rgb: the water's colour; w: 1 - the texture's own (auto)
    Float4 sun{};            // xyz: toward the sun (world); w: 1 when there is one
    Float4 sun_colour{};     // rgb: the sun's colour; w: the glitter's peak
    Float4 sky{};            // rgb: the sky's colour where nothing else is known; w: 1 when the sky capture is bound
    Float4 camera{};         // xyz: the camera's position (world); w: the refraction's strength
    Float4 misc{};           // x: screen-space reflections on; y: the swell's wavelength scale; z, w: unused
};

constexpr std::size_t kMatrixBytes = 64;
constexpr std::size_t kVectorBytes = 16;
static_assert(offsetof(VertexUniforms, texture_matrix) == 3 * kMatrixBytes, "std140: matrices are 64 bytes");
static_assert(offsetof(VertexUniforms, pixel) == (3 + kStageCount) * kMatrixBytes, "std140");
constexpr std::size_t kVectorsBeforeLights = 10; // pixel ... point_scale
constexpr std::size_t kFragmentVectors = kPixelShaderConstants + 4;
static_assert(offsetof(VertexUniforms, light_direction) == offsetof(VertexUniforms, pixel) + (kVectorsBeforeLights * kVectorBytes),
              "std140: vectors are 16 bytes");
static_assert(offsetof(VertexUniforms, previous_clip) == offsetof(VertexUniforms, light_direction) + (4 * kMaxLights * kVectorBytes),
              "std140: no padding");
static_assert(sizeof(VertexUniforms) == offsetof(VertexUniforms, previous_clip) + kMatrixBytes + kVectorBytes,
              "std140: no padding");
static_assert(sizeof(FragmentUniforms) == kFragmentVectors * kVectorBytes, "std140: no padding");
constexpr std::size_t kSurfaceVectors = (2 * kMaxLights) + 6;
static_assert(sizeof(SurfaceUniforms) == kSurfaceVectors * kVectorBytes, "std140: no padding");
constexpr std::size_t kWaterVectors = 10;
static_assert(sizeof(WaterUniforms) == (3 * kMatrixBytes) + (kWaterVectors * kVectorBytes), "std140: no padding");

} // namespace render::vulkan
