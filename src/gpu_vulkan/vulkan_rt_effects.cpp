// NOT ORIGINAL - ray-traced ambient occlusion and its denoiser (vulkan_rt_effects.h; docs/design/vulkan.md, R5).
#include "gpu_vulkan/vulkan_rt_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52350001U;
constexpr uint64_t kReportNoScene = 0x52350002U;
constexpr uint64_t kReportTargets = 0x52350003U;
constexpr uint64_t kReportPipeline = 0x52350004U;
constexpr uint64_t kReportFirstPass = 0x52350005U;
constexpr uint64_t kReportImages = 0x52350006U;

constexpr float kMaxStrength = 1.0F;
constexpr float kMinRadius = 1.0F;
constexpr float kMaxRadius = 1000.0F;
constexpr unsigned kMaxRays = 16;
constexpr float kOffsetScale = 1.0F; // the rays' start pushed off the surface by this much, growing with distance
constexpr uint32_t kGroupSide = 8;   // the compute passes' work groups: 8 x 8 pixels
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr std::size_t kPushConstantBytes = 128; // (the space every Vulkan device has)
constexpr double kSingular = 1e-300;            // a pivot this small: the matrix can't be inverted
constexpr uint32_t kSequenceWrap = 4096;        // (the directions' sequence index wraps: float precision)

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares: the push constants (their meaning per pass in PassConstants below) and the guide's packing -
// the world position's three floats and the normal (octahedral, two 16-bit numbers from 1: 0 means no surface).
constexpr const char* kCommon = R"(
layout(push_constant) uniform Pass {
    mat4 matrix;
    ivec4 rect;
    vec4 camera;
    vec4 params;
    ivec4 extra;
} pc;

vec2 sign_of(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

uint pack_normal(vec3 n)
{
    vec2 o = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        o = (1.0 - abs(o.yx)) * sign_of(o);
    }
    uvec2 q = uvec2(round(clamp(o * 0.5 + 0.5, 0.0, 1.0) * 65534.0)) + uvec2(1u);
    return q.x | (q.y << 16);
}

vec3 unpack_normal(uint v)
{
    vec2 o = (vec2(float(v & 0xFFFFu), float(v >> 16)) - 1.0) / 65534.0 * 2.0 - 1.0;
    vec3 n = vec3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * sign_of(n.xy);
    }
    return normalize(n);
}

bool inside(ivec2 local) { return all(greaterThanEqual(local, ivec2(0))) && all(lessThan(local, pc.rect.zw)); }

// How far apart two points on one surface may be from its plane (world units) at `distance` from the camera: the
// depth's precision falls with distance.
float plane_tolerance(float distance) { return 0.5 + 0.005 * distance + 1.2e-7 * distance * distance; }
)";

// Step 1: the rays. matrix: (pixel x, pixel y, depth, 1) -> world (the target's pixels); rect: the viewport; camera: the
// camera's position (w: the depth sample's offset from the pixel's centre, y); params: x the rays' length, y rays, z the
// offset's scale, w the depth sample's offset (x); extra: x the frame's index, y the mask.
constexpr const char* kTraceBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform writeonly uimage2D o_guide;

float depth_at(ivec2 p) { return texelFetch(u_depth, p, 0).r; }

