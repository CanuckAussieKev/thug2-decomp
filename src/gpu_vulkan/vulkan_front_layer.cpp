// NOT ORIGINAL - round FL: the front layer (vulkan_front_layer.h; docs/design/vulkan.md "Round FL: the front layer").
#include "gpu_vulkan/vulkan_front_layer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_light_list.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_restir_gi.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_view_matrix.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x464C0001U;
constexpr uint64_t kReportFirst = 0x464C0002U;

// The bindings of set 0 (push descriptors); set 1: the material table.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingGeometry = 1;
constexpr uint32_t kBindingBlock = 2;
constexpr uint32_t kBindingOpaque = 3;      // the prepass's depth (R32F)
constexpr uint32_t kBindingDrawn = 4;       // the see-through draws' depth (sampled)
constexpr uint32_t kBindingDepthNow = 5;    // the layer's depth, light and the disc's light written
constexpr uint32_t kBindingLightNow = 6;
constexpr uint32_t kBindingSunNow = 7;
constexpr uint32_t kBindingDepthBefore = 8; // ... the last view's, read (the history)
constexpr uint32_t kBindingLightBefore = 9;
constexpr uint32_t kBindingSunBefore = 10;
constexpr uint32_t kBindingList = 11; // 11 - 18: the light list and the radiance cache (vulkan_light_list.h FrameLightList)
constexpr uint32_t kListBuffers = 8;
constexpr uint32_t kBindingCloudMap = kBindingList + kListBuffers; // the clouds' shadow map on the disc's light
constexpr uint32_t kBindingCount = kBindingCloudMap + 1;
constexpr uint32_t kMaterialSet = 1;
constexpr uint32_t kGroupSide = 8;
constexpr VkDeviceSize kUniformAlignment = 256;
constexpr uint32_t kTimingSlots = 8;
constexpr uint32_t kTimingReport = 600;

// The pass's block (std140).
struct FrontBlock {
    std::array<float, 16> to_world{};      // (pixel x, pixel y, depth, 1) -> world, column-major
    std::array<float, 16> prev_to_pixel{}; // world -> the last view's (pixel x, pixel y, depth, w)
    std::array<float, 16> prev_to_world{}; // the last view's (pixel, depth, 1) -> world
    std::array<float, 4> eye{};            // xyz the camera; w the frame's pre-exposure
    std::array<float, 4> prev{};           // x this frame's pre-exposure over the last view's, y 1: history, z its frames
    std::array<int32_t, 4> rect{};         // the view: x, y, width, height (target pixels)
    std::array<uint32_t, 4> counts{};      // x the list's entries, y the frame, z the samples, w 1: the cache is read
    std::array<uint32_t, 4> instances{};   // xy the TLAS instances' device address, z their count
    std::array<float, 4> sun{};            // toward the disc, w the cosine of its radius (kNoDisc: none)
    std::array<float, 4> sun_radiance{};   // rgb its radiance, w its solid angle
    std::array<float, 4> sky{};            // x 1 / the sky grid's luminance integral
    std::array<float, 4> cache_cells{};    // x the angle a radiance cache cell spans
    std::array<float, 4> cloud_place{};    // (vulkan_rt_sky_pass.h CloudShadowMap)
    std::array<float, 4> cloud_light{};
};
static_assert(sizeof(FrontBlock) == (3 * 64) + (11 * 16), "the shader's block (std140)");

