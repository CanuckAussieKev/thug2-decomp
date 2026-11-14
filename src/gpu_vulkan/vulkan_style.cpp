// NOT ORIGINAL - an art style: the toon filter (vulkan_style.h; docs/design/vulkan.md "Art style").
#include "gpu_vulkan/vulkan_style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_style_film.h" // (NOT ORIGINAL hook: the vhs, retro and watercolour styles)
#include "gpu_vulkan/vulkan_style_looks.h"
#include "gpu_vulkan/vulkan_style_twin.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportTarget = 0x53545901U;
constexpr uint64_t kReportPipeline = 0x53545902U;
constexpr uint64_t kReportFirstPass = 0x53545903U;
constexpr uint64_t kReportNoDepth = 0x53545904U;
constexpr uint64_t kReportUnbuilt = 0x53545910U; // (+ the style)

constexpr uint32_t kGroupSide = 8;
constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kBindingColour = 0; // the viewport's colour (sampled)
constexpr uint32_t kBindingDepth = 1;  // the depth surface (sampled)
constexpr uint32_t kBindingOut = 2;    // a compute pass's output (storage)
constexpr uint32_t kBindingHalf = 3;   // the half-resolution colour, or the smoothed one (sampled)
constexpr uint32_t kBindingLinear = 4; // the linear depth (sampled)
constexpr uint32_t kBindingEdges = 5;  // the edge pass's output (sampled; comic, sketch)
constexpr uint32_t kBindingCount = 6;
constexpr float kGamma = 2.2F;
constexpr float kHalfWeight = 0.5F;
constexpr int kQuadrants = 4;
constexpr uint32_t kTimerPairs = 8;
constexpr uint64_t kTimingLogEvery = 300;
constexpr float kLeastWeights = 1.0e-30F; // (the Kuwahara weights' sum below which the centre is kept)

// --- the shaders -------------------------------------------------------------------------------------------------

constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares: the constants (StyleConstants below) and the helpers.
constexpr const char* kCommon = R"(
layout(push_constant) uniform Style {
    ivec4 out_rect;   // the colour's viewport in its target: x, y, width, height
    ivec4 depth_rect; // the depth's
    ivec4 sizes;      // x, y: the half-resolution size; z: the Kuwahara radius (0: none); w: the outline spacing h
    vec4 tone;        // x: bands (0: none); y: half a band edge's softness; z: saturation; w: strength
    vec4 outline;     // x: outlines (0: none); y: the focal length (depth pixels); z: fade start; w: fade end
    vec4 projection;  // P22, P32, P23, P33
    vec4 depth_map;   // x: the depth range's minimum; y: 1 / its span; z: 1 with a depth; w: unused
    vec4 lines;       // x: the line's lightness; y: its chroma; z: the crease's weight; w: the lightness kept unbanded
    vec4 look_a;      // (the looks' - vulkan_style_looks.h) x: grain; y: the grain's cell (pixels); z: the frame's seed;
                      // w: vignette
    vec4 look_b;      // each look's own
    vec4 look_c;      // noir's dark colour
    vec4 look_d;      // noir's light colour
} pc;

const float kSky = 1.0e9;

float finite(float c) { return c >= 0.0 ? min(c, 1.0) : 0.0; } // (not a number fails the comparison: 0)
vec3 finite3(vec3 c) { return vec3(finite(c.r), finite(c.g), finite(c.b)); }
)";

constexpr const char* kPrepBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 2, r32f) uniform writeonly image2D o_out;

float linear_depth(float d)
{
    if (!(d < 1.0)) {
        return kSky;
    }
    float ndc = (d - pc.depth_map.x) * pc.depth_map.y;
    float z = (pc.projection.y - ndc * pc.projection.w) / (ndc * pc.projection.z - pc.projection.x);
    float w = z * pc.projection.z + pc.projection.w;
    float along = abs(pc.projection.z) > 0.5 ? w : abs(z);
    return (along > 0.0 && along < kSky) ? along : kSky;
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.depth_rect.zw))) {
        return;
    }
    ivec2 p = pc.depth_rect.xy + local;
    imageStore(o_out, p, vec4(linear_depth(texelFetch(u_depth, p, 0).r), 0.0, 0.0, 0.0));
}
)";

constexpr const char* kDownBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_colour;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, pc.sizes.xy))) {
        return;
    }
    ivec2 last = pc.out_rect.xy + pc.out_rect.zw - 1;
    ivec2 base = pc.out_rect.xy + q * 2;
    vec3 sum = finite3(texelFetch(u_colour, min(base, last), 0).rgb);
    sum += finite3(texelFetch(u_colour, min(base + ivec2(1, 0), last), 0).rgb);
    sum += finite3(texelFetch(u_colour, min(base + ivec2(0, 1), last), 0).rgb);
    sum += finite3(texelFetch(u_colour, min(base + ivec2(1, 1), last), 0).rgb);
    imageStore(o_out, q, vec4(sum * 0.25, 1.0));
}
)";

constexpr const char* kKuwaharaBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) uniform sampler2D u_half;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, pc.sizes.xy))) {
        return;
    }
    int r = pc.sizes.z;
    ivec2 last = pc.sizes.xy - 1;
    vec3 sum[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    vec3 squares[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            vec3 c = texelFetch(u_half, clamp(q + ivec2(dx, dy), ivec2(0), last), 0).rgb;
            vec3 c2 = c * c;
            if (dx <= 0 && dy <= 0) { sum[0] += c; squares[0] += c2; }
            if (dx >= 0 && dy <= 0) { sum[1] += c; squares[1] += c2; }
            if (dx <= 0 && dy >= 0) { sum[2] += c; squares[2] += c2; }
            if (dx >= 0 && dy >= 0) { sum[3] += c; squares[3] += c2; }
        }
    }
    float count = float((r + 1) * (r + 1));
    vec3 total = vec3(0.0);
    float weights = 0.0;
    for (int k = 0; k < 4; ++k) {
        vec3 mean = sum[k] / count;
        vec3 spread = max(squares[k] / count - mean * mean, vec3(0.0));
        float t = (spread.r + spread.g + spread.b) * 65025.0;
        float t2 = t * t;
        float w = 1.0 / (1.0 + t2 * t2);
        total += mean * w;
        weights += w;
    }
    vec3 result = weights > 1.0e-30 ? total / weights : texelFetch(u_half, q, 0).rgb;
    imageStore(o_out, q, vec4(result, 1.0));
}
)";

// What every composite shares (style_reference's and the looks' twins): the bindings it reads, the edge-aware upsample
// of the smoothed colour, OKLab.
constexpr const char* kCompositeShared = R"(
layout(set = 0, binding = 0) uniform sampler2D u_colour;
layout(set = 0, binding = 3) uniform sampler2D u_smooth;
layout(location = 0) out vec4 o_colour;

const float kGuide = 22.0;
const float kWeightFloor = 1.0e-4;