// (a multisampled depth is read at sample 0, which isn't at the pixel's centre: the point is where the sample is)
vec3 world_at(ivec2 p, float d)
{
    vec4 w = pc.matrix * vec4(vec2(p) + vec2(pc.params.w, pc.camera.w), d, 1.0);
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

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float occluded(vec3 origin, vec3 direction)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          uint(pc.extra.y) & 0xFFu, origin, 0.0, direction, pc.params.x);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.0 : 1.0;
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    float d = depth_at(p);
    if (d >= 1.0) {
        imageStore(o_out, local, vec4(0.0));
        imageStore(o_guide, local, uvec4(0u));
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
    float offset = pc.params.z * (1.0 + distance * 0.0005 + distance * distance * 6e-8);
    vec3 origin = here + normal * offset;
    // the hemisphere's frame (an orthonormal basis without a branch on the normal's direction)
    float s = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + normal.z);
    float b = normal.x * normal.y * a;
    vec3 across = vec3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
    vec3 up = vec3(b, s + normal.y * normal.y * a, -normal.y);
    uint seed = hash(uint(local.x) + hash(uint(local.y)));
    vec2 turn = vec2(float(hash(seed) >> 8u), float(hash(seed + 1u) >> 8u)) * (1.0 / 16777216.0);
    uint rays = uint(pc.params.y);
    float hits = 0.0;
    for (uint i = 0u; i < rays; ++i) {
        uint n = (uint(pc.extra.x) * rays + i) & 4095u;
        vec2 u = fract(turn + float(n) * vec2(0.7548776662, 0.5698402910));
        float r = sqrt(u.x);
        float phi = 6.2831853 * u.y;
        vec3 direction = across * (r * cos(phi)) + up * (r * sin(phi)) + normal * sqrt(max(0.0, 1.0 - u.x));
        hits += occluded(origin, direction);
    }
    imageStore(o_out, local, vec4(hits / float(rays), 0.0, 0.0, 0.0));
    imageStore(o_guide, local, uvec4(floatBitsToUint(here), pack_normal(normal)));
}
)";

// Step 2: the history. matrix: world -> the previous frame's pixel (the target's, homogeneous); camera: the camera's
// position; params: x the longest history, y whether there is one (1), z rays.
constexpr const char* kTemporalBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform readonly uimage2D u_guide;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 5, rgba32ui) uniform readonly uimage2D u_guide_before;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D u_history;

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    uvec4 guide = imageLoad(u_guide, local);
    float now = imageLoad(u_in, local).r;
    if (guide.w == 0u) {
        imageStore(o_out, local, vec4(0.0));
        return;
    }
    vec3 here = uintBitsToFloat(guide.xyz);
    vec3 normal = unpack_normal(guide.w);
    float tolerance = plane_tolerance(length(pc.camera.xyz - here));
    // this frame's spread around the pixel (its surface's neighbours): what the history may be
    float sum = 0.0;
    float squares = 0.0;
    float count = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = local + ivec2(dx, dy);
            if (!inside(q) || imageLoad(u_guide, q).w == 0u) {
                continue;
            }
            float v = imageLoad(u_in, q).r;
            sum += v;
            squares += v * v;
            count += 1.0;
        }
    }
    float mean = sum / count;
    float spread = sqrt(max(0.0, squares / count - mean * mean));
    float noise = 0.5 / sqrt(max(1.0, pc.params.z * count));
    float low = mean - 1.5 * spread - noise;
    float high = mean + 1.5 * spread + noise;
    float history = 0.0;
    float history_length = 0.0;
    if (pc.params.y > 0.5) {
        vec4 before = pc.matrix * vec4(here, 1.0);
        if (before.w > 0.0) {
            vec2 at = before.xy / before.w - vec2(pc.rect.xy);
            ivec2 base = ivec2(floor(at));
            vec2 f = at - vec2(base);
            float weights = 0.0;
            float values = 0.0;
            float lengths = 0.0;
            for (int corner = 0; corner < 4; ++corner) {
                ivec2 q = base + ivec2(corner & 1, corner >> 1);
                float w = ((corner & 1) != 0 ? f.x : 1.0 - f.x) * ((corner >> 1) != 0 ? f.y : 1.0 - f.y);
                if (w <= 0.0 || !inside(q)) {
                    continue;
                }
                uvec4 was = imageLoad(u_guide_before, q);
                if (was.w == 0u) {
                    continue;
                }
                vec3 there = uintBitsToFloat(was.xyz);
                if (abs(dot(normal, there - here)) > tolerance || dot(normal, unpack_normal(was.w)) < 0.9) {
                    continue;
                }
                vec4 kept = imageLoad(u_history, q);
                weights += w;
                values += w * kept.r;
                lengths += w * kept.g;
            }
            if (weights > 1e-3) {
                history = values / weights;
                history_length = lengths / weights;
            }
        }
    }
    history = clamp(history, low, high);
    history_length = min(floor(history_length + 0.5) + 1.0, pc.params.x);
    float ao = clamp(mix(history, now, 1.0 / history_length), 0.0, 1.0);
    imageStore(o_out, local, vec4(ao, history_length, 0.0, 0.0));
}
)";

