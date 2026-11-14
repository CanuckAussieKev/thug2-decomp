// NOT ORIGINAL - ray-traced sun shadows (vulkan_rt_shadows.h; docs/design/vulkan.md, R4).
#include "gpu_vulkan/vulkan_rt_shadows.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52340001U;
constexpr uint64_t kReportNoScene = 0x52340002U;
constexpr uint64_t kReportTargets = 0x52340004U;
constexpr uint64_t kReportPipeline = 0x52340005U;
constexpr uint64_t kReportFirstPass = 0x52340006U;
constexpr uint64_t kReportAlphaPipeline = 0x52340007U;

constexpr float kMaxStrength = 1.0F;
constexpr float kMaxSoftnessDegrees = 5.0F;
constexpr unsigned kMaxRays = 16;
constexpr float kRayLength = 1.0e6F;   // (world units: further than any level's far plane)
constexpr unsigned kMaxSkyRays = 16;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kPushConstantBytes = 128; // (the space every Vulkan device has)
constexpr uint32_t kBindingGeometry = 3;        // (added for the alpha-tested casters) the frame's geometry table
constexpr uint32_t kMaterialSet = 1;
constexpr double kSingular = 1e-300;            // a pivot this small: the matrix can't be inverted

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
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
// Where the level's bake agrees with the traced shadows (engine/gfx/rt_sun.h make_agreement): a grid over x and z.
layout(std430, set = 0, binding = 2) readonly buffer Agreement {
    vec4 area;  // x, z: the grid's corner; z: 1 / a cell's side; w: 1 when there is a grid
    uvec4 size; // columns, rows
    float cells[];
} u_agreement;
layout(push_constant) uniform Pass {
    mat4 to_world;  // (pixel x, pixel y, depth, 1) -> world, homogeneous
    ivec4 rect;     // the viewport: x, y, width, height
    vec4 to_light;  // xyz: toward the light (normalised); w: the strength
    vec4 shape;     // x: tan of the light's angular radius; y: rays; z: the ray's length; w: the sky test's rays (0: off)
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

// The agreement at a point (rt_sun.cpp agreement_at: bilinear between the cells' centres, clamped at the edges).
float agreement_at(vec3 p)
{
    if (u_agreement.area.w == 0.0) {
        return 1.0;
    }
    vec2 g = (p.xz - u_agreement.area.xy) * u_agreement.area.z - 0.5;
    vec2 f = g - floor(g);
    ivec2 last = ivec2(u_agreement.size.xy) - 1;
    ivec2 a = clamp(ivec2(floor(g)), ivec2(0), last);
    ivec2 b = clamp(ivec2(floor(g)) + 1, ivec2(0), last);
    int columns = int(u_agreement.size.x);
    float near = mix(u_agreement.cells[a.y * columns + a.x], u_agreement.cells[a.y * columns + b.x], f.x);
    float far = mix(u_agreement.cells[b.y * columns + a.x], u_agreement.cells[b.y * columns + b.x], f.x);
    return mix(near, far, f.y);
}

const float OFFSET_SCALE = 1.0; // the ray's start pushed off the surface by this much, growing with distance
const float SKY_TILT = 0.6;     // the sky test's slanted rays: this far out from straight up per unit up (about 31 deg)

const uint OBJECTS_MASK = 0x02u; // (kMaskObjects: the skater, peds, vehicles)

float blocked_by(uint mask, vec3 origin, vec3 direction)
{
    if (mask == 0u) {
        return 0.0;
    }
    rayQueryEXT query;
#ifdef ALPHA_TESTED
    // (added for the alpha-tested casters - a cannon's spoked wheels, railings, foliage: instance mask 0x04, their
    // instances not forced opaque) each such candidate is kept only where its material's alpha test passes at the hit
    // (vulkan_rt_materials.h: its texel x its colour against its alpha reference, as the game's draw tests it); the
    // opaque instances end the ray as before
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0, direction, pc.shape.z);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false);
            HitVertices v;
            bool keep = true;
            if (hit_vertices(rayQueryGetIntersectionInstanceIdEXT(query, false),
                             rayQueryGetIntersectionPrimitiveIndexEXT(query, false), v)) {
                keep = material_alpha_passes_at(id, hit_uv(v, rayQueryGetIntersectionBarycentricsEXT(query, false)));
            }
            if (keep) {
                rayQueryConfirmIntersectionEXT(query);
            }
        }
    }