vec3 upsampled(ivec2 local, vec3 own)
{
    vec2 at = (vec2(local) + 0.5) * 0.5 - 0.5;
    ivec2 corner = ivec2(floor(at));
    vec2 f = at - vec2(corner);
    ivec2 last = pc.sizes.xy - 1;
    vec3 total = vec3(0.0);
    vec3 plain = vec3(0.0);
    float weights = 0.0;
    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 2; ++ox) {
            vec3 c = texelFetch(u_smooth, clamp(corner + ivec2(ox, oy), ivec2(0), last), 0).rgb;
            float wb = (ox == 1 ? f.x : 1.0 - f.x) * (oy == 1 ? f.y : 1.0 - f.y);
            vec3 d = c - own;
            float w = wb * exp(-kGuide * dot(d, d));
            total += c * w;
            plain += c * wb;
            weights += w;
        }
    }
    return weights >= kWeightFloor ? total / weights : plain;
}

vec3 to_lab(vec3 c)
{
    vec3 lin = pow(c, vec3(2.2));
    float l = 0.4122214708 * lin.r + 0.5363325363 * lin.g + 0.0514459929 * lin.b;
    float m = 0.2119034982 * lin.r + 0.6806995451 * lin.g + 0.1073969566 * lin.b;
    float s = 0.0883024619 * lin.r + 0.2817188376 * lin.g + 0.6299787005 * lin.b;
    l = pow(max(l, 0.0), 1.0 / 3.0);
    m = pow(max(m, 0.0), 1.0 / 3.0);
    s = pow(max(s, 0.0), 1.0 / 3.0);
    return vec3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
}

vec3 from_lab(vec3 lab)
{
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    vec3 lin = vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
    return clamp(pow(max(lin, vec3(0.0)), vec3(1.0 / 2.2)), 0.0, 1.0);
}
)";

// The depth's edges (vulkan_style.h step 4c; edge_at is the twin): the toon composite's and the edge pass's.
constexpr const char* kEdgeGlsl = R"(
layout(set = 0, binding = 4) uniform sampler2D u_linear;

const float kCreaseLow = 0.18;
const float kCreaseHigh = 0.45;
const float kFarSide = 0.02;
const float kStepSide = 0.02;

float depth_at(ivec2 p) { return texelFetch(u_linear, p, 0).r; }

// One axis' edge: s from the pixel's linear depth and its two neighbours'.
float axis_edge(float s)
{
    float v = abs(s) * pc.outline.y / float(pc.sizes.w);
    float e = smoothstep(kCreaseLow, kCreaseHigh, v);
    if (s < -kFarSide) {
        e = 0.0;
    }
    if (!(s > kStepSide)) {
        e *= pc.lines.z;
    }
    return e;
}

// The depth pixel under an output pixel (the depth's rectangle scaled to the colour's).
ivec2 depth_pixel(ivec2 local)
{
    vec2 scale = vec2(pc.depth_rect.zw) / vec2(pc.out_rect.zw);
    return pc.depth_rect.xy + min(ivec2((vec2(local) + 0.5) * scale), pc.depth_rect.zw - 1);
}

// The edge at a depth pixel, faded with the distance (0 on the sky).
float depth_edge(ivec2 q)
{
    ivec2 lo = pc.depth_rect.xy;
    ivec2 hi = pc.depth_rect.xy + pc.depth_rect.zw - 1;
    float zc = depth_at(q);
    if (zc >= kSky * 0.5) {
        return 0.0;
    }
    int h = pc.sizes.w;
    float ex = 0.0;
    float ey = 0.0;
    if (q.x - h >= lo.x && q.x + h <= hi.x) {
        float s = 1.0 - zc * (1.0 / depth_at(q - ivec2(h, 0)) + 1.0 / depth_at(q + ivec2(h, 0))) * 0.5;
        ex = axis_edge(s);
    }
    if (q.y - h >= lo.y && q.y + h <= hi.y) {
        float s = 1.0 - zc * (1.0 / depth_at(q - ivec2(0, h)) + 1.0 / depth_at(q + ivec2(0, h))) * 0.5;
        ey = axis_edge(s);
    }
    return max(ex, ey) * (1.0 - smoothstep(pc.outline.z, pc.outline.w, zc));
}
)";

// The edge pass (the comic's and the sketch's lines): each depth pixel's edge into an R32F image (the looks' ink widens
// it).
constexpr const char* kEdgePassBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, r32f) uniform writeonly image2D o_out;

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.depth_rect.zw))) {
        return;
    }
    ivec2 q = pc.depth_rect.xy + local;
    imageStore(o_out, q, vec4(depth_edge(q), 0.0, 0.0, 0.0));
}
)";

// The toon composite (style_reference is its twin): reads the viewport's colour, the smoothed half-resolution colour
// and the linear depth, writes the target's colour.
constexpr const char* kToonBody = R"(
vec3 simplified(vec3 lab)
{
    float n = pc.tone.x;
    if (n > 0.0) {
        float t = lab.x * n - 0.5;
        float i = floor(t);
        float f = t - i;
        float banded = clamp((i + 0.5 + smoothstep(0.5 - pc.tone.y, 0.5 + pc.tone.y, f)) / n, 0.0, 1.0);
        lab.x = mix(banded, lab.x, pc.lines.w);
    }
    lab.yz *= pc.tone.z;
    return lab;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = finite3(texelFetch(u_colour, p, 0).rgb);
    vec3 smoothed = pc.sizes.z > 0 ? upsampled(local, own) : own;
    vec3 lab = simplified(to_lab(smoothed));
    if (pc.depth_map.z > 0.5 && pc.outline.x > 0.0) {
        float e = depth_edge(depth_pixel(local)) * pc.outline.x;
        vec3 ink = vec3(lab.x * pc.lines.x, lab.yz * pc.lines.y);
        lab = mix(lab, ink, clamp(e, 0.0, 1.0));
    }
    vec3 toon = from_lab(lab);
    o_colour = vec4(mix(own, toon, pc.tone.w), 1.0);
}
)";

// The push constants, as the shaders' block lays them out.
struct StyleConstants {
    std::array<int32_t, 4> out_rect{};
    std::array<int32_t, 4> depth_rect{};
    std::array<int32_t, 4> sizes{};
    std::array<float, 4> tone{};
    std::array<float, 4> outline{};
    std::array<float, 4> projection{};
    std::array<float, 4> depth_map{};
    std::array<float, 4> lines{};
    std::array<float, 4> look_a{};
    std::array<float, 4> look_b{};
    std::array<float, 4> look_c{};
    std::array<float, 4> look_d{};
};
constexpr std::size_t kConstantBytes = 192; // (past Vulkan 1.0's least 128: checked against the device's limit)
static_assert(sizeof(StyleConstants) == kConstantBytes, "the shaders' push-constant block");

struct CompositePipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ArtStyle look = ArtStyle::kToon;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct StyleState {
    StyleSettings settings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> prep{}; // single-sampled depth, multisampled
    VkPipeline down = VK_NULL_HANDLE;
    VkPipeline kuwahara = VK_NULL_HANDLE;
    VkPipeline edge_pass = VK_NULL_HANDLE; // (comic, sketch)
    std::vector<CompositePipeline> composites;
    Image source; // the viewport's colour copied out (single-sampled)
    Image half;   // the half-resolution colour
    Image smooth; // ... smoothed
    Image linear; // the linear depth
    Image edges;  // the edge pass's output (comic, sketch; the depth's size)
    StyleParameters last; // the last pass's
    bool failed = false;
    bool outlined = false;
    uint64_t passes = 0;
    uint64_t upscales_seen = 0; // the upscaler's passes when the view's opaque world was drawn
    // the passes timed on the device (fog's scheme: kTimerPairs pairs in turn, a pair's result read when it is reused)
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0; // (negative: no timestamps here)
    std::array<uint64_t, kTimerPairs> timer_serials{};
    uint64_t timed = 0;
    uint64_t window_samples = 0;
    double window_milliseconds = 0.0;
};

