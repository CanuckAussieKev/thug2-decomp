// NOT ORIGINAL - a draw's key, uniforms and states from the Gpu's state block (vulkan_draw_state.h).
#include "gpu_vulkan/vulkan_draw_state.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_pixel_program.h"

namespace render::vulkan {

namespace {

// The block's transforms (engine/gfx/gpu.cpp's order): world, view, projection, then the stages'.
constexpr std::size_t kWorldSlot = 0;
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kProjectionSlot = 2;
constexpr std::size_t kFirstStageSlot = 3;
constexpr std::size_t kRows = 4;
constexpr uint32_t kLowHalf = 0xFFFFU;
constexpr uint32_t kHighHalfShift = 16;
constexpr uint32_t kCountMask = 0xFFU;
constexpr uint32_t kByte = 0xFFU;
constexpr float kByteScale = 255.0F;
// D3DFVF's position kinds (with the blend weights) and the parts after the normal
constexpr uint32_t kFvfPointSize = 0x020U;
constexpr uint32_t kFvfXyzB1 = 0x006U;
constexpr uint32_t kFvfXyzB5 = 0x00EU;
constexpr uint32_t kFloatBytes = 4;
constexpr uint32_t kColourBytes = 4;
// D3DBLEND / D3DBLENDOP values gpu.h doesn't name
constexpr uint32_t kBlendInvSrcColour = 4;
constexpr uint32_t kBlendDestAlpha = 7;
constexpr uint32_t kBlendInvDestAlpha = 8;
constexpr uint32_t kBlendInvDestColour = 10;
constexpr uint32_t kBlendSrcAlphaSaturate = 11;
constexpr uint32_t kBlendOpSubtract = 2;
constexpr uint32_t kBlendOpMin = 4;
constexpr uint32_t kBlendOpMax = 5;
constexpr std::array<VkFormat, 4> kFloatFormats = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                                   VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};

uint32_t state(const GpuStateBlock& block, GpuRenderState which) { return block.render_state(which); }
float float_state(const GpuStateBlock& block, GpuRenderState which) { return std::bit_cast<float>(state(block, which)); }
uint32_t stage(const GpuStateBlock& block, std::size_t index, GpuStageState which)
{
    return block.stage_state(static_cast<uint32_t>(index), which);
}

float& at(Float4x4& matrix, std::size_t row, std::size_t column) { return core::at(matrix, (row * kRows) + column); }
float at(const Float4x4& matrix, std::size_t row, std::size_t column)
{
    return core::at(matrix, (row * kRows) + column);
}

// a x b, Direct3D's row-major matrices (row vectors: v x a x b).
Float4x4 multiply(const Float4x4& a, const Float4x4& b)
{
    Float4x4 product{};
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kRows; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < kRows; ++k) {
                sum += at(a, row, k) * at(b, k, column);
            }
            at(product, row, column) = sum;
        }
    }
    return product;
}

// The transpose of the inverse of a matrix's 3x3 part (what turns normals into camera space with row vectors), in a
// 4x4: the cofactor matrix over the determinant. Cofactor (r, c) is the 2x2 determinant of the rows and columns other
// than r and c, taken cyclically (which gives each its sign).
Float4x4 normal_matrix(const Float4x4& matrix)
{
    const auto cofactor = [&matrix](std::size_t row, std::size_t column) {
        const std::size_t row1 = (row + 1) % 3;
        const std::size_t row2 = (row + 2) % 3;
        const std::size_t column1 = (column + 1) % 3;
        const std::size_t column2 = (column + 2) % 3;
        return (at(matrix, row1, column1) * at(matrix, row2, column2)) -
               (at(matrix, row1, column2) * at(matrix, row2, column1));
    };
    const float determinant = (at(matrix, 0, 0) * cofactor(0, 0)) + (at(matrix, 0, 1) * cofactor(0, 1)) +
                              (at(matrix, 0, 2) * cofactor(0, 2));
    Float4x4 result{};
    if (determinant == 0.0F) {
        return result;
    }
    const float scale = 1.0F / determinant;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            at(result, row, column) = cofactor(row, column) * scale;
        }
    }
    at(result, 3, 3) = 1.0F;
    return result;
}

Float4 as_float4(const GpuColour& colour) { return colour; }

ColourSource colour_source(uint32_t value)
{
    switch (value) {
    case gpu_value::kSourceColour1:
        return ColourSource::kColour1;
    case gpu_value::kSourceColour2:
        return ColourSource::kColour2;
    default:
        return ColourSource::kMaterial;
    }
}

FogMode fog_mode(uint32_t value) { return value <= 3 ? static_cast<FogMode>(value) : FogMode::kNone; }

} // namespace