// Step 3: one a-trous pass. camera: the camera's position; params: z rays; extra: x the step (pixels).
constexpr const char* kFilterBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32ui) uniform readonly uimage2D u_guide;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;

const float kKernel[3] = float[3](0.375, 0.25, 0.0625);

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(local)) {
        return;
    }
    uvec4 guide = imageLoad(u_guide, local);
    vec4 centre = imageLoad(u_in, local);
    if (guide.w == 0u) {
        imageStore(o_out, local, centre);
        return;
    }
    vec3 here = uintBitsToFloat(guide.xyz);
    vec3 normal = unpack_normal(guide.w);
    float tolerance = plane_tolerance(length(pc.camera.xyz - here));
    // the noise a history this long leaves (a share of rays: at most 0.5 / sqrt(rays) a frame), with a margin
    float noise = 2.0 * 0.5 / sqrt(max(1.0, max(centre.g, 1.0) * pc.params.z)) + 1e-3;
    float weights = 0.0;
    float sum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 q = local + ivec2(dx, dy) * pc.extra.x;
            if (!inside(q)) {
                continue;
            }
            uvec4 there_guide = imageLoad(u_guide, q);
            if (there_guide.w == 0u) {
                continue;
            }
            float value = imageLoad(u_in, q).r;
            vec3 there = uintBitsToFloat(there_guide.xyz);
            float w = kKernel[abs(dx)] * kKernel[abs(dy)];
            w *= pow(max(0.0, dot(normal, unpack_normal(there_guide.w))), 32.0);
            w *= exp(-abs(dot(normal, there - here)) / tolerance);
            w *= exp(-abs(value - centre.r) / noise);
            weights += w;
            sum += w * value;
        }
    }
    imageStore(o_out, local, vec4(weights > 0.0 ? sum / weights : centre.r, centre.g, 0.0, 0.0));
}
)";

// Step 4: the colour multiplied (a multiplying blend). rect: the viewport; params: x the strength.
constexpr const char* kApplyBody = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(location = 0) out vec4 o_factor;

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    float ao = inside(local) ? clamp(imageLoad(u_in, local).r, 0.0, 1.0) : 0.0;
    o_factor = vec4(vec3(1.0 - pc.params.x * ao), 1.0);
}
)";

// The push constants, as the shaders' block lays them out.
struct PassConstants {
    std::array<float, kMatrixFloats> matrix{}; // column-major (GLSL's mat4)
    std::array<int32_t, 4> rect{};
    std::array<float, 4> camera{};
    std::array<float, 4> params{};
    std::array<int32_t, 4> extra{};
};
static_assert(sizeof(PassConstants) == kPushConstantBytes, "Vulkan's guaranteed push-constant space");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The bindings: 0 the scene, 1 the depth, 2 - 6 storage images (2 written; 3 the guide - written by the rays, read
// after; 4 the input; 5 the guide before; 6 the history before).
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOut = 2;
constexpr uint32_t kBindingGuide = 3;
constexpr uint32_t kBindingIn = 4;
constexpr uint32_t kBindingGuideBefore = 5;
constexpr uint32_t kBindingHistory = 6;
constexpr uint32_t kBindingCount = 7;

// A pipeline for a colour format and sample count (the apply pass).
struct ApplyPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

constexpr std::size_t kSlotImages = 7; // (a viewport's: two guides, the rays', two histories, two filtered)

// A viewport's images and history.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    std::array<Image, 2> guides;    // this frame's and the one before (by `current`)
    Image raw;                      // step 1
    std::array<Image, 2> histories; // step 2's, this frame's and the one before
    std::array<Image, 2> filtered;  // step 3's ping-pong
    uint32_t current = 0;           // which of guides / histories this frame writes
    bool fresh = true;              // made, not cleared yet
    bool have_before = false;       // a frame before whose history and camera can be used
    uint64_t frame_drawn = 0;       // Backend::frame when last drawn
    uint64_t passes = 0;            // the frame index of the directions
    Matrix world_to_pixel{};        // the camera of the frame before
    Image* applied = nullptr;       // what the last apply read
    Image* history_written = nullptr;
    uint64_t last_use = 0;
};