StyleState& style()
{
    static StyleState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener: last at world_drawn (after the tone mapping and the upscaler).
class StyleListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kStyle; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override { style().upscales_seen = upscale_passes(); }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_style_pass(state, view);
        }
    }
};

StyleListener& listener()
{
    static StyleListener instance;
    return instance;
}

} // namespace

// --- the processor's twin --------------------------------------------------------------------------------------

namespace style_twin {

using Matrix3 = std::array<std::array<float, 3>, 3>;

// OKLab (Bjorn Ottosson's): linear sRGB -> the cone responses, their cube roots -> L, a, b; and back.
constexpr Matrix3 kToCones{{{0.4122214708F, 0.5363325363F, 0.0514459929F},
                            {0.2119034982F, 0.6806995451F, 0.1073969566F},
                            {0.0883024619F, 0.2817188376F, 0.6299787005F}}};
constexpr Matrix3 kConesToLab{{{0.2104542553F, 0.7936177850F, -0.0040720468F},
                               {1.9779984951F, -2.4285922050F, 0.4505937099F},
                               {0.0259040371F, 0.7827717662F, -0.8086757660F}}};
constexpr Matrix3 kLabToCones{{{1.0F, 0.3963377774F, 0.2158037573F},
                               {1.0F, -0.1055613458F, -0.0638541728F},
                               {1.0F, -0.0894841775F, -1.2914855480F}}};
constexpr Matrix3 kConesToLinear{{{4.0767416621F, -3.3077115913F, 0.2309699292F},
                                  {-1.2684380046F, 2.6097574011F, -0.3413193965F},
                                  {-0.0041960863F, -0.7034186147F, 1.7076147010F}}};
constexpr float kThird = 1.0F / 3.0F;
constexpr float kHermiteThree = 3.0F; // (smoothstep's 3 t^2 - 2 t^3)
constexpr float kHermiteTwo = 2.0F;

float finite(float value) { return value >= 0.0F ? std::min(value, 1.0F) : 0.0F; }

StyleRgb finite3(const StyleRgb& colour)
{
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = finite(core::at(colour, channel));
    }
    return out;
}

// A value as a 16-bit float stores it (rounded to the nearest; the passes' images are RGBA16F).
float as_half(float value)
{
    constexpr float kLargestHalf = 65504.0F;
    constexpr int kMantissaBits = 11;   // (10 stored and the leading one)
    constexpr int kLeastExponent = -13; // (frexp's for the smallest normal half: 2^-14 = 0.5 x 2^-13)
    if (!std::isfinite(value) || value == 0.0F) {
        return value;
    }
    if (std::abs(value) > kLargestHalf) {
        return std::copysign(std::numeric_limits<float>::infinity(), value);
    }
    int exponent = 0;
    (void)std::frexp(value, &exponent);
    exponent = std::max(exponent, kLeastExponent);
    const float quantum = std::ldexp(1.0F, exponent - kMantissaBits);
    return std::nearbyint(value / quantum) * quantum;
}

StyleRgb as_half3(const StyleRgb& colour)
{
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = as_half(core::at(colour, channel));
    }
    return out;
}

float smoothstep(float low, float high, float value)
{
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (kHermiteThree - (kHermiteTwo * along));
}

namespace {

StyleRgb times(const Matrix3& matrix, const StyleRgb& vector)
{
    StyleRgb out{};
    for (std::size_t row = 0; row < out.size(); ++row) {
        const std::array<float, 3>& weights = core::at(matrix, row);
        core::at(out, row) = (core::at(weights, 0) * core::at(vector, 0)) + (core::at(weights, 1) * core::at(vector, 1)) +
                             (core::at(weights, 2) * core::at(vector, 2));
    }
    return out;
}

} // namespace

StyleRgb to_lab(const StyleRgb& colour)
{
    StyleRgb linear{};
    for (std::size_t channel = 0; channel < linear.size(); ++channel) {
        core::at(linear, channel) = std::pow(core::at(colour, channel), kGamma);
    }
    StyleRgb cones = times(kToCones, linear);
    for (float& cone : cones) {
        cone = std::pow(std::max(cone, 0.0F), kThird);
    }
    return times(kConesToLab, cones);
}

StyleRgb from_lab(const StyleRgb& lab)
{
    StyleRgb cones = times(kLabToCones, lab);
    for (float& cone : cones) {
        cone = cone * cone * cone;
    }
    const StyleRgb linear = times(kConesToLinear, cones);
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = std::clamp(std::pow(std::max(core::at(linear, channel), 0.0F), 1.0F / kGamma), 0.0F, 1.0F);
    }
    return out;
}

StyleRgb simplified_lab(const StyleParameters& parameters, StyleRgb lab)
{
    const float bands = parameters.bands;
    if (bands > 0.0F) {
        const float position = (core::at(lab, kLightness) * bands) - kHalfWeight;
        const float whole = std::floor(position);
        const float part = position - whole;
        const float soft = kStyleBandSoftness * kHalfWeight;
        const float banded =
            std::clamp((whole + kHalfWeight + smoothstep(kHalfWeight - soft, kHalfWeight + soft, part)) / bands, 0.0F, 1.0F);
        core::at(lab, kLightness) = banded + ((core::at(lab, kLightness) - banded) * kStyleBandHold);
    }
    core::at(lab, kGreenRed) *= parameters.saturation;
    core::at(lab, kBlueYellow) *= parameters.saturation;
    return lab;
}

float mix(float from, float to, float part) { return from + ((to - from) * part); }

StyleRgb mix3(const StyleRgb& from, const StyleRgb& to, float part)
{
    StyleRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = core::at(from, channel) + ((core::at(to, channel) - core::at(from, channel)) * part);
    }
    return out;
}

