// NOT ORIGINAL - grass: the blades over the level's grass ground (vulkan_grass.h; docs/design/vulkan.md "Grass").
#include "gpu_vulkan/vulkan_grass.h"

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
#include "base/core/format.h"
#include "engine/gfx/grass.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportTarget = 0x47525301U;
constexpr uint64_t kReportPipeline = 0x47525302U;
constexpr uint64_t kReportFirstPass = 0x47525303U;
constexpr uint64_t kReportNoDepth = 0x47525304U;
constexpr uint64_t kReportUpload = 0x47525305U;
constexpr uint64_t kReportNoRoom = 0x47525306U;

constexpr uint32_t kGroupSize = 64;
constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kBindingBlades = 0;  // the field's blades (storage)
constexpr uint32_t kBindingColours = 1; // the blades' colours (storage: two uints a blade - half floats r g, b known)
constexpr uint32_t kBindingParams = 2;  // the view's block (uniform, in the ring)
constexpr uint32_t kBindingSource = 3;  // the viewport's colour copied out (sampled)
constexpr uint32_t kBindingDepth = 4;   // the depth (sampled; the source when there is no samplable depth)
constexpr uint32_t kBindingProbe = 5;   // the probe's answers (storage)
constexpr uint32_t kBindingCount = 6;
constexpr std::size_t kColourBytes = 8; // (a blade's colour)
constexpr uint32_t kTimerPairs = 8;
constexpr uint64_t kTimingLogEvery = 300;
constexpr uint32_t kNearSegments = 3;
constexpr uint32_t kMiddleSegments = 2;
constexpr uint32_t kFarSegments = 1;
constexpr float kNearSegmentsShare = 0.25F; // (of the distance: the tiles nearer get kNearSegments)
constexpr float kMiddleSegmentsShare = 0.55F;
constexpr float kTipStretch = 1.6F;  // (a tile's box grown up by this x the height, out by the height: its blades)
constexpr float kControlBend = 0.2F; // the curve's middle point: this share of the bend, at the tip's height
constexpr float kTau = 6.28318530718F;
constexpr uint32_t kByte = 0xFFU;
constexpr uint32_t kShadeMask = 0x7FU;
constexpr uint32_t kHeightShift = 8;
constexpr uint32_t kFacingShift = 16;
constexpr uint32_t kShadeShift = 25;
constexpr float kRanks = 256.0F;
constexpr float kShadeSteps = 127.0F;
constexpr float kTiny = 1.0e-3F;
constexpr float kHalf = 0.5F;
// the wind's two waves: their speeds (radians a second), how they move over the ground (radians a unit), their mix
constexpr float kWindSpeedA = 1.9F;
constexpr float kWindSpeedB = 0.73F;
constexpr float kWindAlongX = 0.0047F;
constexpr float kWindAlongZ = 0.0031F;
constexpr float kWindStretchB = 2.3F;
constexpr float kWindShiftB = 1.7F;
constexpr float kWindShareA = 0.6F;
constexpr float kWindShareB = 0.4F;
constexpr double kWindClockWraps = 3600.0; // (seconds: the clock kept small, a float's steps fine)
constexpr float kClipSpan = 2.0F;          // (clip space from -1 to 1)
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

// --- the shaders -------------------------------------------------------------------------------------------------

// A float as GLSL reads it (shortest round trip; a decimal point always).
std::string glsl_float(float value)
{
    std::string written = text::format("%.9g", static_cast<double>(value));
    if (written.find_first_of(".eEn") == std::string::npos) {
        written += ".0";
    }
    return written;
}

// The constants the shaders share with the processor's twin.
std::string constants_source()
{
    std::string out;
    const auto define = [&out](const char* name, float value) {
        out += "#define ";
        out += name;
        out += ' ';
        out += glsl_float(value);
        out += '\n';
    };
    define("SHORTEST", kGrassShortest);
    define("TALLEST", kGrassTallest);
    define("WIDTH", kGrassWidth);
    define("LEAST_PIXELS", kGrassLeastPixels);
    define("KEEP_POWER", kGrassKeepPower);
    define("GROW_BAND", kGrassGrowBand);
    define("FADE_START", kGrassFadeStart);
    define("LEAN", kGrassLean);
    define("WIND", kGrassWind);
    define("PUSH", kGrassPush);
    define("PUSH_HEIGHT", kGrassPushHeight);
    define("MOST_BEND", kGrassMostBend);
    define("FACE_CAMERA", kGrassFaceCamera);
    define("ROOT_SHADE", kGrassRootShade);
    define("TIP_SHADE", kGrassTipShade);
    define("SHADE_SPREAD", kGrassShadeSpread);
    define("MATCH_SHARE", kGrassMatchShare);
    define("MATCH_SLACK", kGrassMatchSlack);
    define("LEARN", kGrassLearn);
    define("CONTROL_BEND", kControlBend);
    define("WIND_X", core::at(kGrassWindDirection, 0));
    define("WIND_Z", core::at(kGrassWindDirection, 1));
    define("WIND_SPEED_A", kWindSpeedA);
    define("WIND_SPEED_B", kWindSpeedB);
    define("WIND_ALONG_X", kWindAlongX);
    define("WIND_ALONG_Z", kWindAlongZ);
    define("WIND_STRETCH_B", kWindStretchB);
    define("WIND_SHIFT_B", kWindShiftB);
    define("WIND_SHARE_A", kWindShareA);
    define("WIND_SHARE_B", kWindShareB);
    return out;
}

// What every stage shares: the bindings, the view's block, the blade's shape (grass_blade_shape is its twin).
constexpr const char* kCommon = R"(
layout(std430, set = 0, binding = 0) readonly buffer Blades { vec4 blades[]; };
layout(std430, set = 0, binding = 1) COLOURS_ACCESS buffer Colours { uvec2 colours[]; };
layout(std140, set = 0, binding = 2) uniform Params {
    mat4 vp;               // D3D's row-vector matrix as stored: vp * v is its v x M
    mat4 vp_before;        // the frame before's, unjittered (the motion)
    vec4 camera;           // xyz; w: the wind's clock (seconds)
    vec4 lod;              // x: the distance; y: the near distance (every blade); z: the share kept's scale; w: a pixel
    vec4 blade;            // x: the base height
    vec4 rect;             // the viewport: x, y, width, height (pixels)
    vec4 offsets;          // x, y: Direct3D 9's half pixel (1 / the size); z, w: the jitter (clip space)
    vec4 depth_projection; // P22, P32, P23, P33
    vec4 depth_map;        // x: the depth range's minimum; y: 1 / its span; z: 1 with a depth; w: 1 probing
    ivec4 counts;          // x: the pushers
    vec4 pushers[8];       // xyz where, w the reach
} p;
layout(push_constant) uniform Range { uvec4 range; } pc; // first, count, segments

const float TAU = 6.28318530718;

struct Shape {
    vec3 root;
    vec3 control;
    vec3 tip;
    vec3 across;
    float height;
    float width;
    float shade;
};

float keep_at(float d)
{
    float near = p.lod.y;
    float k = d <= near ? 1.0 : pow(near / d, KEEP_POWER);
    return k * p.lod.z;
}

