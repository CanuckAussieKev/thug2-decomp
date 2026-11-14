// NOT ORIGINAL - the night lighting's pass (vulkan_rt_lights.h; docs/design/vulkan.md "Lights at night").
#include "gpu_vulkan/vulkan_rt_lights.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52360001U;
constexpr uint64_t kReportNoScene = 0x52360002U;
constexpr uint64_t kReportTargets = 0x52360003U;
constexpr uint64_t kReportPipeline = 0x52360004U;
constexpr uint64_t kReportFirstPass = 0x52360005U;
constexpr uint64_t kReportNoMaterials = 0x52360006U;

constexpr float kMaxStrength = 8.0F;
constexpr float kMaxScale = 8.0F;
constexpr float kOffsetScale = 1.0F; // the shadow rays' start pushed off the surface by this much, growing with distance
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kPushConstantBytes = 128; // (the space every Vulkan device has)
constexpr double kSingular = 1e-300;
constexpr VkDeviceSize kUniformAlignment = 256; // (at least any device's minUniformBufferOffsetAlignment)
constexpr uint32_t kTimingSlots = 16;
constexpr uint32_t kTimingReport = 600;

// The bindings: set 0 - 0 the scene, 1 the depth, 2 the frame's geometry table, 3 the lights; set 1 the materials.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingGeometry = 2;
constexpr uint32_t kBindingLights = 3;
constexpr uint32_t kBindingCount = 4;
constexpr uint32_t kMaterialSet = 1;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The fragment shader (after the material and geometry tables' GLSL; MULTISAMPLED defined for a multisampled depth).
constexpr const char* kFragmentBody = R"(
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
// A light: at.xyz its position, at.w its size; colour.rgb its colour x intensity, colour.w its reach; more.x its
// material (the bits of a uint: whose texture's mean tints it, 0 none), more.y its falloff's core.
struct LightRecord {
    vec4 at;
    vec4 colour;
    vec4 more;
};
layout(set = 0, binding = 3, std140) uniform Lights {
    uvec4 count;  // x lights, y traced a pixel, z the shadow rays' mask, w the camera ray's mask
    vec4 params;  // x strength, y the rays' offset scale, z, w the knee and the most of the lights' sum (limited_light)
    LightRecord lights[256];
} u_lights;
layout(push_constant) uniform Pass {
    mat4 to_world; // (pixel x, pixel y, depth, 1) -> world, homogeneous
    ivec4 rect;    // the viewport: x, y, width, height
    vec4 camera;   // xyz: the camera's position
    vec4 unused0;
    vec4 unused1;
} pc;
layout(location = 0) out vec4 o_added;

float depth_at(ivec2 p) { return texelFetch(u_depth, p, 0).r; }

vec3 world_at(ivec2 p, float d)
{
    vec4 w = pc.to_world * vec4(vec2(p), d, 1.0);
    return w.xyz / w.w;
}

// The surface's step to the next pixel along `step` (toward the neighbour whose depth is nearer: a silhouette's other
// side isn't taken), zero when neither neighbour is inside the viewport.
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

// The texture's level for a footprint `width` world units across on the triangle (as the bounce light's).
float level_for(uint id, HitVertices v, vec3 a, vec3 b, vec3 c, float width)
{
    float world_area = length(cross(b - a, c - a));
    vec2 du = v.uv[1] - v.uv[0];
    vec2 dv = v.uv[2] - v.uv[0];
    float uv_area = abs(du.x * dv.y - du.y * dv.x);
    if (world_area <= 1e-12 || uv_area <= 1e-12 || width <= 0.0) {
        return 0.0;
    }
    ivec2 size = textureSize(sampler2D(material_textures[nonuniformEXT(material(id).texture_slot)], material_samplers[0]), 0);
    float texels = sqrt(uv_area * float(size.x) * float(size.y) / world_area);
    return max(0.0, log2(max(width * texels, 1e-6)));
}

const uint OBJECT_FLAG = 32u; // (rt::kObject: the skater, peds, vehicles)

// The pixel's surface: a ray from the camera finds its triangle (kept where it ends at the depth's point); its albedo -
// its material's texel at the hit's texture coordinates x the material's colour, at most 1 (the vertex colours, the
// level's baked light, left out) - in rgb, 1 in a when found; `object` whether it is an object's.
vec4 surface_albedo(vec3 here, float distance, float footprint, out bool object)
{
    object = false;
    vec3 eye = pc.camera.xyz;
    vec3 view = (here - eye) / max(distance, 1e-6);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, u_lights.count.w & 0xFFu, eye, 0.0, view,
                          distance * 1.05 + 1.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return vec4(0.0);
    }
    float t = rayQueryGetIntersectionTEXT(query, true);
    if (abs(t - distance) > 1.0 + 0.02 * distance) {
        return vec4(0.0);
    }
    uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
    uint instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
    object = (geometry_record(instance).instance_flags & OBJECT_FLAG) != 0u;
    HitVertices v;
    if (!hit_vertices(instance, rayQueryGetIntersectionPrimitiveIndexEXT(query, true), v)) {
        return vec4(min(material(id).colour.rgb, vec3(1.0)), 1.0);
    }
    mat4x3 to_world = rayQueryGetIntersectionObjectToWorldEXT(query, true);
    vec3 a = to_world * vec4(v.position[0], 1.0);
    vec3 b = to_world * vec4(v.position[1], 1.0);
    vec3 c = to_world * vec4(v.position[2], 1.0);
    vec2 uv = hit_uv(v, rayQueryGetIntersectionBarycentricsEXT(query, true));
    vec3 albedo = (material_texel_lod(id, uv, level_for(id, v, a, b, c, footprint)) * material(id).colour).rgb;
    return vec4(min(albedo, vec3(1.0)), 1.0);
}

