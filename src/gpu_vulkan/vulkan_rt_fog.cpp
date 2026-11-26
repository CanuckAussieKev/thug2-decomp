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
#include "engine/gfx/gpu_markers.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_debug_view.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_light_list.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_pass_profile.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_history.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_restir_gi.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_surface_light.h"
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
// (round FOG - the froxel grid) its slices (a multiple of kSliceColumns), its columns' side in pixels, the samples a
// froxel takes a frame
constexpr unsigned kMinSteps = 16;
constexpr unsigned kMaxSteps = 128;
constexpr uint32_t kSliceColumns = 8; // (a volume's slices side by side across its image, this many a row)
constexpr uint32_t kLeastScale = 2;
constexpr uint32_t kMostScale = 32;
constexpr unsigned kMostSamples = 16;
// the slices' distribution: n = the grid's far / this - each slice ~ 1 / 10 deeper than the one before past n
constexpr float kSliceSpread = 400.0F;
constexpr double kGoldenRatio = 0.6180339887498949; // the samples' place along a froxel advances by this a frame
constexpr uint32_t kGroupSide = 8;
constexpr std::size_t kMatrixSide = 4;
constexpr std::size_t kMatrixFloats = kMatrixSide * kMatrixSide;
constexpr double kSingular = 1e-300;
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

// What every pass shares: the block (FogBlock below), the depth, the view rays and the froxel grid.
constexpr const char* kCommon = R"(
layout(set = 0, binding = 7) uniform FogBlock {
    mat4 to_world;      // the target's pixel (x, y, depth, 1) -> world
    mat4 to_before;     // world -> the target's pixel in the viewport's frame before (homogeneous)
    vec4 camera;        // xyz
    vec4 camera_before; // xyz; w 1: there is a history
    vec4 sun;           // xyz toward the light (the sun, or the moon at night - rt::medium_light); w the aerosol's g
    vec4 sun_tint;      // rgb its colour over the level's own sun's (the aerial air: its irradiance in the sky's unit,
                        // linear); w 1: there is a light
    vec4 fog_colour;    // rgb; w the density (x the level's)
    vec4 fog_range;     // x start, y end, z 1 / the height falloff (0: even), w the frame's jitter (0 - 1)
    vec4 offsets;       // xy depth sample 0 from the pixel's centre; zw unused
    vec4 shares;        // x ambient, y sky, z shafts (mode 1), w the history's light into this frame's pre-exposure
    ivec4 rect;         // the viewport in the target's pixels
    ivec4 low;          // the froxel grid: its columns across and down, its slices, the samples a froxel takes a frame
    ivec4 flags;        // x the mode (2: aerial), y the mask, z the frames the froxels' history holds, w the frame's index
    vec4 aerial;        // (the aerial air) x the air's 1 / height (Rayleigh's, per world unit), y the level's ground's y,
                        // z the aerosol's 1 / height, w the mist's density over it
    vec4 sun_light;     // w how far the aerial air is followed (rgb unused)
    vec4 medium;        // rgb the light the medium sends the eye per unit of light (phase 1); the aerial air: the air's
                        // Rayleigh scattering per world unit at the ground
    vec4 soft;          // x tan of the light's angular radius, yzw unused
    vec4 cloud_map;     // the clouds' shadow map's place (vulkan_rt_sky_pass.h CloudShadowMap)
    vec4 cloud_light;   // xyz toward its light, w its shadow's share kept (0: no map)
    vec4 air;           // (the street air - rt_volume_light.h street_air) x its extinction per unit at its ground, y 1 /
                        // its height, z its ground's y, w the falling snow's extinction per unit
    vec4 air_light;     // x its light per unit of light (phase 1), y x its shade's light, z unused, w 1 / the snow's
                        // height
    vec4 air_ambient;   // rgb its shade's light where the level's fog is replaced (the fog's colour's share), w the sun's
                        // brightness over its tint's (the tint is a hue over a level's fog)
    vec4 air_look;      // x 1: the sky's pixels left alone (the atmosphere drawn holds the whole ray - the aerial air),
                        // y the air's extinction at the eye's height (the aerial air: the god rays' share before what
                        // stands toward the light), zw unused
    // (the distinct shafts - rt_volume_light.h air_map_half) the air's sun map: its square across the light's way - xyz
    // its side's and its up's axes, the side's w its half side (world units; 0: no map), the up's w how far below its
    // centre's plane its rays reach; the centre's xyz the eye's place snapped to its texels, w how far up the light's way
    // its rays start
    vec4 map_side;
    vec4 map_up;
    vec4 map_centre;
    // (the god rays - rt_volume_light.h god_ray_peak) xy the light's place on the screen in the rays' images' texels
    // (half the viewport's pixels), z rt_fog_god_rays, w 0: off (the froxels keep the whole phase), 1: on, the light in
    // front of the eye (the radial passes run), 2: on, the light behind it
    vec4 rays;
    // (the physical air - the aerial air; rt_sky.h Aerosol) the weather's aerosol per world unit at the ground: its
    // scattering (rgb, w the mist's 1 / height), its extinction (rgb, w the falling rain's or snow's extinction), the
    // light scattered more than once per unit of scattering (rgb: Psi at the ground x the light's irradiance, the sky's
    // unit; w the precipitation's 1 / height)
    vec4 aerosol_s;
    vec4 aerosol_t;
    vec4 ambient;
    // (round WF) the light list's local group (vulkan_light_list.h): x its entries (0: none), y the candidates a sample
    // resamples; the TLAS instances' device address (xy) and count (z); x the frame's pre-exposure, y the in-scattered
    // light kept (0: a debug view without it)
    uvec4 list;
    uvec4 instances;
    vec4 list_light;
    // (round FOG - the froxels) x the grid's far distance, y its slices' scale (the distribution's near distance), zw
    // the same of the frame before (the history's grid)
    vec4 grid;
    // (round FOG) the light grid's picks this frame (vulkan_light_list.h light_grid_reader_glsl: xy their address, z 1
    // when there) - RTXDI's ReGIR: a froxel inside ReSTIR DI's light grid takes its candidates from its cell's picks
    uvec4 picks;
    // (round MEDIUM - rt_volume_light.h open_sky_phase_mean) the mean of 4 pi x the aerosol's phase over the upper
    // hemisphere for view elevations' sines -1 .. 1 (32 entries)
    vec4 sky_phase[8];
} fog;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// (round FOG) a value kept to what the volumes' 16-bit floats hold - a sample that came out not finite counts as none
// (the frame's colour, and Ray Reconstruction's history after it, must never take a NaN)
bool finite4(vec4 v) { return !any(isnan(v)) && !any(isinf(v)); }
vec4 finite_or_zero(vec4 v) { return finite4(v) ? clamp(v, vec4(-65504.0), vec4(65504.0)) : vec4(0.0); }

// the aerosol's phase, Cornette-Shanks (per steradian - rt_sky.h aerosol_phase)
float cs(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1e-6);
    return 0.119366207 * (1.0 - g * g) * (1.0 + c * c) / ((2.0 + g * g) * k * sqrt(k));
}
// the air's phase x 4 pi (rt_volume_light.h medium_phase: 1 on average over the sphere)
float phase(float c) { return 12.5663706 * cs(c, fog.sun.w); }
// (the god rays) the forward peak the screen's shadows resolve (rt_volume_light.h god_ray_peak, x 4 pi): 0 while off
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
// The view ray through a point of the target in its pixels' coordinates (a pixel's centre at its index).
vec3 direction_through(vec2 at)
{
    vec4 w = fog.to_world * vec4(at, 0.5, 1.0);
    vec3 to = w.xyz / w.w - fog.camera.xyz;
    float length_of = length(to);
    return length_of > 1e-12 ? to / length_of : vec3(0.0, 0.0, -1.0);
}

// The froxel grid (Wronski 2014, Hillaire 2015, UE's volumetric fog): fog.low.x x fog.low.y columns over the viewport,
// each fog.low.z slices deep - slice x's near face at n ((far / n + 1)^(x / slices) - 1) from the eye (exponential: each
// slice the same share deeper than the one before past n, from the eye at 0 to the grid's far at the last). A volume is
// a 2D image of the slices side by side, SLICE_COLUMNS of them across.
float slice_distance(float x, float far, float n) { return n * (pow(far / n + 1.0, x / float(fog.low.z)) - 1.0); }
float slice_of(float d, float far, float n) { return float(fog.low.z) * log(max(d, 0.0) / n + 1.0) / log(far / n + 1.0); }
float slice_distance(float x) { return slice_distance(x, fog.grid.x, fog.grid.y); }
float slice_of(float d) { return slice_of(d, fog.grid.x, fog.grid.y); }
ivec2 froxel(ivec2 column, int slice)
{
    return column + ivec2((slice % SLICE_COLUMNS) * fog.low.x, (slice / SLICE_COLUMNS) * fog.low.y);
}
// the size of a column in the target's pixels
vec2 column_size() { return vec2(fog.rect.zw) / vec2(fog.low.xy); }

// The optical depth of a layer between s0 and s1 along the view ray (y linear along it): extinction `sigma` at height
// `ground`, thinning by e every 1 / inv_h above it, `sigma` at and below it - in closed form (round MEDIUM: the medium's
// one definition, rt_volume_light.h kMediumGlsl - the path tracer's too).
float layer_depth(vec3 direction, float s0, float s1, float sigma, float inv_h, float ground)
{
    return sigma * medium_layer_depth(fog.camera.y - ground, direction.y, s0, s1, inv_h);
}

// The street air's optical depth between s0 and s1: the weather's air over the level's ground and the falling snow.
float air_depth(vec3 direction, float s0, float s1)
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

#ifndef AERIAL
// The level's own fog between s0 and s1 (round UNITS - rt_volume_light.h level_fog_extinction): an even medium of
// extinction fog.medium.w per world unit (Koschmieder's, from the game's distances) from `start` on, x rt_fog_density,
// x the height falloff at the segment's middle.
float main_depth(vec3 direction, float s0, float s1)
{
    float start = fog.fog_range.x;
    float x0 = max(s0, start);
    float x1 = max(s1, start);
    if (!(x1 > x0)) {
        return 0.0;
    }
    float height = 1.0;
    if (fog.fog_range.z > 0.0) {
        float y = fog.camera.y + direction.y * 0.5 * (s0 + s1);
        height = clamp(exp(-(y - fog.camera.y) * fog.fog_range.z), 0.0, 8.0);
    }
    return max(fog.fog_colour.w * fog.medium.w * height * (x1 - x0), 0.0);
}
#endif

// The media's optical depth between s0 and s1 along `direction` (the extinction's luma for the aerial air: one
// transmittance for the three channels).
float media_depth(vec3 direction, float s0, float s1)
{
#ifdef AERIAL
    float d_r = layer_depth(direction, s0, s1, 1.0, fog.aerial.x, fog.aerial.y);
    float d_a = layer_depth(direction, s0, s1, 1.0, fog.aerial.z, fog.aerial.y);
    if (fog.aerial.w > 0.0) {
        d_a += fog.aerial.w * layer_depth(direction, s0, s1, 1.0, fog.aerosol_s.w, fog.aerial.y);
    }
    float d_p = fog.aerosol_t.w > 0.0 ? layer_depth(direction, s0, s1, fog.aerosol_t.w, fog.ambient.w, fog.aerial.y)
                                      : 0.0;
    return dot(fog.medium.rgb * d_r + fog.aerosol_t.rgb * d_a + vec3(d_p), LUMA);
#else
    bool air_on = fog.air.x > 0.0 || fog.air.w > 0.0;
    return main_depth(direction, s0, s1) + (air_on ? max(air_depth(direction, s0, s1), 0.0) : 0.0);
#endif
}
)";

// Step 1: the froxels' light (one froxel each) - the light that reaches the air in it, FROXEL_SAMPLES points a frame
// (each jittered inside the froxel), with the history of the frames before reprojected and blended in.
constexpr const char* kLightBody = R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
// rgb the local lights' light scattered toward the eye (the aerosol's or the level fog's phase), a the sun's or the
// moon's share seen; rgb the same with Rayleigh's phase (the aerial air), a the sky's open share
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_light;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_light_b;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D u_history;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D u_history_b;
// (the distinct shafts) the air's sun map (step 0's): each texel the distance down the light's way to the first thing
// its ray hits
layout(set = 0, binding = 13, r32f) uniform readonly image2D u_air_map;