constexpr const char* kCommon = R"(
layout(std140, set = 0, binding = 2) uniform FrontBlock {
    mat4 to_world;
    mat4 prev_to_pixel;
    mat4 prev_to_world;
    vec4 eye;
    vec4 prev;
    ivec4 rect;
    uvec4 counts;
    uvec4 instances;
    vec4 sun;
    vec4 sun_radiance;
    vec4 sky;
    vec4 cache_cells;
    vec4 cloud_place;
    vec4 cloud_light;
} pass;
struct LightEntry {
    float threshold;
    uint alias;
    float pdf;
    uint payload;
};
layout(std430, set = 0, binding = 11) readonly buffer Lights { LightEntry light_entries[]; };
layout(std430, set = 0, binding = 12) readonly buffer Emitters {
    uint emitter_count;
    uint emitter_spare;
    uint emitter_unused;
    uint emitter_pad;
    uvec4 emitter_entries[];
};
layout(std430, set = 0, binding = 13) readonly buffer Lamps { LightRecord lamps[]; };
layout(std430, set = 0, binding = 14) readonly buffer Membership { uint membership[]; };
layout(std430, set = 0, binding = 15) readonly buffer Slots { uint slots[]; };
layout(std430, set = 0, binding = 16) readonly buffer SkyCells { vec4 sky_cells[]; };
layout(std430, set = 0, binding = 17) readonly buffer SkyAlias { uvec2 sky_alias[]; };
layout(std430, set = 0, binding = 18) readonly buffer Cache { uvec4 cache[]; };
#define LIST_INSTANCE_ADDRESS pass.instances.xy
#define LIST_INSTANCES pass.instances.z
#define LL_COUNT pass.counts.x
#define LL_SUN pass.sun
#define LL_SUN_RADIANCE pass.sun_radiance
#define LL_SKY_DIST pass.sky.x
#define LL_REACH 1.0e6
#define LL_CLOUD_PLACE pass.cloud_place
#define LL_CLOUD_LIGHT pass.cloud_light
#define LL_NO_DISC
#define LL_CACHE
#define LL_EYE pass.eye.xyz
#define LL_CACHE_CELLS pass.cache_cells.x
#define LL_CACHE_ON (pass.counts.w != 0u)
)";

// The layer's light: per pixel of the view where the see-through draws' depth is in front of the prepass's (not the
// prepass's own surface - within 1 % of its distance, the shading's light_exact), the point there and its normal from the
// layer's own neighbours (the nearer one on each axis); the disc's visibility by one ray toward a point of its cone, the
// rest of the list by kSamples light_sample draws on a white diffuser + the cache's bounce; both pre-exposed; averaged
// with the point's history (through the last view's camera, its layer's depth there within 2 % of the distance + 1 unit).
constexpr const char* kBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3, r32f) uniform readonly image2D u_opaque;
layout(set = 0, binding = 4) uniform sampler2D u_drawn;
layout(set = 0, binding = 5, r32f) uniform writeonly image2D o_depth;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D o_light;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_sun;
layout(set = 0, binding = 8, r32f) uniform readonly image2D h_depth;
layout(set = 0, binding = 9, rgba16f) uniform readonly image2D h_light;
layout(set = 0, binding = 10, rgba16f) uniform readonly image2D h_sun;
const uint FRONT_RAYS = 7u; // (the light rays' kinds: the world, the objects, the cut-outs - ReSTIR GI's)