// A light's falloff at `d` (engine/gfx/rt_lights.cpp light_falloff - the same formula).
float falloff(float d, float core, float reach)
{
    if (!(d < reach) || !(core > 0.0)) {
        return 0.0;
    }
    float x = d / reach;
    float x2 = x * x;
    float window = 1.0 - x2 * x2;
    return window * window * core * core / (core * core + d * d);
}

// The hue of a material's texture: its mean (four texels of its smallest level), the brightest channel 1; white for
// none or a black texture.
vec3 light_tint(uint id)
{
    if (id == 0u) {
        return vec3(1.0);
    }
    ivec2 size = textureSize(sampler2D(material_textures[nonuniformEXT(material(id).texture_slot)], material_samplers[0]), 0);
    float top = log2(float(max(max(size.x, size.y), 1)));
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        sum += material_texel_lod(id, vec2(0.25 + 0.5 * float(i & 1), 0.25 + 0.5 * float(i >> 1)), top).rgb;
    }
    float brightest = max(sum.r, max(sum.g, sum.b));
    return brightest > 4e-3 ? sum / brightest : vec3(1.0);
}

// Whether a ray is blocked before `reach`.
float blocked(uint mask, vec3 origin, vec3 direction, float reach)
{
    if (mask == 0u || reach <= 0.0) {
        return 0.0;
    }
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0,
                          direction, reach);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.0 : 1.0;
}