float fade_at(float d) { return 1.0 - smoothstep(FADE_START * p.lod.x, p.lod.x, d); }

Shape blade_shape(vec4 b, float known)
{
    uint packed = floatBitsToUint(b.w);
    float rank = (float(packed & 255u) + 0.5) / 256.0;
    float tall = mix(SHORTEST, TALLEST, float((packed >> 8u) & 255u) / 255.0);
    float turn = float((packed >> 16u) & 255u) / 256.0 * TAU;
    float hash = float((packed >> 25u) & 127u) / 127.0;
    Shape s;
    s.root = b.xyz;
    vec3 to = p.camera.xyz - s.root;
    float d = length(to);
    float grow = clamp((keep_at(d) - rank) / GROW_BAND, 0.0, 1.0);
    float h = p.blade.x * tall * fade_at(d) * clamp(known, 0.0, 1.0) * grow;
    vec2 facing = vec2(cos(turn), sin(turn));
    vec2 across = facing;
    float flat_length = length(to.xz);
    if (flat_length > 1.0e-3) {
        vec2 side = vec2(-to.z, to.x) / flat_length;
        vec2 turned = mix(facing, side, FACE_CAMERA);
        float turned_length = length(turned);
        across = turned_length > 1.0e-3 ? turned / turned_length : facing;
    }
    vec2 lean = vec2(-across.y, across.x);
    vec2 bend = lean * (LEAN * (hash * 2.0 - 1.0) * h);
    float phase = s.root.x * WIND_ALONG_X + s.root.z * WIND_ALONG_Z;
    float wave = sin(p.camera.w * WIND_SPEED_A + phase) * WIND_SHARE_A +
                 sin(p.camera.w * WIND_SPEED_B + phase * WIND_STRETCH_B + WIND_SHIFT_B) * WIND_SHARE_B;
    bend += vec2(WIND_X, WIND_Z) * (WIND * h * (0.5 + 0.5 * wave));
    for (int k = 0; k < p.counts.x && k < 8; ++k) {
        vec4 q = p.pushers[k];
        vec2 away = s.root.xz - q.xz;
        float apart = length(away);
        if (apart < q.w && abs(s.root.y - q.y) < PUSH_HEIGHT) {
            float t = 1.0 - apart / q.w;
            t = t * t * (3.0 - 2.0 * t);
            vec2 way = apart > 1.0e-3 ? away / apart : lean;
            bend += way * (PUSH * h * t);
        }
    }
    float bent = length(bend);
    float most = MOST_BEND * h;
    if (bent > most) {
        bend *= most / bent;
    }
    float up = sqrt(max(h * h - dot(bend, bend), 0.0));
    s.tip = s.root + vec3(bend.x, up, bend.y);
    s.control = s.root + vec3(bend.x * CONTROL_BEND, up, bend.y * CONTROL_BEND);
    s.across = vec3(across.x, 0.0, across.y);
    s.height = h;
    s.width = h > 0.0 ? max(WIDTH, LEAST_PIXELS * p.lod.w * d) : 0.0;
    s.shade = hash;
    return s;
}

vec3 blade_point(Shape s, float t)
{
    float u = 1.0 - t;
    return u * u * s.root + 2.0 * u * t * s.control + t * t * s.tip;
}

vec3 finite_colour(vec3 c)
{
    return vec3(c.r >= 0.0 ? min(c.r, 64.0) : 0.0, c.g >= 0.0 ? min(c.g, 64.0) : 0.0, c.b >= 0.0 ? min(c.b, 64.0) : 0.0);
}
)";

// The gather: each blade's root on the screen; where the depth there is the root's, its colour taken.
constexpr const char* kGatherBody = R"(
layout(local_size_x = 64) in;
layout(set = 0, binding = 3) uniform sampler2D u_source;
#ifdef MULTISAMPLED
layout(set = 0, binding = 4) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 4) uniform sampler2D u_depth;
#endif

float linear_depth(float d)
{
    float ndc = (d - p.depth_map.x) * p.depth_map.y;
    float z = (p.depth_projection.y - ndc * p.depth_projection.w) / (ndc * p.depth_projection.z - p.depth_projection.x);
    float w = z * p.depth_projection.z + p.depth_projection.w;
    return abs(p.depth_projection.z) > 0.5 ? w : abs(z);
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.range.y) {
        return;
    }
    uint index = pc.range.x + i;
    vec3 root = blades[index].xyz;
    vec4 clip = p.vp * vec4(root, 1.0);
    if (!(clip.w > 1.0e-3)) {
        return;
    }
    vec2 ndc = clip.xy / clip.w;
    vec2 at = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * p.rect.zw;
    if (!(all(greaterThan(at, vec2(-0.1) * p.rect.zw)) && all(lessThan(at, vec2(1.1) * p.rect.zw)))) {
        return;
    }
    ivec2 size = max(ivec2(p.rect.zw), ivec2(1));
    ivec2 pixel = clamp(ivec2(floor(at)), ivec2(0), size - 1) + ivec2(p.rect.xy);
    if (p.depth_map.z > 0.5) {
        float z = linear_depth(texelFetch(u_depth, pixel, 0).r);
        if (!(abs(z - clip.w) <= clip.w * MATCH_SHARE + MATCH_SLACK)) {
            return;
        }
    }
    vec3 c = finite_colour(texelFetch(u_source, pixel, 0).rgb);
    float known = min(unpackHalf2x16(colours[index].y).y + LEARN, 1.0);
    colours[index] = uvec2(packHalf2x16(c.rg), packHalf2x16(vec2(c.b, known)));
}
)";

// The probe (the check's): the blades shaped, each fully known; the tip and the height out.
constexpr const char* kProbeBody = R"(
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 5) writeonly buffer Probe { vec4 answers[]; };

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.range.y) {
        return;
    }
    Shape s = blade_shape(blades[pc.range.x + i], 1.0);
    answers[i] = vec4(s.tip, s.height);
}
)";

constexpr const char* kVertexBody = R"(
layout(location = 0) out vec3 v_colour;
#ifdef MOTION
layout(location = 1) out vec4 v_now;
layout(location = 2) out vec4 v_before;
#endif
out gl_PerVertex { vec4 gl_Position; };

void main()
{
    uint index = uint(gl_InstanceIndex);
    uvec2 packed = colours[index];
    vec2 rg = unpackHalf2x16(packed.x);
    vec2 bk = unpackHalf2x16(packed.y);
    Shape s = blade_shape(blades[index], bk.y);
    int segments = max(int(pc.range.z), 1);
    int vertex = gl_VertexIndex;
    int level = min(vertex / 2, segments);
    float t = float(level) / float(segments);
    float side = vertex >= 2 * segments ? 0.0 : ((vertex & 1) == 0 ? -0.5 : 0.5);
    vec3 position = blade_point(s, t) + s.across * (s.width * (1.0 - t) * side);
    vec4 clip = p.vp * vec4(position, 1.0);
    gl_Position = s.height > 0.0
                      ? vec4(clip.x + clip.w * p.offsets.x, -clip.y + clip.w * p.offsets.y, clip.z, clip.w)
                      : vec4(0.0);
    float shade = mix(ROOT_SHADE, TIP_SHADE, t) * (1.0 + SHADE_SPREAD * (s.shade * 2.0 - 1.0));
    v_colour = finite_colour(vec3(rg, bk.x)) * shade;
#ifdef MOTION
    v_now = vec4(clip.xy - p.offsets.zw * clip.w, 0.0, clip.w);
    v_before = p.vp_before * vec4(position, 1.0);
#endif
}
)";

