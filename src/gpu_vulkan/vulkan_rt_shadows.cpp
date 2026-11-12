// NOT ORIGINAL - ray-traced sun shadows (vulkan_rt_shadows.h; docs/design/vulkan.md, R4).
#include "gpu_vulkan/vulkan_rt_shadows.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52340001U;
constexpr uint64_t kReportNoScene = 0x52340002U;
constexpr uint64_t kReportNoLight = 0x52340003U;
constexpr uint64_t kReportTargets = 0x52340004U;
constexpr uint64_t kReportPipeline = 0x52340005U;
constexpr uint64_t kReportFirstPass = 0x52340006U;

constexpr float kMaxStrength = 1.0F;
constexpr float kMaxSoftnessDegrees = 5.0F;
constexpr unsigned kMaxRays = 16;
constexpr float kRayLength = 1.0e6F;   // (world units: further than any level's far plane)
constexpr float kOffsetScale = 1.0F;   // the ray's start pushed off the surface by this much, growing with distance
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kPushConstantBytes = 128; // (the space every Vulkan device has)
constexpr double kSingular = 1e-300;            // a pivot this small: the matrix can't be inverted
constexpr double kShortest = 1e-6;              // a light direction shorter than this: none

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The fragment shader: written to the colour target with a multiplying blend (colour x the output's rgb). PER_SAMPLE
// and MULTISAMPLED are defined before it as the variant needs.
constexpr const char* kFragmentBody = R"(
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(push_constant) uniform Pass {
    mat4 to_world;  // (pixel x, pixel y, depth, 1) -> world, homogeneous
    ivec4 rect;     // the viewport: x, y, width, height
    vec4 to_light;  // xyz: toward the light (normalised); w: the strength
    vec4 shape;     // x: tan of the light's angular radius; y: rays; z: the ray's length; w: the offset's scale
    vec4 camera;    // xyz: the camera's position; w: the instance mask's bits
} pc;
layout(location = 0) out vec4 o_factor;

float depth_at(ivec2 p)
{
#if defined(PER_SAMPLE)
    return texelFetch(u_depth, p, gl_SampleID).r;
#else
    return texelFetch(u_depth, p, 0).r;
#endif
}

vec3 world_at(ivec2 p, float d)
{
    vec4 w = pc.to_world * vec4(vec2(p), d, 1.0);
    return w.xyz / w.w;
}

// The surface's step to the next pixel along `step`: toward the neighbour whose depth is nearer this pixel's (so a
// silhouette's other side isn't taken), zero when neither neighbour is inside the viewport.
vec3 surface_step(ivec2 p, float d, vec3 here, ivec2 step)
{
    ivec2 low = pc.rect.xy;
    ivec2 high = pc.rect.xy + pc.rect.zw - ivec2(1);
    ivec2 ahead = p + step;
    ivec2 behind = p - step;
    bool ahead_in = all(greaterThanEqual(ahead, low)) && all(lessThanEqual(ahead, high));
    bool behind_in = all(greaterThanEqual(behind, low)) && all(lessThanEqual(behind, high));
    float d_ahead = ahead_in ? depth_at(ahead) : 2.0;
    float d_behind = behind_in ? depth_at(behind) : 2.0;
    if (!ahead_in && !behind_in) {
        return vec3(0.0);
    }
    if (ahead_in && (!behind_in || abs(d_ahead - d) <= abs(d_behind - d))) {
        return world_at(ahead, d_ahead) - here;
    }
    return here - world_at(behind, d_behind);
}

float blocked(vec3 origin, vec3 direction)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          floatBitsToUint(pc.camera.w) & 0xFFu, origin, 0.0, direction, pc.shape.z);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.0 : 1.0;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = depth_at(p);
    if (d >= 1.0) {
        o_factor = vec4(1.0); // (the sky, or nothing drawn)
        return;
    }
    vec3 here = world_at(p, d);
    vec3 to_camera = pc.camera.xyz - here;
    float distance = length(to_camera);
    vec3 normal = cross(surface_step(p, d, here, ivec2(1, 0)), surface_step(p, d, here, ivec2(0, 1)));
    float normal_length = length(normal);
    normal = normal_length > 1e-20 ? normal / normal_length : to_camera / max(distance, 1e-6);
    if (dot(normal, to_camera) < 0.0) {
        normal = -normal;
    }
    vec3 light = pc.to_light.xyz;
    float occlusion = 1.0; // (facing away from the light: in its own shadow)
    if (dot(normal, light) > 0.0) {
        // pushed off the surface by more as the depth's precision falls with distance
        float offset = pc.shape.w * (1.0 + distance * 0.0005 + distance * distance * 6e-8);
        vec3 origin = here + normal * offset;
        uint rays = uint(pc.shape.y);
        if (rays <= 1u || pc.shape.x <= 0.0) {
            occlusion = blocked(origin, light);
        } else {
            vec3 across = normalize(abs(light.y) < 0.99 ? cross(light, vec3(0.0, 1.0, 0.0))
                                                         : cross(light, vec3(1.0, 0.0, 0.0)));
            vec3 up = cross(light, across);
            // (interleaved gradient noise turns each pixel's spiral of directions over the light's disc)
            float turn = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
            float sum = 0.0;
            for (uint i = 0u; i < rays; ++i) {
                float radius = sqrt((float(i) + 0.5) / float(rays)) * pc.shape.x;
                float angle = float(i) * 2.39996323 + turn;
                sum += blocked(origin, normalize(light + (across * cos(angle) + up * sin(angle)) * radius));
            }
            occlusion = sum / float(rays);
        }
    }
    o_factor = vec4(vec3(1.0 - pc.to_light.w * occlusion), 1.0);
}
)";

