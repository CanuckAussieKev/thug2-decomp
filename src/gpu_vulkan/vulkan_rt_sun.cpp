// NOT ORIGINAL - the sun, its glare, screen-space rays and the bloom (vulkan_rt_sun.h; docs/design/vulkan.md "The sun,
// its glare and the sky").
#include "gpu_vulkan/vulkan_rt_sun.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportTargets = 0x53550001U;
constexpr uint64_t kReportPipeline = 0x53550002U;
constexpr uint64_t kReportFirstPass = 0x53550003U;
constexpr uint64_t kReportImages = 0x53550004U;
constexpr uint64_t kReportNoRoom = 0x53550005U;
constexpr uint64_t kReportNoSun = 0x53550006U;

constexpr uint32_t kGroupSide = 8;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr double kSingular = 1e-300;
constexpr float kMinSize = 0.25F;
constexpr float kMaxSize = 4.0F;
constexpr float kMaxBloom = 0.5F;
constexpr float kMaxShaft = 4.0F;
constexpr double kInFront = 1e-9;
constexpr double kDegreesPerRadian = 57.29577951308232;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares: the block (SunBlock below) and the level's push constants.
constexpr const char* kCommon = R"(
layout(set = 0, binding = 6) uniform SunBlock {
    mat4 to_world;      // the target's pixel (x, y, depth, 1) -> world
    vec4 camera;        // xyz
    vec4 sun;           // xyz toward the sun; w 1: there is one in front
    vec4 sun_colour;    // rgb its colour; w the disc's intensity
    vec4 sun_screen;    // xy its place (the target's pixels, centres whole); z 1 in front; w the rays' fade
    vec4 disc;          // x its angular radius, y a pixel's angle, z the near glow's width, w the wide glow's
    vec4 aureole;       // x the near glow's share, y the wide glow's
    vec4 shafts;        // x strength, y decay, z the reach, w the source's width (radians)
    vec4 bloom;         // x the share of the frame's colour spread, y of the light added, z lens dirt, w 1: kept above 1 (rt_hdr)
    ivec4 rect;         // the viewport in the target's pixels
    ivec4 low;          // xy the half-resolution images' size, z the rays' points, w the bloom's levels
    ivec4 flags;        // x the colour's samples, y the disc, z the rays, w the bloom
} sun;

layout(push_constant) uniform Level {
    ivec4 size;    // xy the level written's size, zw the level read's
    vec4 weights;  // x this level's weight, y the lower level's factor
} level;

vec3 direction_of(vec2 p)
{
    vec4 w = sun.to_world * vec4(p, 0.5, 1.0);
    vec3 to = w.xyz / w.w - sun.camera.xyz;
    float l = length(to);
    return l > 1e-12 ? to / l : vec3(0.0, 0.0, -1.0);
}

// the angle between the view ray through p and the way to the sun (accurate near 0)
float angle_to_sun(vec2 p)
{
    vec3 d = direction_of(p);
    return atan(length(cross(d, sun.sun.xyz)), dot(d, sun.sun.xyz));
}

vec3 finite_colour(vec3 c)
{
    return mix(vec3(0.0), min(c, vec3(65504.0)), greaterThanEqual(c, vec3(0.0)));
}
)";

// Step 1: the rays (half resolution).
constexpr const char* kShaftsBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_a;

float jitter_at(vec2 p) { return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y)); }

// a point's light: the sky's (nothing in front of the sun there), brighter nearer the sun
float source_at(vec2 local)
{
    ivec2 at = clamp(ivec2(floor(local + 0.5)), ivec2(0), sun.rect.zw - ivec2(1));
    if (texelFetch(u_depth, sun.rect.xy + at, 0).r < 1.0) {
        return 0.0;
    }
    return exp(-angle_to_sun(vec2(sun.rect.xy) + local) / sun.shafts.w);
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, sun.low.xy))) {
        return;
    }
    vec2 at = vec2(q) * 2.0 + 0.5;
    vec2 toward = sun.sun_screen.xy - vec2(sun.rect.xy);
    int n = sun.low.z;
    vec2 delta = (toward - at) * (sun.shafts.z / float(n));
    float u = jitter_at(vec2(q));
    float sum = 0.0;
    float weights = 0.0;
    float w = 1.0;
    for (int i = 0; i < n; ++i) {
        sum += w * source_at(at + delta * (float(i) + u));
        weights += w;
        w *= sun.shafts.y;
    }
    float value = weights > 0.0 ? sum / weights : 0.0;
    imageStore(o_a, q, vec4(sun.sun_colour.rgb * (value * sun.shafts.x * sun.sun_screen.w), 1.0));
}
)";

// Step 2: the source (full resolution): the frame's colour, and the light added.
constexpr const char* kSourceBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_colour;
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 0) uniform sampler2D u_colour;
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_b;
layout(set = 0, binding = 4) uniform sampler2D u_a;

void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, sun.rect.zw))) {
        return;
    }
    ivec2 at = sun.rect.xy + p;
    vec3 colour = vec3(0.0);
    float sky = 0.0;
#ifdef MULTISAMPLED
    int samples = max(sun.flags.x, 1);
    for (int s = 0; s < samples; ++s) {
        colour += finite_colour(texelFetch(u_colour, at, s).rgb);
        sky += texelFetch(u_depth, at, s).r >= 1.0 ? 1.0 : 0.0;
    }
    colour /= float(samples);
    sky /= float(samples);
#else
    colour = finite_colour(texelFetch(u_colour, at, 0).rgb);
    sky = texelFetch(u_depth, at, 0).r >= 1.0 ? 1.0 : 0.0;
#endif
    vec3 light = vec3(0.0);
    if (sun.flags.y != 0 && sky > 0.0) {
        float theta = angle_to_sun(vec2(at));
        float radius = sun.disc.x;
        float edge = clamp((radius - theta) / max(sun.disc.y, 1e-6) + 0.5, 0.0, 1.0);
        float t = min(theta / radius, 1.0);
        float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - t * t, 0.0)));
        float glow = sun.aureole.x * exp(-theta / sun.disc.z) + sun.aureole.y * exp(-theta / sun.disc.w);
        light += sky * sun.sun_colour.rgb * (sun.sun_colour.w * limb * edge + glow);
    }
    if (sun.flags.z != 0) {
        light += textureLod(u_a, (vec2(p) + 0.5) / (2.0 * vec2(sun.low.xy)), 0.0).rgb;
    }
    imageStore(o_a, p, vec4(colour, 1.0));
    imageStore(o_b, p, vec4(light, sky));
}
)";

