// NOT ORIGINAL - volumetric fog and light shafts (vulkan_rt_fog.h; docs/design/vulkan.md "Volumetric fog").
#include "gpu_vulkan/vulkan_rt_fog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_history.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoRayTracing = 0x52460001U;
constexpr uint64_t kReportNoScene = 0x52460002U;
constexpr uint64_t kReportTargets = 0x52460003U;
constexpr uint64_t kReportPipeline = 0x52460004U;
constexpr uint64_t kReportFirstPass = 0x52460005U;
constexpr uint64_t kReportImages = 0x52460006U;
constexpr uint64_t kReportNoFog = 0x52460007U;
constexpr uint64_t kReportNoRoom = 0x52460008U;
constexpr uint64_t kReportSeeded = 0x52460009U; // (added by the first frame: a history seeded - vulkan_rt_history.h)

constexpr uint64_t kReportLight = 0x5246000AU; // (added by the volumetric light: what lights the air)
constexpr uint64_t kReportRays = 0x5246000BU; // (added by the god rays: their passes not made)

constexpr float kMaxDensity = 8.0F;
constexpr float kMaxHeight = 1.0e6F;
constexpr unsigned kMinSteps = 4;
constexpr unsigned kMaxSteps = 128;
constexpr double kGoldenRatio = 0.6180339887498949; // the march's jitter advances by this a frame
constexpr uint32_t kGroupSide = 8;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr double kSingular = 1e-300;
// (changed by the street air: the march's block 2 x 2 or 4 x 4 - rt_fog_quality) the pixel of each block marched
// cycles through the block's pixels frame by frame in the order of their ordered-dither (Bayer) index, each next one far
// from the last: (0, 0), (1, 1), (1, 0), (0, 1) in a 2 x 2 block
constexpr std::array<std::array<uint8_t, 4>, 4> kBayer4{{{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}}};
constexpr std::array<std::array<uint8_t, 2>, 2> kBayer2{{{0, 2}, {3, 1}}};
constexpr uint32_t kLeastScale = 2;
constexpr uint32_t kMostScale = 4;
constexpr unsigned kLeastAirMap = 64; // (the air's sun map's side, texels)
constexpr double kAirMapUpright = 0.9; // (a light this near straight up: the map's axes from the x axis)
constexpr unsigned kMostAirMap = 2048;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// What every pass shares: the block (FogBlock below), the depth and the view rays.
constexpr const char* kCommon = R"(
layout(set = 0, binding = 7) uniform FogBlock {
    mat4 to_world;      // the target's pixel (x, y, depth, 1) -> world
    mat4 to_before;     // world -> the target's pixel in the viewport's frame before (homogeneous)
    vec4 camera;        // xyz
    vec4 camera_before; // xyz; w 1: there is a history
    vec4 sun;           // xyz toward the light (the sun, or the moon at night - rt::medium_light); w the aerosol's g
    vec4 sun_tint;      // rgb its colour over the level's own sun's (the aerial march: its irradiance in the sky's unit,
                        // linear); w 1: there is a light
    vec4 fog_colour;    // rgb; w the density (x the level's)
    vec4 fog_range;     // x start, y end, z 1 / the height falloff (0: even), w the frame's jitter (0 - 1)
    vec4 offsets;       // xy depth sample 0 from the pixel's centre; zw the pixel of its block this frame marches
    vec4 shares;        // x ambient, y sky, z shafts (mode 1), w unused (round F: was rt_hdr's clamp)
    ivec4 rect;         // the viewport in the target's pixels
    ivec4 low;          // the low-resolution images' width and height, the scale, the steps
    ivec4 flags;        // x the mode (2: aerial), y the mask, z the longest history, w the frame's index
    vec4 aerial;        // (the aerial march - changed by the physical air) x the air's 1 / height (Rayleigh's, per world
                        // unit), y the level's ground's y, z the aerosol's 1 / height, w the mist's density over it
    vec4 sun_light;     // w how far the aerial march follows the air (rgb unused)
    vec4 medium;        // rgb the light the medium sends the eye per unit of light (phase 1); the aerial march: the air's
                        // Rayleigh scattering per world unit at the ground
    vec4 soft;          // x tan of the light's angular radius, y the water layer's extinction over the haze's at the
                        // ground, z 1 / its height (world units), w (added by the low sun's glare) the even lobe of
                        // the street air's phase for the sun's or moon's light, x the look's glow (rt_volume_light.h
                        // air_sun_phase: its forward lobe stays the physical one)
    vec4 cloud_map;     // the clouds' shadow map's place (vulkan_rt_sky_pass.h CloudShadowMap)
    vec4 cloud_light;   // xyz toward its light, w its shadow's share kept (0: no map)
    vec4 air;           // (the street air - rt_volume_light.h street_air) x its extinction per unit at its ground, y 1 /
                        // its height, z its ground's y, w the falling snow's extinction per unit
    vec4 air_light;     // x its light per unit of light (phase 1), y x its shade's light, z the even lobe added to its
                        // phase (rt_volume_light.h AirArt: the air's look), w 1 / the snow's height
    vec4 air_ambient;   // rgb its shade's light where the level's fog is replaced (the fog's colour's share; the aerial
                        // march: the sky's), w the sun's brightness over its tint's (the tint is a hue over a level's fog)
    vec4 air_look;      // (changed by the physical air) x 1: the sky's pixels left alone (the atmosphere drawn holds the
                        // whole ray - the aerial march), y the air's extinction at the eye's height (the aerial march:
                        // the god rays' share before what stands toward the light), z how far the march's near steps
                        // reach (the air's sun map's reach; 0: t^2 over the whole ray)
    // (added by the distinct shafts - rt_volume_light.h air_map_half) the air's sun map: its square across the light's
    // way - xyz its side's and its up's axes, the side's w its half side (world units; 0: no map), the up's w how far
    // below its centre's plane its rays reach; the centre's xyz the eye's place snapped to its texels, w how far up the
    // light's way its rays start
    vec4 map_side;
    vec4 map_up;
    vec4 map_centre;
    // (added by the god rays - rt_volume_light.h god_ray_peak) xy the light's place on the screen in the rays' images'
    // texels (half the viewport's pixels), z rt_fog_god_rays, w 0: off (the march keeps the whole phase), 1: on, the
    // light in front of the eye (the radial passes run), 2: on, the light behind it (its peak nowhere on the screen)
    vec4 rays;
    // (added by the physical air - the aerial march; rt_sky.h Aerosol) the weather's aerosol per world unit at the
    // ground: its scattering (rgb, w the mist's 1 / height), its extinction (rgb, w the falling rain's or snow's
    // extinction), the light scattered more than once per unit of scattering (rgb: Psi at the ground x the light's
    // irradiance, the sky's unit; w the precipitation's 1 / height)
    vec4 aerosol_s;
    vec4 aerosol_t;
    vec4 ambient;
} fog;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// (changed by the physical air) the aerosol's phase, Cornette-Shanks (per steradian - rt_sky.h aerosol_phase)
float cs(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1e-6);
    return 0.119366207 * (1.0 - g * g) * (1.0 + c * c) / ((2.0 + g * g) * k * sqrt(k));
}
// the air's phase x 4 pi (rt_volume_light.h medium_phase: 1 on average over the sphere)
float phase(float c) { return 12.5663706 * cs(c, fog.sun.w); }
// (added by the god rays) the forward peak the screen's shadows resolve (rt_volume_light.h god_ray_peak, x 4 pi): 0
// while they are off
float god_peak(float c)
{
    if (!(fog.rays.w > 0.5)) {
        return 0.0;
    }
    return 12.5663706 * max(cs(c, fog.sun.w) - cs(GOD_RAYS_CONE_COS, fog.sun.w), 0.0);
}

#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif

bool inside_low(ivec2 q) { return all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, fog.low.xy)); }

float depth_at(ivec2 p) { return texelFetch(u_depth, p, 0).r; }

// (a multisampled depth is read at sample 0, which isn't at the pixel's centre: the point is where the sample is)
vec3 world_at(ivec2 p, float d)
{
    vec4 w = fog.to_world * vec4(vec2(p) + fog.offsets.xy, d, 1.0);
    return w.xyz / w.w;
}

// The view ray through the target's pixel p: its direction, and how far it goes to the depth's surface (-1: the sky).
vec3 direction_of(ivec2 p)
{
    vec3 to = world_at(p, 0.5) - fog.camera.xyz;
    float length_of = length(to);
    return length_of > 1e-12 ? to / length_of : vec3(0.0, 0.0, -1.0);
}
float distance_of(ivec2 p)
{
    float d = depth_at(p);
    return d >= 1.0 ? -1.0 : length(world_at(p, d) - fog.camera.xyz);
}
)";

// Step 1: the march (one texel of the low-resolution images each).
constexpr const char* kMarchBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32f) uniform writeonly image2D o_info;
// (round W2) the god rays' potential under the march's own shadow (the sun's rays, the air's sun map, the clouds')
layout(set = 0, binding = 16, r32f) uniform writeonly image2D o_lit;
// (the lamps that light the air: rt_lights' lit set for the view, the most important first)
layout(set = 0, binding = 10, std140) uniform FogLamps {
    ivec4 count;                // x lamps
    vec4 at[FOG_MOST_LAMPS];     // xyz the lamp, w its size (its shadow rays stop that short of it)
    vec4 colour[FOG_MOST_LAMPS]; // rgb its light (colour x intensity x rt_lights' strength x the lamps on), w its reach
    vec4 more[FOG_MOST_LAMPS];   // x its falloff's core
} lamps;
// (added by the distinct shafts) the air's sun map (step 0's): each texel the distance down the light's way to the first
// thing its ray hits
layout(set = 0, binding = 13, r32f) uniform readonly image2D u_air_map;

// Whether the light gets from `at` along `toward` for `reach` (the world and the objects; with CUTOUTS the cut-outs by
// their alpha too - the one lighting path's test, vulkan_rt_scene.h cutout_rays_glsl).
float visible(vec3 at, vec3 toward, float reach)
{
#ifdef CUTOUTS
    return ray_blocked(uint(fog.flags.y) & 0xFFu, at, toward, reach) ? 0.0 : 1.0;
#else
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          uint(fog.flags.y) & 0xFFu, at, 0.0, toward, reach);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
#endif
}

// interleaved gradient noise (the viewport's pixel): where in each segment the light is sampled
float jitter_at(ivec2 p) { return fract(52.9829189 * fract(0.06711056 * float(p.x) + 0.00583715 * float(p.y))); }

// a number in [0, 1) for the pixel, the frame and the step (the soft light's point on its disc, the lamp picked)
float hash01(ivec2 p, int frame, int i, uint salt)
{
    uint h = uint(p.x) * 0x8DA6B343u ^ uint(p.y) * 0xD8163841u ^ uint(frame) * 0xCB1AB31Fu ^ uint(i) * 0x165667B1u ^ salt;
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;
    return float(h >> 8u) / 16777216.0;
}


// The light's way from `at`: toward a point on its disc (the sun's, or the larger moon's: soft shafts' edges)
vec3 toward_light(ivec2 p, int i)
{
    vec3 l = fog.sun.xyz;
    if (!(fog.soft.x > 0.0)) {
        return l;
    }
    vec3 side = normalize(abs(l.y) < 0.9 ? cross(l, vec3(0.0, 1.0, 0.0)) : cross(l, vec3(1.0, 0.0, 0.0)));
    vec3 up = cross(side, l);
    float r = sqrt(hash01(p, fog.flags.w, i, 0x51u)) * fog.soft.x;
    float a = 6.2831853 * hash01(p, fog.flags.w, i, 0xA7u);
    return normalize(l + side * (r * cos(a)) + up * (r * sin(a)));
}

// How much of the sun's or the moon's light reaches `at` (its shadow ray, the clouds' shadow map): 0 - 1
float light_seen(vec3 at, ivec2 p, int i)
{
    if (!(fog.sun_tint.w > 0.5)) {
        return 0.0;
    }
    float through = 1.0;
#ifdef CLOUDS
    through = cloud_map_kept(at, fog.cloud_map, fog.cloud_light);
#endif
    if (!(through > 0.0)) {
        return 0.0;
    }
    return visible(at, toward_light(p, i), SUN_DISTANCE) * through;
}

// (added by the indoor air) How much of the sky the air at `at` sees: a ray toward a point of the upper hemisphere,
// even in solid angle (cos theta uniform), a fresh one each pair, pixel and frame - the history averages them into the
// open share of the sky round the point. The light scattered more than once (Psi) and the sky's own light are the sky's:
// they reach the air only through what is open above it - none in a closed room, all of it in the open.
float sky_seen(vec3 at, ivec2 p, int i)
{
    float z = hash01(p, fog.flags.w, i, 0x3Bu);
    float a = 6.2831853 * hash01(p, fog.flags.w, i, 0xC5u);
    float r = sqrt(max(1.0 - z * z, 0.0));
    return visible(at, vec3(r * cos(a), z, r * sin(a)), SUN_DISTANCE);
}

// (round W2) The same for the street air's shade light: four rays stratified over the hemisphere (its two bands of
// height and two halves round) - the street air's light is mostly its shade's at dusk, so its share is sampled four times
// as finely as the aerial march's.
float sky_seen_stratified(vec3 at, ivec2 p, int i)
{
    float open = 0.0;
    for (int k = 0; k < 4; ++k) {
        float z = (float(k & 1) + hash01(p, fog.flags.w, i, 0x3Bu + uint(k))) * 0.5;
        float a = 3.14159265 * (float(k >> 1) + hash01(p, fog.flags.w, i, 0xC5u + uint(k)));
        float r = sqrt(max(1.0 - z * z, 0.0));
        open += visible(at, vec3(r * cos(a), z, r * sin(a)), SUN_DISTANCE);
    }
    return open * 0.25;
}

// (added by the distinct shafts - rt_volume_light.h air_map_half) How much of the light reaches `at` by the air's sun
// map: -1 where `at` is outside its square (or there is none) - the light's ray is traced there instead. Its penumbra:
// the place read moved across the light's disc (a point of it a step - the history averages them) by the distance from
// the nearest thing round there that shadows it x the disc's tangent; then the four texels round it, bilinearly - lit
// where the point lies before their first hit.
float map_texel(ivec2 t, ivec2 size) { return imageLoad(u_air_map, clamp(t, ivec2(0), size - ivec2(1))).r; }
float map_light_seen(vec3 at, ivec2 p, int i)
{
    float half_side = fog.map_side.w;
    if (!(half_side > 0.0)) {
        return -1.0;
    }
    vec3 rel = at - fog.map_centre.xyz;
    vec2 uv = vec2(dot(rel, fog.map_side.xyz), dot(rel, fog.map_up.xyz));
    if (any(greaterThan(abs(uv), vec2(half_side)))) {
        return -1.0;
    }
    if (!(fog.sun_tint.w > 0.5)) {
        return 0.0;
    }
    float through = 1.0;
#ifdef CLOUDS
    through = cloud_map_kept(at, fog.cloud_map, fog.cloud_light);
#endif
    if (!(through > 0.0)) {
        return 0.0;
    }
    ivec2 size = imageSize(u_air_map);
    float texel = 2.0 * half_side / float(size.x);
    float depth = fog.map_centre.w - dot(rel, fog.sun.xyz); // (down the light's way from the map's plane)
    vec2 f = (uv + half_side) / texel - 0.5;
    ivec2 b = ivec2(floor(f));
    float nearest = min(min(map_texel(b, size), map_texel(b + ivec2(1, 0), size)),
                        min(map_texel(b + ivec2(0, 1), size), map_texel(b + ivec2(1, 1), size)));
    float r = min(max(depth - nearest, 0.0) * fog.soft.x / texel, 8.0);
    if (r > 0.0) {
        float a = 6.2831853 * hash01(p, fog.flags.w, i, 0x5Du);
        f += vec2(cos(a), sin(a)) * (sqrt(hash01(p, fog.flags.w, i, 0x77u)) * r);
        b = ivec2(floor(f));
    }
    vec2 w = f - vec2(b);
    float bias = 2.0 * texel; // (a slope's texel)
    float l00 = depth < map_texel(b, size) + bias ? 1.0 : 0.0;
    float l10 = depth < map_texel(b + ivec2(1, 0), size) + bias ? 1.0 : 0.0;
    float l01 = depth < map_texel(b + ivec2(0, 1), size) + bias ? 1.0 : 0.0;
    float l11 = depth < map_texel(b + ivec2(1, 1), size) + bias ? 1.0 : 0.0;
    return mix(mix(l00, l10, w.x), mix(l01, l11, w.x), w.y) * through;
}

