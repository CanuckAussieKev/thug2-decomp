// NOT ORIGINAL - a modern sky: the atmosphere and the clouds drawn behind the world (vulkan_rt_sky_pass.h;
// docs/design/vulkan.md "The sky").
#include "gpu_vulkan/vulkan_rt_sky_pass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_clouds.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_sky_match.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportTargets = 0x534B5001U;
constexpr uint64_t kReportPipeline = 0x534B5002U;
constexpr uint64_t kReportFirst = 0x534B5003U;
constexpr uint64_t kReportImages = 0x534B5004U;
constexpr uint64_t kReportNoRoom = 0x534B5005U;
constexpr uint64_t kReportNoSun = 0x534B5006U;
constexpr uint64_t kReportNoCapture = 0x534B5007U;
constexpr uint64_t kReportShadows = 0x534B5008U;
constexpr uint32_t kShadowSamples = 4;
constexpr double kShadowStrength = 0.65; // how dark a cloud's full shadow is (0 - 1)

constexpr uint32_t kGroupSide = 8;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr double kSingular = 1e-300;
constexpr double kPi = std::numbers::pi;
constexpr double kDegrees = kPi / 180.0;
constexpr double kGolden = 0.6180339887498949;
constexpr float kSameSun = 0.99999F;
constexpr std::array<double, 3> kLuma{0.2126, 0.7152, 0.0722};
constexpr double kLeastGain = 0.05;
constexpr double kMostGain = 20.0;
constexpr float kGroundCovered = 0.3F; // the share of the ground band the dome must cover for its colour to be used

// The full-screen triangle at the far plane (depth 1: drawn where the world left the depth's far value).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 1.0, 1.0);
}
)";

// What the passes share: the block, the direction through a pixel.
constexpr const char* kCommon = R"(
layout(set = 0, binding = 6) uniform SkyBlock {
    mat4 to_world;        // the target's pixel (x, y, depth, 1) -> world
    mat4 prev_to_pixel;   // a world direction (w 0) -> the last frame's pixel
    vec4 camera;          // xyz the camera (world); w its altitude (metres)
    vec4 sun;             // xyz toward the sun; w the clouds' coverage
    vec4 sun_light;       // rgb the sun's light at the clouds; w their density scale
    vec4 ambient_bottom;  // rgb the sky's light at the clouds' base; w the clouds' aerial distance (metres)
    vec4 ambient_top;     // rgb at their top; w the frame's jitter offset
    vec4 offset;          // xy the clouds' offset (metres); z this frame's share in the history
    vec4 cloud_tint;      // rgb the clouds' gain
    ivec4 rect;           // the viewport in the target's pixels
    ivec4 low;            // xy the clouds' images' size, z pixels per texel, w steps
    ivec4 flags;          // x the depth's samples, y clouds, z a history, w 1: values above 1 kept
} sky;

vec3 direction_of(vec2 p)
{
    vec4 w = sky.to_world * vec4(p, 0.5, 1.0);
    vec3 to = w.xyz / w.w - sky.camera.xyz;
    float l = length(to);
    return l > 1e-12 ? to / l : vec3(0.0, 0.0, -1.0);
}

// the centre of a low-resolution texel, in the target's pixels (centres at whole numbers)
vec2 low_centre(ivec2 q) { return vec2(sky.rect.xy) + vec2(q * sky.low.z) + 0.5 * float(sky.low.z - 1); }

float jitter_at(vec2 p) { return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y)); }
)";

// 2. The clouds' march (low resolution): only texels with sky in their block.
constexpr const char* kMarchBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 0) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_b;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, sky.low.xy))) {
        return;
    }
    bool open = false;
    for (int dy = 0; dy < sky.low.z && !open; ++dy) {
        for (int dx = 0; dx < sky.low.z && !open; ++dx) {
            ivec2 p = sky.rect.xy + min(q * sky.low.z + ivec2(dx, dy), sky.rect.zw - ivec2(1));
            open = texelFetch(u_depth, p, 0).r >= 1.0;
        }
    }
    if (!open) {
        imageStore(o_a, q, vec4(0.0, 0.0, 0.0, 1.0));
        imageStore(o_b, q, vec4(-1.0, 0.0, 0.0, 0.0));
        return;
    }
    CloudRay ray;
    ray.camera_altitude = sky.camera.w;
    ray.offset = sky.offset.xy;
    ray.direction = direction_of(low_centre(q));
    ray.to_sun = sky.sun.xyz;
    ray.coverage = sky.sun.w;
    ray.density_scale = sky.sun_light.w;
    ray.steps = sky.low.w;
    ray.jitter = fract(jitter_at(vec2(q)) + sky.ambient_top.w);
    ray.sun_light = sky.sun_light.rgb;
    ray.ambient_bottom = sky.ambient_bottom.rgb;
    ray.ambient_top = sky.ambient_top.rgb;
    CloudResult result = march_clouds(ray);
    imageStore(o_a, q, vec4(result.light, result.transmittance));
    imageStore(o_b, q, vec4(result.distance, 1.0, 0.0, 0.0));
}
)";

// 3. The temporal pass.
constexpr const char* kTemporalBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_a;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_b;
layout(set = 0, binding = 9) uniform sampler2D u_hist_a;
layout(set = 0, binding = 10) uniform sampler2D u_hist_b;
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D i_a;
layout(set = 0, binding = 12, rgba16f) uniform readonly image2D i_b;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, sky.low.xy))) {
        return;
    }
    vec4 current = imageLoad(i_a, q);
    vec4 info = imageLoad(i_b, q);
    vec4 result = current;
    if (info.y > 0.5 && sky.flags.z != 0) {
        vec4 prev = sky.prev_to_pixel * vec4(direction_of(low_centre(q)), 0.0);
        if (prev.w > 1e-6) {
            vec2 pixel = prev.xy / prev.w;
            vec2 low = (pixel - vec2(sky.rect.xy) - 0.5 * float(sky.low.z - 1)) / float(sky.low.z);
            vec2 uv = (low + 0.5) / vec2(sky.low.xy);
            if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                textureLod(u_hist_b, uv, 0.0).y > 0.99) {
                vec4 history = textureLod(u_hist_a, uv, 0.0);
                vec4 least = current;
                vec4 most = current;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        ivec2 n = clamp(q + ivec2(dx, dy), ivec2(0), sky.low.xy - ivec2(1));
                        if (imageLoad(i_b, n).y > 0.5) {
                            vec4 v = imageLoad(i_a, n);
                            least = min(least, v);
                            most = max(most, v);
                        }
                    }
                }
                result = mix(clamp(history, least, most), current, sky.offset.z);
            }
        }
    }
    imageStore(o_a, q, result);
    imageStore(o_b, q, info);
}
)";

