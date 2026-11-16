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
#include "engine/gfx/rt_daylight.h"
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

constexpr float kMaxDensity = 8.0F;
constexpr float kMaxStrength = 4.0F;
constexpr float kMaxHeight = 1.0e6F;
constexpr float kMaxAnisotropy = 0.9F;
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
    vec4 sun;           // xyz toward the light (the sun, or the moon at night - rt::medium_light); w the anisotropy (g)
    vec4 sun_tint;      // rgb its colour over the level's own sun's (x the sky model's); w 1: there is a light
    vec4 fog_colour;    // rgb; w the density (x the level's)
    vec4 fog_range;     // x start, y end, z 1 / the height falloff (0: even), w the frame's jitter (0 - 1)
    vec4 offsets;       // xy depth sample 0 from the pixel's centre; zw the pixel of its block this frame marches
    vec4 shares;        // x ambient, y sky, z shafts (mode 1), w 1: the light unclamped (rt_hdr)
    ivec4 rect;         // the viewport in the target's pixels
    ivec4 low;          // the low-resolution images' width and height, the scale, the steps
    ivec4 flags;        // x the mode (2: aerial), y the mask, z the longest history, w the frame's index
    vec4 aerial;        // the sky model's haze: x its extinction per unit at the ground, y 1 / its height, z the
                        // ground's y, w the share of its light the sky gives (the rest the sun's)
    vec4 sun_light;     // rgb the sun's colour (the sky model's); w how far the haze is followed
    vec4 medium;        // rgb the light the medium sends the eye per unit of light (phase 1); w the strength
    vec4 soft;          // x tan of the light's angular radius, y the water layer's extinction over the haze's at the
                        // ground, z 1 / its height (world units), w unused
    vec4 cloud_map;     // the clouds' shadow map's place (vulkan_rt_sky_pass.h CloudShadowMap)
    vec4 cloud_light;   // xyz toward its light, w its shadow's share kept (0: no map)
    vec4 air;           // (the street air - rt_volume_light.h street_air) x its extinction per unit at its ground, y 1 /
                        // its height, z its ground's y, w the falling snow's extinction per unit
    vec4 air_light;     // x its light per unit of light (phase 1), y x its shade's light, z the even lobe added to its
                        // phase (rt_volume_light.h AirArt: the air's look), w 1 / the snow's height
    vec4 air_ambient;   // rgb its shade's light where the level's fog is replaced (the fog's colour's share; the aerial
                        // march: the sky's), w the sun's brightness over its tint's (the tint is a hue over a level's fog)
                        // x the look's glow (the sun's light it scatters)
    vec4 air_look;      // (added by the cinematic air - rt_volume_light.h AirArt) x the lamps' light it scatters over
                        // the other media's, y where its volume round the eye starts thinning, z 1 / how far it thins
                        // to none (0: everywhere)
} fog;

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
// (the lamps that light the air: rt_lights' lit set for the view, the most important first)
layout(set = 0, binding = 10, std140) uniform FogLamps {
    ivec4 count;                // x lamps
    vec4 at[FOG_MOST_LAMPS];     // xyz the lamp, w its size (its shadow rays stop that short of it)
    vec4 colour[FOG_MOST_LAMPS]; // rgb its light (colour x intensity x rt_lights' strength x the lamps on), w its reach
    vec4 more[FOG_MOST_LAMPS];   // x its falloff's core
} lamps;

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

// Henyey-Greenstein x 4 pi (1 on average over the sphere)
float hg(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1e-4);
    return (1.0 - g * g) / (k * sqrt(k));
}
// the air's phase (engine/gfx/rt_volume_light.h medium_phase: Mie's strong forward lobe and a weak back one)
float phase(float c) { return MEDIUM_FORWARD * hg(c, fog.sun.w) + (1.0 - MEDIUM_FORWARD) * hg(c, MEDIUM_BACK_G); }

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