// a lamp's falloff (engine/gfx/rt_lights.h light_falloff - the same formula as the lamps' pass)
float lamp_falloff(float d, float core, float reach)
{
    if (!(d < reach) || !(core > 0.0)) {
        return 0.0;
    }
    float x = d / reach;
    float x2 = x * x;
    float window = 1.0 - x2 * x2;
    return window * window * core * core / (core * core + d * d);
}

// (changed by the street air: lamps culled by their reach) The lamps whose reach the view ray from the camera along
// `direction` enters before `b`: a bit each.
uint lamps_reached(vec3 direction, float b)
{
    uint mask = 0u;
    int count = min(lamps.count.x, FOG_MOST_LAMPS);
    for (int k = 0; k < count; ++k) {
        vec3 to = lamps.at[k].xyz - fog.camera.xyz;
        float reach = lamps.colour[k].w;
        float along = dot(to, direction);
        float aside = dot(to, to) - along * along;
        if (aside < reach * reach && along + reach > 0.0 && along - reach < b) {
            mask |= 1u << uint(k);
        }
    }
    return mask;
}

// The lamps' light at `at` toward the eye, unshadowed (x the aerosol's phase x 4 pi): the lamps of `mask`; (changed by
// the physical air) `rayleigh` the same light x Rayleigh's phase x 4 pi
vec3 lamps_open(vec3 at, vec3 direction, uint mask, out vec3 rayleigh)
{
    vec3 sum = vec3(0.0);
    rayleigh = vec3(0.0);
    uint left = mask;
    while (left != 0u) {
        int k = findLSB(left);
        left &= left - 1u;
        vec3 to = lamps.at[k].xyz - at;
        float d = length(to);
        float f = lamp_falloff(d, lamps.more[k].x, lamps.colour[k].w);
        if (f > 0.0) {
            float c = dot(direction, to / max(d, 1e-4));
            sum += lamps.colour[k].rgb * (f * phase(c));
            rayleigh += lamps.colour[k].rgb * (f * 0.75 * (1.0 + c * c));
        }
    }
    return sum;
}

// Whether the lamps' light reaches `at`: one lamp of `mask` picked by its share of the light there (one random number,
// streamed over the lamps) and its shadow ray traced - standing for all of them (exact for one lamp; the history
// averages the picks). The cones and pools of light a lamp's fixture and the walls round it cut in the air.
float lamps_seen(vec3 at, vec3 direction, uint mask, ivec2 p, int i)
{
    float u = hash01(p, fog.flags.w, i, 0x3Cu);
    float total = 0.0;
    int picked = -1;
    vec3 picked_to = vec3(0.0);
    float picked_d = 0.0;
    uint left = mask;
    while (left != 0u) {
        int k = findLSB(left);
        left &= left - 1u;
        vec3 to = lamps.at[k].xyz - at;
        float d = length(to);
        float f = lamp_falloff(d, lamps.more[k].x, lamps.colour[k].w);
        float w = f > 0.0 ? dot(lamps.colour[k].rgb, vec3(0.2126, 0.7152, 0.0722)) * f *
                                phase(dot(direction, to / max(d, 1e-4)))
                          : 0.0;
        if (!(w > 0.0)) {
            continue;
        }
        total += w;
        float share = w / total;
        if (u < share) {
            picked = k;
            picked_to = to;
            picked_d = d;
            u /= share;
        } else {
            u = (u - share) / (1.0 - share);
        }
    }
    if (picked < 0) {
        return 0.0;
    }
    return visible(at, picked_to / max(picked_d, 1e-4), max(picked_d - lamps.at[picked].w, 0.0));
}

// The optical depth of a layer between s0 and s1 along the view ray (y linear along it): extinction `sigma` at height
// `ground`, thinning by e every 1 / inv_h above it, `sigma` at and below it (thicker toward the ground, never past the
// ground's) - in closed form: the part below the ground at the ground's extinction, the part above it through the
// exponential.
float layer_depth(vec3 direction, float s0, float s1, float sigma, float inv_h, float ground)
{
    float u0 = fog.camera.y - ground; // the camera's height above the ground
    float dy = direction.y;
    float lo = s0; // [lo, hi]: the part above the ground
    float hi = s1;
    float below = 0.0;
    if (inv_h > 0.0) {
        if (abs(dy) < 1e-12) {
            if (u0 <= 0.0) {
                below = s1 - s0;
                hi = lo;
            }
        } else {
            float cut = clamp(-u0 / dy, s0, s1); // where the ray crosses the ground's height
            if (dy > 0.0) {
                below = cut - s0;
                lo = cut;
            } else {
                below = s1 - cut;
                hi = cut;
            }
        }
    }
    float depth = sigma * below;
    if (hi > lo) {
        float k = dy * inv_h;
        float e_lo = exp(-max(u0 + dy * lo, 0.0) * inv_h);
        float e_hi = exp(-max(u0 + dy * hi, 0.0) * inv_h);
        if (abs(k) * (hi - lo) < 1e-4) {
            depth += sigma * exp(-max(u0 + dy * 0.5 * (lo + hi), 0.0) * inv_h) * (hi - lo);
        } else {
            depth += sigma * (e_lo - e_hi) / k;
        }
    }
    return depth;
}

// (water: the view ray's surface found by a ray from the camera - CUTOUTS' material table: water's reflection class)
// The water's surface height where the view ray ends on water; a large negative number elsewhere.
float water_under(vec3 direction, float distance)
{
#ifdef CUTOUTS
    if (distance < 0.0 || !(fog.soft.y > 0.0)) {
        return -3.0e38;
    }
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, 0x07u, fog.camera.xyz, 0.0, direction,
                          distance * 1.05 + 1.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return -3.0e38;
    }
    float t = rayQueryGetIntersectionTEXT(query, true);
    if (abs(t - distance) > 1.0 + 0.02 * distance) {
        return -3.0e38;
    }
    uint id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
    if (material(id).reflection_class != WATER_CLASS) {
        return -3.0e38;
    }
    return fog.camera.y + direction.y * t;
#else
    return -3.0e38;
#endif
}

// (added by the street air) The street air's optical depth between s0 and s1: the weather's air over the level's
// ground and the falling rain or snow (changed by the physical air: one layer of the weather's aerosol - rt_volume_light.h
// street_air -, no wetter layer over water, no volume round the eye).
float air_depth(vec3 direction, float s0, float s1, float water)
{
    float depth = 0.0;
    if (fog.air.x > 0.0) {
        depth += layer_depth(direction, s0, s1, fog.air.x, fog.air.y, fog.air.z);
    }
    if (fog.air.w > 0.0) {
        depth += layer_depth(direction, s0, s1, fog.air.w, fog.air_light.w, fog.air.z);
    }
    return depth;
}

// (added by the cinematic air) Where along the ray the march's x-th step boundary lies (x in 0 - steps): a + (b - a) t^2,
// or with `near_steps` steps for the volume round the eye, those over a - split (t^2) and the rest over split - b.
// (Changed by the distinct shafts:) the rest in a geometric series - each segment the same share longer than the one
// before -, so the first kilometres past the volume (the cloud gaps' columns) get as many steps as the far haze.
float march_at(float x, float a, float b, float split, int near_steps, int steps)
{
    if (near_steps <= 0) {
        float t = x / float(steps);
        return a + (b - a) * t * t;
    }
    if (x <= float(near_steps)) {
        float t = x / float(near_steps);
        return a + (split - a) * t * t;
    }
    float t = (x - float(near_steps)) / float(steps - near_steps);
    return split * pow(b / split, t);
}

#ifdef AERIAL
// The sky's pixels are the world past the haze's reach (the sky dome's painted horizon - the sea, the hills - goes on
// where the level's meshes end): marched as a surface at the far plane along the ray (where the camera clips the meshes
// - AU's sea is cut there, triangle by triangle) or the haze's end (sun_light.w), the nearer - so a mesh cut at the far
// plane meets the sky with the same haze, no seam.
float far_along(ivec2 p)
{
    vec4 w = fog.to_world * vec4(vec2(p) + fog.offsets.xy, 1.0, 1.0);
    if (!(abs(w.w) > 1e-30)) {
        return fog.sun_light.w;
    }
    float far = length(w.xyz / w.w - fog.camera.xyz);
    return far > 0.0 && far < 3.0e38 ? far : fog.sun_light.w;
}

// (rewritten by the physical air) The aerial march: the atmosphere's own air - the weather's aerosol and the air's
// molecules, the coefficients the sky is made of (rt_sky.h; the block's medium, aerosol_s / aerosol_t, aerial) - and the
// rain or snow falling near the ground, from the camera to the surface (the sky's pixels: nothing where the atmosphere
// is drawn - its sky-view table holds the whole ray -; over the level's painted sky dome its far world at the air's
// reach). Linear radiance in the sky's unit, what a path tracer's single-scattering estimate of the same medium gives:
//   the light's: its irradiance (sun_tint) x its visibility (the shadow ray, the air's sun map, the clouds' map) x
//     (Rayleigh's scattering x its phase + the aerosol's x Cornette-Shanks' - the god rays' peak taken out);
//   every higher order: the scattering x Psi (ambient - the atmosphere's multiple-scattering transfer at the ground) x
//     the sky's open share at the point (sky_seen - added by the indoor air: Psi is the sky's light; a closed room's air
//     gets none of it);
//   the lamps': their irradiance (pi x the lamps' light) x the phases;
// each segment's integral exact for a medium even along it - its source x (1 - e^-tau) / tau -, T x e^-tau (tau the
// extinction's luma: one transmittance for the three channels).
void march_aerial(ivec2 q, ivec2 local, vec3 direction, float distance)
{
    bool sky = distance < 0.0;
    if (sky && fog.air_look.x > 0.5) {
        imageStore(o_out, q, vec4(0.0, 0.0, 0.0, 1.0));
        imageStore(o_info, q, vec4(distance, float(local.x), float(local.y), 0.0));
        imageStore(o_lit, q, vec4(0.0));
        return;
    }
    float a = 0.0;
    float b = min(sky ? far_along(fog.rect.xy + local) : distance, fog.sun_light.w);
    vec3 light_in = vec3(0.0);
    float transmittance = 1.0;
    float potential = 0.0;
    float potential_lit = 0.0; // (round W2: the same under the march's own shadow)
    bool lit = fog.sun_tint.w > 0.5;
    bool gather_peak = fog.rays.w > 0.5 && lit;
    if (b > a) {
        float mu = dot(direction, fog.sun.xyz);
        float rayleigh_phase = 0.0596831 * (1.0 + mu * mu); // (3 / (16 pi))
        float aerosol_phase = cs(mu, fog.sun.w) - 0.0795775 * god_peak(mu); // (the peak x 4 pi: / (4 pi))
        // (the steps as the level fog's march takes them: `steps` within the air's sun map's reach, half as many more
        // past it)
        int steps = fog.low.w;
        int near_steps = 0;
        float split = b;
        if (fog.air_look.z > a && b > 1.05 * fog.air_look.z) {
            near_steps = steps;
            steps = steps + max(steps / 2, 4);
            split = fog.air_look.z;
        }
        float u = fract(jitter_at(local) + fog.fog_range.w);
        uint reached = lamps_reached(direction, b);
        int parity = fog.flags.w & 1;
        int traced_pair = -1;
        float traced_sun = 0.0;
        float lamp_seen = 0.0;
        float sky_open = 1.0; // (added by the indoor air: the pair's open share of the sky)
        for (int i = 0; i < steps; ++i) {
            float s0 = march_at(float(i), a, b, split, near_steps, steps);
            float s1 = march_at(float(i + 1), a, b, split, near_steps, steps);
            float s = march_at(float(i) + u, a, b, split, near_steps, steps);
            vec3 at = fog.camera.xyz + direction * s;
            if ((i >> 1) != traced_pair) {
                traced_pair = i >> 1;
                int j = min(i + parity, steps - 1);
                vec3 traced = fog.camera.xyz + direction * march_at(float(j) + u, a, b, split, near_steps, steps);
                float mapped = map_light_seen(traced, local, j);
                traced_sun = mapped >= 0.0 ? -1.0 : light_seen(traced, local, j);
                lamp_seen = reached != 0u ? lamps_seen(traced, direction, reached, local, j) : 0.0;
                sky_open = sky_seen(traced, local, j);
            }
            float sun_seen = map_light_seen(at, local, i);
            if (sun_seen < 0.0) {
                if (traced_sun < 0.0) {
                    traced_sun = light_seen(at, local, i);
                }
                sun_seen = traced_sun;
            }
            float d_r = layer_depth(direction, s0, s1, 1.0, fog.aerial.x, fog.aerial.y);
            float d_a = layer_depth(direction, s0, s1, 1.0, fog.aerial.z, fog.aerial.y);
            if (fog.aerial.w > 0.0) {
                d_a += fog.aerial.w * layer_depth(direction, s0, s1, 1.0, fog.aerosol_s.w, fog.aerial.y);
            }
            float d_p = fog.aerosol_t.w > 0.0
                            ? layer_depth(direction, s0, s1, fog.aerosol_t.w, fog.ambient.w, fog.aerial.y)
                            : 0.0;
            vec3 r_s = fog.medium.rgb * d_r;
            vec3 a_s = fog.aerosol_s.rgb * d_a + vec3(d_p); // (the falling drops and flakes: grey, as much scattered)
            float tau = dot(r_s + fog.aerosol_t.rgb * d_a + vec3(d_p), LUMA);
            if (!(tau > 1e-9)) {
                continue;
            }
            float e = exp(-min(tau, 40.0));
            vec3 source = fog.ambient.rgb * ((r_s + a_s) * sky_open);
            if (lit) {
                source += fog.sun_tint.rgb * (sun_seen * (r_s * rayleigh_phase + a_s * aerosol_phase));
            }
            if (reached != 0u) {
                vec3 lamp_rayleigh;
                vec3 lamp_aerosol = lamps_open(at, direction, reached, lamp_rayleigh);
                // (the lamps' irradiance pi x their light, the phases x 4 pi: x 1 / 4)
                source += (0.25 * lamp_seen) * (a_s * lamp_aerosol + r_s * lamp_rayleigh);
            }
            float w = transmittance * (1.0 - e) / tau;
            light_in += w * source;
            if (gather_peak) {
                float clouds_kept = 1.0;
#ifdef CLOUDS
                clouds_kept = cloud_map_kept(at, fog.cloud_map, fog.cloud_light);
#endif
                potential += w * dot(a_s, LUMA) * clouds_kept;
                potential_lit += w * dot(a_s, LUMA) * clouds_kept * sun_seen;
            }
            transmittance *= e;
            if (transmittance < 1e-4) {
                transmittance = 0.0;
                break;
            }
        }
    }
    imageStore(o_out, q, vec4(light_in, transmittance));
    imageStore(o_info, q, vec4(distance, float(local.x), float(local.y), potential));
    imageStore(o_lit, q, vec4(potential_lit));
}
#endif

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (!inside_low(q)) {
        return;
    }
    ivec2 local = min(q * fog.low.z + ivec2(fog.offsets.zw), fog.rect.zw - ivec2(1));
    ivec2 p = fog.rect.xy + local;
    vec3 direction = direction_of(p);
    float distance = distance_of(p);