vec3 good3(vec3 x) { return any(isnan(x)) || any(isinf(x)) ? vec3(0.0) : x; }
vec3 world_at(vec2 pixel, float depth)
{
    vec4 w = pass.to_world * vec4(pixel, depth, 1.0);
    return w.xyz / w.w;
}
vec3 before_world_at(vec2 pixel, float depth)
{
    vec4 w = pass.prev_to_world * vec4(pixel, depth, 1.0);
    return w.xyz / w.w;
}
// Whether pixel q has a front layer, and its depth.
bool in_front(ivec2 q, out float z)
{
    q = clamp(q, pass.rect.xy, pass.rect.xy + pass.rect.zw - 1);
    z = texelFetch(u_drawn, q, 0).r;
    float opaque = imageLoad(u_opaque, q).r;
    return z < 1.0 && z < opaque && abs(z - opaque) > max(1.0e-6, (1.0 - opaque) * 0.01);
}
// The layer's normal at `here` (toward the eye): from its neighbours on the layer, the nearer along the view on each axis.
vec3 front_normal(ivec2 p, vec3 here, vec3 dir)
{
    float best[2] = float[2](1.0e30, 1.0e30);
    vec3 step[2] = vec3[2](vec3(0.0), vec3(0.0));
    for (int k = 0; k < 4; ++k) {
        ivec2 o = k == 0 ? ivec2(1, 0) : (k == 1 ? ivec2(-1, 0) : (k == 2 ? ivec2(0, 1) : ivec2(0, -1)));
        float zq;
        if (!in_front(p + o, zq)) {
            continue;
        }
        vec3 q = world_at(vec2(p + o) + 0.5, zq);
        float apart = abs(dot(q - here, dir));
        int axis = k < 2 ? 0 : 1;
        if (apart < best[axis]) {
            best[axis] = apart;
            step[axis] = (q - here) * float(o.x + o.y);
        }
    }
    vec3 n = cross(step[0], step[1]);
    n = dot(n, n) > 0.0 ? normalize(n) : -dir;
    return dot(n, dir) > 0.0 ? -n : n;
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    ivec2 p = pass.rect.xy + local;
    float z;
    vec3 eye = pass.eye.xyz;
    vec3 here = vec3(0.0);
    float dist = 0.0;
    if (in_front(p, z)) {
        here = world_at(vec2(p) + 0.5, z);
        dist = length(here - eye);
    }
    if (!(dist > 0.0)) {
        imageStore(o_depth, p, vec4(1.0, 0.0, 0.0, 0.0));
        imageStore(o_light, p, vec4(0.0));
        imageStore(o_sun, p, vec4(0.0));
        return;
    }
    vec3 dir = (here - eye) / dist;
    vec3 n = front_normal(p, here, dir);
    vec3 at = here + n * local_light_offset(here);
    uint rng = path_sequence_start(uint(p.x), uint(p.y), pass.counts.y, 0x464C4159u);
    // the rest of the list on a white diffuser (the disc's picks bring nothing: LL_NO_DISC) + every further bounce
    vec3 rest = vec3(0.0);
    uint samples = max(pass.counts.z, 1u);
    for (uint k = 0u; k < samples; ++k) {
        vec3 toward;
        vec3 incident = light_sample(at, n, n, FRONT_RAYS, rng, toward);
        rest += incident * max(dot(n, toward), 0.0);
    }
    rest = rest / (PATH_PI * float(samples)) + cache_bounce(here, n);
    // the disc: its visibility toward a point of its cone (the clouds' shadow with it), its light on a white diffuser
    vec3 sun = vec3(0.0);
    float seen = 0.0;
    if (pass.sun.w < 1.5) {
        float u1 = path_next(rng);
        float u2 = path_next(rng);
        vec3 l = path_to_world(pass.sun.xyz, path_cone_direction(pass.sun.w, u1, u2));
        if (dot(n, l) > 0.0) {
            seen = sun_kept(at) * ray_light_share(FRONT_RAYS, at, l, LL_REACH);
        }
        sun = pass.sun_radiance.rgb * (pass.sun_radiance.w * max(dot(n, pass.sun.xyz), 0.0) * seen / PATH_PI);
    }
    rest = good3(rest * pass.eye.w);
    sun = good3(sun * pass.eye.w);
    // the history: the point through the last view's camera, the same layer there
    float frames = 0.0;
    vec3 rest_before = vec3(0.0);
    vec4 sun_before = vec4(0.0);
    if (pass.prev.y > 0.5) {
        vec4 q = pass.prev_to_pixel * vec4(here, 1.0);
        if (q.w > 0.0) {
            ivec2 qi = ivec2(floor(q.xy / q.w));
            if (all(greaterThanEqual(qi, pass.rect.xy)) && all(lessThan(qi, pass.rect.xy + pass.rect.zw))) {
                float hz = imageLoad(h_depth, qi).r;
                if (hz < 1.0 && length(before_world_at(vec2(qi) + 0.5, hz) - here) <= 0.02 * dist + 1.0) {
                    vec4 hl = imageLoad(h_light, qi);
                    vec4 hs = imageLoad(h_sun, qi);
                    frames = clamp(hl.a, 0.0, pass.prev.z - 1.0);
                    rest_before = good3(hl.rgb * pass.prev.x);
                    sun_before = vec4(good3(hs.rgb * pass.prev.x), clamp(hs.a, 0.0, 1.0));
                }
            }
        }
    }
    float w = 1.0 / (frames + 1.0);
    vec3 rest_out = mix(rest_before, rest, w);
    vec4 sun_out = mix(sun_before, vec4(sun, seen), w);
    imageStore(o_depth, p, vec4(z, 0.0, 0.0, 0.0));
    imageStore(o_light, p, vec4(min(rest_out, vec3(65504.0)), frames + 1.0));
    imageStore(o_sun, p, vec4(min(sun_out.rgb, vec3(65504.0)), sun_out.a));
}
)";