const uint MOST = 8u;
const uint OBJECTS_MASK = 0x02u;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = depth_at(p);
    if (d >= 1.0 || u_lights.count.x == 0u) {
        discard;
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
    float footprint = length(world_at(p + ivec2(1, 0), d) - here);
    bool object;
    vec4 albedo = surface_albedo(here, distance, footprint, object);
    if (!(albedo.a > 0.0)) {
        discard;
    }
    // every light's unshadowed light here; the brightest `picks` kept to be traced, the rest summed
    uint picks = min(u_lights.count.y, MOST);
    uint chosen[MOST];
    float weight[MOST];
    vec3 light[MOST];
    uint n = 0u;
    vec3 rest = vec3(0.0);
    uint count = min(u_lights.count.x, 256u);
    for (uint i = 0u; i < count; ++i) {
        vec3 to = u_lights.lights[i].at.xyz - here;
        float dist = length(to);
        float reach = u_lights.lights[i].colour.w;
        if (!(dist < reach)) {
            continue;
        }
        float facing = dot(normal, to / max(dist, 1e-6));
        if (facing <= 0.0) {
            continue;
        }
        vec3 c = u_lights.lights[i].colour.rgb * (falloff(dist, u_lights.lights[i].more.y, reach) * facing);
        float w = dot(c, vec3(0.299, 0.587, 0.114));
        if (!(w > 0.0)) {
            continue;
        }
        if (n < picks) {
            chosen[n] = i;
            weight[n] = w;
            light[n] = c;
            ++n;
            continue;
        }
        uint least = 0u;
        for (uint k = 1u; k < n; ++k) {
            if (weight[k] < weight[least]) {
                least = k;
            }
        }
        if (n > 0u && w > weight[least]) {
            rest += light[least];
            chosen[least] = i;
            weight[least] = w;
            light[least] = c;
        } else {
            rest += c;
        }
    }
    if (n == 0u) {
        discard;
    }
    float offset = u_lights.params.y * (1.0 + distance * 0.0005 + distance * distance * 6e-8);
    vec3 origin = here + normal * offset;
    uint mask = u_lights.count.z & 0xFFu;
    if (object) {
        mask &= ~OBJECTS_MASK; // (an object takes the world's shadows only: its low-poly body would shadow itself)
    }
    vec3 lit = vec3(0.0);
    float through = 0.0;
    float total = 0.0;
    for (uint k = 0u; k < n; ++k) {
        vec3 at = u_lights.lights[chosen[k]].at.xyz;
        vec3 to = at - origin;
        float dist = length(to);
        float stop = dist - u_lights.lights[chosen[k]].at.w; // (a lamp's own fixture doesn't shadow it)
        float seen = 1.0 - blocked(mask, origin, to / max(dist, 1e-6), stop);
        lit += light[k] * light_tint(floatBitsToUint(u_lights.lights[chosen[k]].more.x)) * seen;
        through += seen * weight[k];
        total += weight[k];
    }
    lit += rest * (total > 0.0 ? through / total : 0.0);
    // (added for the washed-out streets) the sum held under params.w (rt_lights.h limited_light): many lamps over one
    // spot light it up to its own colour, they don't burn it white
    float luminance = dot(lit, vec3(0.299, 0.587, 0.114));
    float knee = u_lights.params.z;
    if (luminance > knee) {
        float over = luminance - knee;
        float room = u_lights.params.w - knee;
        lit *= (knee + room * over / (room + over)) / luminance;
    }
    vec3 added = u_lights.params.x * albedo.rgb * lit;
    if (any(isnan(added)) || any(isinf(added))) {
        discard;
    }
    o_added = vec4(added, 0.0);
}
)";

// The push constants, as the shader's block lays them out.
struct PassConstants {
    std::array<float, kMatrixFloats> to_world{}; // column-major (GLSL's mat4)
    std::array<int32_t, 4> rect{};
    std::array<float, 4> camera{};
    std::array<float, 4> unused0{};
    std::array<float, 4> unused1{};
};
static_assert(sizeof(PassConstants) == kPushConstantBytes, "Vulkan's guaranteed push-constant space");

// The lights block (std140), as the shader lays it out.
struct LightRecord {
    std::array<float, 4> at{};
    std::array<float, 4> colour{};
    std::array<float, 4> more{};
};
struct LightsBlock {
    std::array<uint32_t, 4> count{};
    std::array<float, 4> params{};
    std::array<LightRecord, rt::kMaxViewLights> lights{};
};
constexpr std::size_t kLightsBlockBytes = 32 + (48 * rt::kMaxViewLights);
static_assert(sizeof(LightsBlock) == kLightsBlockBytes, "the shader's Lights block (std140)");

struct PassPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct PassTiming {
    VkQueryPool pool = VK_NULL_HANDLE;
    std::array<uint64_t, kTimingSlots> pending{};
    double period_ns = 0.0;
    double sum_ms = 0.0;
    uint32_t measured = 0;
    uint64_t lights = 0; // (the lights shone over the measured passes)
    bool unavailable = false;
};