#ifdef AERIAL
    march_aerial(q, local, direction, distance);
#else
    bool sky = distance < 0.0;
    bool air_on = fog.air.x > 0.0 || fog.air.w > 0.0;
    // The media along the ray: the level's own fog (its linear range fog as a medium, extinction 1 / (end - d)
    // between start and end - integrated exactly over each segment, so its transmittance is the game's f, to the
    // power rt_fog_density, whatever the step count; x the height falloff), or the thin haze of a level without fog -
    // and the street air near the ground. Each scatters the light that reaches it - the sun's or the moon's, the lamps'
    // - and its shade's own light; their light mixed by their share of each segment's optical depth.
    bool replace = fog.flags.x == 0 && !sky;
    float start = fog.fog_range.x;
    float end = fog.fog_range.y;
    float a = air_on ? 0.0 : max(start, 0.0);
    float b = sky ? end : min(distance, end);
    float fade = end - start;
    // the medium's light per unit of light: the replaced fog whole, the shafts over the game's fog and the haze a
    // share, the sky's pixels a smaller one (the street air's likewise on the sky's pixels)
    float share = sky ? fog.shares.y : (replace ? 1.0 : fog.shares.z);
    vec3 ambient = replace ? fog.fog_colour.rgb * fog.shares.x : vec3(0.0);
    vec3 medium = fog.medium.rgb * share;
    vec3 air_ambient = replace ? fog.air_ambient.rgb * fog.air_light.y : vec3(0.0);
    vec3 air_medium = vec3(fog.air_light.x * (sky ? fog.shares.y : 1.0));
    vec3 light_in = vec3(0.0);
    float transmittance = 1.0;
    float potential = 0.0; // (added by the god rays)
    float potential_lit = 0.0; // (round W2: the same under the march's own shadow)
    bool gather_peak = fog.rays.w > 0.5 && fog.sun_tint.w > 0.5;
    bool lit_air = fog.sun_tint.w > 0.5 || lamps.count.x > 0;
    if (b > a && (replace || lit_air)) {
        float water = water_under(direction, distance);
        // (changed by the god rays: their forward peak taken out - the screen draws it, its shadows at the screen's
        // resolution; the march adds up its potential beside the light - rt_volume_light.h march_sun_phase)
        float toward_sun = dot(direction, fog.sun.xyz);
        float shaped = phase(toward_sun) - god_peak(toward_sun);
        vec3 sun = fog.sun_tint.rgb * shaped;
        vec3 sun_air = fog.sun_tint.rgb * shaped;
        int steps = fog.low.w;
        float u = fract(jitter_at(local) + fog.fog_range.w);
        uint reached = lamps_reached(direction, b);
        // (changed by the street air: the light's rays every other step) the steps in pairs: one point of each pair -
        // the first or the second, by turns frame to frame - traces the sun's (or the moon's) shadow ray and the picked
        // lamp's; both steps take what it saw. (Changed by the distinct shafts:) inside the air's sun map's square
        // every step reads the map for the light's share instead - the lamps' rays stay in pairs
        int parity = fog.flags.w & 1;
        int traced_pair = -1;
        float traced_sun = 0.0;
        float lamp_seen = 0.0;
        // (round W2) the street air's shade light is the sky's: only as much of it as the sky the air sees (the pair's
        // ray toward the upper hemisphere - sky_seen, as the aerial march) - none in a closed room
        float sky_open = 1.0;
        bool sky_traced = replace && air_on;
        // (the steps closer together near the eye - a + span x t^2: the shafts between the buildings round the camera
        // sampled every few metres, the far air - smooth - in long segments; each segment's depth is still exact. A ray
        // that leaves the air's sun map's reach gives it `steps` steps and the way past it half as many more, in a
        // geometric series - how the near air's shadows are sampled, not what it holds)
        int near_steps = 0;
        int total = steps;
        float split = b;
        if (fog.air_look.z > a && b > 1.05 * fog.air_look.z) {
            near_steps = steps;
            total = steps + max(steps / 2, 4);
            split = fog.air_look.z;
        }
        for (int i = 0; i < total; ++i) {
            float s0 = march_at(float(i), a, b, split, near_steps, total);
            float s1 = march_at(float(i + 1), a, b, split, near_steps, total);
            float s = march_at(float(i) + u, a, b, split, near_steps, total);
            vec3 at = fog.camera.xyz + direction * s;
            if ((i >> 1) != traced_pair) {
                traced_pair = i >> 1;
                int j = min(i + parity, total - 1);
                vec3 traced = fog.camera.xyz + direction * march_at(float(j) + u, a, b, split, near_steps, total);
                float mapped = map_light_seen(traced, local, j);
                traced_sun = mapped >= 0.0 ? -1.0 : light_seen(traced, local, j);
                lamp_seen = reached != 0u ? lamps_seen(traced, direction, reached, local, j) : 0.0;
                sky_open = sky_traced ? sky_seen_stratified(traced, local, j) : 1.0;
            }
            float sun_seen = map_light_seen(at, local, i);
            if (sun_seen < 0.0) {
                // (outside the map's square: the pair's traced ray - traced now when the pair's own point was inside)
                if (traced_sun < 0.0) {
                    traced_sun = light_seen(at, local, i);
                }
                sun_seen = traced_sun;
            }
            float x0 = clamp(s0, start, end);
            float x1 = clamp(s1, start, end);
            float tau_main = 0.0;
            if (x1 > x0) {
                float height = fog.fog_range.z > 0.0 ? clamp(exp(-(at.y - fog.camera.y) * fog.fog_range.z), 0.0, 8.0)
                                                     : 1.0;
                // the game's linear fog as a medium: extinction 1 / (end - s), its integral over the segment exactly
                tau_main = fog.fog_colour.w * height * log(max(end - x0, 1e-4 * fade) / max(end - x1, 1e-4 * fade));
            }
            float tau_air = air_on ? air_depth(direction, s0, s1, water) : 0.0;
            float tau = max(tau_main, 0.0) + max(tau_air, 0.0);
            if (!(tau > 1e-9)) {
                continue;
            }
            float e = exp(-min(tau, 40.0));
            vec3 lamp_light = vec3(0.0);
            if (reached != 0u) {
                vec3 lamp_rayleigh;
                lamp_light = lamps_open(at, direction, reached, lamp_rayleigh) * lamp_seen;
            }
            vec3 sun_light = sun * sun_seen;
            vec3 here_main = ambient + medium * (sun_light + lamp_light);
            vec3 here_air = air_ambient * sky_open + air_medium * (sun_air * (sun_seen * fog.air_ambient.w) + lamp_light);
            vec3 here = (max(tau_main, 0.0) * here_main + max(tau_air, 0.0) * here_air) / tau;
            light_in += transmittance * (1.0 - e) * here;
            if (gather_peak) {
                // (the god rays' potential: the light the peak's phase would scatter here per unit of it, unshadowed by
                // the world - the screen's shadows stand for those - but under the clouds' shadow, luma of the media's
                // light per unit of light: the main medium's colour, the street air's grey x its light's brightness)
                float clouds_kept = 1.0;
#ifdef CLOUDS
                clouds_kept = cloud_map_kept(at, fog.cloud_map, fog.cloud_light);
#endif
                float per_unit = (max(tau_main, 0.0) * dot(medium, vec3(0.2126, 0.7152, 0.0722)) +
                                  max(tau_air, 0.0) * air_medium.x * fog.air_ambient.w) / tau;
                potential += transmittance * (1.0 - e) * per_unit * clouds_kept;
                potential_lit += transmittance * (1.0 - e) * per_unit * clouds_kept * sun_seen;
            }
            transmittance *= e;
            if (transmittance < 1e-4) {
                transmittance = 0.0; // (nothing past here is seen)
                break;
            }
        }
    }
    imageStore(o_out, q, vec4(light_in, replace ? transmittance : 1.0));
    imageStore(o_info, q, vec4(distance, float(local.x), float(local.y), potential));
    imageStore(o_lit, q, vec4(potential_lit));
#endif
}
)";

// (added by the distinct shafts) Step 0: the air's sun map (rt_volume_light.h air_map_half) - one texel each: a ray down
// the light's way from the map's plane, the distance to the first thing it hits (with CUTOUTS the cut-outs by their
// alpha - the one lighting path's test, vulkan_rt_scene.h cutout_rays_glsl's), 3e38 where it hits nothing.
constexpr const char* kAirMapBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 13, r32f) uniform writeonly image2D o_air_map;

void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_air_map);
    if (any(greaterThanEqual(t, size))) {
        return;
    }
    float half_side = fog.map_side.w;
    float texel = 2.0 * half_side / float(size.x);
    vec2 uv = (vec2(t) + 0.5) * texel - half_side;
    vec3 down = -fog.sun.xyz;
    vec3 origin = fog.map_centre.xyz + fog.map_side.xyz * uv.x + fog.map_up.xyz * uv.y - down * fog.map_centre.w;
    float reach = fog.map_centre.w + fog.map_up.w;
    uint mask = uint(fog.flags.y) & 0xFFu;
    rayQueryEXT query;
#ifdef CUTOUTS
    rayQueryInitializeEXT(query, u_scene, 0u, mask, origin, 0.0, down, reach);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            cutout_kept(rayQueryGetIntersectionInstanceIdEXT(query, false),
                        rayQueryGetIntersectionPrimitiveIndexEXT(query, false),
                        rayQueryGetIntersectionInstanceCustomIndexEXT(query, false),
                        rayQueryGetIntersectionBarycentricsEXT(query, false))) {
            rayQueryConfirmIntersectionEXT(query);
        }
    }
#else
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT, mask, origin, 0.0, down, reach);
    while (rayQueryProceedEXT(query)) {
    }
#endif
    float hit = rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT
                    ? 3.0e38
                    : rayQueryGetIntersectionTEXT(query, true);
    imageStore(o_air_map, t, vec4(hit));
}
)";

// Step 2: the history.
constexpr const char* kTemporalBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_out;
layout(set = 0, binding = 3, rgba32f) uniform writeonly image2D o_info;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 5, rgba32f) uniform readonly image2D u_in_info;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D u_history;
layout(set = 0, binding = 8, rgba32f) uniform readonly image2D u_history_info;

bool agrees(float was, float expected)
{
    if (was < 0.0 || expected < 0.0) {
        return was < 0.0 && expected < 0.0;
    }
    return abs(was - expected) <= 0.05 * expected + 4.0;
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (!inside_low(q)) {
        return;
    }
    vec4 now = imageLoad(u_in, q);
    vec4 info = imageLoad(u_in_info, q);
    float distance = info.x;
    // this frame's spread around the texel: what the history may be. (Changed by the distinct shafts:) the neighbours'
    // mean +- HISTORY_SPREAD x their deviation, inside their least and most - a history that has moved off a beam's edge
    // (a camera turning, the light moving) is held to what this frame shows there, not to the whole 3 x 3's range
    vec4 low = now;
    vec4 high = now;
    vec4 sum = vec4(0.0);
    vec4 squares = vec4(0.0);
    float counted = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 n = q + ivec2(dx, dy);
            if (inside_low(n)) {
                vec4 v = imageLoad(u_in, n);
                low = min(low, v);
                high = max(high, v);
                sum += v;
                squares += v * v;
                counted += 1.0;
            }
        }
    }
    vec4 middle = sum / counted;
    vec4 deviation = sqrt(max(squares / counted - middle * middle, vec4(0.0)));
    low = max(low, middle - HISTORY_SPREAD * deviation);
    high = min(high, middle + HISTORY_SPREAD * deviation);
    vec4 history = now;
    float history_length = 0.0;
    if (fog.camera_before.w > 0.5) {
        vec3 direction = direction_of(fog.rect.xy + ivec2(info.yz));
        vec3 point = fog.camera.xyz + direction * max(distance, 0.0);
        vec4 before = distance < 0.0 ? fog.to_before * vec4(direction, 0.0) : fog.to_before * vec4(point, 1.0);
        float expected = distance < 0.0 ? -1.0 : length(point - fog.camera_before.xyz);
        if (before.w > 0.0) {
            vec2 at = (before.xy / before.w - vec2(fog.rect.xy) - vec2(0.5 * float(fog.low.z - 1))) / float(fog.low.z);
            ivec2 base = ivec2(floor(at));
            vec2 f = at - vec2(base);
            float weights = 0.0;
            vec4 values = vec4(0.0);
            float lengths = 0.0;
            for (int corner = 0; corner < 4; ++corner) {
                ivec2 n = base + ivec2(corner & 1, corner >> 1);
                float w = ((corner & 1) != 0 ? f.x : 1.0 - f.x) * ((corner >> 1) != 0 ? f.y : 1.0 - f.y);
                if (w <= 0.0 || !inside_low(n)) {
                    continue;
                }
                vec4 was = imageLoad(u_history_info, n);
                if (was.y < 0.5 || !agrees(was.x, expected)) {
                    continue;
                }
                weights += w;
                values += w * imageLoad(u_history, n);
                lengths += w * was.y;
            }
            if (weights > 1e-3) {
                history = values / weights;
                history_length = lengths / weights;
            }
        }
    }
    history = clamp(history, low, high);
    history_length = min(floor(history_length + 0.5) + 1.0, float(fog.flags.z));
    vec4 result = mix(history, now, 1.0 / history_length);
    imageStore(o_out, q, result);
    imageStore(o_info, q, vec4(distance, history_length, info.y, info.z));
}
)";

// (added by the god rays - rt_volume_light.h god_ray_peak) Step 2a: what each texel of the rays' images (half the
// viewport's pixels each way) puts in the light's way: r its 2 x 2 pixels' share of sky (the depth's far value; the
// cut-outs' gaps are sky - open), g the rest's distance x their share, b their share (what stands there, and how far).
constexpr const char* kRaysMaskBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 15, rgba16f) uniform writeonly image2D o_rays;

void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(t, imageSize(o_rays)))) {
        return;
    }
    float sky = 0.0;
    float solid = 0.0;
    float solid_distance = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 local = min(2 * t + ivec2(i & 1, i >> 1), fog.rect.zw - ivec2(1));
        float d = distance_of(fog.rect.xy + local);
        if (d < 0.0) {
            sky += 0.25;
        } else {
            solid += 0.25;
            solid_distance += 0.25 * d;
        }
    }
    imageStore(o_rays, t, vec4(sky, solid_distance, solid, 0.0));
}
)";

// Step 2b: one radial pass - each texel the mean of RAYS_TAPS points of the image before along the line toward the
// light's place, over RAYS_LENGTH of the way there (the passes 1/taps^2, 1/taps, the whole way: taps^3 points in all).
constexpr const char* kRaysRadialBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 14) uniform sampler2D u_rays;
layout(set = 0, binding = 15, rgba16f) uniform writeonly image2D o_rays;

