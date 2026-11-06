#pragma once

#include <array>
#include <cstdint>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_pixel_program.h"

// NOT ORIGINAL - what a draw takes from the Gpu's state block (engine/gfx/gpu.h; docs/design/vulkan.md, V7): the
// fixed-function emulation's key (ff_key.h) and its uniform blocks (ff_uniforms.h), the vertex format's attributes, and
// Direct3D 9's raster, blend and depth numbers as Vulkan's. The block holds Direct3D 9's values, so each field converts
// as it is.
namespace render::vulkan {

// The draw's shaders: the vertex format, lighting, fog, the stages' coordinates and operations, the alpha test, point
// sprites, and the bound pixel shader's program (null: the stages), normalised (ff_key.h).
[[nodiscard]] ShaderKey draw_shader_key(const GpuStateBlock& block, const ShaderProgram* program, bool points);

// The draw's uniform blocks.
void fill_vertex_uniforms(const GpuStateBlock& block, VertexUniforms* uniforms);
void fill_fragment_uniforms(const GpuStateBlock& block, FragmentUniforms* uniforms);

// A vertex format's attributes in Direct3D 9's order and at its offsets (position and blend weights, normal, point size,
// diffuse, specular, the coordinate sets), one binding; the stride is the stream's.
constexpr std::size_t kMostAttributes = 4 + kMaxTexSets;
struct VertexLayout {
    std::array<VkVertexInputAttributeDescription, kMostAttributes> attributes{};
    uint32_t count = 0;
    uint32_t bytes = 0; // the format's own size
};
[[nodiscard]] VertexLayout vertex_layout(uint32_t fvf);

// Direct3D 9's numbers as Vulkan's.
[[nodiscard]] VkBlendFactor blend_factor(uint32_t blend);
[[nodiscard]] VkBlendOp blend_op(uint32_t op);
[[nodiscard]] VkCompareOp compare_op(uint32_t func);
[[nodiscard]] VkCullModeFlags cull_mode(uint32_t cull);
[[nodiscard]] VkPrimitiveTopology topology(GpuPrimitive primitive);
// A D3DCOLOR (A8R8G8B8) as 0 - 1 RGBA.
[[nodiscard]] Float4 colour_of(uint32_t argb);

} // namespace render::vulkan