ShaderKey draw_shader_key(const GpuStateBlock& block, const ShaderProgram* program, bool points)
{
    ShaderKey key;
    VertexKey& vertex = key.vertex;
    vertex.fvf = block.vertex_format;
    vertex.lighting = state(block, GpuRenderState::kLighting) != 0;
    vertex.colour_vertex = state(block, GpuRenderState::kColourVertex) != 0;
    vertex.specular = state(block, GpuRenderState::kSpecularEnable) != 0;
    vertex.local_viewer = state(block, GpuRenderState::kLocalViewer) != 0;
    vertex.normalize_normals = state(block, GpuRenderState::kNormalizeNormals) != 0;
    vertex.light_count = static_cast<uint8_t>(std::min<int>(block.enabled_light_count(), kMaxLights));
    vertex.diffuse_source = colour_source(state(block, GpuRenderState::kDiffuseMaterialSource));
    vertex.ambient_source = colour_source(state(block, GpuRenderState::kAmbientMaterialSource));
    vertex.specular_source = colour_source(state(block, GpuRenderState::kSpecularMaterialSource));
    vertex.emissive_source = colour_source(state(block, GpuRenderState::kEmissiveMaterialSource));
    const bool fog = state(block, GpuRenderState::kFogEnable) != 0;
    const FogMode table_fog = fog_mode(state(block, GpuRenderState::kFogTableMode));
    vertex.fog = fog && table_fog == FogMode::kNone ? fog_mode(state(block, GpuRenderState::kFogVertexMode)) : FogMode::kNone;
    vertex.range_fog = state(block, GpuRenderState::kRangeFogEnable) != 0;
    vertex.points = points;
    vertex.point_scale = state(block, GpuRenderState::kPointScaleEnable) != 0;
    vertex.coord_mask = static_cast<uint8_t>((1U << kStageCount) - 1);
    for (std::size_t index = 0; index < kStageCount; ++index) {
        const uint32_t coordinates = stage(block, index, GpuStageState::kTexCoordIndex);
        const uint32_t flags = stage(block, index, GpuStageState::kTextureTransformFlags);
        core::at(vertex.coords, index) = {.set = static_cast<uint8_t>(coordinates & kLowHalf),
                                          .gen = static_cast<TexGen>((coordinates >> kHighHalfShift) & kByte),
                                          .count = static_cast<uint8_t>(flags & kCountMask),
                                          .projected = (flags & gpu_value::kTextureTransformProjected) != 0};
    }

    FragmentKey& fragment = key.fragment;
    if (program != nullptr) {
        apply_program(*program, &fragment);
    }
    fragment.alpha_test = state(block, GpuRenderState::kAlphaTestEnable) != 0
                              ? static_cast<CompareFunc>(state(block, GpuRenderState::kAlphaFunc) & kByte)
                              : CompareFunc::kOff;
    fragment.fog = fog;
    fragment.table_fog = table_fog;
    fragment.specular_add = fragment.program == PixelProgram::kStages && vertex.specular;
    fragment.point_sprite = state(block, GpuRenderState::kPointSpriteEnable) != 0;
    for (std::size_t index = 0; index < kStageCount; ++index) {
        const auto arg = [&block, index](GpuStageState which) {
            return static_cast<uint8_t>(stage(block, index, which) & kByte);
        };
        core::at(fragment.stages, index) = {
            .colour_op = static_cast<StageOp>(arg(GpuStageState::kColourOp)),
            .colour_arg0 = arg(GpuStageState::kColourArg0),
            .colour_arg1 = arg(GpuStageState::kColourArg1),
            .colour_arg2 = arg(GpuStageState::kColourArg2),
            .alpha_op = static_cast<StageOp>(arg(GpuStageState::kAlphaOp)),
            .alpha_arg0 = arg(GpuStageState::kAlphaArg0),
            .alpha_arg1 = arg(GpuStageState::kAlphaArg1),
            .alpha_arg2 = arg(GpuStageState::kAlphaArg2),
        };
    }
    // A stage that reads its texture with none set is taken as disabled, and the stages after it with it, as Direct3D 9
    // draws it (seen with the loading bar: stage 0 selects its texture, none is set, and the bar is drawn in its
    // vertices' colours - not in the blank texture's white).
    if (fragment.program == PixelProgram::kStages) {
        const uint32_t sampled = sampled_stages(fragment);
        for (std::size_t index = 0; index < kStageCount; ++index) {
            if ((sampled & (1U << index)) != 0U && core::at(block.textures, index) == nullptr) {
                StageKey& off = core::at(fragment.stages, index);
                off.colour_op = StageOp::kDisable;
                off.alpha_op = StageOp::kDisable;
                break;
            }
        }
    }
    return normalised(key);
}