constexpr const char* kFragmentBody = R"(
layout(location = 0) in vec3 v_colour;
layout(location = 0) out vec4 o_colour;
#ifdef MOTION
layout(location = 1) in vec4 v_now;
layout(location = 2) in vec4 v_before;
layout(location = 1) out vec4 o_motion;
#endif

void main()
{
    o_colour = vec4(v_colour, 1.0);
#ifdef MOTION
    vec2 now = v_now.xy / v_now.w;
    vec2 before = v_before.w > 0.0 ? v_before.xy / v_before.w : now;
    o_motion = vec4((before - now) * vec2(0.5, -0.5), 0.0, 1.0);
#endif
}
)";

// The view's block, as the shaders' std140 block lays it out.
struct ParamsBlock {
    Float4x4 vp{};
    Float4x4 vp_before{};
    std::array<float, 4> camera{};
    std::array<float, 4> lod{};
    std::array<float, 4> blade{};
    std::array<float, 4> rect{};
    std::array<float, 4> offsets{};
    std::array<float, 4> depth_projection{};
    std::array<float, 4> depth_map{};
    std::array<int32_t, 4> counts{};
    std::array<std::array<float, 4>, grass::kMostPushers> pushers{};
};
constexpr std::size_t kParamsBytes = 384;
static_assert(sizeof(ParamsBlock) == kParamsBytes, "the shaders' Params block");

struct RangeConstants {
    std::array<uint32_t, 4> range{};
};

struct DrawPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool motion = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct GrassState {
    GrassSettings settings;
    std::shared_ptr<const grass::Field> field;    // given by render_pc (or the check)
    std::shared_ptr<const grass::Field> uploaded; // the one in the buffers
    std::vector<grass::Pusher> pushers;
    DeviceBuffer blades;
    DeviceBuffer colours;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> gather{}; // single-sampled depth, multisampled
    VkPipeline probe = VK_NULL_HANDLE;
    std::array<VkShaderModule, 2> vertex{}; // without, with motion
    std::array<VkShaderModule, 2> fragment{};
    std::vector<DrawPipeline> draws;
    Image source; // the viewport's colour copied out
    bool failed = false;
    uint64_t passes = 0;
    uint32_t last_blades = 0;
    uint32_t last_draws = 0;
    // the pass timed on the device (the style's scheme)
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    std::array<uint64_t, kTimerPairs> timer_serials{};
    std::array<uint32_t, kTimerPairs> timer_blades{};
    uint64_t timed = 0;
    uint64_t window_samples = 0;
    double window_milliseconds = 0.0;
    double window_blades = 0.0;
};

GrassState& grass_state()
{
    static GrassState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener: at opaque_drawn, after the ray-traced effects have shaded the ground.
class GrassListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kGrass; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_grass_pass(state, view);
        }
    }
};

GrassListener& listener()
{
    static GrassListener instance;
    return instance;
}

// The field and pushers' way in (render_pc's grass_world).
class GrassSink final : public grass::Sink {
public:
    [[nodiscard]] float wanted_density() const override
    {
        const std::scoped_lock lock(backend().mutex);
        const GrassSettings& settings = grass_state().settings;
        return settings.on ? settings.density : 0.0F;
    }
    void set_field(std::shared_ptr<const grass::Field> field) override
    {
        const std::scoped_lock lock(backend().mutex);
        grass_state().field = std::move(field);
    }
    void set_pushers(std::span<const grass::Pusher> pushers) override
    {
        const std::scoped_lock lock(backend().mutex);
        std::vector<grass::Pusher>& kept = grass_state().pushers;
        kept.assign(pushers.begin(),
                    pushers.begin() + static_cast<std::ptrdiff_t>(std::min(pushers.size(), grass::kMostPushers)));
    }
};

GrassSink& sink_instance()
{
    static GrassSink instance;
    return instance;
}

// --- the processor's twin ------------------------------------------------------------------------------------------

using Vec3 = std::array<float, 3>;
using Vec2 = std::array<float, 2>;

float smoothstep(float low, float high, float value)
{
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    return along * along * (kThree - (kTwo * along));
}

float length2(const Vec2& v)
{
    return std::sqrt((core::at(v, 0) * core::at(v, 0)) + (core::at(v, 1) * core::at(v, 1)));
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_grass_settings(const GrassSettings& settings)
{
    GrassSettings clean = settings;
    const auto finite_or = [](float value, float fallback, float low, float high) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    constexpr float kLeastDensity = 0.25F;
    constexpr float kMostDensity = 4.0F;
    constexpr float kLeastHeight = 0.25F;
    constexpr float kMostHeight = 3.0F;
    constexpr float kLeastDistance = 10.0F;
    constexpr float kMostDistance = 200.0F;
    clean.density = finite_or(settings.density, 1.0F, kLeastDensity, kMostDensity);
    clean.height = finite_or(settings.height, 1.0F, kLeastHeight, kMostHeight);
    clean.distance = finite_or(settings.distance, kGrassDistance, kLeastDistance, kMostDistance);
    {
        const std::scoped_lock lock(backend().mutex);
        GrassState& own = grass_state();
        own.settings = clean;
        if (!clean.on) {
            own.field.reset(); // (the buffers go at the next pass or the shutdown)
        }
    }
    if (clean.on) {
        rt::add_view_listener(&listener());
        grass::set_sink(&sink_instance());
    } else {
        rt::remove_view_listener(&listener());
        grass::set_sink(nullptr);
    }
}

GrassSettings grass_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return grass_state().settings;
}

grass::Sink& grass_sink() { return sink_instance(); }

std::vector<grass::Pusher> grass_pushers()
{
    const std::scoped_lock lock(backend().mutex);
    return grass_state().pushers;
}

GrassView grass_view(const GrassSettings& settings, const rt::ViewDrawn& view, uint32_t height_pixels)
{
    GrassView made;
    const auto& v = view.camera.view;
    const auto& projection = view.camera.projection;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += core::at(core::at(v, row), k) * core::at(core::at(projection, k), column);
            }
            core::at(core::at(made.view_projection, row), column) = sum;
        }
    }
    made.camera = view.camera.position;
    made.distance = settings.distance * kGrassUnitsPerMetre;
    made.height = kGrassBaseHeight * settings.height;
    const float scale = std::abs(core::at(core::at(projection, 1), 1));
    made.pixel = scale > 0.0F && height_pixels > 0 ? kClipSpan / (scale * static_cast<float>(height_pixels)) : 0.0F;
    made.seconds = static_cast<float>(std::fmod(view.game_seconds, kWindClockWraps));
    return made;
}

float grass_keep(const GrassView& view, float distance)
{
    const float full = view.distance * kGrassNearShare;
    const float keep = distance <= full ? 1.0F : std::pow(full / distance, kGrassKeepPower);
    return keep * view.keep_scale;
}