#else
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0,
                          direction, pc.shape.z);
    while (rayQueryProceedEXT(query)) {
    }
#endif
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.0 : 1.0;
}

// (added for the skater's shadow) Whether the pixel shows an object - a ray from the camera through it against the
// objects alone ends where the depth does.
bool on_object(vec3 here, vec3 to_camera, float distance)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, OBJECTS_MASK, pc.camera.xyz, 0.0,
                          -to_camera / max(distance, 1e-6), distance * 1.02 + 2.0);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT &&
           rayQueryGetIntersectionTEXT(query, true) >= distance - (2.0 + 0.02 * distance);
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
    uint mask = floatBitsToUint(pc.camera.w) & 0xFFu;
    // (added for the skater's shadow) The world's casters and the objects' are traced apart: the objects' shadows
    // (the skater's, the peds') are kept whole - not left out under roofs or where the bake disagrees, which are about
    // the level's own shadows (the open ground has no baked shadow, so its agreement dropped the skater's too); and an
    // object casts onto the world, not onto itself or another object (its low-poly body traced against itself drew
    // blotches over the skater) - the objects receive the world's shadows only.
    uint world_mask = mask & ~OBJECTS_MASK;
    uint objects_mask = mask & OBJECTS_MASK;
    bool object_pixel = on_object(here, to_camera, distance);
    if (object_pixel) {
        objects_mask = 0u;
    }
    vec3 normal = cross(surface_step(p, d, here, ivec2(1, 0)), surface_step(p, d, here, ivec2(0, 1)));
    float normal_length = length(normal);
    normal = normal_length > 1e-20 ? normal / normal_length : to_camera / max(distance, 1e-6);
    if (dot(normal, to_camera) < 0.0) {
        normal = -normal;
    }
    vec3 light = pc.to_light.xyz;
    float occlusion = 1.0; // (facing away from the light: in its own shadow)
    float cast_by_objects = 0.0;
    // (added for the skater's look) An object's pixel is never put in its own shadow by facing away from the light:
    // the depth's normal is its triangle's, flat, so the test shaded the skater's smooth body triangle by triangle
    // (dark facets over the shirt and arms); the game's own lighting already darkens its side away from the lights.
    // It takes the world's shadows only, its ray started off the surface toward the light.
    if (object_pixel || dot(normal, light) > 0.0) {
        // pushed off the surface by more as the depth's precision falls with distance
        float offset = OFFSET_SCALE * (1.0 + distance * 0.0005 + distance * distance * 6e-8);
        vec3 origin = here + normal * offset;
        uint rays = uint(pc.shape.y);
        if (rays <= 1u || pc.shape.x <= 0.0) {
            occlusion = blocked_by(world_mask, origin, light);
            cast_by_objects = blocked_by(objects_mask, origin, light);
        } else {
            vec3 across = normalize(abs(light.y) < 0.99 ? cross(light, vec3(0.0, 1.0, 0.0))
                                                         : cross(light, vec3(1.0, 0.0, 0.0)));
            vec3 up = cross(light, across);
            // (interleaved gradient noise turns each pixel's spiral of directions over the light's disc)
            float turn = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
            float sum = 0.0;
            float objects_sum = 0.0;
            for (uint i = 0u; i < rays; ++i) {
                float radius = sqrt((float(i) + 0.5) / float(rays)) * pc.shape.x;
                float angle = float(i) * 2.39996323 + turn;
                vec3 toward = normalize(light + (across * cos(angle) + up * sin(angle)) * radius);
                sum += blocked_by(world_mask, origin, toward);
                objects_sum += blocked_by(objects_mask, origin, toward);
            }
            occlusion = sum / float(rays);
            cast_by_objects = objects_sum / float(rays);
        }
    }
    // The sky test: a point under a roof (none of the rays up - straight up, and slanted round it - reaches the sky)
    // is lit by the level's own lamps, which the original baked in: no sun shadow there. Each ray seeing the sky lets
    // in a share of the shadow, all of it from half the rays on.
    uint sky_rays = uint(pc.shape.w);
    if (occlusion > 0.0 && sky_rays > 0u) {
        float offset = OFFSET_SCALE * (1.0 + distance * 0.0005 + distance * distance * 6e-8);
        vec3 origin = here + normal * offset;
        float open = 1.0 - blocked_by(world_mask, origin, vec3(0.0, 1.0, 0.0));
        float turn = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.00583715, 0.06711056))));
        for (uint i = 1u; i < sky_rays; ++i) {
            float angle = turn + 6.2831853 * float(i) / float(sky_rays - 1u);
            open += 1.0 - blocked_by(world_mask, origin, normalize(vec3(cos(angle) * SKY_TILT, 1.0, sin(angle) * SKY_TILT)));
        }
        occlusion *= clamp(2.0 * open / float(sky_rays), 0.0, 1.0);
    }
    if (occlusion > 0.0) {
        occlusion *= agreement_at(here);
    }
    occlusion = max(occlusion, cast_by_objects);
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

