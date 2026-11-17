// NOT ORIGINAL - rain: the wet ground on the world's draws, the wet mask for the reflections, the drops and splashes
// (vulkan_rain.h; docs/design/vulkan.md "Rain").
#include "gpu_vulkan/vulkan_rain.h"

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
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/precip_occlusion.h"
#include "engine/gfx/rain.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_trail.h"
#include "engine/gfx/wind.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_precip.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_scatter.h"
#include "gpu_vulkan/vulkan_snow.h"
#include "gpu_vulkan/vulkan_surface_trail.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportTarget = 0x5241494E01U;
constexpr uint64_t kReportPipeline = 0x5241494E02U;
constexpr uint64_t kReportFirstAir = 0x5241494E03U;
constexpr uint64_t kReportFirstWet = 0x5241494E04U;
constexpr uint64_t kReportNoRoom = 0x5241494E05U;
constexpr uint64_t kReportFirstMask = 0x5241494E06U;
constexpr uint64_t kReportBuffer = 0x5241494E07U;

constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kBindingBlock = 0;  // the pass's block (uniform, in the ring)
constexpr uint32_t kBindingMap = 1;    // the precipitation occlusion map (usampler2D, GENERAL)
constexpr uint32_t kBindingEnv = 2;    // the frame blurred (a mip chain): what lies behind a drop
constexpr uint32_t kBindingDrops = 3;  // the drops and splashes placed and lit (storage)
constexpr uint32_t kBindingDepth = 4;  // the depth (the wet mask's)
constexpr uint32_t kBindingDecals = 5; // the decals' wet mask (the wet mask's)
constexpr uint32_t kBindingScene = 6;  // the frame's scene (the drops' rays toward the light; ray tracing only)
constexpr uint32_t kBindingCount = 7;
constexpr uint32_t kComputeGroup = 64;
constexpr uint32_t kStripVertices = 4;
constexpr std::size_t kDropBytes = 48; // three vec4 a drop
constexpr VkFormat kEnvFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;
constexpr double kClockWraps = 3600.0;
constexpr float kHalf = 0.5F;
constexpr float kMostStreak = 96.0F; // pixels (a near drop's streak)
constexpr uint32_t kTotalDrops = kRainNearDrops + kRainFarDrops;
constexpr uint32_t kTotalInstances = kTotalDrops + kRainSplashes;

// --- the shaders -----------------------------------------------------------------------------------------------------

// Every stage's block (std140; Block below byte for byte). The instance is `rain`: precip_reached and rain_wetness read it.
constexpr const char* kBlockSource = R"(
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 vp;               // D3D's row-vector matrix as stored: vp * v is its v x M
    mat4 vp_before;        // the frame before's, unjittered
    mat4 vp_inverse;
    vec4 camera;           // xyz; w the rain's clock (seconds, wrapped)
    vec4 drops;            // x Lambda (mm^-1); y the least diameter drawn (mm); z the share of the distribution drawn
                           // (1 - e^(-Lambda (most - least))); w the most diameter (mm)
    vec4 counts;           // x near drops, y far drops, z splashes, w the drops a near one stands for
    vec4 boxes;            // x, y the near box's side and height; z, w the far box's
    vec4 wind;             // xy downwind (x, z); z, w the drift (units) wrapped by the near and far boxes
    vec4 motion;           // x seconds since the frame before; y the drift since; z the drops a far one stands for; w a
                           // splash's
    vec4 light;            // xyz toward the sun or the moon; w 1 when it lights
    vec4 light_colour;     // rgb its light (the frame's unit, per unit of N.L); w the drops' phase's g
    vec4 lamps;            // x how many; y how much they're on; z 1 with the decals' wet mask bound; w unused
    vec4 rect;             // the viewport: x, y, width, height (pixels)
    vec4 offsets;          // x, y Direct3D 9's half pixel (1 / the size); z, w the jitter (clip space)
    vec4 lens;             // x the focal length (pixels); y the aperture (units); z the focus (units); w the least radius (px)
    vec4 env;              // x the blurred frame's level (< 0: none - the sky's light); y the shutter; z the most streak (px);
                           // w the motion share
    vec4 env_scale;        // xy 1 / the colour target's size; zw unused
    vec4 env_sky;          // rgb the light behind a drop with no blurred frame (the sky's, the unit)
    vec4 splash;           // x the reach (units); y a splash's life (s); z the least diameter splashing (mm); w the spread
    vec4 depth_projection; // P22, P32, P23, P33
    vec4 depth_map;        // x the depth range's minimum; y 1 / its span
    vec4 precip_fall;      // (vulkan_precip.h PrecipMap)
    vec4 precip_slant;
    vec4 precip_near;
    vec4 precip_far;
    vec4 ground;           // (rain.h Ground) x stored mm, y pores, z the puddles' level, w the film's rate (mm/h)
    vec4 ripple;           // (rain.h Ripples) x the cell (units), y the layers, z the clock, w the life
    vec4 lamp_at[MOST_LAMPS];     // xyz where; w its core
    vec4 lamp_colour[MOST_LAMPS]; // rgb its colour x intensity (the frame's unit); w its reach
} rain;
layout(set = 0, binding = 1) uniform usampler2D rain_map;
#define PRECIP_BLOCK rain
#define PRECIP_MAP rain_map
)";

constexpr const char* kCommonSource = R"(
struct Drop { vec4 now; vec4 before; vec4 colour; };
uint hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7FEB352Du;
    x ^= x >> 15u;
    x *= 0x846CA68Bu;
    x ^= x >> 16u;
    return x;
}
float unit(uint x) { return float(x >> 8u) / 16777216.0; }
vec4 to_target(vec4 clip)
{
    return vec4(clip.x + clip.w * rain.offsets.x, -clip.y + clip.w * rain.offsets.y, clip.z, clip.w);
}
)";

// The drops and splashes placed and lit, one invocation each (drop_place's twin for the places).
constexpr const char* kPlaceBody = R"(
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 3) buffer Drops { Drop drops[]; };
layout(set = 0, binding = 2) uniform sampler2D u_env;
#ifdef TRACED
layout(set = 0, binding = 6) uniform accelerationStructureEXT u_scene;
bool blocked(vec3 origin, vec3 direction, float reach)
{
    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0x07u, origin, 0.0,
                          direction, reach);
    while (rayQueryProceedEXT(q)) {
    }
    return rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