void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_rays);
    if (any(greaterThanEqual(t, size))) {
        return;
    }
    vec2 x = vec2(t) + 0.5;
    vec2 d = (fog.rays.xy - x) * RAYS_LENGTH;
    vec4 sum = vec4(0.0);
    for (int i = 0; i < RAYS_TAPS; ++i) {
        vec2 at = x + d * ((float(i) + 0.5) / float(RAYS_TAPS));
        sum += textureLod(u_rays, at / vec2(size), 0.0);
    }
    imageStore(o_rays, t, sum / float(RAYS_TAPS));
}
)";

// Step 3: the composite (a blend: colour x T + the light; the aerial march's light linear, the level fog's display-referred).
constexpr const char* kCompositeBody = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 5, rgba32f) uniform readonly image2D u_in_info;
// (added by the god rays) this frame's march's info (its potential in a) and the rays' visibility (r / g)
layout(set = 0, binding = 8, rgba32f) uniform readonly image2D u_march_info;
// (round W2) the potential under the march's own shadow
layout(set = 0, binding = 16, r32f) uniform readonly image2D u_lit;
layout(set = 0, binding = 14) uniform sampler2D u_rays;
layout(location = 0) out vec4 o_colour;

// (added by the god rays; changed by the physical air) The share of the air's light along a view ray that lies within
// `d` of the eye - an even medium's (rt_volume_light.h air_share_before): its extinction at the eye's height
// (air_look.y in the aerial march; the street air's) and the ray's whole transmittance.
float air_before(float d, float transmittance)
{
    float sigma = fog.flags.x == 2 ? fog.air_look.y : fog.air.x;
    float whole = 1.0 - clamp(transmittance, 0.0, 1.0);
    if (!(whole > 1e-6) || !(sigma > 0.0)) {
        return 0.0;
    }
    return clamp((1.0 - exp(-sigma * max(d, 0.0))) / whole, 0.0, 1.0);
}

// (added by the god rays) The peak's light at the pixel: the march's potential there (upsampled as the light is, by
// distance), x the peak toward the pixel, x what of the light's surroundings is open along its line to the light.
vec3 god_rays_light(ivec2 local, float distance, ivec2 base, vec2 f, float tolerance, float transmittance)
{
    float peak = god_peak(dot(direction_of(fog.rect.xy + local), fog.sun.xyz));
    if (!(peak > 0.0) || !(fog.sun_tint.w > 0.5)) {
        return vec3(0.0);
    }
    float sum = 0.0;
    float sum_lit = 0.0;
    float weights = 0.0;
    float nearest = 0.0;
    float nearest_lit = 0.0;
    float nearest_gap = 3.0e38;
    for (int corner = 0; corner < 4; ++corner) {
        ivec2 n = clamp(base + ivec2(corner & 1, corner >> 1), ivec2(0), fog.low.xy - ivec2(1));
        float w = ((corner & 1) != 0 ? f.x : 1.0 - f.x) * ((corner >> 1) != 0 ? f.y : 1.0 - f.y);
        vec4 info = imageLoad(u_march_info, n);
        float lit_here = imageLoad(u_lit, n).r;
        float gap = (distance < 0.0) != (info.x < 0.0) ? 1.0e30 : (distance < 0.0 ? 0.0 : abs(info.x - distance));
        if (gap < nearest_gap) {
            nearest_gap = gap;
            nearest = info.w;
            nearest_lit = lit_here;
        }
        w *= exp(-min(gap / tolerance, 60.0));
        sum += w * info.w;
        sum_lit += w * lit_here;
        weights += w;
    }
    float potential = weights > 1e-4 ? sum / weights : nearest;
    float potential_lit = weights > 1e-4 ? sum_lit / weights : nearest_lit;
    float open = 1.0;
    if (fog.rays.w < 1.5) {
        vec2 size = vec2(textureSize(u_rays, 0));
        vec4 r = textureLod(u_rays, (vec2(local) + 0.5) * 0.5 / size, 0.0);
        if (r.b > 1e-4) {
            // what stands on the line, at its mean distance: the air before it - at a distance t, shadowed by what the
            // line holds at the fraction 1 - t / that distance of its way, evenly (the open share of the line) -; the air
            // past it lit (a thing shadows only the air between it and the eye along the lines toward the light)
            float before = air_before(r.g / r.b, transmittance);
            open = 1.0 - (1.0 - clamp(r.r, 0.0, 1.0)) * before;
        }
    }
    // (round W2) the screen's estimate shadows the air only by what the screen shows; the march's own shadow by the world
    // (a roof between the air and the sun that the view doesn't show): the air is lit by no more than either allows
    return fog.sun_tint.rgb * (peak * min(max(potential, 0.0) * open, max(potential_lit, 0.0)) * fog.rays.z);
}

void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - fog.rect.xy;
    float distance = distance_of(fog.rect.xy + local);
    vec2 at = (vec2(local) - vec2(0.5 * float(fog.low.z - 1))) / float(fog.low.z);
    ivec2 base = ivec2(floor(at));
    vec2 f = at - vec2(base);
    vec4 sum = vec4(0.0);
    float weights = 0.0;
    vec4 nearest = vec4(0.0, 0.0, 0.0, 1.0);
    float nearest_gap = 3.0e38;
    float tolerance = distance < 0.0 ? 1.0 : 0.02 * distance + 2.0;
    for (int corner = 0; corner < 4; ++corner) {
        ivec2 n = clamp(base + ivec2(corner & 1, corner >> 1), ivec2(0), fog.low.xy - ivec2(1));
        float w = ((corner & 1) != 0 ? f.x : 1.0 - f.x) * ((corner >> 1) != 0 ? f.y : 1.0 - f.y);
        float there = imageLoad(u_in_info, n).x;
        vec4 v = imageLoad(u_in, n);
        float gap = (distance < 0.0) != (there < 0.0) ? 1.0e30 : (distance < 0.0 ? 0.0 : abs(there - distance));
        if (gap < nearest_gap) {
            nearest_gap = gap;
            nearest = v;
        }
        w *= exp(-min(gap / tolerance, 60.0));
        sum += w * v;
        weights += w;
    }
    vec4 v = weights > 1e-4 ? sum / weights : nearest;
    float transmittance = clamp(v.a, 0.0, 1.0);
    vec3 light_in = max(v.rgb, vec3(0.0)) + god_rays_light(local, distance, base, f, tolerance, transmittance);
#ifdef LINEAR_COMPOSITE
    // (the physical air; round W2: blended - the frame is linear light since round F, so the copy the composite once took
    // the frame to linear light through is gone) colour x T + the light: the blend's dst x src alpha + src
    o_colour = vec4(min(light_in, vec3(65504.0)), transmittance);
    return;
#endif
    // (round F) the level fog path's light is display-referred (the level's fog colour as its light - A5): decoded as
    // the frame's inputs are, then added to the linear frame; never clamped (rt_hdr retired: no second pipeline)
    o_colour = vec4(linear_from_display(light_in), transmittance);
}
)";

// The block, as the shaders' std140 layout has it.
struct FogBlock {
    std::array<float, kMatrixFloats> to_world{}; // column-major (GLSL's mat4)
    std::array<float, kMatrixFloats> to_before{};
    std::array<float, 4> camera{};
    std::array<float, 4> camera_before{};
    std::array<float, 4> sun{};
    std::array<float, 4> sun_tint{};
    std::array<float, 4> fog_colour{};
    std::array<float, 4> fog_range{};
    std::array<float, 4> offsets{};
    std::array<float, 4> shares{};
    std::array<int32_t, 4> rect{};
    std::array<int32_t, 4> low{};
    std::array<int32_t, 4> flags{};
    std::array<float, 4> aerial{};
    std::array<float, 4> sun_light{};
    std::array<float, 4> medium{};      // (added by the volumetric light)
    std::array<float, 4> soft{};
    std::array<float, 4> cloud_map{};
    std::array<float, 4> cloud_light{};
    std::array<float, 4> air{}; // (added by the street air)
    std::array<float, 4> air_light{};
    std::array<float, 4> air_ambient{};
    std::array<float, 4> air_look{}; // (added by the cinematic air)
    std::array<float, 4> map_side{}; // (added by the distinct shafts)
    std::array<float, 4> map_up{};
    std::array<float, 4> map_centre{};
    std::array<float, 4> rays{}; // (added by the god rays)
    std::array<float, 4> aerosol_s{}; // (added by the physical air)
    std::array<float, 4> aerosol_t{};
    std::array<float, 4> ambient{};
};
constexpr std::size_t kFogBlockBytes = 576;

// The lamps' block (binding 10, std140).
struct FogLampsBlock {
    std::array<int32_t, 4> count{};
    std::array<std::array<float, 4>, kFogMostLamps> at{};
    std::array<std::array<float, 4>, kFogMostLamps> colour{};
    std::array<std::array<float, 4>, kFogMostLamps> more{};
};
static_assert(sizeof(FogLampsBlock) == 16 + (3 * 16 * kFogMostLamps), "the shaders' std140 block");
static_assert(sizeof(FogBlock) == kFogBlockBytes, "the shaders' std140 block");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The bindings: 0 the scene, 1 the depth, 2 / 3 written (the light, the info), 4 / 5 read (the light, the info), 6 / 8
// the history before (the light, the info), 7 the block.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOut = 2;
constexpr uint32_t kBindingOutInfo = 3;
constexpr uint32_t kBindingIn = 4;
constexpr uint32_t kBindingInInfo = 5;
constexpr uint32_t kBindingHistory = 6;
constexpr uint32_t kBindingBlock = 7;
constexpr uint32_t kBindingHistoryInfo = 8;
constexpr uint32_t kBindingSky = 9; // (the sky model's table: the aerial march's)
constexpr uint32_t kBindingLamps = 10;    // (added by the volumetric light) the lamps' block
constexpr uint32_t kBindingCloudMap = 11; // the clouds' shadow map (vulkan_rt_sky_pass.h)
constexpr uint32_t kBindingGeometry = 12; // the frame's geometry table (CUTOUTS: the cut-outs' test, the water's)
constexpr uint32_t kBindingAirMap = 13;   // (added by the distinct shafts) the air's sun map
constexpr uint32_t kBindingRaysIn = 14;   // (added by the god rays) the rays' image read (sampled, linear)
constexpr uint32_t kBindingRaysOut = 15;  // ... and written
constexpr uint32_t kBindingLit = 16;      // (round W2) the god rays' potential under the march's own shadow
constexpr uint32_t kBindingCount = 17;
constexpr uint32_t kMaterialSet = 1; // (CUTOUTS: the material table's set)

// The march's variants (bits): a multisampled depth, the sky model's haze, the cut-outs and water (the material table
// and the frame's geometry), the clouds' shadow map.
constexpr uint32_t kVariantMultisampled = 1;
constexpr uint32_t kVariantAerial = 2;
constexpr uint32_t kVariantCutouts = 4;
constexpr uint32_t kVariantClouds = 8;
constexpr uint32_t kVariants = 16;

struct CompositePipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool linear = false; // (added by the physical air: the aerial march's, over the colour's copy)
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

constexpr std::size_t kSlotImages = 9;

// A viewport's images and history.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    uint32_t scale = kLeastScale; // (the march's block: its images 1 / this of the viewport's on each axis)
    Image raw;                         // step 1: the light, the transmittance
    Image raw_info;                    // step 1: the distance, the pixel marched
    Image raw_lit;                     // (round W2) step 1: the god rays' potential under the march's own shadow
    std::array<Image, 2> histories;    // step 2's, this frame's and the one before (by `current`)
    std::array<Image, 2> history_info; // likewise: the distance, the history's length
    std::array<Image, 2> rays;         // (added by the god rays) the rays' images (half the viewport's pixels), in turn
    Image* rays_written = nullptr; // the one the composite read last (read_rt_fog_image's kRays)
    uint32_t current = 0;
    bool fresh = true;
    bool have_before = false;
    uint64_t frame_drawn = 0;
    uint64_t passes = 0; // the frame index of the jitter
    Matrix world_to_pixel{};
    std::array<float, 3> camera{};
    HistoryCamera shot; // (added by the first frame) the camera of the frame before as a cut is told by
    bool written = false; // a history was written (read_rt_fog_image's)
    uint64_t last_use = 0;
};

constexpr uint32_t kTimerPairs = 8; // (the pairs of timestamps the passes are timed with, used in turn)

struct FogState {
    RtFogSettings settings;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineLayout layout_materials = VK_NULL_HANDLE; // (set 0 + the material table's: the CUTOUTS variants)
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler linear_sampler = VK_NULL_HANDLE; // (added by the god rays)
    // (added by the god rays) the rays' passes: the mask (by the depth's samples: single, multi) and the three radial
    // passes (made on first use; failed ones not tried again)
    std::array<VkPipeline, 2> rays_mask{};
    std::array<VkPipeline, 3> rays_radial{};
    bool rays_failed = false;
    std::array<float, 4> rays{}; // this view's block.rays
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, kVariants> march{}; // by variant (made on first use)
    std::array<bool, kVariants> march_failed{};
    // (added by the distinct shafts) the air's sun map: its pass (without / with the cut-outs; made on first use) and its
    // image (rt_fog_quality's side; every viewport's in turn - each makes it before its march)
    std::array<VkPipeline, 2> air_map_pass{};
    std::array<bool, 2> air_map_failed{};
    Image air_map;
    bool air_map_made = false;
    VkPipeline temporal = VK_NULL_HANDLE;
    std::vector<CompositePipeline> composites;
    std::vector<std::unique_ptr<ViewSlot>> slots;
    std::array<float, 3> to_sun{};
    std::array<float, 3> sun_tint{};
    bool have_sun = false;
    rt::MediumLight light; // (added by the volumetric light) the light the air is lit by this view
    // (added by the cinematic air) the street air last logged: its ground's height and look (logged again when either
    // changes - a new level, another look)
    float logged_street = -1.0e30F;
    rt::Weather logged_weather = rt::Weather::kClear;
    bool logged_air = false;
    std::vector<rt::Emitter> lamps; // the lamps that light the air this view (rt_lights' lit set)
    float lamp_scale = 0.0F;
    bool cutouts = false;  // this view's march takes the cut-outs and water (the material table, the frame's geometry)
    CloudShadowMap cloud{}; // the clouds' shadow map (made this frame), when have_cloud
    bool have_cloud = false;
    SceneView scene_view;
    bool haze = false; // this view's level has no fog: the haze drawn instead (rt_fog_haze)
    bool aerial = false; // ... as the sky model's aerial perspective (rt_sky_model=1, a sun and its table)
    std::array<float, 3> sun_colour{1.0F, 1.0F, 1.0F}; // the sky model's (display-referred, brightest channel 1)
    bool standard_samples = false;
    bool failed = false;
    uint64_t passes = 0;
    uint64_t uses = 0;
    // the passes timed on the device: timestamps before and after each, kTimerPairs pairs used in turn (a pair's last
    // result read, when the device has written it, before it is used again)
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;                    // (0: the device can't time - none taken)
    uint64_t timed = 0;                                // pairs written
    std::array<uint64_t, kTimerPairs> timer_serials{}; // the submission that last wrote each pair (0: none)
    RtFogTiming timing;
    uint64_t window_samples = 0; // since the last log line
    double window_milliseconds = 0.0;
};

constexpr uint64_t kTimingLogEvery = 240; // passes timed

FogState& fog_state()
{
    static FogState state;
    return state;
}