float grass_fade(const GrassView& view, float distance)
{
    return 1.0F - smoothstep(kGrassFadeStart * view.distance, view.distance, distance);
}

GrassShape grass_blade_shape(const grass::Blade& blade, const GrassView& view, std::span<const grass::Pusher> pushers,
                             float known)
{
    const uint32_t packed = blade.packed;
    const float rank = (static_cast<float>(packed & kByte) + kHalf) / kRanks;
    const float tall =
        kGrassShortest + ((kGrassTallest - kGrassShortest) *
                          (static_cast<float>((packed >> kHeightShift) & kByte) / static_cast<float>(kByte)));
    const float turn = static_cast<float>((packed >> kFacingShift) & kByte) / kRanks * kTau;
    const float hash = static_cast<float>((packed >> kShadeShift) & kShadeMask) / kShadeSteps;
    GrassShape shape;
    shape.root = blade.root;
    const Vec3 to{core::at(view.camera, 0) - core::at(blade.root, 0),
                  core::at(view.camera, 1) - core::at(blade.root, 1),
                  core::at(view.camera, 2) - core::at(blade.root, 2)};
    const float apart_camera = std::sqrt((core::at(to, 0) * core::at(to, 0)) + (core::at(to, 1) * core::at(to, 1)) +
                                         (core::at(to, 2) * core::at(to, 2)));
    const float grow = std::clamp((grass_keep(view, apart_camera) - rank) / kGrassGrowBand, 0.0F, 1.0F);
    const float tall_now = view.height * tall * grass_fade(view, apart_camera) * std::clamp(known, 0.0F, 1.0F) * grow;
    const Vec2 facing{std::cos(turn), std::sin(turn)};
    Vec2 across = facing;
    const float flat_length = length2({core::at(to, 0), core::at(to, 2)});
    if (flat_length > kTiny) {
        const Vec2 side{-core::at(to, 2) / flat_length, core::at(to, 0) / flat_length};
        const Vec2 turned{core::at(facing, 0) + ((core::at(side, 0) - core::at(facing, 0)) * kGrassFaceCamera),
                          core::at(facing, 1) + ((core::at(side, 1) - core::at(facing, 1)) * kGrassFaceCamera)};
        const float turned_length = length2(turned);
        across = turned_length > kTiny ? Vec2{core::at(turned, 0) / turned_length, core::at(turned, 1) / turned_length}
                                       : facing;
    }
    const Vec2 lean{-core::at(across, 1), core::at(across, 0)};
    const float own = kGrassLean * ((hash * 2.0F) - 1.0F) * tall_now;
    Vec2 bend{core::at(lean, 0) * own, core::at(lean, 1) * own};
    const float phase = (core::at(blade.root, 0) * kWindAlongX) + (core::at(blade.root, 2) * kWindAlongZ);
    const float wave = (std::sin((view.seconds * kWindSpeedA) + phase) * kWindShareA) +
                       (std::sin((view.seconds * kWindSpeedB) + (phase * kWindStretchB) + kWindShiftB) * kWindShareB);
    const float sway = kGrassWind * tall_now * (kHalf + (kHalf * wave));
    core::at(bend, 0) += core::at(kGrassWindDirection, 0) * sway;
    core::at(bend, 1) += core::at(kGrassWindDirection, 1) * sway;
    for (std::size_t k = 0; k < std::min(pushers.size(), grass::kMostPushers); ++k) {
        const grass::Pusher& pusher = core::at(pushers, k);
        const Vec2 away{core::at(blade.root, 0) - core::at(pusher.at, 0),
                        core::at(blade.root, 2) - core::at(pusher.at, 2)};
        const float apart = length2(away);
        if (apart < pusher.radius && std::abs(core::at(blade.root, 1) - core::at(pusher.at, 1)) < kGrassPushHeight) {
            const float push = smoothstep(0.0F, 1.0F, 1.0F - (apart / pusher.radius));
            const Vec2 way = apart > kTiny ? Vec2{core::at(away, 0) / apart, core::at(away, 1) / apart} : lean;
            core::at(bend, 0) += core::at(way, 0) * kGrassPush * tall_now * push;
            core::at(bend, 1) += core::at(way, 1) * kGrassPush * tall_now * push;
        }
    }
    const float bent = length2(bend);
    const float most = kGrassMostBend * tall_now;
    if (bent > most) {
        core::at(bend, 0) *= most / bent;
        core::at(bend, 1) *= most / bent;
    }
    const float up = std::sqrt(std::max(
        (tall_now * tall_now) - ((core::at(bend, 0) * core::at(bend, 0)) + (core::at(bend, 1) * core::at(bend, 1))),
        0.0F));
    shape.tip = {core::at(blade.root, 0) + core::at(bend, 0), core::at(blade.root, 1) + up,
                 core::at(blade.root, 2) + core::at(bend, 1)};
    shape.control = {core::at(blade.root, 0) + (core::at(bend, 0) * kControlBend), core::at(blade.root, 1) + up,
                     core::at(blade.root, 2) + (core::at(bend, 1) * kControlBend)};
    shape.across = {core::at(across, 0), 0.0F, core::at(across, 1)};
    shape.height = tall_now;
    shape.width = tall_now > 0.0F ? std::max(kGrassWidth, kGrassLeastPixels * view.pixel * apart_camera) : 0.0F;
    shape.shade = hash;
    return shape;
}

namespace {

// The tile's box (its roots, grown by what its blades reach) outside the view: every corner beyond one plane.
bool outside_view(const grass::Tile& tile, const GrassView& view)
{
    const float out = view.height * kGrassTallest;
    const Vec3 low{core::at(tile.low, 0) - out, core::at(tile.low, 1), core::at(tile.low, 2) - out};
    const Vec3 high{core::at(tile.high, 0) + out, core::at(tile.high, 1) + (out * kTipStretch),
                    core::at(tile.high, 2) + out};
    constexpr std::size_t kPlanes = 6;
    std::array<std::size_t, kPlanes> beyond{};
    constexpr std::size_t kCorners = 8;
    for (std::size_t corner = 0; corner < kCorners; ++corner) {
        const Vec3 at{(corner & 1U) != 0 ? core::at(high, 0) : core::at(low, 0),
                      (corner & 2U) != 0 ? core::at(high, 1) : core::at(low, 1),
                      (corner & 4U) != 0 ? core::at(high, 2) : core::at(low, 2)};
        std::array<float, 4> clip{};
        for (std::size_t column = 0; column < 4; ++column) {
            const auto& matrix = view.view_projection;
            core::at(clip, column) = (core::at(at, 0) * core::at(core::at(matrix, 0), column)) +
                                     (core::at(at, 1) * core::at(core::at(matrix, 1), column)) +
                                     (core::at(at, 2) * core::at(core::at(matrix, 2), column)) +
                                     core::at(core::at(matrix, 3), column);
        }
        const float w = core::at(clip, 3);
        const std::array<bool, kPlanes> past{core::at(clip, 0) < -w, core::at(clip, 0) > w,    core::at(clip, 1) < -w,
                                             core::at(clip, 1) > w,  core::at(clip, 2) < 0.0F, core::at(clip, 2) > w};
        for (std::size_t plane = 0; plane < kPlanes; ++plane) {
            core::at(beyond, plane) += core::at(past, plane) ? 1U : 0U;
        }
    }
    return std::ranges::any_of(beyond, [](std::size_t count) { return count == kCorners; });
}

float distance_to_box(const grass::Tile& tile, const Vec3& point)
{
    float sum = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float nearest = std::clamp(core::at(point, axis), core::at(tile.low, axis), core::at(tile.high, axis));
        const float apart = core::at(point, axis) - nearest;
        sum += apart * apart;
    }
    return std::sqrt(sum);
}