#endif
// Henyey-Greenstein x 4 pi (1 on average over the sphere)
float phase(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1.0e-4);
    return (1.0 - g * g) / (k * sqrt(k));
}
// what a drop shows of its surroundings: a water sphere is a wide lens - it refracts toward the eye what lies within about
// 82.5 degrees of the way through it (Garg and Nayar), the sky above the horizon and the scene below it by the share of
// that cone above the horizon (0.5 + 0.504 sin of its axis's elevation); the scene's part the frame blurred round the
// drop's place, the sky's the sky's light (its mean radiance: the unit's sky_light). Without the clock the scene alone.
vec3 behind(vec3 world, vec3 view)
{
    vec3 scene = rain.env_sky.rgb;
    vec4 clip = rain.vp * vec4(world, 1.0);
    if (rain.env.x >= 0.0 && clip.w > 1.0e-3) {
        vec2 ndc = clip.xy / clip.w;
        vec2 at = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * rain.rect.zw + rain.rect.xy;
        scene = textureLod(u_env, at * rain.env_scale.xy, rain.env.x).rgb;
    }
    float sky_share = rain.env_sky.w > 0.5 ? clamp(0.5 + 0.504 * view.y, 0.0, 1.0) : 0.0;
    return mix(scene, rain.env_sky.rgb, sky_share);
}
// a water sphere's light toward the eye (`view`: from the eye toward it): what it refracts from behind it, and the sun's
// or moon's and the lamps' light it scatters (its phase; a quarter of the light per unit of N.L: pi E p / 4 pi)
vec3 drop_light(vec3 world, vec3 view, bool traced)
{
    vec3 light = behind(world, view);
    float g = rain.light_colour.w;
    if (rain.light.w > 0.5) {
        float seen = 1.0;
#ifdef TRACED
        if (traced) seen = blocked(world + rain.light.xyz * 0.5, rain.light.xyz, SUN_REACH) ? 0.0 : 1.0;
#endif
        light += rain.light_colour.rgb * (0.25 * phase(dot(rain.light.xyz, view), g) * seen);
    }
    int lamps = int(rain.lamps.x);
    for (int i = 0; i < MOST_LAMPS; ++i) {
        if (i >= lamps) break;
        vec3 to = rain.lamp_at[i].xyz - world;
        float d = length(to);
        float reach = rain.lamp_colour[i].w;
        if (d >= reach) continue;
        float core = rain.lamp_at[i].w;
        float q = d / reach;
        float window = (1.0 - q * q * q * q);
        float falloff = core * core / (core * core + d * d) * window * window;
        light += rain.lamp_colour[i].rgb * (falloff * 0.25 * phase(dot(to / max(d, 1.0e-3), view), g) * rain.lamps.y);
    }
    return light;
}
float diameter_at(float u, float least)
{
    float l = rain.drops.x;
    float span = 1.0 - exp(-l * (rain.drops.w - least));
    return min(least - log(1.0 - u * span) / l, rain.drops.w);
}
void place_drop(uint i)
{
    bool near = i < uint(rain.counts.x);
    float box = near ? rain.boxes.x : rain.boxes.z;
    float height = near ? rain.boxes.y : rain.boxes.w;
    float drift = near ? rain.wind.z : rain.wind.w;
    float hx = unit(hash(i * 5u + 1u));
    float hy = unit(hash(i * 5u + 2u));
    float hz = unit(hash(i * 5u + 3u));
    float hd = unit(hash(i * 5u + 4u));
    float d = diameter_at(hd, rain.drops.y);
    float speed = max(9.65 - 10.3 * exp(-0.6 * d), 0.0) * UNITS_PER_METRE;
    float t = rain.camera.w;
    vec2 downwind = rain.wind.xy;
    vec3 raw = vec3(hx * box + downwind.x * drift, hy * height - speed * t, hz * box + downwind.y * drift);
    vec3 low = vec3(rain.camera.x - 0.5 * box, rain.camera.y - 0.6 * height, rain.camera.z - 0.5 * box);
    vec3 world = low + mod(raw - low, vec3(box, height, box));
    vec3 moved = vec3(downwind.x * rain.motion.y, -speed * rain.motion.x, downwind.y * rain.motion.y);
    vec3 rel = world - rain.camera.xyz;
    float distance = length(rel);
    // (a far drop inside the near box: the near box's own)
    bool inner = !near && abs(rel.x) < 0.5 * rain.boxes.x && abs(rel.z) < 0.5 * rain.boxes.x && rel.y > -0.6 * rain.boxes.y &&
                 rel.y < 0.4 * rain.boxes.y;
    // faded where its box wraps (its sides, top and bottom) and right at the lens
    float edge = (world.y - low.y) / height;
    float flat_distance = length(rel.xz);
    float fade = (1.0 - smoothstep(0.4 * box, 0.5 * box, flat_distance)) * smoothstep(0.0, 0.06, edge) *
                 (1.0 - smoothstep(0.94, 1.0, edge)) * smoothstep(NEAR_CLEAR, 2.0 * NEAR_CLEAR, distance);
    float reached = inner || fade <= 0.0 ? 0.0 : precip_reached(world);
    vec3 light = reached > 0.0 ? drop_light(world, rel / max(distance, 1.0e-3), true) : vec3(0.0);
    float weight = near ? rain.counts.w : rain.motion.z;
    drops[i].now = vec4(world, d * 0.5 / MM_PER_UNIT);
    drops[i].before = vec4(world - moved, weight * fade * reached);
    drops[i].colour = vec4(light, reached > 0.0 ? 1.0 : 0.0);
}
void place_splash(uint k, uint slot)
{
    float life = rain.splash.y;
    uint seed = hash(k * 9u + 11u);
    float cycles = rain.camera.w / life + unit(seed);
    float age = fract(cycles);
    uint at = hash(seed ^ (uint(int(floor(cycles))) * 0x27D4EB2Fu));
    vec2 line = rain.camera.xz + rain.precip_slant.xy * (rain.camera.y - rain.precip_slant.z);
    vec2 uv = line + (vec2(unit(at), unit(hash(at + 1u))) * 2.0 - 1.0) * rain.splash.x;
    float y = precip_line_height(uv);
    bool landed = y > -1.0e8 && abs(y - rain.camera.y) < 4000.0;
    vec3 world = vec3(uv.x - rain.precip_slant.x * (y - rain.precip_slant.z), y, uv.y - rain.precip_slant.y * (y - rain.precip_slant.z));
    float d = diameter_at(unit(hash(at + 2u)), rain.splash.z);
    float radius = (1.0 + (rain.splash.w - 1.0) * sqrt(age)) * d * 0.5 / MM_PER_UNIT;
    world.y += radius * 0.5;
    vec3 rel = world - rain.camera.xyz;
    float distance = length(rel);
    vec3 light = landed ? drop_light(world, rel / max(distance, 1.0e-3), false) : vec3(0.0);
    drops[slot].now = vec4(world, radius);
    drops[slot].before = vec4(world, landed ? rain.motion.w : 0.0);
    drops[slot].colour = vec4(light, landed ? 1.0 + age : 0.0); // (w: 1 + its age, 0 none)
}
void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint drops_total = uint(rain.counts.x + rain.counts.y);
    if (i < drops_total) {
        place_drop(i);
    } else if (i < drops_total + uint(rain.counts.z)) {
        place_splash(i - drops_total, i);
    }
}
)";

// A drop or a splash drawn from what the compute pass left: a drop as a streak along its move on the screen over the
// shutter, widened by the defocus, its coverage spread over the streak and times the drops it stands for; a splash as a
// small crown facing the camera, thinning as it spreads. MOTION_ONLY (the drops' second draw): their own motion where they
// cover enough of a pixel.
constexpr const char* kDrawVertexBody = R"(
layout(std430, set = 0, binding = 3) readonly buffer Drops { Drop drops[]; };
layout(push_constant) uniform Part { uvec4 part; } pc; // x: the first instance
layout(location = 0) out vec2 v_corner;
layout(location = 1) out vec4 v_colour;
layout(location = 2) out float v_soft;
layout(location = 3) flat out float v_streak;
layout(location = 4) flat out vec2 v_motion;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    Drop d = drops[pc.part.x + uint(gl_InstanceIndex)];
    vec3 world = d.now.xyz;
    vec4 clip = rain.vp * vec4(world, 1.0);
    float w = max(clip.w, 1.0e-3);
    float radius = d.now.w * rain.lens.x / w;
    float blur = rain.lens.y * rain.lens.x * abs(1.0 / w - 1.0 / rain.lens.z);
    float drawn = max(sqrt(radius * radius + blur * blur), rain.lens.w);
    float coverage = clamp(radius * radius / (drawn * drawn), 0.0, 1.0);
    vec2 now = (clip.xy - rain.offsets.zw * clip.w) / w;
    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0, (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
#ifdef SPLASH
    float age = d.colour.w - 1.0;
    float alpha = coverage * d.before.w * (1.0 - age) * (1.0 - age);
    clip.xy += corner * drawn * 2.0 / rain.rect.zw * clip.w;
    v_corner = corner;
    v_soft = clamp(blur / drawn, 0.0, 1.0);
    v_streak = 0.0;
    v_motion = vec2(0.0);
    bool hidden = d.colour.w < 0.5 || clip.w < 2.0 || alpha < 0.001;
    gl_Position = hidden ? vec4(0.0) : to_target(clip);
    v_colour = vec4(d.colour.rgb, min(alpha, 1.0));
#else
    vec4 before_clip = rain.vp_before * vec4(d.before.xyz, 1.0);
    vec2 before = before_clip.w > 1.0e-3 ? before_clip.xy / before_clip.w : now;
    vec2 streak = (before - now) * 0.5 * rain.rect.zw * rain.env.y;
    float streak_length = length(streak);
    if (streak_length > rain.env.z) {
        streak *= rain.env.z / streak_length;
        streak_length = rain.env.z;
    }
    vec2 axis = streak_length > 1.0e-3 ? streak / streak_length : vec2(0.0, 1.0);
    vec2 side = vec2(-axis.y, axis.x);
    float half_length = 0.5 * streak_length;
    vec2 offset = 0.5 * streak + axis * (corner.x * (half_length + drawn)) + side * (corner.y * drawn);
    clip.xy += offset * 2.0 / rain.rect.zw * clip.w;
    v_corner = vec2(corner.x * (half_length / drawn + 1.0), corner.y);
    v_soft = clamp(blur / drawn, 0.0, 1.0);
    v_streak = half_length / drawn;
    v_motion = (before - now) * vec2(0.5, -0.5);
    // (the disc's area against the capsule's: the drop's coverage over the exposure, x the drops it stands for)
    float spread = 3.14159265 / (3.14159265 + 4.0 * v_streak);
    float alpha = coverage * spread * d.before.w;
    bool hidden = d.colour.w < 0.5 || clip.w < 2.0 || alpha < 0.001;
    gl_Position = hidden ? vec4(0.0) : to_target(clip);
    v_colour = vec4(d.colour.rgb, min(alpha, 1.0));
#endif
}
)";

constexpr const char* kDrawFragmentBody = R"(
layout(location = 0) in vec2 v_corner;
layout(location = 1) in vec4 v_colour;
layout(location = 2) in float v_soft;
layout(location = 3) flat in float v_streak;
layout(location = 4) flat in vec2 v_motion;
layout(location = 0) out vec4 o_colour;
#ifdef MOTION
layout(location = 1) out vec4 o_motion;
#endif
void main()
{
#ifdef SPLASH
    float r = length(v_corner);
    float sheet = smoothstep(0.35, 0.8, r) * (1.0 - smoothstep(0.8, 1.0, r)); // (a crown: a thin rising sheet round its foot)
    float alpha = v_colour.a * mix(sheet * 2.2, max(1.0 - r * r, 0.0), v_soft);
#else
    float r = length(vec2(max(abs(v_corner.x) - v_streak, 0.0), v_corner.y));
    float crisp = 1.0 - smoothstep(0.55, 1.0, r);
    float soft = max(1.0 - r * r, 0.0);
    float alpha = v_colour.a * mix(crisp, soft * soft * 1.6, v_soft);
#endif
    if (alpha <= 0.001) {
        discard;
    }
    alpha = min(alpha, 1.0);
#ifdef MOTION_ONLY
    if (alpha < rain.env.w) {
        discard;
    }
    o_colour = vec4(0.0);
    o_motion = vec4(v_motion, 0.0, 0.0);
#else
    o_colour = vec4(max(v_colour.rgb, vec3(0.0)), alpha); // (linear light: the frame's unit)
#ifdef MOTION
    o_motion = vec4(0.0, 0.0, alpha >= rain.env.w ? 0.5 * alpha : alpha, 0.0);
#endif
#endif
}
)";