std::atomic<bool>& replacing()
{
    static std::atomic<bool> flag{false};
    return flag;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class FogListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kFog; }
    void opaque_drawn(const rt::ViewDrawn& view) override { (void)view; }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_rt_fog_pass(state, view);
        }
    }
};

FogListener& listener()
{
    static FogListener instance;
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

std::string source_of(const char* body, bool traces, bool multisampled, uint32_t variant = 0)
{
    const bool aerial = (variant & kVariantAerial) != 0;
    const bool cutouts = traces && (variant & kVariantCutouts) != 0;
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    source += "#define HISTORY_SPREAD " + std::to_string(kFogHistorySpread) + "\n";
    // (the god rays' peak's edge - the march, the rays' passes and the composite)
    source += "#define GOD_RAYS_CONE_COS " +
              std::to_string(std::cos(rt::kGodRayConeDegrees * std::numbers::pi / 180.0)) + "\n";
    source += "#define RAYS_TAPS " + std::to_string(rt::kGodRayTaps) + "\n";
    if (aerial) {
        source += "#define AERIAL\n";
    }
    if (traces) {
        source += "#extension GL_EXT_ray_query : require\n";
        source += "#define SUN_DISTANCE " + std::to_string(kFogSunDistance) + "\n";
        source += "#define FOG_MOST_LAMPS " + std::to_string(kFogMostLamps) + "\n";
        if ((variant & kVariantClouds) != 0) {
            source += "#define CLOUDS\n";
        }
        if (cutouts) {
            source += "#define CUTOUTS\n";
            source += "#define WATER_CLASS " + std::to_string(rt::kReflectWater) + "u\n";
            source += "#define THUG2_MATERIAL_SET " + std::to_string(kMaterialSet) + "\n";
            source += material_glsl();
            source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING " + std::to_string(kBindingGeometry) +
                      "\n";
            source += scene_geometry_glsl();
        }
        source += "layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;\n";
        if (cutouts) {
            source += cutout_rays_glsl();
        }
    }
    source += kCommon;
    if (traces && (variant & kVariantClouds) != 0) {
        source += cloud_map_glsl(kBindingCloudMap);
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
    FogState& own = fog_state();
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (binding == kBindingScene) {
            type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        } else if (binding == kBindingDepth || binding == kBindingSky || binding == kBindingCloudMap ||
                   binding == kBindingRaysIn) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (binding == kBindingBlock || binding == kBindingLamps) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingGeometry) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
    // (added by the god rays) the rays' images read between their taps, clamped at the screen's edges (a light off the
    // screen: its line goes on as the edge shows it)
    VkSamplerCreateInfo linear_info = sampler_info;
    linear_info.magFilter = VK_FILTER_LINEAR;
    linear_info.minFilter = VK_FILTER_LINEAR;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS ||
        vkCreateSampler(device, &linear_info, nullptr, &own.linear_sampler) != VK_SUCCESS) {
        *error = "the descriptor layout or the sampler";
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
    return true;
}

// (added by the volumetric light) The layout of the CUTOUTS variants: set 0 and the material table's (once it has one).
bool ensure_material_layout(Backend& state)
{
    FogState& own = fog_state();
    if (own.layout_materials != VK_NULL_HANDLE) {
        return own.material_layout == state.materials.layout();
    }
    if (!state.materials.ready() || state.materials.layout() == VK_NULL_HANDLE) {
        return false;
    }
    own.material_layout = state.materials.layout();
    const std::array<VkDescriptorSetLayout, 2> sets{own.set_layout, own.material_layout};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = static_cast<uint32_t>(sets.size()),
                                                 .pSetLayouts = sets.data(),
                                                 .pushConstantRangeCount = 0,
                                                 .pPushConstantRanges = nullptr};
    if (vkCreatePipelineLayout(state.context.device(), &layout_info, nullptr, &own.layout_materials) != VK_SUCCESS) {
        own.layout_materials = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// The layouts, the sampler, the vertex shader and the compute pipelines (once).
bool ensure_objects(Backend& state, std::string* error)
{
    FogState& own = fog_state();
    if (own.temporal != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    own.standard_samples = properties.limits.standardSampleLocations == VK_TRUE;
    if (own.layout == VK_NULL_HANDLE && !make_layouts(state, error)) {
        return false;
    }
    if (own.vertex == VK_NULL_HANDLE) {
        own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
        if (own.vertex == VK_NULL_HANDLE) {
            return false;
        }
    }
    // (the march's variants are made on first use: march_pipeline)
    // (the temporal pass reads no depth: the single-sampled declaration is never bound)
    own.temporal = make_compute(device, own.layout, source_of(kTemporalBody, false, false), error);
    return own.temporal != VK_NULL_HANDLE;
}

// A march variant's pipeline (made on first use; VK_NULL_HANDLE: it can't be - logged once, that variant not tried again).
VkPipeline march_pipeline(Backend& state, uint32_t variant, std::string* error)
{
    FogState& own = fog_state();
    if (core::at(own.march, variant) != VK_NULL_HANDLE || core::at(own.march_failed, variant)) {
        return core::at(own.march, variant);
    }
    const VkPipelineLayout layout = (variant & kVariantCutouts) != 0 ? own.layout_materials : own.layout;
    core::at(own.march, variant) =
        make_compute(state.context.device(), layout,
                     source_of(kMarchBody, true, (variant & kVariantMultisampled) != 0, variant), error);
    core::at(own.march_failed, variant) = core::at(own.march, variant) == VK_NULL_HANDLE;
    return core::at(own.march, variant);
}

// (added by the distinct shafts) The air's sun map's pass (with the cut-outs or without; made on first use - VK_NULL_HANDLE:
// it can't be, not tried again).
VkPipeline air_map_pipeline(Backend& state, bool cutouts, std::string* error)
{
    FogState& own = fog_state();
    const std::size_t which = cutouts ? 1 : 0;
    if (core::at(own.air_map_pass, which) != VK_NULL_HANDLE || core::at(own.air_map_failed, which)) {
        return core::at(own.air_map_pass, which);
    }
    core::at(own.air_map_pass, which) =
        make_compute(state.context.device(), cutouts ? own.layout_materials : own.layout,
                     source_of(kAirMapBody, true, false, cutouts ? kVariantCutouts : 0U), error);
    core::at(own.air_map_failed, which) = core::at(own.air_map_pass, which) == VK_NULL_HANDLE;
    return core::at(own.air_map_pass, which);
}

// (added by the god rays) The rays' passes (made on first use): the mask for the depth's samples and the three radial
// passes (their lengths 1/taps^2, 1/taps, 1). False: they can't be made (logged by the caller once; not tried again).
bool rays_pipelines(Backend& state, bool multisampled, std::string* error)
{
    FogState& own = fog_state();
    if (own.rays_failed) {
        return false;
    }
    const VkDevice device = state.context.device();
    VkPipeline& mask = core::at(own.rays_mask, multisampled ? 1 : 0);
    if (mask == VK_NULL_HANDLE) {
        mask = make_compute(device, own.layout, source_of(kRaysMaskBody, false, multisampled), error);
    }
    const double taps = rt::kGodRayTaps;
    const std::array<double, 3> lengths{1.0 / (taps * taps), 1.0 / taps, 1.0};
    for (std::size_t i = 0; i < lengths.size() && mask != VK_NULL_HANDLE; ++i) {
        VkPipeline& radial = core::at(own.rays_radial, i);
        if (radial == VK_NULL_HANDLE) {
            const std::string body = "#define RAYS_LENGTH " + std::to_string(core::at(lengths, i)) + "\n" + kRaysRadialBody;
            radial = make_compute(device, own.layout, source_of(body.c_str(), false, false), error);
            if (radial == VK_NULL_HANDLE) {
                break;
            }
        }
    }
    own.rays_failed = mask == VK_NULL_HANDLE ||
                      std::ranges::any_of(own.rays_radial, [](VkPipeline made) { return made == VK_NULL_HANDLE; });
    return !own.rays_failed;
}

// The composite pass's pipeline for a colour target (made on first use).
VkPipeline composite_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error,
                              bool linear = false)
{
    FogState& own = fog_state();
    for (const CompositePipeline& made : own.composites) {
        if (made.format == format && made.samples == samples && made.linear == linear) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const std::string body = std::string(linear ? "#define LINEAR_COMPOSITE\n" : "") + rt::kDisplayTransferGlsl + kCompositeBody;
    const VkShaderModule fragment = make_module(
        device, RtShaderStage::kFragment, source_of(body.c_str(), false, samples != VK_SAMPLE_COUNT_1_BIT), error);
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
                                                           .rasterizationSamples = samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // colour x T + the light: (src x 1) + (dst x src alpha); alpha kept
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
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
    own.composites.push_back({.format = format, .samples = samples, .linear = linear, .pipeline = pipeline});
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

// World -> (pixel x, pixel y, depth, 1) in the target's pixels (column vectors): view x projection (Direct3D's row
// vectors, transposed), then clip space to Direct3D 9's pixels (centres at whole coordinates, y down) and the depth range.
Matrix world_to_pixel(const rt::ViewDrawn& view)
{
    Matrix clip_from_world{}; // column vectors
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

// --- the viewports' images ---------------------------------------------------------------------------------------

std::array<Image*, kSlotImages> images_of(ViewSlot& slot)
{
    return {&slot.raw,
            &slot.raw_info,
            &slot.raw_lit,
            &slot.histories.front(),
            &slot.histories.back(),
            &slot.history_info.front(),
            &slot.history_info.back(),
            &slot.rays.front(),
            &slot.rays.back()};
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
                               // (sampled too: the memory makes the view of a sampled image)
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
    return {.width = (core::at(rect, 2) + scale - 1) / scale, .height = (core::at(rect, 3) + scale - 1) / scale};
}

// (added by the god rays) The rays' images: half the viewport's pixels each way.
Extent rays_extent(const std::array<uint32_t, 4>& rect)
{
    return {.width = (core::at(rect, 2) + 1) / 2, .height = (core::at(rect, 3) + 1) / 2};
}

ViewSlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, const Extent& target, uint32_t scale)
{
    FogState& own = fog_state();
    ++own.uses;
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == target.width && slot->target.height == target.height &&
            slot->scale == scale) {
            slot->last_use = own.uses;
            return slot.get();
        }
    }
    if (own.slots.size() >= kFogViewSlots) {
        const auto oldest = std::ranges::min_element(
            own.slots, {}, [](const std::unique_ptr<ViewSlot>& slot) { return slot->last_use; });
        release_slot(state, oldest->get(), false);
        own.slots.erase(oldest);
    }
    auto made_slot = std::make_unique<ViewSlot>();
    ViewSlot& slot = *made_slot;
    slot.rect = rect;
    slot.target = target;
    slot.scale = scale;
    slot.last_use = own.uses;
    const Extent extent = low_extent(rect, scale);
    bool made = make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, &slot.raw) &&
                make_storage_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, extent, &slot.raw_info) &&
                make_storage_image(state, VK_FORMAT_R32_SFLOAT, extent, &slot.raw_lit);
    for (std::size_t i = 0; i < 2; ++i) {
        made = made && make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, &core::at(slot.histories, i)) &&
               make_storage_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, extent, &core::at(slot.history_info, i)) &&
               make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, rays_extent(rect), &core::at(slot.rays, i));
    }
    if (!made) {
        release_slot(state, &slot, true);
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_fog: the viewport's images (%ux%u) couldn't be made - the pass skipped there",
                    extent.width, extent.height);
        }
        return nullptr;
    }
    own.slots.push_back(std::move(made_slot));
    return &slot;
}

// (added by the distinct shafts) The air's sun map's image at `side` texels (made again when the side changes; GENERAL,
// cleared to "nothing hit" when made - the march binds it whether or not a map is made this frame).
bool ensure_air_map(Backend& state, VkCommandBuffer commands, uint32_t side)
{
    FogState& own = fog_state();
    if (own.air_map_made && own.air_map.extent.width == side) {
        return true;
    }
    if (own.air_map_made) {
        retire_image(state, &own.air_map);
        own.air_map_made = false;
    }
    if (!make_storage_image(state, VK_FORMAT_R32_SFLOAT, {.width = side, .height = side}, &own.air_map)) {
        return false;
    }
    own.air_map_made = true;
    const VkImageSubresourceRange all{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    const VkImageMemoryBarrier to_general{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                          .pNext = nullptr,
                                          .srcAccessMask = 0,
                                          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = own.air_map.image,
                                          .subresourceRange = all};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_general);
    constexpr float kNothingHit = 3.0e38F;
    const VkClearColorValue nothing{.float32 = {kNothingHit, kNothingHit, kNothingHit, kNothingHit}};
    vkCmdClearColorImage(commands, own.air_map.image, VK_IMAGE_LAYOUT_GENERAL, &nothing, 1, &all);
    own.air_map.layout = VK_IMAGE_LAYOUT_GENERAL;
    const VkMemoryBarrier cleared{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cleared, 0,
                         nullptr, 0, nullptr);
    return true;
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
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &cleared,
                         0, nullptr, 0, nullptr);
    slot->fresh = false;
}