struct EffectsState {
    RtAoSettings settings;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> trace{}; // single-sampled depth, multisampled
    VkPipeline temporal = VK_NULL_HANDLE;
    VkPipeline filter = VK_NULL_HANDLE;
    std::vector<ApplyPipeline> applies;
    std::vector<std::unique_ptr<ViewSlot>> slots;
    bool standard_samples = false; // the device puts multisampled targets' samples where Vulkan's standard says // (kept where they are: the slots' images are pointed at)
    bool failed = false; // the shaders or layouts couldn't be made (not tried again)
    uint64_t passes = 0;
    uint64_t uses = 0;
};

EffectsState& effects()
{
    static EffectsState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's opaque world is drawn.
class EffectsListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kOcclusion; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_ao_pass(state, view);
        }
    }
};

EffectsListener& listener()
{
    static EffectsListener instance;
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

// A pass's GLSL: the version, the defines, the ray-query extension where it traces, what all share, its body.
std::string source_of(const char* body, bool traces, bool multisampled)
{
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    if (traces) {
        source += "#extension GL_EXT_ray_query : require\n";
    }
    source += kCommon;
    source += body;
    return source;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const VkShaderModule module = make_module(device, RtShaderStage::kCompute, source, error);
    if (module == VK_NULL_HANDLE) {
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

// The layouts, the sampler, the vertex shader and the compute pipelines (once).
bool ensure_objects(Backend& state, std::string* error)
{
    EffectsState& own = effects();
    if (own.filter != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    own.standard_samples = properties.limits.standardSampleLocations == VK_TRUE;
    if (own.layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            if (binding == kBindingScene) {
                type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            } else if (binding == kBindingDepth) {
                type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            core::at(bindings, binding) = {.binding = binding,
                                           .descriptorType = type,
                                           .descriptorCount = 1,
                                           .stageFlags = kStages,
                                           .pImmutableSamplers = nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                       .pBindings = bindings.data()};
        const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(PassConstants)};
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
    }
    if (own.vertex == VK_NULL_HANDLE) {
        own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
        if (own.vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t multisampled = 0; multisampled < own.trace.size(); ++multisampled) {
        if (core::at(own.trace, multisampled) == VK_NULL_HANDLE) {
            core::at(own.trace, multisampled) =
                make_compute(device, own.layout, source_of(kTraceBody, true, multisampled != 0), error);
            if (core::at(own.trace, multisampled) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    if (own.temporal == VK_NULL_HANDLE) {
        own.temporal = make_compute(device, own.layout, source_of(kTemporalBody, false, false), error);
        if (own.temporal == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.filter = make_compute(device, own.layout, source_of(kFilterBody, false, false), error);
    return own.filter != VK_NULL_HANDLE;
}

// The apply pass's pipeline for a colour target (made on first use).
VkPipeline apply_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    EffectsState& own = effects();
    for (const ApplyPipeline& made : own.applies) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        make_module(device, RtShaderStage::kFragment, source_of(kApplyBody, false, false), error);
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
    own.applies.push_back({.format = format, .samples = samples, .pipeline = pipeline});
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

// The matrix (column vectors) that takes world space to (pixel x, pixel y, depth, 1) in the target's pixels: view x
// projection (their product transposed: Direct3D's row vectors), then clip space to Direct3D 9's pixels (centres at
// whole coordinates, y down) and the viewport's depth range. pixel_to_world is its inverse.
Matrix world_to_pixel(const rt::ViewDrawn& view)
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
    const double x = core::at(view.rect, 0);
    const double y = core::at(view.rect, 1);
    const double width = std::max(1U, core::at(view.rect, 2));
    const double height = std::max(1U, core::at(view.rect, 3));
    const double near_z = core::at(view.depth_range, 0);
    const double depth_span = std::max(1e-6, static_cast<double>(core::at(view.depth_range, 1)) - near_z);
    const Matrix pixel_from_clip{{{width / 2.0, 0.0, 0.0, x + (width / 2.0)},
                                  {0.0, -height / 2.0, 0.0, y + (height / 2.0)},
                                  {0.0, 0.0, depth_span, near_z},
                                  {0.0, 0.0, 0.0, 1.0}}};
    return multiply(pixel_from_clip, clip_from_world);
}

std::array<float, kMatrixFloats> column_major(const Matrix& matrix)
{
    std::array<float, kMatrixFloats> out{};
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            core::at(out, (column * kMatrixSide) + row) = static_cast<float>(core::at(core::at(matrix, row), column));
        }
    }
    return out;
}

// --- the viewports' images ---------------------------------------------------------------------------------------

std::array<Image*, kSlotImages> images_of(ViewSlot& slot)
{
    return {&slot.guides.front(),    &slot.guides.back(),     &slot.raw,           &slot.histories.front(),
            &slot.histories.back(), &slot.filtered.front(), &slot.filtered.back()};
}

void release_slot(Backend& state, ViewSlot* slot, bool now)
{
    for (Image* image : images_of(*slot)) {
        if (now) {
            state.memory.destroy_image(image);
        } else {
            retire_image(state, image);
        }
    }
}

bool make_storage_image(Backend& state, VkFormat format, const Extent& extent, Image* out)
{
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, out)) {
        return false;
    }
    out->resting = VK_IMAGE_LAYOUT_GENERAL; // (storage images: read and written in place)
    return true;
}

// The slot for a viewport (made, or made again when the size changed; the least recently used one dropped when all are
// taken); null when its images can't be made.
ViewSlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, const Extent& target)
{
    EffectsState& own = effects();
    ++own.uses;
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == target.width && slot->target.height == target.height) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kAoViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<ViewSlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<ViewSlot>();
    ViewSlot& slot = *made_slot;
    slot.rect = rect;
    slot.target = target;
    slot.last_use = own.uses;
    const Extent extent{.width = core::at(rect, 2), .height = core::at(rect, 3)};
    bool made = true;
    for (Image& guide : slot.guides) {
        made = made && make_storage_image(state, VK_FORMAT_R32G32B32A32_UINT, extent, &guide);
    }
    for (Image* image : {&slot.raw, &slot.histories.front(), &slot.histories.back(), &slot.filtered.front(),
                         &slot.filtered.back()}) {
        made = made && make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, image);
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_ao: the viewport's images (%ux%u) couldn't be made - the pass skipped there",
                    extent.width, extent.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

// The new images moved to GENERAL and cleared (no NaN read from an unwritten history), before the passes.
void clear_slot(VkCommandBuffer commands, ViewSlot* slot)
{
    const std::array<Image*, kSlotImages> images = images_of(*slot);
    std::array<VkImageMemoryBarrier, kSlotImages> barriers{};
    for (std::size_t i = 0; i < images.size(); ++i) {
        core::at(barriers, i) = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = 0,
                                 .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                 .image = core::at(images, i)->image,
                                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                      .baseMipLevel = 0,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = 0,
                                                      .layerCount = 1}};
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    const VkClearColorValue zero{};
    for (Image* image : images) {
        vkCmdClearColorImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
        image->layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    const VkMemoryBarrier cleared{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &cleared, 0,
                         nullptr, 0, nullptr);
    slot->fresh = false;
}

// One pass's descriptors: the scene, the depth and the storage images it uses (null: not written).
struct PassImages {
    bool scene = false;
    const Image* depth = nullptr;
    const Image* out = nullptr;
    const Image* guide = nullptr;
    const Image* in = nullptr;
    const Image* guide_before = nullptr;
    const Image* history = nullptr;
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const PassImages& images)
{
    const EffectsState& own = effects();
    const VkWriteDescriptorSetAccelerationStructureKHR scene{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &own.scene};
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        return write;
    };
    if (images.scene) {
        add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR).pNext = &scene;
    }
    if (images.depth != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingDepth);
        info = {.sampler = own.sampler,
                .imageView = images.depth->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const std::array<std::pair<uint32_t, const Image*>, 5> storage{{{kBindingOut, images.out},
                                                                    {kBindingGuide, images.guide},
                                                                    {kBindingIn, images.in},
                                                                    {kBindingGuideBefore, images.guide_before},
                                                                    {kBindingHistory, images.history}}};
    for (const auto& [binding, image] : storage) {
        if (image == nullptr) {
            continue;
        }
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &info;
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

// A compute pass's writes before the next pass reads or writes them (and the apply's fragment shader reads them).
void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const PassImages& images, const PassConstants& constants)
{
    const EffectsState& own = effects();
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, images);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(PassConstants), &constants);
    const auto groups = [](int32_t pixels) {
        return (static_cast<uint32_t>(pixels) + kGroupSide - 1) / kGroupSide;
    };
    vkCmdDispatch(commands, groups(core::at(constants.rect, 2)), groups(core::at(constants.rect, 3)), 1);
    compute_done(commands);
}

// The depth moved to be sampled by the compute passes (from whatever wrote it) and back to its resting layout after.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_compute)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
                                    : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .oldLayout = depth->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth->image,
        .subresourceRange =
            {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_compute ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         to_compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    depth->layout = layout;
}

// The colour multiplied by the occlusion in `ao` over the viewport.
void record_apply(VkCommandBuffer commands, VkPipeline pipeline, Image* colour, const Image* ao,
                  const PassConstants& constants)
{
    const EffectsState& own = effects();
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{
        .offset = {.x = core::at(constants.rect, 0), .y = core::at(constants.rect, 1)},
        .extent = {.width = static_cast<uint32_t>(core::at(constants.rect, 2)),
                   .height = static_cast<uint32_t>(core::at(constants.rect, 3))}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.in = ao});
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// Where sample 0 of a multisampled target is, from the pixel's centre (Vulkan's standard sample locations; the centre
// when the device doesn't use them, or for one sample).
std::array<float, 2> sample_zero_offset(VkSampleCountFlagBits samples, bool standard)
{
    if (!standard) {
        return {0.0F, 0.0F};
    }
    // sample 0 in the pixel (0 - 1) as the standard places it for 2, 4, 8 and 16 samples
    constexpr std::array<float, 2> kTwo{0.75F, 0.75F};
    constexpr std::array<float, 2> kFour{0.375F, 0.125F};
    constexpr std::array<float, 2> kEight{0.5625F, 0.3125F};
    constexpr std::array<float, 2> kSixteen{0.5625F, 0.5625F};
    constexpr float kCentre = 0.5F;
    const auto from_centre = [](const std::array<float, 2>& at) {
        return std::array<float, 2>{at.front() - kCentre, at.back() - kCentre};
    };
    switch (samples) {
    case VK_SAMPLE_COUNT_2_BIT:
        return from_centre(kTwo);
    case VK_SAMPLE_COUNT_4_BIT:
        return from_centre(kFour);
    case VK_SAMPLE_COUNT_8_BIT:
        return from_centre(kEight);
    case VK_SAMPLE_COUNT_16_BIT:
        return from_centre(kSixteen);
    default:
        return {0.0F, 0.0F};
    }
}

// IEEE half to float (the read-back of the 16-bit images).
float from_half(uint16_t half)
{
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr int kBias = 15;
    const uint32_t sign = (static_cast<uint32_t>(half) >> 15U) & 1U;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(static_cast<float>(mantissa | (1U << kExponentShift)),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return sign != 0 ? -value : value;
}

} // namespace

void set_rt_ao_settings(const RtAoSettings& settings)
{
    RtAoSettings kept = settings;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : 0.0F;
    kept.radius = std::isfinite(kept.radius) ? std::clamp(kept.radius, kMinRadius, kMaxRadius) : kRtAoRadius;
    kept.rays = std::clamp(kept.rays, 1U, kMaxRays);
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        effects().settings = kept;
        rt_effects_reset_history(state);
    }
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtAoSettings rt_ao_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return effects().settings;
}

void rt_effects_set_scene(VkAccelerationStructureKHR tlas)
{
    const std::scoped_lock lock(backend().mutex);
    rt_effects_set_scene_locked(tlas);
}

void rt_effects_set_scene_locked(VkAccelerationStructureKHR tlas) { effects().scene = tlas; }

VkImageUsageFlags rt_effects_depth_usage(const Backend& state, VkFormat format)
{
    if (!effects().settings.on || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

void rt_effects_reset_history(Backend& state)
{
    (void)state;
    for (const std::unique_ptr<ViewSlot>& slot : effects().slots) {
        slot->have_before = false;
        slot->passes = 0;
    }
}

bool record_rt_ao_pass(Backend& state, const rt::ViewDrawn& view)
{
    EffectsState& own = effects();
    if (!own.settings.on || own.failed) {
        return false;
    }
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing)) {
            get_log("vulkan: rt_ao=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device with "
                    "VK_KHR_ray_query) - no ray-traced ambient occlusion");
        }
        return false;
    }
    if (own.scene == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene)) {
            get_log("vulkan: rt_ao: no acceleration structure for the scene yet - the ambient occlusion pass skipped");
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
            get_log("vulkan: rt_ao: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the ambient occlusion pass skipped there");
        }
        return false;
    }
    std::string error;
    const VkPipeline apply =
        ensure_objects(state, &error) ? apply_pipeline(state, colour->format, colour->samples, &error) : VK_NULL_HANDLE;
    if (apply == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_ao: the passes couldn't be made (%s) - no ray-traced ambient occlusion", error.c_str());
        }
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
    rt::ViewDrawn clipped = view;
    clipped.rect = {left, top, width, height};
    const Matrix to_pixel = world_to_pixel(clipped);
    Matrix to_world{};
    if (!invert(to_pixel, &to_world)) {
        return false;
    }
    ViewSlot* const slot = slot_for(state, clipped.rect, colour->extent);
    if (slot == nullptr) {
        return false;
    }
    const bool denoise = own.settings.denoise;
    const bool have_before = denoise && slot->have_before && slot->frame_drawn + 1 >= state.frame;
    const uint32_t now = slot->current;
    const uint32_t before = now ^ 1U;

    VkCommandBuffer commands = outside_pass(state);
    if (slot->fresh) {
        clear_slot(commands, slot);
    }
    // the scene's build (recorded earlier on this queue) and the last frame's uses of the images before this one's
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask =
                                    VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);

    PassConstants constants{};
    constants.rect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width),
                      static_cast<int32_t>(height)};
    constants.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                        core::at(view.camera.position, 2), 0.0F};
    const auto rays = static_cast<float>(own.settings.rays);

    // 1. the rays
    PassConstants trace = constants;
    trace.matrix = column_major(to_world);
    const std::array<float, 2> sample = sample_zero_offset(colour->samples, own.standard_samples);
    trace.params = {own.settings.radius, rays, kOffsetScale, sample.front()};
    core::at(trace.camera, 3) = sample.back();
    trace.extra = {static_cast<int32_t>(slot->passes % kSequenceWrap), static_cast<int32_t>(kAoMask), 0, 0};
    const Image* const guide = &core::at(slot->guides, now);
    dispatch(commands, core::at(own.trace, colour->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U),
             {.scene = true, .depth = depth, .out = &slot->raw, .guide = guide}, trace);
    Image* result = &slot->raw;
    if (denoise) {
        // 2. the history
        PassConstants temporal = constants;
        temporal.matrix = column_major(slot->world_to_pixel);
        temporal.params = {static_cast<float>(kAoMaxHistory), have_before ? 1.0F : 0.0F, rays, 0.0F};
        Image* const history = &core::at(slot->histories, now);
        dispatch(commands, own.temporal,
                 {.out = history,
                  .guide = guide,
                  .in = &slot->raw,
                  .guide_before = &core::at(slot->guides, before),
                  .history = &core::at(slot->histories, before)},
                 temporal);
        slot->history_written = history;
        // 3. the spatial filter
        result = history;
        PassConstants filter = constants;
        filter.params = {0.0F, 0.0F, rays, 0.0F};
        for (uint32_t pass = 0; pass < kAoFilterPasses; ++pass) {
            Image* const target = &core::at(slot->filtered, pass % 2);
            filter.extra = {static_cast<int32_t>(1U << pass), 0, 0, 0};
            dispatch(commands, own.filter, {.out = target, .guide = guide, .in = result}, filter);
            result = target;
        }
    } else {
        slot->history_written = nullptr;
    }
    depth_barrier(commands, depth, depth->resting, false);

    // 4. the colour multiplied
    PassConstants apply_constants = constants;
    apply_constants.params = {own.settings.strength, 0.0F, 0.0F, 0.0F};
    record_apply(commands, apply, colour, result, apply_constants);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    for (Image* image : images_of(*slot)) {
        image->used = serial;
    }
    slot->applied = result;
    slot->world_to_pixel = to_pixel;
    slot->have_before = denoise;
    slot->frame_drawn = state.frame;
    slot->current = before;
    ++slot->passes;
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_ao: the first ambient occlusion pass (%ux%u, %u rays, radius %.1f, strength %.2f, %s)",
                width, height, own.settings.rays, static_cast<double>(own.settings.radius),
                static_cast<double>(own.settings.strength), denoise ? "denoised" : "not denoised");
    }
    return true;
}