// The wet mask: a full-screen triangle - each pixel's surface (the depth back to the world, its normal from its
// neighbours'), the water's share on it (rain_wetness: the film's or the puddle's), the decals' puddles over it.
constexpr const char* kMaskVertexBody = R"(
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kMaskFragmentBody = R"(
layout(set = 0, binding = 4) uniform sampler2D u_depth;
layout(set = 0, binding = 5) uniform sampler2D u_decals;
layout(location = 0) out vec4 o_wet;
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float d = texelFetch(u_depth, pixel, 0).r;
    vec2 at = gl_FragCoord.xy - rain.rect.xy;
    vec2 ndc = vec2(at.x / rain.rect.z * 2.0 - 1.0, 1.0 - 2.0 * at.y / rain.rect.w);
    vec4 back = rain.vp_inverse * vec4(ndc, (d - rain.depth_map.x) * rain.depth_map.y, 1.0);
    vec3 world = back.xyz / (abs(back.w) > 1.0e-9 ? back.w : 1.0e-9);
    vec3 n = cross(dFdx(world), dFdy(world));
    float length_n = length(n);
    n = length_n > 1.0e-12 ? n / length_n : vec3(0.0, 1.0, 0.0);
    if (dot(n, rain.camera.xyz - world) < 0.0) n = -n;
    float wet = 0.0;
    if (d < 1.0) {
        float flux = max(dot(n, -rain.precip_fall.xyz), 0.0) / max(-rain.precip_fall.y, 0.05);
        float reach = flux > 0.0 ? precip_reached(world + n * 0.5) : 0.0;
        vec4 water = rain_wetness(world, n, reach, flux);
        wet = water.w; // (the flat water: a puddle, a film over the texture)
    }
    if (rain.lamps.z > 0.5) {
        wet = max(wet, texelFetch(u_decals, pixel, 0).r);
    }
    o_wet = vec4(wet, 0.0, 0.0, 0.0);
}
)";

// The block, as the shaders' std140 block lays it out.
struct Block {
    Float4x4 vp{};
    Float4x4 vp_before{};
    Float4x4 vp_inverse{};
    std::array<float, 4> camera{};
    std::array<float, 4> drops{};
    std::array<float, 4> counts{};
    std::array<float, 4> boxes{};
    std::array<float, 4> wind{};
    std::array<float, 4> motion{};
    std::array<float, 4> light{};
    std::array<float, 4> light_colour{};
    std::array<float, 4> lamps{};
    std::array<float, 4> rect{};
    std::array<float, 4> offsets{};
    std::array<float, 4> lens{};
    std::array<float, 4> env{};
    std::array<float, 4> env_scale{};
    std::array<float, 4> env_sky{};
    std::array<float, 4> splash{};
    std::array<float, 4> depth_projection{};
    std::array<float, 4> depth_map{};
    std::array<float, 4> precip_fall{};
    std::array<float, 4> precip_slant{};
    std::array<float, 4> precip_near{};
    std::array<float, 4> precip_far{};
    std::array<float, 4> ground{};
    std::array<float, 4> ripple{};
    std::array<std::array<float, 4>, kRainMostLamps> lamp_at{};
    std::array<std::array<float, 4>, kRainMostLamps> lamp_colour{};
};
static_assert(sizeof(Block) == (3 * 64) + (24 * 16) + (2 * kRainMostLamps * 16), "the shaders' Block");

// The world draws' WetUniforms (ff_shaders.cpp wet_block), byte for byte.
struct WetUniforms {
    Float4x4 eye_to_world{};
    std::array<float, 4> precip_fall{};
    std::array<float, 4> precip_slant{};
    std::array<float, 4> precip_near{};
    std::array<float, 4> precip_far{};
    std::array<float, 4> ground{};
    std::array<float, 4> ripple{};
};
static_assert(sizeof(WetUniforms) == 64 + (6 * 16), "ff_shaders.cpp's WetUniforms");

enum class DrawKind : uint8_t { kDrops, kSplashes, kDropMotion, kMask };

struct DrawPipeline {
    DrawKind kind = DrawKind::kDrops;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool motion = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

// The passes' timestamps: the map's start and end (begin_rain_view), the wet mask's, the air's.
constexpr uint32_t kTimerSets = 8;
constexpr uint32_t kTimerPoints = 6;
constexpr uint32_t kMapStart = 0;
constexpr uint32_t kMapEnd = 1;
constexpr uint32_t kMaskStart = 2;
constexpr uint32_t kMaskEnd = 3;
constexpr uint32_t kAirStart = 4;
constexpr uint32_t kAirEnd = 5;
constexpr uint64_t kTimingLogFrames = 600;

struct RainState {
    RainSettings settings;
    // the view
    rt::ViewDrawn view;
    bool view_known = false;
    uint64_t view_serial = 0;
    uint64_t wet_block_serial = 0;
    uint64_t wet_block_frame = 0;
    VkDescriptorBufferInfo wet_block{};
    // the rain's clock and the wind's drift (the drops' horizontal move) at the frame and the one before
    uint64_t clock_frame = 0;
    double clock_now = 0.0;
    double clock_before = 0.0;
    double drift_now = 0.0;
    double drift_before = 0.0;
    bool clock_known = false;
    // the objects
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    VkSampler linear = VK_NULL_HANDLE;
    bool scene_binding = false;
    std::array<VkPipeline, 2> place{};             // plain, traced
    std::array<VkShaderModule, 2> drop_vertex{};   // drops, splashes
    std::array<VkShaderModule, 5> drop_fragment{}; // drops [motion], splashes [motion], drops' motion alone
    VkShaderModule mask_vertex = VK_NULL_HANDLE;
    VkShaderModule mask_fragment = VK_NULL_HANDLE;
    std::vector<DrawPipeline> draws;
    DeviceBuffer drops;
    Image env;
    Image mask;
    uint64_t mask_serial = 0;
    std::array<uint32_t, 4> mask_rect{};
    bool failed = false;
    RainStats stats;
    // timing
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    std::array<uint64_t, kTimerSets> timer_frames{};
    std::array<uint8_t, kTimerSets> timer_written{};
    uint64_t timed = 0;
    std::array<double, 3> window_milliseconds{};
    std::array<uint64_t, 3> window_samples{};
    uint64_t window_frames = 0;
};

RainState& rain_state()
{
    static RainState own;
    return own;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class RainListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kRain; }
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            begin_rain_view(state, view);
        }
    }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rain_wet_mask(state, view);
        }
    }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rain_air(state, view);
        }
        rain_state().view_known = false;
    }
};

RainListener& listener()
{
    static RainListener instance;
    return instance;
}

using Matrix = std::array<std::array<float, 4>, 4>;

Float4x4 flat(const Matrix& matrix)
{
    Float4x4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(matrix, row), column);
        }
    }
    return out;
}

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(out, row), column) = sum;
        }
    }
    return out;
}

// A 4 x 4 matrix's inverse (Gauss-Jordan with partial pivoting; a singular one: the identity).
Matrix inverse(const Matrix& matrix)
{
    Matrix a = matrix;
    Matrix out{};
    for (std::size_t i = 0; i < 4; ++i) {
        core::at(core::at(out, i), i) = 1.0F;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::abs(core::at(core::at(a, row), column)) > std::abs(core::at(core::at(a, pivot), column))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(a, pivot), column)) < 1.0e-12F) {
            Matrix identity{};
            for (std::size_t i = 0; i < 4; ++i) {
                core::at(core::at(identity, i), i) = 1.0F;
            }
            return identity;
        }
        std::swap(core::at(a, column), core::at(a, pivot));
        std::swap(core::at(out, column), core::at(out, pivot));
        const float scale = 1.0F / core::at(core::at(a, column), column);
        for (std::size_t k = 0; k < 4; ++k) {
            core::at(core::at(a, column), k) *= scale;
            core::at(core::at(out, column), k) *= scale;
        }
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const float factor = core::at(core::at(a, row), column);
            for (std::size_t k = 0; k < 4; ++k) {
                core::at(core::at(a, row), k) -= factor * core::at(core::at(a, column), k);
                core::at(core::at(out, row), k) -= factor * core::at(core::at(out, column), k);
            }
        }
    }
    return out;
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

// --- the weather now ---------------------------------------------------------------------------------------------------

double rate_now() { return static_cast<double>(rain_state().settings.rate); }

// How the rain falls now: the median drop against the weather's wind (its mean - the map's slant stays put through the
// gusts; the drops take the gusts in their drift).
rain::Fall fall_now()
{
    return rain::fall(
        rate_now(), wind::speed(),
        {static_cast<double>(core::at(wind::kDirection, 0)), static_cast<double>(core::at(wind::kDirection, 1))});
}

// The drops' downwind speed at their height now (units a second, with the gusts).
double drift_speed(double clock)
{
    return rain::wind_at(wind::speed(), rain::kDropHeight) * rain::kUnitsPerMetre *
           static_cast<double>(wind::gust(static_cast<float>(std::fmod(clock, kClockWraps))));
}