// Step 3a: a level down (Jimenez's 13 taps, bilinear: weights summing to 1). FIRST: from the source's two images (what
// is spread: the frame's colour x bloom.x + the light x bloom.y).
constexpr const char* kDownBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 4) uniform sampler2D u_a;
#ifdef FIRST
layout(set = 0, binding = 5) uniform sampler2D u_b;
vec3 fetch(vec2 uv)
{
    vec3 light = textureLod(u_b, uv, 0.0).rgb;
    return sun.bloom.x * textureLod(u_a, uv, 0.0).rgb + sun.bloom.y * light;
}
#else
vec3 fetch(vec2 uv) { return textureLod(u_a, uv, 0.0).rgb; }
#endif

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, level.size.xy))) {
        return;
    }
    // (the level's texel covers 2 x 2 of the one above: its centre, in that one's coordinates)
    vec2 t = 1.0 / vec2(level.size.zw);
    vec2 uv = (vec2(q) * 2.0 + 1.0) * t;
    vec3 a = fetch(uv + t * vec2(-2.0, -2.0));
    vec3 b = fetch(uv + t * vec2(0.0, -2.0));
    vec3 c = fetch(uv + t * vec2(2.0, -2.0));
    vec3 d = fetch(uv + t * vec2(-2.0, 0.0));
    vec3 e = fetch(uv);
    vec3 f = fetch(uv + t * vec2(2.0, 0.0));
    vec3 g = fetch(uv + t * vec2(-2.0, 2.0));
    vec3 h = fetch(uv + t * vec2(0.0, 2.0));
    vec3 i = fetch(uv + t * vec2(2.0, 2.0));
    vec3 j = fetch(uv + t * vec2(-1.0, -1.0));
    vec3 k = fetch(uv + t * vec2(1.0, -1.0));
    vec3 l = fetch(uv + t * vec2(-1.0, 1.0));
    vec3 m = fetch(uv + t * vec2(1.0, 1.0));
    vec3 value = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    imageStore(o_a, q, vec4(finite_colour(value), 1.0));
}
)";

// Step 3b: a level up: this level's own (x its weight) + the one below it upsampled (a 3 x 3 tent, x its factor).
constexpr const char* kUpBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 4) uniform sampler2D u_a;
layout(set = 0, binding = 7, rgba16f) uniform readonly image2D i_a;

vec3 tent(vec2 uv, vec2 t)
{
    vec3 sum = textureLod(u_a, uv, 0.0).rgb * 4.0;
    sum += (textureLod(u_a, uv + vec2(-t.x, 0.0), 0.0).rgb + textureLod(u_a, uv + vec2(t.x, 0.0), 0.0).rgb +
            textureLod(u_a, uv + vec2(0.0, -t.y), 0.0).rgb + textureLod(u_a, uv + vec2(0.0, t.y), 0.0).rgb) * 2.0;
    sum += textureLod(u_a, uv - t, 0.0).rgb + textureLod(u_a, uv + t, 0.0).rgb +
           textureLod(u_a, uv + vec2(t.x, -t.y), 0.0).rgb + textureLod(u_a, uv + vec2(-t.x, t.y), 0.0).rgb;
    return sum / 16.0;
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, level.size.xy))) {
        return;
    }
    // (this level's texel centre in the one below's coordinates: the lower is half the size)
    vec2 uv = (vec2(q) + 0.5) / (2.0 * vec2(level.size.zw));
    vec3 lower = tent(uv, 1.0 / vec2(level.size.zw)) * level.weights.y;
    vec3 here = imageLoad(i_a, q).rgb * level.weights.x;
    imageStore(o_a, q, vec4(here + lower, 1.0));
}
)";

// Step 4: the composite (writes the colour, alpha kept): frame + light + s (B - X).
constexpr const char* kCompositeBody = R"(
layout(set = 0, binding = 4) uniform sampler2D u_a;
layout(set = 0, binding = 7, rgba16f) uniform readonly image2D i_a;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D i_b;
layout(location = 0) out vec4 o_colour;

vec3 tent(vec2 uv, vec2 t)
{
    vec3 sum = textureLod(u_a, uv, 0.0).rgb * 4.0;
    sum += (textureLod(u_a, uv + vec2(-t.x, 0.0), 0.0).rgb + textureLod(u_a, uv + vec2(t.x, 0.0), 0.0).rgb +
            textureLod(u_a, uv + vec2(0.0, -t.y), 0.0).rgb + textureLod(u_a, uv + vec2(0.0, t.y), 0.0).rgb) * 2.0;
    sum += textureLod(u_a, uv - t, 0.0).rgb + textureLod(u_a, uv + t, 0.0).rgb +
           textureLod(u_a, uv + vec2(t.x, -t.y), 0.0).rgb + textureLod(u_a, uv + vec2(-t.x, t.y), 0.0).rgb;
    return sum / 16.0;
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// a faint, even dirt on the lens: soft spots of a few sizes (procedural; the same every frame)
float dirt(vec2 uv)
{
    float aspect = float(sun.rect.z) / float(max(sun.rect.w, 1));
    float sum = 0.0;
    for (int layer = 0; layer < 3; ++layer) {
        float cells = 3.0 + 3.0 * float(layer);
        vec2 g = uv * vec2(cells * aspect, cells);
        vec2 cell = floor(g);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                vec2 c = cell + vec2(dx, dy);
                float h = hash(c + float(layer) * 17.0);
                vec2 centre = c + vec2(hash(c + 3.1), hash(c + 7.7));
                float radius = 0.15 + 0.3 * h;
                float spot = 1.0 - smoothstep(radius * 0.5, radius, length(g - centre));
                sum += spot * (0.3 + 0.7 * hash(c + 11.3)) * step(0.45, h);
            }
        }
    }
    return sum;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy) - sun.rect.xy;
    vec3 base = imageLoad(i_a, p).rgb;
    vec3 light = imageLoad(i_b, p).rgb;
    vec3 colour = base + light;
    if (sun.flags.w != 0) {
        vec3 spread = sun.bloom.x * base + sun.bloom.y * light;
        vec2 uv = (vec2(p) + 0.5) / (2.0 * vec2(level.size.xy));
        vec3 bloomed = tent(uv, 1.0 / vec2(level.size.xy));
        colour += bloomed - spread;
        if (sun.bloom.z > 0.0) {
            colour += sun.bloom.z * dirt((vec2(p) + 0.5) / vec2(sun.rect.zw)) * bloomed;
        }
    }
    colour = max(colour, vec3(0.0));
    if (sun.bloom.w < 0.5) {
        colour = min(colour, vec3(1.0));
    }
    o_colour = vec4(colour, 1.0);
}
)";