// Whether the light gets from `at` along `toward` for `reach` (the world and the objects; with CUTOUTS the cut-outs by
// their alpha too - the one lighting path's test, vulkan_rt_scene.h cutout_rays_glsl).
float visible(vec3 at, vec3 toward, float reach)
{
#ifdef CUTOUTS
    return ray_light_share(uint(fog.flags.y) & 0xFFu, at, toward, reach);
#else
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          uint(fog.flags.y) & 0xFFu, at, 0.0, toward, reach);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
#endif
}

// a number in [0, 1) for the froxel, the frame and the sample
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

// (the indoor air; round MEDIUM - rt_volume_light.h open_sky_phase_mean) How much of the open air's light from the sky
// reaches the air at `at` toward the eye (seen along `d`): a ray toward a point of the upper hemisphere, even in solid
// angle, weighed by the medium's phase toward the eye over its mean for the view's elevation (`rayleigh` / `aerosol`:
// each phase's share of the scattering here) - the samples and the history average it into the open share of the
// phase-weighted sky (1 in the open). The light scattered more than once (Psi) and the sky's own light reach the air
// only through what is open, and send the eye what the phase sends from where they come in.
float sky_phase_mean(float y)
{
    float x = clamp((y + 1.0) * 0.5 * 31.0, 0.0, 31.0);
    int i = int(floor(x));
    int k = min(i + 1, 31);
    return mix(fog.sky_phase[i / 4][i % 4], fog.sky_phase[k / 4][k % 4], x - float(i));
}
float sky_seen(vec3 at, vec3 d, float rayleigh, float aerosol, ivec2 p, int i)
{
    float z = hash01(p, fog.flags.w, i, 0x3Bu);
    float a = 6.2831853 * hash01(p, fog.flags.w, i, 0xC5u);
    float r = sqrt(max(1.0 - z * z, 0.0));
    vec3 w = vec3(r * cos(a), z, r * sin(a));
    float c = dot(w, d);
    float mean = rayleigh + aerosol * sky_phase_mean(d.y);
    if (!(mean > 0.0)) {
        return 0.0;
    }
    float weight = (rayleigh * 0.75 * (1.0 + c * c) + aerosol * phase(c)) / mean;
    return weight * visible(at, w, SUN_DISTANCE);
}

// (the distinct shafts - rt_volume_light.h air_map_half) How much of the light reaches `at` by the air's sun map: -1
// where `at` is outside its square (or there is none) - the light's ray is traced there instead. Its penumbra: the place
// read moved across the light's disc by the distance from the nearest thing round there that shadows it x the disc's
// tangent; then the four texels round it, bilinearly - lit where the point lies before their first hit.
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

// (round WF - path tracing, stage 7) The local lights' light scattered at `at` toward the eye along `direction`: the
// light list's local group - every lamp and emitting surface of the level, by power: the lights ReSTIR DI, GI and the
// reference path tracer sample - by resampled importance sampling (RIS - Talbot et al. 2005; the volumetric form of Lin,
// Wyman and Yuksel 2021's ReSTIR for participating media, without the reuse): fog.list.y candidates from the presampled
// pool, weighed toward the target p^ = the luminance of the irradiance it sends here x the aerosol's phase toward the
// eye, one chosen, its shadow ray traced (the one light ray). Returns the estimate of E x phase x 4 pi / pi (a white
// diffuser's radiance - the unit the level fog's light has always had), pre-exposed; `rayleigh` the same sample x
// Rayleigh's phase x 4 pi. Unbiased for one sample; the froxel's samples and its history average them.
// (Round FOG) `near`: the froxel's own size - a light's point nearer the sample than that is taken as that far (its
// inverse-square light averaged over the cell the froxel stands for, as voxel and froxel lighting regularise it): a point
// sampled a hair from a neon strip's sampled point otherwise sent that one froxel thousands of times its neighbours'
// light, a blotch that moved with the next frame's picks.
#ifdef LIGHT_LIST
vec3 list_lights(vec3 at, vec3 direction, ivec2 p, int i, float near, out vec3 rayleigh)
{
    rayleigh = vec3(0.0);
    uint count = fog.list.x;
    uint candidates = fog.list.y;
    if (count == 0u || candidates == 0u) {
        return vec3(0.0);
    }
    float wsum = 0.0;
    float chosen_target = 0.0;
    LocalLight chosen = LocalLight(vec3(0.0), vec3(0.0, 1.0, 0.0), 0.0, 0.0);
    bool have = false;
    for (uint k = 0u; k < candidates; ++k) {
        uint salt = 0x1000u + k * 4u;
        float u = hash01(p, fog.flags.w, i, salt);
        LocalLight l;
#ifdef LIGHTS_PICKED_HERE
        // (round OM - the checks': the pick made here, as before the pool - the same distribution)
        bool seen = local_light_at(light_pick(u, count), count,
                                   vec2(hash01(p, fog.flags.w, i, salt + 1u), hash01(p, fog.flags.w, i, salt + 2u)), at, l);
#else
        // (round FOG - NVIDIA RTXDI's ReGIR, as the light list's readers at a surface take it: light_sample_glsl) inside
        // ReSTIR DI's light grid a candidate comes from the picks of the cell round the point moved by up to a cell - the
        // lights near it, each with its slot's pdf (generalised RIS stays unbiased); outside it from the pool. (A cell's
        // picks are the same for every point in it all frame: moved by a whole cell, a froxel's candidates come from the 27
        // cells round it, not 8 - the frame's error of one cell's picks shared over a wider region, fainter.) The
        // pool by power alone put a small bright light among hundreds in about one candidate set in tens: the air near
        // BO's neon strips lit by rare picks, blotches that crawled
        LightPick pick;
        bool from_grid = false;
#ifdef LL_GRID
        if (LL_GRID_ON) {
            vec4 place = uintBitsToFloat(light_grid[0]);
            vec3 jitter = (vec3(hash01(p, fog.flags.w, i, salt + 0x101u), hash01(p, fog.flags.w, i, salt + 0x102u),
                               hash01(p, fog.flags.w, i, salt + 0x103u)) - 0.5) * (2.0 * place.w);
            uint slot;
            if (place.w > 0.0 && light_grid_slot(floor((at + jitter - place.xyz) / place.w), u, slot)) {
                pick = light_pick_words(light_grid[2u * slot], light_grid[2u * slot + 1u]);
                from_grid = true;
            }
        }
#endif
        if (!from_grid) {
            pick = light_pool_read(min(uint(u * float(LIGHT_POOL_SIZE)), LIGHT_POOL_SIZE - 1u));
        }
        bool seen = light_seen_from(pick, at, l);
#endif
        if (!seen || !(l.pdf > 0.0)) {
            continue;
        }
        float d2 = l.reach * l.reach;
        l.light *= d2 / max(d2, near * near);
        float target = dot(l.light, LUMA) * phase(dot(direction, l.dir));
        float w = target / l.pdf;
        if (!(w > 0.0) || isinf(w) || isnan(w)) {
            continue;
        }
        wsum += w;
        if (hash01(p, fog.flags.w, i, salt + 3u) * wsum < w) {
            chosen = l;
            chosen_target = target;
            have = true;
        }
    }
    if (!have || !(chosen_target > 0.0)) {
        return vec3(0.0);
    }
    float weight = wsum / (float(candidates) * chosen_target);
    float seen = visible(at, chosen.dir, chosen.reach);
    if (!(seen > 0.0)) {
        return vec3(0.0);
    }
    float c = dot(direction, chosen.dir);
    vec3 e = chosen.light * (weight * seen * fog.list_light.x * 0.318309886);
    rayleigh = e * (0.75 * (1.0 + c * c));
    return e * phase(c);
}
#endif

// The history at a place of the grid of the frame before (continuous texel coordinates, a texel's centre at its index):
// trilinear over the eight froxels round it inside the grid; false where it lies outside.
bool history_at(vec3 g, out vec4 a, out vec4 b)
{
    a = vec4(0.0);
    b = vec4(0.0);
    vec3 size = vec3(fog.low.xyz);
    if (any(lessThan(g, vec3(-0.5))) || any(greaterThan(g, size - 0.5))) {
        return false;
    }
    g = clamp(g, vec3(0.0), size - 1.0);
    ivec3 base = ivec3(floor(g));
    vec3 f = g - vec3(base);
    for (int corner = 0; corner < 8; ++corner) {
        ivec3 o = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
        ivec3 n = min(base + o, fog.low.xyz - 1);
        vec3 w3 = mix(vec3(1.0) - f, f, vec3(o));
        float w = w3.x * w3.y * w3.z;
        ivec2 t = froxel(n.xy, n.z);
        a += w * imageLoad(u_history, t);
        b += w * imageLoad(u_history_b, t);
    }
    return true;
}

void main()
{
    ivec3 f = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(f, fog.low.xyz))) {
        return;
    }
    ivec2 p = ivec2(f.x, f.y + fog.low.y * f.z); // (the samples' key)
    vec2 cell = column_size();
    vec2 corner = vec2(fog.rect.xy) + vec2(f.xy) * cell - 0.5; // (the column's corner, in the pixels' coordinates)
    vec3 centre_direction = direction_through(corner + 0.5 * cell);
    bool lamps_on = fog.list.x > 0u;
    // (the froxel's size: half its diagonal - across, its column's width at its middle; along, its slice's depth)
    float s_near = slice_distance(float(f.z));
    float s_far = slice_distance(float(f.z + 1));
    float across = length(direction_through(corner + cell) - direction_through(corner)) * 0.5 * (s_near + s_far);
    float near = 0.5 * sqrt(across * across + (s_far - s_near) * (s_far - s_near));
#ifdef AERIAL
    bool need_sky = true;
#else
    // (round UNITS) the open sky lights the replaced fog's air where the lighting has a sky
    bool need_sky = fog.flags.x == 0 && dot(fog.medium.rgb, vec3(1.0)) > 0.0;
#endif
    int samples = max(fog.low.w, 1);
    vec4 a = vec4(0.0);
    vec4 b = vec4(0.0);
    int seen = 0;
    for (int j = 0; j < samples; ++j) {
        // a point in the froxel: across it at random, along it stratified (the frame's golden-ratio offset)
        vec2 across = vec2(hash01(p, fog.flags.w, j, 0x21u), hash01(p, fog.flags.w, j, 0x22u));
        float along = fract(fog.fog_range.w + (float(j) + hash01(p, fog.flags.w, j, 0x23u)) / float(samples));
        vec2 through = corner + across * cell;
        vec3 direction = direction_through(through);
        float s_at = slice_distance(float(f.z) + along);
        // (round MEDIUM) only the air the eye sees: a point past the surface its pixel shows (behind a wall - the
        // sunlit, open air outside a hall) lights no pixel through this froxel; it is left out of the froxel's light
        // (none of its points seen this frame: the froxel keeps its history). Before, a froxel whose slice reached
        // through a wall took the outside's sky and sun into the air in front of it - Training's hall a blue haze
        // along its walls the reference's air doesn't have.
        ivec2 pixel = clamp(ivec2(floor(through + 0.5)), fog.rect.xy, fog.rect.xy + fog.rect.zw - 1);
        float surface = distance_of(pixel);
        if (surface >= 0.0 && s_at > surface) {
            continue;
        }
        ++seen;
        vec3 at = fog.camera.xyz + direction * s_at;
        float sun_seen = map_light_seen(at, p, j);
        if (sun_seen < 0.0) {
            sun_seen = light_seen(at, p, j);
        }
        vec3 lamp = vec3(0.0);
        vec3 lamp_rayleigh = vec3(0.0);
#ifdef LIGHT_LIST
        if (lamps_on) {
            lamp = list_lights(at, centre_direction, p, j, near, lamp_rayleigh);
        }
#endif
        float open = 1.0;
        if (need_sky) {
            // (round MEDIUM) each phase's share of the scattering here: the molecules' and the aerosol's (its mist and
            // the falling drops taken with it) in the atmosphere's air, the aerosol's alone in the street air
            float rayleigh = 0.0;
            float aerosol = 1.0;
#ifdef AERIAL
            float u = at.y - fog.aerial.y;
            rayleigh = dot(fog.medium.rgb, LUMA) * medium_layer_density(u, fog.aerial.x);
            aerosol = dot(fog.aerosol_s.rgb, LUMA) * (medium_layer_density(u, fog.aerial.z) +
                                                      fog.aerial.w * medium_layer_density(u, fog.aerosol_s.w)) +
                      fog.aerosol_t.w * medium_layer_density(u, fog.ambient.w);
#endif
            open = sky_seen(at, direction, rayleigh, aerosol, p, j);
        }
        a += vec4(lamp, sun_seen);
        b += vec4(lamp_rayleigh, open);
    }
    if (seen == 0) {
        // (nothing of the froxel seen this frame: its history, or no light)
        a = vec4(0.0);
        b = vec4(0.0);
    } else {
        a /= float(seen);
        b /= float(seen);
    }
    // the history (Frostbite's, UE's): the froxel's centre where the grid of the frame before had it, its light
    // trilinearly read there and blended in exponentially - 1 / (frames + 1) while the history is young, then
    // 1 / HISTORY_FRAMES; none where it was outside that grid
    float keep = 0.0;
    if (fog.camera_before.w > 0.5) {
        vec3 centre = fog.camera.xyz + centre_direction * slice_distance(float(f.z) + 0.5);
        vec4 before = fog.to_before * vec4(centre, 1.0);
        if (before.w > 0.0) {
            vec2 pixel = before.xy / before.w - vec2(fog.rect.xy);
            vec3 g = vec3((pixel + 0.5) / cell - 0.5,
                          slice_of(length(centre - fog.camera_before.xyz), fog.grid.z, fog.grid.w) - 0.5);
            vec4 ha;
            vec4 hb;
            if (history_at(g, ha, hb)) {
                ha.rgb *= fog.shares.w; // (round PX: the history's light into this frame's pre-exposure)
                hb.rgb *= fog.shares.w;
                if (!any(isnan(ha)) && !any(isnan(hb)) && !any(isinf(ha)) && !any(isinf(hb))) {
                    keep = seen == 0 ? 1.0 : 1.0 - 1.0 / float(fog.flags.z + 1);
                    a = mix(a, ha, keep);
                    b = mix(b, hb, keep);
                }
            }
        }
    }
    ivec2 t = froxel(f.xy, f.z);
    // (the volumes are 16-bit floats: kept finite)
    imageStore(o_light, t, finite_or_zero(a));
    imageStore(o_light_b, t, finite_or_zero(b));
}
)";