// The agreement buffer's header, as the shader's block lays it out; the cells follow.
struct AgreementHeader {
    std::array<float, 4> area{};
    std::array<uint32_t, 4> size{};
};
constexpr VkDeviceSize kAgreementBytes =
    sizeof(AgreementHeader) + (sizeof(float) * std::size_t{rt::kAgreementSide} * rt::kAgreementSide);
constexpr uint64_t kNoAgreement = ~uint64_t{0};

// A pipeline for a colour format and sample count.
struct PassPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool per_sample = false;
    bool alpha = false; // (added for the alpha-tested casters) the ALPHA_TESTED variant: alpha_layout's
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct ShadowState {
    RtShadowSettings settings;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    // (added for the alpha-tested casters) set 0 with the frame's geometry table at binding 3, and the material table's
    // set 1 (its layout when made: none while the device has no table - the pass then traces the opaque casters alone)
    VkDescriptorSetLayout alpha_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout alpha_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    DeviceBuffer agreement;                  // the bake's agreement (update_agreement)
    uint64_t agreement_scene = ~uint64_t{0}; // the grid it holds (its BakedSun::scene; 0: none - the header says so)
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
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
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
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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
    // (added for the alpha-tested casters) the same + the geometry table, and the material table's set
    own.material_layout = state.materials.layout();
    if (own.material_layout != VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 4> alpha_bindings{};
        std::copy(bindings.begin(), bindings.end(), alpha_bindings.begin());
        alpha_bindings.back() = {.binding = kBindingGeometry,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 .descriptorCount = 1,
                                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                 .pImmutableSamplers = nullptr};
        const VkDescriptorSetLayoutCreateInfo alpha_set_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
            .bindingCount = static_cast<uint32_t>(alpha_bindings.size()),
            .pBindings = alpha_bindings.data()};
        if (vkCreateDescriptorSetLayout(device, &alpha_set_info, nullptr, &own.alpha_set_layout) != VK_SUCCESS) {
            own.alpha_set_layout = VK_NULL_HANDLE;
            own.material_layout = VK_NULL_HANDLE;
        } else {
            const std::array<VkDescriptorSetLayout, 2> both{own.alpha_set_layout, own.material_layout};
            const VkPipelineLayoutCreateInfo alpha_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .setLayoutCount = static_cast<uint32_t>(both.size()),
                                                        .pSetLayouts = both.data(),
                                                        .pushConstantRangeCount = 1,
                                                        .pPushConstantRanges = &constants};
            if (vkCreatePipelineLayout(device, &alpha_info, nullptr, &own.alpha_layout) != VK_SUCCESS) {
                own.alpha_layout = VK_NULL_HANDLE;
                own.material_layout = VK_NULL_HANDLE;
            }
        }
    }
    if (!state.memory.create_buffer(kAgreementBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload,
                                    &own.agreement) ||
        own.agreement.mapped == nullptr) {
        *error = "the agreement's buffer";
        return false;
    }
    own.agreement_scene = kNoAgreement;
    own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