StyleRgb upsampled(const std::vector<StyleRgb>& smooth, Extent half, Pixel at, const StyleRgb& own, bool guided)
{
    const float place_x = ((static_cast<float>(at.column) + kHalfWeight) * kHalfWeight) - kHalfWeight;
    const float place_y = ((static_cast<float>(at.row) + kHalfWeight) * kHalfWeight) - kHalfWeight;
    const float floor_x = std::floor(place_x);
    const float floor_y = std::floor(place_y);
    const Pixel corner{.column = static_cast<int>(floor_x), .row = static_cast<int>(floor_y)};
    const float part_x = place_x - floor_x;
    const float part_y = place_y - floor_y;
    StyleRgb total{};
    StyleRgb plain{};
    float weights = 0.0F;
    for (int down = 0; down < 2; ++down) {
        for (int across = 0; across < 2; ++across) {
            const StyleRgb& texel = clamped_at(smooth, half, {.column = corner.column + across, .row = corner.row + down});
            const float bilinear = (across == 1 ? part_x : 1.0F - part_x) * (down == 1 ? part_y : 1.0F - part_y);
            float distance = 0.0F;
            for (std::size_t channel = 0; channel < texel.size(); ++channel) {
                const float difference = core::at(texel, channel) - core::at(own, channel);
                distance += difference * difference;
            }
            const float weight = bilinear * std::exp(-kStyleGuide * distance);
            for (std::size_t channel = 0; channel < texel.size(); ++channel) {
                core::at(total, channel) += core::at(texel, channel) * weight;
                core::at(plain, channel) += core::at(texel, channel) * bilinear;
            }
            weights += weight;
        }
    }
    if (!guided || weights < kStyleWeightFloor) {
        return plain;
    }
    for (float& value : total) {
        value /= weights;
    }
    return total;
}

namespace {

// One axis' edge from its s (vulkan_style.h step 4c).
float axis_edge(const StyleParameters& parameters, float stands_out)
{
    const float crease = std::abs(stands_out) * parameters.focal / static_cast<float>(parameters.spacing);
    float edge = smoothstep(kStyleCreaseLow, kStyleCreaseHigh, crease);
    if (stands_out < -kStyleFarSide) {
        edge = 0.0F;
    }
    if (!(stands_out > kStyleStepSide)) {
        edge *= kStyleCreaseWeight;
    }
    return edge;
}

} // namespace

float edge_at(const StyleParameters& parameters, const std::vector<float>& depth, Extent size, Pixel at)
{
    const auto depth_at = [&](int column, int row) { return clamped_at(depth, size, {.column = column, .row = row}); };
    const float centre = depth_at(at.column, at.row);
    if (centre >= kStyleSky * kHalfWeight) {
        return 0.0F;
    }
    const int spacing = parameters.spacing;
    float across = 0.0F;
    float down = 0.0F;
    if (at.column - spacing >= 0 && at.column + spacing <= static_cast<int>(size.width) - 1) {
        const float stands_out = 1.0F - (centre *
                                         ((1.0F / depth_at(at.column - spacing, at.row)) +
                                          (1.0F / depth_at(at.column + spacing, at.row))) *
                                         kHalfWeight);
        across = axis_edge(parameters, stands_out);
    }
    if (at.row - spacing >= 0 && at.row + spacing <= static_cast<int>(size.height) - 1) {
        const float stands_out = 1.0F - (centre *
                                         ((1.0F / depth_at(at.column, at.row - spacing)) +
                                          (1.0F / depth_at(at.column, at.row + spacing))) *
                                         kHalfWeight);
        down = axis_edge(parameters, stands_out);
    }
    return std::max(across, down) * (1.0F - smoothstep(kStyleFadeStart, kStyleFadeEnd, centre));
}

} // namespace style_twin

namespace {

using style_twin::as_half3;
using style_twin::clamped_at;
using style_twin::edge_at;
using style_twin::finite3;
using style_twin::from_lab;
using style_twin::kBlueYellow;
using style_twin::kGreenRed;
using style_twin::kLightness;
using style_twin::mix3;
using style_twin::Pixel;
using style_twin::simplified_lab;
using style_twin::to_lab;
using style_twin::upsampled;

// --- the passes' objects -----------------------------------------------------------------------------------------

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

std::string source_of(const char* body, bool multisampled, const char* before = "")
{
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    source += kCommon;
    source += before;
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

bool make_layouts(Backend& state, std::string* error)
{
    StyleState& own = style();
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.maxPushConstantsSize < sizeof(StyleConstants)) {
        *error = "the device's push constants are too few";
        return false;
    }
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = binding == kBindingOut ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .descriptorCount = 1,
                                       .stageFlags = kStages,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
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
    const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(StyleConstants)};
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
    return true;
}