uint64_t rt_ao_passes() { return effects().passes; }

bool read_rt_ao_image(Backend& state, const std::array<uint32_t, 4>& rect, RtAoImage which, RtAoTexels* out)
{
    *out = RtAoTexels{};
    for (const std::unique_ptr<ViewSlot>& slot : effects().slots) {
        if (slot->rect != rect) {
            continue;
        }
        Image* image = &slot->raw;
        if (which == RtAoImage::kHistory) {
            image = slot->history_written;
        } else if (which == RtAoImage::kFinal) {
            image = slot->applied;
        }
        if (image == nullptr || !finish_work(state)) {
            return false;
        }
        constexpr std::size_t kChannels = 4;
        const std::size_t texels = static_cast<std::size_t>(image->extent.width) * image->extent.height;
        std::vector<uint8_t> bytes(texels * kChannels * sizeof(uint16_t));
        if (!state.memory.read_image_level(image, 0, &bytes)) {
            return false;
        }
        out->width = image->extent.width;
        out->height = image->extent.height;
        out->texels.resize(texels);
        for (std::size_t i = 0; i < texels; ++i) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(bytes, ((i * kChannels) + channel) * sizeof(uint16_t)), sizeof(half));
                core::at(core::at(out->texels, i), channel) = from_half(half);
            }
        }
        return true;
    }
    return false;
}

void release_rt_effects(Backend& state)
{
    EffectsState& own = effects();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const ApplyPipeline& made : own.applies) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {own.trace.front(), own.trace.back(), own.temporal, own.filter}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
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
    own.applies.clear();
    own.trace = {};
    own.temporal = VK_NULL_HANDLE;
    own.filter = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.failed = false;
}

} // namespace render::vulkan