// One pass's descriptors (null: not bound).
struct PassBindings {
    bool scene = false;
    bool sky = false; // the sky model's table (vulkan_rt_sky.h)
    bool cloud_map = false; // (the clouds' shadow map)
    bool cutouts = false;   // (the frame's geometry; the material table's set bound by the caller)
    VkDescriptorBufferInfo lamps{}; // (buffer VK_NULL_HANDLE: none)
    const Image* depth = nullptr;
    const Image* out = nullptr;
    const Image* out_info = nullptr;
    const Image* in = nullptr;
    const Image* in_info = nullptr;
    const Image* history = nullptr;
    const Image* history_info = nullptr;
    const Image* air_map = nullptr; // (added by the distinct shafts)
    const Image* rays_in = nullptr; // (added by the god rays: read through the linear sampler)
    const Image* rays_out = nullptr;
    const Image* lit = nullptr; // (round W2: the march's potential under its own shadow - written, then read)
    VkDescriptorBufferInfo block{};
};

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const PassBindings& bound)
{
    const FogState& own = fog_state();
    const VkWriteDescriptorSetAccelerationStructureKHR scene{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &own.scene};
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
    if (bound.scene) {
        add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR).pNext = &scene;
    }
    if (bound.sky) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingSky);
        info = sky_table_descriptor();
        add(kBindingSky, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    if (bound.cloud_map) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingCloudMap);
        info = own.cloud.image;
        add(kBindingCloudMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const VkDescriptorBufferInfo geometry{
        .buffer = own.scene_view.geometry, .offset = 0, .range = own.scene_view.geometry_bytes};
    if (bound.cutouts) {
        add(kBindingGeometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = &geometry;
    }
    if (bound.lamps.buffer != VK_NULL_HANDLE) {
        add(kBindingLamps, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &bound.lamps;
    }
    if (bound.depth != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingDepth);
        info = {.sampler = own.sampler,
                .imageView = bound.depth->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    if (bound.rays_in != nullptr) {
        VkDescriptorImageInfo& info = core::at(infos, kBindingRaysIn);
        info = {.sampler = own.linear_sampler, .imageView = bound.rays_in->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(kBindingRaysIn, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &info;
    }
    const std::array<std::pair<uint32_t, const Image*>, 9> storage{{{kBindingLit, bound.lit},
                                                                    {kBindingRaysOut, bound.rays_out},
                                                                    {kBindingOut, bound.out},
                                                                    {kBindingOutInfo, bound.out_info},
                                                                    {kBindingIn, bound.in},
                                                                    {kBindingInInfo, bound.in_info},
                                                                    {kBindingHistory, bound.history},
                                                                    {kBindingHistoryInfo, bound.history_info},
                                                                    {kBindingAirMap, bound.air_map}}};
    for (const auto& [binding, image] : storage) {
        if (image == nullptr) {
            continue;
        }
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        add(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &info;
    }
    add(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &bound.block;
    const VkPipelineLayout layout = bound.cutouts ? own.layout_materials : own.layout;
    vkCmdPushDescriptorSetKHR(commands, point, layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
    if (bound.cutouts) {
        const VkDescriptorSet material_set = backend().materials.descriptor_set();
        vkCmdBindDescriptorSets(commands, point, layout, kMaterialSet, 1, &material_set, 0, nullptr);
    }
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

// The depth moved to be sampled by the passes (from whatever wrote it), and back to its resting layout after them.
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
                      const std::array<uint32_t, 4>& rect)
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
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

std::array<float, 2> sample_zero_offset(VkSampleCountFlagBits samples, bool standard)
{
    if (!standard) {
        return {0.0F, 0.0F};
    }
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

// (changed by the volumetric light) The light the air is lit by (rt_volume_light.h medium_light): the scene's sun -
// the one every effect lights by (rt_scene.h scene_sun) - with, while the clock lights the level, the clock's state
// for it (rt_daylight.h DayLight: the sun, or the moon at night; its colour and strength over the level's own sun's, its
// size). None while the fit hasn't run yet, or when the bake shows no sun (an indoor level): the fog is then the game's
// fog alone, the lamps' light in it. The fit is asked for while the fog is on (render_pc fits a new world in its first
// frame).
void read_light()
{
    FogState& own = fog_state();
    own.light = rt::medium_light();
    // (changed by the physical air) the air is lit from where the sun is - the sky's and the disc's sun: its glow toward
    // the sun lies round the drawn sun, and the light reaching the air is the sun's through its own air mass
    if (own.light.have && own.light.clock && !own.light.moonlit) {
        own.light.toward = rt::day_light().sun;
    }
    own.have_sun = own.light.have;
    own.sun_tint = {1.0F, 1.0F, 1.0F};
    rt::note_sun_used(rt::SunUser::kFog, own.light.have, own.light.toward);
    if (!own.light.have) {
        return;
    }
    own.to_sun = own.light.toward;
    // (added by the sun's glare) with the sky model (or rt_sun_colour) sunlight has a colour: the air's for the light
    // the level was lit with - the clock's own sun's (DayLight::native) while the clock lights it, its light's colour
    // being relative to that -, else the light's own; brightness kept (Rec. 709 luma 1)
    std::array<float, 3> model{1.0F, 1.0F, 1.0F};
    const RtSunSettings& sun = sun_settings_held();
    if (sun.sky_model) {
        own.sun_colour = sky_colours(own.to_sun).sun;
        const rt::DayLight day = rt::day_light();
        const std::array<float, 3> native = own.light.clock ? day.native : own.to_sun;
        const std::array<float, 3> colour = sky_colours(native).sun;
        const float luma = (0.2126F * core::at(colour, 0)) + (0.7152F * core::at(colour, 1)) +
                           (0.0722F * core::at(colour, 2));
        if (luma > 0.0F) {
            for (std::size_t i = 0; i < 3; ++i) {
                core::at(model, i) = core::at(colour, i) / luma;
            }
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(own.sun_tint, i) = core::at(own.light.colour, i) * core::at(model, i);
    }
}

// The lamps that light the air this view: rt_lights' lit set (vulkan_rt_lights.h - the lamps on as the clock says, the
// view's most important first), at most rt_fog_quality's count, x rt_lights' strength and the lamps' share on. None
// when the lamps' pass didn't light this frame (by day, rt_lights off).
void read_lamps(const Backend& state)
{
    FogState& own = fog_state();
    own.lamps.clear();
    own.lamp_scale = 0.0F;
    const RtLitLamps lit = rt_lights_lit_locked(state);
    if (lit.frame != state.frame || !(lit.scale > 0.0F) || own.settings.lamps == 0) {
        return;
    }
    const std::size_t count = std::min<std::size_t>({lit.lights.size(), own.settings.lamps, kFogMostLamps});
    own.lamps.assign(lit.lights.begin(), lit.lights.begin() + static_cast<std::ptrdiff_t>(count));
    own.lamp_scale = lit.scale;
}

FogLampsBlock lamps_block()
{
    const FogState& own = fog_state();
    FogLampsBlock block{};
    core::at(block.count, 0) = static_cast<int32_t>(own.lamps.size());
    for (std::size_t i = 0; i < own.lamps.size(); ++i) {
        const rt::Emitter& lamp = core::at(own.lamps, i);
        const float light = lamp.intensity * own.lamp_scale;
        core::at(block.at, i) = {core::at(lamp.position, 0), core::at(lamp.position, 1), core::at(lamp.position, 2),
                                 lamp.size};
        core::at(block.colour, i) = {core::at(lamp.colour, 0) * light, core::at(lamp.colour, 1) * light,
                                     core::at(lamp.colour, 2) * light, lamp.reach};
        core::at(block.more, i) = {lamp.core, 0.0F, 0.0F, 0.0F};
    }
    return block;
}

// The timestamps' pool (once; none when the device can't time).
void ensure_timer(Backend& state)
{
    FogState& own = fog_state();
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
        own.tick_milliseconds = -1.0; // (not tried again)
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

// A pair's last result (when the device has written both), added to the timing; logged every kTimingLogEvery.
void read_timer(Backend& state, uint32_t pair, const Extent& size)
{
    FogState& own = fog_state();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, 2> stamps{};
    const VkResult got =
        vkGetQueryPoolResults(state.context.device(), own.timer, pair * 2, 2, sizeof(stamps), stamps.data(),
                              sizeof(Stamp), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((got != VK_SUCCESS && got != VK_NOT_READY) || stamps.front().available == 0 || stamps.back().available == 0 ||
        stamps.back().value < stamps.front().value) {
        return;
    }
    const double milliseconds = static_cast<double>(stamps.back().value - stamps.front().value) * own.tick_milliseconds;
    ++own.timing.samples;
    own.timing.milliseconds += milliseconds;
    ++own.window_samples;
    own.window_milliseconds += milliseconds;
    if (own.window_samples >= kTimingLogEvery) {
        get_log(
            "vulkan: rt_fog: the passes take %.3f ms on the device (%ux%u, %u steps; the last %llu passes' average)",
            own.window_milliseconds / static_cast<double>(own.window_samples), size.width, size.height,
            own.settings.steps, static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = 0.0;
    }
}

// The block for a view (its matrices, the fog, the sun and the frame's jitter).
// The pixel of its block (scale x scale) a texel marches in its `pass`-th frame: the block's pixels in turn, in the
// order of their ordered-dither index (each next one far from the last).
std::array<float, 2> block_pixel(uint32_t scale, uint64_t pass)
{
    const auto index = static_cast<uint8_t>(pass % (static_cast<uint64_t>(scale) * scale));
    for (uint32_t y = 0; y < scale; ++y) {
        for (uint32_t x = 0; x < scale; ++x) {
            const uint8_t at =
                scale == kMostScale ? core::at(core::at(kBayer4, y), x) : core::at(core::at(kBayer2, y % 2), x % 2);
            if (at == index) {
                return {static_cast<float>(x), static_cast<float>(y)};
            }
        }
    }
    return {0.0F, 0.0F};
}

// (added by the distinct shafts) The air's sun map's square in the block (rt_volume_light.h air_map_half): across the
// light's way, the eye's place in it snapped to whole texels. Nothing when the look has no volume round the eye.
void set_air_map(const rt::ViewDrawn& view, FogBlock* block)
{
    const FogState& own = fog_state();
    const double half_side = rt::air_map_half();
    if (!(half_side > 0.0)) {
        return;
    }
    using Vector = std::array<double, 3>;
    const auto cross = [](const Vector& a, const Vector& b) {
        return Vector{(core::at(a, 1) * core::at(b, 2)) - (core::at(a, 2) * core::at(b, 1)),
                      (core::at(a, 2) * core::at(b, 0)) - (core::at(a, 0) * core::at(b, 2)),
                      (core::at(a, 0) * core::at(b, 1)) - (core::at(a, 1) * core::at(b, 0))};
    };
    const auto dot = [](const Vector& a, const Vector& b) {
        return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
    };
    const Vector toward{core::at(own.to_sun, 0), core::at(own.to_sun, 1), core::at(own.to_sun, 2)};
    const Vector across = std::abs(core::at(toward, 1)) < kAirMapUpright ? Vector{0.0, 1.0, 0.0} : Vector{1.0, 0.0, 0.0};
    Vector side = cross(toward, across);
    const double side_length = std::sqrt(dot(side, side));
    if (!(side_length > 0.0)) {
        return;
    }
    for (double& v : side) {
        v /= side_length;
    }
    const Vector up = cross(side, toward);
    const double texel = 2.0 * half_side / static_cast<double>(own.air_map.extent.width);
    const Vector eye{core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                     core::at(view.camera.position, 2)};
    const double u = std::round(dot(eye, side) / texel) * texel;
    const double v = std::round(dot(eye, up) / texel) * texel;
    const double w = dot(eye, toward);
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(block->map_side, i) = static_cast<float>(core::at(side, i));
        core::at(block->map_up, i) = static_cast<float>(core::at(up, i));
        core::at(block->map_centre, i) =
            static_cast<float>((core::at(side, i) * u) + (core::at(up, i) * v) + (core::at(toward, i) * w));
    }
    core::at(block->map_side, 3) = static_cast<float>(half_side);
    core::at(block->map_up, 3) = static_cast<float>(2.0 * half_side);
    core::at(block->map_centre, 3) = static_cast<float>(rt::kAirMapLift);
}

// (added by the physical air) The aerial march's light: the light's irradiance and the light scattered more than once
// per unit of scattering (Psi at the camera's height), in the sky's unit (rt_sky.h sky_exposure) - the sun's through
// the weather's air (its transmittance), or the moon's as the clock lights the level (its light over the level's own
// sun's direct light, a TOA-equivalent irradiance for Psi).
struct AerialLight {
    std::array<float, 3> irradiance{};
    std::array<float, 3> ambient{};
};
AerialLight aerial_light(const rt::ViewDrawn& view, float ground)
{
    const FogState& own = fog_state();
    AerialLight out;
    if (!own.light.have) {
        return out;
    }
    constexpr uint32_t kLightSteps = 64;
    const auto luma = [](const rt::Rgb& c) {
        return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2));
    };
    const double unit = rt::sky_exposure();
    const double rise = std::clamp(static_cast<double>(core::at(own.to_sun, 1)), -1.0, 1.0);
    const double elevation = std::asin(rise);
    rt::Rgb irradiance{};
    double top = 1.0; // (the light's irradiance at the atmosphere's top, the sun's 1)
    if (own.light.moonlit) {
        const rt::DayLight day = rt::day_light();
        const double native =
            std::asin(std::clamp(static_cast<double>(core::at(day.native, 1)), -1.0, 1.0));
        const double native_direct = luma(rt::sun_transmittance(native, kLightSteps));
        for (std::size_t i = 0; i < 3; ++i) {
            core::at(irradiance, i) =
                rt::linear_from_display(static_cast<double>(core::at(own.light.colour, i))) * native_direct;
        }
        const double through = luma(rt::sun_transmittance(elevation, kLightSteps));
        top = through > 1e-6 ? luma(irradiance) / through : 0.0;
    } else {
        // (round S: the clock's sun through the clouds' mean - the map gives each point's over it - and the frame's
        // pre-exposure, as the lighting state's light carries them)
        const rt::DayLight day = rt::day_light();
        const double pre = day.on ? static_cast<double>(day.pre_exposure) : 1.0;
        const double beam = day.on ? static_cast<double>(day.cloud_beam) : 1.0;
        irradiance = rt::sun_transmittance(elevation, kLightSteps);
        for (double& value : irradiance) {
            value *= pre * beam;
        }
        top = pre;
    }
    // (round S) the sky's light under the clouds: the share of the clear sky's the lighting state keeps
    const rt::DayLight sky_day = rt::day_light();
    top *= sky_day.on ? static_cast<double>(sky_day.cloud_sky) : 1.0;
    const double above = std::max(static_cast<double>(core::at(view.camera.position, 1)) - ground, 0.0) /
                         rt::kUnitsPerMetre;
    constexpr rt::MultipleSteps kPsiSteps{.steps = 10, .light = 8};
    const rt::Rgb psi = rt::multiple_scattering({.altitude = above, .cos_sun = rise}, kPsiSteps);
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out.irradiance, i) = static_cast<float>(core::at(irradiance, i) * unit);
        core::at(out.ambient, i) = static_cast<float>(core::at(psi, i) * unit * top);
    }
    return out;
}

FogBlock block_for(const rt::ViewDrawn& view, const Matrix& to_world, const ViewSlot& slot, bool have_before,
                   VkSampleCountFlagBits samples)
{
    const FogState& own = fog_state();
    const rt::Aerosol air = rt::aerosol();
    FogBlock block{};
    block.to_world = column_major(to_world);
    block.to_before = column_major(slot.world_to_pixel);
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), 0.0F};
    block.camera_before = {core::at(slot.camera, 0), core::at(slot.camera, 1), core::at(slot.camera, 2),
                           have_before ? 1.0F : 0.0F};
    // (the phase: the weather's aerosol's - rt_sky.h)
    block.sun = {core::at(own.to_sun, 0), core::at(own.to_sun, 1), core::at(own.to_sun, 2), static_cast<float>(air.g)};
    // (the volumetric light) the light's colour: the haze is lit by the light as it is now (the moon's 0.22 blue at
    // night); the game's own fog - whose colour the game sets for the time of day (its states' fog colours) - by its hue
    // alone, the fog's colour carrying its brightness
    std::array<float, 3> tint = own.sun_tint;
    if (!own.haze && !own.aerial) {
        const float luma = (0.2126F * core::at(tint, 0)) + (0.7152F * core::at(tint, 1)) + (0.0722F * core::at(tint, 2));
        for (float& channel : tint) {
            channel = luma > 1e-4F ? channel / luma : 1.0F;
        }
    }
    block.sun_tint = {core::at(tint, 0), core::at(tint, 1), core::at(tint, 2), own.have_sun ? 1.0F : 0.0F};
    block.fog_colour = {core::at(view.fog.colour, 0), core::at(view.fog.colour, 1), core::at(view.fog.colour, 2),
                        own.settings.density};
    const double jitter = std::fmod(static_cast<double>(slot.passes % (1U << 20U)) * kGoldenRatio, 1.0);
    block.fog_range = {view.fog.start, view.fog.end, own.settings.height > 0.0F ? 1.0F / own.settings.height : 0.0F,
                       static_cast<float>(jitter)};
    const std::array<float, 2> sample = sample_zero_offset(samples, own.standard_samples);
    // the pixel of its block each texel marches this frame (kBayer4 / kBayer2's order)
    const std::array<float, 2> pick = block_pixel(slot.scale, slot.passes);
    block.offsets = {sample.front(), sample.back(), pick.front(), pick.back()};
    // (no light - indoors: the replaced fog is the game's colour whole, the lamps' light added in it)
    block.shares = {own.have_sun ? kFogAmbientShare : 1.0F, kFogSkyShare, kFogShaftShare, 0.0F}; // (w: unused - round F)
    block.rect = {static_cast<int32_t>(core::at(view.rect, 0)), static_cast<int32_t>(core::at(view.rect, 1)),
                  static_cast<int32_t>(core::at(view.rect, 2)), static_cast<int32_t>(core::at(view.rect, 3))};
    const Extent low = low_extent(view.rect, slot.scale);
    block.low = {static_cast<int32_t>(low.width), static_cast<int32_t>(low.height), static_cast<int32_t>(slot.scale),
                 static_cast<int32_t>(own.settings.steps)};
    block.flags = {own.settings.mode == RtFogMode::kShafts || own.haze ? 1 : 0,
                   static_cast<int32_t>(own.cutouts ? kFogCutoutMask : kFogMask), static_cast<int32_t>(kFogMaxHistory),
                   static_cast<int32_t>(slot.passes % (1U << 24U))};
    // the medium's light per unit of light (phase 1): the fog's (or the haze's) colour x the light's share
    const float light_share = 1.0F - kFogAmbientShare;
    block.medium = {core::at(view.fog.colour, 0) * light_share, core::at(view.fog.colour, 1) * light_share,
                    core::at(view.fog.colour, 2) * light_share, 0.0F};
    const double radius = static_cast<double>(own.light.radius_degrees) * std::numbers::pi / 180.0;
    block.soft = {own.light.have ? static_cast<float>(std::tan(radius)) : 0.0F, 0.0F, 0.0F, 0.0F};
    if (own.have_cloud) {
        block.cloud_map = own.cloud.place;
        block.cloud_light = own.cloud.light;
    }
    // (the level's ground: the air thicker toward it)
    const rt::BakedSun& baked = rt::baked_sun();
    const float ground = baked.scene != 0 && baked.ground_known ? baked.ground : core::at(view.camera.position, 1);
    // (the air's sun map round the eye, where a light lights the air - how its shadows are sampled near the eye)
    if (own.have_sun && own.air_map_made && own.settings.air_map) {
        set_air_map(view, &block);
        // (the march's steps: `steps` within the map's reach, half as many more past it)
        core::at(block.air_look, 2) = static_cast<float>(rt::kAirMapReach * rt::kUnitsPerMetre);
    }
    // (added by the street air; changed by the physical air) the level's own fog's air near the ground
    // (rt_volume_light.h street_air): the weather's aerosol as one layer and the falling rain or snow; its light the
    // level fog path's (display-referred - kFogStreetAirLight), x the light's whole colour (over a level's fog the tint
    // is a hue: its brightness given back here)
    if (own.light.have && !own.aerial) {
        const rt::StreetAir street_air = rt::street_air(air, rt::air_weather());
        // (over the level's street level - the middle of its upward faces -, not its lowest floor)
        const float street = baked.scene != 0 && baked.street_known ? baked.street : ground;
        block.air = {static_cast<float>(street_air.extinction),
                     street_air.height > 0.0 ? static_cast<float>(1.0 / street_air.height) : 0.0F, street,
                     static_cast<float>(street_air.snow)};
        block.air_light = {kFogStreetAirLight, 1.0F, 0.0F,
                           street_air.snow_height > 0.0 ? static_cast<float>(1.0 / street_air.snow_height) : 0.0F};
        const float tint_luma = (0.2126F * core::at(tint, 0)) + (0.7152F * core::at(tint, 1)) +
                                (0.0722F * core::at(tint, 2));
        const float whole_luma = (0.2126F * core::at(own.sun_tint, 0)) + (0.7152F * core::at(own.sun_tint, 1)) +
                                 (0.0722F * core::at(own.sun_tint, 2));
        block.air_ambient = {core::at(view.fog.colour, 0) * kFogAmbientShare,
                             core::at(view.fog.colour, 1) * kFogAmbientShare,
                             core::at(view.fog.colour, 2) * kFogAmbientShare,
                             tint_luma > 1e-4F ? whole_luma / tint_luma : 1.0F};
    }
    if (own.aerial) {
        // (rewritten by the physical air) the atmosphere's air (rt_sky.h): the air's molecules (Rayleigh) and the
        // weather's aerosol per world unit at the level's ground, their heights, the precipitation; the light's
        // irradiance and Psi in the sky's unit
        constexpr int32_t kAerialMode = 2;
        constexpr double kUnits = rt::kUnitsPerMetre;
        core::at(block.flags, 0) = kAerialMode;
        block.aerial = {static_cast<float>(1.0 / (rt::kRayleighHeight * kUnits)), ground,
                        static_cast<float>(1.0 / (air.height * kUnits)), static_cast<float>(air.mist)};
        block.sun_light = {0.0F, 0.0F, 0.0F, kFogAerialFar};
        block.medium = {static_cast<float>(core::at(rt::kRayleigh, 0) / kUnits),
                        static_cast<float>(core::at(rt::kRayleigh, 1) / kUnits),
                        static_cast<float>(core::at(rt::kRayleigh, 2) / kUnits), 0.0F};
        const double falling = rt::precipitation_extinction(rt::air_weather()) / kUnits;
        block.aerosol_s = {static_cast<float>(core::at(air.scattering, 0) / kUnits),
                           static_cast<float>(core::at(air.scattering, 1) / kUnits),
                           static_cast<float>(core::at(air.scattering, 2) / kUnits),
                           static_cast<float>(air.mist_height > 0.0 ? 1.0 / (air.mist_height * kUnits) : 0.0)};
        block.aerosol_t = {static_cast<float>(core::at(air.extinction, 0) / kUnits),
                           static_cast<float>(core::at(air.extinction, 1) / kUnits),
                           static_cast<float>(core::at(air.extinction, 2) / kUnits), static_cast<float>(falling)};
        const AerialLight light = aerial_light(view, ground);
        block.sun_tint = {core::at(light.irradiance, 0), core::at(light.irradiance, 1), core::at(light.irradiance, 2),
                          own.have_sun ? 1.0F : 0.0F};
        block.ambient = {core::at(light.ambient, 0), core::at(light.ambient, 1), core::at(light.ambient, 2),
                         static_cast<float>(1.0 / (rt::kPrecipitationHeight * kUnits))};
        // (the sky's pixels: the atmosphere drawn holds the whole ray - nothing added there; the air's extinction at
        // the eye's height for the god rays' share before what stands toward the light)
        const double above = std::max(static_cast<double>(core::at(view.camera.position, 1) - ground), 0.0);
        const double sigma = (core::at(rt::kRayleigh, 1) * std::exp(-above / (rt::kRayleighHeight * kUnits)) +
                              core::at(air.extinction, 1) * rt::aerosol_density(air, above / kUnits)) /
                             kUnits;
        core::at(block.air_look, 0) = rt::sky_replacement().on ? 1.0F : 0.0F;
        core::at(block.air_look, 1) = static_cast<float>(sigma);
    }
    block.rays = own.rays; // (added by the god rays: set_rays)
    if (own.aerial) {
        core::at(block.rays, 2) = static_cast<float>(1.0 / (4.0 * std::numbers::pi)); // (the peak x 4 pi, per sr)
    }
    return block;
}

// (added by the god rays) Where the light is on the screen, in the rays' images' texels (a texel t's middle is t + 0.5,
// the viewport's pixel 2t + 0.5), and whether they run: none while rt_fog_god_rays is 0 or no light lights the air;
// the light behind the eye (or on its plane) - its peak nowhere on the screen - the march still hands it over and the
// composite draws it unshadowed (it is 0 there but at the very edge of a wide view).
void set_rays(const Matrix& to_pixel, const std::array<uint32_t, 4>& rect)
{
    FogState& own = fog_state();
    own.rays = {};
    if (!own.settings.god_rays || !own.have_sun) {
        return;
    }
    std::array<double, 4> clip{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        double sum = 0.0;
        for (std::size_t k = 0; k < 3; ++k) {
            sum += core::at(core::at(to_pixel, row), k) * static_cast<double>(core::at(own.to_sun, k));
        }
        core::at(clip, row) = sum;
    }
    // (the peak's light as the march gathers it: 1 - block_for puts the aerial march's per-steradian scale in)
    constexpr float strength = 1.0F;
    constexpr double kInFront = 1e-6;
    constexpr float kOn = 1.0F;
    constexpr float kBehind = 2.0F;
    const double w = core::at(clip, 3);
    if (!(w > kInFront)) {
        own.rays = {0.0F, 0.0F, strength, kBehind};
        return;
    }
    // (very far off the screen: brought in along its way from the screen's corner - the line toward it the same to a
    // texel's width)
    constexpr double kFarthest = 1.0e6;
    double x = (core::at(clip, 0) / w) - static_cast<double>(core::at(rect, 0));
    double y = (core::at(clip, 1) / w) - static_cast<double>(core::at(rect, 1));
    const double farthest = std::max(std::abs(x), std::abs(y));
    if (farthest > kFarthest) {
        x *= kFarthest / farthest;
        y *= kFarthest / farthest;
    }
    own.rays = {static_cast<float>((x + 0.5) * 0.5), static_cast<float>((y + 0.5) * 0.5), strength, kOn};
}

// What the pass draws (for the log).
const char* what_is_drawn()
{
    const FogState& own = fog_state();
    if (own.aerial) {
        return "no fog in the level: the atmosphere's air (aerial perspective)";
    }
    if (own.haze) {
        return "no fog in the level: the haze's shafts";
    }
    return own.settings.mode == RtFogMode::kShafts ? "shafts over the level's fog" : "the level's fog replaced";
}

// Why a view gets no pass (null: it can have one).
bool skip(Backend& state, const rt::ViewDrawn& view)
{
    const FogState& own = fog_state();
    if (!own.settings.on || own.failed) {
        return true;
    }
    if (!state.context.ray_tracing_enabled()) {
        if (first_report(state, kReportNoRayTracing)) {
            get_log("vulkan: rt_fog=1 but ray tracing isn't enabled ([Graphics] ray_tracing=1, and a device with "
                    "VK_KHR_ray_query) - no volumetric fog");
        }
        return true;
    }
    FogState& state_of_fog = fog_state();
    state_of_fog.haze = !view.fog.on || !(view.fog.end > view.fog.start) || !std::isfinite(view.fog.start) ||
                        !std::isfinite(view.fog.end);
    if (state_of_fog.haze && (!own.settings.haze || (!own.have_sun && own.lamps.empty()))) {
        if (first_report(state, kReportNoFog)) {
            get_log("vulkan: rt_fog: the level has no fog (and %s) - the fog pass skipped",
                    own.settings.haze ? "no sun or lamps for the haze's light" : "rt_fog_haze=0");
        }
        return true;
    }
    if (own.scene == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoScene)) {
            get_log("vulkan: rt_fog: no acceleration structure for the scene yet - the fog pass skipped");
        }
        return true;
    }
    return false;
}

bool record_pass(Backend& state, const rt::ViewDrawn& view)
{
    FogState& own = fog_state();
    read_light();
    read_lamps(state);
    if (skip(state, view)) {
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
            get_log("vulkan: rt_fog: the bound targets can't be read (no samplable depth of the colour's size and "
                    "samples) - the fog pass skipped there");
        }
        return false;
    }
    std::string error;
    const VkPipeline composite = ensure_objects(state, &error)
                                     ? composite_pipeline(state, colour->format, colour->samples, &error)
                                     : VK_NULL_HANDLE;
    if (composite == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_fog: the passes couldn't be made (%s) - no volumetric fog", error.c_str());
        }
        return false;
    }
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
    ViewSlot* const slot = slot_for(state, clipped.rect, colour->extent, own.settings.scale);
    if (slot == nullptr) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    // (added by the sun's glare) the sky model's haze: aerial perspective, coloured by the model's table
    own.aerial = own.haze && own.have_sun && sun_settings_held().sky_model && update_sky(state, commands, own.to_sun);
    if (own.aerial) {
        clipped.fog = {.on = true, .start = 0.0F, .end = kFogAerialFar, .colour = kFogHazeColour};
    } else if (own.haze) {
        // (no fog in the level: a thin haze whose sunlit light alone is added - shafts mode - rt_fog_haze)
        clipped.fog = {.on = true, .start = 0.0F, .end = kFogHazeEnd, .colour = kFogHazeColour};
    }
    // (added by the volumetric light) the cut-outs' test and the water's - the material table and this frame's geometry
    // for the same scene -, and the clouds' shadow map made for this viewport
    own.scene_view = current_scene(state);
    own.cutouts = own.scene_view.tlas == own.scene && own.scene_view.tlas != VK_NULL_HANDLE &&
                  own.scene_view.geometry != VK_NULL_HANDLE && ensure_material_layout(state) &&
                  state.materials.descriptor_set() != VK_NULL_HANDLE;
    own.have_cloud = own.have_sun && cloud_shadow_map(state, &own.cloud) && core::at(own.cloud.light, 3) > 0.0F;
    const FogLampsBlock lamps = lamps_block();
    std::optional<RingSpan> lamps_span;
    {
        const void* const lamp_bytes = &lamps;
        lamps_span = state.ring.copy(std::span(static_cast<const uint8_t*>(lamp_bytes), sizeof(lamps)),
                                     state.uniform_alignment);
    }
    const uint32_t variant = (colour->samples != VK_SAMPLE_COUNT_1_BIT ? kVariantMultisampled : 0U) |
                             (own.aerial ? kVariantAerial : 0U) | (own.cutouts ? kVariantCutouts : 0U) |
                             (own.have_cloud ? kVariantClouds : 0U);
    const VkPipeline march = lamps_span ? march_pipeline(state, variant, &error) : VK_NULL_HANDLE;
    if (march == VK_NULL_HANDLE) {
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_fog: the march (variant %u) couldn't be made (%s) - no volumetric fog there", variant,
                    lamps_span ? error.c_str() : "no room in the ring");
        }
        return false;
    }
    const VkDescriptorBufferInfo lamps_info{
        .buffer = lamps_span->buffer, .offset = lamps_span->offset, .range = sizeof(FogLampsBlock)};
    bool have_before = slot->have_before && slot->frame_drawn + 1 >= state.frame;
    const bool cut = have_before && camera_cut(slot->shot, view.camera);
    have_before = have_before && !cut;
    // (added by the first frame - vulkan_rt_history.h: a history that starts again is seeded with a full history's
    // frames of marches, so nothing fades in)
    const uint32_t seeds = seed_frames(true, !have_before, kFogMaxHistory);
    // (added by the distinct shafts) the air's sun map's image (bound by every march; made this frame where the look
    // has a volume round the eye and a light)
    if (!ensure_air_map(state, commands, own.settings.map)) {
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_fog: the air's sun map (%u texels a side) couldn't be made - the fog pass skipped",
                    own.settings.map);
        }
        return false;
    }
    set_rays(to_pixel, clipped.rect); // (added by the god rays)
    if (core::at(own.rays, 3) > 0.0F && !rays_pipelines(state, colour->samples != VK_SAMPLE_COUNT_1_BIT, &error)) {
        own.rays = {}; // (the march keeps the whole phase)
        if (first_report(state, kReportRays)) {
            get_log("vulkan: rt_fog: the god rays' passes couldn't be made (%s) - none", error.c_str());
        }
    }
    FogBlock block = block_for(clipped, to_world, *slot, have_before, colour->samples);
    const auto block_in_ring = [&state](const FogBlock& written) -> std::optional<VkDescriptorBufferInfo> {
        const void* const block_bytes = &written;
        const std::optional<RingSpan> span = state.ring.copy(
            std::span(static_cast<const uint8_t*>(block_bytes), sizeof(written)), state.uniform_alignment);
        if (!span) {
            return std::nullopt;
        }
        return VkDescriptorBufferInfo{.buffer = span->buffer, .offset = span->offset, .range = sizeof(written)};
    };
    std::optional<VkDescriptorBufferInfo> first_block = block_in_ring(block);
    if (!first_block) {
        if (first_report(state, kReportNoRoom)) {
            get_log("vulkan: rt_fog: no room in the ring for the pass's block - the fog pass skipped");
        }
        return false;
    }
    VkDescriptorBufferInfo block_info = *first_block;
    const Extent low = low_extent(clipped.rect, slot->scale);

    if (slot->fresh) {
        clear_slot(commands, slot);
    }
    ensure_timer(state);
    // (a pair is used again once the submission that last wrote it has finished - its result read first; until then
    // this pass isn't timed)
    const auto pair = static_cast<uint32_t>(own.timed % kTimerPairs);
    uint64_t& written = core::at(own.timer_serials, pair);
    const bool timing = own.timer != VK_NULL_HANDLE && (written == 0 || state.frames.completed() >= written);
    if (timing) {
        if (written != 0) {
            read_timer(state, pair, {.width = width, .height = height});
        }
        written = state.frames.serial();
        vkCmdResetQueryPool(commands, own.timer, pair * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer, pair * 2);
    }
    // the scene's build (earlier on this queue) and the last frame's uses of the images, before this one's
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

    // 0. (added by the distinct shafts) the air's sun map, once a frame (the seeds' marches share the camera and light)
    if (core::at(block.map_side, 3) > 0.0F) {
        const VkPipeline map_pass = air_map_pipeline(state, own.cutouts, &error);
        if (map_pass == VK_NULL_HANDLE) {
            core::at(block.map_side, 3) = 0.0F; // (no map: the march traces the light's rays everywhere)
            const std::optional<VkDescriptorBufferInfo> without = block_in_ring(block);
            if (!without) {
                return false;
            }
            block_info = *without;
            if (first_report(state, kReportPipeline)) {
                get_log("vulkan: rt_fog: the air's sun map's pass couldn't be made (%s) - the light's rays traced instead",
                        error.c_str());
            }
        } else {
            dispatch(commands, map_pass,
                     {.scene = true, .cutouts = own.cutouts, .air_map = &own.air_map, .block = block_info},
                     {.width = own.air_map.extent.width, .height = own.air_map.extent.height});
        }
    }

    const Image* history = nullptr;
    const Image* history_info = nullptr;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        if (seed > 0) {
            // (the next frame's march from the same camera: the history just written is the one before)
            slot->current ^= 1U;
            ++slot->passes;
            slot->world_to_pixel = to_pixel;
            slot->camera = view.camera.position;
            block = block_for(clipped, to_world, *slot, true, colour->samples);
            const std::optional<VkDescriptorBufferInfo> next = block_in_ring(block);
            if (!next) {
                slot->current ^= 1U; // (no room for more: the history so far is the frame's)
                --slot->passes;
                break;
            }
            block_info = *next;
        }
        const uint32_t now = slot->current;
        const uint32_t before = now ^ 1U;
        // 1. the march
        dispatch(commands, march,
                 {.scene = true,
                  .cloud_map = own.have_cloud,
                  .cutouts = own.cutouts,
                  .lamps = lamps_info,
                  .depth = depth,
                  .out = &slot->raw,
                  .out_info = &slot->raw_info,
                  .air_map = &own.air_map,
                  .lit = &slot->raw_lit,
                  .block = block_info},
                 low);
        // 2. the history
        history = &core::at(slot->histories, now);
        history_info = &core::at(slot->history_info, now);
        dispatch(commands, own.temporal,
                 {.out = history,
                  .out_info = history_info,
                  .in = &slot->raw,
                  .in_info = &slot->raw_info,
                  .history = &core::at(slot->histories, before),
                  .history_info = &core::at(slot->history_info, before),
                  .block = block_info},
                 low);
    }
    // 2a / 2b. (added by the god rays) the light's surroundings seen along each texel's line to the light: the mask,
    // then three radial passes, the images in turn - the last one written is the composite's
    const Image* rays = &slot->rays.front();
    constexpr float kRaysOnScreen = 1.5F; // (rays.w 1: the light in front - 2 behind)
    if (core::at(own.rays, 3) > 0.0F && core::at(own.rays, 3) < kRaysOnScreen) {
        const Extent half = rays_extent(clipped.rect);
        Image* const first = &slot->rays.front();
        Image* const second = &slot->rays.back();
        dispatch(commands, core::at(own.rays_mask, colour->samples != VK_SAMPLE_COUNT_1_BIT ? 1 : 0),
                 {.depth = depth, .rays_in = second, .rays_out = first, .block = block_info}, half);
        Image* from = first;
        Image* to = second;
        for (const VkPipeline radial : own.rays_radial) {
            dispatch(commands, radial, {.rays_in = from, .rays_out = to, .block = block_info}, half);
            std::swap(from, to);
        }
        rays = from;
        slot->rays_written = from;
    }
    // 3. the composite (the aerial march's light linear - round W2: blended over the frame as the level fog's is)
    const VkPipeline drawn =
        own.aerial ? composite_pipeline(state, colour->format, colour->samples, &error, true) : composite;
    if (drawn == VK_NULL_HANDLE) {
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_fog: the aerial composite couldn't be made - the aerial air skipped");
        }
        depth_barrier(commands, depth, depth->resting, false);
        return false;
    }
    record_composite(commands, drawn, colour,
                     {.depth = depth,
                      .in = history,
                      .in_info = history_info,
                      .history_info = &slot->raw_info,
                      .rays_in = rays,
                      .lit = &slot->raw_lit,
                      .block = block_info},
                     clipped.rect);
    if (timing) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timer, (pair * 2) + 1);
        ++own.timed;
    }
    depth_barrier(commands, depth, depth->resting, false);
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
    slot->world_to_pixel = to_pixel;
    slot->camera = view.camera.position;
    slot->shot = history_camera(view.camera);
    slot->have_before = true;
    slot->written = true;
    slot->frame_drawn = state.frame;
    slot->current ^= 1U;
    ++slot->passes;
    ++own.passes;
    if (seeds > 1 && first_report(state, kReportSeeded)) {
        get_log("vulkan: rt_fog: a history started again (%s) - seeded with %u frames' marches in one (nothing fades "
                "in; logged once)",
                cut ? "a camera cut" : "a new view, a load or the settings", seeds);
    }
    if (first_report(state, kReportLight)) {
        get_log("vulkan: rt_fog: the air lit by %s (%.1f degrees up, colour %.2f %.2f %.2f, %.1f degrees across), %zu "
                "lamps, %s, %s",
                !own.light.have ? "no light" : (own.light.moonlit ? "the moon" : "the sun"),
                std::asin(std::clamp(static_cast<double>(core::at(own.to_sun, 1)), -1.0, 1.0)) * 180.0 /
                    std::numbers::pi,
                static_cast<double>(core::at(own.sun_tint, 0)), static_cast<double>(core::at(own.sun_tint, 1)),
                static_cast<double>(core::at(own.sun_tint, 2)), 2.0 * static_cast<double>(own.light.radius_degrees),
                own.lamps.size(), own.have_cloud ? "the clouds' shadows in it" : "no clouds' shadows",
                own.cutouts ? "the cut-outs and water told" : "no material table (cut-outs opaque, no water)");
    }
    constexpr float kStreetLoggedApart = 1.0F; // (world units)
    const float logged_y = own.aerial ? core::at(block.aerial, 1) : core::at(block.air, 2);
    if ((own.aerial || core::at(block.air, 0) > 0.0F) &&
        (std::abs(logged_y - own.logged_street) > kStreetLoggedApart || own.settings.weather != own.logged_weather ||
         own.aerial != own.logged_air)) {
        own.logged_street = logged_y;
        own.logged_weather = own.settings.weather;
        own.logged_air = own.aerial;
        const rt::Aerosol air = rt::aerosol();
        get_log("vulkan: rt_fog: the %s air over y %.0f (%s): the aerosol's extinction %.2e %.2e %.2e a metre at the "
                "ground over %.0f m%s, g %.2f; the air's light %.3f %.3f %.3f, multiply scattered %.4f %.4f %.4f",
                own.aerial ? "atmosphere's" : "level fog's", static_cast<double>(logged_y),
                rt::weather_name(own.settings.weather), core::at(air.extinction, 0), core::at(air.extinction, 1),
                core::at(air.extinction, 2), air.height,
                air.mist > 0.0 ? text::format(" and a mist %.0f x as dense over %.0f m", air.mist, air.mist_height).c_str()
                               : "",
                air.g, static_cast<double>(core::at(block.sun_tint, 0)), static_cast<double>(core::at(block.sun_tint, 1)),
                static_cast<double>(core::at(block.sun_tint, 2)), static_cast<double>(core::at(block.ambient, 0)),
                static_cast<double>(core::at(block.ambient, 1)), static_cast<double>(core::at(block.ambient, 2)));
    }
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_fog: the first fog pass (%ux%u at 1/%u, %u steps, %s, fog %.0f - %.0f, density %.2f, "
                "height %.0f, %s)",
                width, height, slot->scale, own.settings.steps, what_is_drawn(), static_cast<double>(clipped.fog.start),
                static_cast<double>(clipped.fog.end), static_cast<double>(own.settings.density),
                static_cast<double>(own.settings.height), own.have_sun ? "a sun" : "no sun");
    }
    return true;
}

} // namespace