// 4. The composite (every sample of the sky's pixels: the depth test).
constexpr const char* kCompositeBody = R"(
#ifdef CLOUDS
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D i_a;
layout(set = 0, binding = 12, rgba16f) uniform readonly image2D i_b;
#endif
layout(location = 0) out vec4 o_colour;

void main()
{
    vec2 p = floor(gl_FragCoord.xy);
    vec3 dir = direction_of(p);
    vec3 light = sky_view_linear(dir, sky.sun.xyz);
#ifdef CLOUDS
    {
        vec2 low = (p - vec2(sky.rect.xy) - 0.5 * float(sky.low.z - 1)) / float(sky.low.z);
        ivec2 base = ivec2(floor(low));
        vec2 f = low - vec2(base);
        vec4 sum = vec4(0.0);
        float distance = 0.0;
        float weights = 0.0;
        for (int i = 0; i < 4; ++i) {
            ivec2 o = ivec2(i & 1, i >> 1);
            ivec2 at = clamp(base + o, ivec2(0), sky.low.xy - ivec2(1));
            vec4 info = imageLoad(i_b, at);
            float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) * (info.y > 0.5 ? 1.0 : 0.0);
            if (w > 0.0) {
                sum += w * imageLoad(i_a, at);
                distance += w * (info.x < 0.0 ? 0.0 : info.x);
                weights += w;
            }
        }
        if (weights > 1e-4) {
            vec4 cloud = sum / weights;
            float fade = exp(-(distance / weights) / sky.ambient_bottom.w);
            float transmittance = mix(1.0, clamp(cloud.a, 0.0, 1.0), fade);
            light = light * transmittance + max(cloud.rgb, vec3(0.0)) * sky.cloud_tint.rgb * fade;
        }
    }
#endif
    vec3 shown = pow(max(light, vec3(0.0)), vec3(1.0 / 2.2));
    shown = min(shown, vec3(sky.flags.w != 0 ? 65504.0 : 1.0));
    o_colour = vec4(shown, 1.0);
}
)";

// 5. The clouds' shadows on the world (rt_cloud_shadows; at opaque_drawn): each surface pixel's way to the sun through
// the layer, its density at kShadowSamples heights (no detail), its transmittance; the colour multiplied toward it.
constexpr const char* kShadowBody = R"(
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 0) uniform sampler2D u_depth;
#endif
layout(location = 0) out vec4 o_colour;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = texelFetch(u_depth, p, 0).r;
    if (d >= 1.0) {
        discard;
    }
    vec4 w = sky.to_world * vec4(vec2(p), d, 1.0);
    vec3 at = w.xyz / w.w;
    float altitude = sky.camera.w + (at.y - sky.camera.y) / GAME_UNITS_PER_METRE;
    vec2 across = sky.offset.xy + (at.xz - sky.camera.xz) / GAME_UNITS_PER_METRE;
    vec3 sun = sky.sun.xyz;
    float rise = max(sun.y, 0.08);
    float depth = 0.0;
    for (int k = 0; k < SHADOW_SAMPLES; ++k) {
        float h = CLOUD_BASE + (float(k) + 0.5) / float(SHADOW_SAMPLES) * (CLOUD_TOP - CLOUD_BASE);
        float t = (h - altitude) / rise;
        vec3 point = vec3(across.x + sun.x * t, h, across.y + sun.z * t);
        depth += cloud_density(point, sky.sun.w, false);
    }
    depth *= CLOUD_DENSITY * sky.sun_light.w * (CLOUD_TOP - CLOUD_BASE) / float(SHADOW_SAMPLES) / rise;
    float kept = mix(1.0, exp(-depth), SHADOW_STRENGTH * smoothstep(0.02, 0.15, sun.y));
    o_colour = vec4(vec3(kept), 1.0);
}
)";

struct SkyBlock {
    std::array<float, kMatrixFloats> to_world{};
    std::array<float, kMatrixFloats> prev_to_pixel{};
    std::array<float, 4> camera{};
    std::array<float, 4> sun{};
    std::array<float, 4> sun_light{};
    std::array<float, 4> ambient_bottom{};
    std::array<float, 4> ambient_top{};
    std::array<float, 4> offset{};
    std::array<float, 4> cloud_tint{};
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> low{};
    std::array<int32_t, 4> flags{};
};
constexpr std::size_t kSkyBlockBytes = 288;
static_assert(sizeof(SkyBlock) == kSkyBlockBytes, "the shaders' std140 block");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

constexpr uint32_t kBindingDepth = 0;
constexpr uint32_t kBindingSkyView = 1;
constexpr uint32_t kBindingTransmittance = 2;
constexpr uint32_t kBindingCloudNoise = 3; // 3, 4, 5
constexpr uint32_t kBindingBlock = 6;
constexpr uint32_t kBindingOutA = 7;
constexpr uint32_t kBindingOutB = 8;
constexpr uint32_t kBindingHistA = 9;
constexpr uint32_t kBindingHistB = 10;
constexpr uint32_t kBindingLoadA = 11;
constexpr uint32_t kBindingLoadB = 12;
constexpr uint32_t kBindingCount = 13;

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

struct CompositePipeline {
    VkFormat colour = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool clouds = false;
    bool shadow = false; // the clouds' shadows' pass (no depth attachment, the colour multiplied)
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// A viewport's images and its last camera.
struct SkySlot {
    std::array<uint32_t, 4> rect{};
    uint32_t scale = 1;
    Image march;      // rgb light, a transmittance
    Image march_info; // x distance, y 1 where marched
    std::array<Image, 2> history;
    std::array<Image, 2> history_info;
    uint32_t current = 0; // the history written last
    bool fresh = true;
    bool has_history = false;
    Matrix last_to_pixel{};
    uint64_t frames = 0;
    uint64_t last_use = 0;
};

constexpr uint32_t kTimerSets = 8;
constexpr uint32_t kTimerPoints = 4; // before the march, after it, after the temporal pass, after the composite
constexpr uint64_t kTimingLogEvery = 240;

struct SkyPassState {
    RtSkySettings settings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> march{}; // single-sampled depth, multisampled
    VkPipeline temporal = VK_NULL_HANDLE;
    std::vector<CompositePipeline> composites;
    std::vector<std::unique_ptr<SkySlot>> slots;
    bool failed = false;
    uint64_t passes = 0;
    uint64_t uses = 0;
    // the match
    RtSkyMatch match;
    std::array<float, 3> matched_sun{};
    bool matched = false;
    uint64_t settings_serial = 1; // (a new value each time the settings change: matched again)
    uint64_t matched_settings = 0;
    RtSkyFrame frame;
    // timing
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    uint64_t timed = 0;
    std::array<uint64_t, kTimerSets> timer_serials{};
    RtSkyTiming timing;
    uint64_t window_samples = 0;
    std::array<double, 3> window_milliseconds{};
};

SkyPassState& pass_state()
{
    static SkyPassState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

bool record_shadow_pass(Backend& state, const rt::ViewDrawn& view);

class SkyListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kSky; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_shadow_pass(state, view);
        }
    }
    void sky_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_sky_pass(state, view);
        }
    }
};