// The layouts, the sampler, the vertex shader and the compute pipelines (once).
bool ensure_objects(Backend& state, std::string* error)
{
    StyleState& own = style();
    if (own.edge_pass != VK_NULL_HANDLE) {
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
    for (std::size_t variant = 0; variant < own.prep.size(); ++variant) {
        if (core::at(own.prep, variant) == VK_NULL_HANDLE) {
            core::at(own.prep, variant) = make_compute(device, own.layout, source_of(kPrepBody, variant == 1), error);
            if (core::at(own.prep, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    if (own.down == VK_NULL_HANDLE) {
        own.down = make_compute(device, own.layout, source_of(kDownBody, false), error);
        if (own.down == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.kuwahara == VK_NULL_HANDLE) {
        own.kuwahara = make_compute(device, own.layout, source_of(kKuwaharaBody, false), error);
        if (own.kuwahara == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.edge_pass = make_compute(device, own.layout, source_of(kEdgePassBody, false, kEdgeGlsl), error);
    return own.edge_pass != VK_NULL_HANDLE;
}

// A composite's fragment source: the toon's, or a look's (vulkan_style_looks.h).
std::string composite_source(ArtStyle look)
{
    std::string source = "#version 460\n";
    source += kCommon;
    source += kCompositeShared;
    source += kEdgeGlsl;
    if (look == ArtStyle::kToon) {
        source += kToonBody;
    } else {
        source += style_look_shared_glsl();
        source += style_look_body(look);
    }
    return source;
}

// The composite's pipeline for a look and a colour target's format and samples (made on first use): colour written,
// alpha kept.
VkPipeline composite_pipeline(Backend& state, ArtStyle look, VkFormat format, VkSampleCountFlagBits samples,
                              std::string* error)
{
    StyleState& own = style();
    for (const CompositePipeline& made : own.composites) {
        if (made.look == look && made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment = make_module(device, RtShaderStage::kFragment, composite_source(look), error);
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
    own.composites.push_back({.format = format, .samples = samples, .look = look, .pipeline = pipeline});
    return pipeline;
}

// An image of `format` and `extent` for `usage` (one sample), made again when they changed; a storage image rests in
// GENERAL.
bool ensure_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == format && image->extent.width == extent.width &&
        image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
        image->resting = VK_IMAGE_LAYOUT_GENERAL;
    }
    prepare_image(state, image);
    return true;
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

// The depth moved to be sampled by the passes (from whatever wrote it), and back to its resting layout after them.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_passes)
{
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
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
    vkCmdPipelineBarrier(commands, to_passes ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : passes, to_passes ? passes : tests, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// A compute pass's writes made visible to the passes after it (compute and fragment).
void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

// One pass's images: what each binding is (null: not bound).
struct Bound {
    const Image* colour = nullptr; // (SHADER_READ_ONLY_OPTIMAL)
    const Image* depth = nullptr;  // (DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    const Image* out = nullptr;    // (GENERAL)
    const Image* half = nullptr;   // (GENERAL)
    const Image* linear = nullptr; // (GENERAL, or the colour's layout when it is the colour)
    const Image* edges = nullptr;  // (as linear)
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    const StyleState& own = style();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, const Image* image, VkImageLayout layout) {
        if (image == nullptr) {
            return;
        }
        const bool storage = binding == kBindingOut;
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = storage ? VK_NULL_HANDLE : own.sampler, .imageView = image->view, .imageLayout = layout};
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
    };
    const auto layout_of = [&bound](const Image* image) {
        return image == bound.colour ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    };
    add(kBindingColour, bound.colour, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    add(kBindingDepth, bound.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    add(kBindingOut, bound.out, VK_IMAGE_LAYOUT_GENERAL);
    add(kBindingHalf, bound.half, layout_of(bound.half));
    add(kBindingLinear, bound.linear, layout_of(bound.linear));
    add(kBindingEdges, bound.edges, layout_of(bound.edges));
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, uint32_t width, uint32_t height)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    compute_done(commands);
}

// The timestamps' pool (once; none when the device can't time).
void ensure_timer(Backend& state)
{
    StyleState& own = style();
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
                                     .queryCount = kTimerPairs * 2,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
        own.timer = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

// A pair's last result (when the device wrote both) added to the timing; logged every kTimingLogEvery.
void read_timer(Backend& state, uint32_t pair, const Extent& size)
{
    StyleState& own = style();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, 2> stamps{};
    const VkResult got =
        vkGetQueryPoolResults(state.context.device(), own.timer, pair * 2, 2, sizeof(stamps), stamps.data(), sizeof(Stamp),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((got != VK_SUCCESS && got != VK_NOT_READY) || stamps.front().available == 0 || stamps.back().available == 0 ||
        stamps.back().value < stamps.front().value) {
        return;
    }
    ++own.window_samples;
    own.window_milliseconds += static_cast<double>(stamps.back().value - stamps.front().value) * own.tick_milliseconds;
    if (own.window_samples >= kTimingLogEvery) {
        get_log("vulkan: rt_style: the passes take %.3f ms on the device (%ux%u; the last %llu passes' average)",
                own.window_milliseconds / static_cast<double>(own.window_samples), size.width, size.height,
                static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = 0.0;
    }
}

// The rectangle `rect` inside an image of `extent` (empty when it falls outside).
std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left), std::min(core::at(rect, 3), extent.height - top)};
}

std::array<int32_t, 4> signed_rect(const std::array<uint32_t, 4>& rect)
{
    return {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
            static_cast<int32_t>(core::at(rect, 2)), static_cast<int32_t>(core::at(rect, 3))};
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_style_settings(const StyleSettings& settings)
{
    {
        const std::scoped_lock lock(backend().mutex);
        style().settings = settings;
    }
    if (style_has_pass(settings.style)) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

StyleSettings style_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return style().settings;
}

const StyleSettings& style_settings_locked() { return style().settings; }

namespace {

constexpr std::array<const char*, kArtStyleCount> kArtStyleNames{"off", "toon",  "cel",         "comic", "sketch",
                                                                 "vhs", "retro", "watercolour", "noir",  "thermal"};

} // namespace

const char* art_style_name(ArtStyle style)
{
    const auto index = static_cast<std::size_t>(style);
    return index < kArtStyleNames.size() ? core::at(kArtStyleNames, index) : core::at(kArtStyleNames, 0);
}

bool parse_art_style(const std::string& text, ArtStyle* style)
{
    std::string lowered;
    for (const char letter : text) {
        if (letter != ' ' && letter != '\t') {
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
        }
    }
    if (lowered.empty() || lowered == "0") {
        *style = ArtStyle::kOff;
        return true;
    }
    if (lowered == "1") {
        *style = ArtStyle::kToon;
        return true;
    }
    for (std::size_t index = 0; index < kArtStyleNames.size(); ++index) {
        if (lowered == core::at(kArtStyleNames, index)) {
            *style = static_cast<ArtStyle>(index);
            return true;
        }
    }
    return false;
}

bool style_built(ArtStyle style)
{
    switch (style) {
    case ArtStyle::kToon:
    case ArtStyle::kComic:
    case ArtStyle::kSketch:
    case ArtStyle::kNoir:
    case ArtStyle::kThermal:
    case ArtStyle::kVhs:         // (vulkan_style_film.h)
    case ArtStyle::kRetro:       // (likewise)
    case ArtStyle::kWatercolour: // (likewise)
        return true;
    case ArtStyle::kOff:
    case ArtStyle::kCel: // (cel shading: no pass of its own)
        break;
    }
    return false;
}

StyleParameters style_parameters(const StyleSettings& settings, const rt::ViewDrawn& view, const StyleHeights& heights)
{
    const auto finite_or = [](float value, float fallback, float low, float high) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    constexpr float kMostBands = 16.0F;
    constexpr float kMostSaturation = 2.0F;
    constexpr float kMostSmooth = 16.0F;
    constexpr float kMostWidth = 4.0F;
    constexpr std::size_t kRowZ = 2; // (the projection's rows and columns: z, w)
    constexpr std::size_t kRowW = 3;
    StyleParameters parameters;
    const float smooth = finite_or(settings.smooth, 0.0F, 0.0F, kMostSmooth);
    if (smooth > 0.0F) {
        const float radius = std::round(smooth * static_cast<float>(heights.output) / kStyleBaseHeight * kHalfWeight);
        parameters.radius = std::clamp(static_cast<int>(radius), 1, kStyleMostRadius);
    }
    parameters.bands = std::round(finite_or(settings.bands, 0.0F, 0.0F, kMostBands));
    parameters.saturation = finite_or(settings.saturation, 1.0F, 0.0F, kMostSaturation);
    parameters.strength = finite_or(settings.strength, 1.0F, 0.0F, 1.0F);
    parameters.outlines = finite_or(settings.outlines, 0.0F, 0.0F, 1.0F);
    const float width = finite_or(settings.outline_width, 1.0F, 0.0F, kMostWidth);
    parameters.spacing = std::max(1, static_cast<int>(std::round(width * static_cast<float>(heights.depth) / kStyleBaseHeight)));
    const auto& projection = view.camera.projection;
    parameters.focal = std::abs(core::at(core::at(projection, 1), 1)) * static_cast<float>(heights.depth) * kHalfWeight;
    parameters.projection = {core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
                             core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
    parameters.depth_range = view.depth_range;
    set_look_parameters(settings, heights, &parameters);
    return parameters;
}

StyleParameters style_last_parameters()
{
    const std::scoped_lock lock(backend().mutex);
    return style().last;
}

std::vector<float> style_edges(const StyleParameters& parameters, const std::vector<float>& depth, Extent size)
{
    std::vector<float> edges(depth.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            core::at(edges, (static_cast<std::size_t>(row) * size.width) + column) =
                edge_at(parameters, depth, size, {.column = static_cast<int>(column), .row = static_cast<int>(row)});
        }
    }
    return edges;
}

float style_linear_depth(const StyleParameters& parameters, float depth)
{
    if (!(depth < 1.0F)) {
        return kStyleSky;
    }
    const float span = core::at(parameters.depth_range, 1) - core::at(parameters.depth_range, 0);
    const float ndc = (depth - core::at(parameters.depth_range, 0)) * (span > 0.0F ? 1.0F / span : 1.0F);
    const std::array<float, 4>& terms = parameters.projection; // P22, P32, P23, P33
    const float view_z = (core::at(terms, 1) - (ndc * core::at(terms, 3))) / ((ndc * core::at(terms, 2)) - core::at(terms, 0));
    const float clip_w = (view_z * core::at(terms, 2)) + core::at(terms, 3);
    const float along = std::abs(core::at(terms, 2)) > kHalfWeight ? clip_w : std::abs(view_z);
    return along > 0.0F && along < kStyleSky ? along : kStyleSky;
}

std::vector<StyleRgb> style_half(const std::vector<StyleRgb>& colour, Extent size)
{
    const Extent half_size{.width = (size.width + 1) / 2, .height = (size.height + 1) / 2};
    std::vector<StyleRgb> half(static_cast<std::size_t>(half_size.width) * half_size.height);
    constexpr float kQuarter = 0.25F;
    for (uint32_t row = 0; row < half_size.height; ++row) {
        for (uint32_t column = 0; column < half_size.width; ++column) {
            StyleRgb sum{};
            for (uint32_t down = 0; down < 2; ++down) {
                for (uint32_t across = 0; across < 2; ++across) {
                    const StyleRgb texel = finite3(clamped_at(
                        colour, size,
                        {.column = static_cast<int>((column * 2) + across), .row = static_cast<int>((row * 2) + down)}));
                    for (std::size_t channel = 0; channel < texel.size(); ++channel) {
                        core::at(sum, channel) += core::at(texel, channel);
                    }
                }
            }
            for (float& value : sum) {
                value *= kQuarter;
            }
            core::at(half, (static_cast<std::size_t>(row) * half_size.width) + column) = as_half3(sum);
        }
    }
    return half;
}

namespace {

// The four quadrants' sums and sums of squares around a half-resolution texel.
struct QuadrantSums {
    std::array<StyleRgb, kQuadrants> sums{};
    std::array<StyleRgb, kQuadrants> squares{};
};

QuadrantSums quadrant_sums(const std::vector<StyleRgb>& half, Extent size, Pixel at, int radius)
{
    QuadrantSums found;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            const StyleRgb& texel = clamped_at(half, size, {.column = at.column + across, .row = at.row + down});
            const std::array<bool, kQuadrants> inside{across <= 0 && down <= 0, across >= 0 && down <= 0,
                                                      across <= 0 && down >= 0, across >= 0 && down >= 0};
            for (std::size_t quadrant = 0; quadrant < inside.size(); ++quadrant) {
                if (!core::at(inside, quadrant)) {
                    continue;
                }
                for (std::size_t channel = 0; channel < texel.size(); ++channel) {
                    core::at(core::at(found.sums, quadrant), channel) += core::at(texel, channel);
                    core::at(core::at(found.squares, quadrant), channel) += core::at(texel, channel) * core::at(texel, channel);
                }
            }
        }
    }
    return found;
}

} // namespace

std::vector<StyleRgb> style_kuwahara(const std::vector<StyleRgb>& half, Extent size, int radius)
{
    std::vector<StyleRgb> out(half.size());
    const auto count = static_cast<float>((radius + 1) * (radius + 1));
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const QuadrantSums found =
                quadrant_sums(half, size, {.column = static_cast<int>(column), .row = static_cast<int>(row)}, radius);
            StyleRgb total{};
            float weights = 0.0F;
            for (std::size_t quadrant = 0; quadrant < found.sums.size(); ++quadrant) {
                float spread = 0.0F;
                StyleRgb mean{};
                for (std::size_t channel = 0; channel < mean.size(); ++channel) {
                    core::at(mean, channel) = core::at(core::at(found.sums, quadrant), channel) / count;
                    spread += std::max((core::at(core::at(found.squares, quadrant), channel) / count) -
                                           (core::at(mean, channel) * core::at(mean, channel)),
                                       0.0F);
                }
                const float scaled = spread * kStyleKuwaharaScale;
                const float squared = scaled * scaled;
                const float weight = 1.0F / (1.0F + (squared * squared));
                for (std::size_t channel = 0; channel < mean.size(); ++channel) {
                    core::at(total, channel) += core::at(mean, channel) * weight;
                }
                weights += weight;
            }
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            if (weights > kLeastWeights) {
                for (float& value : total) {
                    value /= weights;
                }
            } else {
                total = core::at(half, at);
            }
            core::at(out, at) = as_half3(total);
        }
    }
    return out;
}

StyleRgb style_simplify(const StyleParameters& parameters, StyleRgb colour)
{
    return from_lab(simplified_lab(parameters, to_lab(finite3(colour))));
}

std::vector<StyleRgb> style_reference(const StyleParameters& parameters, const StyleImages& images, Extent size)
{
    if (parameters.style != ArtStyle::kToon) {
        return style_look_reference(parameters, images, size);
    }
    const Extent half_size{.width = (size.width + 1) / 2, .height = (size.height + 1) / 2};
    std::vector<StyleRgb> smooth;
    if (images.smoothed != nullptr && !images.smoothed->empty()) {
        smooth = *images.smoothed;
    } else if (parameters.radius > 0) {
        smooth = style_kuwahara(style_half(*images.colour, size), half_size, parameters.radius);
    }
    const std::vector<StyleRgb>& colour = *images.colour;
    std::vector<StyleRgb> out(colour.size());
    const bool outlined = images.depth != nullptr && !images.depth->empty() && parameters.outlines > 0.0F;
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            const Pixel pixel{.column = static_cast<int>(column), .row = static_cast<int>(row)};
            const StyleRgb own = finite3(core::at(colour, at));
            const StyleRgb smoothed = parameters.radius > 0 ? upsampled(smooth, half_size, pixel, own) : own;
            StyleRgb lab = simplified_lab(parameters, to_lab(smoothed));
            if (outlined) {
                const float edge =
                    std::clamp(edge_at(parameters, *images.depth, size, pixel) * parameters.outlines, 0.0F, 1.0F);
                const StyleRgb ink{core::at(lab, kLightness) * kStyleLineLightness,
                                   core::at(lab, kGreenRed) * kStyleLineChroma, core::at(lab, kBlueYellow) * kStyleLineChroma};
                lab = mix3(lab, ink, edge);
            }
            core::at(out, at) = mix3(own, from_lab(lab), parameters.strength);
        }
    }
    return out;
}

// --- the passes --------------------------------------------------------------------------------------------------

VkImageUsageFlags style_depth_usage(const Backend& state, VkFormat format)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
}

namespace {

// What a view's passes work on.
struct ViewPlan {
    Image* colour = nullptr;
    Image* depth = nullptr; // (null: no samplable depth - no outlines)
    std::array<uint32_t, 4> shown{};      // the colour's rectangle (the upscaler's output when it upscaled)
    std::array<uint32_t, 4> depth_rect{}; // the depth's
    bool upscaled = false;
    StyleParameters parameters;
};

// The view's targets and rectangles; false (logged once) when there is nothing to filter.
bool plan_view(Backend& state, const rt::ViewDrawn& view, ViewPlan* plan)
{
    StyleState& own = style();
    Image* const colour = image_of_surface(core::at(state.targets, 0));
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE || (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
        (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: rt_style: a view drawn into a target that can't be read - not filtered there");
        }
        return false;
    }
    // where the colour is: the upscaler's output when it upscaled this view (it ran since the opaque world), else the
    // drawn rectangle
    plan->upscaled = upscale_passes() != own.upscales_seen && view.output_rect != view.rect &&
                     core::at(view.output_rect, 2) != 0 && core::at(view.output_rect, 3) != 0;
    own.upscales_seen = upscale_passes();
    plan->colour = colour;
    plan->shown = clip_rect(plan->upscaled ? view.output_rect : view.rect, colour->extent);
    if (core::at(plan->shown, 2) == 0 || core::at(plan->shown, 3) == 0) {
        return false;
    }
    Image* const depth = image_of_surface(state.depth);
    if (depth != nullptr) {
        plan->depth_rect = clip_rect(view.rect, depth->extent);
    }
    const bool depth_ok = depth != nullptr && (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
                          (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE &&
                          core::at(plan->depth_rect, 2) != 0 && core::at(plan->depth_rect, 3) != 0;
    const bool wanted = look_reads_depth(own.settings.style);
    plan->depth = depth_ok && wanted ? depth : nullptr;
    if (!depth_ok && wanted && own.settings.outlines > 0.0F && first_report(state, kReportNoDepth)) {
        get_log("vulkan: rt_style: no samplable depth with the view - no outlines there");
    }
    plan->parameters = style_parameters(
        own.settings, view, {.output = core::at(plan->shown, 3), .depth = depth_ok ? core::at(plan->depth_rect, 3) : 1U});
    constexpr uint64_t kSeedMask = 0xFFFFU; // (exact as a float)
    plan->parameters.seed = static_cast<uint32_t>(state.frames.serial() & kSeedMask);
    return true;
}

// The pipelines and images the plan needs (the composite's pipeline out); false (logged once, the style off) when
// they can't be made.
bool ensure_pass_objects(Backend& state, const ViewPlan& plan, VkPipeline* composite)
{
    StyleState& own = style();
    const Image& colour = *plan.colour;
    const Extent half_extent{.width = (colour.extent.width + 1) / 2, .height = (colour.extent.height + 1) / 2};
    constexpr VkImageUsageFlags kStorage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    std::string error;
    const ArtStyle look = plan.parameters.style;
    *composite = ensure_objects(state, &error) ? composite_pipeline(state, look, colour.format, colour.samples, &error)
                                               : VK_NULL_HANDLE;
    const bool half = plan.parameters.radius > 0 || look_reads_half(look);
    const bool made =
        *composite != VK_NULL_HANDLE &&
        ensure_image(state, &own.source, colour.format, colour.extent,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
        (!half || ensure_image(state, &own.half, VK_FORMAT_R16G16B16A16_SFLOAT, half_extent, kStorage)) &&
        (plan.parameters.radius == 0 ||
         ensure_image(state, &own.smooth, VK_FORMAT_R16G16B16A16_SFLOAT, half_extent, kStorage)) &&
        (plan.depth == nullptr || ensure_image(state, &own.linear, VK_FORMAT_R32_SFLOAT, plan.depth->extent, kStorage)) &&
        (plan.depth == nullptr || !look_draws_ink(look) ||
         ensure_image(state, &own.edges, VK_FORMAT_R32_SFLOAT, plan.depth->extent, kStorage));
    if (!made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_style: the passes couldn't be made (%s) - no art style",
                    error.empty() ? "their images" : error.c_str());
        }
    }
    return made;
}

StyleConstants constants_for(const ViewPlan& plan)
{
    const StyleParameters& parameters = plan.parameters;
    const bool depth = plan.depth != nullptr;
    StyleConstants constants{};
    constants.out_rect = signed_rect(plan.shown);
    constants.depth_rect = signed_rect(plan.depth_rect);
    constants.sizes = {static_cast<int32_t>((core::at(plan.shown, 2) + 1) / 2),
                       static_cast<int32_t>((core::at(plan.shown, 3) + 1) / 2), parameters.radius, parameters.spacing};
    constants.tone = {parameters.bands, kStyleBandSoftness * kHalfWeight, parameters.saturation, parameters.strength};
    constants.outline = {depth ? parameters.outlines : 0.0F, parameters.focal, kStyleFadeStart, kStyleFadeEnd};
    constants.projection = parameters.projection;
    const float span = core::at(parameters.depth_range, 1) - core::at(parameters.depth_range, 0);
    constants.depth_map = {core::at(parameters.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, depth ? 1.0F : 0.0F,
                           0.0F};
    constants.lines = {kStyleLineLightness, kStyleLineChroma, kStyleCreaseWeight, kStyleBandHold};
    const LookConstants look = look_constants(parameters);
    constants.tone = {parameters.bands, look.softness, parameters.saturation, parameters.strength};
    constants.look_a = look.a;
    constants.look_b = look.b;
    constants.look_c = look.c;
    constants.look_d = look.d;
    return constants;
}

// A timestamp pair begun for this pass when one is free (its last result read first); false: not timed.
bool begin_timing(Backend& state, VkCommandBuffer commands, const Extent& size, uint32_t* pair)
{
    StyleState& own = style();
    ensure_timer(state);
    *pair = static_cast<uint32_t>(own.timed % kTimerPairs);
    uint64_t& written = core::at(own.timer_serials, *pair);
    if (own.timer == VK_NULL_HANDLE || (written != 0 && state.frames.completed() < written)) {
        return false;
    }
    if (written != 0) {
        read_timer(state, *pair, size);
    }
    written = state.frames.serial();
    vkCmdResetQueryPool(commands, own.timer, *pair * 2, 2);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer, *pair * 2);
    return true;
}

// 0. The viewport's colour copied out (resolved when multisampled) into the source image.
void copy_colour(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& shown)
{
    StyleState& own = style();
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(shown, 2), .height = core::at(shown, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // the work images: the last frame's (or the last view's) reads of them done before they are written again
    for (Image* image : {&own.half, &own.smooth, &own.linear, &own.edges}) {
        if (image->image != VK_NULL_HANDLE) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL);
        }
    }
    const VkMemoryBarrier reads_done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .pNext = nullptr,
                                     .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &reads_done, 0, nullptr, 0, nullptr);
}

// 4. The composite: one triangle over the colour's rectangle.
void record_composite(VkCommandBuffer commands, VkPipeline composite, const ViewPlan& plan)
{
    StyleState& own = style();
    Image* const colour = plan.colour;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(plan.shown, 0)),
                                   .y = static_cast<int32_t>(core::at(plan.shown, 1))},
                        .extent = {.width = core::at(plan.shown, 2), .height = core::at(plan.shown, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, composite);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    // (what the composite doesn't use this time is bound to the colour: every binding it declares is valid)
    const ArtStyle look = plan.parameters.style;
    const Image* half = &own.source;
    if (plan.parameters.radius > 0) {
        half = &own.smooth;
    } else if (look_reads_half(look)) {
        half = &own.half;
    }
    const Image* edges = nullptr; // (the toon composite has no edge binding)
    if (look != ArtStyle::kToon) {
        edges = plan.depth != nullptr && look_draws_ink(look) ? &own.edges : &own.source;
    }
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     {.colour = &own.source,
                      .half = half,
                      .linear = plan.depth != nullptr ? &own.linear : &own.source,
                      .edges = edges});
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

void log_first_pass(Backend& state, const ViewPlan& plan)
{
    if (!first_report(state, kReportFirstPass)) {
        return;
    }
    const StyleParameters& parameters = plan.parameters;
    get_log("vulkan: rt_style: the first %s pass (%ux%u%s, %u sample%s; smoothing radius %d, %.0f bands, outlines "
            "%.2f every %d depth pixel%s, saturation %.2f, strength %.2f%s)",
            art_style_name(parameters.style), core::at(plan.shown, 2), core::at(plan.shown, 3), plan.upscaled ? " upscaled" : "",
            static_cast<unsigned>(plan.colour->samples), plan.colour->samples == 1 ? "" : "s", parameters.radius,
            static_cast<double>(parameters.bands), static_cast<double>(plan.depth != nullptr ? parameters.outlines : 0.0F),
            parameters.spacing, parameters.spacing == 1 ? "" : "s", static_cast<double>(parameters.saturation),
            static_cast<double>(parameters.strength), plan.depth != nullptr ? "" : "; no depth");
}

} // namespace

bool record_style_pass(Backend& state, const rt::ViewDrawn& view)
{
    StyleState& own = style();
    if (!style_has_pass(own.settings.style) || own.failed || !(own.settings.strength > 0.0F)) {
        return false;
    }
    if (!style_built(own.settings.style)) {
        if (first_report(state, kReportUnbuilt + static_cast<uint64_t>(own.settings.style))) {
            get_log("vulkan: rt_style=%s is not built in - nothing drawn", art_style_name(own.settings.style));
        }
        return false;
    }
    ViewPlan plan;
    // NOT ORIGINAL hook - the vhs, retro and watercolour styles: their own passes over the planned colour
    // (vulkan_style_film.h)
    if (is_film_look(own.settings.style)) {
        return plan_view(state, view, &plan) &&
               record_film_pass(state, view, own.settings, {.colour = plan.colour, .shown = plan.shown});
    }
    VkPipeline composite = VK_NULL_HANDLE;
    if (!plan_view(state, view, &plan) || !ensure_pass_objects(state, plan, &composite)) {
        return false;
    }
    const StyleConstants constants = constants_for(plan);
    const uint32_t half_width = (core::at(plan.shown, 2) + 1) / 2;
    const uint32_t half_height = (core::at(plan.shown, 3) + 1) / 2;
    VkCommandBuffer commands = outside_pass(state);
    uint32_t pair = 0;
    const bool timing =
        begin_timing(state, commands, {.width = core::at(plan.shown, 2), .height = core::at(plan.shown, 3)}, &pair);
    copy_colour(commands, plan.colour, plan.shown);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(StyleConstants), &constants);
    // 1. the linear depth
    if (plan.depth != nullptr) {
        depth_barrier(commands, plan.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
        dispatch(commands, core::at(own.prep, plan.depth->samples != VK_SAMPLE_COUNT_1_BIT ? 1 : 0),
                 {.depth = plan.depth, .out = &own.linear}, core::at(plan.depth_rect, 2), core::at(plan.depth_rect, 3));
        depth_barrier(commands, plan.depth, plan.depth->resting, false);
    }
    // (comic, sketch) the edges
    if (plan.depth != nullptr && look_draws_ink(plan.parameters.style)) {
        dispatch(commands, own.edge_pass, {.out = &own.edges, .linear = &own.linear}, core::at(plan.depth_rect, 2),
                 core::at(plan.depth_rect, 3));
    }
    // 2, 3. half resolution, smoothed
    if (plan.parameters.radius > 0 || look_reads_half(plan.parameters.style)) {
        dispatch(commands, own.down, {.colour = &own.source, .out = &own.half}, half_width, half_height);
    }
    if (plan.parameters.radius > 0) {
        dispatch(commands, own.kuwahara, {.out = &own.smooth, .half = &own.half}, half_width, half_height);
    }
    record_composite(commands, composite, plan);
    if (timing) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timer, (pair * 2) + 1);
        ++own.timed;
    }
    if (plan.colour->layout != plan.colour->resting) {
        Memory::transition(commands, plan.colour, plan.colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    plan.colour->used = serial;
    for (Image* image : {&own.source, &own.half, &own.smooth, &own.linear, &own.edges}) {
        if (image->image != VK_NULL_HANDLE) {
            image->used = serial;
        }
    }
    own.outlined = plan.depth != nullptr && plan.parameters.outlines > 0.0F && look_draws_lines(plan.parameters.style);
    own.last = plan.parameters;
    ++own.passes;
    log_first_pass(state, plan);
    return true;
}

uint64_t style_passes() { return style().passes; }

bool style_outlined() { return style().outlined; }

bool read_style_depth(Backend& state, std::vector<float>* depth, Extent* size)
{
    StyleState& own = style();
    if (own.linear.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(own.linear.extent.width) * own.linear.extent.height * sizeof(float));
    if (!state.memory.read_image_level(&own.linear, 0, &bytes)) {
        return false;
    }
    depth->resize(bytes.size() / sizeof(float));
    std::memcpy(depth->data(), bytes.data(), depth->size() * sizeof(float));
    *size = own.linear.extent;
    return true;
}

bool read_style_edges(Backend& state, std::vector<float>* edges, Extent* size)
{
    StyleState& own = style();
    if (own.edges.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(own.edges.extent.width) * own.edges.extent.height * sizeof(float));
    if (!state.memory.read_image_level(&own.edges, 0, &bytes)) {
        return false;
    }
    edges->resize(bytes.size() / sizeof(float));
    std::memcpy(edges->data(), bytes.data(), edges->size() * sizeof(float));
    *size = own.edges.extent;
    return true;
}

bool read_style_smoothed(Backend& state, std::vector<uint16_t>* halves, Extent* size)
{
    StyleState& own = style();
    if (own.smooth.image == VK_NULL_HANDLE) {
        return false;
    }
    constexpr std::size_t kTexelBytes = 4 * sizeof(uint16_t);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(own.smooth.extent.width) * own.smooth.extent.height * kTexelBytes);
    if (!state.memory.read_image_level(&own.smooth, 0, &bytes)) {
        return false;
    }
    halves->resize(bytes.size() / sizeof(uint16_t));
    std::memcpy(halves->data(), bytes.data(), halves->size() * sizeof(uint16_t));
    *size = own.smooth.extent;
    return true;
}

void release_style(Backend& state)
{
    StyleState& own = style();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const CompositePipeline& made : own.composites) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline pipeline : {core::at(own.prep, 0), core::at(own.prep, 1), own.down, own.kuwahara, own.edge_pass}) {
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
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    if (state.memory.ready()) {
        for (Image* image : {&own.source, &own.half, &own.smooth, &own.linear, &own.edges}) {
            state.memory.destroy_image(image);
        }
    }
    own.composites.clear();
    own.prep = {};
    own.down = VK_NULL_HANDLE;
    own.kuwahara = VK_NULL_HANDLE;
    own.edge_pass = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timer_serials = {};
    own.failed = false;
}

} // namespace render::vulkan