struct LightsState {
    RtLightSettings settings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE; // the table's when `layout` was made
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::vector<PassPipeline> pipelines;
    PassTiming timing;
    std::vector<rt::Emitter> last_view;
    uint64_t emitters_logged = 0;
    bool failed = false;
    uint64_t passes = 0;
};

LightsState& lights_state()
{
    static LightsState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class LightsListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kLights; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_lights_pass(state, view);
        }
    }
};

LightsListener& listener()
{
    static LightsListener instance;
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
    LightsState& own = lights_state();
    if (own.layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        if (binding == kBindingDepth) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (binding == kBindingGeometry) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        } else if (binding == kBindingLights) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = type,
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = kBindingCount,
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
    own.material_layout = state.materials.layout();
    const std::array<VkDescriptorSetLayout, 2> sets{own.set_layout, own.material_layout};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = static_cast<uint32_t>(sets.size()),
                                                 .pSetLayouts = sets.data(),
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

// Whether a colour format holds values above 1 (a float target: the light is added, not screened).
bool float_format(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
        return true;
    default:
        return false;
    }
}

// The pipeline for a colour target (made on first use).
VkPipeline pass_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    LightsState& own = lights_state();
    for (const PassPipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 460\n";
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        source += "#define MULTISAMPLED\n";
    }
    source += "#extension GL_EXT_ray_query : require\n";
    source += "#define THUG2_MATERIAL_SET 1\n";
    source += material_glsl();
    source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 2\n";
    source += scene_geometry_glsl();
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
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // colour + the light x (1 - colour) - a screen, never past white - or, on a float target, + the light; alpha kept
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = float_format(format) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
    own.pipelines.push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

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

// view x projection (D3D's row vectors), as doubles.
Matrix view_projection_of(const rt::Camera& camera)
{
    Matrix product{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(camera.view, row), k)) *
                       static_cast<double>(core::at(core::at(camera.projection, k), column));
            }
            core::at(core::at(product, row), column) = sum;
        }
    }
    return product;
}

// (pixel x, pixel y, depth, 1) -> world (column vectors), as the shadow pass's (vulkan_rt_shadows.cpp pixel_to_world).
bool pixel_to_world(const rt::ViewDrawn& view, Matrix* out)
{
    const Matrix view_projection = view_projection_of(view.camera);
    Matrix clip_from_world{};
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
    Matrix product{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += core::at(core::at(world_from_clip, row), k) * core::at(core::at(clip_from_pixel, k), column);
            }
            core::at(core::at(product, row), column) = sum;
        }
    }
    *out = product;
    return true;
}

bool ensure_timing(Backend& state)
{
    PassTiming& timing = lights_state().timing;
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

// A slot's last pair read back when ready, and every kTimingReport passes the mean logged.
void collect_timing(Backend& state, uint32_t slot, uint32_t width, uint32_t height, std::size_t lights)
{
    LightsState& own = lights_state();
    PassTiming& timing = own.timing;
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
    timing.lights += lights;
    if (++timing.measured >= kTimingReport) {
        get_log("vulkan: rt_lights: %.3f ms a pass on the device over the last %u (%ux%u, %.1f lights in view on "
                "average, %u traced a pixel)",
                timing.sum_ms / timing.measured, timing.measured, width, height,
                static_cast<double>(timing.lights) / timing.measured, own.settings.shadowed);
        timing.sum_ms = 0.0;
        timing.measured = 0;
        timing.lights = 0;
    }
}

// The level's emitters logged when a new set arrives (how many, of which kind).
void log_emitters(const rt::LevelEmitters& level)
{
    LightsState& own = lights_state();
    if (level.serial == 0 || level.serial == own.emitters_logged) {
        return;
    }
    own.emitters_logged = level.serial;
    std::size_t glows = 0;
    for (const rt::Emitter& emitter : level.emitters) {
        glows += emitter.kind == rt::EmitterKind::kGlow ? 1U : 0U;
    }
    get_log("vulkan: rt_lights: the level's emitters: %zu (%zu glows, %zu self-lit)", level.emitters.size(), glows,
            level.emitters.size() - glows);
}

} // namespace