// The push constants, as the fragment shader's block lays them out.
struct PassConstants {
    std::array<float, kMatrixFloats> to_world{}; // column-major (GLSL's mat4)
    std::array<int32_t, 4> rect{};
    std::array<float, 4> to_light{};
    std::array<float, 4> shape{};
    std::array<float, 4> camera{};
};
static_assert(sizeof(PassConstants) == kPushConstantBytes,"Vulkan's guaranteed push-constant space");

// A pipeline for a colour format and sample count.
struct PassPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool per_sample = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct ShadowState {
    RtShadowSettings settings;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    bool have_light = false;
    std::array<float, 3> to_light{}; // the last directional light seen (toward it)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::vector<PassPipeline> pipelines;
    bool failed = false; // the shaders or layouts couldn't be made (not tried again)
    uint64_t passes = 0;
};

ShadowState& shadows()
{
    static ShadowState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's opaque world is drawn.
class ShadowListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kShadows; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_shadow_pass(state, view);
        }
    }
};

ShadowListener& listener()
{
    static ShadowListener instance;
    return instance;
}

VkShaderModule make_module(VkDevice device, RtShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(stage, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                        .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    return module;
}

// The layouts, the sampler and the vertex shader (once).
bool ensure_layouts(Backend& state, std::string* error)
{
    ShadowState& own = shadows();
    if (own.layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .pImmutableSamplers = nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PassConstants)};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
        *error = "the descriptor layout or the sampler";
        return false;
    }
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

// The pipeline for a colour target (made on first use).
VkPipeline pass_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    ShadowState& own = shadows();
    const bool multisampled = samples != VK_SAMPLE_COUNT_1_BIT;
    const bool per_sample = multisampled && state.context.enabled_features().sampleRateShading == VK_TRUE;
    for (const PassPipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples && made.per_sample == per_sample) {
            return made.pipeline;
        }
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    if (per_sample) {
        source += "#define PER_SAMPLE\n";
    }
    source += kFragmentBody;
    const VkShaderModule fragment = make_module(device, RtShaderStage::kFragment, source, error);
    if (fragment == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = own.vertex,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = samples,
        .sampleShadingEnable = per_sample ? VK_TRUE : VK_FALSE,
        .minSampleShading = 1.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // colour x the factor: (dst x src) + (dst x 0); alpha kept
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = own.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.pipelines.push_back(
        {.format = format, .samples = samples, .per_sample = per_sample, .pipeline = pipeline});
    return pipeline;
}

// The image a surface draws into (a texture's level 0, a render target's, a depth surface's).
Image* image_of_surface(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        return &surface->image;
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

// The inverse of a 4x4 matrix (Gauss-Jordan with partial pivoting); false when it is singular.
bool invert(Matrix matrix, Matrix* inverse)
{
    Matrix result{};
    for (std::size_t i = 0; i < kMatrixSide; ++i) {
        core::at(core::at(result, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kMatrixSide; ++row) {
            if (std::abs(core::at(core::at(matrix, row), column)) > std::abs(core::at(core::at(matrix, pivot), column))) {
                pivot = row;
            }
        }
        const double top = core::at(core::at(matrix, pivot), column);
        if (std::abs(top) < kSingular) {
            return false;
        }
        std::swap(core::at(matrix, pivot), core::at(matrix, column));
        std::swap(core::at(result, pivot), core::at(result, column));
        for (std::size_t k = 0; k < kMatrixSide; ++k) {
            core::at(core::at(matrix, column), k) /= top;
            core::at(core::at(result, column), k) /= top;
        }
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(matrix, row), column);
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                core::at(core::at(matrix, row), k) -= factor * core::at(core::at(matrix, column), k);
                core::at(core::at(result, row), k) -= factor * core::at(core::at(result, column), k);
            }
        }
    }
    *inverse = result;
    return true;
}

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix product{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(product, row), column) = sum;
        }
    }
    return product;
}

