// NOT ORIGINAL - decals and clutter: marks and small props over the level's surfaces (vulkan_scatter.h;
// docs/design/vulkan.md "Decals and clutter").
#include "gpu_vulkan/vulkan_scatter.h"

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
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/scatter.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_reflections.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportTarget = 0x53435401U;
constexpr uint64_t kReportPipeline = 0x53435402U;
constexpr uint64_t kReportFirstDecals = 0x53435403U;
constexpr uint64_t kReportNoDepth = 0x53435404U;
constexpr uint64_t kReportUpload = 0x53435405U;
constexpr uint64_t kReportNoRoom = 0x53435406U;
constexpr uint64_t kReportFirstClutter = 0x53435407U;

constexpr uint32_t kGroupSize = 64;
constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
constexpr uint32_t kBindingDecals = 0; // the decals' vertices (storage)
constexpr uint32_t kBindingItems = 1;  // the clutter's items (storage)
constexpr uint32_t kBindingLights =
    2; // the items' ground light (storage: two uints an item - half floats r g, b known)
constexpr uint32_t kBindingMeshes = 3; // the clutter's meshes (storage)
constexpr uint32_t kBindingParams = 4; // the view's block (uniform, in the ring)
constexpr uint32_t kBindingAtlas = 5;  // the decals' atlas (sampled, mipmapped)
constexpr uint32_t kBindingSource = 6; // the viewport's colour copied out (sampled)
constexpr uint32_t kBindingDepth = 7;  // the depth (sampled)
constexpr uint32_t kBindingCount = 8;
constexpr std::size_t kLightBytes = 8;
constexpr float kRanks = 256.0F;
constexpr float kHalf = 0.5F;
constexpr uint32_t kByte = 0xFFU;
constexpr float kGrowBand = 0.08F;
constexpr double kWindClockWraps = 3600.0;
constexpr float kClipSpan = 2.0F;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr std::size_t kTexel = 4;
constexpr float kItemReach = 12.0F; // (a tile's box grown by this: its items' meshes)
constexpr VkFormat kWetFormat = VK_FORMAT_R8_UNORM;

std::string glsl_float(float value)
{
    std::string written = text::format("%.9g", static_cast<double>(value));
    if (written.find_first_of(".eEn") == std::string::npos) {
        written += ".0";
    }
    return written;
}

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
    define("DECAL_FADE_START", kDecalFadeStart);
    define("DECAL_FADE_UNITS", static_cast<float>(scatter::kDecalFadeUnits));
    define("DECAL_MATCH_SHARE", kDecalMatchShare);
    define("DECAL_MATCH_SLACK", kDecalMatchSlack);
    define("ATLAS_COLUMNS", static_cast<float>(scatter::kDecalVariants));
    define("ATLAS_ROWS", static_cast<float>(scatter::kDecalKinds));
    define("ATLAS_TEXELS", static_cast<float>(scatter::kDecalTile));
    define("KEEP_POWER", kClutterKeepPower);
    define("GROW_BAND", kGrowBand);
    define("FADE_START", kClutterFadeStart);
    define("GROUND_ALBEDO", kClutterGroundAlbedo);
    define("LEARN", kClutterLearn);
    define("MATCH_SHARE", kClutterMatchShare);
    define("MATCH_SLACK", kClutterMatchSlack);
    out += "#define MESH_VERTICES " + std::to_string(scatter::kMeshVertices) + "u\n";
    define("WET_ROW", static_cast<float>(scatter::DecalKind::kWet));
    return out;
}

// What every stage shares: the view's block, the depth's linearisation.
constexpr const char* kCommon = R"(
layout(std140, set = 0, binding = 4) uniform Params {
    mat4 vp;               // D3D's row-vector matrix as stored: vp * v is its v x M
    mat4 vp_before;        // the frame before's, unjittered (the motion)
    vec4 camera;           // xyz; w: the wind's clock (seconds)
    vec4 lod;              // x: the clutter's distance; y: its near distance; z: the share kept's scale; w: the decals'
    vec4 rect;             // the viewport: x, y, width, height (pixels)
    vec4 offsets;          // x, y: Direct3D 9's half pixel (1 / the size); z, w: the jitter (clip space)
    vec4 depth_projection; // P22, P32, P23, P33
    vec4 depth_map;        // x: the depth range's minimum; y: 1 / its span
} p;

const float TAU = 6.28318530718;

float linear_depth(float d)
{
    float ndc = (d - p.depth_map.x) * p.depth_map.y;
    float z = (p.depth_projection.y - ndc * p.depth_projection.w) / (ndc * p.depth_projection.z - p.depth_projection.x);
    float w = z * p.depth_projection.z + p.depth_projection.w;
    return abs(p.depth_projection.z) > 0.5 ? w : abs(z);
}

vec4 to_screen(vec3 position)
{
    vec4 clip = p.vp * vec4(position, 1.0);
    return vec4(clip.x + clip.w * p.offsets.x, -clip.y + clip.w * p.offsets.y, clip.z, clip.w);
}

vec3 finite_colour(vec3 c)
{
    return vec3(c.r >= 0.0 ? min(c.r, 64.0) : 0.0, c.g >= 0.0 ? min(c.g, 64.0) : 0.0, c.b >= 0.0 ? min(c.b, 64.0) : 0.0);
}
)";

// --- the decals ----------------------------------------------------------------------------------------------------

constexpr const char* kDecalVertexBody = R"(
layout(std430, set = 0, binding = 0) readonly buffer Decals { float decals[]; };
layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out vec2 v_cell;
layout(location = 2) out float v_weight;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
    uint at = uint(gl_VertexIndex) * 6u;
    vec3 position = vec3(decals[at], decals[at + 1u], decals[at + 2u]);
    uint packed = floatBitsToUint(decals[at + 5u]);
    float opacity = float((packed >> 8u) & 255u) / 255.0;
    float own = min(float((packed >> 16u) & 255u) * DECAL_FADE_UNITS, p.lod.w);
    float d = distance(position, p.camera.xyz);
    v_weight = opacity * (1.0 - smoothstep(DECAL_FADE_START * own, own, d));
    v_uv = vec2(decals[at + 3u], decals[at + 4u]);
    v_cell = vec2(float((packed >> 4u) & 3u), float(packed & 15u));
    gl_Position = to_screen(position);
}
)";