// How many of the tile's first blades have a rank below `keep` (the tile's are in rank order) - and a grow band's worth.
uint32_t kept_in_tile(const grass::Field& field, const grass::Tile& tile, float keep)
{
    const float limit = std::min(keep, 1.0F) * kRanks; // (a blade shows while (rank + 0.5) / 256 < keep)
    uint32_t low = 0;
    uint32_t high = tile.count;
    while (low < high) {
        const uint32_t middle = low + ((high - low) / 2);
        const auto rank = static_cast<float>(core::at(field.blades, tile.first + middle).packed & kByte);
        if (rank + kHalf < limit) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

// A blade's segments at a distance: near, middling or far.
uint32_t segments_at(float distance, float most)
{
    if (distance < kNearSegmentsShare * most) {
        return kNearSegments;
    }
    return distance < kMiddleSegmentsShare * most ? kMiddleSegments : kFarSegments;
}

std::vector<GrassDraw> draws_at(const grass::Field& field, const GrassView& view, uint32_t* blades)
{
    std::vector<GrassDraw> draws;
    *blades = 0;
    bool last_whole = false; // (the last draw ends with a whole tile: the next tile's blades follow on)
    for (const grass::Tile& tile : field.tiles) {
        const float closest = distance_to_box(tile, view.camera);
        if (closest >= view.distance || tile.count == 0 || outside_view(tile, view)) {
            last_whole = false;
            continue;
        }
        const uint32_t count = kept_in_tile(field, tile, grass_keep(view, closest));
        if (count == 0) {
            last_whole = false;
            continue;
        }
        const uint32_t segments = segments_at(closest, view.distance);
        *blades += count;
        if (last_whole && draws.back().segments == segments && draws.back().first + draws.back().count == tile.first) {
            draws.back().count += count; // (one draw for both)
        } else {
            draws.push_back({.first = tile.first, .count = count, .segments = segments});
        }
        last_whole = count == tile.count;
    }
    return draws;
}

} // namespace

std::vector<GrassDraw> plan_grass_draws(const grass::Field& field, GrassView* view, uint32_t* blades)
{
    view->keep_scale = 1.0F;
    std::vector<GrassDraw> draws = draws_at(field, *view, blades);
    if (*blades > kGrassMostDrawn) {
        view->keep_scale = static_cast<float>(kGrassMostDrawn) / static_cast<float>(*blades);
        draws = draws_at(field, *view, blades);
    }
    return draws;
}

namespace {

// --- the pass's objects -----------------------------------------------------------------------------------------

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

std::string source_of(const char* body, bool writes_colours, const char* define)
{
    std::string source = "#version 460\n";
    if (define != nullptr) {
        source += "#define ";
        source += define;
        source += '\n';
    }
    source += writes_colours ? "#define COLOURS_ACCESS\n" : "#define COLOURS_ACCESS readonly\n";
    source += constants_source();
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

bool make_layouts(Backend& state, std::string* error)
{
    GrassState& own = grass_state();
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (binding == kBindingParams) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingSource || binding == kBindingDepth) {
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
    const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(RangeConstants)};
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

// The layouts, the sampler, the compute pipelines and the draw's shader modules (once).
bool ensure_objects(Backend& state, std::string* error)
{
    GrassState& own = grass_state();
    if (own.probe != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    for (std::size_t variant = 0; variant < own.gather.size(); ++variant) {
        if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
            core::at(own.gather, variant) = make_compute(
                device, own.layout, source_of(kGatherBody, true, variant == 1 ? "MULTISAMPLED" : nullptr), error);
            if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    for (std::size_t variant = 0; variant < own.vertex.size(); ++variant) {
        const char* motion = variant == 1 ? "MOTION" : nullptr;
        if (core::at(own.vertex, variant) == VK_NULL_HANDLE) {
            core::at(own.vertex, variant) =
                make_module(device, RtShaderStage::kVertex, source_of(kVertexBody, false, motion), error);
        }
        if (core::at(own.fragment, variant) == VK_NULL_HANDLE) {
            core::at(own.fragment, variant) =
                make_module(device, RtShaderStage::kFragment, source_of(kFragmentBody, false, motion), error);
        }
        if (core::at(own.vertex, variant) == VK_NULL_HANDLE || core::at(own.fragment, variant) == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.probe = make_compute(device, own.layout, source_of(kProbeBody, false, nullptr), error);
    return own.probe != VK_NULL_HANDLE;
}

// The draw's pipeline for the targets' formats and samples, with or without the motion attachment (made on first use):
// triangle strips, no culling, the depth tested and written, the colour's red, green and blue written (alpha kept).
VkPipeline draw_pipeline(Backend& state, const DrawPipeline& wanted, std::string* error)
{
    GrassState& own = grass_state();
    for (const DrawPipeline& made : own.draws) {
        if (made.format == wanted.format && made.depth == wanted.depth && made.samples == wanted.samples &&
            made.motion == wanted.motion) {
            return made.pipeline;
        }
    }
    const std::size_t variant = wanted.motion ? 1 : 0;
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = core::at(own.vertex, variant),
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = core::at(own.fragment, variant),
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{.sType =
                                                               VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0,
                                                           .rasterizationSamples = wanted.samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
    core::at(blend, 0).colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    core::at(blend, 1).colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = wanted.motion ? 2U : 1U;
    blending.pAttachments = blend.data();
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    const std::array<VkFormat, 2> formats{wanted.format, kMotionFormat};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = wanted.motion ? 2U : 1U;
    rendering.pColorAttachmentFormats = formats.data();
    rendering.depthAttachmentFormat = wanted.depth;
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
    if (vkCreateGraphicsPipelines(state.context.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    DrawPipeline made = wanted;
    made.pipeline = pipeline;
    own.draws.push_back(made);
    return pipeline;
}

bool ensure_source(Backend& state, const Image& colour)
{
    GrassState& own = grass_state();
    Image& source = own.source;
    if (source.image != VK_NULL_HANDLE && source.format == colour.format &&
        source.extent.width == colour.extent.width && source.extent.height == colour.extent.height) {
        return true;
    }
    retire_image(state, &source);
    const ImageRequest request{.format = colour.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = colour.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &source)) {
        return false;
    }
    prepare_image(state, &source);
    return true;
}

// The field's blades into a buffer of the device's, and its colours' buffer cleared (none known) - once a field; the
// old buffers retired.
bool upload_field(Backend& state)
{
    GrassState& own = grass_state();
    if (own.uploaded == own.field) {
        return own.field != nullptr;
    }
    retire_buffer(state, &own.blades);
    retire_buffer(state, &own.colours);
    own.uploaded = own.field;
    if (own.field == nullptr || own.field->blades.empty()) {
        return false;
    }
    const std::vector<grass::Blade>& blades = own.field->blades;
    const VkDeviceSize blade_bytes = blades.size() * sizeof(grass::Blade);
    const VkDeviceSize colour_bytes = blades.size() * kColourBytes;
    const bool made =
        state.memory.create_buffer(blade_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   BufferPlace::kDevice, &own.blades) &&
        state.memory.create_buffer(colour_bytes,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   BufferPlace::kDevice, &own.colours);
    std::optional<RingSpan> staged;
    if (made) {
        std::vector<uint8_t> bytes(static_cast<std::size_t>(blade_bytes));
        std::memcpy(bytes.data(), blades.data(), bytes.size());
        staged = state.staging.copy(bytes, sizeof(float));
    }
    if (!staged) {
        if (first_report(state, kReportUpload)) {
            get_log("vulkan: grass: the field's %zu blades couldn't be put on the device - no grass", blades.size());
        }
        state.memory.destroy_buffer(&own.blades);
        state.memory.destroy_buffer(&own.colours);
        own.uploaded.reset();
        own.field.reset();
        return false;
    }
    VkCommandBuffer commands = state.frames.uploads();
    const VkBufferCopy region{.srcOffset = staged->offset, .dstOffset = 0, .size = blade_bytes};
    vkCmdCopyBuffer(commands, staged->buffer, own.blades.buffer, 1, &region);
    vkCmdFillBuffer(commands, own.colours.buffer, 0, VK_WHOLE_SIZE, 0);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
    get_log("vulkan: grass: a field of %zu blades in %zu tiles on the device (%.1f MB)", blades.size(),
            own.field->tiles.size(), static_cast<double>(blade_bytes + colour_bytes) / kBytesPerMegabyte);
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

std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left),
            std::min(core::at(rect, 3), extent.height - top)};
}

Float4x4 flat(const std::array<std::array<float, 4>, 4>& matrix)
{
    Float4x4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(matrix, row), column);
        }
    }
    return out;
}

// What a view's pass works on.
struct ViewPlan {
    Image* colour = nullptr;
    Image* depth = nullptr;     // the depth attachment (the same samples as the colour)
    bool sampled_depth = false; // ... and readable by the gather
    bool motion = false;
    std::array<uint32_t, 4> rect{};
    GrassView shape;
    std::vector<GrassDraw> draws;
    uint32_t blades = 0;
};

// The view's block: its camera, the blades' shape, the depth's mapping, the pushers.
ParamsBlock params_for(const Backend& state, const ViewPlan& plan, const rt::ViewDrawn& view, bool probing)
{
    const GrassState& own = grass_state();
    ParamsBlock block{};
    block.vp = flat(plan.shape.view_projection);
    block.vp_before = block.vp;
    const MotionVectors& motion = state.motion;
    if (plan.motion && motion.before.known) {
        block.vp_before = motion_multiply(motion.before.view, motion.before.projection);
    }
    block.camera = {core::at(plan.shape.camera, 0), core::at(plan.shape.camera, 1), core::at(plan.shape.camera, 2),
                    plan.shape.seconds};
    block.lod = {plan.shape.distance, plan.shape.distance * kGrassNearShare, plan.shape.keep_scale, plan.shape.pixel};
    block.blade = {plan.shape.height, 0.0F, 0.0F, 0.0F};
    block.rect = {static_cast<float>(core::at(plan.rect, 0)), static_cast<float>(core::at(plan.rect, 1)),
                  static_cast<float>(core::at(plan.rect, 2)), static_cast<float>(core::at(plan.rect, 3))};
    block.offsets = {core::at(plan.rect, 2) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 2)) : 0.0F,
                     core::at(plan.rect, 3) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 3)) : 0.0F,
                     plan.motion ? core::at(motion.jitter, 0) : 0.0F, plan.motion ? core::at(motion.jitter, 1) : 0.0F};
    constexpr std::size_t kRowZ = 2;
    constexpr std::size_t kRowW = 3;
    const auto& projection = view.camera.projection;
    block.depth_projection = {
        core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
        core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
    const float span = core::at(view.depth_range, 1) - core::at(view.depth_range, 0);
    block.depth_map = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F,
                       plan.sampled_depth ? 1.0F : 0.0F, probing ? 1.0F : 0.0F};
    const std::size_t pushers = std::min(own.pushers.size(), grass::kMostPushers);
    block.counts = {static_cast<int32_t>(pushers), 0, 0, 0};
    for (std::size_t k = 0; k < pushers; ++k) {
        const grass::Pusher& pusher = core::at(own.pushers, k);
        core::at(block.pushers, k) = {core::at(pusher.at, 0), core::at(pusher.at, 1), core::at(pusher.at, 2),
                                      pusher.radius};
    }
    return block;
}