// The matrix (column vectors) that takes (pixel x, pixel y, depth, 1) to world space: Direct3D 9's pixel centres are
// at whole coordinates (the Gpu's draws shift Vulkan's half a pixel to match), y down; the viewport's depth range
// mapped back to 0 - 1; then the inverse of view x projection (their product transposed: Direct3D's row vectors).
bool pixel_to_world(const rt::ViewDrawn& view, Matrix* out)
{
    Matrix view_projection{}; // row vectors: clip = world x view x projection
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(view.camera.view, row), k)) *
                       static_cast<double>(core::at(core::at(view.camera.projection, k), column));
            }
            core::at(core::at(view_projection, row), column) = sum;
        }
    }
    Matrix clip_from_world{}; // column vectors
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(clip_from_world, row), column) = core::at(core::at(view_projection, column), row);
        }
    }
    Matrix world_from_clip{};
    if (!invert(clip_from_world, &world_from_clip)) {
        return false;
    }
    const double x = core::at(view.rect, 0);
    const double y = core::at(view.rect, 1);
    const double width = std::max(1U, core::at(view.rect, 2));
    const double height = std::max(1U, core::at(view.rect, 3));
    const double near_z = core::at(view.depth_range, 0);
    const double depth_span = std::max(1e-6, static_cast<double>(core::at(view.depth_range, 1)) - near_z);
    const Matrix clip_from_pixel{{{2.0 / width, 0.0, 0.0, (-2.0 * x / width) - 1.0},
                                  {0.0, -2.0 / height, 0.0, (2.0 * y / height) + 1.0},
                                  {0.0, 0.0, 1.0 / depth_span, -near_z / depth_span},
                                  {0.0, 0.0, 0.0, 1.0}}};
    *out = multiply(world_from_clip, clip_from_pixel);
    return true;
}

// The frame's directional light (toward it), or the last one seen.
bool light_direction(std::array<float, 3>* to_light)
{
    ShadowState& own = shadows();
    for (const rt::Light& light : rt::frame_record().lights) {
        if (light.kind != rt::Light::Kind::kDirectional) {
            continue;
        }
        const double length = std::hypot(static_cast<double>(core::at(light.direction, 0)),
                                         static_cast<double>(core::at(light.direction, 1)),
                                         static_cast<double>(core::at(light.direction, 2)));
        if (!(length > kShortest) || !std::isfinite(length)) {
            continue;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            core::at(own.to_light, i) = static_cast<float>(-static_cast<double>(core::at(light.direction, i)) / length);
        }
        own.have_light = true;
        break;
    }
    *to_light = own.to_light;
    return own.have_light;
}

} // namespace

void set_rt_shadow_settings(const RtShadowSettings& settings)
{
    RtShadowSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.softness = std::isfinite(kept.softness) ? std::clamp(kept.softness, 0.0F, kMaxSoftnessDegrees) : 0.0F;
    kept.rays = std::clamp(kept.rays, 1U, kMaxRays);
    {
        const std::scoped_lock lock(backend().mutex);
        shadows().settings = kept;
    }
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtShadowSettings rt_shadow_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return shadows().settings;
}

void rt_shadows_set_scene(VkAccelerationStructureKHR tlas)
{
    const std::scoped_lock lock(backend().mutex);
    rt_shadows_set_scene_locked(tlas);
}

void rt_shadows_set_scene_locked(VkAccelerationStructureKHR tlas) { shadows().scene = tlas; }