// The agreement's buffer written when the grid for the sun traced toward (rt_scene.h agreement_for: the baked sun's, or
// the relit colours' for another scene sun) or match_bake changed: its header, then its cells (a grid of at most
// kAgreementSide x kAgreementSide). (Written while frames in flight may read it: at a level's start, or when the scene's
// sun moves - the frame or two that mix both are the sun's change anyway.)
void update_agreement(Backend& state, const std::array<float, 3>& to_light)
{
    ShadowState& own = shadows();
    const rt::BakedSun* grid = own.settings.match_bake ? rt::agreement_for(to_light) : nullptr;
    const bool used = grid != nullptr && !grid->agreement.empty() &&
                      grid->agreement.size() == static_cast<std::size_t>(grid->columns) * grid->rows &&
                      grid->columns <= rt::kAgreementSide && grid->rows <= rt::kAgreementSide;
    const uint64_t key = used ? grid->scene : 0U;
    if (key == own.agreement_scene) {
        return;
    }
    own.agreement_scene = key;
    AgreementHeader header{};
    const std::span<uint8_t> mapped(static_cast<uint8_t*>(own.agreement.mapped), kAgreementBytes);
    if (used) {
        header.area = {std::get<0>(grid->origin), std::get<1>(grid->origin), 1.0F / grid->cell, 1.0F};
        header.size = {grid->columns, grid->rows, 0U, 0U};
        std::memcpy(mapped.subspan(sizeof(header)).data(), grid->agreement.data(), grid->agreement.size() * sizeof(float));
    }
    std::memcpy(mapped.data(), &header, sizeof(header));
    state.memory.flush(own.agreement, 0, kAgreementBytes);
}