// The block, as the shaders' std140 layout has it.
struct SunBlock {
    std::array<float, kMatrixFloats> to_world{}; // column-major (GLSL's mat4)
    std::array<float, 4> camera{};
    std::array<float, 4> sun{};
    std::array<float, 4> sun_colour{};
    std::array<float, 4> sun_screen{};
    std::array<float, 4> disc{};
    std::array<float, 4> aureole{};
    std::array<float, 4> shafts{};
    std::array<float, 4> bloom{};
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> low{};
    std::array<int32_t, 4> flags{};
};
constexpr std::size_t kSunBlockBytes = 240;
static_assert(sizeof(SunBlock) == kSunBlockBytes, "the shaders' std140 block");

struct LevelConstants {
    std::array<int32_t, 4> size{};
    std::array<float, 4> weights{};
};
constexpr std::size_t kLevelBytes = 32;
static_assert(sizeof(LevelConstants) == kLevelBytes, "the shaders' push-constant block");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The bindings: 0 the colour's copy, 1 the depth, 2 / 3 written, 4 / 5 read (filtered), 6 the block, 7 / 8 read (texels).
constexpr uint32_t kBindingColour = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOutA = 2;
constexpr uint32_t kBindingOutB = 3;
constexpr uint32_t kBindingInA = 4;
constexpr uint32_t kBindingInB = 5;
constexpr uint32_t kBindingBlock = 6;
constexpr uint32_t kBindingLoadA = 7;
constexpr uint32_t kBindingLoadB = 8;
constexpr uint32_t kBindingCount = 9;

struct CompositePipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

// A viewport's images.
struct SunSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    Image copy;   // the colour's viewport copied out (the target's format and samples)
    Image base;   // the frame's colour (16-bit floats)
    Image light;  // the light added
    Image shafts; // half resolution
    std::vector<Image> down; // level 1 (half) and smaller
    std::vector<Image> up;   // levels 1 .. n - 1 (the last level's up is its down)
    bool fresh = true;
    bool written = false;
    uint64_t last_use = 0;
};

constexpr uint32_t kTimerSets = 8;   // (the sets of timestamps the passes are timed with, used in turn)
constexpr uint32_t kTimerPoints = 5; // before, after the rays, the source, the bloom, the composite
constexpr uint64_t kTimingLogEvery = 240;

struct SunState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> shafts{}; // single-sampled depth, multisampled
    std::array<VkPipeline, 2> source{};
    VkPipeline down_first = VK_NULL_HANDLE;
    VkPipeline down = VK_NULL_HANDLE;
    VkPipeline up = VK_NULL_HANDLE;
    std::vector<CompositePipeline> composites;
    std::vector<std::unique_ptr<SunSlot>> slots;
    RtSunFrame frame;
    std::array<float, 3> logged_toward{}; // (the sun last logged)
    bool failed = false;
    uint64_t passes = 0;
    uint64_t uses = 0;
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    uint64_t timed = 0;
    std::array<uint64_t, kTimerSets> timer_serials{};
    RtSunTiming timing;
    uint64_t window_samples = 0;
    std::array<double, 4> window_milliseconds{};
};

SunState& sun_state()
{
    static SunState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class SunListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kSun; }
    void opaque_drawn(const rt::ViewDrawn& view) override { (void)view; }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_sun_pass(state, view);
        }
    }
};