// Step 0: the air's sun map (rt_volume_light.h air_map_half) - one texel each: a ray down the light's way from the
// map's plane, the distance to the first thing it hits (with CUTOUTS the cut-outs by their alpha), 3e38 where it hits
// nothing.
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

// Step 2: the integration (one froxel column each) - front to back along the column's ray (Hillaire 2015's
// energy-conserving form: each slice's light S x (1 - e^-tau) / sigma_t, its media's exact optical depth over the slice).
// Each slice stores what lies before its far face (the light, the transmittance), its own source (the light per unit of
// optical depth) and the god rays' potentials - the composite continues from the slice's near face to the pixel's depth.
constexpr const char* kIntegrateBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_light;
layout(set = 0, binding = 5, rgba16f) uniform readonly image2D u_light_b;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_integrated; // rgb the light, a the transmittance
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_source;     // rgb the slice's light per optical depth
// r, g the god rays' potential before the far face (unshadowed, under the froxels' own shadow), b, a the slice's own
// per unit of optical depth
layout(set = 0, binding = 16, rgba16f) uniform writeonly image2D o_potential;
// (the level's fog) the sky's pixels: rgb the light along the whole column (the sky's share), a the potential; r the
// potential under the froxels' own shadow
layout(set = 0, binding = 23, rgba16f) uniform writeonly image2D o_sky;
layout(set = 0, binding = 24, rgba16f) uniform writeonly image2D o_sky_lit;

void main()
{
    ivec2 c = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(c, fog.low.xy))) {
        return;
    }
    vec2 cell = column_size();
    vec3 direction = direction_through(vec2(fog.rect.xy) + (vec2(c) + 0.5) * cell - 0.5);
    bool lit = fog.sun_tint.w > 0.5;
    bool gather_peak = fog.rays.w > 0.5 && lit;
    float mu = dot(direction, fog.sun.xyz);
#ifdef AERIAL
    float rayleigh_phase = 0.0596831 * (1.0 + mu * mu); // (3 / (16 pi))
    float aerosol_phase = cs(mu, fog.sun.w) - 0.0795775 * god_peak(mu); // (the peak x 4 pi: / (4 pi))
#else
    // The level's fog (or the haze of a level without it) and the street air near the ground; each scatters the light
    // that reaches it - the sun's or the moon's, the lamps' - and its shade's own light, mixed by their share of each
    // slice's optical depth. Replaced (mode 0): the surfaces' fog whole; the shafts over the game's fog (mode 1) and the
    // haze a share; the sky's pixels a smaller one.
    // (round UNITS - one physical unit) Both media - the level's fog and the street air - are the weather's aerosol
    // (rt_volume_light.h level_fog_extinction): albedo fog.fog_colour.rgb, lit in the global unit x the frame's
    // pre-exposure by the light's irradiance (fog.sun_tint) x the phase / 4 pi, the lamps' estimate (E x phase / pi) x
    // pi / 4 pi and the open sky (fog.medium.rgb: half the sky's light on a level surface - an even upper hemisphere's
    // in-scattering) - linear radiance. Replaced (mode 0) all of it; the shafts over the game's fog (mode 1) and the haze
    // a share of the sunlit and lamp light; the sky's pixels a smaller one. fog.ambient.rgb: a level without physical
    // light (no relit data, no clock) - the game's fog colour as its own light, its look's unit.
    bool replace = fog.flags.x == 0;
    bool air_on = fog.air.x > 0.0 || fog.air.w > 0.0;
    float share = replace ? 1.0 : fog.shares.z;
    vec3 albedo = fog.fog_colour.rgb;
    vec3 ambient = replace ? fog.ambient.rgb : vec3(0.0);
    vec3 sky_in = replace ? albedo * fog.medium.rgb : vec3(0.0);
    vec3 medium = albedo * share;
    vec3 medium_sky = albedo * fog.shares.y;
    // (the god rays: their forward peak taken out - the screen draws it)
    vec3 sun = fog.sun_tint.rgb * ((phase(mu) - god_peak(mu)) * 0.0795775); // (/ 4 pi)
    vec3 sky_light = vec3(0.0);
    float sky_potential = 0.0;
    float sky_potential_lit = 0.0;
#endif
    vec3 light_in = vec3(0.0);
    float transmittance = 1.0;
    float potential = 0.0;
    float potential_lit = 0.0;
    int slices = fog.low.z;
    for (int k = 0; k < slices; ++k) {
        float s0 = slice_distance(float(k));
        float s1 = slice_distance(float(k + 1));
        vec3 at = fog.camera.xyz + direction * (0.5 * (s0 + s1));
        ivec2 t = froxel(c, k);
        vec4 a = imageLoad(u_light, t);
        vec4 b = vec4(0.0);
        // the local lights' light spatially filtered too - a 3 x 3 x 3 tent over the froxel and its neighbours (weights 1,
        // 1/2, 1/4, 1/8 by the axes it is off on): the temporal history averages a froxel's own samples, this its
        // neighbours' - the near field of a small bright light (its light falls with the square of the distance) caught by
        // one froxel's samples one frame and another's the next is shared out; the sun's and the sky's shares stay the
        // froxel's own (the shafts' edges sharp, a closed room's air given no sky from outside its walls)
        vec3 lamps = vec3(0.0);
        float weights = 0.0;
        for (int n = 0; n < 27; ++n) {
            ivec3 o = ivec3(n % 3, (n / 3) % 3, n / 9) - 1;
            ivec3 at_n = ivec3(c, k) + o;
            if (any(lessThan(at_n, ivec3(0))) || any(greaterThanEqual(at_n, fog.low.xyz))) {
                continue;
            }
            float w = exp2(-float(abs(o.x) + abs(o.y) + abs(o.z)));
            ivec2 tn = froxel(at_n.xy, at_n.z);
            lamps += w * imageLoad(u_light, tn).rgb;
            b += w * imageLoad(u_light_b, tn);
            weights += w;
        }
        a.rgb = lamps / weights;
        b = vec4(b.rgb / weights, imageLoad(u_light_b, t).a);
        float sun_seen = a.a;
        float clouds_kept = 1.0;
#ifdef CLOUDS
        if (gather_peak) {
            clouds_kept = cloud_map_kept(at, fog.cloud_map, fog.cloud_light);
        }
#endif
        vec3 here = vec3(0.0);
        float per_unit = 0.0;
        float tau = 0.0;
#ifdef AERIAL
        float d_r = layer_depth(direction, s0, s1, 1.0, fog.aerial.x, fog.aerial.y);
        float d_a = layer_depth(direction, s0, s1, 1.0, fog.aerial.z, fog.aerial.y);
        if (fog.aerial.w > 0.0) {
            d_a += fog.aerial.w * layer_depth(direction, s0, s1, 1.0, fog.aerosol_s.w, fog.aerial.y);
        }
        float d_p = fog.aerosol_t.w > 0.0 ? layer_depth(direction, s0, s1, fog.aerosol_t.w, fog.ambient.w, fog.aerial.y)
                                          : 0.0;
        vec3 r_s = fog.medium.rgb * d_r;
        vec3 a_s = fog.aerosol_s.rgb * d_a + vec3(d_p); // (the falling drops and flakes: grey, as much scattered)
        tau = dot(r_s + fog.aerosol_t.rgb * d_a + vec3(d_p), LUMA);
        if (tau > 1e-9) {
            // the light's (its irradiance x its visibility x the two phases), every higher order (the scattering x Psi
            // x the sky's open share), the lamps' (their irradiance pi x the estimate's unit, the phases x 4 pi: x 1 / 4)
            vec3 source = fog.ambient.rgb * ((r_s + a_s) * b.a);
            if (lit) {
                source += fog.sun_tint.rgb * (sun_seen * (r_s * rayleigh_phase + a_s * aerosol_phase));
            }
            source += 0.25 * (a_s * a.rgb + r_s * b.rgb);
            here = source / tau;
            per_unit = dot(a_s, LUMA) / tau;
        }
#else
        float tau_main = main_depth(direction, s0, s1);
        float tau_air = air_on ? max(air_depth(direction, s0, s1), 0.0) : 0.0;
        tau = tau_main + tau_air;
        if (tau > 1e-9) {
            vec3 lights = sun * sun_seen + 0.25 * a.rgb;
            here = medium * lights + sky_in * b.a + ambient * (tau_main / tau);
            vec3 here_sky = medium_sky * lights;
            // (the god rays' potential: the albedo the peak's phase would scatter here per unit of the light's
            // irradiance, unshadowed by the world - the screen's shadows stand for those)
            per_unit = dot(medium, LUMA);
            float per_unit_sky = dot(medium_sky, LUMA);
            float e_sky = exp(-min(tau, 40.0));
            float w_sky = transmittance * (1.0 - e_sky);
            sky_light += w_sky * here_sky;
            if (gather_peak) {
                sky_potential += w_sky * per_unit_sky * clouds_kept;
                sky_potential_lit += w_sky * per_unit_sky * clouds_kept * sun_seen;
            }
        }
#endif
        if (!gather_peak) {
            per_unit = 0.0;
        }
        float e = exp(-min(tau, 40.0));
        float w = transmittance * (1.0 - e);
        light_in += w * here;
        potential += w * per_unit * clouds_kept;
        potential_lit += w * per_unit * clouds_kept * sun_seen;
        transmittance *= e;
        if (transmittance < 1e-4) {
            transmittance = 0.0; // (nothing past here is seen)
        }
        if (!finite4(vec4(light_in, transmittance))) {
            light_in = vec3(0.0); // (a slice that came out not finite: the column goes on from no light, unseen past it)
            transmittance = 0.0;
        }
        imageStore(o_integrated, t, vec4(min(light_in, vec3(65504.0)), transmittance));
        imageStore(o_source, t, finite_or_zero(vec4(here, 0.0)));
        imageStore(o_potential, t, vec4(potential, potential_lit, per_unit * clouds_kept,
                                        per_unit * clouds_kept * sun_seen));
    }
#ifndef AERIAL
    imageStore(o_sky, c, vec4(sky_light, sky_potential));
    imageStore(o_sky_lit, c, vec4(sky_potential_lit, 0.0, 0.0, 0.0));
#endif
}
)";

// What the composite and the resolve read: the integrated volume at a pixel's depth.
constexpr const char* kLookupGlsl = R"(
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D u_integrated;
layout(set = 0, binding = 5, rgba16f) uniform readonly image2D u_source;
layout(set = 0, binding = 16, rgba16f) uniform readonly image2D u_potential;
layout(set = 0, binding = 23, rgba16f) uniform readonly image2D u_sky;
layout(set = 0, binding = 24, rgba16f) uniform readonly image2D u_sky_lit;

struct Along {
    vec3 light;
    float transmittance;
    float potential;
    float potential_lit;
};