struct Timing {
    VkQueryPool pool = VK_NULL_HANDLE;
    bool unavailable = false;
    double period_ns = 0.0;
    std::array<uint64_t, kTimingSlots> pending{};
    double sum_ms = 0.0;
    uint32_t measured = 0;
};

struct FrontState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> pipelines{}; // without, with the clouds' shadow map
    VkSampler sampler = VK_NULL_HANDLE;
    bool failed = false;
    // the last lit view's camera (the history's)
    bool before = false;
    std::array<float, 16> prev_to_pixel{};
    std::array<float, 16> prev_to_world{};
    float prev_exposure = 1.0F;
    Timing timing;
    uint64_t passes = 0;
    double last_ms = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
};

FrontState& front_state()
{
    static FrontState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

VkDescriptorType binding_type(uint32_t binding)
{
    switch (binding) {
    case kBindingScene: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    case kBindingGeometry: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case kBindingBlock: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case kBindingDrawn:
    case kBindingCloudMap: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    default: break;
    }
    return binding >= kBindingList ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

std::string shader_source(bool cloud_map)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    source += "layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;\n";
    source += "#define THUG2_MATERIAL_SET 1\n";
    source += material_glsl();
    source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 1\n";
    source += scene_geometry_glsl();
    source += cutout_rays_glsl();
    source += rt::kDisplayTransferGlsl;
    source += rt::brdf::brdf_glsl();
    source += hit_surface_glsl();
    source += rt::path::kPathGlsl;
    source += rt_lamp_glsl();
    if (cloud_map) {
        source += "#define CLOUD_MAP\n" + cloud_map_glsl(kBindingCloudMap);
    }
    source += "const uint SKY_WIDTH = " + std::to_string(rt::lights::kSkyGridWidth) + "u;\n";
    source += "const uint SKY_HEIGHT = " + std::to_string(rt::lights::kSkyGridHeight) + "u;\n";
    source += "const uint CACHE_CAPACITY = " + std::to_string(kRadianceCacheCapacity) + "u;\n";
    source += "const uint CACHE_PROBES = " + std::to_string(kRadianceCacheProbes) + "u;\n";
    source += "const float CACHE_CELL_LEAST = " + std::to_string(kRadianceCacheCellLeast) + ";\n";
    source += kCommon;
    source += light_instances_glsl();
    source += rt::lights::kLightListGlsl;
    source += local_light_glsl();
    source += light_sample_glsl();
    source += kBody;
    return source;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateComputePipelines failed";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// The layout, the sampler and the pipeline wanted (made on first use). False (reported once): they can't be made.
VkPipeline ensure_pipeline(Backend& state, bool cloud_map, std::string* error)
{
    FrontState& own = front_state();
    const VkDevice device = state.context.device();
    if (own.set_layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            core::at(bindings, binding) = {.binding = binding,
                                           .descriptorType = binding_type(binding),
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                           .pImmutableSamplers = nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = kBindingCount,
                                                       .pBindings = bindings.data()};
        if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
            own.set_layout = VK_NULL_HANDLE;
            *error = "vkCreateDescriptorSetLayout failed";
            return VK_NULL_HANDLE;
        }
    }
    if (own.layout == VK_NULL_HANDLE) {
        own.material_layout = state.materials.layout();
        const std::array<VkDescriptorSetLayout, 2> sets{own.set_layout, own.material_layout};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = static_cast<uint32_t>(sets.size()),
                                                     .pSetLayouts = sets.data(),
                                                     .pushConstantRangeCount = 0,
                                                     .pPushConstantRanges = nullptr};
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
            own.layout = VK_NULL_HANDLE;
            *error = "vkCreatePipelineLayout failed";
            return VK_NULL_HANDLE;
        }
    }
    if (own.sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &info, nullptr, &own.sampler) != VK_SUCCESS) {
            own.sampler = VK_NULL_HANDLE;
            *error = "vkCreateSampler failed";
            return VK_NULL_HANDLE;
        }
    }
    VkPipeline& pipeline = core::at(own.pipelines, cloud_map ? 1U : 0U);
    if (pipeline == VK_NULL_HANDLE) {
        pipeline = make_compute(device, own.layout, shader_source(cloud_map), error);
    }
    return pipeline;
}