SunListener& listener()
{
    static SunListener instance;
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

std::string source_of(const char* body, const char* define)
{
    std::string source = "#version 460\n";
    if (define != nullptr) {
        source += "#define ";
        source += define;
        source += "\n";
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

VkSampler make_sampler(VkDevice device, VkFilter filter)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0F;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}

bool make_layouts(Backend& state, std::string* error)
{
    SunState& own = sun_state();
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (binding == kBindingColour || binding == kBindingDepth || binding == kBindingInA || binding == kBindingInB) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (binding == kBindingBlock) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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
    own.nearest = make_sampler(device, VK_FILTER_NEAREST);
    own.linear = make_sampler(device, VK_FILTER_LINEAR);
    if (own.nearest == VK_NULL_HANDLE || own.linear == VK_NULL_HANDLE ||
        vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        *error = "the descriptor layout or the samplers";
        return false;
    }
    const VkPushConstantRange range{.stageFlags = kStages, .offset = 0, .size = sizeof(LevelConstants)};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &range};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    return true;
}

// The layouts, the samplers, the vertex shader and the compute pipelines (once).
bool ensure_objects(Backend& state, std::string* error)
{
    SunState& own = sun_state();
    if (own.up != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    if (own.vertex == VK_NULL_HANDLE) {
        own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
        if (own.vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t multisampled = 0; multisampled < 2; ++multisampled) {
        const char* define = multisampled != 0 ? "MULTISAMPLED" : nullptr;
        if (core::at(own.shafts, multisampled) == VK_NULL_HANDLE) {
            core::at(own.shafts, multisampled) = make_compute(device, own.layout, source_of(kShaftsBody, define), error);
        }
        if (core::at(own.source, multisampled) == VK_NULL_HANDLE) {
            core::at(own.source, multisampled) = make_compute(device, own.layout, source_of(kSourceBody, define), error);
        }
        if (core::at(own.shafts, multisampled) == VK_NULL_HANDLE || core::at(own.source, multisampled) == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.down_first == VK_NULL_HANDLE) {
        own.down_first = make_compute(device, own.layout, source_of(kDownBody, "FIRST"), error);
    }
    if (own.down == VK_NULL_HANDLE) {
        own.down = make_compute(device, own.layout, source_of(kDownBody, nullptr), error);
    }
    if (own.down_first == VK_NULL_HANDLE || own.down == VK_NULL_HANDLE) {
        return false;
    }
    own.up = make_compute(device, own.layout, source_of(kUpBody, nullptr), error);
    return own.up != VK_NULL_HANDLE;
}

// The composite's pipeline for a colour target (made on first use): rgb written, alpha kept, no blending.
VkPipeline composite_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    SunState& own = sun_state();
    for (const CompositePipeline& made : own.composites) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        make_module(device, RtShaderStage::kFragment, source_of(kCompositeBody, nullptr), error);
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
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
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
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
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
    own.composites.push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

Image* colour_of(Surface* surface)
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

// --- the matrices (as the fog's: vulkan_rt_fog.cpp) ----------------------------------------------------------------

bool invert(Matrix matrix, Matrix* inverse)
{
    Matrix result{};
    for (std::size_t i = 0; i < kMatrixSide; ++i) {
        core::at(core::at(result, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kMatrixSide; ++row) {
            if (std::abs(core::at(core::at(matrix, row), column)) >
                std::abs(core::at(core::at(matrix, pivot), column))) {
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

// World -> (pixel x, pixel y, depth, 1) in the target's pixels (centres at whole coordinates, y down).
Matrix world_to_pixel(const rt::ViewDrawn& view)
{
    Matrix clip_from_world{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(view.camera.view, column), k)) *
                       static_cast<double>(core::at(core::at(view.camera.projection, k), row));
            }
            core::at(core::at(clip_from_world, row), column) = sum;
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

std::array<double, 4> transform(const Matrix& matrix, const std::array<double, 4>& v)
{
    std::array<double, 4> out{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        double sum = 0.0;
        for (std::size_t k = 0; k < kMatrixSide; ++k) {
            sum += core::at(core::at(matrix, row), k) * core::at(v, k);
        }
        core::at(out, row) = sum;
    }
    return out;
}

// The view ray's direction through a target pixel (as the shaders' direction_of).
std::array<double, 3> direction_through(const Matrix& to_world, const std::array<float, 3>& camera, double x, double y)
{
    const std::array<double, 4> w = transform(to_world, {x, y, 0.5, 1.0});
    std::array<double, 3> to{};
    double length = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(to, i) = (core::at(w, i) / core::at(w, 3)) - static_cast<double>(core::at(camera, i));
        length += core::at(to, i) * core::at(to, i);
    }
    length = std::sqrt(length);
    for (double& v : to) {
        v = length > 0.0 ? v / length : 0.0;
    }
    return to;
}

// --- the viewports' images -----------------------------------------------------------------------------------------

Extent half_of(const Extent& extent)
{
    return {.width = std::max(1U, (extent.width + 1) / 2), .height = std::max(1U, (extent.height + 1) / 2)};
}

std::vector<Image*> images_of(SunSlot& slot)
{
    std::vector<Image*> images{&slot.base, &slot.light, &slot.shafts};
    for (Image& image : slot.down) {
        images.push_back(&image);
    }
    for (Image& image : slot.up) {
        images.push_back(&image);
    }
    return images;
}

void release_slot(Backend& state, SunSlot* slot, bool now)
{
    std::vector<Image*> images = images_of(*slot);
    images.push_back(&slot->copy);
    for (Image* image : images) {
        if (image->image == VK_NULL_HANDLE) {
            continue;
        }
        if (now) {
            state.memory.destroy_image(image);
        } else {
            retire_image(state, image);
        }
    }
}

bool make_storage_image(Backend& state, const Extent& extent, Image* out)
{
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
    out->resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

// The bloom's level sizes for a viewport: half its size, then halves, while the shorter side stays kSunBloomSmallest.
std::vector<Extent> level_sizes(const Extent& full)
{
    std::vector<Extent> sizes;
    Extent at = half_of(full);
    sizes.push_back(at);
    while (sizes.size() < kSunBloomLevels) {
        const Extent next = half_of(at);
        if (std::min(next.width, next.height) < kSunBloomSmallest) {
            break;
        }
        sizes.push_back(next);
        at = next;
    }
    return sizes;
}

SunSlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, const Image& colour)
{
    SunState& own = sun_state();
    ++own.uses;
    for (const std::unique_ptr<SunSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == colour.extent.width &&
            slot->target.height == colour.extent.height && slot->copy.format == colour.format &&
            slot->copy.samples == colour.samples) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kSunViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<SunSlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<SunSlot>();
    SunSlot& slot = *made_slot;
    slot.rect = rect;
    slot.target = colour.extent;
    slot.last_use = own.uses;
    const Extent full{.width = core::at(rect, 2), .height = core::at(rect, 3)};
    const ImageRequest copy_request{.format = colour.format,
                                    .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .extent = colour.extent,
                                    .levels = 1,
                                    .samples = colour.samples,
                                    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                    .alpha_one = false};
    bool made = state.memory.create_image(copy_request, &slot.copy);
    if (made) {
        prepare_image(state, &slot.copy);
    }
    made = made && make_storage_image(state, full, &slot.base) && make_storage_image(state, full, &slot.light) &&
           make_storage_image(state, half_of(full), &slot.shafts);
    const std::vector<Extent> sizes = level_sizes(full);
    slot.down.resize(sizes.size());
    slot.up.resize(sizes.size() - 1);
    for (std::size_t i = 0; made && i < sizes.size(); ++i) {
        made = make_storage_image(state, core::at(sizes, i), &core::at(slot.down, i)) &&
               (i + 1 >= sizes.size() || make_storage_image(state, core::at(sizes, i), &core::at(slot.up, i)));
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_sun: the viewport's images (%ux%u) couldn't be made - the pass skipped there",
                    full.width, full.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

// The new images moved to GENERAL and cleared, before the passes.
void clear_slot(VkCommandBuffer commands, SunSlot* slot)
{
    const std::vector<Image*> images = images_of(*slot);
    std::vector<VkImageMemoryBarrier> barriers(images.size());
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
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
                                 .subresourceRange = all};
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
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
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &cleared,
                         0, nullptr, 0, nullptr);
    slot->fresh = false;
}

// One pass's descriptors (null: not bound).
struct PassBindings {
    const Image* colour = nullptr; // (SHADER_READ_ONLY)
    const Image* depth = nullptr;
    const Image* out_a = nullptr;
    const Image* out_b = nullptr;
    const Image* in_a = nullptr; // filtered
    const Image* in_b = nullptr;
    const Image* load_a = nullptr;
    const Image* load_b = nullptr;
    VkDescriptorBufferInfo block{};
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const PassBindings& bound)
{
    const SunState& own = sun_state();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kBindingCount);
    const auto add = [&writes](uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
        VkWriteDescriptorSet& write = writes.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        return write;
    };
    if (bound.colour != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingColour);
        info = {.sampler = own.nearest,
                .imageView = bound.colour->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        add(kBindingColour, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    if (bound.depth != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingDepth);
        info = {.sampler = own.nearest,
                .imageView = bound.depth->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const std::array<std::pair<uint32_t, const Image*>, 2> filtered{{{kBindingInA, bound.in_a}, {kBindingInB, bound.in_b}}};
    for (const auto& [binding, image] : filtered) {
        if (image == nullptr) {
            continue;
        }
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = own.linear, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const std::array<std::pair<uint32_t, const Image*>, 4> storage{{{kBindingOutA, bound.out_a},
                                                                    {kBindingOutB, bound.out_b},
                                                                    {kBindingLoadA, bound.load_a},
                                                                    {kBindingLoadB, bound.load_b}}};
    for (const auto& [binding, image] : storage) {
        if (image == nullptr) {
            continue;
        }
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &info;
    }
    add(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &bound.block;
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
}

void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
}

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const PassBindings& bound, const LevelConstants& level,
              const Extent& extent)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdPushConstants(commands, sun_state().layout, kStages, 0, sizeof(level), &level);
    vkCmdDispatch(commands, (extent.width + kGroupSide - 1) / kGroupSide, (extent.height + kGroupSide - 1) / kGroupSide,
                  1);
    compute_done(commands);
}

void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_passes)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkPipelineStageFlags passes = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_passes ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_passes ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(commands, to_passes ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : passes, to_passes ? passes : tests,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

void record_composite(VkCommandBuffer commands, VkPipeline pipeline, Image* colour, const PassBindings& bound,
                      const LevelConstants& level, const std::array<uint32_t, 4>& rect)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{
        .offset = {.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1))},
        .extent = {.width = core::at(rect, 2), .height = core::at(rect, 3)}};
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
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound);
    vkCmdPushConstants(commands, sun_state().layout, kStages, 0, sizeof(level), &level);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// --- timing ----------------------------------------------------------------------------------------------------------

void ensure_timer(Backend& state)
{
    SunState& own = sun_state();
    if (own.timer != VK_NULL_HANDLE || own.tick_milliseconds < 0.0) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimerSets * kTimerPoints,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
        own.timer = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

void read_timer(Backend& state, uint32_t set, const Extent& size)
{
    SunState& own = sun_state();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, kTimerPoints> stamps{};
    const VkResult got = vkGetQueryPoolResults(state.context.device(), own.timer, set * kTimerPoints, kTimerPoints,
                                               sizeof(stamps), stamps.data(), sizeof(Stamp),
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (got != VK_SUCCESS && got != VK_NOT_READY) {
        return;
    }
    for (std::size_t i = 0; i < kTimerPoints; ++i) {
        if (core::at(stamps, i).available == 0 || (i > 0 && core::at(stamps, i).value < core::at(stamps, i - 1).value)) {
            return;
        }
    }
    ++own.timing.samples;
    ++own.window_samples;
    for (std::size_t part = 0; part < own.timing.milliseconds.size(); ++part) {
        const double milliseconds =
            static_cast<double>(core::at(stamps, part + 1).value - core::at(stamps, part).value) * own.tick_milliseconds;
        core::at(own.timing.milliseconds, part) += milliseconds;
        core::at(own.window_milliseconds, part) += milliseconds;
    }
    if (own.window_samples >= kTimingLogEvery) {
        const auto count = static_cast<double>(own.window_samples);
        const std::array<double, 4>& w = own.window_milliseconds;
        get_log("vulkan: rt_sun: the passes take %.3f ms on the device - rays %.3f, source %.3f, bloom %.3f, composite "
                "%.3f (%ux%u; the last %llu passes' average)",
                (core::at(w, 0) + core::at(w, 1) + core::at(w, 2) + core::at(w, 3)) / count, core::at(w, 0) / count,
                core::at(w, 1) / count, core::at(w, 2) / count, core::at(w, 3) / count, size.width, size.height,
                static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = {};
    }
}

// --- the pass ------------------------------------------------------------------------------------------------------

// What this view gets, worked out from the settings, the sun and the camera.
struct Plan {
    bool disc = false;
    bool shafts = false;
    bool bloom = false;
    bool frame_bloomed = false;
};

// A view's matrices: world -> the target's pixels, and back.
struct ViewMatrices {
    Matrix to_pixel{};
    Matrix to_world{};
};

// toward_sun: the sun (null: none).
RtSunFrame frame_for(const RtSunSettings& settings, const rt::ViewDrawn& view, const ViewMatrices& matrices,
                     const std::array<float, 3>* toward_sun)
{
    const Matrix& to_world = matrices.to_world;
    RtSunFrame frame;
    frame.sun = toward_sun != nullptr;
    frame.disc_radius = static_cast<float>(kSunDiscRadius * settings.sun_size);
    const double left = core::at(view.rect, 0);
    const double top = core::at(view.rect, 1);
    const double width = core::at(view.rect, 2);
    const double height = core::at(view.rect, 3);
    const double centre_x = left + (width / 2.0);
    const double centre_y = top + (height / 2.0);
    const std::array<double, 3> here = direction_through(to_world, view.camera.position, centre_x, centre_y);
    const std::array<double, 3> next = direction_through(to_world, view.camera.position, centre_x + 1.0, centre_y);
    double cosine = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        cosine += core::at(here, i) * core::at(next, i);
    }
    frame.pixel_angle = static_cast<float>(std::acos(std::clamp(cosine, -1.0, 1.0)));
    if (toward_sun == nullptr) {
        return frame;
    }
    const std::array<float, 3>& toward = *toward_sun;
    frame.toward = toward;
    frame.colour = sun_colour_for(toward);
    const std::array<double, 4> at = transform(
        matrices.to_pixel, {static_cast<double>(core::at(toward, 0)), static_cast<double>(core::at(toward, 1)),
                   static_cast<double>(core::at(toward, 2)), 0.0});
    frame.in_front = core::at(at, 3) > kInFront;
    if (frame.in_front) {
        const double x = core::at(at, 0) / core::at(at, 3);
        const double y = core::at(at, 1) / core::at(at, 3);
        frame.screen = {static_cast<float>(x), static_cast<float>(y)};
        const double outside_x = std::max({0.0, left - x, x - (left + width)}) / std::max(width, 1.0);
        const double outside_y = std::max({0.0, top - y, y - (top + height)}) / std::max(height, 1.0);
        const double outside = std::max(outside_x, outside_y);
        frame.fade = static_cast<float>(std::clamp(1.0 - (outside / kSunShaftFadeOut), 0.0, 1.0));
    }
    return frame;
}

// What the block is made from.
struct BlockInput {
    const RtSunSettings* settings = nullptr;
    const rt::ViewDrawn* view = nullptr;
    const Matrix* to_world = nullptr;
    const RtSunFrame* frame = nullptr;
    const Plan* plan = nullptr;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t levels = 0;
};

SunBlock block_for(const BlockInput& input)
{
    const RtSunSettings& settings = *input.settings;
    const rt::ViewDrawn& view = *input.view;
    const RtSunFrame& frame = *input.frame;
    const Plan& plan = *input.plan;
    SunBlock block{};
    block.to_world = column_major(*input.to_world);
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), 0.0F};
    block.sun = {core::at(frame.toward, 0), core::at(frame.toward, 1), core::at(frame.toward, 2),
                 frame.sun && frame.in_front ? 1.0F : 0.0F};
    block.sun_colour = {core::at(frame.colour, 0), core::at(frame.colour, 1), core::at(frame.colour, 2),
                        kSunDiscIntensity};
    block.sun_screen = {core::at(frame.screen, 0), core::at(frame.screen, 1), frame.in_front ? 1.0F : 0.0F, frame.fade};
    block.disc = {frame.disc_radius, frame.pixel_angle, kSunAureoleNearWidth, kSunAureoleWideWidth};
    block.aureole = {kSunAureoleNear, kSunAureoleWide, 0.0F, 0.0F};
    block.shafts = {settings.shaft_strength, kSunShaftDecay, kSunShaftReach, kSunShaftWidth};
    block.bloom = {plan.frame_bloomed ? settings.bloom_strength : 0.0F, kSunGlareBloom,
                   settings.lens_dirt ? 1.0F : 0.0F, settings.hdr ? 1.0F : 0.0F};
    block.rect = {static_cast<int32_t>(core::at(view.rect, 0)), static_cast<int32_t>(core::at(view.rect, 1)),
                  static_cast<int32_t>(core::at(view.rect, 2)), static_cast<int32_t>(core::at(view.rect, 3))};
    const Extent half = half_of({.width = core::at(view.rect, 2), .height = core::at(view.rect, 3)});
    block.low = {static_cast<int32_t>(half.width), static_cast<int32_t>(half.height),
                 static_cast<int32_t>(kSunShaftSamples), static_cast<int32_t>(input.levels)};
    block.flags = {static_cast<int32_t>(input.samples), plan.disc ? 1 : 0, plan.shafts ? 1 : 0, plan.bloom ? 1 : 0};
    return block;
}

bool targets_readable(Image* colour, Image* depth)
{
    return colour != nullptr && depth != nullptr && colour->attachment_view != VK_NULL_HANDLE &&
           (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0 && (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
           (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE &&
           depth->samples == colour->samples && depth->extent.width >= colour->extent.width &&
           depth->extent.height >= colour->extent.height;
}

// The bloom's chain: down from the source's two images to the smallest level, then up to level 1.
void record_bloom(VkCommandBuffer commands, SunSlot* slot, const VkDescriptorBufferInfo& block)
{
    const SunState& own = sun_state();
    const std::size_t levels = slot->down.size();
    const float weight = 1.0F / static_cast<float>(levels);
    const Extent full = slot->base.extent;
    for (std::size_t i = 0; i < levels; ++i) {
        const Image& out = core::at(slot->down, i);
        const Extent from = i == 0 ? full : core::at(slot->down, i - 1).extent;
        const LevelConstants level{.size = {static_cast<int32_t>(out.extent.width), static_cast<int32_t>(out.extent.height),
                                            static_cast<int32_t>(from.width), static_cast<int32_t>(from.height)},
                                   .weights = {}};
        if (i == 0) {
            dispatch(commands, own.down_first,
                     {.out_a = &out, .in_a = &slot->base, .in_b = &slot->light, .block = block}, level, out.extent);
        } else {
            dispatch(commands, own.down, {.out_a = &out, .in_a = &core::at(slot->down, i - 1), .block = block}, level,
                     out.extent);
        }
    }
    // up: level i = its own x the weight + the one below upsampled (the smallest level's own, x the weight)
    for (std::size_t i = levels - 1; i-- > 0;) {
        const Image& out = core::at(slot->up, i);
        const bool below_is_last = i + 1 == levels - 1;
        const Image& below = below_is_last ? core::at(slot->down, i + 1) : core::at(slot->up, i + 1);
        const LevelConstants level{
            .size = {static_cast<int32_t>(out.extent.width), static_cast<int32_t>(out.extent.height),
                     static_cast<int32_t>(below.extent.width), static_cast<int32_t>(below.extent.height)},
            .weights = {weight, below_is_last ? weight : 1.0F, 0.0F, 0.0F}};
        dispatch(commands, own.up, {.out_a = &out, .in_a = &below, .load_a = &core::at(slot->down, i), .block = block},
                 level, out.extent);
    }
}

// The bloom's top: what the composite upsamples (level 1's up, or with one level its down, x the weight 1).
const Image& bloom_top(const SunSlot& slot) { return slot.up.empty() ? slot.down.front() : slot.up.front(); }

// The sun this view draws (vulkan_rt_sky.h visible_sun), whether the original's is left out, its direction logged when
// it moves; no sun: the other effects told so. False: none (or none wanted).
bool find_sun(Backend& state, const RtSunSettings& settings, std::array<float, 3>* toward)
{
    SunState& own = sun_state();
    const bool want_sun = settings.sun || settings.shafts;
    bool have_sun = want_sun && visible_sun(toward);
    // (added by the time of day) the clock's sun under the horizon: no disc (the moon is the sky pass's)
    const rt::DayLight day = rt::day_light();
    constexpr float kBelowHorizon = -0.02F;
    if (have_sun && day.on && core::at(*toward, 1) < kBelowHorizon) {
        have_sun = false;
    }
    if (want_sun) {
        rt::note_sun_used(rt::SunUser::kDrawnSun, have_sun, *toward);
    }
    rt::set_sun_glare_replaced(settings.sun && have_sun);
    // (a running clock moves the sun twice a second: logged every 5 degrees of it)
    constexpr float kLoggedApart = 0.9962F;
    const float moved = (core::at(*toward, 0) * core::at(own.logged_toward, 0)) +
                        (core::at(*toward, 1) * core::at(own.logged_toward, 1)) +
                        (core::at(*toward, 2) * core::at(own.logged_toward, 2));
    if (have_sun && *toward != own.logged_toward && (!day.on || moved < kLoggedApart)) {
        // (logged when the sun moves: a new level, the time of day, rt_sun_source)
        own.logged_toward = *toward;
        const rt::SceneSun scene = rt::scene_sun();
        get_log("vulkan: rt_sun: the sun toward (%.3f, %.3f, %.3f), %.1f degrees up - %s",
                static_cast<double>(core::at(*toward, 0)), static_cast<double>(core::at(*toward, 1)),
                static_cast<double>(core::at(*toward, 2)),
                std::asin(std::clamp(static_cast<double>(core::at(*toward, 1)), -1.0, 1.0)) * kDegreesPerRadian,
                scene.found() ? rt::sun_origin_name(scene.origin) : "where the game shows its sun (lighting nothing)");
    }
    if (want_sun && !have_sun) {
        sky_without_sun();
        if (!settings.bloom && first_report(state, kReportNoSun)) {
            get_log("vulkan: rt_sun: no sun in this level (none baked, or the fit not run yet) - the original's sun kept");
        }
    }
    return have_sun;
}

// What a view's passes are recorded with.
struct Recording {
    Image* colour = nullptr;
    Image* depth = nullptr;
    SunSlot* slot = nullptr;
    VkPipeline composite = VK_NULL_HANDLE;
    VkDescriptorBufferInfo block{};
    Plan plan;
    std::array<uint32_t, 4> rect{}; // the viewport inside the target
};

// The timestamps' set this pass uses (none when the device can't time, or the set's last results aren't in yet).
struct Stamps {
    VkCommandBuffer commands = nullptr;
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t set = 0;
    bool on = false;
    void stamp(uint32_t point, VkPipelineStageFlagBits stage) const
    {
        if (on) {
            vkCmdWriteTimestamp(commands, stage, pool, (set * kTimerPoints) + point);
        }
    }
};
Stamps begin_timing(Backend& state, VkCommandBuffer commands, const Extent& full)
{
    SunState& own = sun_state();
    ensure_timer(state);
    const auto set = static_cast<uint32_t>(own.timed % kTimerSets);
    uint64_t& written = core::at(own.timer_serials, set);
    const bool timing = own.timer != VK_NULL_HANDLE && (written == 0 || state.frames.completed() >= written);
    if (timing) {
        if (written != 0) {
            read_timer(state, set, full);
        }
        written = state.frames.serial();
        vkCmdResetQueryPool(commands, own.timer, set * kTimerPoints, kTimerPoints);
        ++own.timed;
    }
    return {.commands = commands, .pool = own.timer, .set = set, .on = timing};
}

// The colour's viewport copied out into the slot's copy (to be sampled).
void copy_colour(VkCommandBuffer commands, Image* colour, SunSlot* slot, const std::array<uint32_t, 4>& rect)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &slot->copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1)), .z = 0};
    const VkImageCopy region{.srcSubresource = layers,
                             .srcOffset = corner,
                             .dstSubresource = layers,
                             .dstOffset = corner,
                             .extent = {.width = core::at(rect, 2), .height = core::at(rect, 3), .depth = 1}};
    vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot->copy.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    Memory::transition(commands, &slot->copy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// The passes recorded (steps 0 - 4 of vulkan_rt_sun.h), timed, the targets put back.
void record_commands(Backend& state, const Recording& recording)
{
    SunState& own = sun_state();
    Image* const colour = recording.colour;
    Image* const depth = recording.depth;
    SunSlot* const slot = recording.slot;
    const Extent full{.width = core::at(recording.rect, 2), .height = core::at(recording.rect, 3)};
    VkCommandBuffer commands = outside_pass(state);
    if (slot->fresh) {
        clear_slot(commands, slot);
    }
    std::array<float, 3> baked{};
    if (frame_sun(&baked)) {
        update_sky(state, commands, baked); // (the lighting sun's colour for the other effects; the fog's table)
    }
    const Stamps stamps = begin_timing(state, commands, full);
    stamps.stamp(0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    // 0. the colour's viewport copied out
    copy_colour(commands, colour, slot, recording.rect);
    // the images' last frame's uses before this one's writes
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
    const std::size_t multisampled = colour->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U;
    const LevelConstants no_level{};
    // 1. the rays
    if (recording.plan.shafts) {
        dispatch(commands, core::at(own.shafts, multisampled),
                 {.depth = depth, .out_a = &slot->shafts, .block = recording.block}, no_level, half_of(full));
    }
    stamps.stamp(1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    // 2. the source
    dispatch(commands, core::at(own.source, multisampled),
             {.colour = &slot->copy,
              .depth = depth,
              .out_a = &slot->base,
              .out_b = &slot->light,
              .in_a = &slot->shafts,
              .block = recording.block},
             no_level, full);
    stamps.stamp(2, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    // 3. the bloom
    record_bloom(commands, slot, recording.block);
    stamps.stamp(3, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    // 4. the composite
    const Image& top_level = bloom_top(*slot);
    const LevelConstants composite_level{
        .size = {static_cast<int32_t>(top_level.extent.width), static_cast<int32_t>(top_level.extent.height), 0, 0},
        .weights = {}};
    record_composite(commands, recording.composite, colour,
                     {.in_a = &top_level, .load_a = &slot->base, .load_b = &slot->light, .block = recording.block},
                     composite_level, recording.rect);
    stamps.stamp(4, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    depth_barrier(commands, depth, depth->resting, false);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
    slot->copy.used = serial;
    for (Image* image : images_of(*slot)) {
        image->used = serial;
    }
    slot->written = true;
}

void log_first_pass(const Recording& recording, const rt::ViewDrawn& view, const RtSunFrame& frame, float frame_share)
{
    const char* where = "none";
    if (frame.sun) {
        where = frame.in_front ? "in front" : "behind";
    }
    const Plan& plan = recording.plan;
    get_log("vulkan: rt_sun: the first pass (%ux%u, %u sample%s; %s%s%s; %zu bloom levels, the frame's share %.3f%s; sun "
            "%s, colour %.3f %.3f %.3f)",
            core::at(recording.rect, 2), core::at(recording.rect, 3), static_cast<unsigned>(recording.colour->samples),
            recording.colour->samples == 1 ? "" : "s", plan.disc ? "the sun's disc and glare, " : "",
            plan.shafts ? "rays, " : "", plan.frame_bloomed ? "the frame bloomed" : "the sun's light bloomed",
            recording.slot->down.size(), static_cast<double>(frame_share),
            view.game_bloom ? ", the game's own bloom on" : "", where, static_cast<double>(core::at(frame.colour, 0)),
            static_cast<double>(core::at(frame.colour, 1)), static_cast<double>(core::at(frame.colour, 2)));
}

bool record_pass(Backend& state, const rt::ViewDrawn& view)
{
    SunState& own = sun_state();
    const RtSunSettings& settings = sun_settings_held();
    if (own.failed || !(settings.sun || settings.shafts || settings.bloom)) {
        rt::set_sun_glare_replaced(false);
        return false;
    }
    std::array<float, 3> toward{};
    const bool have_sun = find_sun(state, settings, &toward);
    Recording recording;
    recording.colour = colour_of(core::at(state.targets, 0));
    recording.depth = colour_of(state.depth);
    if (!targets_readable(recording.colour, recording.depth)) {
        if (first_report(state, kReportTargets)) {
            get_log("vulkan: rt_sun: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the sun's pass skipped there");
        }
        return false;
    }
    const Extent target = recording.colour->extent;
    const uint32_t left = std::min(core::at(view.rect, 0), target.width);
    const uint32_t top = std::min(core::at(view.rect, 1), target.height);
    recording.rect = {left, top, std::min(core::at(view.rect, 2), target.width - left),
                      std::min(core::at(view.rect, 3), target.height - top)};
    if (core::at(recording.rect, 2) == 0 || core::at(recording.rect, 3) == 0) {
        return false;
    }
    rt::ViewDrawn clipped = view;
    clipped.rect = recording.rect;
    ViewMatrices matrices{.to_pixel = world_to_pixel(clipped)};
    if (!invert(matrices.to_pixel, &matrices.to_world)) {
        return false;
    }
    const RtSunFrame frame = frame_for(settings, clipped, matrices, have_sun ? &toward : nullptr);
    Plan& plan = recording.plan;
    plan.disc = settings.sun && have_sun && frame.in_front;
    plan.shafts = settings.shafts && have_sun && frame.in_front && frame.fade > 0.0F;
    plan.frame_bloomed = settings.bloom && !view.game_bloom;
    plan.bloom = plan.frame_bloomed || plan.disc || plan.shafts;
    own.frame = frame;
    if (!plan.bloom) {
        return false; // (nothing to add: the sun behind the camera, no bloom of the frame)
    }
    std::string error;
    recording.composite = ensure_objects(state, &error) ? composite_pipeline(state, recording.colour->format,
                                                                               recording.colour->samples, &error)
                                                        : VK_NULL_HANDLE;
    if (recording.composite == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_sun: the passes couldn't be made (%s) - no sun, rays or bloom", error.c_str());
        }
        return false;
    }
    recording.slot = slot_for(state, recording.rect, *recording.colour);
    if (recording.slot == nullptr) {
        return false;
    }
    const SunBlock block = block_for({.settings = &settings,
                                      .view = &clipped,
                                      .to_world = &matrices.to_world,
                                      .frame = &frame,
                                      .plan = &plan,
                                      .samples = recording.colour->samples,
                                      .levels = static_cast<uint32_t>(recording.slot->down.size())});
    const void* const block_bytes = &block;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(block_bytes), sizeof(block)), state.uniform_alignment);
    if (!span) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: rt_sun: no room in the ring for the pass's block - the pass skipped");
        }
        return false;
    }
    recording.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)};
    record_commands(state, recording);
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        log_first_pass(recording, view, frame, core::at(block.bloom, 0));
    }
    return true;
}

} // namespace

void set_rt_sun_settings(const RtSunSettings& settings)
{
    RtSunSettings kept = settings;
    kept.sun_size = std::isfinite(kept.sun_size) ? std::clamp(kept.sun_size, kMinSize, kMaxSize) : 1.0F;
    kept.bloom_strength =
        std::isfinite(kept.bloom_strength) ? std::clamp(kept.bloom_strength, 0.0F, kMaxBloom) : kRtBloomStrength;
    kept.shaft_strength =
        std::isfinite(kept.shaft_strength) ? std::clamp(kept.shaft_strength, 0.0F, kMaxShaft) : kRtShaftStrength;
    for (float& channel : kept.colour) {
        channel = std::isfinite(channel) ? std::clamp(channel, 0.0F, 1.0F) : 1.0F;
    }
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        sun_settings_held() = kept;
        // (a split comparison's swap between its sides: the sky's model kept - rt_scene.h settings_swap)
        // (and the original's sun left out on the side whose backend draws it - its pass tells again when it draws)
        if (!rt::settings_swap()) {
            invalidate_sky();
            rt::set_sun_glare_replaced(false);
        } else {
            rt::set_sun_glare_replaced(kept.sun);
        }
    }
    if (kept.sun || kept.shafts || kept.bloom) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtSunSettings rt_sun_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return sun_settings_held();
}

VkImageUsageFlags rt_sun_depth_usage(const Backend& state, VkFormat format)
{
    const RtSunSettings& settings = sun_settings_held();
    if (!(settings.sun || settings.shafts || settings.bloom)) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_sun_pass(Backend& state, const rt::ViewDrawn& view) { return record_pass(state, view); }

uint64_t rt_sun_passes() { return sun_state().passes; }

RtSunTiming rt_sun_timing() { return sun_state().timing; }

RtSunFrame rt_sun_frame() { return sun_state().frame; }

bool read_rt_sun_image(Backend& state, const std::array<uint32_t, 4>& rect, RtSunImage which, RtSunTexels* out)
{
    *out = RtSunTexels{};
    // (the viewport's slot drawn last: one per target format and size)
    const SunSlot* latest = nullptr;
    for (const std::unique_ptr<SunSlot>& slot : sun_state().slots) {
        if (slot->rect == rect && slot->written && (latest == nullptr || slot->last_use > latest->last_use)) {
            latest = slot.get();
        }
    }
    for (const std::unique_ptr<SunSlot>& slot : sun_state().slots) {
        if (slot.get() != latest) {
            continue;
        }
        Image* image = &slot->shafts;
        switch (which) {
        case RtSunImage::kShafts:
            break;
        case RtSunImage::kBase:
            image = &slot->base;
            break;
        case RtSunImage::kLight:
            image = &slot->light;
            break;
        case RtSunImage::kBloom:
            image = slot->up.empty() ? &slot->down.front() : &slot->up.front();
            break;
        case RtSunImage::kBloomInput:
            image = &slot->down.front();
            break;
        }
        if (!finish_work(state)) {
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
                core::at(core::at(out->texels, i), channel) = half_to_float(half);
            }
        }
        return true;
    }
    return false;
}

void release_rt_sun(Backend& state)
{
    SunState& own = sun_state();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<SunSlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const CompositePipeline& made : own.composites) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {own.shafts.front(), own.shafts.back(), own.source.front(), own.source.back(),
                                          own.down_first, own.down, own.up}) {
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
        for (const VkSampler sampler : {own.nearest, own.linear}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timed = 0;
    own.timer_serials = {};
    own.composites.clear();
    own.shafts = {};
    own.source = {};
    own.down_first = VK_NULL_HANDLE;
    own.down = VK_NULL_HANDLE;
    own.up = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.nearest = VK_NULL_HANDLE;
    own.linear = VK_NULL_HANDLE;
    own.failed = false;
    rt::set_sun_glare_replaced(false);
    release_sky(state);
}

} // namespace render::vulkan