VkImageUsageFlags rt_shadow_depth_usage(const Backend& state, VkFormat format)
{
    if (!shadows().settings.on || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_shadow_pass(Backend& state, const rt::ViewDrawn& view)
{
    ShadowState& own = shadows();
    if (!own.settings.on || own.failed) {
        return false;
    }
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing)) {
            get_log("vulkan: rt_shadows=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device with "
                    "VK_KHR_ray_query) - no ray-traced shadows");
        }
        return false;
    }
    if (own.scene == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene)) {
            get_log("vulkan: rt_shadows: no acceleration structure for the scene yet - the shadow pass skipped");
        }
        return false;
    }
    std::array<float, 3> to_light{};
    if (!light_direction(&to_light)) {
        if (first_report(state, kReportNoLight)) {
            get_log("vulkan: rt_shadows: no directional light in the frame record - the shadow pass skipped");
        }
        return false;
    }
    Image* const colour = image_of_surface(core::at(state.targets, 0));
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || depth == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE ||
        depth->samples != colour->samples || depth->extent.width < colour->extent.width ||
        depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportTargets)) {
            get_log("vulkan: rt_shadows: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the shadow pass skipped there");
        }
        return false;
    }
    std::string error;
    const VkPipeline pipeline = pass_pipeline(state, colour->format, colour->samples, &error);
    Matrix to_world{};
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_shadows: the pass couldn't be made (%s) - no ray-traced shadows", error.c_str());
        }
        return false;
    }
    if (!pixel_to_world(view, &to_world)) {
        return false;
    }
    // the viewport, inside the target
    const uint32_t left = std::min(core::at(view.rect, 0), colour->extent.width);
    const uint32_t top = std::min(core::at(view.rect, 1), colour->extent.height);
    const uint32_t width = std::min(core::at(view.rect, 2), colour->extent.width - left);
    const uint32_t height = std::min(core::at(view.rect, 3), colour->extent.height - top);
    if (width == 0 || height == 0) {
        return false;
    }

    PassConstants constants{};
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            core::at(constants.to_world, (column * kMatrixSide) + row) =
                static_cast<float>(core::at(core::at(to_world, row), column));
        }
    }
    constants.rect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width),
                      static_cast<int32_t>(height)};
    constants.to_light = {core::at(to_light, 0), core::at(to_light, 1), core::at(to_light, 2), own.settings.strength};
    const bool soft = own.settings.softness > 0.0F && own.settings.rays > 1;
    constants.shape = {
        static_cast<float>(std::tan(static_cast<double>(own.settings.softness) * kDegreesToRadians)),
        static_cast<float>(soft ? own.settings.rays : 1U), kRayLength, kOffsetScale};
    const uint32_t mask = kMaskWorld | (own.settings.objects ? kMaskObjects : 0U);
    constants.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                        core::at(view.camera.position, 2),
                        std::bit_cast<float>(mask)};

    VkCommandBuffer commands = outside_pass(state);
    // the scene's build (recorded earlier on this queue) before the rays read it
    const VkMemoryBarrier built{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &built, 0, nullptr, 0, nullptr);
    Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top)},
                        .extent = {.width = width, .height = height}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(left),
                              .y = static_cast<float>(top),
                              .width = static_cast<float>(width),
                              .height = static_cast<float>(height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    const VkWriteDescriptorSetAccelerationStructureKHR scene{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &own.scene};
    const VkDescriptorImageInfo depth_image{.sampler = own.sampler,
                                            .imageView = depth->view,
                                            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    VkWriteDescriptorSet& scene_write = writes.front();
    scene_write.pNext = &scene;
    scene_write.dstBinding = 0;
    scene_write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkWriteDescriptorSet& depth_write = writes.back();
    depth_write.dstBinding = 1;
    depth_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depth_write.pImageInfo = &depth_image;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    // the targets back where the Gpu's passes expect them; its bound state forgotten (pipeline, descriptors, dynamics)
    for (Image* image : {colour, depth}) {
        if (image->layout != image->resting) {
            Memory::transition(commands, image, image->resting);
        }
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        const char* traced = "1 sample";
        if (colour->samples != VK_SAMPLE_COUNT_1_BIT) {
            traced = state.context.enabled_features().sampleRateShading == VK_TRUE ? "per sample" : "per pixel";
        }
        get_log("vulkan: rt_shadows: the first shadow pass (%ux%u, %s, strength %.2f, softness %.2f deg x %u rays%s)",
                width, height, traced,
                static_cast<double>(own.settings.strength), static_cast<double>(own.settings.softness),
                soft ? own.settings.rays : 1U, own.settings.objects ? ", objects cast" : "");
    }
    return true;
}

uint64_t rt_shadow_passes() { return shadows().passes; }

void release_rt_shadows(Backend& state)
{
    ShadowState& own = shadows();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const PassPipeline& made : own.pipelines) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        if (own.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, own.vertex, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.failed = false;
}

} // namespace render::vulkan