constexpr const char* kDecalFragmentBody = R"(
layout(set = 0, binding = 5) uniform sampler2D u_atlas;
#ifdef MULTISAMPLED
layout(set = 0, binding = 7) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 7) uniform sampler2D u_depth;
#endif
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in vec2 v_cell;
layout(location = 2) in float v_weight;
layout(location = 0) out vec4 o_colour;
#ifdef WET
layout(location = 1) out vec4 o_wet;
#endif

void main()
{
    const float edge = 0.5 / ATLAS_TEXELS;
    vec2 uv = (v_cell + clamp(v_uv, vec2(edge), vec2(1.0 - edge))) / vec2(ATLAS_COLUMNS, ATLAS_ROWS);
    vec4 texel = texture(u_atlas, uv); // (before any discard: the derivatives are the quad's)
    float own = linear_depth(gl_FragCoord.z);
    float scene = linear_depth(texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r);
    if (!(abs(scene - own) <= own * DECAL_MATCH_SHARE + DECAL_MATCH_SLACK) || !(v_weight > 0.0)) {
        discard;
    }
    float amount = clamp(texel.a * v_weight, 0.0, 1.0);
    o_colour = vec4(mix(vec3(1.0), clamp(texel.rgb, 0.0, 1.0), amount), 1.0);
#ifdef WET
    // (a wet patch: its darkening is its wetness - the art's full darkening, about 0.4, a puddle)
    float wet = v_cell.y == WET_ROW ? clamp((1.0 - texel.g) * 2.5, 0.0, 1.0) * amount : 0.0;
    o_wet = vec4(wet, 0.0, 0.0, 0.0);
#endif
}
)";

// --- the clutter ---------------------------------------------------------------------------------------------------

constexpr const char* kItemCommon = R"(
layout(std430, set = 0, binding = 1) readonly buffer Items { vec4 items[]; };
layout(std430, set = 0, binding = 2) LIGHTS_ACCESS buffer Lights { uvec2 lights[]; };

const uint KIND_LEAF = 0u;
const uint KIND_PAPER = 1u;
const uint KIND_CAN = 2u;
const uint KIND_WEED = 3u;
const uint KIND_PEBBLE = 4u;

float keep_at(float d)
{
    float near = p.lod.y;
    float k = d <= near ? 1.0 : pow(near / d, KEEP_POWER);
    return k * p.lod.z;
}

float fade_at(float d) { return 1.0 - smoothstep(FADE_START * p.lod.x, p.lod.x, d); }
)";

// The gather: each item's root on the screen; where the depth there is the root's, the colour there kept.
constexpr const char* kGatherBody = R"(
layout(local_size_x = 64) in;
layout(set = 0, binding = 6) uniform sampler2D u_source;
#ifdef MULTISAMPLED
layout(set = 0, binding = 7) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 7) uniform sampler2D u_depth;
#endif
layout(push_constant) uniform Range { uvec4 range; } pc; // first, count

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.range.y) {
        return;
    }
    uint index = pc.range.x + i;
    vec3 root = items[index * 2u].xyz;
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
    float z = linear_depth(texelFetch(u_depth, pixel, 0).r);
    if (!(abs(z - clip.w) <= clip.w * MATCH_SHARE + MATCH_SLACK)) {
        return;
    }
    vec3 c = finite_colour(texelFetch(u_source, pixel, 0).rgb);
    float known = min(unpackHalf2x16(lights[index].y).y + LEARN, 1.0);
    lights[index] = uvec2(packHalf2x16(c.rg), packHalf2x16(vec2(c.b, known)));
}
)";

constexpr const char* kItemVertexBody = R"(
layout(std430, set = 0, binding = 3) readonly buffer Meshes { vec4 meshes[]; };
layout(location = 0) out vec3 v_colour;
#ifdef MOTION
layout(location = 1) out vec4 v_now;
layout(location = 2) out vec4 v_before;
#endif
out gl_PerVertex { vec4 gl_Position; };

vec3 own_colour(uint kind, float h, float part)
{
    if (kind == KIND_LEAF) {
        vec3 a = h < 0.25 ? vec3(0.55, 0.30, 0.08) : (h < 0.5 ? vec3(0.62, 0.48, 0.12)
                                                     : (h < 0.75 ? vec3(0.36, 0.22, 0.10) : vec3(0.32, 0.36, 0.12)));
        return a * (0.85 + 0.3 * fract(h * 7.0));
    }
    if (kind == KIND_PAPER) {
        return mix(vec3(0.82, 0.81, 0.77), vec3(0.72, 0.68, 0.58), fract(h * 3.0));
    }
    if (kind == KIND_CAN) {
        if (part < 0.5) {
            return vec3(0.6, 0.6, 0.62);
        }
        return h < 0.3 ? vec3(0.62, 0.08, 0.06) : (h < 0.55 ? vec3(0.1, 0.2, 0.55)
                                                  : (h < 0.8 ? vec3(0.1, 0.4, 0.15) : vec3(0.7, 0.7, 0.72)));
    }
    return mix(vec3(0.18, 0.34, 0.08), vec3(0.32, 0.42, 0.12), h); // a weed
}