// The drops a box's drawn drop stands for: the true number of drops of kRainLeastDrawn mm and over in the box's volume
// (the far box less the near one) over the drops drawn there.
float box_weight(double rate, bool inner)
{
    const double metres = 1.0 / rain::kUnitsPerMetre;
    const double near_volume = kRainNearBox * kRainNearBox * kRainNearHeight * metres * metres * metres;
    const double far_volume = (kRainFarBox * kRainFarBox * kRainFarHeight * metres * metres * metres) - near_volume;
    const double density = rain::number_density_over(rate, kRainLeastDrawn);
    return static_cast<float>(inner ? density * near_volume / kRainNearDrops : density * far_volume / kRainFarDrops);
}

float splash_weight(double rate)
{
    // the impacts a square metre a second x the area x a splash's life, over the splashes drawn
    const double side = 2.0 * kRainSplashReach / rain::kUnitsPerMetre;
    return static_cast<float>(rain::impact_rate(rate, kRainSplashLeast) * side * side * kRainSplashLife /
                              kRainSplashes);
}

// --- the objects ----------------------------------------------------------------------------------------------------

std::string constants_source()
{
    return text::format(
        "#define MOST_LAMPS %u\n#define UNITS_PER_METRE %.4f\n#define MM_PER_UNIT %.4f\n#define NEAR_CLEAR %.3f\n"
        "#define SUN_REACH 6000.0\n#define PRECIP_SIZE %u\n",
        kRainMostLamps, rain::kUnitsPerMetre, 25.4, static_cast<double>(kRainNearClear), precip::kMapSize);
}

std::string source_of(const char* body, const std::string& defines, bool surface)
{
    std::string source = "#version 460\n";
    source += defines;
    source += constants_source();
    source += kBlockSource;
    source += precip::reached_glsl();
    if (surface) {
        source += rain::glsl_constants();
        source += rain::ripple_glsl();
        source += rain::surface_glsl();
    }
    source += kCommonSource;
    source += body;
    return source;
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

VkSampler make_sampler(VkDevice device, VkFilter filter, float most_lod)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = most_lod;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}

bool make_layouts(Backend& state, std::string* error)
{
    RainState& own = rain_state();
    const VkDevice device = state.context.device();
    own.scene_binding = state.context.ray_tracing_enabled();
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if (binding == kBindingBlock) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingDrops) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        } else if (binding == kBindingScene) {
            if (!own.scene_binding) {
                continue;
            }
            type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        }
        bindings.push_back({.binding = binding,
                            .descriptorType = type,
                            .descriptorCount = 1,
                            .stageFlags = kStages,
                            .pImmutableSamplers = nullptr});
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    constexpr float kManyLevels = 16.0F;
    own.nearest = make_sampler(device, VK_FILTER_NEAREST, 0.0F);
    own.linear = make_sampler(device, VK_FILTER_LINEAR, kManyLevels);
    if (own.nearest == VK_NULL_HANDLE || own.linear == VK_NULL_HANDLE ||
        vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        *error = "the descriptor layout or the samplers";
        return false;
    }
    const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(std::array<uint32_t, 4>)};
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