void set_rt_light_settings(const RtLightSettings& settings)
{
    RtLightSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.emissive = std::isfinite(kept.emissive) ? std::clamp(kept.emissive, 0.0F, kMaxScale) : 0.0F;
    kept.level = std::isfinite(kept.level) ? std::clamp(kept.level, 0.0F, kMaxScale) : 0.0F;
    kept.shadowed = std::clamp(kept.shadowed, 1U, kMostShadowedLights);
    kept.max_lights = std::clamp(kept.max_lights, 1U, static_cast<unsigned>(rt::kMaxViewLights));
    {
        const std::scoped_lock lock(backend().mutex);
        lights_state().settings = kept;
    }
    rt::set_emitters_wanted(kept.on && kept.emissive > 0.0F);
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtLightSettings rt_light_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return lights_state().settings;
}

VkImageUsageFlags rt_lights_depth_usage(const Backend& state, VkFormat format)
{
    if (!lights_state().settings.on || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_lights_pass(Backend& state, const rt::ViewDrawn& view)
{
    LightsState& own = lights_state();
    if (!own.settings.on || own.failed) {
        return false;
    }
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing)) {
            get_log("vulkan: rt_lights=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device with "
                    "VK_KHR_ray_query) - no ray-traced lights");
        }
        return false;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene)) {
            get_log("vulkan: rt_lights: no acceleration structure for the scene yet - the pass skipped");
        }
        return false;
    }
    if (!state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoMaterials)) {
            get_log("vulkan: rt_lights: no material table - the pass skipped");
        }
        return false;
    }
    // the view's lights
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    log_emitters(*level);
    rt::ViewLightRequest request;
    request.camera = view.camera.position;
    const Matrix view_projection = view_projection_of(view.camera);
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(request.view_projection, row), column) =
                static_cast<float>(core::at(core::at(view_projection, row), column));
        }
    }
    request.max = own.settings.max_lights;
    request.points = own.settings.level > 0.0F;
    request.emitters = own.settings.emissive > 0.0F;
    request.point_scale = own.settings.level;
    request.emitter_scale = own.settings.emissive;
    own.last_view = rt::select_view_lights(rt::frame_record(), level.get(), request);
    if (own.last_view.empty()) {
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
            get_log("vulkan: rt_lights: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the pass skipped there");
        }
        return false;
    }
    std::string error;
    const VkPipeline pipeline = pass_pipeline(state, colour->format, colour->samples, &error);
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_lights: the pass couldn't be made (%s) - no ray-traced lights", error.c_str());
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
        return false; // (the table was made again with another layout: not while the device lives)
    }
    Matrix to_world{};
    if (!pixel_to_world(view, &to_world)) {
        return false;
    }
    const uint32_t left = std::min(core::at(view.rect, 0), colour->extent.width);
    const uint32_t top = std::min(core::at(view.rect, 1), colour->extent.height);
    const uint32_t width = std::min(core::at(view.rect, 2), colour->extent.width - left);
    const uint32_t height = std::min(core::at(view.rect, 3), colour->extent.height - top);
    if (width == 0 || height == 0) {
        return false;
    }

    // the lights' block, in the frame's ring
    auto block = std::make_unique<LightsBlock>();
    block->count = {static_cast<uint32_t>(own.last_view.size()), own.settings.shadowed, kLightsShadowMask,
                    kLightsSurfaceMask};
    // (added by the time of day - engine/gfx/rt_daylight.h) the lamps on as the clock says: off by day, on at night
    const rt::DayLight day = rt::day_light();
    block->params = {day.on ? own.settings.strength * day.lamps : own.settings.strength, kOffsetScale, rt::kLightKnee,
                     rt::kLightMost};
    for (std::size_t i = 0; i < own.last_view.size(); ++i) {
        const rt::Emitter& light = core::at(own.last_view, i);
        LightRecord& record = core::at(block->lights, i);
        record.at = {core::at(light.position, 0), core::at(light.position, 1), core::at(light.position, 2), light.size};
        record.colour = {core::at(light.colour, 0) * light.intensity, core::at(light.colour, 1) * light.intensity,
                         core::at(light.colour, 2) * light.intensity, light.reach};
        record.more = {std::bit_cast<float>(static_cast<uint32_t>(light.material)), light.core, 0.0F, 0.0F};
    }
    const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(block.get()), sizeof(LightsBlock));
    const std::optional<RingSpan> lights_span = state.ring.copy(bytes, kUniformAlignment);
    if (!lights_span) {
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
    constants.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                        core::at(view.camera.position, 2), 0.0F};

    VkCommandBuffer commands = outside_pass(state);
    const bool timed = ensure_timing(state);
    const auto timing_slot = static_cast<uint32_t>(own.passes % kTimingSlots);
    if (timed) {
        collect_timing(state, timing_slot, width, height, own.last_view.size());
        vkCmdResetQueryPool(commands, own.timing.pool, timing_slot * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timing.pool, timing_slot * 2);
    }
    // the scene's build (recorded earlier on this queue) and the lights' write before the shader reads them
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_HOST_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_UNIFORM_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
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
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const VkDescriptorImageInfo depth_image{.sampler = own.sampler,
                                            .imageView = depth->view,
                                            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo geometry{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const VkDescriptorBufferInfo lights{
        .buffer = lights_span->buffer, .offset = lights_span->offset, .range = sizeof(LightsBlock)};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.dstBinding = binding;
    }
    core::at(writes, kBindingScene).pNext = &scene_info;
    core::at(writes, kBindingScene).descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    core::at(writes, kBindingDepth).descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    core::at(writes, kBindingDepth).pImageInfo = &depth_image;
    core::at(writes, kBindingGeometry).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    core::at(writes, kBindingGeometry).pBufferInfo = &geometry;
    core::at(writes, kBindingLights).descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    core::at(writes, kBindingLights).pBufferInfo = &lights;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    for (Image* image : {colour, depth}) {
        if (image->layout != image->resting) {
            Memory::transition(commands, image, image->resting);
        }
    }
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (timing_slot * 2) + 1);
        core::at(own.timing.pending, timing_slot) = state.frames.serial();
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    ++own.passes;
    // [debug] rt_lights_log=1: the lights of every 600th pass logged (where the light comes from)
    if (own.settings.log && own.passes % kTimingReport == 1) {
        for (const rt::Emitter& light : own.last_view) {
            get_log("vulkan: rt_lights: light %s at %.0f %.0f %.0f size %.1f reach %.0f intensity %.3f colour %.2f %.2f "
                    "%.2f",
                    light.kind == rt::EmitterKind::kPoint ? "point" : (light.kind == rt::EmitterKind::kGlow ? "glow" : "self-lit"),
                    static_cast<double>(core::at(light.position, 0)), static_cast<double>(core::at(light.position, 1)),
                    static_cast<double>(core::at(light.position, 2)), static_cast<double>(light.size),
                    static_cast<double>(light.reach), static_cast<double>(light.intensity),
                    static_cast<double>(core::at(light.colour, 0)), static_cast<double>(core::at(light.colour, 1)),
                    static_cast<double>(core::at(light.colour, 2)));
        }
    }
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_lights: the first lights pass (%ux%u, %zu lights in view, %u traced a pixel, strength "
                "%.2f, emissive x%.2f, level lights x%.2f, %s)",
                width, height, own.last_view.size(), own.settings.shadowed, static_cast<double>(own.settings.strength),
                static_cast<double>(own.settings.emissive), static_cast<double>(own.settings.level),
                float_format(colour->format) ? "added" : "screened");
    }
    return true;
}

uint64_t rt_lights_passes() { return lights_state().passes; }

std::vector<rt::Emitter> rt_lights_last_view()
{
    const std::scoped_lock lock(backend().mutex);
    return lights_state().last_view;
}

void release_rt_lights(Backend& state)
{
    LightsState& own = lights_state();
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
        if (own.timing.pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timing.pool, nullptr);
        }
    }
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.timing = PassTiming{};
    own.failed = false;
}

} // namespace render::vulkan