void fill_vertex_uniforms(const GpuStateBlock& block, VertexUniforms* uniforms)
{
    const Float4x4& world = core::at(block.transforms, kWorldSlot);
    const Float4x4& view = core::at(block.transforms, kViewSlot);
    uniforms->world_view = multiply(world, view);
    uniforms->projection = core::at(block.transforms, kProjectionSlot);
    uniforms->normal_matrix = normal_matrix(uniforms->world_view);
    for (std::size_t index = 0; index < kStageCount; ++index) {
        core::at(uniforms->texture_matrix, index) = core::at(block.transforms, kFirstStageSlot + index);
    }
    const GpuViewport& viewport = block.viewport;
    uniforms->pixel = {viewport.width != 0 ? 1.0F / static_cast<float>(viewport.width) : 0.0F,
                       viewport.height != 0 ? 1.0F / static_cast<float>(viewport.height) : 0.0F,
                       static_cast<float>(viewport.x), static_cast<float>(viewport.y)};
    uniforms->ambient = colour_of(state(block, GpuRenderState::kAmbient));
    uniforms->material_diffuse = as_float4(block.material.diffuse);
    uniforms->material_ambient = as_float4(block.material.ambient);
    uniforms->material_specular = as_float4(block.material.specular);
    uniforms->material_emissive = as_float4(block.material.emissive);
    uniforms->material_power = {block.material.power, 0.0F, 0.0F, 0.0F};
    uniforms->fog = {float_state(block, GpuRenderState::kFogStart), float_state(block, GpuRenderState::kFogEnd),
                     float_state(block, GpuRenderState::kFogDensity), 0.0F};
    uniforms->point = {float_state(block, GpuRenderState::kPointSize), float_state(block, GpuRenderState::kPointSizeMin),
                       float_state(block, GpuRenderState::kPointSizeMax), static_cast<float>(viewport.height)};
    uniforms->point_scale = {float_state(block, GpuRenderState::kPointScaleA),
                             float_state(block, GpuRenderState::kPointScaleB),
                             float_state(block, GpuRenderState::kPointScaleC), 0.0F};
    // the enabled lights in order, their directions into camera space (the view's 3x3: row vectors)
    std::size_t lit = 0;
    for (std::size_t index = 0; index < kGpuLightCount && lit < kMaxLights; ++index) {
        if ((block.lights_enabled & (1U << index)) == 0U) {
            continue;
        }
        const GpuLight& light = core::at(block.lights, index);
        const std::array<float, 3>& from = light.direction;
        Float4 direction{};
        for (std::size_t column = 0; column < 3; ++column) {
            float sum = 0.0F;
            for (std::size_t row = 0; row < 3; ++row) {
                sum += core::at(from, row) * at(view, row, column);
            }
            core::at(direction, column) = sum;
        }
        core::at(uniforms->light_direction, lit) = direction;
        core::at(uniforms->light_diffuse, lit) = as_float4(light.diffuse);
        core::at(uniforms->light_specular, lit) = as_float4(light.specular);
        core::at(uniforms->light_ambient, lit) = as_float4(light.ambient);
        ++lit;
    }
    for (; lit < kMaxLights; ++lit) {
        core::at(uniforms->light_direction, lit) = {};
        core::at(uniforms->light_diffuse, lit) = {};
        core::at(uniforms->light_specular, lit) = {};
        core::at(uniforms->light_ambient, lit) = {};
    }
}

void fill_fragment_uniforms(const GpuStateBlock& block, FragmentUniforms* uniforms)
{
    for (std::size_t index = 0; index < kPixelShaderConstants; ++index) {
        core::at(uniforms->constant, index) = core::at(block.pixel_constants, index);
    }
    uniforms->texture_factor = colour_of(state(block, GpuRenderState::kTextureFactor));
    uniforms->fog_colour = colour_of(state(block, GpuRenderState::kFogColour));
    uniforms->fog = {float_state(block, GpuRenderState::kFogStart), float_state(block, GpuRenderState::kFogEnd),
                     float_state(block, GpuRenderState::kFogDensity), 0.0F};
    uniforms->alpha_reference = {static_cast<float>(state(block, GpuRenderState::kAlphaRef) & kByte), 0.0F, 0.0F, 0.0F};
}