bool ensure_objects(Backend& state, std::string* error)
{
    RainState& own = rain_state();
    if (own.mask_fragment != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    for (std::size_t traced = 0; traced < 2; ++traced) {
        if (core::at(own.place, traced) != VK_NULL_HANDLE || (traced == 1 && !own.scene_binding)) {
            continue;
        }
        const std::string defines = traced == 1 ? "#define TRACED\n#extension GL_EXT_ray_query : require\n" : "";
        core::at(own.place, traced) = make_compute(device, own.layout, source_of(kPlaceBody, defines, false), error);
        if (core::at(own.place, traced) == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t splash = 0; splash < 2; ++splash) {
        VkShaderModule& module = core::at(own.drop_vertex, splash);
        if (module == VK_NULL_HANDLE) {
            module = make_module(device, RtShaderStage::kVertex,
                                 source_of(kDrawVertexBody, splash == 1 ? "#define SPLASH\n" : "", false), error);
            if (module == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    const std::array<const char*, 5> fragments{"", "#define MOTION\n", "#define SPLASH\n",
                                               "#define SPLASH\n#define MOTION\n",
                                               "#define MOTION\n#define MOTION_ONLY\n"};
    for (std::size_t variant = 0; variant < fragments.size(); ++variant) {
        VkShaderModule& module = core::at(own.drop_fragment, variant);
        if (module == VK_NULL_HANDLE) {
            module = make_module(device, RtShaderStage::kFragment,
                                 source_of(kDrawFragmentBody, core::at(fragments, variant), false), error);
            if (module == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    if (own.mask_vertex == VK_NULL_HANDLE) {
        own.mask_vertex = make_module(device, RtShaderStage::kVertex, source_of(kMaskVertexBody, "", false), error);
        if (own.mask_vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    own.mask_fragment = make_module(device, RtShaderStage::kFragment, source_of(kMaskFragmentBody, "", true), error);
    return own.mask_fragment != VK_NULL_HANDLE;
}

// A draw's pipeline (made on first use). The drops and splashes: strips blended by their alpha over the colour, the depth
// tested, not written; the motion's z the most of their share (the reactive mask). The drops' motion alone: their own
// motion's x, y where they cover enough. The mask: a full-screen triangle into the R8 mask.
VkPipeline draw_pipeline(Backend& state, const DrawPipeline& wanted, std::string* error)
{
    RainState& own = rain_state();
    for (const DrawPipeline& made : own.draws) {
        if (made.kind == wanted.kind && made.format == wanted.format && made.depth == wanted.depth &&
            made.samples == wanted.samples && made.motion == wanted.motion) {
            return made.pipeline;
        }
    }
    const bool mask = wanted.kind == DrawKind::kMask;
    const bool motion_only = wanted.kind == DrawKind::kDropMotion;
    VkShaderModule vertex_module = core::at(own.drop_vertex, 0);
    VkShaderModule fragment_module = core::at(own.drop_fragment, wanted.motion ? 1 : 0);
    if (wanted.kind == DrawKind::kSplashes) {
        vertex_module = core::at(own.drop_vertex, 1);
        fragment_module = core::at(own.drop_fragment, wanted.motion ? 3 : 2);
    } else if (motion_only) {
        fragment_module = core::at(own.drop_fragment, 4);
    } else if (mask) {
        vertex_module = own.mask_vertex;
        fragment_module = own.mask_fragment;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex_module,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment_module,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = mask ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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
    depth.depthTestEnable = mask ? VK_FALSE : VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
    VkPipelineColorBlendAttachmentState& colour_blend = core::at(blend, 0);
    if (mask) {
        colour_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    } else if (motion_only) {
        colour_blend.colorWriteMask = 0;
    } else {
        colour_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        colour_blend.blendEnable = VK_TRUE;
        colour_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colour_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colour_blend.colorBlendOp = VK_BLEND_OP_ADD;
        colour_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colour_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colour_blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendAttachmentState& motion_blend = core::at(blend, 1);
    if (motion_only) {
        motion_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    } else {
        motion_blend.blendEnable = VK_TRUE;
        motion_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.colorBlendOp = VK_BLEND_OP_MAX;
        motion_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_blend.alphaBlendOp = VK_BLEND_OP_MAX;
        motion_blend.colorWriteMask = VK_COLOR_COMPONENT_B_BIT;
    }
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = wanted.motion && !mask ? 2U : 1U;
    blending.pAttachments = blend.data();
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    const std::array<VkFormat, 2> formats{mask ? kMaskFormat : wanted.format, kMotionFormat};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = wanted.motion && !mask ? 2U : 1U;
    rendering.pColorAttachmentFormats = formats.data();
    rendering.depthAttachmentFormat = mask ? VK_FORMAT_UNDEFINED : wanted.depth;
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

bool ensure_drops(Backend& state)
{
    RainState& own = rain_state();
    if (own.drops.buffer != VK_NULL_HANDLE) {
        return true;
    }
    constexpr VkDeviceSize kBytes = static_cast<VkDeviceSize>(kTotalInstances) * kDropBytes;
    if (!state.memory.create_buffer(kBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    BufferPlace::kDevice, &own.drops)) {
        state.memory.destroy_buffer(&own.drops);
        return false;
    }
    return true;
}

// The blurred frame's image for a colour target (half its size, its mip chain down to a texel).
bool ensure_env(Backend& state, const Image& colour)
{
    RainState& own = rain_state();
    const Extent extent{.width = std::max(colour.extent.width / 2, 1U),
                        .height = std::max(colour.extent.height / 2, 1U)};
    if (own.env.image != VK_NULL_HANDLE && own.env.extent.width == extent.width &&
        own.env.extent.height == extent.height) {
        return true;
    }
    retire_image(state, &own.env);
    uint32_t levels = 1;
    for (uint32_t side = std::max(extent.width, extent.height); side > 1; side /= 2) {
        ++levels;
    }
    const ImageRequest request{.format = kEnvFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = levels,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.env)) {
        return false;
    }
    own.env.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prepare_image(state, &own.env);
    return true;
}

bool ensure_mask(Backend& state, const Image& colour)
{
    RainState& own = rain_state();
    if (own.mask.image != VK_NULL_HANDLE && own.mask.extent.width == colour.extent.width &&
        own.mask.extent.height == colour.extent.height) {
        return true;
    }
    retire_image(state, &own.mask);
    const ImageRequest request{.format = kMaskFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = colour.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.mask)) {
        return false;
    }
    own.mask.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prepare_image(state, &own.mask);
    return true;
}

// The colour copied into the blurred frame's first level (half its size, filtered) and each level from the last.
void blur_frame(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& rect)
{
    RainState& own = rain_state();
    Image& env = own.env;
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &env, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const auto to_offset = [](uint32_t x, uint32_t y) {
        return VkOffset3D{.x = static_cast<int32_t>(x), .y = static_cast<int32_t>(y), .z = 0};
    };
    VkImageBlit first{};
    first.srcSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    first.srcOffsets[0] = to_offset(0, 0);
    first.srcOffsets[1] = VkOffset3D{
        .x = static_cast<int32_t>(colour->extent.width), .y = static_cast<int32_t>(colour->extent.height), .z = 1};
    first.dstSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    first.dstOffsets[0] = to_offset(0, 0);
    first.dstOffsets[1] =
        VkOffset3D{.x = static_cast<int32_t>(env.extent.width), .y = static_cast<int32_t>(env.extent.height), .z = 1};
    (void)rect;
    vkCmdBlitImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, env.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &first, VK_FILTER_LINEAR);
    uint32_t width = env.extent.width;
    uint32_t height = env.extent.height;
    for (uint32_t level = 1; level < env.levels; ++level) {
        // (the last level a source: its own barrier to TRANSFER_SRC)
        const VkImageMemoryBarrier to_source{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = env.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = level - 1,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_source);
        const uint32_t next_width = std::max(width / 2, 1U);
        const uint32_t next_height = std::max(height / 2, 1U);
        VkImageBlit blit{};
        blit.srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .baseArrayLayer = 0, .layerCount = 1};
        blit.srcOffsets[0] = to_offset(0, 0);
        blit.srcOffsets[1] = VkOffset3D{.x = static_cast<int32_t>(width), .y = static_cast<int32_t>(height), .z = 1};
        blit.dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = 0, .layerCount = 1};
        blit.dstOffsets[0] = to_offset(0, 0);
        blit.dstOffsets[1] =
            VkOffset3D{.x = static_cast<int32_t>(next_width), .y = static_cast<int32_t>(next_height), .z = 1};
        vkCmdBlitImage(commands, env.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, env.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        width = next_width;
        height = next_height;
    }
    // every level to the shaders' layout: all but the last are TRANSFER_SRC, the last TRANSFER_DST
    std::array<VkImageMemoryBarrier, 2> to_read{};
    for (std::size_t part = 0; part < to_read.size(); ++part) {
        const bool last = part == 1;
        core::at(to_read, part) = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = last ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = env.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = last ? env.levels - 1 : 0,
                                 .levelCount = last ? 1 : env.levels - 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };
    }
    const uint32_t barriers = env.levels > 1 ? 2U : 1U;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, barriers, env.levels > 1 ? to_read.data() : &core::at(to_read, 1));
    env.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// --- the passes' time on the device (the log's "rain: the passes take") ---------------------------------------------

void read_timer_set(Backend& state, uint32_t set)
{
    RainState& own = rain_state();
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
    const uint8_t written = core::at(own.timer_written, set);
    const auto span = [&](uint32_t from, uint32_t to, std::size_t part) {
        const Stamp& a = core::at(stamps, from);
        const Stamp& b = core::at(stamps, to);
        if ((written & (1U << from)) == 0U || (written & (1U << to)) == 0U || a.available == 0 || b.available == 0 ||
            b.value < a.value) {
            return;
        }
        core::at(own.window_milliseconds, part) += static_cast<double>(b.value - a.value) * own.tick_milliseconds;
        ++core::at(own.window_samples, part);
    };
    span(kMapStart, kMapEnd, 0);
    span(kMaskStart, kMaskEnd, 1);
    span(kAirStart, kAirEnd, 2);
    if (++own.window_frames >= kTimingLogFrames) {
        const auto average = [&own](std::size_t part) {
            const uint64_t samples = core::at(own.window_samples, part);
            return samples != 0 ? core::at(own.window_milliseconds, part) / static_cast<double>(samples) : 0.0;
        };
        get_log("vulkan: rain: the passes take %.3f ms on the device - the occlusion map %.3f, the wet mask %.3f, the "
                "drops and "
                "splashes %.3f (the last %llu frames' average; %u lamps lighting the drops)",
                average(0) + average(1) + average(2), average(0), average(1), average(2),
                static_cast<unsigned long long>(own.window_frames), own.stats.last_lamps);
        own.window_frames = 0;
        own.window_milliseconds = {};
        own.window_samples = {};
    }
}

bool timer_set(Backend& state, VkCommandBuffer commands, uint32_t* set)
{
    RainState& own = rain_state();
    if (own.tick_milliseconds < 0.0) {
        return false;
    }
    if (own.timer == VK_NULL_HANDLE) {
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
            return false;
        }
        own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
    }
    const uint64_t frame = state.frames.serial();
    const auto current = static_cast<uint32_t>(own.timed % kTimerSets);
    if (core::at(own.timer_frames, current) == frame) {
        *set = current;
        return true;
    }
    const auto next = static_cast<uint32_t>((own.timed + 1) % kTimerSets);
    uint64_t& written = core::at(own.timer_frames, next);
    if (written != 0 && state.frames.completed() < written) {
        return false;
    }
    if (written != 0) {
        read_timer_set(state, next);
    }
    ++own.timed;
    written = frame;
    core::at(own.timer_written, next) = 0;
    vkCmdResetQueryPool(commands, own.timer, next * kTimerPoints, kTimerPoints);
    *set = next;
    return true;
}

void stamp_time(Backend& state, VkCommandBuffer commands, uint32_t point, VkPipelineStageFlagBits stage)
{
    RainState& own = rain_state();
    uint32_t set = 0;
    if (!timer_set(state, commands, &set) || (core::at(own.timer_written, set) & (1U << point)) != 0U) {
        return;
    }
    vkCmdWriteTimestamp(commands, stage, own.timer, (set * kTimerPoints) + point);
    core::at(own.timer_written, set) = static_cast<uint8_t>(core::at(own.timer_written, set) | (1U << point));
}

// --- the blocks -------------------------------------------------------------------------------------------------------

void fill_precip(const PrecipMap& map, const rain::Fall& fall, std::array<float, 4>* fall_out,
                 std::array<float, 4>* slant, std::array<float, 4>* near_window, std::array<float, 4>* far_window)
{
    *fall_out = {static_cast<float>(core::at(fall.direction, 0)), static_cast<float>(core::at(fall.direction, 1)),
                 static_cast<float>(core::at(fall.direction, 2)), map.on ? 1.0F : 0.0F};
    *slant = map.slant;
    *near_window = map.near_window;
    *far_window = map.far_window;
}

std::array<float, 4> ground_now()
{
    const rain::Ground ground = rain::ground_of(rain::stored(), rate_now());
    return {static_cast<float>(ground.stored), static_cast<float>(ground.pores), static_cast<float>(ground.level),
            static_cast<float>(ground.film_rate)};
}

std::array<float, 4> ripple_now(double clock)
{
    const rain::Ripples ripples = rain::ripples_of(rate_now());
    return {static_cast<float>(ripples.cell), static_cast<float>(ripples.layers),
            static_cast<float>(std::fmod(clock, kClockWraps)), static_cast<float>(ripples.life)};
}

std::optional<VkDescriptorBufferInfo> in_ring(Backend& state, std::span<const uint8_t> bytes)
{
    const std::optional<RingSpan> span = state.ring.copy(bytes, state.uniform_alignment);
    if (!span) {
        return std::nullopt;
    }
    return VkDescriptorBufferInfo{.buffer = span->buffer, .offset = span->offset, .range = bytes.size()};
}

std::optional<VkDescriptorBufferInfo> wet_block(Backend& state)
{
    RainState& own = rain_state();
    const uint64_t frame = state.frames.serial();
    if (own.wet_block_serial == own.view_serial && own.wet_block_frame == frame &&
        own.wet_block.buffer != VK_NULL_HANDLE) {
        return own.wet_block;
    }
    WetUniforms uniforms{};
    uniforms.eye_to_world = trail_eye_to_world(own.view.camera.view);
    fill_precip(precip_map(state), fall_now(), &uniforms.precip_fall, &uniforms.precip_slant, &uniforms.precip_near,
                &uniforms.precip_far);
    uniforms.ground = ground_now();
    uniforms.ripple = ripple_now(own.clock_now);
    const void* const data = &uniforms;
    const std::optional<VkDescriptorBufferInfo> block =
        in_ring(state, std::span(static_cast<const uint8_t*>(data), sizeof(uniforms)));
    if (!block) {
        return std::nullopt;
    }
    own.wet_block = *block;
    own.wet_block_serial = own.view_serial;
    own.wet_block_frame = frame;
    return own.wet_block;
}

// What a view's passes work on.
struct ViewPlan {
    Image* colour = nullptr;
    Image* depth = nullptr;
    bool sampled_depth = false;
    bool motion = false;
    std::array<uint32_t, 4> rect{};
};

bool plan_view(Backend& state, const rt::ViewDrawn& view, ViewPlan* plan)
{
    Surface* const colour_surface = core::at(state.targets, 0);
    Image* const colour = image_of_surface(colour_surface);
    Image* const depth = image_of_surface(state.depth);
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE ||
        (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 || depth == nullptr ||
        (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || depth->attachment_view == VK_NULL_HANDLE ||
        depth->samples != colour->samples) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: rain: a view drawn into targets that can't take it (no depth) - no rain there");
        }
        return false;
    }
    plan->colour = colour;
    plan->depth = depth;
    plan->sampled_depth = (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE;
    const uint32_t width = std::min(colour->extent.width, depth->extent.width);
    const uint32_t height = std::min(colour->extent.height, depth->extent.height);
    const uint32_t left = std::min(core::at(view.rect, 0), width);
    const uint32_t top = std::min(core::at(view.rect, 1), height);
    plan->rect = {left, top, std::min(core::at(view.rect, 2), width - left),
                  std::min(core::at(view.rect, 3), height - top)};
    plan->motion = motion_wanted(state, colour_surface, colour) && state.motion.image.image != VK_NULL_HANDLE;
    return core::at(plan->rect, 2) != 0 && core::at(plan->rect, 3) != 0;
}

Block block_for(Backend& state, const ViewPlan& plan, const rt::ViewDrawn& view, bool env, bool env_pass = true)
{
    RainState& own = rain_state();
    const double rate = rate_now();
    Block block{};
    const Matrix vp = multiply(view.camera.view, view.camera.projection);
    block.vp = flat(vp);
    block.vp_inverse = flat(inverse(vp));
    block.vp_before = block.vp;
    const MotionVectors& motion = state.motion;
    if (plan.motion && motion.before.known) {
        block.vp_before = motion_multiply(motion.before.view, motion.before.projection);
    }
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), static_cast<float>(std::fmod(own.clock_now, kClockWraps))};
    const double slope = rain::size_slope(rate);
    block.drops = {static_cast<float>(slope), static_cast<float>(kRainLeastDrawn),
                   static_cast<float>(1.0 - std::exp(-slope * (rain::kMostDiameter - kRainLeastDrawn))),
                   static_cast<float>(rain::kMostDiameter)};
    own.stats.near_weight = box_weight(rate, true);
    own.stats.far_weight = box_weight(rate, false);
    own.stats.splash_weight = splash_weight(rate);
    block.counts = {static_cast<float>(kRainNearDrops), static_cast<float>(kRainFarDrops),
                    static_cast<float>(kRainSplashes), own.stats.near_weight};
    block.boxes = {kRainNearBox, kRainNearHeight, kRainFarBox, kRainFarHeight};
    block.wind = {core::at(wind::kDirection, 0), core::at(wind::kDirection, 1),
                  static_cast<float>(std::fmod(own.drift_now, static_cast<double>(kRainNearBox))),
                  static_cast<float>(std::fmod(own.drift_now, static_cast<double>(kRainFarBox)))};
    constexpr double kMostStep = 0.1; // (a pause's resume isn't a streak)
    const double since = own.clock_now - own.clock_before;
    const double kept = std::clamp(since, 0.0, kMostStep);
    const double drifted = since > 0.0 ? (own.drift_now - own.drift_before) * (kept / since) : 0.0;
    block.motion = {static_cast<float>(kept), static_cast<float>(drifted), own.stats.far_weight,
                    own.stats.splash_weight};
    // the sun's or moon's light (the clock's, in the frame's unit); without the clock the drops take only what lies behind
    const rt::DayLight day = rt::day_light();
    if (day.on) {
        block.light = {core::at(day.light, 0), core::at(day.light, 1), core::at(day.light, 2),
                       core::at(day.light, 1) > 0.0F ? 1.0F : 0.0F};
        block.light_colour = {core::at(day.sun_light, 0), core::at(day.sun_light, 1), core::at(day.sun_light, 2),
                              kRainDropPhaseG};
    } else {
        block.light_colour = {0.0F, 0.0F, 0.0F, kRainDropPhaseG};
    }
    // the lamps (the lamps pass's choice for the view: the nearest, most important; as on as the clock says)
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    const float lamps_on = rt::lamps_on(day, level != nullptr && level->dark);
    uint32_t lamps = 0;
    if (lamps_on > 0.0F && env_pass) { // (the drops alone read the lamps: the wet mask has none)
        rt::ViewLightRequest request;
        request.camera = view.camera.position;
        request.view_projection = vp;
        request.max = kRainMostLamps;
        const std::vector<rt::Emitter> lights = rt::select_view_lights(rt::frame_record(), level.get(), request);
        for (const rt::Emitter& light : lights) {
            if (lamps >= kRainMostLamps) {
                break;
            }
            core::at(block.lamp_at, lamps) = {core::at(light.position, 0), core::at(light.position, 1),
                                              core::at(light.position, 2), std::max(light.core, 1.0F)};
            core::at(block.lamp_colour, lamps) = {core::at(light.colour, 0) * light.intensity,
                                                  core::at(light.colour, 1) * light.intensity,
                                                  core::at(light.colour, 2) * light.intensity, light.reach};
            ++lamps;
        }
    }
    own.stats.last_lamps = lamps;
    block.lamps = {static_cast<float>(lamps), lamps_on, 0.0F, 0.0F};
    block.rect = {static_cast<float>(core::at(plan.rect, 0)), static_cast<float>(core::at(plan.rect, 1)),
                  static_cast<float>(core::at(plan.rect, 2)), static_cast<float>(core::at(plan.rect, 3))};
    block.offsets = {core::at(plan.rect, 2) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 2)) : 0.0F,
                     core::at(plan.rect, 3) != 0 ? 1.0F / static_cast<float>(core::at(plan.rect, 3)) : 0.0F,
                     plan.motion ? core::at(motion.jitter, 0) : 0.0F, plan.motion ? core::at(motion.jitter, 1) : 0.0F};
    // the lens: the snow's camera (one camera: its aperture, focus and shutter)
    const auto& projection = view.camera.projection;
    const float focal = core::at(plan.rect, 3) != 0 ? static_cast<float>(core::at(plan.rect, 3)) * kHalf *
                                                          std::abs(core::at(core::at(projection, 1), 1))
                                                    : 1.0F;
    const float focus = view.focus_known && view.focus_distance > 1.0F ? view.focus_distance : kSnowFocus;
    block.lens = {focal, kSnowAperture, focus, kSnowLeastPixels};
    // the blurred frame: the level whose texels span about kRainEnvironmentPixels across the view
    float level_of_env = -1.0F;
    if (env && own.env.image != VK_NULL_HANDLE) {
        const float across = static_cast<float>(core::at(plan.rect, 2)) * kHalf; // (level 0 is half the target)
        level_of_env = std::clamp(std::log2(std::max(across / kRainEnvironmentPixels, 1.0F)), 0.0F,
                                  static_cast<float>(own.env.levels - 1));
        block.env_scale = {1.0F / static_cast<float>(plan.colour->extent.width),
                           1.0F / static_cast<float>(plan.colour->extent.height), 0.0F, 0.0F};
    }
    block.env = {level_of_env, kSnowShutter, kMostStreak, kRainMotionShare};
    const rt::DayLight& sky = day;
    block.env_sky = {core::at(sky.sky_light, 0), core::at(sky.sky_light, 1), core::at(sky.sky_light, 2), sky.on ? 1.0F : 0.0F};
    block.splash = {kRainSplashReach, static_cast<float>(kRainSplashLife), static_cast<float>(kRainSplashLeast),
                    kRainSplashSpread};
    constexpr std::size_t kRowZ = 2;
    constexpr std::size_t kRowW = 3;
    block.depth_projection = {
        core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
        core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
    const float span = core::at(view.depth_range, 1) - core::at(view.depth_range, 0);
    block.depth_map = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F,
                       plan.sampled_depth ? 1.0F : 0.0F, 0.0F};
    fill_precip(precip_map(state), fall_now(), &block.precip_fall, &block.precip_slant, &block.precip_near,
                &block.precip_far);
    block.ground = ground_now();
    block.ripple = ripple_now(own.clock_now);
    return block;
}

struct Bound {
    VkDescriptorBufferInfo block{};
    bool env = false;
    const Image* depth = nullptr;
    const Image* decals = nullptr;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
};

void push_descriptors(Backend& state, VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    RainState& own = rain_state();
    std::array<VkDescriptorImageInfo, kBindingCount> images{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* image,
                         const VkDescriptorBufferInfo* buffer) {
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pImageInfo = image;
        write.pBufferInfo = buffer;
    };
    add(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bound.block);
    core::at(images, kBindingMap) = precip_map(state).image; // (made at the view's beginning; untraced: never read)
    add(kBindingMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingMap), nullptr);
    const VkDescriptorBufferInfo drops{.buffer = own.drops.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    if (own.drops.buffer != VK_NULL_HANDLE) {
        add(kBindingDrops, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &drops);
    }
    // (the blurred frame; without one a blank texture - the shader reads env.x < 0 and the sky's light instead)
    core::at(images, kBindingEnv) = {.sampler = own.linear,
                                     .imageView = bound.env ? own.env.view : state.blank.view,
                                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    add(kBindingEnv, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingEnv), nullptr);
    if (bound.depth != nullptr) {
        core::at(images, kBindingDepth) = {.sampler = own.nearest,
                                           .imageView = bound.depth->view,
                                           .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingDepth), nullptr);
        const Image* const decals = bound.decals != nullptr ? bound.decals : bound.depth;
        core::at(images, kBindingDecals) = {.sampler = own.nearest,
                                            .imageView = decals->view,
                                            .imageLayout = bound.decals != nullptr
                                                               ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                               : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDecals, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &core::at(images, kBindingDecals), nullptr);
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &bound.scene};
    if (bound.scene != VK_NULL_HANDLE && own.scene_binding) {
        add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr);
        core::at(writes, count - 1).pNext = &scene;
    }
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

void push_part(VkCommandBuffer commands, uint32_t first)
{
    const std::array<uint32_t, 4> part{first, 0, 0, 0};
    vkCmdPushConstants(commands, rain_state().layout, kStages, 0, sizeof(part), part.data());
}

void begin_rendering(Backend& state, VkCommandBuffer commands, const ViewPlan& plan, const rt::ViewDrawn& view)
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
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (plan.motion) {
        Image& image = state.motion.image;
        Memory::transition(commands, &image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo& motion_info = core::at(colours, 1);
        motion_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        motion_info.imageView = image.attachment_view;
        motion_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        motion_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        motion_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        image.used = state.frames.serial();
    }
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_info.imageView = depth->attachment_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = std::clamp(core::at(view.depth_range, 0), 0.0F, 1.0F),
                              .maxDepth = std::clamp(core::at(view.depth_range, 1), 0.0F, 1.0F)};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    depth->used = serial;
}

void depth_to(VkCommandBuffer commands, Image* depth, bool read)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout layout =
        read ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = read ? VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}
                              : VkAccessFlags{VK_ACCESS_SHADER_READ_BIT},
        .dstAccessMask = read ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(commands,
                         read ? tests | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         read ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

} // namespace