bool ensure_timing(Backend& state, Timing& timing)
{
    if (timing.pool != VK_NULL_HANDLE || timing.unavailable) {
        return timing.pool != VK_NULL_HANDLE;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimingSlots * 2,
                                     .pipelineStatistics = 0};
    if (properties.limits.timestampComputeAndGraphics != VK_TRUE ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &timing.pool) != VK_SUCCESS) {
        timing.pool = VK_NULL_HANDLE;
        timing.unavailable = true;
        return false;
    }
    timing.period_ns = static_cast<double>(properties.limits.timestampPeriod);
    timing.pending = {};
    return true;
}

void collect_timing(Backend& state, FrontState& own, uint32_t slot)
{
    Timing& timing = own.timing;
    const uint64_t written = core::at(timing.pending, slot);
    core::at(timing.pending, slot) = 0;
    if (written == 0 || state.frames.completed() < written) {
        return;
    }
    std::array<uint64_t, 2> ticks{};
    if (vkGetQueryPoolResults(state.context.device(), timing.pool, slot * 2, 2, sizeof(ticks), ticks.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS ||
        ticks.back() < ticks.front()) {
        return;
    }
    constexpr double kNsPerMs = 1e6;
    timing.sum_ms += static_cast<double>(ticks.back() - ticks.front()) * timing.period_ns / kNsPerMs;
    if (++timing.measured >= kTimingReport) {
        own.last_ms = timing.sum_ms / timing.measured;
        get_log("front layer: %.3f ms a frame on the device over the last %u (%ux%u; the see-through draws' depth and "
                "their light)",
                own.last_ms, timing.measured, own.width, own.height);
        timing.sum_ms = 0.0;
        timing.measured = 0;
    }
}

class FrontListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kFrontLayer; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.device_made || !state.draws_made) {
            return;
        }
        (void)record_front_layer_pass(state, view);
    }
};

FrontListener& listener()
{
    static FrontListener instance;
    return instance;
}

} // namespace

void install_front_layer() { rt::add_view_listener(&listener()); }

void uninstall_front_layer() { rt::remove_view_listener(&listener()); }

void release_front_layer(Backend& state)
{
    FrontState& own = front_state();
    const VkDevice device = state.context.device();
    if (device != VK_NULL_HANDLE) {
        for (const VkPipeline pipeline : own.pipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
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
        if (own.timing.pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timing.pool, nullptr);
        }
    }
    own = FrontState{};
}