// The lamps' light at `at` toward the eye, unshadowed (x the phase): the lamps of `mask`; (added by the cinematic air)
// `even` the same light without the phase (the street air's look adds it x its even lobe)
vec3 lamps_open(vec3 at, vec3 direction, uint mask, out vec3 even)
{
    vec3 sum = vec3(0.0);
    even = vec3(0.0);
    uint left = mask;
    while (left != 0u) {
        int k = findLSB(left);
        left &= left - 1u;
        vec3 to = lamps.at[k].xyz - at;
        float d = length(to);
        float f = lamp_falloff(d, lamps.more[k].x, lamps.colour[k].w);
        if (f > 0.0) {
            sum += lamps.colour[k].rgb * (f * phase(dot(direction, to / max(d, 1e-4))));
            even += lamps.colour[k].rgb * f;
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

// (added by the street air) The street air's optical depth between s0 and s1: its mist over the level's ground, the
// falling snow, and over water its wetter layer.
// (changed by the cinematic air: its mist in a volume round the eye - rt_volume_light.h air_reach, at the segment's
// middle; the falling snow everywhere)
float air_depth(vec3 direction, float s0, float s1, float water)
{
    float depth = 0.0;
    if (fog.air.x > 0.0) {
        float mist = layer_depth(direction, s0, s1, fog.air.x, fog.air.y, fog.air.z);
        if (water > -1.0e38) {
            mist += layer_depth(direction, s0, s1, fog.air.x * fog.soft.y, fog.soft.z, water);
        }
        if (fog.air_look.z > 0.0) {
            float t = clamp((0.5 * (s0 + s1) - fog.air_look.y) * fog.air_look.z, 0.0, 1.0);
            mist *= 1.0 - t * t * (3.0 - 2.0 * t);
        }
        depth += mist;
    }
    if (fog.air.w > 0.0) {
        depth += layer_depth(direction, s0, s1, fog.air.w, fog.air_light.w, fog.air.z);
    }
    return depth;
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

// The sky model's colour of the haze toward `direction` (no lower than AERIAL_LOWEST above the horizon; the glow
// toward the sun capped: the haze no brighter than the sky at the same height a quarter turn from the sun, x (1 +
// AERIAL_GLOW) - the table's aureole near a low sun whited out a harbour looking into it)
vec3 aerial_sky(vec3 direction)
{
    vec3 level = normalize(vec3(direction.x, max(direction.y, AERIAL_LOWEST), direction.z));
    vec3 sky_colour = sky_display(level, fog.sun.xyz);
    vec2 flat_sun = fog.sun.xz;
    float flat_length = length(flat_sun);
    if (flat_length > 1e-5) {
        vec2 across = vec2(-flat_sun.y, flat_sun.x) / flat_length;
        float horizontal = length(level.xz);
        vec3 side = vec3(across.x * horizontal, level.y, across.y * horizontal);
        sky_colour = min(sky_colour, sky_display(side, fog.sun.xyz) * (1.0 + AERIAL_GLOW));
    }
    return sky_colour;
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
    bool sky = distance < 0.0;
    bool air_on = fog.air.x > 0.0 || fog.air.w > 0.0;
    // The media along the ray: the main one - the sky model's haze (AERIAL: aerial perspective, dimming what is far
    // toward the sky's colour), or the level's own fog (its linear range fog as a medium, extinction 1 / (end - d)
    // between start and end - integrated exactly over each segment, so its transmittance is the game's f, to the
    // power rt_fog_density, whatever the step count; x the height falloff), or the thin haze of a level without fog -
    // and the street air near the ground. Each scatters the light that reaches it - the sun's or the moon's, the lamps'
    // - and its shade's own light; their light mixed by their share of each segment's optical depth.
#ifdef AERIAL
    bool replace = true; // (the haze dims what it covers)
    float a = 0.0;
    float b = min(sky ? far_along(p) : distance, fog.sun_light.w);
    vec3 sky_colour = aerial_sky(direction);
    vec3 ambient = sky_colour * fog.aerial.w;
    vec3 medium = fog.medium.rgb * fog.medium.w;
    vec3 air_ambient = ambient * fog.air_light.y;
    vec3 air_medium = vec3(fog.air_light.x * fog.medium.w);
#else
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
    vec3 medium = fog.medium.rgb * (fog.medium.w * share);
    vec3 air_ambient = replace ? fog.air_ambient.rgb * fog.air_light.y : vec3(0.0);
    vec3 air_medium = vec3(fog.air_light.x * (fog.medium.w * (sky ? fog.shares.y : 1.0)));
#endif
    vec3 light_in = vec3(0.0);
    float transmittance = 1.0;
    bool lit_air = fog.sun_tint.w > 0.5 || lamps.count.x > 0;
    if (b > a && (replace || lit_air)) {
        float water = water_under(direction, distance);
        float shaped = phase(dot(direction, fog.sun.xyz));
        vec3 sun = fog.sun_tint.rgb * shaped;
        // (the street air's: its look's even lobe added - rt_volume_light.h air_phase)
        vec3 sun_air = fog.sun_tint.rgb * (shaped + fog.air_light.z);
        int steps = fog.low.w;
        float span = b - a;
        float u = fract(jitter_at(local) + fog.fog_range.w);
        uint reached = lamps_reached(direction, b);
        // (changed by the street air: the light's rays every other step) the steps in pairs: one point of each pair -
        // the first or the second, by turns frame to frame - traces the sun's (or the moon's) shadow ray and the picked
        // lamp's; both steps take what it saw
        int parity = fog.flags.w & 1;
        float sun_seen = 0.0;
        float lamp_seen = 0.0;
        // (the steps closer together near the eye - a + span x t^2: the shafts between the buildings round the camera
        // sampled every few metres, the far air - smooth - in long segments; each segment's depth is still exact)
        for (int i = 0; i < steps; ++i) {
            float t0 = float(i) / float(steps);
            float t1 = float(i + 1) / float(steps);
            float t = (float(i) + u) / float(steps);
            float s0 = a + span * t0 * t0;
            float s1 = a + span * t1 * t1;
            float s = a + span * t * t;
            vec3 at = fog.camera.xyz + direction * s;
            if ((i & 1) == 0) {
                int j = min(i + parity, steps - 1);
                float tj = (float(j) + u) / float(steps);
                vec3 traced = fog.camera.xyz + direction * (a + span * tj * tj);
                sun_seen = light_seen(traced, local, j);
                lamp_seen = reached != 0u ? lamps_seen(traced, direction, reached, local, j) : 0.0;
            }
#ifdef AERIAL
            float tau_main = layer_depth(direction, s0, s1, fog.aerial.x, fog.aerial.y, fog.aerial.z);
            if (water > -1.0e38) {
                tau_main += layer_depth(direction, s0, s1, fog.aerial.x * fog.soft.y, fog.soft.z, water);
            }
            tau_main *= fog.fog_colour.w;
#else
            float x0 = clamp(s0, start, end);
            float x1 = clamp(s1, start, end);
            float tau_main = 0.0;
            if (x1 > x0) {
                float height = fog.fog_range.z > 0.0 ? clamp(exp(-(at.y - fog.camera.y) * fog.fog_range.z), 0.0, 8.0)
                                                     : 1.0;
                // the game's linear fog as a medium: extinction 1 / (end - s), its integral over the segment exactly
                tau_main = fog.fog_colour.w * height * log(max(end - x0, 1e-4 * fade) / max(end - x1, 1e-4 * fade));
            }
#endif
            float tau_air = air_on ? air_depth(direction, s0, s1, water) : 0.0;
            float tau = max(tau_main, 0.0) + max(tau_air, 0.0);
            if (!(tau > 1e-9)) {
                continue;
            }
            float e = exp(-min(tau, 40.0));
            vec3 lamp_even = vec3(0.0);
            vec3 lamp_light = vec3(0.0);
            if (reached != 0u) {
                lamp_light = lamps_open(at, direction, reached, lamp_even) * lamp_seen;
                lamp_even *= lamp_seen * fog.air_light.z;
            }
            vec3 lamp_air = (lamp_light + lamp_even) * fog.air_look.x;
            vec3 sun_light = sun * sun_seen;
            vec3 here_main = ambient + medium * (sun_light + lamp_light);
            vec3 here_air =
                air_ambient + air_medium * (sun_air * (sun_seen * fog.air_ambient.w) + lamp_air);
            vec3 here = (max(tau_main, 0.0) * here_main + max(tau_air, 0.0) * here_air) / tau;
            light_in += transmittance * (1.0 - e) * here;
            transmittance *= e;
            if (transmittance < 1e-4) {
                transmittance = 0.0; // (nothing past here is seen)
                break;
            }
        }
    }
    imageStore(o_out, q, vec4(light_in, replace ? transmittance : 1.0));
    imageStore(o_info, q, vec4(distance, float(local.x), float(local.y), 0.0));
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
    // this frame's spread around the texel: what the history may be
    vec4 low = now;
    vec4 high = now;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 n = q + ivec2(dx, dy);
            if (inside_low(n)) {
                vec4 v = imageLoad(u_in, n);
                low = min(low, v);
                high = max(high, v);
            }
        }
    }
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

// Step 3: the composite (a blend: colour x T + the light).
constexpr const char* kCompositeBody = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_in;
layout(set = 0, binding = 5, rgba32f) uniform readonly image2D u_in_info;
layout(location = 0) out vec4 o_colour;

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
    vec3 light_in = max(v.rgb, vec3(0.0));
    if (fog.shares.w < 0.5) {
        light_in = min(light_in, vec3(1.0));
    }
    o_colour = vec4(light_in, clamp(v.a, 0.0, 1.0));
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
};
constexpr std::size_t kFogBlockBytes = 464;

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
constexpr uint32_t kBindingCount = 13;
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
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

constexpr std::size_t kSlotImages = 6;

// A viewport's images and history.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    uint32_t scale = kLeastScale; // (the march's block: its images 1 / this of the viewport's on each axis)
    Image raw;                         // step 1: the light, the transmittance
    Image raw_info;                    // step 1: the distance, the pixel marched
    std::array<Image, 2> histories;    // step 2's, this frame's and the one before (by `current`)
    std::array<Image, 2> history_info; // likewise: the distance, the history's length
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
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkPipeline, kVariants> march{}; // by variant (made on first use)
    std::array<bool, kVariants> march_failed{};
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
    rt::AirLook logged_look = rt::AirLook::kNone;
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
    if (aerial) {
        source += "#define AERIAL\n";
        source += "#define AERIAL_LOWEST " + std::to_string(kFogAerialLowest) + "\n";
        source += "#define AERIAL_GLOW " + std::to_string(kFogAerialGlow) + "\n";
    }
    if (traces) {
        source += "#extension GL_EXT_ray_query : require\n";
        source += "#define SUN_DISTANCE " + std::to_string(kFogSunDistance) + "\n";
        source += "#define FOG_MOST_LAMPS " + std::to_string(kFogMostLamps) + "\n";
        source += "#define MEDIUM_FORWARD " + std::to_string(rt::kMediumForwardShare) + "\n";
        source += "#define MEDIUM_BACK_G " + std::to_string(rt::kMediumBackG) + "\n";
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
    if (aerial) {
        source += sky_table_glsl(kBindingSky);
    }
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
        } else if (binding == kBindingDepth || binding == kBindingSky || binding == kBindingCloudMap) {
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

// The composite pass's pipeline for a colour target (made on first use).
VkPipeline composite_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    FogState& own = fog_state();
    for (const CompositePipeline& made : own.composites) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const VkShaderModule fragment = make_module(
        device, RtShaderStage::kFragment, source_of(kCompositeBody, false, samples != VK_SAMPLE_COUNT_1_BIT), error);
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
    own.composites.push_back({.format = format, .samples = samples, .pipeline = pipeline});
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
            &slot.histories.front(),
            &slot.histories.back(),
            &slot.history_info.front(),
            &slot.history_info.back()};
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
                make_storage_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, extent, &slot.raw_info);
    for (std::size_t i = 0; i < 2; ++i) {
        made = made && make_storage_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, extent, &core::at(slot.histories, i)) &&
               make_storage_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, extent, &core::at(slot.history_info, i));
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
    const std::array<std::pair<uint32_t, const Image*>, 6> storage{{{kBindingOut, bound.out},
                                                                    {kBindingOutInfo, bound.out_info},
                                                                    {kBindingIn, bound.in},
                                                                    {kBindingInInfo, bound.in_info},
                                                                    {kBindingHistory, bound.history},
                                                                    {kBindingHistoryInfo, bound.history_info}}};
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
    if (!rt::baked_sun_wanted()) {
        rt::set_baked_sun_wanted(true);
    }
    own.light = rt::medium_light();
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
    if (sun.sky_model || sun.colour_set) {
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

FogBlock block_for(const rt::ViewDrawn& view, const Matrix& to_world, const ViewSlot& slot, bool have_before,
                   VkSampleCountFlagBits samples)
{
    const FogState& own = fog_state();
    FogBlock block{};
    block.to_world = column_major(to_world);
    block.to_before = column_major(slot.world_to_pixel);
    block.camera = {core::at(view.camera.position, 0), core::at(view.camera.position, 1),
                    core::at(view.camera.position, 2), 0.0F};
    block.camera_before = {core::at(slot.camera, 0), core::at(slot.camera, 1), core::at(slot.camera, 2),
                           have_before ? 1.0F : 0.0F};
    block.sun = {core::at(own.to_sun, 0), core::at(own.to_sun, 1), core::at(own.to_sun, 2), own.settings.anisotropy};
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
    // (the haze is thicker round sunrise and sunset; the game's own fog keeps its thickness)
    const auto condition = static_cast<float>(own.light.have ? rt::haze_condition(own.light.sun_elevation) : 1.0);
    block.fog_colour = {core::at(view.fog.colour, 0), core::at(view.fog.colour, 1), core::at(view.fog.colour, 2),
                        own.settings.density * (own.haze && !own.aerial ? condition : 1.0F)};
    const double jitter = std::fmod(static_cast<double>(slot.passes % (1U << 20U)) * kGoldenRatio, 1.0);
    block.fog_range = {view.fog.start, view.fog.end, own.settings.height > 0.0F ? 1.0F / own.settings.height : 0.0F,
                       static_cast<float>(jitter)};
    const std::array<float, 2> sample = sample_zero_offset(samples, own.standard_samples);
    // the pixel of its block each texel marches this frame (kBayer4 / kBayer2's order)
    const std::array<float, 2> pick = block_pixel(slot.scale, slot.passes);
    block.offsets = {sample.front(), sample.back(), pick.front(), pick.back()};
    // (no light - indoors: the replaced fog is the game's colour whole, the lamps' light added in it)
    block.shares = {own.have_sun ? kFogAmbientShare : 1.0F, kFogSkyShare, kFogShaftShare, own.settings.hdr ? 1.0F : 0.0F};
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
                    core::at(view.fog.colour, 2) * light_share, own.settings.strength};
    const double radius = static_cast<double>(own.light.radius_degrees) * std::numbers::pi / 180.0;
    block.soft = {own.light.have ? static_cast<float>(std::tan(radius)) : 0.0F, 0.0F, 0.0F, 0.0F};
    if (own.have_cloud) {
        block.cloud_map = own.cloud.place;
        block.cloud_light = own.cloud.light;
    }
    // (the level's ground: the haze and the street air thicker toward it)
    const rt::BakedSun& baked = rt::baked_sun();
    const float ground = baked.scene != 0 && baked.ground_known ? baked.ground : core::at(view.camera.position, 1);
    // (added by the street air) the near-ground air (rt_volume_light.h street_air): outdoors - where a sun or moon
    // lights the level -, as thick as the light's time of day and the weather make it x rt_fog_air; its light the
    // haze's, x the light's whole colour (over a level's fog the tint is a hue: its brightness given back here)
    if (own.light.have) {
        const rt::StreetAir air = rt::street_air(own.light.sun_elevation, rt::air_weather(), own.settings.air);
        // (over the level's street level - the middle of its upward faces -, not its lowest floor)
        const float street = baked.scene != 0 && baked.street_known ? baked.street : ground;
        block.air = {static_cast<float>(air.extinction), air.height > 0.0 ? static_cast<float>(1.0 / air.height) : 0.0F,
                     street, static_cast<float>(air.snow)};
        // (the air's look: its shade's light and the even lobe of its phase - rt_volume_light.h AirArt)
        const rt::AirArt art = rt::air_art(own.settings.air);
        const float scatter = (1.0F - kFogAerialSkyShare) * rt::kHazeSunlit;
        block.air_light = {scatter, static_cast<float>(art.shade), static_cast<float>(art.side),
                           air.snow_height > 0.0 ? static_cast<float>(1.0 / air.snow_height) : 0.0F};
        const float tint_luma = (0.2126F * core::at(tint, 0)) + (0.7152F * core::at(tint, 1)) +
                                (0.0722F * core::at(tint, 2));
        const float whole_luma = (0.2126F * core::at(own.sun_tint, 0)) + (0.7152F * core::at(own.sun_tint, 1)) +
                                 (0.0722F * core::at(own.sun_tint, 2));
        block.air_ambient = {core::at(view.fog.colour, 0) * kFogAmbientShare,
                             core::at(view.fog.colour, 1) * kFogAmbientShare,
                             core::at(view.fog.colour, 2) * kFogAmbientShare,
                             (tint_luma > 1e-4F ? whole_luma / tint_luma : 1.0F) * static_cast<float>(art.glow)};
        const double range = art.range * rt::kUnitsPerMetre;
        block.air_look = {static_cast<float>(art.lamp_glow), static_cast<float>(0.5 * range),
                          range > 0.0 ? static_cast<float>(2.0 / range) : 0.0F, 0.0F};
    }
    if (own.cutouts) {
        // (the wetter air over water: the haze's and the street air's)
        core::at(block.soft, 1) = static_cast<float>(rt::kWaterHazeExtra);
        core::at(block.soft, 2) = static_cast<float>(1.0 / rt::kWaterHazeHeight);
    }
    if (own.aerial) {
        // (the sky model's haze: thicker toward the level's ground)
        constexpr int32_t kAerialMode = 2;
        core::at(block.flags, 0) = kAerialMode;
        block.aerial = {kFogAerialExtinction * own.settings.haze_density * condition,
                        own.settings.haze_height > 0.0F ? 1.0F / own.settings.haze_height : 0.0F, ground,
                        kFogAerialSkyShare};
        block.sun_light = {core::at(own.sun_colour, 0), core::at(own.sun_colour, 1), core::at(own.sun_colour, 2),
                           kFogAerialFar};
        // (the haze's own light: as bright as the day sky by the horizon under the level's own sun, x the light's
        // colour now; over water a wetter layer - with the material table to tell water)
        const float sunlit = (1.0F - kFogAerialSkyShare) * rt::kHazeSunlit;
        block.medium = {sunlit, sunlit, sunlit, own.settings.strength};
    }
    return block;
}