// --- settings ---------------------------------------------------------------------------------------------------------

void set_rain_settings(const RainSettings& settings)
{
    rain::set_settings({.rate = static_cast<double>(settings.rate)});
    const rain::Settings clean = rain::settings();
    {
        const std::scoped_lock lock(backend().mutex);
        rain_state().settings = {.rate = static_cast<float>(clean.rate)};
    }
    // (kept while the ground is still wet after the rain stopped: it dries on the rain's clock)
    if (clean.rate > 0.0 || rain::stored() > 0.0) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RainSettings rain_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return rain_state().settings;
}

RainStats rain_stats() { return rain_state().stats; }

void begin_rain_view(Backend& state, const rt::ViewDrawn& view)
{
    RainState& own = rain_state();
    own.view = view;
    own.view_known = true;
    ++own.view_serial;
    if (!(own.settings.rate > 0.0F) && !(rain::stored() > 0.0)) {
        return;
    }
    const uint64_t frame = state.frames.serial();
    if (own.clock_frame != frame) { // (the frame's first view moves the rain's clock and drift on and traces the map)
        own.clock_frame = frame;
        own.clock_before = own.clock_now;
        own.drift_before = own.drift_now;
        own.clock_now = rain::clock();
        if (!own.clock_known || own.clock_now < own.clock_before) {
            own.clock_before = own.clock_now;
            own.drift_before = own.drift_now;
        }
        own.drift_now += drift_speed(own.clock_now) * (own.clock_now - own.clock_before);
        own.clock_known = true;
        VkCommandBuffer commands = outside_pass(state);
        stamp_time(state, commands, kMapStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        update_precip_map(state, view, fall_now().direction);
        commands = outside_pass(state);
        stamp_time(state, commands, kMapEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
}

void rain_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders, WetDescriptors* out)
{
    (void)call;
    *out = {};
    RainState& own = rain_state();
    if (!(own.settings.rate > 0.0F) && !(rain::stored() > 0.0)) {
        return; // (no rain, and the ground dry)
    }
    if (!own.view_known || !surface_trail::current_ground() || fvf_pre_transformed(block.vertex_format) ||
        shaders->fragment.water != 0U) {
        return;
    }
    ShaderKey with_wet = *shaders;
    with_wet.fragment.trails = static_cast<uint8_t>(with_wet.fragment.trails | kTrailWet);
    with_wet = normalised(with_wet);
    if ((with_wet.fragment.trails & kTrailWet) == 0U) {
        return;
    }
    std::string error;
    if (own.nearest == VK_NULL_HANDLE && !ensure_objects(state, &error)) {
        return;
    }
    const std::optional<VkDescriptorBufferInfo> uniforms = wet_block(state);
    if (!uniforms) {
        return;
    }
    const PrecipMap map = precip_map(state);
    if (map.image.imageView == VK_NULL_HANDLE) {
        return; // (the map not made yet: the view's beginning makes it)
    }
    *shaders = with_wet;
    out->on = true;
    out->block = *uniforms;
    out->map = map.image;
    ++own.stats.wet_draws;
    if (first_report(state, kReportFirstWet)) {
        get_log("vulkan: rain: the first draw wet (%s)", describe(with_wet.fragment).c_str());
    }
}

bool record_rain_wet_mask(Backend& state, const rt::ViewDrawn& view)
{
    RainState& own = rain_state();
    if (own.failed || (!(own.settings.rate > 0.0F) && !(rain::stored() > 0.0)) ||
        precip_map(state).image.imageView == VK_NULL_HANDLE) {
        return false;
    }
    ViewPlan plan;
    if (!plan_view(state, view, &plan) || !plan.sampled_depth || plan.colour->samples != VK_SAMPLE_COUNT_1_BIT) {
        return false; // (the reflections read the mask on single-sampled targets alone)
    }
    std::string error;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (ensure_objects(state, &error) && ensure_drops(state) && ensure_mask(state, *plan.colour)) {
        pipeline = draw_pipeline(state,
                                 {.kind = DrawKind::kMask,
                                  .format = kMaskFormat,
                                  .depth = VK_FORMAT_UNDEFINED,
                                  .samples = VK_SAMPLE_COUNT_1_BIT,
                                  .motion = false},
                                 &error);
    }
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rain: the passes couldn't be made (%s) - no rain",
                    error.empty() ? "its images" : error.c_str());
        }
        return false;
    }
    Block block = block_for(state, plan, view, false, false);
    const Image* const decals = scatter_wet_mask(state, view.rect);
    block.lamps = {0.0F, 0.0F, decals != nullptr ? 1.0F : 0.0F, 0.0F};
    const void* const bytes = &block;
    const std::optional<VkDescriptorBufferInfo> ring =
        in_ring(state, std::span(static_cast<const uint8_t*>(bytes), sizeof(block)));
    if (!ring) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: rain: no room in the ring for the pass's block - skipped");
        }
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    stamp_time(state, commands, kMaskStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    depth_to(commands, plan.depth, true);
    Memory::transition(commands, &own.mask, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo mask_info{};
    mask_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    mask_info.imageView = own.mask.attachment_view;
    mask_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    mask_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    mask_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    mask_info.clearValue = {};
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(plan.rect, 0)),
                                   .y = static_cast<int32_t>(core::at(plan.rect, 1))},
                        .extent = {.width = core::at(plan.rect, 2), .height = core::at(plan.rect, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &mask_info;
    vkCmdBeginRendering(commands, &info);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    push_descriptors(state, commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     {.block = *ring, .depth = plan.depth, .decals = decals});
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    Memory::transition(commands, &own.mask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    depth_to(commands, plan.depth, false);
    stamp_time(state, commands, kMaskEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    own.mask.used = serial;
    plan.depth->used = serial;
    own.mask_serial = serial;
    own.mask_rect = view.rect;
    ++own.stats.wet_masks;
    if (first_report(state, kReportFirstMask)) {
        get_log("vulkan: rain: the first wet mask drawn (%ux%u%s)", core::at(plan.rect, 2), core::at(plan.rect, 3),
                decals != nullptr ? ", over the decals' puddles" : "");
    }
    return true;
}

const Image* rain_wet_mask(const Backend& state, const std::array<uint32_t, 4>& rect)
{
    const RainState& own = rain_state();
    if (own.mask.image == VK_NULL_HANDLE || own.mask_serial == 0 || own.mask_serial != state.frames.serial() ||
        own.mask_rect != rect) {
        return nullptr;
    }
    return &own.mask;
}

bool record_rain_air(Backend& state, const rt::ViewDrawn& view)
{
    RainState& own = rain_state();
    if (own.failed || !(own.settings.rate > 0.0F) || precip_map(state).image.imageView == VK_NULL_HANDLE) {
        return false;
    }
    ViewPlan plan;
    if (!plan_view(state, view, &plan)) {
        return false;
    }
    std::string error;
    VkPipeline drops_pipeline = VK_NULL_HANDLE;
    VkPipeline splash_pipeline = VK_NULL_HANDLE;
    VkPipeline motion_pipeline = VK_NULL_HANDLE;
    const auto wanted = [&plan](DrawKind kind) {
        return DrawPipeline{.kind = kind,
                            .format = plan.colour->format,
                            .depth = plan.depth->format,
                            .samples = plan.colour->samples,
                            .motion = plan.motion};
    };
    if (ensure_objects(state, &error)) {
        if (!ensure_drops(state)) {
            error = "the drops' buffer";
            if (first_report(state, kReportBuffer)) {
                get_log("vulkan: rain: the drops' buffer couldn't be made - no drops");
            }
        } else {
            drops_pipeline = draw_pipeline(state, wanted(DrawKind::kDrops), &error);
            splash_pipeline = draw_pipeline(state, wanted(DrawKind::kSplashes), &error);
            motion_pipeline =
                plan.motion ? draw_pipeline(state, wanted(DrawKind::kDropMotion), &error) : VK_NULL_HANDLE;
        }
    }
    if (drops_pipeline == VK_NULL_HANDLE || splash_pipeline == VK_NULL_HANDLE ||
        (plan.motion && motion_pipeline == VK_NULL_HANDLE)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rain: the passes couldn't be made (%s) - no rain", error.c_str());
        }
        return false;
    }
    // the frame blurred behind the drops (single-sampled, copyable targets)
    const bool env = plan.colour->samples == VK_SAMPLE_COUNT_1_BIT &&
                     (plan.colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 && ensure_env(state, *plan.colour);
    const SceneView scene = own.scene_binding ? current_scene(state) : SceneView{};
    const bool traced = scene.tlas != VK_NULL_HANDLE && scene.instances > 0;
    const Block block = block_for(state, plan, view, env);
    const void* const bytes = &block;
    const std::optional<VkDescriptorBufferInfo> ring =
        in_ring(state, std::span(static_cast<const uint8_t*>(bytes), sizeof(block)));
    if (!ring) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: rain: no room in the ring for the pass's block - skipped");
        }
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    stamp_time(state, commands, kAirStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (env) {
        blur_frame(commands, plan.colour, plan.rect);
    }
    // the drops and splashes placed and lit
    const VkMemoryBarrier before_place{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                       .pNext = nullptr,
                                       .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                       .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &before_place, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.place, traced ? 1 : 0));
    push_descriptors(state, commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                     {.block = *ring, .env = env, .scene = traced ? scene.tlas : VK_NULL_HANDLE});
    vkCmdDispatch(commands, (kTotalInstances + kComputeGroup - 1) / kComputeGroup, 1, 1);
    const VkMemoryBarrier placed{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                 .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1,
                         &placed, 0, nullptr, 0, nullptr);
    begin_rendering(state, commands, plan, view);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, drops_pipeline);
    push_descriptors(state, commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *ring});
    push_part(commands, 0);
    vkCmdDraw(commands, kStripVertices, kTotalDrops, 0, 0);
    if (motion_pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, motion_pipeline);
        push_descriptors(state, commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *ring});
        push_part(commands, 0);
        vkCmdDraw(commands, kStripVertices, kTotalDrops, 0, 0);
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, splash_pipeline);
    push_descriptors(state, commands, VK_PIPELINE_BIND_POINT_GRAPHICS, {.block = *ring});
    push_part(commands, kTotalDrops);
    vkCmdDraw(commands, kStripVertices, kRainSplashes, 0, 0);
    vkCmdEndRendering(commands);
    stamp_time(state, commands, kAirEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    if (env) {
        own.env.used = serial;
    }
    own.stats.last_drops = kTotalDrops;
    own.stats.last_splashes = kRainSplashes;
    own.stats.last_traced = traced;
    ++own.stats.air_passes;
    if (first_report(state, kReportFirstAir)) {
        get_log("vulkan: rain: the first rain in the air (%.1f mm/h: %u drops - each near one %.1f drops, far %.1f -, "
                "%u splashes "
                "x %.2f%s%s%s, %u lamps; the occlusion map %s)",
                static_cast<double>(own.settings.rate), kTotalDrops, static_cast<double>(own.stats.near_weight),
                static_cast<double>(own.stats.far_weight), kRainSplashes, static_cast<double>(own.stats.splash_weight),
                traced ? ", traced" : "", plan.motion ? ", their own motion" : "", env ? ", the frame behind them" : "",
                own.stats.last_lamps, precip_map(state).on ? "on" : "off");
    }
    return true;
}