void main()
{
    uint index = uint(gl_InstanceIndex);
    vec4 a = items[index * 2u];
    vec4 b = items[index * 2u + 1u];
    uint packed = floatBitsToUint(a.w);
    float rank = (float(packed & 255u) + 0.5) / 256.0;
    uint kind = (packed >> 8u) & 15u;
    float variant = float((packed >> 12u) & 15u) / 15.0;
    float turn = float((packed >> 16u) & 255u) / 256.0 * TAU;
    float h = float((packed >> 24u) & 255u) / 255.0;
    vec3 root = a.xyz;
    uvec2 light = lights[index];
    vec2 rg = unpackHalf2x16(light.x);
    vec2 bk = unpackHalf2x16(light.y);
    float d = distance(root, p.camera.xyz);
    float grow = clamp((keep_at(d) - rank) / GROW_BAND, 0.0, 1.0);
    float scale = b.x * fade_at(d) * clamp(bk.y, 0.0, 1.0) * grow;
    uint vertex = min(kind, 4u) * MESH_VERTICES + uint(gl_VertexIndex);
    vec4 m = meshes[vertex * 2u];
    vec4 n = meshes[vertex * 2u + 1u];
    vec3 local = m.xyz;
    if (kind == KIND_PEBBLE) {
        local *= vec3(0.75 + 0.5 * variant, 0.7 + 0.6 * h, 0.75 + 0.5 * fract(variant * 5.3));
    }
    // its tilt (about its own x), then its turn (about y)
    float ct = cos(b.y);
    float st = sin(b.y);
    local = vec3(local.x, local.y * ct - local.z * st, local.y * st + local.z * ct);
    float c = cos(turn);
    float s = sin(turn);
    vec3 turned = vec3(local.x * c - local.z * s, local.y, local.x * s + local.z * c);
    vec3 position = root + turned * scale;
    if (kind == KIND_LEAF || kind == KIND_WEED) {
        float phase = root.x * 0.0047 + root.z * 0.0031 + h * TAU;
        float wave = sin(p.camera.w * 1.9 + phase) * 0.6 + sin(p.camera.w * 0.73 + phase * 2.3 + 1.7) * 0.4;
        float reach = kind == KIND_WEED ? 0.18 : 0.06;
        position.xz += vec2(0.8, 0.6) * (reach * scale * max(local.y, 0.0) * (0.5 + 0.5 * wave));
        position.y += kind == KIND_LEAF ? 0.04 * scale * wave * abs(local.x) : 0.0;
    }
    vec4 clip = p.vp * vec4(position, 1.0);
    gl_Position = scale > 0.0 ? vec4(clip.x + clip.w * p.offsets.x, -clip.y + clip.w * p.offsets.y, clip.z, clip.w)
                              : vec4(0.0);
    vec3 ground = finite_colour(vec3(rg, bk.x));
    float shade = m.w * (0.7 + 0.3 * clamp(n.y, 0.0, 1.0));
    if (kind == KIND_PEBBLE) {
        v_colour = ground * (0.8 + 0.4 * h) * shade;
    } else {
        float lit = clamp(dot(ground, vec3(0.2126, 0.7152, 0.0722)) / GROUND_ALBEDO, 0.0, 4.0);
        v_colour = own_colour(kind, h, n.w) * lit * shade;
    }
#ifdef MOTION
    v_now = vec4(clip.xy - p.offsets.zw * clip.w, 0.0, clip.w);
    v_before = p.vp_before * vec4(position, 1.0);
#endif
}
)";

constexpr const char* kItemFragmentBody = R"(
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
    std::array<float, 4> rect{};
    std::array<float, 4> offsets{};
    std::array<float, 4> depth_projection{};
    std::array<float, 4> depth_map{};
};
constexpr std::size_t kParamsBytes = 224;
static_assert(sizeof(ParamsBlock) == kParamsBytes, "the shaders' Params block");

struct RangeConstants {
    std::array<uint32_t, 4> range{};
};

struct DrawPipeline {
    bool decals = false;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool motion = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct ScatterState {
    ScatterSettings settings;
    std::shared_ptr<const scatter::Scatter> scatter;  // given by render_pc (or the check)
    std::shared_ptr<const scatter::Scatter> uploaded; // the one in the buffers
    DeviceBuffer decal_vertices;
    DeviceBuffer items;
    DeviceBuffer lights;
    DeviceBuffer meshes;
    Image atlas;
    Image source; // the viewport's colour copied out (the clutter's gather)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> gather{}; // single-sampled depth, multisampled
    VkShaderModule decal_vertex = VK_NULL_HANDLE;
    std::array<VkShaderModule, 2> decal_fragment{};     // single-sampled depth, multisampled
    VkShaderModule decal_fragment_wet = VK_NULL_HANDLE; // single-sampled, with the wet mask (WET)
    Image wet;                                          // the puddles' wet mask (R8, the colour's size)
    uint64_t wet_serial = 0;                            // the frame it was drawn in ...
    std::array<uint32_t, 4> wet_rect{};                 // ... and for which viewport
    uint64_t wet_drawn = 0;
    std::array<VkShaderModule, 2> item_vertex{}; // without, with motion
    std::array<VkShaderModule, 2> item_fragment{};
    std::vector<DrawPipeline> draws;
    bool failed = false;
    uint64_t decal_passes = 0;
    uint64_t clutter_passes = 0;
    uint32_t last_vertices = 0;
    uint32_t last_items = 0;
};

ScatterState& scatter_state()
{
    static ScatterState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class DecalListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kDecals; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_decal_pass(state, view);
        }
    }
};

class ClutterListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kClutter; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_clutter_pass(state, view);
        }
    }
};

DecalListener& decal_listener()
{
    static DecalListener instance;
    return instance;
}

ClutterListener& clutter_listener()
{
    static ClutterListener instance;
    return instance;
}

class ScatterSink final : public scatter::Sink {
public:
    [[nodiscard]] float wanted_decals() const override
    {
        const std::scoped_lock lock(backend().mutex);
        const ScatterSettings& settings = scatter_state().settings;
        return settings.decals ? settings.decal_density : 0.0F;
    }
    [[nodiscard]] float wanted_clutter() const override
    {
        const std::scoped_lock lock(backend().mutex);
        const ScatterSettings& settings = scatter_state().settings;
        return settings.clutter ? settings.clutter_density : 0.0F;
    }
    void set_scatter(std::shared_ptr<const scatter::Scatter> made) override
    {
        const std::scoped_lock lock(backend().mutex);
        scatter_state().scatter = std::move(made);
    }
};

ScatterSink& sink_instance()
{
    static ScatterSink instance;
    return instance;
}

float smoothstep(float low, float high, float value)
{
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (kThree - (kTwo * along));
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_scatter_settings(const ScatterSettings& settings)
{
    ScatterSettings clean = settings;
    const auto finite_or = [](float value, float fallback, float low, float high) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    constexpr float kLeastDensity = 0.25F;
    constexpr float kMostDensity = 4.0F;
    clean.decal_density = finite_or(settings.decal_density, 1.0F, kLeastDensity, kMostDensity);
    clean.clutter_density = finite_or(settings.clutter_density, 1.0F, kLeastDensity, kMostDensity);
    {
        const std::scoped_lock lock(backend().mutex);
        ScatterState& own = scatter_state();
        own.settings = clean;
        if (!clean.decals && !clean.clutter) {
            own.scatter.reset(); // (the buffers go at the next pass or the shutdown)
        }
    }
    if (clean.decals) {
        rt::add_view_listener(&decal_listener());
    } else {
        rt::remove_view_listener(&decal_listener());
    }
    if (clean.clutter) {
        rt::add_view_listener(&clutter_listener());
    } else {
        rt::remove_view_listener(&clutter_listener());
    }
    scatter::set_sink(clean.decals || clean.clutter ? &sink_instance() : nullptr);
}

ScatterSettings scatter_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return scatter_state().settings;
}

scatter::Sink& scatter_sink() { return sink_instance(); }

ScatterView scatter_view(const rt::ViewDrawn& view)
{
    ScatterView made;
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
    made.seconds = static_cast<float>(std::fmod(view.game_seconds, kWindClockWraps));
    return made;
}