void set_rt_fog_settings(const RtFogSettings& settings)
{
    RtFogSettings kept = settings;
    kept.density = std::isfinite(kept.density) ? std::clamp(kept.density, 0.0F, kMaxDensity) : 1.0F;
    kept.height = std::isfinite(kept.height) ? std::clamp(kept.height, 0.0F, kMaxHeight) : 0.0F;

    kept.steps = std::clamp(kept.steps, kMinSteps, kMaxSteps);
    kept.lamps = std::min(kept.lamps, kFogMostLamps);
    kept.scale = kept.scale >= kMostScale ? kMostScale : kLeastScale;
    kept.map = std::clamp(kept.map, kLeastAirMap, kMostAirMap);
    // (the physical air: the weather is the sky's aerosol too - published whatever the fog's own setting)
    rt::set_weather(kept.weather);
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        fog_state().settings = kept;
        // (a split comparison's swap between its sides keeps each side's history - rt_scene.h settings_swap)
        if (!rt::settings_swap()) {
            rt_fog_reset_history(state);
            replacing() = false;
        }
    }
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

RtFogSettings rt_fog_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return fog_state().settings;
}

bool rt_fog_on_locked(const Backend& /*state*/) { return fog_state().settings.on; }

void rt_fog_set_scene(VkAccelerationStructureKHR tlas)
{
    const std::scoped_lock lock(backend().mutex);
    rt_fog_set_scene_locked(tlas);
}