// the volumes bilinearly across the columns at slice k (g: the grid's coordinates, a column's centre at its index)
void columns_at(vec2 g, int k, bool before, out vec4 integrated, out vec4 source, out vec4 potential)
{
    integrated = vec4(0.0);
    source = vec4(0.0);
    potential = vec4(0.0);
    g = clamp(g, vec2(0.0), vec2(fog.low.xy) - 1.0);
    ivec2 base = ivec2(floor(g));
    vec2 f = g - vec2(base);
    for (int corner = 0; corner < 4; ++corner) {
        ivec2 o = ivec2(corner & 1, corner >> 1);
        ivec2 n = min(base + o, fog.low.xy - 1);
        vec2 w2 = mix(vec2(1.0) - f, f, vec2(o));
        float w = w2.x * w2.y;
        ivec2 t = froxel(n, k);
        potential += w * imageLoad(u_potential, t);
        if (before) {
            integrated += w * imageLoad(u_integrated, t);
        } else {
            source += w * imageLoad(u_source, t);
        }
    }
}

// The fog along the view ray `direction` to `d` (clamped to the grid's far) at the grid's place g: what lies before the
// slice holding d (its near face's integral, read across the columns), then the slice's own source from its near face to
// d - its optical depth exact along this ray (Hillaire's form again): the transmittance follows the medium, not the grid.
Along along(vec2 g, vec3 direction, float d)
{
    d = clamp(d, 0.0, fog.grid.x);
    int k = clamp(int(floor(slice_of(d))), 0, fog.low.z - 1);
    float near = slice_distance(float(k));
    vec4 integrated = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 potential = vec4(0.0);
    vec4 unused;
    if (k > 0) {
        columns_at(g, k - 1, true, integrated, unused, potential);
    }
    vec4 source;
    vec4 own;
    columns_at(g, k, false, unused, source, own);
    float tau = d > near ? media_depth(direction, near, d) : 0.0;
    float w = clamp(integrated.a, 0.0, 1.0) * (1.0 - exp(-min(tau, 40.0)));
    Along out_value;
    out_value.light = max(integrated.rgb + w * source.rgb, vec3(0.0));
    out_value.transmittance = clamp(integrated.a * exp(-min(tau, 40.0)), 0.0, 1.0);
    out_value.potential = max(potential.r + w * own.b, 0.0);
    out_value.potential_lit = max(potential.g + w * own.a, 0.0);
    return out_value;
}

#ifdef AERIAL
// The sky's pixels are the world past the haze's reach (the sky dome's painted horizon goes on where the level's meshes
// end): followed as a surface at the far plane along the ray or the air's reach (sun_light.w), the nearer.
float far_along(ivec2 p)
{
    vec4 w = fog.to_world * vec4(vec2(p) + fog.offsets.xy, 1.0, 1.0);
    if (!(abs(w.w) > 1e-30)) {
        return fog.sun_light.w;
    }
    float far = length(w.xyz / w.w - fog.camera.xyz);
    return far > 0.0 && far < 3.0e38 ? far : fog.sun_light.w;
}
#endif

// The fog the pixel p (the target's) sees, at `distance` (-1: the sky): the light, the transmittance the colour is kept
// by (1 where the fog only adds - the shafts, the haze, the sky's pixels) and the god rays' potentials.
Along fog_of(ivec2 p, float distance)
{
    vec2 g = (vec2(p - fog.rect.xy) + 0.5) / column_size() - 0.5;
    bool sky = distance < 0.0;
    Along out_value;
#ifdef AERIAL
    if (sky && fog.air_look.x > 0.5) {
        out_value.light = vec3(0.0);
        out_value.transmittance = 1.0;
        out_value.potential = 0.0;
        out_value.potential_lit = 0.0;
        return out_value;
    }
    return along(g, direction_of(p), sky ? min(far_along(p), fog.sun_light.w) : distance);
#else
    if (sky) {
        g = clamp(g, vec2(0.0), vec2(fog.low.xy) - 1.0);
        ivec2 base = ivec2(floor(g));
        vec2 f = g - vec2(base);
        vec4 s = vec4(0.0);
        float lit = 0.0;
        for (int corner = 0; corner < 4; ++corner) {
            ivec2 o = ivec2(corner & 1, corner >> 1);
            ivec2 n = min(base + o, fog.low.xy - 1);
            vec2 w2 = mix(vec2(1.0) - f, f, vec2(o));
            s += w2.x * w2.y * imageLoad(u_sky, n);
            lit += w2.x * w2.y * imageLoad(u_sky_lit, n).r;
        }
        out_value.light = max(s.rgb, vec3(0.0));
        out_value.transmittance = 1.0;
        out_value.potential = max(s.a, 0.0);
        out_value.potential_lit = max(lit, 0.0);
        return out_value;
    }
    out_value = along(g, direction_of(p), distance);
    if (fog.flags.x != 0) {
        out_value.transmittance = 1.0;
    }
    return out_value;
#endif
}
)";

// (the god rays - rt_volume_light.h god_ray_peak) Step 2a: what each texel of the rays' images (half the viewport's
// pixels each way) puts in the light's way: r its 2 x 2 pixels' share of sky, g the rest's distance x their share, b
// their share (what stands there, and how far).
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

// Step 3: the composite (a blend: colour x T + the light; the aerial air's light linear, the level fog's
// display-referred). With LAYER: Ray Reconstruction's transparency layer - its premultiplied light L and coverage a - at
// the front layer's depth: L x T + a x the light (the fog before the see-through surface; what is behind it got its own).
constexpr const char* kCompositeBody = R"(
layout(set = 0, binding = 14) uniform sampler2D u_rays;
#ifdef LAYER
layout(set = 0, binding = 25, r32f) uniform readonly image2D u_front; // (the front layer's depth, 1: none)
#endif
layout(location = 0) out vec4 o_colour;

// (the god rays) The share of the air's light along a view ray that lies within `d` of the eye - an even medium's
// (rt_volume_light.h air_share_before): its extinction at the eye's height and the ray's whole transmittance.
float air_before(float d, float transmittance)
{
    float sigma = fog.flags.x == 2 ? fog.air_look.y : fog.air.x;
    float whole = 1.0 - clamp(transmittance, 0.0, 1.0);
    if (!(whole > 1e-6) || !(sigma > 0.0)) {
        return 0.0;
    }
    return clamp((1.0 - exp(-sigma * max(d, 0.0))) / whole, 0.0, 1.0);
}

// (the god rays) The peak's light at the pixel: the froxels' potential along its ray, x the peak toward the pixel, x
// what of the light's surroundings is open along its line to the light - no more than the froxels' own shadow allows.
vec3 god_rays_light(ivec2 local, Along f)
{
    float peak = god_peak(dot(direction_of(fog.rect.xy + local), fog.sun.xyz));
    if (!(peak > 0.0) || !(fog.sun_tint.w > 0.5)) {
        return vec3(0.0);
    }
    float open = 1.0;
    if (fog.rays.w < 1.5) {
        vec2 size = vec2(textureSize(u_rays, 0));
        vec4 r = textureLod(u_rays, (vec2(local) + 0.5) * 0.5 / size, 0.0);
        if (r.b > 1e-4) {
            float before = air_before(r.g / r.b, f.transmittance);
            open = 1.0 - (1.0 - clamp(r.r, 0.0, 1.0)) * before;
        }
    }
    return fog.sun_tint.rgb * (peak * min(f.potential * open, f.potential_lit) * fog.rays.z);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - fog.rect.xy;
    float distance = distance_of(p);
#ifdef LAYER
    float front = fog.offsets.z > 0.5 ? imageLoad(u_front, p).r : 1.0;
    if (front < 1.0 && front < depth_at(p)) {
        distance = length(world_at(p, front) - fog.camera.xyz);
    }
#endif
    Along f = fog_of(p, distance);
    vec3 light_in = f.light + god_rays_light(local, f);
    // (round FA - the debug views: without the fog its in-scattered light is none; what the air takes away stays)
    light_in *= fog.list_light.y;
    // (round UNITS: the level fog's light is linear radiance as the aerial air's - before, it was taken as
    // display-referred and decoded with the display's 2.2 power, which raised a lamp's pre-exposed light in the air,
    // tens to thousands at night, to its 2.2th power: the glowing balls round street lamps)
    // (not finite: the colour left as it is)
    o_colour = finite4(vec4(light_in, f.transmittance)) ? vec4(min(light_in, vec3(65504.0)), f.transmittance)
                                                       : vec4(0.0, 0.0, 0.0, 1.0);
}
)";

// (the checks) The resolve: one texel of each low-resolution image per froxel column - the fog its middle pixel sees,
// as the composite reads it (without the god rays' peak): rgb the light, a the transmittance; the distance, the history's
// frames and the pixel; the distance, the pixel and the potential; the potential under the froxels' own shadow.
constexpr const char* kResolveBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_resolved;
layout(set = 0, binding = 3, rgba32f) uniform writeonly image2D o_resolved_info;
layout(set = 0, binding = 26, rgba32f) uniform writeonly image2D o_resolved_raw_info;
layout(set = 0, binding = 15, r32f) uniform writeonly image2D o_resolved_lit;

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(q, fog.low.xy))) {
        return;
    }
    ivec2 local = min(ivec2((vec2(q) + 0.5) * column_size()), fog.rect.zw - ivec2(1));
    ivec2 p = fog.rect.xy + local;
    float distance = distance_of(p);
    Along f = fog_of(p, distance);
    imageStore(o_resolved, q, vec4(f.light, f.transmittance));
    imageStore(o_resolved_info, q, vec4(distance, float(fog.flags.z + 1), float(local.x), float(local.y)));
    imageStore(o_resolved_raw_info, q, vec4(distance, float(local.x), float(local.y), f.potential));
    imageStore(o_resolved_lit, q, vec4(f.potential_lit));
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
    std::array<uint32_t, 4> list{};      // (round WF) the light list's local group: x entries, y candidates a step pair
    std::array<uint32_t, 4> instances{}; // xy the TLAS instances' device address, z their count
    std::array<float, 4> list_light{};   // x the frame's pre-exposure; (round FA) y the in-scattered light kept (0: a debug view without it)
    std::array<float, 4> grid{}; // (round FOG) x the grid's far, y its slices' scale; zw the same of the frame before
    std::array<uint32_t, 4> picks{}; // (round FOG) the light grid's picks (FrameLightList::grid)
    std::array<float, rt::kSkyPhaseTable> sky_phase{}; // (round MEDIUM) open_sky_phase_table
};
constexpr std::size_t kFogBlockBytes = 784;
static_assert(sizeof(FogBlock) == kFogBlockBytes, "the shaders' std140 block");

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The bindings: 0 the scene, 1 the depth, 2 / 3 written, 4 / 5 read, 6 / 8 the froxels' history, 7 the block; the
// passes' own images at the rest (the shaders above name each).
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingOut = 2;
constexpr uint32_t kBindingOutInfo = 3;
constexpr uint32_t kBindingIn = 4;
constexpr uint32_t kBindingInInfo = 5;
constexpr uint32_t kBindingHistory = 6;
constexpr uint32_t kBindingBlock = 7;
constexpr uint32_t kBindingHistoryInfo = 8;
constexpr uint32_t kBindingSky = 9; // (the sky model's table)
constexpr uint32_t kBindingLights = 10;   // (round WF) the light list's local group: its alias table
constexpr uint32_t kBindingCloudMap = 11; // the clouds' shadow map (vulkan_rt_sky_pass.h)
constexpr uint32_t kBindingGeometry = 12; // the frame's geometry table (CUTOUTS: the cut-outs' test)
constexpr uint32_t kBindingAirMap = 13;   // (added by the distinct shafts) the air's sun map
constexpr uint32_t kBindingRaysIn = 14;   // (added by the god rays) the rays' image read (sampled, linear)
constexpr uint32_t kBindingRaysOut = 15;  // ... and written (the resolve: its potential under the froxels' shadow)
constexpr uint32_t kBindingPotential = 16; // (round FOG) the god rays' potentials, a volume
constexpr uint32_t kBindingEmitters = 17;   // (round WF) ... its emitting triangles (vulkan_light_list.h)
constexpr uint32_t kBindingLampRecords = 18; // ... its lamps' records
constexpr uint32_t kBindingMembership = 19;  // ... the instances' first triangles
constexpr uint32_t kBindingSlots = 20;       // ... per triangle its place in the list + 1
constexpr uint32_t kBindingLightPool = 21;   // (round PF) ... its presampled pool (kLightPoolSize picks)
constexpr uint32_t kBindingSkyLight = 23;    // (round FOG) the level fog's sky pixels' light
constexpr uint32_t kBindingSkyLit = 24;      // ... their potential under the froxels' shadow
constexpr uint32_t kBindingFront = 25;       // ... the front layer's depth (the transparency layer's composite)
constexpr uint32_t kBindingRawInfo = 26;     // ... the resolve's second info
constexpr uint32_t kBindingCount = 27;
constexpr uint32_t kMaterialSet = 1; // (CUTOUTS: the material table's set)