// The pipeline for a colour target (made on first use).
VkPipeline pass_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, bool alpha, std::string* error)
{
    ShadowState& own = shadows();
    const bool multisampled = samples != VK_SAMPLE_COUNT_1_BIT;
    const bool per_sample = multisampled && state.context.enabled_features().sampleRateShading == VK_TRUE;
    for (const PassPipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples && made.per_sample == per_sample && made.alpha == alpha) {
            return made.pipeline;
        }
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    if (alpha && own.alpha_layout == VK_NULL_HANDLE) {
        *error = "no layout for the alpha-tested casters";
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
    source += "#extension GL_EXT_ray_query : require\n";
    if (alpha) {
        // (added for the alpha-tested casters) the material table (set 1) and the frame's geometry table (binding 3)
        source += "#define ALPHA_TESTED\n#define THUG2_MATERIAL_SET 1\n";
        source += material_glsl();
        source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 3\n";
        source += scene_geometry_glsl();
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
    info.layout = alpha ? own.alpha_layout : own.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.pipelines.push_back(
        {.format = format, .samples = samples, .per_sample = per_sample, .alpha = alpha, .pipeline = pipeline});
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

// The frame record's directional lights logged when they change (toward each, its colour).
void log_lights()
{
    std::string text;
    for (const rt::Light& light : rt::frame_record().lights) {
        if (light.kind == rt::Light::Kind::kDirectional) {
            const auto [x, y, z] = light.direction;
            const auto [red, green, blue] = light.colour;
            text += text::format(" [toward %.3f %.3f %.3f colour %.2f %.2f %.2f]", -static_cast<double>(x),
                                 -static_cast<double>(y), -static_cast<double>(z), static_cast<double>(red),
                                 static_cast<double>(green), static_cast<double>(blue));
        }
    }
    static std::string last;
    if (!text.empty() && text != last) {
        last = text;
        get_log("vulkan: rt_shadows: the frame's directional lights:%s", text.c_str());
    }
}

// The sun the pass traces toward (changed by the scene's sun): rt_scene.h scene_sun - the console's `rt sun`, else
// [Graphics] rt_sun_source's (the game's sun where the level is relit by it, the baked sun, a fixed direction) - the one
// every effect lights by. Waiting: the level's baked sun not fitted yet; none: its bake shows no sun (indoors).
enum class Sun : uint8_t { kFound, kWaiting, kNoneBaked };
Sun sun_direction(std::array<float, 3>* to_light)
{
    log_lights();
    const rt::SceneSun sun = rt::scene_sun();
    rt::note_sun_used(rt::SunUser::kShadows, sun.found(), sun.toward);
    if (sun.found()) {
        *to_light = sun.toward;
        return Sun::kFound;
    }
    return sun.origin == rt::SunOrigin::kWaiting ? Sun::kWaiting : Sun::kNoneBaked;
}

} // namespace

void set_rt_shadow_settings(const RtShadowSettings& settings)
{
    RtShadowSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.softness = std::isfinite(kept.softness) ? std::clamp(kept.softness, 0.0F, kMaxSoftnessDegrees) : 0.0F;
    kept.rays = std::clamp(kept.rays, 1U, kMaxRays);
    kept.sky_rays = std::clamp(kept.sky_rays, 1U, kMaxSkyRays);
    {
        const std::scoped_lock lock(backend().mutex);
        shadows().settings = kept;
    }
    rt::set_baked_sun_wanted(kept.on); // (every scene sun but a fixed one needs the fit: an indoor level has none)
    if (!kept.on || !kept.objects) {
        rt::set_object_shadows_replaced(false); // (the objects' own shadows back)
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

namespace {
bool record_pass(Backend& state, const rt::ViewDrawn& view);
} // namespace

bool record_rt_shadow_pass(Backend& state, const rt::ViewDrawn& view)
{
    // (added by cel shading, vulkan_cel_draw.h: while its draws trace the sun's shadows themselves, none here)
    const bool drawn = !cel_owns_shadows(state) && record_pass(state, view);
    // (added for the skater's shadow) while this pass casts the objects' shadows, their own (the skater's projected
    // one, the peds' blobs) are hidden; a view without a pass gives them back - but not while the baked sun is only
    // being fitted again (the world changed: a second or two without a pass), which flashed the original's shadow
    // back between two of ours
    std::array<float, 3> toward{};
    const bool refitting = shadows().settings.on && sun_direction(&toward) == Sun::kWaiting;
    rt::set_object_shadows_replaced(shadows().settings.objects &&
                                    (drawn || (refitting && rt::object_shadows_replaced())));
    return drawn;
}

namespace {
bool record_pass(Backend& state, const rt::ViewDrawn& view)
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
    const Sun sun = sun_direction(&to_light);
    if (sun != Sun::kFound) {
        // (no baked sun fitted yet - a new world gets it in its first frame, before it is drawn -, or a level whose bake shows
        // none: indoors, lit by its lamps - nothing to add; rt_record logs the fit)
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
    // (added for the alpha-tested casters) traced with their alpha test where the frame's geometry table belongs to the
    // scene traced and the material table is there (the game's frames); otherwise the opaque casters alone, as before
    const SceneView frame_scene = current_scene(state);
    bool alpha = frame_scene.tlas == own.scene && frame_scene.geometry != VK_NULL_HANDLE && state.materials.ready() &&
                 state.materials.descriptor_set() != VK_NULL_HANDLE && state.materials.layout() != VK_NULL_HANDLE &&
                 (own.layout == VK_NULL_HANDLE || own.material_layout == state.materials.layout());
    VkPipeline pipeline = pass_pipeline(state, colour->format, colour->samples, alpha, &error);
    if (pipeline == VK_NULL_HANDLE && alpha) {
        if (first_report(state, kReportAlphaPipeline)) {
            get_log("vulkan: rt_shadows: the alpha-tested casters' pass couldn't be made (%s) - the opaque casters only",
                    error.c_str());
        }
        alpha = false;
        pipeline = pass_pipeline(state, colour->format, colour->samples, false, &error);
    }
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
    // (added by the time of day - engine/gfx/rt_daylight.h) the shadows as strong as the sun's (or moon's) share of the
    // light at the clock's time: none at twilight
    const rt::DayLight day = rt::day_light();
    const float strength = day.on ? own.settings.strength * day.shadow : own.settings.strength;
    constants.to_light = {core::at(to_light, 0), core::at(to_light, 1), core::at(to_light, 2), strength};
    const bool soft = own.settings.softness > 0.0F && own.settings.rays > 1;
    constants.shape = {
        static_cast<float>(std::tan(static_cast<double>(own.settings.softness) * kDegreesToRadians)),
        static_cast<float>(soft ? own.settings.rays : 1U), kRayLength,
        static_cast<float>(own.settings.covered ? 0U : own.settings.sky_rays)};
    const uint32_t mask = kMaskWorld | (own.settings.objects ? kMaskObjects : 0U) | (alpha ? kMaskAlphaTested : 0U);
    constants.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                        core::at(view.camera.position, 2),
                        std::bit_cast<float>(mask)};

    VkCommandBuffer commands = outside_pass(state);
    // the scene's build (recorded earlier on this queue) before the rays read it
    // (and, for the alpha-tested casters, the geometry table's write before the shader reads it)
    const VkMemoryBarrier built{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_HOST_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_HOST_BIT,
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
    update_agreement(state, to_light);
    const VkDescriptorBufferInfo agreement{.buffer = own.agreement.buffer, .offset = 0, .range = kAgreementBytes};
    const VkDescriptorBufferInfo geometry{
        .buffer = frame_scene.geometry, .offset = 0, .range = alpha ? frame_scene.geometry_bytes : VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    VkWriteDescriptorSet& scene_write = writes.front();
    scene_write.pNext = &scene;
    scene_write.dstBinding = 0;
    scene_write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkWriteDescriptorSet& depth_write = writes.at(1);
    depth_write.dstBinding = 1;
    depth_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depth_write.pImageInfo = &depth_image;
    VkWriteDescriptorSet& agreement_write = writes.at(2);
    agreement_write.dstBinding = 2;
    agreement_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    agreement_write.pBufferInfo = &agreement;
    VkWriteDescriptorSet& geometry_write = writes.back();
    geometry_write.dstBinding = kBindingGeometry;
    geometry_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    geometry_write.pBufferInfo = &geometry;
    const VkPipelineLayout layout = alpha ? own.alpha_layout : own.layout;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                              static_cast<uint32_t>(writes.size()) - (alpha ? 0U : 1U), writes.data());
    if (alpha) {
        const VkDescriptorSet material_set = state.materials.descriptor_set();
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, kMaterialSet, 1, &material_set, 0,
                                nullptr);
    }
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PassConstants), &constants);
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
        get_log("vulkan: rt_shadows: the first shadow pass (%ux%u, %s, strength %.2f, softness %.2f deg x %u rays%s%s)",
                width, height, traced,
                static_cast<double>(own.settings.strength), static_cast<double>(own.settings.softness),
                soft ? own.settings.rays : 1U, own.settings.objects ? ", objects cast" : "",
                alpha ? ", alpha-tested casters by their alpha" : "");
    }
    return true;
}
} // namespace

uint64_t rt_shadow_passes() { return shadows().passes; }

void release_rt_shadows(Backend& state)
{
    ShadowState& own = shadows();
    const VkDevice device = state.context.device();
    rt::set_object_shadows_replaced(false);
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
        if (own.alpha_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.alpha_layout, nullptr);
        }
        if (own.alpha_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.alpha_set_layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    state.memory.destroy_buffer(&own.agreement);
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.alpha_layout = VK_NULL_HANDLE;
    own.alpha_set_layout = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.failed = false;
}

} // namespace render::vulkan