void rt_fog_set_scene_locked(VkAccelerationStructureKHR tlas) { fog_state().scene = tlas; }

VkImageUsageFlags rt_fog_depth_usage(const Backend& state, VkFormat format)
{
    if (!fog_state().settings.on || !state.context.ray_tracing_enabled()) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

bool rt_fog_replaces_level_fog() { return replacing().load(std::memory_order_relaxed); }

bool record_rt_fog_pass(Backend& state, const rt::ViewDrawn& view)
{
    const bool recorded = record_pass(state, view);
    replacing() = recorded && fog_state().settings.mode == RtFogMode::kReplace && !fog_state().haze;
    return recorded;
}

uint64_t rt_fog_passes() { return fog_state().passes; }

RtFogTiming rt_fog_timing() { return fog_state().timing; }

void rt_fog_reset_history(Backend& state)
{
    (void)state;
    for (const std::unique_ptr<ViewSlot>& slot : fog_state().slots) {
        slot->have_before = false;
        slot->passes = 0;
    }
}

bool read_rt_fog_image(Backend& state, const std::array<uint32_t, 4>& rect, RtFogImage which, RtFogTexels* out)
{
    *out = RtFogTexels{};
    // (the viewport's slot last drawn: a change of rt_fog_quality's scale makes another for the same rectangle)
    const ViewSlot* latest = nullptr;
    for (const std::unique_ptr<ViewSlot>& slot : fog_state().slots) {
        if (slot->rect == rect && slot->written && (latest == nullptr || slot->last_use > latest->last_use)) {
            latest = slot.get();
        }
    }
    for (const std::unique_ptr<ViewSlot>& slot : fog_state().slots) {
        if (slot.get() != latest) {
            continue;
        }
        const uint32_t last = slot->current ^ 1U; // (the pass flips `current` after writing)
        Image* image = &slot->raw;
        if (which == RtFogImage::kRawInfo) {
            image = &slot->raw_info;
        } else if (which == RtFogImage::kHistory) {
            image = &core::at(slot->histories, last);
        } else if (which == RtFogImage::kHistoryInfo) {
            image = &core::at(slot->history_info, last);
        } else if (which == RtFogImage::kRawLit) {
            image = &slot->raw_lit;
        } else if (which == RtFogImage::kRays) {
            if (slot->rays_written == nullptr) {
                return false;
            }
            image = slot->rays_written;
        }
        if (!finish_work(state)) {
            return false;
        }
        const bool single = image->format == VK_FORMAT_R32_SFLOAT; // (round W2: the sunlit potential - r alone)
        const std::size_t kChannels = single ? 1 : 4;
        const bool wide = single || image->format == VK_FORMAT_R32G32B32A32_SFLOAT;
        const std::size_t texels = static_cast<std::size_t>(image->extent.width) * image->extent.height;
        std::vector<uint8_t> bytes(texels * kChannels * (wide ? sizeof(float) : sizeof(uint16_t)));
        if (!state.memory.read_image_level(image, 0, &bytes)) {
            return false;
        }
        out->width = image->extent.width;
        out->height = image->extent.height;
        out->texels.resize(texels);
        for (std::size_t i = 0; i < texels; ++i) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                float value = 0.0F;
                if (wide) {
                    std::memcpy(&value, &core::at(bytes, ((i * kChannels) + channel) * sizeof(float)), sizeof(value));
                } else {
                    uint16_t half = 0;
                    std::memcpy(&half, &core::at(bytes, ((i * kChannels) + channel) * sizeof(uint16_t)), sizeof(half));
                    value = from_half(half);
                }
                core::at(core::at(out->texels, i), channel) = value;
            }
        }
        return true;
    }
    return false;
}

void release_rt_fog(Backend& state)
{
    FogState& own = fog_state();
    const VkDevice device = state.context.device();
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        release_slot(state, slot.get(), true);
    }
    own.slots.clear();
    if (own.air_map_made) {
        state.memory.destroy_image(&own.air_map);
        own.air_map_made = false;
    }
    if (device != nullptr) {
        for (const VkPipeline pipeline : own.air_map_pass) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (const CompositePipeline& made : own.composites) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        for (const VkPipeline pipeline : own.march) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.temporal != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.temporal, nullptr);
        }
        for (const VkPipeline pipeline : own.rays_mask) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (const VkPipeline pipeline : own.rays_radial) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.linear_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.linear_sampler, nullptr);
        }
        if (own.layout_materials != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout_materials, nullptr);
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
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timed = 0;
    own.timer_serials = {};
    own.composites.clear();
    own.march = {};
    own.march_failed = {};
    own.air_map_pass = {};
    own.air_map_failed = {};
    own.rays_mask = {};
    own.rays_radial = {};
    own.rays_failed = false;
    own.linear_sampler = VK_NULL_HANDLE;
    own.layout_materials = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.temporal = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.failed = false;
    replacing() = false;
}

} // namespace render::vulkan