std::optional<VkDescriptorBufferInfo> block_in_ring(Backend& state, const ParamsBlock& block)
{
    const void* const bytes = &block;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(bytes), sizeof(block)), state.uniform_alignment);
    if (!span) {
        return std::nullopt;
    }
    return VkDescriptorBufferInfo{.buffer = span->buffer, .offset = span->offset, .range = sizeof(block)};
}

// The bindings pushed: the blades, the colours, the block; the images and the probe's answers when given.
struct Bound {
    VkDescriptorBufferInfo params{};
    const Image* source = nullptr;
    const Image* depth = nullptr; // (DEPTH_STENCIL_READ_ONLY_OPTIMAL; null: the source in its place)
    const DeviceBuffer* probe = nullptr;
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    const GrassState& own = grass_state();
    const VkDescriptorBufferInfo blades{.buffer = own.blades.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo colours{.buffer = own.colours.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    VkDescriptorBufferInfo probe{};
    std::array<VkDescriptorImageInfo, 2> images{};
    std::vector<VkWriteDescriptorSet> writes; // (pointing at the infos above, which outlive the push)
    writes.reserve(kBindingCount);
    const auto add_buffer = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet& write = writes.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = info;
    };
    add_buffer(kBindingBlades, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &blades);
    add_buffer(kBindingColours, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &colours);
    add_buffer(kBindingParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bound.params);
    if (bound.source != nullptr) {
        core::at(images, 0) = {.sampler = own.sampler,
                               .imageView = bound.source->view,
                               .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        core::at(images, 1) =
            bound.depth != nullptr
                ? VkDescriptorImageInfo{.sampler = own.sampler,
                                        .imageView = bound.depth->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}
                : core::at(images, 0);
        for (uint32_t which = 0; which < 2; ++which) {
            VkWriteDescriptorSet& write = writes.emplace_back();
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = kBindingSource + which;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &core::at(images, which);
        }
    }
    if (bound.probe != nullptr) {
        probe = {.buffer = bound.probe->buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        add_buffer(kBindingProbe, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &probe);
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
}

void push_range(VkCommandBuffer commands, uint32_t first, uint32_t count, uint32_t segments)
{
    const RangeConstants constants{.range = {first, count, segments, 0}};
    vkCmdPushConstants(commands, grass_state().layout, kStages, 0, sizeof(constants), &constants);
}

void memory_barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags written, VkPipelineStageFlags to,
                    VkAccessFlags read)
{
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = written, .dstAccessMask = read};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// The depth moved for the gather to read (from what wrote it), or back to be drawn into.
void depth_barrier(VkCommandBuffer commands, Image* depth, bool to_gather)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout layout =
        to_gather ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_gather ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_gather ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(
        commands, to_gather ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        to_gather ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// The viewport's colour copied out (resolved when multisampled) into the source image.
void copy_colour(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& rect)
{
    GrassState& own = grass_state();
    // (the last pass's gather read the source: done before it is written again)
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{
        .x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(rect, 2), .height = core::at(rect, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{.srcSubresource = layers,
                                 .srcOffset = corner,
                                 .dstSubresource = layers,
                                 .dstOffset = corner,
                                 .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{.srcSubresource = layers,
                                    .srcOffset = corner,
                                    .dstSubresource = layers,
                                    .dstOffset = corner,
                                    .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    const VkImageMemoryBarrier to_read{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = own.source.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &to_read);
    own.source.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// The timestamps' pool (once; none when the device can't time).
void ensure_timer(Backend& state)
{
    GrassState& own = grass_state();
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

void read_timer(Backend& state, uint32_t pair)
{
    GrassState& own = grass_state();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, 2> stamps{};
    const VkResult got =
        vkGetQueryPoolResults(state.context.device(), own.timer, pair * 2, 2, sizeof(stamps), stamps.data(),
                              sizeof(Stamp), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((got != VK_SUCCESS && got != VK_NOT_READY) || core::at(stamps, 0).available == 0 ||
        core::at(stamps, 1).available == 0 || core::at(stamps, 1).value < core::at(stamps, 0).value) {
        return;
    }
    ++own.window_samples;
    own.window_milliseconds +=
        static_cast<double>(core::at(stamps, 1).value - core::at(stamps, 0).value) * own.tick_milliseconds;
    own.window_blades += static_cast<double>(core::at(own.timer_blades, pair));
    if (own.window_samples >= kTimingLogEvery) {
        const auto samples = static_cast<double>(own.window_samples);
        get_log(
            "vulkan: grass: the pass takes %.3f ms on the device (%.0f blades a pass; the last %llu passes' average)",
            own.window_milliseconds / samples, own.window_blades / samples,
            static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = 0.0;
        own.window_blades = 0.0;
    }
}

bool begin_timing(Backend& state, VkCommandBuffer commands, uint32_t blades, uint32_t* pair)
{
    GrassState& own = grass_state();
    ensure_timer(state);
    *pair = static_cast<uint32_t>(own.timed % kTimerPairs);
    uint64_t& written = core::at(own.timer_serials, *pair);
    if (own.timer == VK_NULL_HANDLE || (written != 0 && state.frames.completed() < written)) {
        return false;
    }
    if (written != 0) {
        read_timer(state, *pair);
    }
    written = state.frames.serial();
    core::at(own.timer_blades, *pair) = blades;
    vkCmdResetQueryPool(commands, own.timer, *pair * 2, 2);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer, *pair * 2);
    return true;
}

// The view's targets, rectangle and draws; false (logged once) when there is nothing to draw.
bool plan_view(Backend& state, const rt::ViewDrawn& view, ViewPlan* plan)
{
    const GrassState& own = grass_state();
    Surface* const colour_surface = core::at(state.targets, 0);
    Image* const colour = image_of_surface(colour_surface);
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 ||
        depth == nullptr || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        depth->attachment_view == VK_NULL_HANDLE || depth->samples != colour->samples) {
        if (first_report(state, kReportTarget)) {
            get_log(
                "vulkan: grass: a view drawn into targets that can't take it (no depth, or not readable) - none there");
        }
        return false;
    }
    plan->colour = colour;
    plan->depth = depth;
    plan->sampled_depth = (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE;
    if (!plan->sampled_depth && first_report(state, kReportNoDepth)) {
        get_log("vulkan: grass: the depth can't be read - each blade takes the colour under its root unchecked");
    }
    const Extent area{.width = std::min(colour->extent.width, depth->extent.width),
                      .height = std::min(colour->extent.height, depth->extent.height)};
    plan->rect = clip_rect(view.rect, area);
    if (core::at(plan->rect, 2) == 0 || core::at(plan->rect, 3) == 0) {
        return false;
    }
    plan->motion = motion_wanted(state, colour_surface, colour) && state.motion.image.image != VK_NULL_HANDLE;
    plan->shape = grass_view(own.settings, view, core::at(plan->rect, 3));
    plan->draws = plan_grass_draws(*own.field, &plan->shape, &plan->blades);
    return !plan->draws.empty();
}

// The blades drawn into the colour and depth (and the motion).
void record_draws(Backend& state, VkCommandBuffer commands, VkPipeline pipeline, const ViewPlan& plan,
                  const VkDescriptorBufferInfo& params, const rt::ViewDrawn& view)
{
    Image* const colour = plan.colour;
    Image* const depth = plan.depth;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    std::array<VkRenderingAttachmentInfo, 2> colours{};
    VkRenderingAttachmentInfo& colour_info = core::at(colours, 0);
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (plan.motion) {
        Image& image = state.motion.image;
        Memory::transition(commands, &image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo& motion_info = core::at(colours, 1);
        motion_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        motion_info.imageView = image.attachment_view;
        motion_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        motion_info.resolveMode = VK_RESOLVE_MODE_NONE;
        motion_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        motion_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        image.used = state.frames.serial();
    }
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_info.imageView = depth->attachment_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(plan.rect, 0)),
                                   .y = static_cast<int32_t>(core::at(plan.rect, 1))},
                        .extent = {.width = core::at(plan.rect, 2), .height = core::at(plan.rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = plan.motion ? 2U : 1U;
    info.pColorAttachments = colours.data();
    info.pDepthAttachment = &depth_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = std::clamp(core::at(view.depth_range, 0), 0.0F, 1.0F),
                              .maxDepth = std::clamp(core::at(view.depth_range, 1), 0.0F, 1.0F)};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.params = params});
    for (const GrassDraw& draw : plan.draws) {
        push_range(commands, draw.first, draw.count, draw.segments);
        vkCmdDraw(commands, (draw.segments * 2) + 1, draw.count, 0, draw.first);
    }
    vkCmdEndRendering(commands);
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
}

void log_first_pass(Backend& state, const ViewPlan& plan)
{
    if (!first_report(state, kReportFirstPass)) {
        return;
    }
    get_log("vulkan: grass: the first pass (%ux%u, %u sample%s%s%s; %u blades in %zu draws, out to %.0f units, %.1f "
            "units tall)",
            core::at(plan.rect, 2), core::at(plan.rect, 3), static_cast<unsigned>(plan.colour->samples),
            plan.colour->samples == VK_SAMPLE_COUNT_1_BIT ? "" : "s", plan.motion ? ", motion" : "",
            plan.sampled_depth ? "" : ", no depth read", plan.blades, plan.draws.size(),
            static_cast<double>(plan.shape.distance), static_cast<double>(plan.shape.height));
}

bool ready_for(Backend& state, const ViewPlan& plan, VkPipeline* pipeline)
{
    GrassState& own = grass_state();
    std::string error;
    *pipeline = ensure_objects(state, &error) ? draw_pipeline(state,
                                                              {.format = plan.colour->format,
                                                               .depth = plan.depth->format,
                                                               .samples = plan.colour->samples,
                                                               .motion = plan.motion},
                                                              &error)
                                              : VK_NULL_HANDLE;
    const bool made = *pipeline != VK_NULL_HANDLE && ensure_source(state, *plan.colour);
    if (!made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: grass: the pass couldn't be made (%s) - no grass",
                    error.empty() ? "its images" : error.c_str());
        }
    }
    return made;
}

} // namespace

bool record_grass_pass(Backend& state, const rt::ViewDrawn& view)
{
    GrassState& own = grass_state();
    if (!own.settings.on || own.failed) {
        return false;
    }
    if (!upload_field(state)) {
        return false;
    }
    ViewPlan plan;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!plan_view(state, view, &plan) || !ready_for(state, plan, &pipeline)) {
        return false;
    }
    const std::optional<VkDescriptorBufferInfo> params = block_in_ring(state, params_for(state, plan, view, false));
    if (!params) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: grass: no room in the ring for the pass's block - skipped");
        }
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    uint32_t pair = 0;
    const bool timing = begin_timing(state, commands, plan.blades, &pair);
    copy_colour(commands, plan.colour, plan.rect);
    // the gather: the last pass's draws read the colours, the last gather wrote them
    memory_barrier(commands, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    if (plan.sampled_depth) {
        depth_barrier(commands, plan.depth, true);
    }
    const bool multisampled_depth = plan.sampled_depth && plan.depth->samples != VK_SAMPLE_COUNT_1_BIT;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.gather, multisampled_depth ? 1 : 0));
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                     {.params = *params, .source = &own.source, .depth = plan.sampled_depth ? plan.depth : nullptr});
    for (const GrassDraw& draw : plan.draws) {
        push_range(commands, draw.first, draw.count, draw.segments);
        vkCmdDispatch(commands, (draw.count + kGroupSize - 1) / kGroupSize, 1, 1);
    }
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    if (plan.sampled_depth) {
        depth_barrier(commands, plan.depth, false);
    }
    record_draws(state, commands, pipeline, plan, *params, view);
    if (timing) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timer, (pair * 2) + 1);
        ++own.timed;
    }
    state.bound = BoundState{};
    own.source.used = state.frames.serial();
    own.last_blades = plan.blades;
    own.last_draws = static_cast<uint32_t>(plan.draws.size());
    ++own.passes;
    log_first_pass(state, plan);
    return true;
}