VertexLayout vertex_layout(uint32_t fvf)
{
    VertexLayout layout;
    const auto add = [&layout](uint32_t location, VkFormat format, uint32_t bytes) {
        core::at(layout.attributes, layout.count++) = {
            .location = location, .binding = 0, .format = format, .offset = layout.bytes};
        layout.bytes += bytes;
    };
    const uint32_t position = fvf & kFvfPositionMask;
    if (position == kFvfXyzRhw) {
        add(kPositionLocation, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * kFloatBytes);
    } else {
        add(kPositionLocation, VK_FORMAT_R32G32B32_SFLOAT, 3 * kFloatBytes);
        if (position >= kFvfXyzB1 && position <= kFvfXyzB5) {
            layout.bytes += (((position - kFvfXyzB1) / 2) + 1) * kFloatBytes; // (blend weights: not read)
        }
    }
    if ((fvf & kFvfNormal) != 0U) {
        add(kNormalLocation, VK_FORMAT_R32G32B32_SFLOAT, 3 * kFloatBytes);
    }
    if ((fvf & kFvfPointSize) != 0U) {
        layout.bytes += kFloatBytes; // (a point size per vertex: not read)
    }
    if ((fvf & kFvfDiffuse) != 0U) {
        add(kDiffuseLocation, VK_FORMAT_B8G8R8A8_UNORM, kColourBytes);
    }
    if ((fvf & kFvfSpecular) != 0U) {
        add(kSpecularLocation, VK_FORMAT_B8G8R8A8_UNORM, kColourBytes);
    }
    for (uint32_t set = 0; set < fvf_tex_count(fvf) && set < kMaxTexSets; ++set) {
        const uint32_t size = fvf_tex_size(fvf, set);
        add(kFirstTexSetLocation + set, core::at(kFloatFormats, size - 1), size * kFloatBytes);
    }
    return layout;
}

VkBlendFactor blend_factor(uint32_t blend)
{
    switch (blend) {
    case gpu_value::kBlendZero:
        return VK_BLEND_FACTOR_ZERO;
    case gpu_value::kBlendOne:
        return VK_BLEND_FACTOR_ONE;
    case gpu_value::kBlendSrcColour:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case kBlendInvSrcColour:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case gpu_value::kBlendSrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case gpu_value::kBlendInvSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case kBlendDestAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case kBlendInvDestAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case gpu_value::kBlendDestColour:
        return VK_BLEND_FACTOR_DST_COLOR;
    case kBlendInvDestColour:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case kBlendSrcAlphaSaturate:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case gpu_value::kBlendFactor:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case gpu_value::kBlendInvFactor:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp blend_op(uint32_t op)
{
    switch (op) {
    case kBlendOpSubtract:
        return VK_BLEND_OP_SUBTRACT;
    case gpu_value::kBlendOpRevSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case kBlendOpMin:
        return VK_BLEND_OP_MIN;
    case kBlendOpMax:
        return VK_BLEND_OP_MAX;
    default:
        return VK_BLEND_OP_ADD;
    }
}

// D3DCMP_NEVER (1) - ALWAYS (8) are Vulkan's NEVER (0) - ALWAYS (7).
VkCompareOp compare_op(uint32_t func)
{
    if (func < gpu_value::kCompareNever || func > gpu_value::kCompareAlways) {
        return VK_COMPARE_OP_ALWAYS;
    }
    return static_cast<VkCompareOp>(func - gpu_value::kCompareNever);
}

// Direct3D 9's front faces are clockwise on the screen, as the pipelines' are (VK_FRONT_FACE_CLOCKWISE in the same
// framebuffer coordinates): D3DCULL_CCW culls the back faces, D3DCULL_CW the front ones.
VkCullModeFlags cull_mode(uint32_t cull)
{
    switch (cull) {
    case gpu_value::kCullCounterClockwise:
        return VK_CULL_MODE_BACK_BIT;
    case gpu_value::kCullClockwise:
        return VK_CULL_MODE_FRONT_BIT;
    default:
        return VK_CULL_MODE_NONE;
    }
}

VkPrimitiveTopology topology(GpuPrimitive primitive)
{
    switch (primitive) {
    case GpuPrimitive::kPointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case GpuPrimitive::kLineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case GpuPrimitive::kLineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case GpuPrimitive::kTriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case GpuPrimitive::kTriangleFan:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case GpuPrimitive::kTriangleList:
        break;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

Float4 colour_of(uint32_t argb)
{
    const auto channel = [argb](uint32_t shift) { return static_cast<float>((argb >> shift) & kByte) / kByteScale; };
    constexpr uint32_t kRed = 16;
    constexpr uint32_t kGreen = 8;
    constexpr uint32_t kAlpha = 24;
    return {channel(kRed), channel(kGreen), channel(0), channel(kAlpha)};
}

} // namespace render::vulkan