SkyListener& listener()
{
    static SkyListener instance;
    return instance;
}

// --- the matrices (as the sun's: vulkan_rt_sun.cpp) ------------------------------------------------------------------

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

// --- the objects -----------------------------------------------------------------------------------------------------

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
    source += atmosphere_glsl(kBindingTransmittance, kBindingSkyView);
    source += cloud_glsl(kBindingCloudNoise);
    source += "#define GAME_UNITS_PER_METRE " + std::to_string(rt::kGameUnitsPerMetre) + "\n";
    source += "#define SHADOW_SAMPLES " + std::to_string(kShadowSamples) + "\n";
    source += "#define SHADOW_STRENGTH " + std::to_string(kShadowStrength) + "\n";
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

bool ensure_objects(Backend& state, std::string* error)
{
    SkyPassState& own = pass_state();
    if (own.temporal != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (binding == kBindingBlock) {
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if (binding == kBindingOutA || binding == kBindingOutB || binding == kBindingLoadA ||
                       binding == kBindingLoadB) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
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
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = 1,
                                                     .pSetLayouts = &own.set_layout,
                                                     .pushConstantRangeCount = 0,
                                                     .pPushConstantRanges = nullptr};
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
    for (std::size_t multisampled = 0; multisampled < 2; ++multisampled) {
        if (core::at(own.march, multisampled) == VK_NULL_HANDLE) {
            core::at(own.march, multisampled) = make_compute(
                device, own.layout, source_of(kMarchBody, multisampled != 0 ? "MULTISAMPLED" : nullptr), error);
        }
        if (core::at(own.march, multisampled) == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.temporal = make_compute(device, own.layout, source_of(kTemporalBody, nullptr), error);
    return own.temporal != VK_NULL_HANDLE;
}

// The composite's pipeline for a colour target and its depth (made on first use): rgb written, alpha kept, the depth
// tested (at the far plane, less or equal: the sky's samples), not written.
VkPipeline composite_pipeline(Backend& state, const Image& colour, const Image& depth, bool clouds, std::string* error,
                              bool shadow = false)
{
    SkyPassState& own = pass_state();
    for (const CompositePipeline& made : own.composites) {
        if (made.colour == colour.format && made.depth == depth.format && made.samples == colour.samples &&
            made.clouds == clouds && made.shadow == shadow) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment =
        shadow ? make_module(device, RtShaderStage::kFragment,
                             source_of(kShadowBody, depth.samples != VK_SAMPLE_COUNT_1_BIT ? "MULTISAMPLED" : nullptr),
                             error)
               : make_module(device, RtShaderStage::kFragment, source_of(kCompositeBody, clouds ? "CLOUDS" : nullptr),
                             error);
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
    const VkPipelineMultisampleStateCreateInfo multisample{.sType =
                                                               VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0,
                                                           .rasterizationSamples = colour.samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth_state{};
    depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = shadow ? VK_FALSE : VK_TRUE;
    depth_state.depthWriteEnable = VK_FALSE;
    depth_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = shadow ? VK_TRUE : VK_FALSE;
    // (the shadows: the colour times the shader's - DST_COLOR x source + ZERO)
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
    const VkFormat colour_format = colour.format;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colour_format;
    rendering.depthAttachmentFormat = shadow ? VK_FORMAT_UNDEFINED : depth.format;
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
    info.pDepthStencilState = &depth_state;
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
    own.composites.push_back({.colour = colour.format,
                              .depth = depth.format,
                              .samples = colour.samples,
                              .clouds = clouds,
                              .shadow = shadow,
                              .pipeline = pipeline});
    return pipeline;
}

Image* image_of(Surface* surface)
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

bool targets_usable(const Image* colour, const Image* depth)
{
    return colour != nullptr && depth != nullptr && colour->attachment_view != VK_NULL_HANDLE &&
           (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0 && (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
           (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE &&
           depth->samples == colour->samples && depth->extent.width >= colour->extent.width &&
           depth->extent.height >= colour->extent.height;
}

// --- the slots -------------------------------------------------------------------------------------------------------

std::vector<Image*> images_of(SkySlot& slot)
{
    return {&slot.march,
            &slot.march_info,
            &core::at(slot.history, 0),
            &core::at(slot.history, 1),
            &core::at(slot.history_info, 0),
            &core::at(slot.history_info, 1)};
}

void release_slot(Backend& state, SkySlot* slot, bool now)
{
    for (Image* image : images_of(*slot)) {
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

Extent low_extent(const std::array<uint32_t, 4>& rect, uint32_t scale)
{
    return {.width = std::max(1U, (core::at(rect, 2) + scale - 1) / scale),
            .height = std::max(1U, (core::at(rect, 3) + scale - 1) / scale)};
}

SkySlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, uint32_t scale)
{
    SkyPassState& own = pass_state();
    ++own.uses;
    for (const std::unique_ptr<SkySlot>& slot : own.slots) {
        if (slot->rect == rect && slot->scale == scale) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kSkyViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<SkySlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<SkySlot>();
    SkySlot& slot = *made_slot;
    slot.rect = rect;
    slot.scale = scale;
    slot.last_use = own.uses;
    const Extent low = low_extent(rect, scale);
    bool made = true;
    for (Image* image : images_of(slot)) {
        made = made && make_storage_image(state, low, image);
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_sky: the clouds' images (%ux%u) couldn't be made - no clouds there", low.width,
                    low.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

void clear_slot(VkCommandBuffer commands, SkySlot* slot)
{
    const std::vector<Image*> images = images_of(*slot);
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    std::vector<VkImageMemoryBarrier> barriers(images.size());
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

// --- the descriptors and the dispatches --------------------------------------------------------------------------------

struct PassBindings {
    const Image* depth = nullptr;
    bool sky = false;    // the sky-view table and the transmittance
    bool clouds = false; // the noise
    const Image* out_a = nullptr;
    const Image* out_b = nullptr;
    const Image* hist_a = nullptr;
    const Image* hist_b = nullptr;
    const Image* load_a = nullptr;
    const Image* load_b = nullptr;
    VkDescriptorBufferInfo block{};
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const PassBindings& bound)
{
    const SkyPassState& own = pass_state();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kBindingCount);
    const auto add = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info) {
        core::at(infos, binding) = info;
        VkWriteDescriptorSet& write = writes.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pImageInfo = &core::at(infos, binding);
    };
    if (bound.depth != nullptr) {
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {.sampler = own.nearest,
             .imageView = bound.depth->view,
             .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL});
    }
    if (bound.sky) {
        add(kBindingSkyView, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            atmosphere_descriptor(AtmosphereTable::kSkyView));
        add(kBindingTransmittance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            atmosphere_descriptor(AtmosphereTable::kTransmittance));
    }
    if (bound.clouds) {
        for (uint32_t i = 0; i < 3; ++i) {
            add(kBindingCloudNoise + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cloud_noise_descriptor(i));
        }
    }
    const std::array<std::pair<uint32_t, const Image*>, 2> sampled{
        {{kBindingHistA, bound.hist_a}, {kBindingHistB, bound.hist_b}}};
    for (const auto& [binding, image] : sampled) {
        if (image != nullptr) {
            add(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {.sampler = binding == kBindingHistA ? own.linear : own.nearest,
                 .imageView = image->view,
                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
        }
    }
    const std::array<std::pair<uint32_t, const Image*>, 4> storage{{{kBindingOutA, bound.out_a},
                                                                    {kBindingOutB, bound.out_b},
                                                                    {kBindingLoadA, bound.load_a},
                                                                    {kBindingLoadB, bound.load_b}}};
    for (const auto& [binding, image] : storage) {
        if (image != nullptr) {
            add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
        }
    }
    VkWriteDescriptorSet& block = writes.emplace_back();
    block.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    block.dstBinding = kBindingBlock;
    block.descriptorCount = 1;
    block.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    block.pBufferInfo = &bound.block;
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

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const PassBindings& bound, const Extent& extent)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
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

void record_composite(VkCommandBuffer commands, VkPipeline pipeline, Image* colour, Image* depth,
                      const PassBindings& bound, const std::array<uint32_t, 4>& rect)
{
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depth != nullptr) {
        depth_info.imageView = depth->attachment_view != VK_NULL_HANDLE ? depth->attachment_view : depth->view;
    }
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{
        .offset = {.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1))},
        .extent = {.width = core::at(rect, 2), .height = core::at(rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    info.pDepthAttachment = depth == nullptr ? nullptr : &depth_info;
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
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// --- timing ------------------------------------------------------------------------------------------------------------

void ensure_timer(Backend& state)
{
    SkyPassState& own = pass_state();
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

void read_timer(Backend& state, uint32_t set, const std::array<uint32_t, 4>& rect)
{
    SkyPassState& own = pass_state();
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
        if (core::at(stamps, i).available == 0 ||
            (i > 0 && core::at(stamps, i).value < core::at(stamps, i - 1).value)) {
            return;
        }
    }
    ++own.timing.samples;
    ++own.window_samples;
    for (std::size_t part = 0; part < own.timing.milliseconds.size(); ++part) {
        const double milliseconds =
            static_cast<double>(core::at(stamps, part + 1).value - core::at(stamps, part).value) *
            own.tick_milliseconds;
        core::at(own.timing.milliseconds, part) += milliseconds;
        core::at(own.window_milliseconds, part) += milliseconds;
    }
    if (own.window_samples >= kTimingLogEvery) {
        const auto count = static_cast<double>(own.window_samples);
        const std::array<double, 3>& w = own.window_milliseconds;
        get_log("vulkan: rt_sky: the passes take %.3f ms on the device - clouds %.3f, temporal %.3f, composite %.3f "
                "(%ux%u; the last %llu passes' average)",
                (core::at(w, 0) + core::at(w, 1) + core::at(w, 2)) / count, core::at(w, 0) / count,
                core::at(w, 1) / count, core::at(w, 2) / count, core::at(rect, 2), core::at(rect, 3),
                static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = {};
    }
}

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

Stamps begin_timing(Backend& state, VkCommandBuffer commands, const std::array<uint32_t, 4>& rect)
{
    SkyPassState& own = pass_state();
    ensure_timer(state);
    const auto set = static_cast<uint32_t>(own.timed % kTimerSets);
    uint64_t& written = core::at(own.timer_serials, set);
    const bool timing = own.timer != VK_NULL_HANDLE && (written == 0 || state.frames.completed() >= written);
    if (timing) {
        if (written != 0) {
            read_timer(state, set, rect);
        }
        written = state.frames.serial();
        vkCmdResetQueryPool(commands, own.timer, set * kTimerPoints, kTimerPoints);
        ++own.timed;
    }
    return {.commands = commands, .pool = own.timer, .set = set, .on = timing};
}

// --- the match (vulkan_rt_sky_pass.h step 0) ---------------------------------------------------------------------------

using Linear = std::array<double, 3>;

Linear linear_of(const rt::Colour& display)
{
    Linear out{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(out, channel) =
            std::pow(std::max(static_cast<double>(core::at(display, channel)), 0.0), rt::kDisplayGamma);
    }
    return out;
}

double luma_of(const Linear& colour)
{
    return (core::at(kLuma, 0) * core::at(colour, 0)) + (core::at(kLuma, 1) * core::at(colour, 1)) +
           (core::at(kLuma, 2) * core::at(colour, 2));
}

// The model's mean (exposed linear) over each band of elevation, from the sky-view table (every azimuth; weighted by
// cos e - the band's solid angle).
std::array<Linear, rt::kSkyBands> band_means(const AtmosphereTexels& table)
{
    std::array<Linear, rt::kSkyBands> sums{};
    std::array<double, rt::kSkyBands> weights{};
    for (uint32_t row = 0; row < table.height; ++row) {
        const double v = (row + 0.5) / table.height;
        const double signed_root = (2.0 * v) - 1.0;
        const double elevation = (signed_root < 0.0 ? -1.0 : 1.0) * signed_root * signed_root * (kPi / 2.0);
        const double degrees = elevation / kDegrees;
        for (std::size_t band = 0; band < rt::kSkyBands; ++band) {
            const rt::BandRange& range = core::at(rt::kSkyBandRanges, band);
            if (degrees < range.low || degrees > range.high) {
                continue;
            }
            for (uint32_t column = 0; column < table.width; ++column) {
                // (u = sqrt(azimuth / pi): a texel's share of the azimuths is d(azimuth) = 2 pi u du)
                const double u = (column + 0.5) / table.width;
                const double weight = std::cos(elevation) * u;
                const std::array<float, 4>& texel =
                    core::at(table.texels, (static_cast<std::size_t>(row) * table.width) + column);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    core::at(core::at(sums, band), channel) +=
                        weight * std::max(0.0, static_cast<double>(core::at(texel, channel)));
                }
                core::at(weights, band) += weight;
            }
        }
    }
    for (std::size_t band = 0; band < rt::kSkyBands; ++band) {
        for (double& value : core::at(sums, band)) {
            value = core::at(weights, band) > 0.0 ? value / core::at(weights, band) : 0.0;
        }
    }
    return sums;
}

// The capture read back and analysed (false: none, or it can't be read).
bool read_capture(Backend& state, const rt::SkyCapture& capture, rt::SkyLook* look)
{
    Texture* texture = capture.texture != nullptr ? object(capture.texture) : nullptr;
    if (texture == nullptr || capture.face_size == 0) {
        return false;
    }
    Image* image = &texture->image;
    const bool bgra = image->format == VK_FORMAT_B8G8R8A8_UNORM || image->format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool rgba = image->format == VK_FORMAT_R8G8B8A8_UNORM || image->format == VK_FORMAT_R8G8B8A8_SRGB;
    if ((!bgra && !rgba) || !finish_work(state)) {
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(image->extent.width) * image->extent.height * 4);
    if (!state.memory.read_image_level(image, 0, &bytes)) {
        return false;
    }
    constexpr double kBytes = 255.0;
    const auto byte_of = [](float value) {
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kBytes));
    };
    *look = rt::analyse_sky({.bytes = bytes,
                             .face_size = capture.face_size,
                             .bgra = bgra,
                             .clear = {byte_of(core::at(capture.clear, 0)), byte_of(core::at(capture.clear, 1)),
                                       byte_of(core::at(capture.clear, 2))}});
    return true;
}

// The atmosphere's input and the clouds' parameters from the level's look and the model's bands.
void match_from(const RtSkySettings& settings, const std::array<float, 3>& sun, RtSkyMatch* match)
{
    AtmosphereInput& input = match->atmosphere;
    input = AtmosphereInput{};
    input.sun = sun;
    const rt::SkyLook& look = match->look;
    const double strength = std::clamp(static_cast<double>(settings.match), 0.0, 1.0);
    if (look.valid) {
        for (std::size_t band = 1; band < rt::kSkyBands; ++band) {
            // (with clouds, the clear sky's colour - the clouds bring their own; without, everything's - the mood kept)
            const Linear target = linear_of(settings.clouds ? core::at(look.clear, band) : core::at(look.all, band));
            const std::array<float, 3>& model = core::at(match->model, band);
            std::array<float, 3>& gain = core::at(input.gains, band - 1);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double ratio =
                    core::at(model, channel) > 1e-6 ? core::at(target, channel) / core::at(model, channel) : 1.0;
                core::at(gain, channel) =
                    static_cast<float>(std::pow(std::clamp(ratio, kLeastGain, kMostGain), strength));
            }
            core::at(input.gain_elevations, band - 1) =
                static_cast<float>(core::at(rt::kSkyBandRanges, band).centre * kDegrees);
        }
        const auto ground = static_cast<std::size_t>(rt::SkyBand::kGround);
        if (core::at(look.covered, ground) > kGroundCovered) {
            const Linear colour = linear_of(core::at(look.all, ground));
            input.ground = {static_cast<float>(core::at(colour, 0)), static_cast<float>(core::at(colour, 1)),
                            static_cast<float>(core::at(colour, 2))};
            input.ground_share = static_cast<float>(strength);
        }
    }
    // the clouds: their coverage (auto: from the level's), their colour (its hue, and how bright they are beside its
    // clear sky - a white cloud about 1.5 x as bright as the blue behind it)
    constexpr double kCoverageBase = 0.35;
    constexpr double kCoverageShare = 0.7;
    constexpr double kWhiteOverBlue = 1.5;
    constexpr double kDarkest = 0.35;
    constexpr double kBrightest = 1.3;
    match->coverage = settings.coverage >= 0.0F
                          ? std::clamp(settings.coverage, 0.0F, 1.0F)
                          : static_cast<float>(std::clamp(kCoverageBase + (kCoverageShare * look.coverage), 0.0, 1.0));
    match->cloud_gain = {1.0F, 1.0F, 1.0F};
    if (look.valid) {
        const Linear cloud = linear_of(look.cloud);
        const double most = std::max({core::at(cloud, 0), core::at(cloud, 1), core::at(cloud, 2), 1e-6});
        const double sky_luma =
            luma_of(linear_of(core::at(look.clear, static_cast<std::size_t>(rt::SkyBand::kMiddle))));
        const double brightness =
            std::clamp(luma_of(cloud) / std::max(sky_luma, 0.02) / kWhiteOverBlue, kDarkest, kBrightest);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(match->cloud_gain, channel) =
                static_cast<float>(std::pow((core::at(cloud, channel) / most) * brightness, strength));
        }
    }
}

void log_match(const RtSkyMatch& match, uint32_t scene)
{
    const rt::SkyLook& look = match.look;
    const auto rgb = [](const rt::Colour& colour) {
        return std::array<double, 3>{core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)};
    };
    const std::array<double, 3> zenith = rgb(core::at(look.clear, 3));
    const std::array<double, 3> middle = rgb(core::at(look.clear, 2));
    const std::array<double, 3> horizon = rgb(core::at(look.clear, 1));
    const std::array<double, 3> ground = rgb(core::at(look.all, 0));
    get_log(
        "vulkan: rt_sky: the level's sky (scene %08x): clear zenith %.3f %.3f %.3f, half way %.3f %.3f %.3f, horizon "
        "%.3f %.3f %.3f; ground %.3f %.3f %.3f (%.0f%% covered); clouds %.0f%% of it, colour %.3f %.3f %.3f; "
        "brightness %.3f%s",
        scene, core::at(zenith, 0), core::at(zenith, 1), core::at(zenith, 2), core::at(middle, 0), core::at(middle, 1),
        core::at(middle, 2), core::at(horizon, 0), core::at(horizon, 1), core::at(horizon, 2), core::at(ground, 0),
        core::at(ground, 1), core::at(ground, 2), 100.0 * core::at(look.covered, 0), 100.0 * look.coverage,
        static_cast<double>(core::at(look.cloud, 0)), static_cast<double>(core::at(look.cloud, 1)),
        static_cast<double>(core::at(look.cloud, 2)), static_cast<double>(look.brightness),
        look.valid ? "" : " - too little of it to match: the model alone");
    const AtmosphereInput& in = match.atmosphere;
    get_log("vulkan: rt_sky: matched - gains horizon %.2f %.2f %.2f, half way %.2f %.2f %.2f, overhead %.2f %.2f %.2f; "
            "ground %.0f%%; clouds' coverage %.2f, colour x %.2f %.2f %.2f",
            static_cast<double>(core::at(core::at(in.gains, 0), 0)),
            static_cast<double>(core::at(core::at(in.gains, 0), 1)),
            static_cast<double>(core::at(core::at(in.gains, 0), 2)),
            static_cast<double>(core::at(core::at(in.gains, 1), 0)),
            static_cast<double>(core::at(core::at(in.gains, 1), 1)),
            static_cast<double>(core::at(core::at(in.gains, 1), 2)),
            static_cast<double>(core::at(core::at(in.gains, 2), 0)),
            static_cast<double>(core::at(core::at(in.gains, 2), 1)),
            static_cast<double>(core::at(core::at(in.gains, 2), 2)), 100.0 * in.ground_share,
            static_cast<double>(match.coverage), static_cast<double>(core::at(match.cloud_gain, 0)),
            static_cast<double>(core::at(match.cloud_gain, 1)), static_cast<double>(core::at(match.cloud_gain, 2)));
}

// The sun the sky is drawn for: the visible one, else a default (logged once).
std::array<float, 3> sky_sun(Backend& state)
{
    std::array<float, 3> toward{};
    if (visible_sun(&toward)) {
        return toward;
    }
    if (first_report(state, kReportNoSun)) {
        get_log("vulkan: rt_sky: no sun shown or baked - the sky drawn with a sun 50 degrees up");
    }
    return kDefaultSkySun;
}

// The level matched again when needed (a new capture, the sun moved, the settings changed). Work is finished for
// the read-backs: the caller takes a new command buffer after.
bool ensure_match(Backend& state, const std::array<float, 3>& sun)
{
    SkyPassState& own = pass_state();
    const rt::SkyCapture capture = rt::sky_capture();
    const float same = (core::at(sun, 0) * core::at(own.matched_sun, 0)) +
                       (core::at(sun, 1) * core::at(own.matched_sun, 1)) +
                       (core::at(sun, 2) * core::at(own.matched_sun, 2));
    const bool new_capture = capture.serial != own.match.capture_serial;
    if (own.matched && !new_capture && same >= kSameSun && own.matched_settings == own.settings_serial) {
        return true;
    }
    if (new_capture) {
        rt::SkyLook look;
        if (capture.serial != 0 && read_capture(state, capture, &look)) {
            own.match.look = look;
        } else {
            own.match.look = rt::SkyLook{};
            if (capture.serial == 0 && first_report(state, kReportNoCapture)) {
                get_log("vulkan: rt_sky: no capture of the level's sky yet - the model's own colours until there is");
            }
        }
        own.match.capture_serial = capture.serial;
    }
    // the model's bands (an unmatched sky-view table, read back)
    AtmosphereInput plain;
    plain.sun = sun;
    if (!update_atmosphere(state, outside_pass(state), plain)) {
        return false;
    }
    AtmosphereTexels table;
    if (read_atmosphere_table(state, AtmosphereTable::kSkyView, &table)) {
        const std::array<Linear, rt::kSkyBands> means = band_means(table);
        for (std::size_t band = 0; band < rt::kSkyBands; ++band) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(core::at(own.match.model, band), channel) =
                    static_cast<float>(core::at(core::at(means, band), channel));
            }
        }
    }
    match_from(own.settings, sun, &own.match);
    if (new_capture && capture.serial != 0) {
        log_match(own.match, capture.scene);
    }
    own.matched_sun = sun;
    own.matched = true;
    own.matched_settings = own.settings_serial;
    return true;
}

// --- the pass ------------------------------------------------------------------------------------------------------------

struct Recording {
    Image* colour = nullptr;
    Image* depth = nullptr;
    SkySlot* slot = nullptr;
    VkPipeline composite = VK_NULL_HANDLE;
    VkDescriptorBufferInfo block{};
    std::array<uint32_t, 4> rect{};
    bool clouds = false;
};

uint32_t steps_for(unsigned quality)
{
    constexpr std::array<uint32_t, 3> kSteps{32, 48, 96};
    return core::at(kSteps, std::min<std::size_t>(quality, kSteps.size() - 1));
}
uint32_t scale_for(unsigned quality)
{
    constexpr std::array<uint32_t, 3> kScales{4, 2, 1};
    return core::at(kScales, std::min<std::size_t>(quality, kScales.size() - 1));
}

// The block for a view, and the frame's parameters (the check's).
SkyBlock block_for(const rt::ViewDrawn& view, const Recording& recording, const Matrix& to_world,
                   const std::array<float, 3>& sun, RtSkyFrame* frame)
{
    const SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    const SkySlot* slot = recording.slot;
    SkyBlock block{};
    block.to_world = column_major(to_world);
    if (slot != nullptr && slot->has_history) {
        block.prev_to_pixel = column_major(slot->last_to_pixel);
    }
    // the camera's altitude above the level's ground (the baked sun's estimate, else the camera's own height) + the
    // model's viewer
    const rt::BakedSun& baked = rt::baked_sun();
    const double ground = baked.ground_known ? baked.ground : 0.0;
    const double altitude =
        rt::kViewerAltitude +
        std::max(0.0, (static_cast<double>(core::at(view.camera.position, 1)) - ground) / rt::kGameUnitsPerMetre);
    const double drift = static_cast<double>(settings.speed) * rt::kCloudWindSpeed * view.game_seconds;
    const std::array<double, 2> offset{
        (core::at(view.camera.position, 0) / rt::kGameUnitsPerMetre) + (core::at(rt::kCloudWind, 0) * drift),
        (core::at(view.camera.position, 2) / rt::kGameUnitsPerMetre) + (core::at(rt::kCloudWind, 1) * drift)};
    // the sun's light at the clouds (the atmosphere's transmittance half way up the layer) and the sky's
    const double layer = rt::kGroundRadius + (0.5 * (rt::kCloudBase + rt::kCloudTop));
    const rt::Rgb through = rt::transmittance_to_top(
        {.radius = layer, .cosine = std::clamp(static_cast<double>(core::at(sun, 1)), -1.0, 1.0)}, 64);
    const double sun_scale = rt::sky_exposure() * kCloudSunScale;
    const AtmosphereInput& atmosphere = own.match.atmosphere;
    std::array<float, 3> sun_light{};
    std::array<float, 3> ambient_bottom{};
    std::array<float, 3> ambient_top{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(sun_light, channel) = static_cast<float>(core::at(through, channel) * sun_scale);
        const double horizon =
            core::at(core::at(own.match.model, 1), channel) * core::at(core::at(atmosphere.gains, 0), channel);
        const double zenith =
            core::at(core::at(own.match.model, 3), channel) * core::at(core::at(atmosphere.gains, 2), channel);
        core::at(ambient_bottom, channel) = static_cast<float>(horizon * kCloudAmbientBottom);
        core::at(ambient_top, channel) = static_cast<float>(zenith * kCloudAmbientTop);
    }
    const uint64_t frames = slot != nullptr ? slot->frames : 0;
    const auto jitter_offset = static_cast<float>(std::fmod(static_cast<double>(frames) * kGolden, 1.0));
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), static_cast<float>(altitude)};
    block.sun = {core::at(sun, 0), core::at(sun, 1), core::at(sun, 2), own.match.coverage};
    block.sun_light = {core::at(sun_light, 0), core::at(sun_light, 1), core::at(sun_light, 2), 1.0F};
    block.ambient_bottom = {core::at(ambient_bottom, 0), core::at(ambient_bottom, 1), core::at(ambient_bottom, 2),
                            static_cast<float>(kCloudAerialDistance)};
    block.ambient_top = {core::at(ambient_top, 0), core::at(ambient_top, 1), core::at(ambient_top, 2), jitter_offset};
    block.offset = {static_cast<float>(core::at(offset, 0)), static_cast<float>(core::at(offset, 1)), kSkyHistoryBlend,
                    0.0F};
    block.cloud_tint = {core::at(own.match.cloud_gain, 0), core::at(own.match.cloud_gain, 1),
                        core::at(own.match.cloud_gain, 2), 0.0F};
    block.rect = {static_cast<int32_t>(core::at(recording.rect, 0)), static_cast<int32_t>(core::at(recording.rect, 1)),
                  static_cast<int32_t>(core::at(recording.rect, 2)), static_cast<int32_t>(core::at(recording.rect, 3))};
    const uint32_t scale = scale_for(settings.quality);
    const Extent low = low_extent(recording.rect, scale);
    block.low = {static_cast<int32_t>(low.width), static_cast<int32_t>(low.height), static_cast<int32_t>(scale),
                 static_cast<int32_t>(steps_for(settings.quality))};
    block.flags = {static_cast<int32_t>(recording.colour->samples), recording.clouds ? 1 : 0,
                   slot != nullptr && slot->has_history ? 1 : 0, settings.hdr ? 1 : 0};
    *frame = RtSkyFrame{.sun = sun,
                        .camera = view.camera.position,
                        .camera_altitude = altitude,
                        .offset = offset,
                        .sun_light = sun_light,
                        .ambient_bottom = ambient_bottom,
                        .ambient_top = ambient_top,
                        .coverage = own.match.coverage,
                        .steps = steps_for(settings.quality),
                        .scale = scale,
                        .jitter_offset = jitter_offset,
                        .to_world = block.to_world,
                        .history = slot != nullptr && slot->has_history};
    return block;
}

void record_commands(Backend& state, const Recording& recording, VkCommandBuffer commands)
{
    SkyPassState& own = pass_state();
    SkySlot* const slot = recording.slot;
    Image* const depth = recording.depth;
    const Stamps stamps = begin_timing(state, commands, recording.rect);
    stamps.stamp(0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, nullptr, 0, nullptr);
    const Image* history_a = nullptr;
    const Image* history_b = nullptr;
    if (recording.clouds) {
        depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
        const Extent low = slot->march.extent;
        const std::size_t multisampled = depth->samples != VK_SAMPLE_COUNT_1_BIT ? 1U : 0U;
        dispatch(commands, core::at(own.march, multisampled),
                 {.depth = depth,
                  .sky = true,
                  .clouds = true,
                  .out_a = &slot->march,
                  .out_b = &slot->march_info,
                  .block = recording.block},
                 low);
        stamps.stamp(1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        const uint32_t previous = slot->current;
        const uint32_t next = 1U - previous;
        dispatch(commands, own.temporal,
                 {.sky = true,
                  .clouds = true,
                  .out_a = &core::at(slot->history, next),
                  .out_b = &core::at(slot->history_info, next),
                  .hist_a = &core::at(slot->history, previous),
                  .hist_b = &core::at(slot->history_info, previous),
                  .load_a = &slot->march,
                  .load_b = &slot->march_info,
                  .block = recording.block},
                 low);
        slot->current = next;
        history_a = &core::at(slot->history, next);
        history_b = &core::at(slot->history_info, next);
        stamps.stamp(2, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);
    } else {
        stamps.stamp(1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        stamps.stamp(2, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        if (depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);
        }
    }
    record_composite(commands, recording.composite, recording.colour, depth,
                     {.sky = true, .load_a = history_a, .load_b = history_b, .block = recording.block}, recording.rect);
    stamps.stamp(3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (depth->layout != depth->resting) {
        depth_barrier(commands, depth, depth->resting, false);
    }
    if (recording.colour->layout != recording.colour->resting) {
        Memory::transition(commands, recording.colour, recording.colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    recording.colour->used = serial;
    depth->used = serial;
    atmosphere_used(serial);
    if (recording.clouds) {
        cloud_noise_used(serial);
        for (Image* image : images_of(*slot)) {
            image->used = serial;
        }
    }
}

// The clouds' shadows over a viewport's opaque world (rt_cloud_shadows; the listener's opaque_drawn). False: skipped.
bool record_shadow_pass(Backend& state, const rt::ViewDrawn& view)
{
    const SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    if (own.failed || !settings.on || !settings.clouds || !settings.cloud_shadows || !own.matched) {
        return false;
    }
    Recording recording;
    recording.colour = image_of(core::at(state.targets, 0));
    recording.depth = image_of(state.depth);
    if (!targets_usable(recording.colour, recording.depth)) {
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
    Matrix to_world{};
    if (!invert(world_to_pixel(clipped), &to_world)) {
        return false;
    }
    std::string error;
    VkCommandBuffer commands = outside_pass(state);
    if (!ensure_objects(state, &error) || !update_cloud_noise(state, commands)) {
        return false;
    }
    const VkPipeline pipeline = composite_pipeline(state, *recording.colour, *recording.depth, true, &error, true);
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }
    recording.clouds = true;
    RtSkyFrame unused;
    const SkyBlock block = block_for(clipped, recording, to_world, own.frame.sun, &unused);
    const void* const block_bytes = &block;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(block_bytes), sizeof(block)), state.uniform_alignment);
    if (!span) {
        return false;
    }
    depth_barrier(commands, recording.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
    record_composite(commands, pipeline, recording.colour, nullptr,
                     {.depth = recording.depth,
                      .clouds = true,
                      .block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)}},
                     recording.rect);
    depth_barrier(commands, recording.depth, recording.depth->resting, false);
    if (recording.colour->layout != recording.colour->resting) {
        Memory::transition(commands, recording.colour, recording.colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    recording.colour->used = serial;
    recording.depth->used = serial;
    cloud_noise_used(serial);
    if (first_report(state, kReportShadows)) {
        get_log("vulkan: rt_sky: the clouds' shadows drawn over the level (%ux%u)", core::at(recording.rect, 2),
                core::at(recording.rect, 3));
    }
    return true;
}

const char* clouds_text(bool clouds, unsigned quality)
{
    if (!clouds) {
        return "";
    }
    if (quality == 0) {
        return " and clouds at a quarter resolution";
    }
    return quality == 1 ? " and clouds at half resolution" : " and clouds at full resolution";
}

bool record_pass(Backend& state, const rt::ViewDrawn& view)
{
    SkyPassState& own = pass_state();
    const RtSkySettings& settings = own.settings;
    if (own.failed || !settings.on) {
        return false;
    }
    Recording recording;
    recording.colour = image_of(core::at(state.targets, 0));
    recording.depth = image_of(state.depth);
    if (!targets_usable(recording.colour, recording.depth)) {
        if (first_report(state, kReportTargets)) {
            const Image* colour = recording.colour;
            const Image* depth = recording.depth;
            get_log(
                "vulkan: rt_sky: the bound targets can't be drawn over (no samplable depth of the colour's size and "
                "samples: colour %s, depth %s%s) - the original sky kept there",
                colour == nullptr ? "none"
                                  : text::format("%ux%u x%u", colour->extent.width, colour->extent.height,
                                                 static_cast<unsigned>(colour->samples))
                                        .c_str(),
                depth == nullptr ? "none"
                                 : text::format("%ux%u x%u", depth->extent.width, depth->extent.height,
                                                static_cast<unsigned>(depth->samples))
                                       .c_str(),
                depth != nullptr && (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ? ", not samplable" : "");
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
    const Matrix to_pixel = world_to_pixel(clipped);
    Matrix to_world{};
    if (!invert(to_pixel, &to_world)) {
        return false;
    }
    std::string error;
    if (!ensure_objects(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_sky: the passes couldn't be made (%s) - the original sky kept", error.c_str());
        }
        return false;
    }
    // 0 - 1: the level matched, the atmosphere's tables
    const std::array<float, 3> sun = sky_sun(state);
    if (!ensure_match(state, sun)) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    if (!update_atmosphere(state, commands, own.match.atmosphere)) {
        return false;
    }
    recording.clouds = settings.clouds && update_cloud_noise(state, commands);
    if (recording.clouds) {
        recording.slot = slot_for(state, recording.rect, scale_for(settings.quality));
        recording.clouds = recording.slot != nullptr;
        if (recording.slot != nullptr && recording.slot->fresh) {
            clear_slot(commands, recording.slot);
        }
    }
    recording.composite = composite_pipeline(state, *recording.colour, *recording.depth, recording.clouds, &error);
    if (recording.composite == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_sky: the passes couldn't be made (%s) - the original sky kept", error.c_str());
        }
        return false;
    }
    const SkyBlock block = block_for(clipped, recording, to_world, sun, &own.frame);
    const void* const block_bytes = &block;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(block_bytes), sizeof(block)), state.uniform_alignment);
    if (!span) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: rt_sky: no room in the ring for the pass's block - the pass skipped");
        }
        return false;
    }
    recording.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)};
    record_commands(state, recording, commands);
    if (recording.slot != nullptr) {
        recording.slot->last_to_pixel = to_pixel;
        recording.slot->has_history = true;
        ++recording.slot->frames;
    }
    ++own.passes;
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_sky: the first pass (%ux%u, %u sample%s; the atmosphere%s; sun %.1f degrees up)",
                core::at(recording.rect, 2), core::at(recording.rect, 3),
                static_cast<unsigned>(recording.colour->samples), recording.colour->samples == 1 ? "" : "s",
                clouds_text(recording.clouds, settings.quality),
                std::asin(std::clamp(static_cast<double>(core::at(sun, 1)), -1.0, 1.0)) / kDegrees);
    }
    return true;
}

} // namespace

void set_rt_sky_settings(const RtSkySettings& settings)
{
    RtSkySettings kept = settings;
    kept.match = std::isfinite(kept.match) ? std::clamp(kept.match, 0.0F, 1.0F) : kRtSkyMatch;
    kept.coverage = std::isfinite(kept.coverage) ? std::min(kept.coverage, 1.0F) : kRtCloudCoverageAuto;
    constexpr float kFastest = 20.0F;
    kept.speed = std::isfinite(kept.speed) ? std::clamp(kept.speed, 0.0F, kFastest) : 1.0F;
    kept.quality = std::min(kept.quality, 2U);
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        SkyPassState& own = pass_state();
        own.settings = kept;
        ++own.settings_serial;
        for (const std::unique_ptr<SkySlot>& slot : own.slots) {
            slot->has_history = false;
        }
        set_table_follows_sky_view(kept.on);
        invalidate_sky();
    }
    rt::set_sky_replacement({.on = kept.on, .keep_layers = !kept.clouds});
    rt::set_sky_capture_wanted(kept.on);
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtSkySettings rt_sky_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return pass_state().settings;
}