uint64_t grass_passes() { return grass_state().passes; }

uint32_t grass_last_blades() { return grass_state().last_blades; }

uint32_t grass_last_draws() { return grass_state().last_draws; }

bool probe_grass(Backend& state, const rt::ViewDrawn& view, uint32_t first, uint32_t count,
                 std::vector<std::array<float, 4>>* tips)
{
    const GrassState& own = grass_state();
    tips->clear();
    if (!upload_field(state) || own.field == nullptr || count == 0 || first + count > own.field->blades.size()) {
        return false;
    }
    std::string error;
    if (!ensure_objects(state, &error)) {
        return false;
    }
    ViewPlan plan;
    plan.rect = view.rect;
    plan.shape = grass_view(own.settings, view, std::max(core::at(view.rect, 3), 1U));
    const std::optional<VkDescriptorBufferInfo> params = block_in_ring(state, params_for(state, plan, view, true));
    DeviceBuffer answers;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(count) * sizeof(std::array<float, 4>);
    if (!params ||
        !state.memory.create_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    BufferPlace::kDevice, &answers)) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.probe);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, {.params = *params, .probe = &answers});
    push_range(commands, first, count, 0);
    vkCmdDispatch(commands, (count + kGroupSize - 1) / kGroupSize, 1, 1);
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    state.bound = BoundState{};
    std::vector<uint8_t> raw;
    const bool read = finish_work(state) && state.memory.read_buffer(answers, 0, bytes, &raw);
    state.memory.destroy_buffer(&answers);
    if (!read || raw.size() < bytes) {
        return false;
    }
    tips->resize(count);
    std::memcpy(tips->data(), raw.data(), static_cast<std::size_t>(bytes));
    return true;
}