// The passes' variants (bits): a multisampled depth, the sky model's air, the cut-outs (the material table and the
// frame's geometry), the clouds' shadow map, the light list, (the checks') the candidates picked here; the composite's:
// the transparency layer's.
constexpr uint32_t kVariantMultisampled = 1;
constexpr uint32_t kVariantAerial = 2;
constexpr uint32_t kVariantCutouts = 4;
constexpr uint32_t kVariantClouds = 8;
constexpr uint32_t kVariantLights = 16; // (round WF) the light list's local group lights the air (with the cut-outs')
constexpr uint32_t kVariantPickedHere = 32; // (round OM - the checks') the candidates picked at each point, not the pool
constexpr uint32_t kVariantLayer = 64;      // (round FOG) the composite into Ray Reconstruction's transparency layer
constexpr uint32_t kVariants = 128;

struct CompositePipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t variant = 0; // (kVariantAerial: linear light; kVariantLayer)
    VkPipeline pipeline = VK_NULL_HANDLE;
};

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

constexpr std::size_t kSlotImages = 15;

// A viewport's images and history.
struct ViewSlot {
    std::array<uint32_t, 4> rect{};
    Extent target;
    uint32_t scale = kLeastScale; // (a froxel column's side in the viewport's pixels)
    uint32_t slices = 0;
    Extent grid;     // the columns across and down
    Extent volume;   // a volume's image: the slices side by side, kSliceColumns a row
    std::array<Image, 2> light;   // step 1: the froxels' light (the lamps', the sun's share), this frame's and the one before
    std::array<Image, 2> light_b; // ... (the lamps' with Rayleigh's phase, the sky's open share)
    Image integrated;  // step 2: the light and the transmittance before each slice's far face
    Image source;      // ... each slice's light per optical depth
    Image potential;   // ... the god rays' potentials
    Image sky;         // ... (the level's fog) the sky's pixels' light and potential (grid columns)
    Image sky_lit;     // ... their potential under the froxels' shadow
    Image resolved;          // (the checks) the resolve's: the light, the transmittance
    Image resolved_info;     // ... the distance, the history's frames, the pixel
    Image resolved_raw_info; // ... the distance, the pixel, the potential
    Image resolved_lit;      // ... the potential under the froxels' shadow
    std::array<Image, 2> rays; // (added by the god rays) the rays' images (half the viewport's pixels), in turn
    Image* rays_written = nullptr; // the one the composite read last (read_rt_fog_image's kRays)
    uint32_t current = 0;
    bool fresh = true;
    bool have_before = false;
    uint64_t frame_drawn = 0;
    uint64_t passes = 0;  // the frame index of the jitter
    uint32_t frames = 0;  // the frames the froxels' history holds (0: none)
    Matrix world_to_pixel{};
    std::array<float, 3> camera{};
    std::array<float, 2> grid_before{}; // the grid's far and slices' scale its history was written with
    float pre_exposure = 1.0F; // (round PX) the frame's pre-exposure its history was written in
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
    // (round FOG) the froxels' light, the integration and the resolve, by variant (made on first use; failed ones not
    // tried again)
    std::array<VkPipeline, kVariants> light_pass{};
    std::array<bool, kVariants> light_failed{};
    std::array<VkPipeline, kVariants> integrate{};
    std::array<bool, kVariants> integrate_failed{};
    std::array<VkPipeline, kVariants> resolve{};
    std::array<bool, kVariants> resolve_failed{};
    // (added by the distinct shafts) the air's sun map: its pass (without / with the cut-outs; made on first use) and its
    // image (rt_fog_quality's side; every viewport's in turn - each makes it before its march)
    std::array<VkPipeline, 2> air_map_pass{};
    std::array<bool, 2> air_map_failed{};
    Image air_map;
    bool air_map_made = false;
    bool made = false; // (the layouts, the samplers and the vertex shader)
    std::vector<CompositePipeline> composites;
    std::vector<std::unique_ptr<ViewSlot>> slots;
    std::array<float, 3> to_sun{};
    std::array<float, 3> sun_tint{};
    bool have_sun = false;
    // (round UNITS) the lighting's light in the global unit (vulkan_light_list.h scene_lighting): the light per unit of
    // N.L and the sky's on a level surface; physical: the level has them (relit data or the clock)
    std::array<float, 3> lit_sun{};
    std::array<float, 3> lit_sky{};
    bool physical = false;
    rt::MediumLight light; // (added by the volumetric light) the light the air is lit by this view
    // (added by the cinematic air) the street air last logged: its ground's height and look (logged again when either
    // changes - a new level, another look)
    float logged_street = -1.0e30F;
    rt::Weather logged_weather = rt::Weather::kClear;
    bool logged_air = false;
    // (round WF) the light list's local group this view (vulkan_light_list.h restir_local_lights - ReSTIR DI's lights,
    // kept up whether or not DI runs), and whether any light of it lights the air
    RestirLocalLights local;
    bool lights = false;
    std::array<uint32_t, 4> picks{}; // (round FOG) the light grid's picks this frame (FrameLightList::grid; z 0: none)
    bool lights_before = false; // (last view's: whether a haze without a sun has anything to light it)
    uint64_t lights_frame = 0;
    float lights_scale = 0.0F; // (the pre-exposure their light was scattered with)
    bool cutouts = false;  // this view's march takes the cut-outs and water (the material table, the frame's geometry)
    CloudShadowMap cloud{}; // the clouds' shadow map (made this frame), when have_cloud
    bool have_cloud = false;
    SceneView scene_view;
    bool haze = false; // this view's level has no fog: the haze drawn instead (rt_fog_haze)
    bool clock_air = false; // (round FW) ... or its fog is the time of day's, under the clock's physical air
    bool aerial = false; // ... as the sky model's aerial perspective (rt_sky_model=1, a sun and its table)
    // (round MEDIUM) the air this view's pass marched, as layers (rt_volume_light.h AirMedium) - the path tracer's too
    rt::AirMedium medium;
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

// (round OM - the checks') the candidates picked at the point (as before the presampled pool) instead of from it
bool& pool_off()
{
    static bool off = false;
    return off;
}

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

// (round WF) The light list's local group as the march reads it (vulkan_light_list.h - ReSTIR DI's buffers).
constexpr const char* kListGlsl = R"(
#define LIGHT_LIST
struct LightEntry {
    float threshold;
    uint alias;
    float pdf;
    uint payload;
};
layout(std430, set = 0, binding = 10) readonly buffer Lights { LightEntry light_entries[]; };
layout(std430, set = 0, binding = 17) readonly buffer Emitters {
    uint emitter_count;
    uint emitter_spare;
    uint emitter_unused;
    uint emitter_pad;
    uvec4 emitter_entries[]; // instance, primitive, power (float bits), -
};
layout(std430, set = 0, binding = 18) readonly buffer Lamps { LightRecord lamps[]; };
layout(std430, set = 0, binding = 19) readonly buffer Membership { uint membership[]; };
layout(std430, set = 0, binding = 20) readonly buffer Slots { uint slots[]; };
const uvec2 sky_alias[1] = uvec2[1](uvec2(0u)); // (the list's sky isn't sampled here: the fog's ambient owns it)
#define LIST_INSTANCE_ADDRESS fog.instances.xy
#define LIST_INSTANCES fog.instances.z
)";