// What the pass draws (for the log).
const char* what_is_drawn()
{
    const FogState& own = fog_state();
    if (own.aerial) {
        return "no fog in the level: the sky model's haze (aerial perspective)";
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
                  .sky = own.aerial,
                  .cloud_map = own.have_cloud,
                  .cutouts = own.cutouts,
                  .lamps = lamps_info,
                  .depth = depth,
                  .out = &slot->raw,
                  .out_info = &slot->raw_info,
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
    // 3. the composite
    record_composite(commands, composite, colour,
                     {.depth = depth, .in = history, .in_info = history_info, .block = block_info}, clipped.rect);
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
    if (core::at(block.air, 0) > 0.0F && (std::abs(core::at(block.air, 2) - own.logged_street) > kStreetLoggedApart ||
                                          own.settings.air != own.logged_look)) {
        own.logged_street = core::at(block.air, 2);
        own.logged_look = own.settings.air;
        get_log("vulkan: rt_fog: the street air (%s) over y %.0f (extinction 1/%.0f a unit there, %.0f units its height; "
                "falling snow 1/%.0f; its shade x %.2f, its phase + %.2f)",
                rt::air_look_name(own.settings.air), static_cast<double>(core::at(block.air, 2)),
                1.0 / static_cast<double>(core::at(block.air, 0)),
                core::at(block.air, 1) > 0.0F ? 1.0 / static_cast<double>(core::at(block.air, 1)) : 0.0,
                core::at(block.air, 3) > 0.0F ? 1.0 / static_cast<double>(core::at(block.air, 3)) : 0.0,
                static_cast<double>(core::at(block.air_light, 1)), static_cast<double>(core::at(block.air_light, 2)));
    }
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_fog: the first fog pass (%ux%u at 1/%u, %u steps, %s, fog %.0f - %.0f, density %.2f, "
                "height %.0f, anisotropy %.2f, %s)",
                width, height, slot->scale, own.settings.steps, what_is_drawn(), static_cast<double>(clipped.fog.start),
                static_cast<double>(clipped.fog.end), static_cast<double>(own.settings.density),
                static_cast<double>(own.settings.height), static_cast<double>(own.settings.anisotropy),
                own.have_sun ? "a sun" : "no sun");
        if (own.aerial) {
            get_log("vulkan: rt_fog: the haze's ground at y %.0f (extinction 1/%.0f a unit there, %.0f units its height)",
                    static_cast<double>(core::at(block.aerial, 2)),
                    static_cast<double>(1.0F / (kFogAerialExtinction * own.settings.haze_density)),
                    static_cast<double>(own.settings.haze_height));
        }
    }
    return true;
}

} // namespace

void set_rt_fog_settings(const RtFogSettings& settings)
{
    RtFogSettings kept = settings;
    kept.density = std::isfinite(kept.density) ? std::clamp(kept.density, 0.0F, kMaxDensity) : 1.0F;
    kept.height = std::isfinite(kept.height) ? std::clamp(kept.height, 0.0F, kMaxHeight) : 0.0F;
    kept.anisotropy = std::isfinite(kept.anisotropy) ? std::clamp(kept.anisotropy, -kMaxAnisotropy, kMaxAnisotropy)
                                                     : kRtFogAnisotropy;
    kept.steps = std::clamp(kept.steps, kMinSteps, kMaxSteps);
    kept.lamps = std::min(kept.lamps, kFogMostLamps);
    kept.scale = kept.scale >= kMostScale ? kMostScale : kLeastScale;
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, kMaxStrength) : kRtFogStrength;
    set_cloud_map_wanted(kept.on); // (the clouds' shadow map for the shafts through their gaps)
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
        }
        if (!finish_work(state)) {
            return false;
        }
        constexpr std::size_t kChannels = 4;
        const bool wide = image->format == VK_FORMAT_R32G32B32A32_SFLOAT;
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
    if (device != nullptr) {
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