bool record_front_layer_pass(Backend& state, const rt::ViewDrawn& view)
{
    FrontState& own = front_state();
    if (own.failed || !state.context.ray_tracing_enabled()) {
        return false;
    }
    const SceneView scene = current_scene(state);
    FrameLightList list;
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE || scene.instances_address == 0 ||
        !state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE ||
        !restir_gi_frame_lights(state, &list)) {
        return false;
    }
    if (own.layout != VK_NULL_HANDLE && own.material_layout != state.materials.layout()) {
        return false;
    }
    CloudShadowMap cloud_map;
    const bool map_now = cloud_shadow_map(state, &cloud_map);
    std::string error;
    const VkPipeline pipeline = ensure_pipeline(state, map_now, &error);
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("front layer: its pass couldn't be made (%s) - see-through surfaces keep their own light", error.c_str());
        }
        return false;
    }
    view_matrix::Matrix to_world{};
    view_matrix::Matrix to_pixel{};
    if (!view_matrix::pixel_to_world(view, &to_world) || !view_matrix::invert(to_world, &to_pixel)) {
        return false;
    }
    const bool timed = ensure_timing(state, own.timing);
    const auto slot = static_cast<uint32_t>(own.passes % kTimingSlots);
    if (timed) {
        collect_timing(state, own, slot);
        VkCommandBuffer commands = outside_pass(state);
        vkCmdResetQueryPool(commands, own.timing.pool, slot * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timing.pool, slot * 2);
    }
    // 1. the see-through draws' depth
    FrontLayerImages images;
    if (!surface_light_front_prepass(state, &images)) {
        if (timed) {
            vkCmdWriteTimestamp(outside_pass(state), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (slot * 2) + 1);
        }
        return false;
    }
    // 2. its light
    const Extent extent{.width = images.light.front()->extent.width, .height = images.light.front()->extent.height};
    std::array<uint32_t, 4> rect = view.rect;
    core::at(rect, 0) = std::min(core::at(rect, 0), extent.width);
    core::at(rect, 1) = std::min(core::at(rect, 1), extent.height);
    core::at(rect, 2) = std::min(core::at(rect, 2), extent.width - core::at(rect, 0));
    core::at(rect, 3) = std::min(core::at(rect, 3), extent.height - core::at(rect, 1));
    const uint32_t width = core::at(rect, 2);
    const uint32_t height = core::at(rect, 3);
    if (width == 0 || height == 0) {
        return false;
    }
    const float exposure = rt::frame_pre_exposure();
    const bool history = images.history && own.before;
    FrontBlock block;
    block.to_world = view_matrix::column_major(to_world);
    block.prev_to_pixel = own.prev_to_pixel;
    block.prev_to_world = own.prev_to_world;
    block.eye = {core::at(view.camera.position, 0), core::at(view.camera.position, 1), core::at(view.camera.position, 2),
                 exposure};
    block.prev = {own.prev_exposure > 0.0F ? exposure / own.prev_exposure : 1.0F, history ? 1.0F : 0.0F,
                  kFrontLayerHistoryFrames, 0.0F};
    block.rect = {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
                  static_cast<int32_t>(width), static_cast<int32_t>(height)};
    block.counts = {list.entries, static_cast<uint32_t>(state.frame & 0xFFFFFFFFULL), kFrontLayerSamples,
                    list.cache_on ? 1U : 0U};
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), scene.instances, 0U};
    block.sun = list.sun;
    block.sun_radiance = list.sun_radiance;
    block.sky = {list.sky_dist, 0.0F, 0.0F, 0.0F};
    block.cache_cells = {list.cache_cells, 0.0F, 0.0F, 0.0F};
    if (map_now) {
        block.cloud_place = cloud_map.place;
        block.cloud_light = cloud_map.light;
    }
    std::array<uint8_t, sizeof(FrontBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> block_span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!block_span) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, images.drawn, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    std::array<Image*, 7> storage{images.opaque,         images.depth.front(), images.light.front(), images.sun.front(),
                                  images.depth.back(),   images.light.back(),  images.sun.back()};
    for (Image* image : storage) {
        Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
    }
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                                 VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_UNIFORM_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready,
                         0, nullptr, 0, nullptr);
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const auto whole = [](VkBuffer buffer) { return VkDescriptorBufferInfo{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE}; };
    const VkDescriptorBufferInfo geometry_info{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const VkDescriptorBufferInfo block_info{.buffer = block_span->buffer, .offset = block_span->offset,
                                            .range = sizeof(FrontBlock)};
    const std::array<VkDescriptorBufferInfo, kListBuffers> list_buffers{
        whole(list.table),      whole(list.emitters),  whole(list.lamps),     whole(list.membership),
        whole(list.slots),      whole(list.sky_cells), whole(list.sky_alias), whole(list.cache)};
    const VkDescriptorImageInfo drawn_info{.sampler = own.sampler,
                                           .imageView = images.drawn->view,
                                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    std::array<VkDescriptorImageInfo, 7> storage_info{};
    for (std::size_t k = 0; k < storage.size(); ++k) {
        core::at(storage_info, k) = {.sampler = VK_NULL_HANDLE, .imageView = core::at(storage, k)->view,
                                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    }
    constexpr std::array<uint32_t, 7> kStorageBindings{kBindingOpaque,      kBindingDepthNow,   kBindingLightNow, kBindingSunNow,
                                                       kBindingDepthBefore, kBindingLightBefore, kBindingSunBefore};
    std::vector<VkWriteDescriptorSet> writes;
    const auto add = [&writes](uint32_t binding, const VkDescriptorBufferInfo* buffer, const VkDescriptorImageInfo* image,
                               const void* next) {
        VkWriteDescriptorSet one{};
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.pNext = next;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = binding_type(binding);
        one.pBufferInfo = buffer;
        one.pImageInfo = image;
        writes.push_back(one);
    };
    add(kBindingScene, nullptr, nullptr, &scene_info);
    add(kBindingGeometry, &geometry_info, nullptr, nullptr);
    add(kBindingBlock, &block_info, nullptr, nullptr);
    add(kBindingDrawn, nullptr, &drawn_info, nullptr);
    for (std::size_t k = 0; k < storage.size(); ++k) {
        add(core::at(kStorageBindings, k), nullptr, &core::at(storage_info, k), nullptr);
    }
    for (uint32_t k = 0; k < kListBuffers; ++k) {
        add(kBindingList + k, &core::at(list_buffers, k), nullptr, nullptr);
    }
    if (map_now) {
        add(kBindingCloudMap, nullptr, &cloud_map.image, nullptr);
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
    Memory::transition(commands, images.drawn, images.drawn->resting);
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (slot * 2) + 1);
        core::at(own.timing.pending, slot) = state.frames.serial();
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    images.drawn->used = serial;
    for (Image* image : storage) {
        image->used = serial;
    }
    own.prev_to_world = block.to_world;
    own.prev_to_pixel = view_matrix::column_major(to_pixel);
    own.prev_exposure = exposure;
    own.before = true;
    own.width = width;
    own.height = height;
    ++own.passes;
    surface_light_front_lit();
    if (first_report(state, kReportFirst)) {
        get_log("front layer: the first see-through surfaces lit by the light list (%ux%u, %u lights listed, %u samples a "
                "pixel, the clouds' shadow %s)",
                width, height, list.entries, kFrontLayerSamples, map_now ? "on the disc" : "none");
    }
    return true;
}

FrontLayerStats front_layer_stats()
{
    const std::scoped_lock lock(backend().mutex);
    const FrontState& own = front_state();
    return {.passes = own.passes, .gpu_ms = own.last_ms, .width = own.width, .height = own.height};
}

} // namespace render::vulkan