bool read_grass_colours(Backend& state, std::vector<std::array<float, 4>>* colours)
{
    const GrassState& own = grass_state();
    colours->clear();
    if (own.colours.buffer == VK_NULL_HANDLE || own.uploaded == nullptr) {
        return false;
    }
    const std::size_t count = own.uploaded->blades.size();
    std::vector<uint8_t> raw;
    if (!finish_work(state) || !state.memory.read_buffer(own.colours, 0, count * kColourBytes, &raw) ||
        raw.size() < count * kColourBytes) {
        return false;
    }
    const auto half = [](uint16_t bits) {
        constexpr uint32_t kMantissaBits = 10;
        constexpr uint32_t kExponentMask = 0x1F;
        constexpr uint32_t kMantissaMask = 0x3FF;
        constexpr uint32_t kImplicitOne = 0x400;
        constexpr int kSmallestPower = -24;
        constexpr int kPowerBias = 25;
        constexpr uint16_t kSign = 0x8000;
        const uint32_t exponent = (static_cast<uint32_t>(bits) >> kMantissaBits) & kExponentMask;
        const uint32_t mantissa = bits & kMantissaMask;
        float magnitude = 0.0F;
        if (exponent == 0) {
            magnitude = std::ldexp(static_cast<float>(mantissa), kSmallestPower);
        } else if (exponent == kExponentMask) {
            magnitude = mantissa == 0 ? INFINITY : NAN;
        } else {
            magnitude =
                std::ldexp(static_cast<float>(mantissa + kImplicitOne), static_cast<int>(exponent) - kPowerBias);
        }
        return (bits & kSign) != 0 ? -magnitude : magnitude;
    };
    colours->resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::array<uint16_t, 4> halves{};
        std::memcpy(halves.data(), std::span(raw).subspan(index * kColourBytes, kColourBytes).data(), kColourBytes);
        core::at(*colours, index) = {half(core::at(halves, 0)), half(core::at(halves, 1)), half(core::at(halves, 2)),
                                     half(core::at(halves, 3))};
    }
    return true;
}

void release_grass(Backend& state)
{
    GrassState& own = grass_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const DrawPipeline& made : own.draws) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline pipeline : {core::at(own.gather, 0), core::at(own.gather, 1), own.probe}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (VkShaderModule module :
             {core::at(own.vertex, 0), core::at(own.vertex, 1), core::at(own.fragment, 0), core::at(own.fragment, 1)}) {
            if (module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, module, nullptr);
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
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.source);
        state.memory.destroy_buffer(&own.blades);
        state.memory.destroy_buffer(&own.colours);
    }
    own.draws.clear();
    own.gather = {};
    own.probe = VK_NULL_HANDLE;
    own.vertex = {};
    own.fragment = {};
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timer_serials = {};
    own.uploaded.reset();
    own.failed = false;
}

} // namespace render::vulkan