float clutter_keep(const ScatterView& view, float distance)
{
    const float full = kClutterDistance * kClutterNearShare;
    const float keep = distance <= full ? 1.0F : std::pow(full / distance, kClutterKeepPower);
    return keep * view.keep_scale;
}

float clutter_fade(float distance)
{
    return 1.0F - smoothstep(kClutterFadeStart * kClutterDistance, kClutterDistance, distance);
}

float decal_fade(const scatter::DecalVertex& vertex, const std::array<float, 3>& camera)
{
    constexpr uint32_t kFadeShift = 16;
    const float own = std::min(static_cast<float>((vertex.packed >> kFadeShift) & kByte) *
                                   static_cast<float>(scatter::kDecalFadeUnits),
                               kDecalDistance);
    float sum = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float apart = core::at(vertex.position, axis) - core::at(camera, axis);
        sum += apart * apart;
    }
    return 1.0F - smoothstep(kDecalFadeStart * own, own, std::sqrt(sum));
}

namespace {

using Vec3 = std::array<float, 3>;

// The tile's box (grown by `out`) outside the view: every corner beyond one plane.
bool outside_view(const scatter::Tile& tile, const ScatterView& view, float out)
{
    const Vec3 low{core::at(tile.low, 0) - out, core::at(tile.low, 1) - out, core::at(tile.low, 2) - out};
    const Vec3 high{core::at(tile.high, 0) + out, core::at(tile.high, 1) + out, core::at(tile.high, 2) + out};
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

float distance_to_box(const scatter::Tile& tile, const Vec3& point)
{
    float sum = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float nearest = std::clamp(core::at(point, axis), core::at(tile.low, axis), core::at(tile.high, axis));
        const float apart = core::at(point, axis) - nearest;
        sum += apart * apart;
    }
    return std::sqrt(sum);
}

uint32_t kept_in_tile(const scatter::Scatter& made, const scatter::Tile& tile, float keep)
{
    const float limit = std::min(keep, 1.0F) * kRanks;
    uint32_t low = 0;
    uint32_t high = tile.item_count;
    while (low < high) {
        const uint32_t middle = low + ((high - low) / 2);
        const auto rank = static_cast<float>(core::at(made.items, tile.first_item + middle).packed & kByte);
        if (rank + kHalf < limit) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

void add_draw(std::vector<ScatterDraw>* draws, bool* last_whole, uint32_t first, uint32_t count, bool whole)
{
    if (*last_whole && !draws->empty() && draws->back().first + draws->back().count == first) {
        draws->back().count += count;
    } else {
        draws->push_back({.first = first, .count = count});
    }
    *last_whole = whole;
}

std::vector<ScatterDraw> clutter_draws_at(const scatter::Scatter& made, const ScatterView& view, uint32_t* items)
{
    std::vector<ScatterDraw> draws;
    *items = 0;
    bool last_whole = false;
    for (const scatter::Tile& tile : made.tiles) {
        const float closest = distance_to_box(tile, view.camera);
        if (tile.item_count == 0 || closest >= kClutterDistance || outside_view(tile, view, kItemReach)) {
            last_whole = false;
            continue;
        }
        const uint32_t count = kept_in_tile(made, tile, clutter_keep(view, closest));
        if (count == 0) {
            last_whole = false;
            continue;
        }
        *items += count;
        add_draw(&draws, &last_whole, tile.first_item, count, count == tile.item_count);
    }
    return draws;
}

} // namespace

std::vector<ScatterDraw> plan_decal_draws(const scatter::Scatter& made, const ScatterView& view, uint32_t* vertices)
{
    std::vector<ScatterDraw> draws;
    *vertices = 0;
    bool last_whole = false;
    for (const scatter::Tile& tile : made.tiles) {
        if (tile.vertex_count == 0 || distance_to_box(tile, view.camera) >= kDecalDistance ||
            outside_view(tile, view, 1.0F)) {
            last_whole = false;
            continue;
        }
        *vertices += tile.vertex_count;
        add_draw(&draws, &last_whole, tile.first_vertex, tile.vertex_count, true);
    }
    return draws;
}

std::vector<ScatterDraw> plan_clutter_draws(const scatter::Scatter& made, ScatterView* view, uint32_t* items)
{
    view->keep_scale = 1.0F;
    std::vector<ScatterDraw> draws = clutter_draws_at(made, *view, items);
    if (*items > kClutterMostDrawn) {
        view->keep_scale = static_cast<float>(kClutterMostDrawn) / static_cast<float>(*items);
        draws = clutter_draws_at(made, *view, items);
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

std::string source_of(const char* body, bool items, bool writes_lights, const char* define)
{
    std::string source = "#version 460\n";
    if (define != nullptr) {
        source += "#define ";
        source += define;
        source += '\n';
    }
    source += writes_lights ? "#define LIGHTS_ACCESS\n" : "#define LIGHTS_ACCESS readonly\n";
    source += constants_source();
    source += kCommon;
    if (items) {
        source += kItemCommon;
    }
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
                                           .basePipelineIndex = 0};
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
    ScatterState& own = scatter_state();
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (binding == kBindingParams) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingAtlas || binding == kBindingSource || binding == kBindingDepth) {
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
    VkSamplerCreateInfo nearest{};
    nearest.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    nearest.magFilter = VK_FILTER_NEAREST;
    nearest.minFilter = VK_FILTER_NEAREST;
    nearest.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    nearest.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest.maxLod = 0.0F;
    VkSamplerCreateInfo linear = nearest;
    linear.magFilter = VK_FILTER_LINEAR;
    linear.minFilter = VK_FILTER_LINEAR;
    linear.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    linear.maxLod = static_cast<float>(std::bit_width(scatter::kDecalTile) - 1); // (a cell is one texel at the last)
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreateSampler(device, &nearest, nullptr, &own.nearest) != VK_SUCCESS ||
        vkCreateSampler(device, &linear, nullptr, &own.linear) != VK_SUCCESS) {
        *error = "the descriptor layout or the samplers";
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

// The layouts, the samplers, the compute pipelines and the draws' shader modules (once).
bool ensure_objects(Backend& state, std::string* error)
{
    ScatterState& own = scatter_state();
    if (core::at(own.item_fragment, 1) != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    for (std::size_t variant = 0; variant < 2; ++variant) {
        const char* samples = variant == 1 ? "MULTISAMPLED" : nullptr;
        if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
            core::at(own.gather, variant) =
                make_compute(device, own.layout, source_of(kGatherBody, true, true, samples), error);
            if (core::at(own.gather, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
        if (core::at(own.decal_fragment, variant) == VK_NULL_HANDLE) {
            core::at(own.decal_fragment, variant) = make_module(
                device, RtShaderStage::kFragment, source_of(kDecalFragmentBody, false, false, samples), error);
            if (core::at(own.decal_fragment, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    if (own.decal_fragment_wet == VK_NULL_HANDLE) {
        own.decal_fragment_wet =
            make_module(device, RtShaderStage::kFragment, source_of(kDecalFragmentBody, false, false, "WET"), error);
        if (own.decal_fragment_wet == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.decal_vertex == VK_NULL_HANDLE) {
        own.decal_vertex =
            make_module(device, RtShaderStage::kVertex, source_of(kDecalVertexBody, false, false, nullptr), error);
        if (own.decal_vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t variant = 0; variant < 2; ++variant) {
        const char* motion = variant == 1 ? "MOTION" : nullptr;
        if (core::at(own.item_vertex, variant) == VK_NULL_HANDLE) {
            core::at(own.item_vertex, variant) =
                make_module(device, RtShaderStage::kVertex, source_of(kItemVertexBody, true, false, motion), error);
        }
        if (core::at(own.item_fragment, variant) == VK_NULL_HANDLE) {
            core::at(own.item_fragment, variant) = make_module(
                device, RtShaderStage::kFragment, source_of(kItemFragmentBody, false, false, motion), error);
        }
        if (core::at(own.item_vertex, variant) == VK_NULL_HANDLE ||
            core::at(own.item_fragment, variant) == VK_NULL_HANDLE) {
            return false;
        }
    }
    return true;
}

// A draw's pipeline (made on first use). Decals: triangle lists, no depth attachment, the colour multiplied (its rgb;
// alpha kept). Clutter: triangle lists, no culling, the depth tested and written, the colour's rgb written (and the
// motion).
VkPipeline draw_pipeline(Backend& state, const DrawPipeline& wanted, std::string* error)
{
    ScatterState& own = scatter_state();
    for (const DrawPipeline& made : own.draws) {
        if (made.decals == wanted.decals && made.format == wanted.format && made.depth == wanted.depth &&
            made.samples == wanted.samples && made.motion == wanted.motion) {
            return made.pipeline;
        }
    }
    const bool multisampled = wanted.samples != VK_SAMPLE_COUNT_1_BIT;
    const std::size_t variant = wanted.motion ? 1 : 0;
    VkShaderModule fragment = core::at(own.item_fragment, variant);
    if (wanted.decals) {
        fragment = wanted.motion ? own.decal_fragment_wet : core::at(own.decal_fragment, multisampled ? 1 : 0);
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = wanted.decals ? own.decal_vertex : core::at(own.item_vertex, variant),
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
    depth.depthTestEnable = wanted.decals ? VK_FALSE : VK_TRUE;
    depth.depthWriteEnable = wanted.decals ? VK_FALSE : VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
    core::at(blend, 0).colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    if (wanted.decals) {
        VkPipelineColorBlendAttachmentState& multiply = core::at(blend, 0);
        multiply.blendEnable = VK_TRUE;
        multiply.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        multiply.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        multiply.colorBlendOp = VK_BLEND_OP_ADD;
        multiply.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        multiply.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        multiply.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    core::at(blend, 1).colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (wanted.decals) {
        // (the wet mask: the most of any wet patch)
        VkPipelineColorBlendAttachmentState& most = core::at(blend, 1);
        most.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        most.blendEnable = VK_TRUE;
        most.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        most.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        most.colorBlendOp = VK_BLEND_OP_MAX;
        most.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        most.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        most.alphaBlendOp = VK_BLEND_OP_MAX;
    }
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = wanted.motion ? 2U : 1U;
    blending.pAttachments = blend.data();
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    const std::array<VkFormat, 2> formats{wanted.format, wanted.decals ? kWetFormat : kMotionFormat};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = wanted.motion ? 2U : 1U;
    rendering.pColorAttachmentFormats = formats.data();
    rendering.depthAttachmentFormat = wanted.decals ? VK_FORMAT_UNDEFINED : wanted.depth;
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
    ScatterState& own = scatter_state();
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

// The wet mask's image for a colour target's size (made again when it changes).
bool ensure_wet(Backend& state, const Image& colour)
{
    ScatterState& own = scatter_state();
    Image& wet = own.wet;
    if (wet.image != VK_NULL_HANDLE && wet.extent.width == colour.extent.width &&
        wet.extent.height == colour.extent.height) {
        return true;
    }
    retire_image(state, &wet);
    const ImageRequest request{.format = kWetFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = colour.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // (the check reads it back)
                               .alpha_one = false};
    if (!state.memory.create_image(request, &wet)) {
        return false;
    }
    wet.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prepare_image(state, &wet);
    return true;
}

// The next mip level (RGBA8 rows): each channel the mean of the 2 x 2 above it.
std::vector<uint8_t> next_level(std::span<const uint8_t> texels, const Extent& size)
{
    const uint32_t width = size.width;
    const uint32_t height = size.height;
    const uint32_t down_w = std::max(1U, width / 2);
    const uint32_t down_h = std::max(1U, height / 2);
    std::vector<uint8_t> out(static_cast<std::size_t>(down_w) * down_h * kTexel);
    for (uint32_t row = 0; row < down_h; ++row) {
        for (uint32_t column = 0; column < down_w; ++column) {
            for (std::size_t channel = 0; channel < kTexel; ++channel) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const uint32_t sx = std::min((column * 2) + dx, width - 1);
                        const uint32_t sy = std::min((row * 2) + dy, height - 1);
                        sum += core::at(texels, (((static_cast<std::size_t>(sy) * width) + sx) * kTexel) + channel);
                    }
                }
                core::at(out, (((static_cast<std::size_t>(row) * down_w) + column) * kTexel) + channel) =
                    static_cast<uint8_t>((sum + 2U) / 4U);
            }
        }
    }
    return out;
}

// The decals' atlas made and put on the device (once; its mips by halving, down to a cell a texel).
bool ensure_atlas(Backend& state)
{
    ScatterState& own = scatter_state();
    if (own.atlas.image != VK_NULL_HANDLE) {
        return true;
    }
    const auto levels = static_cast<uint32_t>(std::bit_width(scatter::kDecalTile));
    const ImageRequest request{.format = VK_FORMAT_R8G8B8A8_UNORM,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = scatter::kAtlasWidth, .height = scatter::kAtlasHeight},
                               .levels = levels,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.atlas)) {
        return false;
    }
    std::vector<uint8_t> all;
    std::vector<VkBufferImageCopy> regions;
    std::vector<uint8_t> level = scatter::make_decal_atlas();
    uint32_t width = scatter::kAtlasWidth;
    uint32_t height = scatter::kAtlasHeight;
    for (uint32_t index = 0; index < levels; ++index) {
        regions.push_back({.bufferOffset = all.size(),
                           .bufferRowLength = 0,
                           .bufferImageHeight = 0,
                           .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                .mipLevel = index,
                                                .baseArrayLayer = 0,
                                                .layerCount = 1},
                           .imageOffset = {.x = 0, .y = 0, .z = 0},
                           .imageExtent = {.width = width, .height = height, .depth = 1}});
        all.insert(all.end(), level.begin(), level.end());
        if (index + 1 < levels) {
            level = next_level(level, {.width = width, .height = height});
            width = std::max(1U, width / 2);
            height = std::max(1U, height / 2);
        }
    }
    const std::optional<RingSpan> staged = state.staging.copy(all, kTexel);
    if (!staged) {
        state.memory.destroy_image(&own.atlas);
        return false;
    }
    for (VkBufferImageCopy& region : regions) {
        region.bufferOffset += staged->offset;
    }
    VkCommandBuffer commands = state.frames.uploads();
    Memory::transition(commands, &own.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(commands, staged->buffer, own.atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    Memory::transition(commands, &own.atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    own.atlas.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    get_log("vulkan: decals: the atlas made (%ux%u, %u levels, %.1f MB)", scatter::kAtlasWidth, scatter::kAtlasHeight,
            levels, static_cast<double>(all.size()) / kBytesPerMegabyte);
    return true;
}

// A buffer of `bytes` from memory into the device (the uploads), or false.
bool upload_buffer(Backend& state, std::span<const uint8_t> bytes, DeviceBuffer* buffer, bool readable)
{
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (readable) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (!state.memory.create_buffer(bytes.size(), usage, BufferPlace::kDevice, buffer)) {
        return false;
    }
    const std::optional<RingSpan> staged = state.staging.copy(bytes, sizeof(float));
    if (!staged) {
        state.memory.destroy_buffer(buffer);
        return false;
    }
    const VkBufferCopy region{.srcOffset = staged->offset, .dstOffset = 0, .size = bytes.size()};
    vkCmdCopyBuffer(state.frames.uploads(), staged->buffer, buffer->buffer, 1, &region);
    return true;
}

template <typename T>
std::span<const uint8_t> bytes_of(const std::vector<T>& values)
{
    const void* const data = values.data();
    return {static_cast<const uint8_t*>(data), values.size() * sizeof(T)};
}

// The scatter's decal vertices and items into buffers of the device's, the items' lights cleared (none known) - once
// a scatter; the old buffers retired. The meshes once.
bool upload_scatter(Backend& state)
{
    ScatterState& own = scatter_state();
    if (own.uploaded == own.scatter) {
        return own.scatter != nullptr;
    }
    retire_buffer(state, &own.decal_vertices);
    retire_buffer(state, &own.items);
    retire_buffer(state, &own.lights);
    own.uploaded = own.scatter;
    if (own.scatter == nullptr) {
        return false;
    }
    const scatter::Scatter& made = *own.scatter;
    bool ok = true;
    if (own.meshes.buffer == VK_NULL_HANDLE) {
        ok = upload_buffer(state, bytes_of(scatter::make_meshes()), &own.meshes, false);
    }
    if (ok && !made.decal_vertices.empty()) {
        ok = upload_buffer(state, bytes_of(made.decal_vertices), &own.decal_vertices, false);
    }
    if (ok && !made.items.empty()) {
        ok = upload_buffer(state, bytes_of(made.items), &own.items, false) &&
             state.memory.create_buffer(made.items.size() * kLightBytes,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        BufferPlace::kDevice, &own.lights);
        if (ok) {
            vkCmdFillBuffer(state.frames.uploads(), own.lights.buffer, 0, VK_WHOLE_SIZE, 0);
        }
    }
    if (!ok) {
        if (first_report(state, kReportUpload)) {
            get_log("vulkan: scatter: the decals and clutter couldn't be put on the device - none");
        }
        state.memory.destroy_buffer(&own.decal_vertices);
        state.memory.destroy_buffer(&own.items);
        state.memory.destroy_buffer(&own.lights);
        own.uploaded.reset();
        own.scatter.reset();
        return false;
    }
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(state.frames.uploads(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
    get_log("vulkan: scatter: %zu decals (%zu vertices) and %zu clutter items in %zu tiles on the device (%.1f MB)",
            made.decals.size(), made.decal_vertices.size(), made.items.size(), made.tiles.size(),
            static_cast<double>((made.decal_vertices.size() * sizeof(scatter::DecalVertex)) +
                                (made.items.size() * (sizeof(scatter::Item) + kLightBytes))) /
                kBytesPerMegabyte);
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
    Image* depth = nullptr;
    bool motion = false;
    std::array<uint32_t, 4> rect{};
    ScatterView shape;
    std::vector<ScatterDraw> draws;
    uint32_t count = 0;
};

ParamsBlock params_for(const Backend& state, const ViewPlan& plan, const rt::ViewDrawn& view)
{
    ParamsBlock block{};
    block.vp = flat(plan.shape.view_projection);
    block.vp_before = block.vp;
    const MotionVectors& motion = state.motion;
    if (plan.motion && motion.before.known) {
        block.vp_before = motion_multiply(motion.before.view, motion.before.projection);
    }
    block.camera = {core::at(plan.shape.camera, 0), core::at(plan.shape.camera, 1), core::at(plan.shape.camera, 2),
                    plan.shape.seconds};
    block.lod = {kClutterDistance, kClutterDistance * kClutterNearShare, plan.shape.keep_scale, kDecalDistance};
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
    block.depth_map = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, 0.0F, 0.0F};
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

// The bindings pushed for the decals' draw or the clutter's gather and draw.
void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const VkDescriptorBufferInfo& params,
                      bool decals, const Image* depth)
{
    const ScatterState& own = scatter_state();
    const VkDescriptorBufferInfo decal_vertices{
        .buffer = own.decal_vertices.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo items{.buffer = own.items.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo lights{.buffer = own.lights.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo meshes{.buffer = own.meshes.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorImageInfo atlas{
        .sampler = own.linear, .imageView = own.atlas.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo source{
        .sampler = own.nearest, .imageView = own.source.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo depth_info{.sampler = own.nearest,
                                           .imageView = depth != nullptr ? depth->view : VK_NULL_HANDLE,
                                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kBindingCount);
    const auto add = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                         const VkDescriptorImageInfo* image) {
        VkWriteDescriptorSet& write = writes.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = buffer;
        write.pImageInfo = image;
    };
    add(kBindingParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &params, nullptr);
    if (decals) {
        add(kBindingDecals, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &decal_vertices, nullptr);
        add(kBindingAtlas, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &atlas);
    } else {
        add(kBindingItems, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &items, nullptr);
        add(kBindingLights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lights, nullptr);
        add(kBindingMeshes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshes, nullptr);
        if (point == VK_PIPELINE_BIND_POINT_COMPUTE) {
            add(kBindingSource, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &source);
        }
    }
    if (depth != nullptr) {
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &depth_info);
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
}

void push_range(VkCommandBuffer commands, uint32_t first, uint32_t count)
{
    const RangeConstants constants{.range = {first, count, 0, 0}};
    vkCmdPushConstants(commands, scatter_state().layout, kStages, 0, sizeof(constants), &constants);
}

void memory_barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags written, VkPipelineStageFlags to,
                    VkAccessFlags read)
{
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = written, .dstAccessMask = read};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// The depth moved to be read by `reader` (from what wrote it), or back to be drawn into (after `reader` read it).
void depth_barrier(VkCommandBuffer commands, Image* depth, bool to_read, VkPipelineStageFlags reader)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout layout =
        to_read ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_read ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_read ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(commands, to_read ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : reader, to_read ? reader : tests, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// The viewport's colour copied out (resolved when multisampled) into the source image.
void copy_colour(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& rect)
{
    ScatterState& own = scatter_state();
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
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // (the transition's reader is the fragment stage: the gather reads it in a compute pass)
    memory_barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

// The view's targets and rectangle; false (logged once) when they can't take the pass.
bool plan_targets(Backend& state, const rt::ViewDrawn& view, ViewPlan* plan)
{
    Surface* const colour_surface = core::at(state.targets, 0);
    Image* const colour = image_of_surface(colour_surface);
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 ||
        depth == nullptr || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        depth->attachment_view == VK_NULL_HANDLE || depth->samples != colour->samples) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: scatter: a view drawn into targets that can't take it (no depth, or not readable) - none "
                    "there");
        }
        return false;
    }
    if ((depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoDepth)) {
            get_log("vulkan: scatter: the depth can't be read - no decals or clutter");
        }
        return false;
    }
    plan->colour = colour;
    plan->depth = depth;
    const Extent area{.width = std::min(colour->extent.width, depth->extent.width),
                      .height = std::min(colour->extent.height, depth->extent.height)};
    plan->rect = clip_rect(view.rect, area);
    if (core::at(plan->rect, 2) == 0 || core::at(plan->rect, 3) == 0) {
        return false;
    }
    plan->shape = scatter_view(view);
    return true;
}

void begin_rendering(VkCommandBuffer commands, Backend& state, const ViewPlan& plan, bool with_depth,
                     const rt::ViewDrawn& view, Image* wet = nullptr)
{
    Image* const colour = plan.colour;
    Image* const depth = plan.depth;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (with_depth && depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
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
    if (wet != nullptr) {
        // (the puddles' wet mask: cleared over the viewport, then the most of any wet patch)
        Memory::transition(commands, wet, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo& wet_info = core::at(colours, 1);
        wet_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        wet_info.imageView = wet->attachment_view;
        wet_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        wet_info.resolveMode = VK_RESOLVE_MODE_NONE;
        wet_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        wet_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        wet_info.clearValue = {};
    } else if (plan.motion) {
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
    info.colorAttachmentCount = plan.motion || wet != nullptr ? 2U : 1U;
    info.pColorAttachments = colours.data();
    info.pDepthAttachment = with_depth ? &depth_info : nullptr;
    vkCmdBeginRendering(commands, &info);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = std::clamp(core::at(view.depth_range, 0), 0.0F, 1.0F),
                              .maxDepth = std::clamp(core::at(view.depth_range, 1), 0.0F, 1.0F)};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
}

bool pipeline_for(Backend& state, const ViewPlan& plan, bool decals, VkPipeline* pipeline)
{
    ScatterState& own = scatter_state();
    std::string error;
    *pipeline = ensure_objects(state, &error) ? draw_pipeline(state,
                                                              {.decals = decals,
                                                               .format = plan.colour->format,
                                                               .depth = plan.depth->format,
                                                               .samples = plan.colour->samples,
                                                               .motion = plan.motion},
                                                              &error)
                                              : VK_NULL_HANDLE;
    bool made = *pipeline != VK_NULL_HANDLE;
    if (made && decals) {
        made = ensure_atlas(state);
    }
    if (made && !decals) {
        made = ensure_source(state, *plan.colour);
    }
    if (!made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: scatter: the pass couldn't be made (%s) - no decals or clutter",
                    error.empty() ? "its images" : error.c_str());
        }
    }
    return made;
}

bool params_in_ring(Backend& state, const ViewPlan& plan, const rt::ViewDrawn& view, VkDescriptorBufferInfo* out)
{
    const std::optional<VkDescriptorBufferInfo> params = block_in_ring(state, params_for(state, plan, view));
    if (!params) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: scatter: no room in the ring for the pass's block - skipped");
        }
        return false;
    }
    *out = *params;
    return true;
}

} // namespace

bool record_decal_pass(Backend& state, const rt::ViewDrawn& view)
{
    ScatterState& own = scatter_state();
    if (!own.settings.decals || own.failed || !upload_scatter(state) || own.scatter->decal_vertices.empty()) {
        return false;
    }
    ViewPlan plan;
    if (!plan_targets(state, view, &plan)) {
        return false;
    }
    plan.draws = plan_decal_draws(*own.scatter, plan.shape, &plan.count);
    // (the puddles' wet mask, for the reflections: single-sampled targets, wet patches placed)
    plan.motion = rt_reflections_active(state) && plan.colour->samples == VK_SAMPLE_COUNT_1_BIT &&
                  core::at(own.scatter->decals_of, static_cast<std::size_t>(scatter::DecalKind::kWet)) > 0 &&
                  ensure_wet(state, *plan.colour);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorBufferInfo params{};
    if (plan.draws.empty() || !pipeline_for(state, plan, true, &pipeline) ||
        !params_in_ring(state, plan, view, &params)) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    depth_barrier(commands, plan.depth, true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    ViewPlan drawn = plan;
    drawn.motion = false; // (the second attachment is the wet mask's, not the motion's)
    begin_rendering(commands, state, drawn, false, view, plan.motion ? &own.wet : nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, params, true, plan.depth);
    for (const ScatterDraw& draw : plan.draws) {
        vkCmdDraw(commands, draw.count, 1, draw.first, 0);
    }
    vkCmdEndRendering(commands);
    depth_barrier(commands, plan.depth, false, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    const uint64_t serial = state.frames.serial();
    if (plan.motion) {
        Memory::transition(commands, &own.wet, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        // (the reflections' trace reads it in a compute pass)
        memory_barrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        own.wet.used = serial;
        own.wet_serial = serial;
        own.wet_rect = view.rect;
        ++own.wet_drawn;
    }
    plan.colour->used = serial;
    plan.depth->used = serial;
    own.atlas.used = serial;
    state.bound = BoundState{};
    own.last_vertices = plan.count;
    ++own.decal_passes;
    if (first_report(state, kReportFirstDecals)) {
        get_log("vulkan: decals: the first pass (%ux%u, %u sample%s; %u vertices in %zu draws)", core::at(plan.rect, 2),
                core::at(plan.rect, 3), static_cast<unsigned>(plan.colour->samples),
                plan.colour->samples == VK_SAMPLE_COUNT_1_BIT ? "" : "s", plan.count, plan.draws.size());
    }
    return true;
}

bool record_clutter_pass(Backend& state, const rt::ViewDrawn& view)
{
    ScatterState& own = scatter_state();
    if (!own.settings.clutter || own.failed || !upload_scatter(state) || own.scatter->items.empty()) {
        return false;
    }
    ViewPlan plan;
    if (!plan_targets(state, view, &plan)) {
        return false;
    }
    plan.motion =
        motion_wanted(state, core::at(state.targets, 0), plan.colour) && state.motion.image.image != VK_NULL_HANDLE;
    plan.draws = plan_clutter_draws(*own.scatter, &plan.shape, &plan.count);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorBufferInfo params{};
    if (plan.draws.empty() || !pipeline_for(state, plan, false, &pipeline) ||
        !params_in_ring(state, plan, view, &params)) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    copy_colour(commands, plan.colour, plan.rect);
    // the gather: the last pass's draws read the lights, the last gather wrote them
    memory_barrier(commands, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    depth_barrier(commands, plan.depth, true, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    const bool multisampled = plan.depth->samples != VK_SAMPLE_COUNT_1_BIT;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.gather, multisampled ? 1 : 0));
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, params, false, plan.depth);
    for (const ScatterDraw& draw : plan.draws) {
        push_range(commands, draw.first, draw.count);
        vkCmdDispatch(commands, (draw.count + kGroupSize - 1) / kGroupSize, 1, 1);
    }
    memory_barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    depth_barrier(commands, plan.depth, false, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    begin_rendering(commands, state, plan, true, view);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, params, false, nullptr);
    for (const ScatterDraw& draw : plan.draws) {
        vkCmdDraw(commands, scatter::kMeshVertices, draw.count, 0, draw.first);
    }
    vkCmdEndRendering(commands);
    const uint64_t serial = state.frames.serial();
    plan.colour->used = serial;
    plan.depth->used = serial;
    own.source.used = serial;
    state.bound = BoundState{};
    own.last_items = plan.count;
    ++own.clutter_passes;
    if (first_report(state, kReportFirstClutter)) {
        get_log("vulkan: clutter: the first pass (%ux%u, %u sample%s%s; %u items in %zu draws)", core::at(plan.rect, 2),
                core::at(plan.rect, 3), static_cast<unsigned>(plan.colour->samples),
                plan.colour->samples == VK_SAMPLE_COUNT_1_BIT ? "" : "s", plan.motion ? ", motion" : "", plan.count,
                plan.draws.size());
    }
    return true;
}

uint64_t decal_passes() { return scatter_state().decal_passes; }

const Image* scatter_wet_mask(const Backend& state, const std::array<uint32_t, 4>& rect)
{
    const ScatterState& own = scatter_state();
    if (own.wet.image == VK_NULL_HANDLE || own.wet_serial == 0 || own.wet_serial != state.frames.serial() ||
        own.wet_rect != rect || !own.settings.decals) {
        return nullptr;
    }
    return &own.wet;
}

uint64_t wet_masks_drawn() { return scatter_state().wet_drawn; }

bool read_wet_mask(Backend& state, std::vector<uint8_t>* texels, uint32_t* width)
{
    ScatterState& own = scatter_state();
    texels->clear();
    if (own.wet.image == VK_NULL_HANDLE || !finish_work(state)) {
        return false;
    }
    *width = own.wet.extent.width;
    texels->resize(static_cast<std::size_t>(own.wet.extent.width) * own.wet.extent.height); // (one byte a texel)
    return state.memory.read_image_level(&own.wet, 0, texels);
}
uint64_t clutter_passes() { return scatter_state().clutter_passes; }
uint32_t decal_last_vertices() { return scatter_state().last_vertices; }
uint32_t clutter_last_items() { return scatter_state().last_items; }

void release_scatter(Backend& state)
{
    ScatterState& own = scatter_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const DrawPipeline& made : own.draws) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline pipeline : own.gather) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (VkShaderModule module :
             {own.decal_vertex, core::at(own.decal_fragment, 0), core::at(own.decal_fragment, 1),
              own.decal_fragment_wet, core::at(own.item_vertex, 0), core::at(own.item_vertex, 1),
              core::at(own.item_fragment, 0), core::at(own.item_fragment, 1)}) {
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
        for (VkSampler sampler : {own.nearest, own.linear}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.source);
        state.memory.destroy_image(&own.atlas);
        state.memory.destroy_image(&own.wet);
        state.memory.destroy_buffer(&own.decal_vertices);
        state.memory.destroy_buffer(&own.items);
        state.memory.destroy_buffer(&own.lights);
        state.memory.destroy_buffer(&own.meshes);
    }
    own.draws.clear();
    own.gather = {};
    own.decal_vertex = VK_NULL_HANDLE;
    own.decal_fragment = {};
    own.decal_fragment_wet = VK_NULL_HANDLE;
    own.wet_serial = 0;
    own.item_vertex = {};
    own.item_fragment = {};
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.nearest = VK_NULL_HANDLE;
    own.linear = VK_NULL_HANDLE;
    own.uploaded.reset();
    own.failed = false;
}

} // namespace render::vulkan