bool make_rain_passes(Backend& state, VkFormat colour, VkFormat depth, bool motion, std::string* error)
{
    std::string why;
    bool made = ensure_objects(state, &why);
    for (const DrawKind kind : {DrawKind::kDrops, DrawKind::kSplashes, DrawKind::kDropMotion, DrawKind::kMask}) {
        if (!made || (kind == DrawKind::kDropMotion && !motion)) {
            continue;
        }
        const bool mask = kind == DrawKind::kMask;
        made = draw_pipeline(state,
                             {.kind = kind,
                              .format = mask ? kMaskFormat : colour,
                              .depth = mask ? VK_FORMAT_UNDEFINED : depth,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .motion = mask ? false : motion},
                             &why) != VK_NULL_HANDLE;
    }
    if (error != nullptr) {
        *error = why;
    }
    return made;
}

bool read_rain_drops(Backend& state, std::vector<std::array<float, 4>>* drops)
{
    RainState& own = rain_state();
    drops->clear();
    if (own.drops.buffer == VK_NULL_HANDLE || !finish_work(state)) {
        return false;
    }
    std::vector<uint8_t> raw;
    if (!state.memory.read_buffer(own.drops, 0, static_cast<VkDeviceSize>(kTotalInstances) * kDropBytes, &raw)) {
        return false;
    }
    drops->resize(kTotalInstances);
    for (uint32_t i = 0; i < kTotalInstances; ++i) {
        std::array<float, 12> words{};
        std::memcpy(words.data(), raw.data() + (static_cast<std::size_t>(i) * kDropBytes), kDropBytes);
        constexpr std::size_t kVisibleWord = 11;
        core::at(*drops, i) = {core::at(words, 0), core::at(words, 1), core::at(words, 2),
                               core::at(words, kVisibleWord) > 0.5F ? 1.0F : 0.0F};
    }
    return true;
}