VkImageUsageFlags rt_sky_depth_usage(const Backend& state, VkFormat format)
{
    if (!pass_state().settings.on) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool record_rt_sky_pass(Backend& state, const rt::ViewDrawn& view) { return record_pass(state, view); }

uint64_t rt_sky_passes() { return pass_state().passes; }

RtSkyMatch rt_sky_match() { return pass_state().match; }

RtSkyFrame rt_sky_frame() { return pass_state().frame; }

RtSkyTiming rt_sky_timing() { return pass_state().timing; }

bool read_rt_sky_image(Backend& state, const std::array<uint32_t, 4>& rect, RtSkyImage which, RtSkyTexels* out)
{
    *out = RtSkyTexels{};
    for (const std::unique_ptr<SkySlot>& slot : pass_state().slots) {
        if (slot->rect != rect) {
            continue;
        }
        Image* image = &slot->march;
        switch (which) {
        case RtSkyImage::kMarch:
            break;
        case RtSkyImage::kMarchInfo:
            image = &slot->march_info;
            break;
        case RtSkyImage::kHistory:
            image = &core::at(slot->history, slot->current);
            break;
        case RtSkyImage::kHistoryInfo:
            image = &core::at(slot->history_info, slot->current);
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

void release_rt_sky(Backend& state)
{
    SkyPassState& own = pass_state();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<SkySlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (device != nullptr) {
        for (const CompositePipeline& made : own.composites) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : {own.march.front(), own.march.back(), own.temporal}) {
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
    const RtSkySettings settings = own.settings;
    const uint64_t serial = own.settings_serial;
    own = SkyPassState{};
    own.settings = settings;
    own.settings_serial = serial + 1;
    release_cloud_noise(state);
}

} // namespace render::vulkan