std::string source_of(const char* body, bool traces, bool multisampled, uint32_t variant = 0)
{
    const bool aerial = (variant & kVariantAerial) != 0;
    const bool cutouts = traces && (variant & kVariantCutouts) != 0;
    const bool lights = cutouts && (variant & kVariantLights) != 0;
    std::string source = "#version 460\n";
    if (lights && (variant & kVariantPickedHere) != 0) {
        source += "#define LIGHTS_PICKED_HERE\n";
    }
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    source += "#define SLICE_COLUMNS " + std::to_string(kSliceColumns) + "\n"; // (round FOG: the volumes' layout)
    // (the god rays' peak's edge - the march, the rays' passes and the composite)
    source += "#define GOD_RAYS_CONE_COS " +
              std::to_string(std::cos(rt::kGodRayConeDegrees * std::numbers::pi / 180.0)) + "\n";
    source += "#define RAYS_TAPS " + std::to_string(rt::kGodRayTaps) + "\n";
    if (aerial) {
        source += "#define AERIAL\n";
    }
    if ((variant & kVariantClouds) != 0) {
        source += "#define CLOUDS\n"; // (the froxels' light and the integration's god rays)
    }
    if (traces) {
        source += "#extension GL_EXT_ray_query : require\n";
        source += "#define SUN_DISTANCE " + std::to_string(kFogSunDistance) + "\n";
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
        if (lights) {
            // (round WF) the light list's local group: what a point of it is and sends (vulkan_light_list.h)
            source += rt::kDisplayTransferGlsl;
            source += rt::brdf::brdf_glsl();
            source += hit_surface_glsl();
            source += rt::path::kPathGlsl;
            source += rt_lamp_glsl();
        }
    }
    source += rt::kMediumGlsl;
    source += kCommon;
    if (lights) {
        source += kListGlsl;
        source += light_grid_reader_glsl("fog.picks"); // (round FOG: ReGIR's cells - list_lights)
        source += "const uint SKY_WIDTH = " + std::to_string(rt::lights::kSkyGridWidth) + "u;\n";
        source += "const uint SKY_HEIGHT = " + std::to_string(rt::lights::kSkyGridHeight) + "u;\n";
        source += light_instances_glsl();
        source += rt::lights::kLightListGlsl;
        // (round PF) the candidates from the presampled pool (vulkan_light_list.h kLightPoolSize)
        source += "#define LIGHT_POOL_SIZE " + std::to_string(kLightPoolSize) + "u\n";
        source += "layout(std430, set = 0, binding = " + std::to_string(kBindingLightPool) +
                  ") readonly buffer LightPool { uvec4 light_pool[]; };\n";
        source += local_light_glsl();
    }
    if ((variant & kVariantClouds) != 0) {
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
                                           .flags = pipeline_statistics_flags(), // (round PERF3)
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
    log_pipeline_statistics(device, pipeline, source); // (round PERF3: with frame_profile)
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
        } else if (binding == kBindingBlock) {
            type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (binding == kBindingGeometry || binding == kBindingLights || binding == kBindingEmitters ||
                   binding == kBindingLampRecords || binding == kBindingMembership || binding == kBindingSlots ||
                   binding == kBindingLightPool) {
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

// The layouts, the samplers and the vertex shader (once).
bool ensure_objects(Backend& state, std::string* error)
{
    FogState& own = fog_state();
    if (own.made) {
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
    own.made = true; // (the passes' variants are made on first use)
    return true;
}

// A pass's variant (made on first use; VK_NULL_HANDLE: it can't be - logged once, that variant not tried again).
VkPipeline variant_pipeline(Backend& state, std::array<VkPipeline, kVariants>& made,
                            std::array<bool, kVariants>& failed, const std::string& body, bool traces, uint32_t variant,
                            std::string* error)
{
    FogState& own = fog_state();
    if (core::at(made, variant) != VK_NULL_HANDLE || core::at(failed, variant)) {
        return core::at(made, variant);
    }
    const VkPipelineLayout layout = (variant & kVariantCutouts) != 0 ? own.layout_materials : own.layout;
    core::at(made, variant) = make_compute(
        state.context.device(), layout,
        source_of(body.c_str(), traces, (variant & kVariantMultisampled) != 0, variant), error);
    core::at(failed, variant) = core::at(made, variant) == VK_NULL_HANDLE;
    return core::at(made, variant);
}

// (round FOG) The froxels' light (it reads no depth), the integration (the air's kind and the clouds' map) and the
// resolve (the depth's samples, the air's kind).
VkPipeline light_pipeline(Backend& state, uint32_t variant, std::string* error)
{
    FogState& own = fog_state();
    const uint32_t kept = variant & ~(kVariantMultisampled | kVariantLayer);
    return variant_pipeline(state, own.light_pass, own.light_failed, kLightBody, true, kept, error);
}
VkPipeline integrate_pipeline(Backend& state, uint32_t variant, std::string* error)
{
    FogState& own = fog_state();
    return variant_pipeline(state, own.integrate, own.integrate_failed, kIntegrateBody, false,
                            variant & (kVariantAerial | kVariantClouds), error);
}
VkPipeline resolve_pipeline(Backend& state, uint32_t variant, std::string* error)
{
    FogState& own = fog_state();
    return variant_pipeline(state, own.resolve, own.resolve_failed, std::string(kLookupGlsl) + kResolveBody, false,
                            variant & (kVariantAerial | kVariantMultisampled), error);
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
// (round FOG: `variant` kVariantAerial - the aerial air, its light linear -, kVariantLayer - into the transparency layer)
VkPipeline composite_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error,
                              uint32_t variant)
{
    FogState& own = fog_state();
    variant &= kVariantAerial | kVariantLayer;
    for (const CompositePipeline& made : own.composites) {
        if (made.format == format && made.samples == samples && made.variant == variant) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    const bool layer = (variant & kVariantLayer) != 0;
    const std::string body = std::string((variant & kVariantAerial) != 0 ? "#define LINEAR_COMPOSITE\n" : "") +
                             (layer ? "#define LAYER\n" : "") + rt::kDisplayTransferGlsl + kLookupGlsl + kCompositeBody;
    const VkShaderModule fragment = make_module(
        device, RtShaderStage::kFragment,
        source_of(body.c_str(), false, samples != VK_SAMPLE_COUNT_1_BIT, variant & kVariantAerial), error);
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
    // colour x T + the light: (src x 1) + (dst x src alpha); alpha kept. (Round FOG) the transparency layer: its light
    // x T + its coverage x the light - (src x dst alpha) + (dst x src alpha)
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = layer ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_ONE;
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
    own.composites.push_back({.format = format, .samples = samples, .variant = variant, .pipeline = pipeline});
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
    return {&slot.light.front(),     &slot.light.back(),     &slot.light_b.front(),   &slot.light_b.back(),
            &slot.integrated,        &slot.source,           &slot.potential,         &slot.sky,
            &slot.sky_lit,           &slot.resolved,         &slot.resolved_info,     &slot.resolved_raw_info,
            &slot.resolved_lit,      &slot.rays.front(),     &slot.rays.back()};
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

ViewSlot* slot_for(Backend& state, const std::array<uint32_t, 4>& rect, const Extent& target, uint32_t scale,
                   uint32_t slices)
{
    FogState& own = fog_state();
    ++own.uses;
    for (const std::unique_ptr<ViewSlot>& slot : own.slots) {
        if (slot->rect == rect && slot->target.width == target.width && slot->target.height == target.height &&
            slot->scale == scale && slot->slices == slices) {
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
    slot.slices = slices;
    slot.last_use = own.uses;
    // (round FOG) the grid: a column per scale x scale pixels, `slices` deep; each volume an image of its slices side by
    // side; the resolve's images a texel per column
    const Extent extent = low_extent(rect, scale);
    slot.grid = extent;
    slot.volume = {.width = extent.width * kSliceColumns,
                   .height = extent.height * ((slices + kSliceColumns - 1) / kSliceColumns)};
    constexpr VkFormat kHalf4 = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat kFloat4 = VK_FORMAT_R32G32B32A32_SFLOAT;
    bool made = make_storage_image(state, kHalf4, slot.volume, &slot.integrated) &&
                make_storage_image(state, kHalf4, slot.volume, &slot.source) &&
                make_storage_image(state, kHalf4, slot.volume, &slot.potential) &&
                make_storage_image(state, kHalf4, extent, &slot.sky) &&
                make_storage_image(state, kHalf4, extent, &slot.sky_lit) &&
                make_storage_image(state, kHalf4, extent, &slot.resolved) &&
                make_storage_image(state, kFloat4, extent, &slot.resolved_info) &&
                make_storage_image(state, kFloat4, extent, &slot.resolved_raw_info) &&
                make_storage_image(state, VK_FORMAT_R32_SFLOAT, extent, &slot.resolved_lit);
    for (std::size_t i = 0; i < 2; ++i) {
        made = made && make_storage_image(state, kHalf4, slot.volume, &core::at(slot.light, i)) &&
               make_storage_image(state, kHalf4, slot.volume, &core::at(slot.light_b, i)) &&
               make_storage_image(state, kHalf4, rays_extent(rect), &core::at(slot.rays, i));
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
    bool lights = false; // (round WF: the light list's local group - FogState::local)
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
    const Image* potential = nullptr; // (round FOG: the god rays' potentials' volume)
    const Image* sky_light = nullptr; // (round FOG: the level fog's sky pixels)
    const Image* sky_lit = nullptr;
    const Image* front = nullptr;    // (round FOG: the front layer's depth)
    const Image* raw_info = nullptr; // (round FOG: the resolve's second info)
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
    const auto whole = [](VkBuffer buffer) { return VkDescriptorBufferInfo{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE}; };
    const std::array<VkDescriptorBufferInfo, 6> lists{whole(own.local.table),      whole(own.local.emitters),
                                                       whole(own.local.lamps),      whole(own.local.membership),
                                                       whole(own.local.slots),      whole(own.local.light_pool)};
    if (bound.lights) {
        const std::array<uint32_t, 6> places{kBindingLights,     kBindingEmitters, kBindingLampRecords,
                                             kBindingMembership, kBindingSlots,    kBindingLightPool};
        for (std::size_t i = 0; i < places.size(); ++i) {
            add(core::at(places, i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = &core::at(lists, i);
        }
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
    const std::array<std::pair<uint32_t, const Image*>, 14> storage{{{kBindingPotential, bound.potential},
                                                                    {kBindingSkyLight, bound.sky_light},
                                                                    {kBindingSkyLit, bound.sky_lit},
                                                                    {kBindingFront, bound.front},
                                                                    {kBindingRawInfo, bound.raw_info},
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

// (`layers`: round FOG - the froxels' light, a group per slice)
void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const PassBindings& bound, const Extent& extent,
              uint32_t layers = 1)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdDispatch(commands, (extent.width + kGroupSide - 1) / kGroupSide, (extent.height + kGroupSide - 1) / kGroupSide,
                  layers);
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

// (round FOG) Ray Reconstruction's transparency layer (vulkan_surface_light.h surface_light_layer): its premultiplied
// light L and coverage a lie over the colour, which has its fog to its own depth - so the fog before the see-through
// surface goes on the layer: L x T + a x the light, both read at the front layer's depth (the nearest see-through
// surface; the colour's where it has none). Nothing without a layer this frame.
void record_layer_composite(Backend& state, VkCommandBuffer commands, const Image* colour, const Image* fallback,
                            const PassBindings& looked, const FogBlock& block, uint32_t variant,
                            const std::array<uint32_t, 4>& rect)
{
    Image* const layer = surface_light_layer(state);
    const uint32_t right = core::at(rect, 0) + core::at(rect, 2);
    const uint32_t bottom = core::at(rect, 1) + core::at(rect, 3);
    if (layer == nullptr || layer->attachment_view == VK_NULL_HANDLE || layer->samples != colour->samples ||
        layer->extent.width < right || layer->extent.height < bottom) {
        return;
    }
    FrontLayerImages front{};
    Image* front_depth = surface_light_front_images(&front) ? core::at(front.depth, 0) : nullptr;
    if (front_depth == nullptr || front_depth->view == VK_NULL_HANDLE || front_depth->layout != VK_IMAGE_LAYOUT_GENERAL ||
        front_depth->extent.width < right || front_depth->extent.height < bottom) {
        front_depth = nullptr;
    }
    // (offsets.z 1: the front layer's depth is read - the layer variant binds an image there either way)
    FogBlock layered = block;
    core::at(layered.offsets, 2) = front_depth != nullptr ? 1.0F : 0.0F;
    const void* const block_bytes = &layered;
    const std::optional<RingSpan> span = state.ring.copy(
        std::span(static_cast<const uint8_t*>(block_bytes), sizeof(layered)), state.uniform_alignment);
    std::string error;
    const VkPipeline pipeline =
        span ? composite_pipeline(state, layer->format, layer->samples, &error, variant | kVariantLayer) : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    PassBindings bound = looked;
    bound.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(layered)};
    bound.front = front_depth != nullptr ? front_depth : fallback; // (an R32F image, unread when offsets.z is 0)
    record_composite(commands, pipeline, layer, bound, rect);
    if (layer->layout != layer->resting) {
        Memory::transition(commands, layer, layer->resting);
    }
    layer->used = state.frames.serial();
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
        // (round CPU: the sun's colour alone - sky_colours also marched the sky overhead, twice a frame, unread here)
        own.sun_colour = sky_sun_colour(own.to_sun);
        const rt::DayLight day = rt::day_light();
        const std::array<float, 3> native = own.light.clock ? day.native : own.to_sun;
        const std::array<float, 3> colour = sky_sun_colour(native);
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

// (round WF - path tracing, stage 7; replaced the lamps pass's lit set) The lights that light the air this view: the light
// list's local group (vulkan_light_list.h restir_local_lights - every lamp and emitting surface of the level by power,
// the lamps as the clock switches them: the lights ReSTIR DI and GI sample; kept up here whether or not they run, so the
// fallback tier's air has the same lights). None without the material table (the triangles' emission is read through it)
// or with rt_fog_quality's candidates 0. `commands` outside a render pass (a new emitters' scan is recorded there).
void read_lights(Backend& state, VkCommandBuffer commands)
{
    FogState& own = fog_state();
    own.lights = false;
    own.local = {};
    const SceneLighting lighting = scene_lighting(false, true);
    own.lit_sun = lighting.sun_light;
    own.lit_sky = lighting.sky;
    // (round FW) physical light is the relit world's: current relit data. The clock alone isn't - over a level with no
    // current relit data (an older bake) its surfaces are drawn with the original's light and its lamps are the found
    // emitters at their display brightness, not light in the global unit: x the frame's pre-exposure (5 000 - 30 000 at
    // night) they lit the air into coloured glows. Such a level's air keeps the game's fog colour as its own light - the
    // unit its surfaces are drawn in - and no lamp lights it: a light with no physical value has no place in a physical
    // medium.
    own.physical = rt::cel_world().relit;
    if (!own.physical || !own.cutouts || own.settings.lamps == 0 || own.material_layout != state.materials.layout()) {
        own.lights_before = false;
        return;
    }
    own.lights = restir_local_lights(state, commands, lighting, &own.local) && own.local.entries > 0 &&
                 own.local.table != VK_NULL_HANDLE && own.local.emitters != VK_NULL_HANDLE &&
                 own.local.lamps != VK_NULL_HANDLE && own.local.membership != VK_NULL_HANDLE &&
                 own.local.slots != VK_NULL_HANDLE && own.local.light_pool != VK_NULL_HANDLE;
    own.lights_before = own.lights;
    own.lights_frame = own.lights ? state.frame : 0;
    // (round FOG) ReSTIR DI's light grid as GI read it this frame: the froxels near a light take it from there
    FrameLightList list;
    own.picks = own.lights && restir_gi_frame_lights(state, &list) ? list.grid : std::array<uint32_t, 4>{};
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

// (round FOG) The froxel grid's depth: from the eye to the fog's end (the level's fog, the haze) or the aerial air's
// reach - past it the level's fog has taken everything (its transmittance 0 at its end), the air is the sky's - and the
// scale of its slices (exponential past it: kSliceSpread).
std::array<float, 2> grid_reach(const rt::ViewDrawn& view, bool aerial)
{
    const float far = aerial ? kFogAerialFar : std::max(view.fog.end, 1.0F);
    return {far, far / kSliceSpread};
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

// (round MEDIUM) The air the pass marches as layers (rt_volume_light.h AirMedium - the path tracer traces the same):
// from the same inputs block_for hands the shaders - the atmosphere's air (the molecules, the weather's aerosol, its
// mist, what falls) at the level's ground, or the street air (the aerosol as one layer and what falls) over its street.
// The level's own fog (from the eye's distances) is no world medium: left out, said in `none`.
rt::AirMedium medium_of(const FogState& own, float ground, float reach)
{
    rt::AirMedium out;
    const rt::Aerosol air = rt::aerosol();
    out.g = static_cast<float>(air.g);
    out.reach = reach;
    const auto add = [&out](const std::array<float, 3>& scattering, const std::array<float, 3>& extinction,
                            double height, float layer_ground, rt::MediumPhase phase) {
        if (out.layers >= rt::kMediumLayers || !(core::at(extinction, 1) > 0.0F)) {
            return;
        }
        core::at(out.layer, out.layers) = {.scattering = scattering,
                                          .extinction = extinction,
                                          .inverse_height = height > 0.0 ? static_cast<float>(1.0 / height) : 0.0F,
                                          .ground = layer_ground,
                                          .phase = phase};
        ++out.layers;
    };
    const auto grey = [](double value) {
        const auto f = static_cast<float>(value);
        return std::array<float, 3>{f, f, f};
    };
    constexpr double kUnits = rt::kUnitsPerMetre;
    if (own.aerial) {
        out.sky_holds_ray = rt::sky_replacement().on;
        std::array<float, 3> rayleigh{};
        std::array<float, 3> s{};
        std::array<float, 3> t{};
        std::array<float, 3> mist_s{};
        std::array<float, 3> mist_t{};
        for (std::size_t i = 0; i < 3; ++i) {
            core::at(rayleigh, i) = static_cast<float>(core::at(rt::kRayleigh, i) / kUnits);
            core::at(s, i) = static_cast<float>(core::at(air.scattering, i) / kUnits);
            core::at(t, i) = static_cast<float>(core::at(air.extinction, i) / kUnits);
            core::at(mist_s, i) = static_cast<float>(core::at(air.scattering, i) * air.mist / kUnits);
            core::at(mist_t, i) = static_cast<float>(core::at(air.extinction, i) * air.mist / kUnits);
        }
        add(rayleigh, rayleigh, rt::kRayleighHeight * kUnits, ground, rt::MediumPhase::kRayleigh);
        add(s, t, air.height * kUnits, ground, rt::MediumPhase::kAerosol);
        if (air.mist > 0.0) {
            add(mist_s, mist_t, air.mist_height > 0.0 ? air.mist_height * kUnits : 0.0, ground,
                rt::MediumPhase::kAerosol);
        }
        const double falling = rt::precipitation_extinction(rt::air_weather()) / kUnits;
        add(grey(falling), grey(falling), rt::kPrecipitationHeight * kUnits, ground, rt::MediumPhase::kAerosol);
        return out;
    }
    if (!own.haze) {
        out.none = "the level's own fog (drawn from the eye's distances - no world medium) left out, its street air kept";
    }
    const rt::BakedSun& baked = rt::baked_sun();
    const float street = baked.scene != 0 && baked.street_known ? baked.street : ground;
    const rt::StreetAir street_air = rt::street_air(air, rt::air_weather());
    const std::array<float, 3> albedo = rt::aerosol_albedo(air);
    const auto scattered = [&albedo](double extinction) {
        const auto e = static_cast<float>(extinction);
        return std::array<float, 3>{core::at(albedo, 0) * e, core::at(albedo, 1) * e, core::at(albedo, 2) * e};
    };
    add(scattered(street_air.extinction), grey(street_air.extinction), street_air.height, street,
        rt::MediumPhase::kAerosol);
    add(scattered(street_air.snow), grey(street_air.snow), street_air.snow_height, street, rt::MediumPhase::kAerosol);
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
    // (round UNITS - one physical unit for every level's fog: rt_volume_light.h level_fog_extinction) the level's fog
    // and the street air are the weather's aerosol: its albedo, lit by the lighting's light in the global unit x the
    // frame's pre-exposure - the light's irradiance (pi x its light per unit of N.L), the open sky's in-scattering (half
    // its light on a level surface), every lamp. A level without physical light (no current relit data - (round FW) the clock alone
    // isn't physical light) keeps the game's fog colour as its own light (decoded as the frame's inputs are - its look's
    // unit), and no lamp lights its air.
    const float pre = rt::frame_pre_exposure();
    const std::array<float, 3> albedo = rt::aerosol_albedo(air);
    const bool lit_sun = own.physical && own.have_sun;
    block.sun_tint = {lit_sun ? std::numbers::pi_v<float> * core::at(own.lit_sun, 0) * pre : 0.0F,
                      lit_sun ? std::numbers::pi_v<float> * core::at(own.lit_sun, 1) * pre : 0.0F,
                      lit_sun ? std::numbers::pi_v<float> * core::at(own.lit_sun, 2) * pre : 0.0F,
                      own.have_sun ? 1.0F : 0.0F};
    block.fog_colour = {core::at(albedo, 0), core::at(albedo, 1), core::at(albedo, 2), own.settings.density};
    const double jitter = std::fmod(static_cast<double>(slot.passes % (1U << 20U)) * kGoldenRatio, 1.0);
    block.fog_range = {view.fog.start, view.fog.end, own.settings.height > 0.0F ? 1.0F / own.settings.height : 0.0F,
                       static_cast<float>(jitter)};
    const std::array<float, 2> sample = sample_zero_offset(samples, own.standard_samples);
    block.offsets = {sample.front(), sample.back(), 0.0F, 0.0F};
    // (no light - indoors: the replaced fog is the game's colour whole, the lamps' light added in it)
    // (w, round PX: the history's light into this frame's pre-exposure - the camera's moves it every frame, UE5's
    // PreExposure correction of a history)
    block.shares = {1.0F, kFogSkyShare, kFogShaftShare,
                    slot.pre_exposure > 0.0F ? rt::frame_pre_exposure() / slot.pre_exposure : 1.0F};
    block.rect = {static_cast<int32_t>(core::at(view.rect, 0)), static_cast<int32_t>(core::at(view.rect, 1)),
                  static_cast<int32_t>(core::at(view.rect, 2)), static_cast<int32_t>(core::at(view.rect, 3))};
    // (round FOG) the grid, the samples a froxel takes, the frames its history holds (none without one)
    block.low = {static_cast<int32_t>(slot.grid.width), static_cast<int32_t>(slot.grid.height),
                 static_cast<int32_t>(slot.slices), static_cast<int32_t>(own.settings.samples)};
    const uint32_t held = have_before ? std::min(slot.frames, kFogMaxHistory - 1) : 0U;
    block.flags = {own.settings.mode == RtFogMode::kShafts || own.haze ? 1 : 0,
                   static_cast<int32_t>(own.cutouts ? kFogCutoutMask : kFogMask), static_cast<int32_t>(held),
                   static_cast<int32_t>(slot.passes % (1U << 24U))};
    const std::array<float, 2> reach = grid_reach(view, own.aerial);
    block.grid = {reach.front(), reach.back(), core::at(slot.grid_before, 0), core::at(slot.grid_before, 1)};
    // (round UNITS) the open sky's in-scattering per unit of albedo (half its light on a level surface, pre-exposed) and
    // the fog's extinction per world unit (Koschmieder's, from the game's distances)
    constexpr float kHalf = 0.5F;
    block.medium = {own.physical ? kHalf * core::at(own.lit_sky, 0) * pre : 0.0F,
                    own.physical ? kHalf * core::at(own.lit_sky, 1) * pre : 0.0F,
                    own.physical ? kHalf * core::at(own.lit_sky, 2) * pre : 0.0F,
                    // (a level without fog - the haze - has the weather's street air alone: the clear night air is no
                    // 100-metre haze; kFogHazeEnd is only the grid's reach then)
                    own.haze ? 0.0F : static_cast<float>(rt::level_fog_extinction(view.fog.start, view.fog.end))};
    if (!own.physical) {
        block.ambient = {rt::linear_from_display(core::at(view.fog.colour, 0)),
                         rt::linear_from_display(core::at(view.fog.colour, 1)),
                         rt::linear_from_display(core::at(view.fog.colour, 2)), 0.0F};
    }
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
    }
    // (added by the street air; changed by the physical air) the level's own fog's air near the ground
    // (rt_volume_light.h street_air): the weather's aerosol as one layer and the falling rain or snow; (round UNITS) lit
    // as the level's fog is - the same aerosol
    // (round UNITS: the weather's air is there with or without a sun - at night it is what the lamps light)
    if (!own.aerial) {
        const rt::StreetAir street_air = rt::street_air(air, rt::air_weather());
        // (over the level's street level - the middle of its upward faces -, not its lowest floor)
        const float street = baked.scene != 0 && baked.street_known ? baked.street : ground;
        block.air = {static_cast<float>(street_air.extinction),
                     street_air.height > 0.0 ? static_cast<float>(1.0 / street_air.height) : 0.0F, street,
                     static_cast<float>(street_air.snow)};
        block.air_light = {0.0F, 0.0F, 0.0F,
                           street_air.snow_height > 0.0 ? static_cast<float>(1.0 / street_air.snow_height) : 0.0F};
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
    fog_state().medium = medium_of(own, ground, reach.front());
    // (round WF) the light list's local group
    if (own.lights) {
        block.list = {own.local.entries, own.settings.lamps, 0U, 0U};
        block.instances = {static_cast<uint32_t>(own.scene_view.instances_address & 0xFFFFFFFFULL),
                           static_cast<uint32_t>(own.scene_view.instances_address >> 32U), own.scene_view.instances, 0U};
        block.list_light = {rt::frame_pre_exposure(), 0.0F, 0.0F, 0.0F};
        block.picks = own.picks;
        fog_state().lights_scale = rt::frame_pre_exposure();
    }
    // (round FA - vulkan_debug_view.h) the in-scattered light kept, or none in a debug view without the fog
    core::at(block.list_light, 1) = (debug_terms() & debug_term::kFog) != 0U ? 1.0F : 0.0F;
    block.rays = own.rays; // (added by the god rays: set_rays)
    // (round MEDIUM) the sky's light indoors weighed by the phase toward the eye (open_sky_phase_mean) - tabled once a g
    {
        static double tabled_g = -2.0;
        static std::array<float, rt::kSkyPhaseTable> table{};
        if (air.g != tabled_g) {
            table = rt::open_sky_phase_table(air.g);
            tabled_g = air.g;
        }
        block.sky_phase = table;
    }
    // (the peak x 4 pi, per sr - round UNITS: the level's fog's too, its light being the irradiance now)
    core::at(block.rays, 2) = static_cast<float>(1.0 / (4.0 * std::numbers::pi));
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
    if (own.clock_air) {
        return "the time of day's fog left out: the weather's air (the clock's physical light)";
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
    // (round FW) the time of day lighting a level by physical light: the fog it hands the draws is the original's time
    // of day states' (tod_manager.h - the level's own TODManager_SetParams, or tod_manager.qb's defaults: night 8 000 -
    // 32 000 units), a linear blend into a dark colour that stood for the night's darkness and the day's haze. The clock
    // has the physical sky and air for those now - the dark the camera's exposure and the light make, the haze the
    // weather's aerosol (rt_fog_air) - so that fog is no medium: the air is the weather's alone, as in a level without
    // fog, and the game's fog stays out of the draws. (As a medium, Koschmieder's from those distances, every night was
    // a 610 m mist - 66 x a clear night's extinction - that the lamps lit into glowing balls.) A level the clock doesn't
    // light, or lit by the original's light (no current relit data), keeps its own fog as a medium.
    state_of_fog.clock_air = rt::day_light().on && rt::cel_world().relit;
    state_of_fog.haze = state_of_fog.clock_air || !view.fog.on || !(view.fog.end > view.fog.start) ||
                        !std::isfinite(view.fog.start) ||
                        !std::isfinite(view.fog.end);
    // (round WF) a haze without a sun has the light list's lamps to light it: those of the last view, or the lighting's
    // analytic lamps now (the emitting triangles are known once a pass has asked for the list)
    // (round OM: the light list's emitting triangles too - a level lit by its glowing surfaces alone, no analytic lamp,
    // had no haze at all: the pass was never run, so its list never read)
    const bool may_have_lights =
        own.settings.lamps > 0 && rt::cel_world().relit && // (round FW: lamps light the air only with physical light)
        (own.lights_before || (!own.have_sun && (!scene_lighting(false, true).lamps.empty() ||
                                                 restir_listed_triangles_locked(state) > 0)));
    // (round FW: rt_fog_haze is the air of a level without fog; under the clock's physical air the weather's air is the
    // level's air whatever it says - the time of day's fog is out of the draws then)
    if (state_of_fog.haze &&
        ((!own.settings.haze && !state_of_fog.clock_air) || (!own.have_sun && !may_have_lights))) {
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
    own.medium = {};
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
                                     ? composite_pipeline(state, colour->format, colour->samples, &error, 0U)
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
    const uint32_t slices = own.settings.steps;
    ViewSlot* const slot = slot_for(state, clipped.rect, colour->extent, own.settings.scale, slices);
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
    // (added by the volumetric light) the cut-outs' test - the material table and this frame's geometry for the same
    // scene -, and the clouds' shadow map made for this viewport
    own.scene_view = current_scene(state);
    own.cutouts = own.scene_view.tlas == own.scene && own.scene_view.tlas != VK_NULL_HANDLE &&
                  own.scene_view.geometry != VK_NULL_HANDLE && ensure_material_layout(state) &&
                  state.materials.descriptor_set() != VK_NULL_HANDLE;
    own.have_cloud = own.have_sun && cloud_shadow_map(state, &own.cloud) && core::at(own.cloud.light, 3) > 0.0F;
    read_lights(state, commands); // (round WF)
    const uint32_t variant = (colour->samples != VK_SAMPLE_COUNT_1_BIT ? kVariantMultisampled : 0U) |
                             (own.aerial ? kVariantAerial : 0U) | (own.cutouts ? kVariantCutouts : 0U) |
                             (own.have_cloud ? kVariantClouds : 0U) | (own.lights ? kVariantLights : 0U) |
                             (own.lights && pool_off() ? kVariantPickedHere : 0U);
    const VkPipeline light = light_pipeline(state, variant, &error);
    const VkPipeline integrate = light != VK_NULL_HANDLE ? integrate_pipeline(state, variant, &error) : VK_NULL_HANDLE;
    const VkPipeline resolve = integrate != VK_NULL_HANDLE ? resolve_pipeline(state, variant, &error) : VK_NULL_HANDLE;
    if (resolve == VK_NULL_HANDLE) {
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_fog: the froxels' passes (variant %u) couldn't be made (%s) - no volumetric fog there",
                    variant, error.c_str());
        }
        return false;
    }
    bool have_before = slot->have_before && slot->frame_drawn + 1 >= state.frame;
    const bool cut = have_before && camera_cut(slot->shot, view.camera);
    have_before = have_before && !cut;
    if (!have_before) {
        slot->frames = 0;
    }
    // (added by the first frame - vulkan_rt_history.h: a history that starts again is seeded with several frames of
    // the froxels' light, so nothing fades in)
    const uint32_t seeds = seed_frames(true, !have_before, kFogSeedFrames);
    // (added by the distinct shafts) the air's sun map's image (bound by every froxel pass; made this frame where the
    // look has a volume round the eye and a light)
    if (!ensure_air_map(state, commands, own.settings.map)) {
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_fog: the air's sun map (%u texels a side) couldn't be made - the fog pass skipped",
                    own.settings.map);
        }
        return false;
    }
    set_rays(to_pixel, clipped.rect); // (added by the god rays)
    if (core::at(own.rays, 3) > 0.0F && !rays_pipelines(state, colour->samples != VK_SAMPLE_COUNT_1_BIT, &error)) {
        own.rays = {}; // (the froxels keep the whole phase)
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
            read_timer(state, pair, slot->grid);
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

    // 0. (added by the distinct shafts) the air's sun map, once a frame (the seeds share the camera and light)
    if (core::at(block.map_side, 3) > 0.0F) {
        const VkPipeline map_pass = air_map_pipeline(state, own.cutouts, &error);
        if (map_pass == VK_NULL_HANDLE) {
            core::at(block.map_side, 3) = 0.0F; // (no map: the froxels trace the light's rays everywhere)
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
            const FrameStep step(state.frames, commands, gpu_markers::section::kFogMap); // (round PF)
            dispatch(commands, map_pass,
                     {.scene = true, .cutouts = own.cutouts, .air_map = &own.air_map, .block = block_info},
                     {.width = own.air_map.extent.width, .height = own.air_map.extent.height});
        }
    }

    // 1. the froxels' light, each with its history (a seeded history: several frames of it from the same camera)
    const std::array<float, 2> reach = grid_reach(clipped, own.aerial);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        if (seed > 0) {
            // (the next frame's light from the same camera: the history just written is the one before)
            slot->current ^= 1U;
            ++slot->passes;
            ++slot->frames;
            slot->world_to_pixel = to_pixel;
            slot->camera = view.camera.position;
            slot->pre_exposure = rt::frame_pre_exposure(); // (round PX)
            slot->grid_before = reach;
            block = block_for(clipped, to_world, *slot, true, colour->samples);
            const std::optional<VkDescriptorBufferInfo> next = block_in_ring(block);
            if (!next) {
                slot->current ^= 1U; // (no room for more: the history so far is the frame's)
                --slot->passes;
                --slot->frames;
                break;
            }
            block_info = *next;
        }
        const uint32_t now = slot->current;
        const uint32_t before = now ^ 1U;
        const FrameStep step(state.frames, commands, gpu_markers::section::kFogLight);
        dispatch(commands, light,
                 {.scene = true,
                  .cloud_map = own.have_cloud,
                  .cutouts = own.cutouts,
                  .lights = own.lights,
                  .depth = depth, // (round MEDIUM: only the air the eye sees)
                  .out = &core::at(slot->light, now),
                  .out_info = &core::at(slot->light_b, now),
                  .history = &core::at(slot->light, before),
                  .history_info = &core::at(slot->light_b, before),
                  .air_map = &own.air_map,
                  .block = block_info},
                 slot->grid, slices);
    }
    // 2. the integration along each column, then the resolve (the checks' images: a texel a column)
    const PassBindings looked{.depth = depth,
                              .in = &slot->integrated,
                              .in_info = &slot->source,
                              .potential = &slot->potential,
                              .sky_light = &slot->sky,
                              .sky_lit = &slot->sky_lit,
                              .block = block_info};
    {
        const FrameStep step(state.frames, commands, gpu_markers::section::kFogIntegrate);
        dispatch(commands, integrate,
                 {.cloud_map = own.have_cloud,
                  .out = &slot->integrated,
                  .out_info = &slot->source,
                  .in = &core::at(slot->light, slot->current),
                  .in_info = &core::at(slot->light_b, slot->current),
                  .potential = &slot->potential,
                  .sky_light = &slot->sky,
                  .sky_lit = &slot->sky_lit,
                  .block = block_info},
                 slot->grid);
        PassBindings resolved = looked;
        resolved.out = &slot->resolved;
        resolved.out_info = &slot->resolved_info;
        resolved.rays_out = &slot->resolved_lit;
        resolved.raw_info = &slot->resolved_raw_info;
        dispatch(commands, resolve, resolved, slot->grid);
    }
    // 2a / 2b. (added by the god rays) the light's surroundings seen along each texel's line to the light: the mask,
    // then three radial passes, the images in turn - the last one written is the composite's
    const Image* rays = &slot->rays.front();
    constexpr float kRaysOnScreen = 1.5F; // (rays.w 1: the light in front - 2 behind)
    if (core::at(own.rays, 3) > 0.0F && core::at(own.rays, 3) < kRaysOnScreen) {
        const FrameStep step(state.frames, commands, gpu_markers::section::kFogRays); // (round PF)
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
    // 3. the composite: each pixel reads the integrated volume at its depth (the aerial air's light linear)
    const uint32_t composite_variant = own.aerial ? kVariantAerial : 0U;
    const VkPipeline drawn = composite_pipeline(state, colour->format, colour->samples, &error, composite_variant);
    if (drawn == VK_NULL_HANDLE) {
        if (first_report(state, kReportImages)) {
            get_log("vulkan: rt_fog: the aerial composite couldn't be made - the aerial air skipped");
        }
        depth_barrier(commands, depth, depth->resting, false);
        return false;
    }
    std::optional<FrameStep> composite_step;
    composite_step.emplace(state.frames, commands, gpu_markers::section::kFogComposite); // (round PF)
    PassBindings composited = looked;
    composited.rays_in = rays;
    record_composite(commands, drawn, colour, composited, clipped.rect);
    // (round FOG) Ray Reconstruction's transparency layer - the see-through draws kept out of the colour - reads the
    // same volume at the front layer's depth (the nearest see-through surface's; the colour's depth where there is none)
    record_layer_composite(state, commands, colour, &slot->resolved_lit, composited, block, composite_variant, clipped.rect);
    composite_step.reset();
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
    slot->pre_exposure = rt::frame_pre_exposure(); // (round PX)
    slot->shot = history_camera(view.camera);
    slot->have_before = true;
    slot->written = true;
    slot->frame_drawn = state.frame;
    slot->grid_before = reach;
    slot->current ^= 1U;
    ++slot->passes;
    ++slot->frames;
    ++own.passes;
    if (seeds > 1 && first_report(state, kReportSeeded)) {
        get_log("vulkan: rt_fog: a history started again (%s) - seeded with %u frames of the froxels' light in one "
                "(nothing fades in; logged once)",
                cut ? "a camera cut" : "a new view, a load or the settings", seeds);
    }
    if (first_report(state, kReportLight)) {
        get_log("vulkan: rt_fog: the air lit by %s (%.1f degrees up, colour %.2f %.2f %.2f, %.1f degrees across), %zu "
                "local lights of the light list, %s, %s",
                !own.light.have ? "no light" : (own.light.moonlit ? "the moon" : "the sun"),
                std::asin(std::clamp(static_cast<double>(core::at(own.to_sun, 1)), -1.0, 1.0)) * 180.0 /
                    std::numbers::pi,
                static_cast<double>(core::at(own.sun_tint, 0)), static_cast<double>(core::at(own.sun_tint, 1)),
                static_cast<double>(core::at(own.sun_tint, 2)), 2.0 * static_cast<double>(own.light.radius_degrees),
                own.lights ? own.local.lamp_list.size() + own.local.triangle_powers.size() : std::size_t{0},
                own.have_cloud ? "the clouds' shadows in it" : "no clouds' shadows",
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
        get_log("vulkan: rt_fog: the first fog pass (%ux%u: froxels %ux%ux%u of %u pixels, %u samples each a frame, %s, "
                "fog %.0f - %.0f, density %.2f, height %.0f, %s)",
                width, height, slot->grid.width, slot->grid.height, slices, slot->scale, own.settings.samples,
                what_is_drawn(), static_cast<double>(clipped.fog.start),
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
    kept.lamps = std::min(kept.lamps, kFogMostCandidates);
    kept.steps = ((kept.steps + kSliceColumns - 1) / kSliceColumns) * kSliceColumns; // (round FOG: whole rows of slices)
    kept.scale = std::clamp(kept.scale, kLeastScale, kMostScale);
    kept.samples = std::clamp(kept.samples, 1U, kMostSamples);
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
    // (round FW: under the clock's physical air the game's time of day fog stays out of the draws - the pass's air is the
    // air there)
    replacing() = recorded && fog_state().settings.mode == RtFogMode::kReplace &&
                  (!fog_state().haze || fog_state().clock_air);
    return recorded;
}

rt::AirMedium rt_fog_medium_locked(const Backend& state)
{
    (void)state;
    const FogState& own = fog_state();
    if (!own.settings.on || own.failed) {
        return {}; // (no pass runs: the last one's air is gone)
    }
    return own.medium;
}

RtFogLights rt_fog_lights_locked(const Backend& state)
{
    (void)state;
    const FogState& own = fog_state();
    if (!own.lights) {
        return {};
    }
    return {.lamps = own.local.lamp_list,
            .triangles = own.local.triangle_powers.size(),
            .scale = own.lights_scale,
            .frame = own.lights_frame};
}

uint64_t rt_fog_passes() { return fog_state().passes; }

void set_rt_fog_light_pool(bool on)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    pool_off() = !on;
}

RtFogTiming rt_fog_timing() { return fog_state().timing; }

void rt_fog_reset_history(Backend& state)
{
    (void)state;
    for (const std::unique_ptr<ViewSlot>& slot : fog_state().slots) {
        slot->have_before = false;
        slot->passes = 0;
        slot->frames = 0;
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
        Image* image = &slot->resolved;
        if (which == RtFogImage::kRawInfo) {
            image = &slot->resolved_raw_info;
        } else if (which == RtFogImage::kHistoryInfo) {
            image = &slot->resolved_info;
        } else if (which == RtFogImage::kRawLit) {
            image = &slot->resolved_lit;
        } else if (which == RtFogImage::kLight) {
            image = &core::at(slot->light, last);
        } else if (which == RtFogImage::kIntegrated) {
            image = &slot->integrated;
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
        for (const auto* passes : {&own.light_pass, &own.integrate, &own.resolve}) {
            for (const VkPipeline pipeline : *passes) {
                if (pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                }
            }
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
    own.light_pass = {};
    own.light_failed = {};
    own.integrate = {};
    own.integrate_failed = {};
    own.resolve = {};
    own.resolve_failed = {};
    own.made = false;
    own.air_map_pass = {};
    own.air_map_failed = {};
    own.rays_mask = {};
    own.rays_radial = {};
    own.rays_failed = false;
    own.linear_sampler = VK_NULL_HANDLE;
    own.layout_materials = VK_NULL_HANDLE;
    own.material_layout = VK_NULL_HANDLE;
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.scene = VK_NULL_HANDLE;
    own.failed = false;
    replacing() = false;
}

} // namespace render::vulkan