void release_rain(Backend& state)
{
    RainState& own = rain_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const DrawPipeline& made : own.draws) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (VkPipeline pipeline : own.place) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        std::vector<VkShaderModule> modules{own.mask_vertex, own.mask_fragment};
        modules.insert(modules.end(), own.drop_vertex.begin(), own.drop_vertex.end());
        modules.insert(modules.end(), own.drop_fragment.begin(), own.drop_fragment.end());
        for (VkShaderModule module : modules) {
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
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
        for (VkSampler sampler : {own.nearest, own.linear}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.env);
        state.memory.destroy_image(&own.mask);
        state.memory.destroy_buffer(&own.drops);
    }
    const RainSettings settings = own.settings;
    own = RainState{};
    own.settings = settings;
}

// --- a drop's place (the compute shader's twin: the check's) ------------------------------------------------------------

namespace {

uint32_t drop_hash(uint32_t x)
{
    constexpr uint32_t kFirst = 0x7FEB352DU;
    constexpr uint32_t kSecond = 0x846CA68BU;
    x ^= x >> 16U;
    x *= kFirst;
    x ^= x >> 15U;
    x *= kSecond;
    x ^= x >> 16U;
    return x;
}

float drop_unit(uint32_t x)
{
    constexpr float kScale = 16777216.0F;
    return static_cast<float>(x >> 8U) / kScale;
}

float glsl_mod(float x, float y) { return x - (y * std::floor(x / y)); }

} // namespace

DropPlace drop_place(uint32_t index, const RainClock& clock, const std::array<float, 3>& camera, double rate,
                     const std::array<float, 2>& downwind)
{
    const bool in_near = index < kRainNearDrops;
    const float box = in_near ? kRainNearBox : kRainFarBox;
    const float height = in_near ? kRainNearHeight : kRainFarHeight;
    const auto drift = static_cast<float>(std::fmod(clock.drift, static_cast<double>(box)));
    const auto hashed = [index](uint32_t k) { return drop_unit(drop_hash((index * 5U) + k)); };
    const float hx = hashed(1);
    const float hy = hashed(2);
    const float hz = hashed(3);
    const float hd = hashed(4);
    const auto slope = static_cast<float>(rain::size_slope(rate));
    const float least = static_cast<float>(kRainLeastDrawn);
    const auto most = static_cast<float>(rain::kMostDiameter);
    const float share = 1.0F - std::exp(-slope * (most - least));
    const float diameter = std::min(least - (std::log(1.0F - (hd * share)) / slope), most);
    const float speed =
        std::max(9.65F - (10.3F * std::exp(-0.6F * diameter)), 0.0F) * static_cast<float>(rain::kUnitsPerMetre);
    const auto t = static_cast<float>(std::fmod(clock.clock, kClockWraps));
    const std::array<float, 3> raw{(hx * box) + (core::at(downwind, 0) * drift), (hy * height) - (speed * t),
                                   (hz * box) + (core::at(downwind, 1) * drift)};
    const std::array<float, 3> low{core::at(camera, 0) - (kHalf * box), core::at(camera, 1) - (0.6F * height),
                                   core::at(camera, 2) - (kHalf * box)};
    const std::array<float, 3> size{box, height, box};
    DropPlace place;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(place.world, axis) =
            core::at(low, axis) + glsl_mod(core::at(raw, axis) - core::at(low, axis), core::at(size, axis));
    }
    place.diameter = diameter;
    place.speed = speed;
    place.in_near_box = in_near;
    if (!in_near) {
        const float dx = core::at(place.world, 0) - core::at(camera, 0);
        const float dy = core::at(place.world, 1) - core::at(camera, 1);
        const float dz = core::at(place.world, 2) - core::at(camera, 2);
        place.drawn = !(std::abs(dx) < kHalf * kRainNearBox && std::abs(dz) < kHalf * kRainNearBox &&
                        dy > -0.6F * kRainNearHeight && dy < 0.4F * kRainNearHeight);
    }
    return place;
}

} // namespace render::vulkan
