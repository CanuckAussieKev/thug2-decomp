// NOT ORIGINAL - the indirect light and the environment's specular by ReSTIR GI (vulkan_restir_gi.h; path tracing plan,
// stage 5).
#include "gpu_vulkan/vulkan_restir_gi.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_light_list.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x52470001U;
constexpr uint64_t kReportBuffers = 0x52470002U;
constexpr uint64_t kReportFirst = 0x52470003U;

// The bindings of set 0 (pushed); set 1 the material table.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingGeometry = 1;
constexpr uint32_t kBindingBlock = 2;
constexpr uint32_t kBindingLights = 3;      // the full light list's alias table (the disc, the sky, triangles, lamps)
constexpr uint32_t kBindingEmitters = 4;    // ReSTIR DI's list of emitting triangles
constexpr uint32_t kBindingLamps = 5;       // ... and its lamps' records
constexpr uint32_t kBindingMembership = 6;
constexpr uint32_t kBindingSlots = 7;
constexpr uint32_t kBindingSkyCells = 8;    // the sky's grid in use (its radiance per cell)
constexpr uint32_t kBindingSkyAlias = 9;    // ... its alias tables
constexpr uint32_t kBindingReservoirsA = 10; // 3 words a pixel: this frame's after the temporal pass
constexpr uint32_t kBindingReservoirsB = 11; // ... after the spatial pass - the next frame's history
constexpr uint32_t kBindingNowAt = 12;
constexpr uint32_t kBindingNowMore = 13;
constexpr uint32_t kBindingBeforeAt = 14;
constexpr uint32_t kBindingBeforeMore = 15;
constexpr uint32_t kBindingSpecularRays = 16; // 2 a pixel: the mirror-like surfaces' specular ray (pass 1 -> pass 2)
constexpr uint32_t kBindingDepth = 17;
constexpr uint32_t kBindingLampsIo = 18;
constexpr uint32_t kBindingLampsOnIo = 19;
constexpr uint32_t kBindingVisibilityIo = 20;
constexpr uint32_t kBindingSpecularOut = 21;
constexpr uint32_t kBindingSpecularHit = 22;
constexpr uint32_t kBindingSums = 23;
constexpr uint32_t kBindingCounters = 24;
constexpr uint32_t kBindingTransmittance = 25; // (the sky's grid: the atmosphere's tables)
constexpr uint32_t kBindingSkyView = 26;
constexpr uint32_t kBindingCloudMap = 27;   // (the clouds' shadow map on the disc's light)
constexpr uint32_t kBindingCloudNoise = 28; // .. 30 (the sky's grid's march)
constexpr uint32_t kCloudNoiseTextures = 3;
constexpr uint32_t kBindingCount = 31;
constexpr uint32_t kMaterialSet = 1;

constexpr uint32_t kGroupSide = 8;
constexpr VkDeviceSize kUniformAlignment = 256;
constexpr std::size_t kMatrixSide = 4;
constexpr double kSingular = 1e-300;
constexpr uint32_t kReservoirWords = 2; // uvec4s a pixel
constexpr uint32_t kSpecularRayWords = 2;

// The estimator's settings (fixed - no per-level values): the spatial neighbours, the reservoirs' sample count kept at
// most (one candidate a frame: 20 frames' worth - Ouyang et al. keep 30), the neighbours' radius (pixels), the surface
// tests (ReSTIR DI's), the share of the initial samples drawn from the sky's distribution (the rest from the BSDF).
constexpr uint32_t kSpatialNeighbours = 2;
constexpr uint32_t kMostSpatial = 4;
constexpr uint32_t kHistoryCap = 20;
constexpr float kSpatialRadius = 24.0F;
constexpr float kNormalAgreement = 0.9F;
constexpr float kPlaneShare = 0.02F;
constexpr float kSkyShare = 0.25F;
constexpr uint32_t kFlagTemporal = 1U;
constexpr uint32_t kFlagSpatial = 2U;
constexpr uint32_t kFlagAccumulate = 4U;
constexpr uint32_t kFlagForced = 8U;
constexpr uint32_t kFlagPermute = 16U; // (round PT4d) vulkan_light_list.h restir_denoiser_glsl
constexpr uint32_t kFlagBoiling = 32U;

constexpr uint32_t kTimingSlots = 16;
constexpr uint32_t kTimingReport = 600;
constexpr uint32_t kCheckTimingReport = 64;

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

// The block every shader of the passes reads (std140).
struct GiBlock {
    std::array<float, 16> to_world{};      // (pixel x, pixel y, depth, 1) -> world, column-major
    std::array<float, 16> prev_to_pixel{}; // world -> last frame's (pixel x, pixel y, depth, w), column-major
    std::array<float, 4> eye{};            // xyz the camera; w the frame's pre-exposure (the inputs' unit)
    std::array<float, 4> prev_eye{};
    std::array<int32_t, 4> rect{};         // the view: x, y, width, height (target pixels)
    std::array<uint32_t, 4> counts{};      // x the list's entries, y lamps, z the frame's number, w flags
    std::array<uint32_t, 4> params{};      // x -, y spatial neighbours, z M's cap, w the seed
    std::array<float, 4> tune{};           // x spatial radius, y least normal agreement, z most plane share, w the sky's share
    std::array<float, 4> force{};          // x roughness, y F0, z the albedo's scale (the check's material)
    std::array<uint32_t, 4> instances{};   // xy the instances' device address, z instances
    std::array<float, 4> sun{};            // xyz toward the disc, w the cosine of its radius (kNoDisc: none)
    std::array<float, 4> sun_radiance{};   // rgb the disc's radiance, w its solid angle
    std::array<float, 4> sky{};            // (the sky's grid's inputs - vulkan_light_list.h SkyGridInputs)
    std::array<float, 4> sky_table{};
    std::array<float, 4> cloud_ray{};
    std::array<float, 4> cloud_sun{};
    std::array<float, 4> cloud_toward{};
    std::array<float, 4> sky_dist{};       // x 1 / the grid in use's luminance integral (0: no sky), y kGlossyReuse
    std::array<float, 4> cloud_place{};    // (the clouds' shadow map: vulkan_rt_sky_pass.h CloudShadowMap)
    std::array<float, 4> cloud_light{};
};
static_assert(sizeof(GiBlock) == (2 * 64) + (18 * 16));

// --- the shaders -------------------------------------------------------------------------------------------------------

constexpr const char* kCommon = R"(
layout(std140, set = 0, binding = 2) uniform GiBlock {
    mat4 to_world;
    mat4 prev_to_pixel;
    vec4 eye;
    vec4 prev_eye;
    ivec4 rect;
    uvec4 counts;
    uvec4 params;
    vec4 tune;
    vec4 force;
    uvec4 instances;
    vec4 sun;
    vec4 sun_radiance;
    vec4 sky;
    vec4 sky_table;
    vec4 cloud_ray;
    vec4 cloud_sun;
    vec4 cloud_toward;
    vec4 sky_dist;
    vec4 cloud_place;
    vec4 cloud_light;
} pass;
struct LightEntry {
    float threshold;
    uint alias;
    float pdf;
    uint payload;
};
layout(std430, set = 0, binding = 3) readonly buffer Lights { LightEntry light_entries[]; };
layout(std430, set = 0, binding = 4) readonly buffer Emitters {
    uint emitter_count;
    uint emitter_spare;
    uint emitter_unused;
    uint emitter_pad;
    uvec4 emitter_entries[]; // instance, primitive, power (float bits), -
};
layout(std430, set = 0, binding = 5) readonly buffer Lamps { LightRecord lamps[]; };
layout(std430, set = 0, binding = 6) readonly buffer Membership { uint membership[]; };
layout(std430, set = 0, binding = 7) readonly buffer Slots { uint slots[]; };
layout(std430, set = 0, binding = 8) buffer SkyCells { vec4 sky_cells[]; };
layout(std430, set = 0, binding = 9) readonly buffer SkyAlias { uvec2 sky_alias[]; };
#define LIST_INSTANCE_ADDRESS pass.instances.xy
#define LIST_INSTANCES pass.instances.z
)";

// The two passes (PASS_TEMPORAL: the surface, the initial sample, the temporal reuse, a mirror's specular ray;
// PASS_SPATIAL: the spatial reuse and the result). CHECK: the check's variant - the surface from camera rays, the result
// accumulated. CLOUD_MAP: the disc's light through the clouds' shadow map.
constexpr const char* kGiBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 10) buffer ReservoirsA { uvec4 res_a[]; };
layout(std430, set = 0, binding = 11) buffer ReservoirsB { uvec4 res_b[]; };
layout(std430, set = 0, binding = 12) buffer SurfacesNow { vec4 now_at[]; };
layout(std430, set = 0, binding = 13) buffer SurfacesNowMore { uvec4 now_more[]; };
layout(std430, set = 0, binding = 14) readonly buffer SurfacesBefore { vec4 before_at[]; };
layout(std430, set = 0, binding = 15) readonly buffer SurfacesBeforeMore { uvec4 before_more[]; };
layout(std430, set = 0, binding = 16) buffer SpecularRays { vec4 specular_rays[]; };
#ifdef CHECK
layout(std430, set = 0, binding = 23) buffer Sums { vec4 sums[]; };
#else
layout(set = 0, binding = 17, r32f) uniform readonly image2D u_depth;
layout(set = 0, binding = 18, rgba16f) uniform image2D io_lamps;
layout(set = 0, binding = 19, rgba16f) uniform image2D io_lamps_on;
layout(set = 0, binding = 20, rgba16f) uniform image2D io_visibility;
layout(set = 0, binding = 21, rgba16f) uniform image2D io_specular;
layout(set = 0, binding = 22, r16f) uniform writeonly image2D o_specular_hit;
#endif
layout(std430, set = 0, binding = 24) buffer Counters { uint bad_samples; uint counter_spare[3]; };

const uint FLAG_TEMPORAL = 1u;
const uint FLAG_SPATIAL = 2u;
const uint FLAG_ACCUMULATE = 4u;
const uint FLAG_FORCED = 8u;
const uint FLAG_PERMUTE = 16u;
const uint FLAG_BOILING = 32u;
const uint MOST_SPATIAL = 4u;
const uint LIGHT_RAYS = 7u;           // world, objects, alpha-tested (the reference's rays)
const uint LIGHT_RAYS_ON_OBJECT = 5u; // (an object's pixel: the world's alone - its low-poly body doesn't shadow itself)
const float GI_REACH = 1.0e6;
const float GI_MISS_DISTANCE = 65504.0; // (Ray Reconstruction's hit distance where the ray met nothing)
const uint KIND_NONE = 0u;
const uint KIND_SURFACE = 1u;
const uint KIND_SKY = 2u;

float ray_offset(vec3 p) { return 0.02 + 2.0e-5 * max(abs(p.x), max(abs(p.y), abs(p.z))); }
vec3 world_at(vec2 pixel, float depth)
{
    vec4 w = pass.to_world * vec4(pixel, depth, 1.0);
    return w.xyz / w.w;
}
vec2 oct_wrap(vec2 v) { return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }
vec2 oct_encode(vec3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
}
vec3 oct_decode(vec2 e)
{
    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = oct_wrap(n.xy);
    }
    return normalize(n);
}
float good(float x) { return isnan(x) || isinf(x) ? 0.0 : x; }
vec3 good3(vec3 x) { return any(isnan(x)) || any(isinf(x)) ? vec3(0.0) : x; }

// A pixel's surface: where it is, its shading normal (toward the eye) and its triangle's, its albedo (linear - what the
// lamps input is put on), its material's F0 and roughness, whether it is an object's, a metal's (no diffuse).
struct Surface {
    vec3 at;
    vec3 n;
    vec3 ng;
    vec3 albedo;
    vec3 f0;
    float rough;
    bool valid;
    bool object;
    bool metal;
};
Surface no_surface()
{
    return Surface(vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), vec3(0.0), 1.0, false, false, false);
}
void put_surface(uint i, Surface s)
{
    now_at[i] = vec4(s.at, s.valid ? 1.0 : 0.0);
    now_more[i] = uvec4(packSnorm2x16(oct_encode(s.n)), packSnorm2x16(oct_encode(s.ng)),
                        packUnorm4x8(vec4(sqrt(clamp(s.albedo, 0.0, 1.0)), clamp(s.rough, 0.0, 1.0))),
                        (s.object ? 1u : 0u) | (s.metal ? 2u : 0u) | (packUnorm4x8(vec4(sqrt(clamp(s.f0, 0.0, 1.0)), 0.0)) << 8));
}
Surface surface_of(vec4 at, uvec4 more)
{
    Surface s;
    s.at = at.xyz;
    s.valid = at.w > 0.5;
    s.n = oct_decode(unpackSnorm2x16(more.x));
    s.ng = oct_decode(unpackSnorm2x16(more.y));
    vec4 a = unpackUnorm4x8(more.z);
    s.albedo = a.rgb * a.rgb;
    s.rough = a.a;
    s.object = (more.w & 1u) != 0u;
    s.metal = (more.w & 2u) != 0u;
    vec3 f = unpackUnorm4x8(more.w >> 8).rgb;
    s.f0 = f * f;
    return s;
}
BrdfMaterial material_of(Surface s) { return brdf_material(s.metal ? vec3(0.0) : s.albedo, s.f0, s.rough); }
bool glossy(Surface s) { return s.rough >= pass.sky_dist.y; }

// Whether `other` is the same surface as `s` near enough to share its light (ReSTIR DI's tests).
bool alike(Surface s, Surface other)
{
    if (!other.valid || dot(s.n, other.n) < pass.tune.y) {
        return false;
    }
    float distance = length(s.at - pass.eye.xyz);
    return abs(dot(other.at - s.at, s.ng)) <= pass.tune.z * distance && abs(dot(s.at - other.at, other.ng)) <= pass.tune.z * distance;
}

// The closest surface along a ray (cut-outs by their alpha).
bool trace_closest(uint mask, vec3 origin, vec3 dir, float reach, out uint instance, out uint primitive, out uint id,
                   out vec2 barycentrics, out float t, out mat4x3 to_world)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsNoneEXT, mask, origin, 0.0, dir, reach);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            cutout_kept(rayQueryGetIntersectionInstanceIdEXT(query, false),
                        rayQueryGetIntersectionPrimitiveIndexEXT(query, false),
                        rayQueryGetIntersectionInstanceCustomIndexEXT(query, false),
                        rayQueryGetIntersectionBarycentricsEXT(query, false))) {
            rayQueryConfirmIntersectionEXT(query);
        }
    }
    instance = 0u;
    primitive = 0u;
    id = 0u;
    barycentrics = vec2(0.0);
    t = reach;
    to_world = mat4x3(1.0);
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return false;
    }
    instance = rayQueryGetIntersectionInstanceIdEXT(query, true);
    primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    id = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
    barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
    t = rayQueryGetIntersectionTEXT(query, true);
    to_world = rayQueryGetIntersectionObjectToWorldEXT(query, true);
    return true;
}

BrdfMaterial hit_material(HitSurface h)
{
    if ((pass.counts.w & FLAG_FORCED) != 0u) {
        return brdf_material(h.base_colour * pass.force.z, vec3(pass.force.y), pass.force.x);
    }
    return h.material;
}

#ifndef CHECK
// (round PT4d) The depth's own surface where the drawn surface isn't the one the rays find (ReSTIR DI's depth_surface,
// vulkan_restir_di.cpp): the point from the depth, the normal from the nearer neighbours' depths, a diffuse dielectric
// (F0 0.04, rough). Before, such a pixel kept the light store's environment - the outlines of tessellated, skinned and
// ray-less surfaces (grass, litter) another owner's light, far brighter than ReSTIR GI's at night: white halos that
// flickered with the jitter.
vec3 depth_point(ivec2 q, out bool drawn)
{
    q = clamp(q, pass.rect.xy, pass.rect.xy + pass.rect.zw - 1);
    float d = imageLoad(u_depth, q).r;
    drawn = d < 1.0;
    return world_at(vec2(q) + 0.5, d);
}
Surface depth_surface(ivec2 p, vec3 here, vec3 dir)
{
    float best[2] = float[2](1.0e30, 1.0e30);
    vec3 step[2] = vec3[2](vec3(0.0), vec3(0.0));
    for (int k = 0; k < 4; ++k) {
        ivec2 o = k == 0 ? ivec2(1, 0) : (k == 1 ? ivec2(-1, 0) : (k == 2 ? ivec2(0, 1) : ivec2(0, -1)));
        bool drawn;
        vec3 q = depth_point(p + o, drawn);
        float apart = abs(dot(q - here, dir));
        int axis = k < 2 ? 0 : 1;
        if (drawn && apart < best[axis]) {
            best[axis] = apart;
            step[axis] = (q - here) * float(o.x + o.y);
        }
    }
    vec3 n = cross(step[0], step[1]);
    n = dot(n, n) > 0.0 ? normalize(n) : -dir;
    if (dot(n, dir) > 0.0) {
        n = -n;
    }
    return Surface(here, n, n, vec3(0.0), vec3(0.04), 1.0, true, false, false);
}
#endif

// The pixel's surface: the check's camera ray through its centre; the game's the prepass's depth there, its triangle the
// one a camera ray meets at that point - (round PT4d) where none does, the depth's own surface (depth_surface).
Surface primary(ivec2 p)
{
    vec2 centre = vec2(p) + 0.5;
    vec3 eye = pass.eye.xyz;
    vec3 dir;
    float reach;
    float expected = -1.0;
#ifdef CHECK
    dir = normalize(world_at(centre, 1.0) - eye);
    reach = 1.0e6;
#else
    float d = imageLoad(u_depth, p).r;
    if (!(d < 1.0)) {
        return no_surface();
    }
    vec3 here = world_at(centre, d);
    expected = length(here - eye);
    if (!(expected > 0.0)) {
        return no_surface();
    }
    dir = (here - eye) / expected;
    reach = expected * 1.05 + 1.0;
#endif
    uint instance;
    uint primitive;
    uint id;
    vec2 bary;
    float t;
    mat4x3 to_world;
    HitSurface h;
    if (!trace_closest(LIGHT_RAYS, eye, dir, reach, instance, primitive, id, bary, t, to_world) ||
        (expected >= 0.0 && abs(t - expected) > 1.0 + 0.02 * expected) ||
        !hit_surface(instance, primitive, id, bary, to_world, dir, h) || h.hidden) {
#ifdef CHECK
        return no_surface();
#else
        return depth_surface(p, here, dir);
#endif
    }
    BrdfMaterial m = hit_material(h);
    Surface s;
    s.at = h.position;
    s.ng = h.normal;
    s.n = dot(h.shading_normal, -dir) > 0.0 ? h.shading_normal : h.normal;
    s.metal = (pass.counts.w & FLAG_FORCED) == 0u && h.kind == HIT_KIND_METAL;
    s.albedo = (pass.counts.w & FLAG_FORCED) != 0u ? m.albedo : h.base_colour;
    s.f0 = m.f0;
    s.rough = m.roughness;
    s.object = (geometry_record(instance).instance_flags & HIT_INSTANCE_OBJECT) != 0u;
    s.valid = true;
    return s;
}

uint rays_of(Surface s) { return s.object ? LIGHT_RAYS_ON_OBJECT : LIGHT_RAYS; }

// The disc's light the clouds let through to `at` (their shadow map - the shadows pass's clouds_through); 1 without it.
float sun_kept(vec3 at)
{
#ifdef CLOUD_MAP
    return cloud_map_kept(at, pass.cloud_place, pass.cloud_light);
#else
    return 1.0;
#endif
}
// The sky's radiance toward `dir` (the disc not in it): the grid in use's cell.
vec3 sky_radiance(vec3 dir) { return pass.sky_dist.x > 0.0 ? sky_cells[sky_cell_of(dir)].rgb : vec3(0.0); }
// The density (solid angle) of the sky's sampling toward `dir`: its cell's luminance over the grid's integral.
float sky_density(vec3 dir) { return pass.sky_dist.x > 0.0 ? path_luma(sky_cells[sky_cell_of(dir)].rgb) * pass.sky_dist.x : 0.0; }

// The secondary vertex's reflected light toward `v` by next-event estimation: one light of the full list (the disc, the
// sky, an emitting triangle, a lamp - picked by power), one ray to it - the reference's light sampling without its MIS
// (no BSDF ray continues from here).
vec3 next_event(vec3 p, vec3 n, vec3 ng, vec3 v, BrdfMaterial m, inout uint rng)
{
    float u = path_next(rng);
    float u1 = path_next(rng);
    float u2 = path_next(rng);
    float u3 = path_next(rng);
    float u4 = path_next(rng);
    uint count = pass.counts.x;
    if (count == 0u) {
        return vec3(0.0);
    }
    LightEntry e = light_entries[light_pick(u, count)];
    if (!(e.pdf > 0.0)) {
        return vec3(0.0);
    }
    uint kind = light_kind(e.payload);
    if (kind == LIGHT_DISC) {
        vec3 dir = path_to_world(pass.sun.xyz, path_cone_direction(pass.sun.w, u1, u2));
        if (dot(dir, ng) <= 0.0) {
            return vec3(0.0);
        }
        vec3 f = path_bsdf_cosine(m, n, v, dir);
        if (max(f.r, max(f.g, f.b)) <= 0.0) {
            return vec3(0.0);
        }
        float pdf = e.pdf / pass.sun_radiance.w;
        return f * pass.sun_radiance.rgb * sun_kept(p) * ray_light(LIGHT_RAYS, p, dir, GI_REACH) / pdf;
    }
    if (kind == LIGHT_SKY) {
        uint cell;
        vec3 dir = sky_sample(u1, u2, u3, u4, cell);
        if (dot(dir, ng) <= 0.0 || dot(dir, n) <= 0.0) {
            return vec3(0.0);
        }
        vec3 radiance = sky_cells[cell].rgb;
        float pdf = e.pdf * path_luma(radiance) * pass.sky_dist.x;
        if (!(pdf > 0.0)) {
            return vec3(0.0);
        }
        vec3 f = path_bsdf_cosine(m, n, v, dir);
        if (max(f.r, max(f.g, f.b)) <= 0.0) {
            return vec3(0.0);
        }
        return f * radiance * ray_light(LIGHT_RAYS, p, dir, GI_REACH) / pdf;
    }
    if (kind == LIGHT_TRIANGLE) {
        uvec4 listed = emitter_entries[light_index(e.payload)];
        HitVertices tri;
        if (!hit_vertices(listed.x, listed.y, tri)) {
            return vec3(0.0);
        }
        mat4x3 to_world = instance_transform(listed.x);
        vec3 a = to_world * vec4(tri.position[0], 1.0);
        vec3 b = to_world * vec4(tri.position[1], 1.0);
        vec3 c = to_world * vec4(tri.position[2], 1.0);
        vec3 across = cross(b - a, c - a);
        float twice = length(across);
        if (!(twice > 0.0)) {
            return vec3(0.0);
        }
        vec3 bc = path_triangle_point(u1, u2);
        vec3 y = a * bc.x + b * bc.y + c * bc.z;
        vec3 to = y - p;
        float d2 = dot(to, to);
        float d = sqrt(d2);
        if (!(d > 0.0)) {
            return vec3(0.0);
        }
        vec3 dir = to / d;
        float cos_there = abs(dot(across / twice, dir));
        if (dot(dir, ng) <= 0.0 || !(cos_there > 0.0)) {
            return vec3(0.0);
        }
        vec3 f = path_bsdf_cosine(m, n, v, dir);
        if (max(f.r, max(f.g, f.b)) <= 0.0) {
            return vec3(0.0);
        }
        vec3 emitted;
        if (!hit_emission(listed.x, listed.y, instance_material(listed.x), bc.yz, emitted)) {
            return vec3(0.0);
        }
        float pdf = e.pdf / (0.5 * twice) * d2 / cos_there;
        return f * max(emitted, vec3(0.0)) * ray_light(LIGHT_RAYS, p, dir, max(d * 0.999 - ray_offset(y), 0.0)) / pdf;
    }
    LightRecord light = lamps[light_index(e.payload)];
    vec3 from;
    float g = lamp_geometry(light, p, n, from);
    if (!(g > 0.0)) {
        return vec3(0.0);
    }
    vec3 to = from - p;
    float d = length(to);
    vec3 dir = to / max(d, 1e-6);
    float facing = dot(n, dir);
    if (!(facing > 1.0e-4) || dot(dir, ng) <= 0.0) {
        return vec3(0.0);
    }
    vec3 through = ray_light(LIGHT_RAYS, p, dir, max(d - light.at.w, 0.0));
    return brdf_direct(m, n, v, dir, light.colour.rgb * through * (g / facing)) / e.pdf;
}

// What a ray from `origin` meets: a surface (its point, its normal toward the ray, its reflected light toward the ray's
// origin - next_event -, how far) or nothing (the sky that way). Meshes the light store hides (the bake's shadows drawn
// as geometry) are passed. (Round BR) Not its emission: the light straight from an emitter - on a diffuser and in a
// specular lobe - is ReSTIR DI's, sampled on the emitter (one owner; a path's first vertex on a light is direct light).
struct Secondary {
    bool hit;
    vec3 at;
    vec3 n;
    vec3 bounce;
    float t;
};
Secondary trace_secondary(uint mask, vec3 origin, vec3 dir, inout uint rng)
{
    Secondary out_s = Secondary(false, vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), GI_REACH);
    float travelled = 0.0;
    for (uint skipped = 0u; skipped < 4u; ++skipped) {
        uint instance;
        uint primitive;
        uint id;
        vec2 bary;
        float t;
        mat4x3 to_world;
        if (!trace_closest(mask, origin, dir, GI_REACH, instance, primitive, id, bary, t, to_world)) {
            return out_s;
        }
        HitSurface h;
        if (!hit_surface(instance, primitive, id, bary, to_world, dir, h)) {
            return out_s;
        }
        if (h.hidden) {
            origin = h.position + dir * ray_offset(h.position);
            travelled += t;
            continue;
        }
        vec3 ng = h.normal;
        vec3 v = -dir;
        vec3 n = dot(h.shading_normal, v) > 0.0 ? h.shading_normal : ng;
        BrdfMaterial m = hit_material(h);
        out_s.hit = true;
        out_s.at = h.position;
        out_s.n = ng;
        out_s.t = travelled + t;
        out_s.bounce = good3(next_event(h.position + ng * ray_offset(h.position), n, ng, v, m, rng));
        return out_s;
    }
    return out_s;
}

// A white diffuser's lobe and the material's specular lobe toward `dir` (x the cosine).
float white_lobe(Surface s, vec3 dir) { return max(dot(s.n, dir), 0.0) / PATH_PI; }
vec3 specular_lobe(Surface s, vec3 v, vec3 dir)
{
    if (dot(s.n, dir) <= 0.0) {
        return vec3(0.0);
    }
    return brdf_direct(brdf_material(vec3(0.0), s.f0, s.rough), s.n, v, dir, vec3(1.0)) / PATH_PI;
}
vec3 view_of(Surface s, vec3 eye) { return normalize(eye - s.at); }

// A reservoir: its sample (a point on a surface and that surface's normal toward the pixel that found it, or a direction
// to the sky), the sample's reflected light, its weight W (the unbiased contribution weight - 1 / the sample's effective
// pdf in solid angle at its pixel) and its sample count M.
struct Reservoir {
    uint kind;
    vec3 y;
    vec3 ny;
    vec3 bounce;
    float W;
    float M;
};
Reservoir no_reservoir(float m) { return Reservoir(KIND_NONE, vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), 0.0, m); }
uvec2 pack_light(vec3 c)
{
    c = max(c, vec3(0.0));
    float most = max(c.r, max(c.g, c.b));
    return uvec2(floatBitsToUint(most), packUnorm4x8(vec4(most > 0.0 ? c / most : vec3(0.0), 0.0)));
}
vec3 unpack_light(uint most, uint ratio) { return uintBitsToFloat(most) * unpackUnorm4x8(ratio).rgb; }
void store_reservoir(bool into_b, uint i, Reservoir r)
{
    uvec2 b = pack_light(r.bounce);
    uvec4 w0 = uvec4(floatBitsToUint(r.y), packSnorm2x16(oct_encode(r.ny)));
    uvec4 w1 = uvec4(b.x, b.y | (r.kind << 24), floatBitsToUint(r.W), floatBitsToUint(r.M));
    if (into_b) {
        res_b[i * 2u] = w0;
        res_b[i * 2u + 1u] = w1;
    } else {
        res_a[i * 2u] = w0;
        res_a[i * 2u + 1u] = w1;
    }
}
Reservoir reservoir_of(uvec4 w0, uvec4 w1)
{
    Reservoir r;
    r.kind = w1.y >> 24;
    r.y = uintBitsToFloat(w0.xyz);
    r.ny = oct_decode(unpackSnorm2x16(w0.w));
    r.bounce = unpack_light(w1.x, w1.y & 0x00FFFFFFu);
    r.W = uintBitsToFloat(w1.z);
    r.M = uintBitsToFloat(w1.w);
    return r;
}
Reservoir load_a(uint i) { return reservoir_of(res_a[i * 2u], res_a[i * 2u + 1u]); }
Reservoir load_b(uint i) { return reservoir_of(res_b[i * 2u], res_b[i * 2u + 1u]); }

// A reservoir's sample seen from surface `s`: the way to it, how far to trace, and its surface element's solid angle
// factor there (cos at the sample / d^2 - the reconnection's Jacobian is this over its value at the sample's own pixel;
// 1 for the sky). False: `s` can't take it (behind the sample's surface - its stored light is its front's).
bool sample_from(Surface s, Reservoir r, out vec3 dir, out float reach, out float g)
{
    dir = vec3(0.0, 1.0, 0.0);
    reach = 0.0;
    g = 0.0;
    if (r.kind == KIND_SKY) {
        dir = r.y;
        reach = GI_REACH;
        g = 1.0;
        return true;
    }
    if (r.kind != KIND_SURFACE) {
        return false;
    }
    vec3 to = r.y - s.at;
    float d2 = dot(to, to);
    if (!(d2 > 0.0)) {
        return false;
    }
    float d = sqrt(d2);
    dir = to / d;
    float c = dot(r.ny, -dir);
    if (!(c > 0.0)) {
        return false;
    }
    g = c / d2;
    reach = max(d * 0.999 - ray_offset(r.y), 0.0);
    return true;
}

// The target at surface `s` (seen from `eye`) of a sample's light from direction `dir`: the luminance of what a white
// diffuser's lobe takes of its reflected light, and where the surface is glossy, what its specular lobe takes of it. (Any
// target > 0 wherever the outputs' integrands are keeps the estimate unbiased; this one follows what the sample brings.)
float target_of(Surface s, vec3 eye, vec3 dir, vec3 bounce)
{
    if (dot(s.ng, dir) <= 0.0 || dot(s.n, dir) <= 0.0) {
        return 0.0;
    }
    float t = path_luma(bounce) * white_lobe(s, dir);
    if (glossy(s)) {
        t += path_luma(bounce) * path_luma(specular_lobe(s, view_of(s, eye), dir));
    }
    return good(t);
}
float target_at(Surface s, vec3 eye, Reservoir r, out vec3 dir, out float reach, out float g)
{
    if (!sample_from(s, r, dir, reach, g)) {
        return 0.0;
    }
    return target_of(s, eye, dir, r.bounce);
}
// What of the light gets from the sample to the surface (the one light ray - vulkan_rt_scene.h ray_light).
vec3 seen(Surface s, vec3 dir, float reach) { return ray_light(rays_of(s), s.at + s.ng * ray_offset(s.at), dir, reach); }
bool seen_at_all(vec3 through) { return max(through.r, max(through.g, through.b)) > 0.0; }

#ifdef PASS_TEMPORAL
// The pixel's reservoir after its sample and the temporal reuse (stored by main, after the boiling filter); `weight` its
// sample's contribution (target x W - what the filter compares). Also the mirror-like surfaces' specular ray.
Reservoir temporal_reservoir(ivec2 local, uint i, out float weight)
{
    weight = 0.0;
    Surface s = primary(pass.rect.xy + local);
    put_surface(i, s);
    specular_rays[i * 2u] = vec4(0.0, 0.0, 0.0, GI_MISS_DISTANCE);
    specular_rays[i * 2u + 1u] = vec4(0.0);
    if (!s.valid) {
        return no_reservoir(0.0);
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z, pass.params.w);
    vec3 eye = pass.eye.xyz;
    vec3 v = view_of(s, eye);
    float nv = max(dot(s.n, v), 1.0e-4);
    BrdfMaterial m = material_of(s);
    float alpha = brdf_alpha(s.rough);
    float cap = float(pass.params.z);
    vec3 origin = s.at + s.ng * ray_offset(s.at);
    uint mask = rays_of(s);

    // 1. one sample: a direction from the sky's distribution or the BSDF (the cosine lobe; GGX's visible normals where
    // glossy), its pdf the mix's
    float sky_share = pass.sky_dist.x > 0.0 ? pass.tune.w : 0.0;
    float specular_share = glossy(s) ? clamp(path_specular_probability(m, nv), 0.0, 0.9) : 0.0;
    float pick = path_next(rng);
    float lobe = path_next(rng);
    float u1 = path_next(rng);
    float u2 = path_next(rng);
    float u3 = path_next(rng);
    float u4 = path_next(rng);
    vec3 dir;
    if (pick < sky_share) {
        uint cell;
        dir = sky_sample(u1, u2, u3, u4, cell);
    } else if (lobe < specular_share) {
        vec3 h = path_to_world(s.n, path_ggx_visible_normal(path_to_local(s.n, v), alpha, u1, u2));
        dir = 2.0 * dot(v, h) * h - v;
    } else {
        dir = path_to_world(s.n, path_cosine_direction(u1, u2));
    }
    Reservoir r = no_reservoir(1.0);
    float chosen = 0.0;
    if (dot(dir, s.ng) > 0.0 && dot(dir, s.n) > 0.0) {
        vec3 hv = normalize(v + dir);
        float bsdf_pdf = specular_share * path_ggx_reflection_pdf(nv, clamp(dot(s.n, hv), 0.0, 1.0), alpha) +
                         (1.0 - specular_share) * dot(s.n, dir) / PATH_PI;
        float pdf = sky_share * sky_density(dir) + (1.0 - sky_share) * bsdf_pdf;
        Secondary sec = trace_secondary(mask, origin, dir, rng);
        Reservoir c;
        c.M = 1.0;
        if (sec.hit) {
            c.kind = KIND_SURFACE;
            c.y = sec.at;
            c.ny = sec.n;
            c.bounce = sec.bounce;
        } else {
            c.kind = KIND_SKY;
            c.y = dir;
            c.ny = -dir;
            c.bounce = good3(sky_radiance(dir));
        }
        float t = target_of(s, eye, dir, c.bounce);
        if (t > 0.0 && pdf > 0.0) {
            c.W = good(1.0 / pdf);
            r = c;
            chosen = t;
        }
    }

    // 2. the reservoir this point had last frame, its sample reconnected here - (round PT4d) a neighbour's in its 4 x 4
    // block by the frame's permutation (vulkan_light_list.h restir_denoiser_glsl)
    float kept_target = chosen;
    if ((pass.counts.w & FLAG_TEMPORAL) != 0u) {
        vec4 q = pass.prev_to_pixel * vec4(s.at, 1.0);
        if (q.w > 0.0) {
            ivec2 before = ivec2(floor(q.xy / q.w)) - pass.rect.xy;
            if ((pass.counts.w & FLAG_PERMUTE) != 0u) {
                before = restir_permuted(before, pass.counts.z);
            }
            if (all(greaterThanEqual(before, ivec2(0))) && all(lessThan(before, pass.rect.zw))) {
                uint j = uint(before.y * pass.rect.z + before.x);
                Surface then = surface_of(before_at[j], before_more[j]);
                if (alike(s, then)) {
                    Reservoir h = load_b(j);
                    h.M = min(h.M, cap);
                    vec3 h_dir;
                    float h_reach;
                    float g_here;
                    float t_h = h.kind != KIND_NONE && h.W > 0.0 ? target_at(s, eye, h, h_dir, h_reach, g_here) : 0.0;
                    vec3 then_dir;
                    float then_reach;
                    float g_then;
                    float jacobian = t_h > 0.0 && sample_from(then, h, then_dir, then_reach, g_then) && g_then > 0.0
                                         ? good(g_here / g_then) : 0.0;
                    float w_r = chosen * r.W * r.M;
                    float w_h = good(t_h * h.W * jacobian * h.M);
                    float total = w_r + w_h;
                    Reservoir out_r = no_reservoir(r.M + h.M);
                    if (total > 0.0) {
                        float z = 0.0;
                        float target = 0.0;
                        if (path_next(rng) * total < w_h) {
                            // its light, seen from here: then this pixel's sample could have been it too
                            if (seen_at_all(seen(s, h_dir, h_reach))) {
                                out_r = h;
                                target = t_h;
                                z = h.M + r.M;
                            }
                        } else {
                            out_r = r;
                            target = chosen;
                            z = r.M;
                            // could last frame's reservoir have made it: its surface's target and a ray from there
                            vec3 t_dir;
                            float t_reach;
                            float t_g;
                            if (target_at(then, pass.prev_eye.xyz, r, t_dir, t_reach, t_g) > 0.0 &&
                                seen_at_all(seen(then, t_dir, t_reach))) {
                                z += h.M;
                            }
                        }
                        out_r.M = r.M + h.M;
                        out_r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
                        kept_target = target;
                    }
                    r = out_r;
                }
            }
        }
    }
    weight = r.kind != KIND_NONE && r.W > 0.0 ? good(kept_target * r.W) : 0.0;

    // 3. a mirror-like surface's specular: one ray along a visible normal of its lobe
    if (!glossy(s)) {
        vec3 e = brdf_specular_albedo(s.f0, nv, s.rough);
        vec3 h = path_to_world(s.n, path_ggx_visible_normal(path_to_local(s.n, v), alpha, path_next(rng), path_next(rng)));
        vec3 l = 2.0 * dot(v, h) * h - v;
        if (max(e.r, max(e.g, e.b)) > 0.0 && dot(l, s.ng) > 0.0 && dot(l, s.n) > 0.0) {
            float pdf = path_ggx_reflection_pdf(nv, clamp(dot(s.n, h), 0.0, 1.0), alpha);
            vec3 lobe_weight = pdf > 0.0 ? good3(specular_lobe(s, v, l) / pdf) : vec3(0.0);
            Secondary sec = trace_secondary(mask, origin, l, rng);
            vec3 through = seen_through(mask, origin, l, sec.hit ? sec.t : GI_REACH);
            vec3 reflected = sec.hit ? sec.bounce : sky_radiance(l);
            specular_rays[i * 2u] = vec4(good3(lobe_weight * reflected * through / max(e, vec3(1.0e-4))),
                                         sec.hit ? sec.t : GI_MISS_DISTANCE);
            specular_rays[i * 2u + 1u] = vec4(good3(lobe_weight * reflected * through), 0.0);
        }
    }
    return r;
}
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    bool inside = all(lessThan(local, pass.rect.zw));
    uint i = uint(local.y * pass.rect.z + local.x);
    float weight = 0.0;
    Reservoir r = no_reservoir(0.0);
    if (inside) {
        r = temporal_reservoir(local, i, weight);
    }
    // (round PT4d) the boiling filter - every invocation of the group takes part (no early return above)
    if ((pass.counts.w & FLAG_BOILING) != 0u && restir_boiling(weight)) {
        r = no_reservoir(0.0);
    }
    if (inside) {
        store_reservoir(false, i, r);
    }
}
#endif

#ifdef PASS_SPATIAL
void put_result(ivec2 p, uint i, Surface s, vec3 diffuse, vec3 specular, float hit, vec3 full)
{
#ifdef CHECK
    if ((pass.counts.w & FLAG_ACCUMULATE) != 0u) {
        sums[i] += vec4(full, 1.0);
        sums[uint(pass.rect.z * pass.rect.w) + i] = vec4(full, 1.0);
    }
#else
    if (s.valid) {
        // (the store's environment gone where this lit the surface: its sky and bounce are these samples)
        vec4 vis = imageLoad(io_visibility, p);
        imageStore(io_visibility, p, vec4(vis.r, vis.g, 0.0, vis.a));
        vec3 exposed = min(diffuse * pass.eye.w, vec3(65504.0));
        imageStore(io_lamps, p, vec4(min(imageLoad(io_lamps, p).rgb + exposed, vec3(65504.0)), 0.0));
        imageStore(io_lamps_on, p, vec4(min(imageLoad(io_lamps_on, p).rgb + exposed * s.albedo, vec3(65504.0)), 0.0));
        // (round BR: added to the lights' own specular - ReSTIR DI's, written before)
        vec3 specular_in = min(imageLoad(io_specular, p).rgb + specular * pass.eye.w, vec3(65504.0));
        imageStore(io_specular, p, vec4(specular_in, 1.0));
    }
    imageStore(o_specular_hit, p, vec4(hit, 0.0, 0.0, 0.0));
#endif
}
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    ivec2 p = pass.rect.xy + local;
    uint i = uint(local.y * pass.rect.z + local.x);
    Surface s = surface_of(now_at[i], now_more[i]);
    float cap = float(pass.params.z);
    if (!s.valid) {
        store_reservoir(true, i, no_reservoir(0.0));
        put_result(p, i, s, vec3(0.0), vec3(0.0), 0.0, vec3(0.0));
        return;
    }
    vec3 eye = pass.eye.xyz;
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z ^ 0x9E3779B9u, pass.params.w + 1u);
    // the pixel's reservoir and its neighbours' that are the same surface
    Reservoir taken[MOST_SPATIAL + 1u];
    Surface where[MOST_SPATIAL + 1u];
    uint n = 1u;
    taken[0] = load_a(i);
    where[0] = s;
    if ((pass.counts.w & FLAG_SPATIAL) != 0u) {
        for (uint k = 0u; k < min(pass.params.y, MOST_SPATIAL); ++k) {
            float angle = 2.0 * PATH_PI * path_next(rng);
            float radius = pass.tune.x * sqrt(path_next(rng));
            ivec2 q = local + ivec2(round(vec2(cos(angle), sin(angle)) * radius));
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, pass.rect.zw)) || q == local) {
                continue;
            }
            uint j = uint(q.y * pass.rect.z + q.x);
            Surface other = surface_of(now_at[j], now_more[j]);
            if (!alike(s, other)) {
                continue;
            }
            taken[n] = load_a(j);
            taken[n].M = min(taken[n].M, cap);
            where[n] = other;
            ++n;
        }
    }
    // resampled toward this pixel's target, each reconnected here (its weight x the Jacobian)
    float total = 0.0;
    float m_sum = 0.0;
    uint chosen = 0xFFFFFFFFu;
    float target = 0.0;
    vec3 chosen_dir = vec3(0.0, 1.0, 0.0);
    float chosen_reach = 0.0;
    for (uint k = 0u; k < n; ++k) {
        m_sum += taken[k].M;
        if (taken[k].kind == KIND_NONE || !(taken[k].W > 0.0)) {
            continue;
        }
        vec3 dir;
        float reach;
        float g_here;
        float t = target_at(s, eye, taken[k], dir, reach, g_here);
        if (!(t > 0.0)) {
            continue;
        }
        float jacobian = 1.0;
        if (k > 0u) {
            vec3 d_there;
            float r_there;
            float g_there;
            jacobian = sample_from(where[k], taken[k], d_there, r_there, g_there) && g_there > 0.0 ? good(g_here / g_there) : 0.0;
        }
        float w = good(t * taken[k].W * jacobian * taken[k].M);
        total += w;
        if (w > 0.0 && path_next(rng) * total < w) {
            chosen = k;
            target = t;
            chosen_dir = dir;
            chosen_reach = reach;
        }
    }
    Reservoir r = no_reservoir(min(m_sum, cap));
    vec3 diffuse = vec3(0.0);
    vec3 specular = specular_rays[i * 2u].rgb;
    float hit = specular_rays[i * 2u].a;
    vec3 full = vec3(0.0);
    BrdfMaterial m = material_of(s);
    vec3 v = view_of(s, eye);
    float nv = max(dot(s.n, v), 1.0e-4);
    vec3 e = brdf_specular_albedo(s.f0, nv, s.rough);
    if (chosen != 0xFFFFFFFFu) {
        vec3 through = seen(s, chosen_dir, chosen_reach);
        if (seen_at_all(through)) {
            // Z: the samples of every reservoir that could have made this one - its own surface's target and a ray
            float z = 0.0;
            for (uint k = 0u; k < n; ++k) {
                if (k == chosen || k == 0u) {
                    z += taken[k].M; // (its own: seen there; this pixel's: seen just now)
                    continue;
                }
                vec3 o_dir;
                float o_reach;
                float o_g;
                if (target_at(where[k], eye, taken[chosen], o_dir, o_reach, o_g) > 0.0 &&
                    seen_at_all(seen(where[k], o_dir, o_reach))) {
                    z += taken[k].M;
                }
            }
            r = taken[chosen];
            r.M = min(m_sum, cap);
            r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
            if (r.W > 0.0) {
                vec3 light = r.bounce * through * r.W;
                diffuse = light * white_lobe(s, chosen_dir);
                float c = max(dot(s.n, chosen_dir), 0.0);
                if (glossy(s)) {
                    vec3 lobe = specular_lobe(s, v, chosen_dir);
                    specular = r.bounce * through * r.W * lobe / max(e, vec3(1.0e-4));
                    hit = r.kind == KIND_SKY ? GI_MISS_DISTANCE : length(r.y - s.at);
#ifdef CHECK
                    full = path_bsdf_cosine(m, s.n, v, chosen_dir) * light;
#endif
                } else {
#ifdef CHECK
                    full = m.albedo * (1.0 - e) * (c / PATH_PI) * light;
#endif
                }
            }
        }
    }
#ifdef CHECK
    if (!glossy(s)) {
        full += specular_rays[i * 2u + 1u].rgb;
    }
#endif
    if (any(isnan(diffuse)) || any(isinf(diffuse)) || any(isnan(specular)) || any(isinf(specular)) || any(isnan(full)) ||
        any(isinf(full))) {
        atomicAdd(bad_samples, 1u);
        diffuse = vec3(0.0);
        specular = vec3(0.0);
        full = vec3(0.0);
        r.W = 0.0;
    }
    // (the next frame's history is this pixel's temporal reservoir, not the spatial one: fed back, the neighbours' samples
    // come back into the neighbours' own spatial reuse through their histories - inputs no longer independent, and the
    // unbiased combination's weights then aren't (the furnace: -1.5 % with a glossy target); each chain apart, none)
    store_reservoir(true, i, taken[0]);
    put_result(p, i, s, diffuse, specular, hit, full);
}
#endif
)";

enum class Shader : uint8_t { kSky, kTemporal, kSpatial };

std::string shader_source(Shader which, bool check, bool cloud_map, SkyGridSource sky)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    if (check) {
        source += "#define CHECK\n";
    }
    if (which == Shader::kSky) {
        source += sky_grid_prelude(sky, kBindingTransmittance, kBindingSkyView, kBindingCloudNoise);
    }
    source += which == Shader::kTemporal ? "#define PASS_TEMPORAL\n" : (which == Shader::kSpatial ? "#define PASS_SPATIAL\n" : "");
    source += "layout(set = 0, binding = 0) uniform accelerationStructureEXT u_scene;\n";
    source += "#define THUG2_MATERIAL_SET 1\n";
    source += material_glsl();
    source += "#define THUG2_GEOMETRY_SET 0\n#define THUG2_GEOMETRY_BINDING 1\n";
    source += scene_geometry_glsl();
    source += cutout_rays_glsl();
    source += rt::kDisplayTransferGlsl;
    source += rt::brdf::brdf_glsl();
    source += hit_surface_glsl();
    source += rt::path::kPathGlsl;
    if (which == Shader::kTemporal) {
        source += restir_denoiser_glsl();
    }
    source += rt_lamp_glsl();
    if (which == Shader::kTemporal && cloud_map) {
        source += "#define CLOUD_MAP\n" + cloud_map_glsl(kBindingCloudMap);
    }
    source += "const uint SKY_WIDTH = " + std::to_string(rt::lights::kSkyGridWidth) + "u;\n";
    source += "const uint SKY_HEIGHT = " + std::to_string(rt::lights::kSkyGridHeight) + "u;\n";
    source += kCommon;
    source += light_instances_glsl();
    source += rt::lights::kLightListGlsl;
    source += which == Shader::kSky ? std::string(sky_grid_glsl()) : std::string(kGiBody);
    return source;
}

// --- the matrices (ReSTIR DI's) ------------------------------------------------------------------------------------------

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(out, row), column) = sum;
        }
    }
    return out;
}

bool invert(Matrix m, Matrix* inverse)
{
    Matrix out{};
    for (std::size_t i = 0; i < kMatrixSide; ++i) {
        core::at(core::at(out, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kMatrixSide; ++row) {
            if (std::abs(core::at(core::at(m, row), column)) > std::abs(core::at(core::at(m, pivot), column))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(m, pivot), column)) < kSingular) {
            return false;
        }
        std::swap(core::at(m, pivot), core::at(m, column));
        std::swap(core::at(out, pivot), core::at(out, column));
        const double scale = 1.0 / core::at(core::at(m, column), column);
        for (std::size_t k = 0; k < kMatrixSide; ++k) {
            core::at(core::at(m, column), k) *= scale;
            core::at(core::at(out, column), k) *= scale;
        }
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(m, row), column);
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                core::at(core::at(m, row), k) -= factor * core::at(core::at(m, column), k);
                core::at(core::at(out, row), k) -= factor * core::at(core::at(out, column), k);
            }
        }
    }
    *inverse = out;
    return true;
}

bool pixel_to_world(const rt::ViewDrawn& view, Matrix* out)
{
    Matrix view_m{};
    Matrix projection{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(view_m, row), column) = core::at(core::at(view.camera.view, row), column);
            core::at(core::at(projection, row), column) = core::at(core::at(view.camera.projection, row), column);
        }
    }
    const Matrix row_vectors = multiply(view_m, projection);
    Matrix clip_from_world{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(clip_from_world, row), column) = core::at(core::at(row_vectors, column), row);
        }
    }
    Matrix world_from_clip{};
    if (!invert(clip_from_world, &world_from_clip)) {
        return false;
    }
    const double x = core::at(view.rect, 0);
    const double y = core::at(view.rect, 1);
    const double width = std::max(1U, core::at(view.rect, 2));
    const double height = std::max(1U, core::at(view.rect, 3));
    const double near_z = core::at(view.depth_range, 0);
    const double depth_span = std::max(1e-6, static_cast<double>(core::at(view.depth_range, 1)) - near_z);
    const Matrix clip_from_pixel{{{2.0 / width, 0.0, 0.0, (-2.0 * x / width) - 1.0},
                                  {0.0, -2.0 / height, 0.0, (2.0 * y / height) + 1.0},
                                  {0.0, 0.0, 1.0 / depth_span, -near_z / depth_span},
                                  {0.0, 0.0, 0.0, 1.0}}};
    *out = multiply(world_from_clip, clip_from_pixel);
    return true;
}

std::array<float, 16> column_major(const Matrix& m)
{
    std::array<float, 16> out{};
    for (std::size_t column = 0; column < kMatrixSide; ++column) {
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            core::at(out, (column * kMatrixSide) + row) = static_cast<float>(core::at(core::at(m, row), column));
        }
    }
    return out;
}

// --- the state ---------------------------------------------------------------------------------------------------------

struct Timing {
    VkQueryPool pool = VK_NULL_HANDLE;
    bool unavailable = false;
    double period_ns = 1.0;
    std::array<uint64_t, kTimingSlots> pending{};
    double sum_ms = 0.0;
    uint32_t measured = 0;
};

struct GiState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    std::array<VkPipeline, kSkyGridSources> sky{};
    std::array<VkPipeline, 4> temporal{}; // [check x 2 + cloud map]
    std::array<VkPipeline, 2> spatial{};  // [check]
    bool failed = false;
    // the sky's grid: the one in use (its alias tables built from it) and the one being made, read back without a wait
    std::array<DeviceBuffer, 2> sky_cells{};
    int sky_now = -1;
    DeviceBuffer sky_read;
    DeviceBuffer sky_alias;
    uint64_t sky_key = 0;      // the lighting the grid in use was made for
    uint64_t sky_made_key = 0; // ... and the one in flight's
    uint64_t sky_serial = 0;   // its submission (0: none in flight)
    int sky_into = 0;
    double sky_integral = 0.0;
    uint64_t sky_grids = 0;
    // the full list's alias table (made again when its lights' power changes)
    DeviceBuffer lights;
    uint64_t table_key = 0;
    uint32_t entries = 0;
    // per pixel
    uint32_t width = 0;
    uint32_t height = 0;
    DeviceBuffer reservoirs_a;
    DeviceBuffer reservoirs_b;
    std::array<DeviceBuffer, 2> at{};
    std::array<DeviceBuffer, 2> more{};
    DeviceBuffer specular_rays;
    DeviceBuffer sums;
    DeviceBuffer counters;
    Image specular_hit;
    uint64_t frames = 0;
    bool history = false;
    bool history_check = false;
    std::array<float, 16> prev_to_pixel{};
    std::array<float, 3> prev_eye{};
    bool accumulate = true;
    bool restart = false;
    // the view
    bool lit_view = false;
    uint64_t hit_frame = 0; // (the frame whose view wrote specular_hit)
    uint64_t passes = 0;
    Timing timing;
    double last_ms = 0.0;
    uint32_t timed_width = 0;
    uint32_t timed_height = 0;
    RestirGiOverrides overrides;
};

GiState& gi_state()
{
    static GiState made;
    return made;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class GiListener final : public rt::ViewListener {
public:
    // (after the lamps' light is in the inputs - ReSTIR DI's or the lamps pass's -, before the decals and the shading)
    [[nodiscard]] int order() const override { return rt::ViewOrder::kLights + 1; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_restir_gi_pass(state, view);
        }
    }
};

GiListener& listener()
{
    static GiListener instance;
    return instance;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
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

VkDescriptorType binding_type(uint32_t binding)
{
    if (binding == kBindingScene) {
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    if (binding == kBindingBlock) {
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    if (binding >= kBindingDepth && binding <= kBindingSpecularHit) {
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    if (binding == kBindingTransmittance || binding == kBindingSkyView || binding == kBindingCloudMap ||
        (binding >= kBindingCloudNoise && binding < kBindingCloudNoise + kCloudNoiseTextures)) {
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}

bool ensure_layout(Backend& state, std::string* error)
{
    GiState& own = gi_state();
    if (own.set_layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = binding_type(binding),
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        own.set_layout = VK_NULL_HANDLE;
        *error = "vkCreateDescriptorSetLayout failed";
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
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "vkCreatePipelineLayout failed";
        return false;
    }
    return true;
}

bool ensure_pipelines(Backend& state, bool check, bool cloud_map, SkyGridSource sky, std::string* error)
{
    GiState& own = gi_state();
    if (!ensure_layout(state, error)) {
        return false;
    }
    const VkDevice device = state.context.device();
    VkPipeline& sky_pipeline = core::at(own.sky, static_cast<std::size_t>(sky));
    if (sky_pipeline == VK_NULL_HANDLE) {
        sky_pipeline = make_compute(device, own.layout, shader_source(Shader::kSky, false, false, sky), error);
        if (sky_pipeline == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& temporal = core::at(own.temporal, (check ? 2U : 0U) + (cloud_map ? 1U : 0U));
    if (temporal == VK_NULL_HANDLE) {
        temporal = make_compute(device, own.layout, shader_source(Shader::kTemporal, check, cloud_map, sky), error);
        if (temporal == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& spatial = core::at(own.spatial, check ? 1U : 0U);
    if (spatial == VK_NULL_HANDLE) {
        spatial = make_compute(device, own.layout, shader_source(Shader::kSpatial, check, false, sky), error);
    }
    return spatial != VK_NULL_HANDLE;
}

bool ensure_buffer(Backend& state, DeviceBuffer* buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, BufferPlace place)
{
    if (buffer->buffer != VK_NULL_HANDLE && buffer->bytes >= bytes) {
        return true;
    }
    retire_buffer(state, buffer);
    return state.memory.create_buffer(bytes, usage, place, buffer);
}

bool upload_buffer(Backend& state, DeviceBuffer* buffer, const void* data, std::size_t bytes)
{
    retire_buffer(state, buffer);
    if (!state.memory.create_buffer(std::max<VkDeviceSize>(bytes, 16), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    BufferPlace::kUpload, buffer) ||
        buffer->mapped == nullptr) {
        return false;
    }
    if (bytes > 0) {
        std::memcpy(buffer->mapped, data, bytes);
    }
    state.memory.flush(*buffer, 0, VK_WHOLE_SIZE);
    return true;
}

void barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags from_access, VkPipelineStageFlags to,
             VkAccessFlags to_access)
{
    const VkMemoryBarrier memory{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = from_access, .dstAccessMask = to_access};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}

constexpr uint64_t kFnvBasis = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
uint64_t mix(uint64_t key, uint64_t value) { return (key ^ value) * kFnvPrime; }
uint64_t mix_floats(uint64_t key, std::span<const float> values)
{
    for (const float value : values) {
        key = mix(key, std::bit_cast<uint32_t>(value));
    }
    return key;
}

// What the sky's grid is made from: the lighting's sky (even, or the table and its factor, the clouds drawn or not), the
// direct light it is lit by. (The clouds' drift is not in it: the grid made when the lighting changes holds the clouds as
// they were then - sampled and seen alike until the next.)
uint64_t sky_key_of(const SceneLighting& lighting)
{
    uint64_t key = mix(kFnvBasis, lighting.cut);
    key = mix_floats(key, lighting.sun_toward);
    key = mix_floats(key, lighting.sun_light);
    key = mix_floats(key, lighting.sky);
    key = mix(key, lighting.table ? 1U : 0U);
    key = mix(key, lighting.clouds ? 1U : 0U);
    key = mix(key, std::bit_cast<uint32_t>(lighting.table_factor));
    return mix_floats(key, lighting.table_input.sun);
}

std::size_t sky_cell_count()
{
    return static_cast<std::size_t>(rt::lights::kSkyGridWidth) * rt::lights::kSkyGridHeight;
}

// A finished grid taken into use (its submission has run): its alias tables built and uploaded.
bool adopt_sky(Backend& state, GiState& own)
{
    vmaInvalidateAllocation(state.memory.allocator(), own.sky_read.allocation, 0, VK_WHOLE_SIZE);
    const auto* floats = static_cast<const float*>(own.sky_read.mapped);
    own.sky_serial = 0;
    if (floats == nullptr) {
        return false;
    }
    const std::size_t cells = sky_cell_count();
    std::vector<std::array<float, 4>> radiance(cells);
    std::memcpy(radiance.data(), floats, cells * sizeof(std::array<float, 4>));
    const rt::lights::SkyDistribution sky =
        rt::lights::build_sky_distribution(radiance, rt::lights::kSkyGridWidth, rt::lights::kSkyGridHeight);
    const std::vector<std::array<uint32_t, 2>> alias = sky_alias_words(sky);
    if (!upload_buffer(state, &own.sky_alias, alias.data(), alias.size() * sizeof(alias.front()))) {
        return false;
    }
    own.sky_now = own.sky_into;
    own.sky_key = own.sky_made_key;
    own.sky_integral = sky.integral;
    own.table_key = 0; // (the sky's power changed: the list made again)
    ++own.sky_grids;
    return true;
}

// The alias table over the full list (rt_light_list.h: the disc and the sky against the local group, each light by its
// power within its group), made again when its lights' power changes.
bool update_table(Backend& state, GiState& own, const SceneLighting& lighting, const RestirLocalLights& local)
{
    std::array<float, 4> sun{};
    std::array<float, 4> disc{};
    const bool has_disc = disc_of(lighting, &sun, &disc);
    uint64_t key = mix(mix(kFnvBasis, local.key), std::bit_cast<uint64_t>(own.sky_integral));
    key = mix(mix_floats(key, lighting.sun_light), has_disc ? std::bit_cast<uint32_t>(core::at(sun, 3)) : 0U);
    if (key == own.table_key && own.lights.buffer != VK_NULL_HANDLE) {
        return true;
    }
    rt::lights::LightSet set;
    if (has_disc) {
        set.disc_light = lighting.sun_light;
        set.disc_cos = core::at(sun, 3);
    }
    set.sky_integral = own.sky_integral;
    set.triangle_powers = local.triangle_powers;
    set.lamps = local.lamp_list;
    const rt::lights::LightList list = rt::lights::build_light_list(set, rt::lights::LightWeights::kPower);
    if (!upload_buffer(state, &own.lights, list.table.entries.data(),
                       list.table.entries.size() * sizeof(rt::lights::AliasEntry))) {
        return false;
    }
    own.entries = static_cast<uint32_t>(list.table.entries.size());
    own.table_key = key;
    return true;
}

bool ensure_timing(Backend& state, GiState& own)
{
    Timing& timing = own.timing;
    if (timing.pool != VK_NULL_HANDLE || timing.unavailable) {
        return timing.pool != VK_NULL_HANDLE;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimingSlots * 2,
                                     .pipelineStatistics = 0};
    if (properties.limits.timestampComputeAndGraphics != VK_TRUE ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &timing.pool) != VK_SUCCESS) {
        timing.pool = VK_NULL_HANDLE;
        timing.unavailable = true;
        return false;
    }
    timing.period_ns = static_cast<double>(properties.limits.timestampPeriod);
    timing.pending = {};
    return true;
}

void collect_timing(Backend& state, GiState& own, uint32_t slot, bool check)
{
    Timing& timing = own.timing;
    const uint64_t written = core::at(timing.pending, slot);
    core::at(timing.pending, slot) = 0;
    if (written == 0 || state.frames.completed() < written) {
        return;
    }
    std::array<uint64_t, 2> ticks{};
    if (vkGetQueryPoolResults(state.context.device(), timing.pool, slot * 2, 2, sizeof(ticks), ticks.data(),
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS ||
        ticks.back() < ticks.front()) {
        return;
    }
    constexpr double kNsPerMs = 1e6;
    timing.sum_ms += static_cast<double>(ticks.back() - ticks.front()) * timing.period_ns / kNsPerMs;
    if (++timing.measured >= (check ? kCheckTimingReport : kTimingReport)) {
        own.last_ms = timing.sum_ms / timing.measured;
        if (!check) {
            get_log("restir gi: %.3f ms a frame on the device over the last %u (%ux%u; %u lights listed)", own.last_ms,
                    timing.measured, own.timed_width, own.timed_height, own.entries);
        }
        timing.sum_ms = 0.0;
        timing.measured = 0;
    }
}

// The check's lighting as the lights see it.
SceneLighting check_lighting(const RestirGiOverrides& overrides)
{
    SceneLighting out;
    out.sun_toward = overrides.sun_toward;
    out.sun_light = overrides.sun_light;
    out.sun_radius_degrees = overrides.sun_radius_degrees;
    out.sky = overrides.sky;
    out.lamps = rt::path::analytic_lamps(overrides.lamps, overrides.lamps_surfaced);
    return out;
}

// The sky's grid recorded into the buffer not in use (its source's shader), copied out for the read-back.
bool record_sky(Backend& state, GiState& own, VkCommandBuffer commands, const SceneLighting& lighting, uint64_t key)
{
    const SkyGridSource source = sky_grid_source(lighting);
    const int into = own.sky_now == 0 ? 1 : 0;
    DeviceBuffer& cells = core::at(own.sky_cells, static_cast<std::size_t>(into));
    const VkDeviceSize bytes = sky_cell_count() * 4 * sizeof(float);
    const VkBufferUsageFlags storage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!ensure_buffer(state, &cells, bytes, storage, BufferPlace::kDevice) ||
        !ensure_buffer(state, &own.sky_read, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack)) {
        return false;
    }
    GiBlock block;
    const SkyGridInputs inputs = sky_grid_inputs(lighting);
    block.sky = inputs.sky;
    block.sky_table = inputs.sky_table;
    block.cloud_ray = inputs.cloud_ray;
    block.cloud_sun = inputs.cloud_sun;
    block.cloud_toward = inputs.cloud_toward;
    std::array<uint8_t, sizeof(GiBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!span) {
        return false;
    }
    const VkDescriptorBufferInfo block_info{.buffer = span->buffer, .offset = span->offset, .range = sizeof(GiBlock)};
    const VkDescriptorBufferInfo cells_info{.buffer = cells.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorImageInfo transmittance_info = atmosphere_descriptor(AtmosphereTable::kTransmittance);
    const VkDescriptorImageInfo sky_view_info = atmosphere_descriptor(AtmosphereTable::kSkyView);
    std::array<VkDescriptorImageInfo, kCloudNoiseTextures> noise_info{};
    for (uint32_t i = 0; i < kCloudNoiseTextures; ++i) {
        core::at(noise_info, i) = cloud_noise_descriptor(i);
    }
    std::vector<VkWriteDescriptorSet> writes;
    const auto write = [&writes](uint32_t binding, const VkDescriptorBufferInfo* buffer, const VkDescriptorImageInfo* image) {
        VkWriteDescriptorSet one{};
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = binding_type(binding);
        one.pBufferInfo = buffer;
        one.pImageInfo = image;
        writes.push_back(one);
    };
    write(kBindingBlock, &block_info, nullptr);
    write(kBindingSkyCells, &cells_info, nullptr);
    const uint64_t serial = state.frames.serial();
    if (source != SkyGridSource::kEven) {
        write(kBindingTransmittance, nullptr, &transmittance_info);
        write(kBindingSkyView, nullptr, &sky_view_info);
        atmosphere_used(serial);
    }
    if (source == SkyGridSource::kTableClouds) {
        for (uint32_t i = 0; i < kCloudNoiseTextures; ++i) {
            write(kBindingCloudNoise + i, nullptr, &core::at(noise_info, i));
        }
        cloud_noise_used(serial);
    }
    barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.sky, static_cast<std::size_t>(source)));
    vkCmdDispatch(commands, (rt::lights::kSkyGridWidth + kGroupSide - 1) / kGroupSide,
                  (rt::lights::kSkyGridHeight + kGroupSide - 1) / kGroupSide, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = bytes};
    vkCmdCopyBuffer(commands, cells.buffer, own.sky_read.buffer, 1, &region);
    barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_HOST_READ_BIT);
    own.sky_made_key = key;
    own.sky_serial = serial;
    own.sky_into = into;
    return true;
}

bool ensure_image(Backend& state, Image* image, VkFormat format, const Extent& extent, VkImageUsageFlags usage)
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
    image->resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, image);
    return true;
}

} // namespace

bool restir_gi_wanted(Backend& state)
{
    const RtGiSettings settings = rt_gi_settings_locked();
    const bool restir = settings.indirect == RtIndirectLight::kRestir ||
                        (settings.indirect == RtIndirectLight::kAuto && ray_reconstruction_active());
    return restir && !gi_state().failed && state.context.ray_tracing_enabled() && surface_inputs(state) != nullptr;
}

bool restir_gi_lit_view(const Backend& /*state*/) { return gi_state().lit_view; }

Image* restir_gi_specular_hit(Backend& state)
{
    GiState& own = gi_state();
    return own.specular_hit.image != VK_NULL_HANDLE && own.hit_frame == state.frames.serial() ? &own.specular_hit
                                                                                               : nullptr;
}

void set_restir_gi_overrides(const RestirGiOverrides& overrides)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().overrides = overrides;
}

void forget_restir_gi_history()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().history = false;
}

void set_restir_gi_accumulate(bool on)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().accumulate = on;
}

void restart_restir_gi_accumulation()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().restart = true;
}

bool read_restir_gi_accumulation(Backend& state, RestirGiAccumulation* out)
{
    *out = RestirGiAccumulation{};
    GiState& own = gi_state();
    if (own.sums.buffer == VK_NULL_HANDLE || own.width == 0 || own.height == 0 || !finish_work(state)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(own.width) * own.height;
    std::vector<uint8_t> bytes;
    if (!state.memory.read_buffer(own.sums, 0, pixels * 2 * 4 * sizeof(float), &bytes) ||
        bytes.size() < pixels * 2 * 4 * sizeof(float)) {
        return false;
    }
    out->width = own.width;
    out->height = own.height;
    out->mean.resize(pixels);
    out->last.resize(pixels);
    float frames = 0.0F;
    for (std::size_t i = 0; i < pixels; ++i) {
        std::array<float, 4> sum{};
        std::array<float, 4> last{};
        std::memcpy(sum.data(), &core::at(bytes, i * sizeof(sum)), sizeof(sum));
        std::memcpy(last.data(), &core::at(bytes, (pixels + i) * sizeof(last)), sizeof(last));
        const float count = std::max(core::at(sum, 3), 1.0F);
        frames = std::max(frames, core::at(sum, 3));
        core::at(out->mean, i) = {core::at(sum, 0) / count, core::at(sum, 1) / count, core::at(sum, 2) / count};
        core::at(out->last, i) = {core::at(last, 0), core::at(last, 1), core::at(last, 2)};
    }
    out->frames = static_cast<uint32_t>(frames);
    std::vector<uint8_t> counters;
    if (state.memory.read_buffer(own.counters, 0, sizeof(uint32_t), &counters) && counters.size() >= sizeof(uint32_t)) {
        std::memcpy(&out->not_finite, counters.data(), sizeof(uint32_t));
    }
    return true;
}

RestirGiStats restir_gi_stats()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    const GiState& own = gi_state();
    return RestirGiStats{.passes = own.passes,
                         .lights = own.entries,
                         .sky_ready = own.sky_now >= 0,
                         .sky_grids = own.sky_grids,
                         .gpu_ms = own.last_ms,
                         .gpu_width = own.timed_width,
                         .gpu_height = own.timed_height};
}

bool record_restir_gi_pass(Backend& state, const rt::ViewDrawn& view)
{
    GiState& own = gi_state();
    own.lit_view = false;
    const bool check = own.overrides.on;
    if (own.failed || (!check && !restir_gi_wanted(state)) || !state.context.ray_tracing_enabled() ||
        rt::ray_tracing() == nullptr) {
        return false;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE || scene.instances_address == 0 ||
        !state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE) {
        return false;
    }
    // the lighting: the game's (lamps kept in their places while they switch - ReSTIR DI's list), or the check's
    const SceneLighting lighting = check ? check_lighting(own.overrides) : scene_lighting(false, true);
    CloudShadowMap cloud_map;
    const bool map_now = !check && cloud_shadow_map(state, &cloud_map);
    const SkyGridSource sky_source = sky_grid_source(lighting);
    std::string error;
    if (!ensure_pipelines(state, check, map_now, sky_source, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("restir gi: the passes couldn't be made (%s) - the light store's environment and the fallback passes "
                    "light the view",
                    error.c_str());
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    // the local lights (ReSTIR DI's scan and lamps)
    RestirLocalLights local;
    if (!restir_local_lights(state, commands, lighting, &local)) {
        return false;
    }
    // the sky's grid: a finished one taken into use; a new one when the lighting changed (one at a time)
    const uint64_t sky_key = sky_key_of(lighting);
    if (own.sky_serial != 0 && state.frames.completed() >= own.sky_serial) {
        if (!adopt_sky(state, own)) {
            if (first_report(state, kReportBuffers)) {
                get_log("restir gi: the sky's grid couldn't be read back");
            }
        }
    }
    if (own.sky_serial == 0 && own.sky_key != sky_key && own.sky_made_key != sky_key) {
        if (!record_sky(state, own, commands, lighting, sky_key)) {
            if (first_report(state, kReportBuffers)) {
                get_log("restir gi: the sky's grid couldn't be made");
            }
            return false;
        }
    } else if (own.sky_serial == 0 && own.sky_key != sky_key && own.sky_made_key == sky_key && own.sky_now >= 0 &&
               own.sky_now == own.sky_into) {
        own.sky_made_key = 0; // (adopted but changed again since: made again next)
    }
    if (own.sky_now < 0) {
        return false; // (the first grid not in yet: the store's environment lights the view for these frames)
    }
    if (!update_table(state, own, lighting, local)) {
        return false;
    }

    // the view's rectangle and its images
    const SurfaceInputs* const inputs = check ? nullptr : surface_inputs(state);
    if (!check && (inputs == nullptr || inputs->depth == nullptr || inputs->lamps == nullptr || inputs->lamps_on == nullptr ||
                   inputs->visibility == nullptr || inputs->specular == nullptr)) {
        return false;
    }
    std::array<uint32_t, 4> rect = view.rect;
    Extent extent{.width = core::at(rect, 0) + core::at(rect, 2), .height = core::at(rect, 1) + core::at(rect, 3)};
    if (inputs != nullptr) {
        extent = Extent{.width = inputs->lamps->extent.width, .height = inputs->lamps->extent.height};
        core::at(rect, 0) = std::min(core::at(rect, 0), extent.width);
        core::at(rect, 1) = std::min(core::at(rect, 1), extent.height);
        core::at(rect, 2) = std::min(core::at(rect, 2), extent.width - core::at(rect, 0));
        core::at(rect, 3) = std::min(core::at(rect, 3), extent.height - core::at(rect, 1));
    }
    const uint32_t width = core::at(rect, 2);
    const uint32_t height = core::at(rect, 3);
    Matrix to_world{};
    if (width == 0 || height == 0 || !pixel_to_world(view, &to_world)) {
        return false;
    }
    Matrix to_pixel{};
    const bool invertible = invert(to_world, &to_pixel);
    // the per-pixel buffers (a new size: no history)
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (own.width != width || own.height != height) {
        own.history = false;
        own.width = width;
        own.height = height;
        own.restart = true;
    }
    bool made = ensure_buffer(state, &own.reservoirs_a, pixels * 16 * kReservoirWords, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.reservoirs_b, pixels * 16 * kReservoirWords, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.specular_rays, pixels * 16 * kSpecularRayWords, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.counters, 16, storage, BufferPlace::kDevice) &&
                (!check || ensure_buffer(state, &own.sums, pixels * 2 * 16, storage, BufferPlace::kDevice)) &&
                (check || ensure_image(state, &own.specular_hit, VK_FORMAT_R16_SFLOAT, extent,
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
    for (std::size_t k = 0; k < 2 && made; ++k) {
        made = ensure_buffer(state, &core::at(own.at, k), pixels * 16, storage, BufferPlace::kDevice) &&
               ensure_buffer(state, &core::at(own.more, k), pixels * 16, storage, BufferPlace::kDevice);
    }
    if (!made) {
        if (first_report(state, kReportBuffers)) {
            get_log("restir gi: the reservoirs (%ux%u) couldn't be made - the light store's environment lights the view",
                    width, height);
        }
        own.history = false;
        return false;
    }

    // the block
    ++own.frames;
    const std::size_t now = own.frames & 1U;
    const std::size_t before = now ^ 1U;
    const bool history = own.history && invertible && own.history_check == check && (!check || own.overrides.temporal);
    GiBlock block;
    block.to_world = column_major(to_world);
    block.prev_to_pixel = own.prev_to_pixel;
    block.eye = {core::at(view.camera.position, 0), core::at(view.camera.position, 1), core::at(view.camera.position, 2),
                 rt::frame_pre_exposure()};
    block.prev_eye = {core::at(own.prev_eye, 0), core::at(own.prev_eye, 1), core::at(own.prev_eye, 2), 0.0F};
    block.rect = {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
                  static_cast<int32_t>(width), static_cast<int32_t>(height)};
    const uint32_t spatial = check && own.overrides.spatial != 0xFFU ? own.overrides.spatial : kSpatialNeighbours;
    const uint32_t flags = (history ? kFlagTemporal : 0U) | (spatial > 0 ? kFlagSpatial : 0U) |
                           (check && own.accumulate ? kFlagAccumulate : 0U) | (check ? kFlagForced : 0U) |
                           (!check || own.overrides.permutation ? kFlagPermute : 0U) |
                           (!check || own.overrides.boiling_filter ? kFlagBoiling : 0U);
    block.counts = {own.entries, static_cast<uint32_t>(local.lamp_list.size()), static_cast<uint32_t>(own.frames), flags};
    block.params = {0U, std::min(spatial, kMostSpatial), kHistoryCap, check ? own.overrides.seed : 0x6C8E9CF5U};
    block.tune = {kSpatialRadius, kNormalAgreement, kPlaneShare, kSkyShare};
    block.force = {own.overrides.roughness, own.overrides.f0, own.overrides.albedo_scale, check ? 1.0F : 0.0F};
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), scene.instances, 0U};
    (void)disc_of(lighting, &block.sun, &block.sun_radiance);
    block.sky_dist = {own.sky_integral > 0.0 ? static_cast<float>(1.0 / own.sky_integral) : 0.0F, kGlossyReuse, 0.0F, 0.0F};
    if (map_now) {
        block.cloud_place = cloud_map.place;
        block.cloud_light = cloud_map.light;
    }
    std::array<uint8_t, sizeof(GiBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> block_span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!block_span) {
        return false;
    }

    // the passes
    const bool timed = ensure_timing(state, own);
    const auto slot = static_cast<uint32_t>(own.passes % kTimingSlots);
    if (timed) {
        collect_timing(state, own, slot, check);
        vkCmdResetQueryPool(commands, own.timing.pool, slot * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timing.pool, slot * 2);
    }
    if (own.restart) {
        if (check) {
            vkCmdFillBuffer(commands, own.sums.buffer, 0, VK_WHOLE_SIZE, 0);
        }
        vkCmdFillBuffer(commands, own.counters.buffer, 0, VK_WHOLE_SIZE, 0);
        own.restart = false;
    }
    barrier(commands,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_UNIFORM_READ_BIT);
    std::array<Image*, 6> images_used{};
    if (inputs != nullptr) {
        images_used = {inputs->depth, inputs->lamps, inputs->lamps_on, inputs->visibility, inputs->specular, &own.specular_hit};
        for (Image* image : images_used) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, image != &own.specular_hit);
        }
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const auto whole = [](VkBuffer buffer) { return VkDescriptorBufferInfo{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE}; };
    const std::array<VkDescriptorBufferInfo, 18> buffers{
        VkDescriptorBufferInfo{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes},
        VkDescriptorBufferInfo{.buffer = block_span->buffer, .offset = block_span->offset, .range = sizeof(GiBlock)},
        whole(own.lights.buffer),
        whole(local.emitters),
        whole(local.lamps),
        whole(local.membership),
        whole(local.slots),
        whole(core::at(own.sky_cells, static_cast<std::size_t>(own.sky_now)).buffer),
        whole(own.sky_alias.buffer),
        whole(own.reservoirs_a.buffer),
        whole(own.reservoirs_b.buffer),
        whole(core::at(own.at, now).buffer),
        whole(core::at(own.more, now).buffer),
        whole(core::at(own.at, before).buffer),
        whole(core::at(own.more, before).buffer),
        whole(own.specular_rays.buffer),
        whole(check ? own.sums.buffer : own.counters.buffer),
        whole(own.counters.buffer)};
    constexpr std::array<uint32_t, 18> kBufferBindings{
        kBindingGeometry,    kBindingBlock,       kBindingLights,     kBindingEmitters,  kBindingLamps,
        kBindingMembership,  kBindingSlots,       kBindingSkyCells,   kBindingSkyAlias,  kBindingReservoirsA,
        kBindingReservoirsB, kBindingNowAt,       kBindingNowMore,    kBindingBeforeAt,  kBindingBeforeMore,
        kBindingSpecularRays, kBindingSums,       kBindingCounters};
    std::array<VkDescriptorImageInfo, 6> images{};
    if (inputs != nullptr) {
        for (std::size_t k = 0; k < images.size(); ++k) {
            core::at(images, k) = {.sampler = VK_NULL_HANDLE, .imageView = core::at(images_used, k)->view,
                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        }
    }
    std::vector<VkWriteDescriptorSet> writes;
    const auto add = [&writes](uint32_t binding, const VkDescriptorBufferInfo* buffer, const VkDescriptorImageInfo* image,
                               const void* next) {
        VkWriteDescriptorSet one{};
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.pNext = next;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = binding_type(binding);
        one.pBufferInfo = buffer;
        one.pImageInfo = image;
        writes.push_back(one);
    };
    add(kBindingScene, nullptr, nullptr, &scene_info);
    for (std::size_t k = 0; k < buffers.size(); ++k) {
        add(core::at(kBufferBindings, k), &core::at(buffers, k), nullptr, nullptr);
    }
    if (inputs != nullptr) {
        for (std::size_t k = 0; k < images.size(); ++k) {
            add(kBindingDepth + static_cast<uint32_t>(k), nullptr, &core::at(images, k), nullptr);
        }
    }
    if (map_now) {
        add(kBindingCloudMap, nullptr, &cloud_map.image, nullptr);
    }
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    const uint32_t groups_x = (width + kGroupSide - 1) / kGroupSide;
    const uint32_t groups_y = (height + kGroupSide - 1) / kGroupSide;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                      core::at(own.temporal, (check ? 2U : 0U) + (map_now ? 1U : 0U)));
    vkCmdDispatch(commands, groups_x, groups_y, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.spatial, check ? 1U : 0U));
    vkCmdDispatch(commands, groups_x, groups_y, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (slot * 2) + 1);
        core::at(own.timing.pending, slot) = state.frames.serial();
        own.timed_width = width;
        own.timed_height = height;
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    if (inputs != nullptr) {
        for (Image* image : images_used) {
            image->used = serial;
        }
        own.hit_frame = serial;
    }
    own.history = true;
    own.history_check = check;
    if (invertible) {
        own.prev_to_pixel = column_major(to_pixel);
    }
    own.prev_eye = view.camera.position;
    ++own.passes;
    own.lit_view = !check;
    if (own.passes == 1 && first_report(state, kReportFirst)) {
        get_log("restir gi: the indirect light by ReSTIR GI - %ux%u, %u spatial neighbours; %u lights listed (%u lamps), "
                "the sky's grid %s",
                width, height, spatial, own.entries, static_cast<uint32_t>(local.lamp_list.size()),
                sky_source == SkyGridSource::kEven ? "even" : (sky_source == SkyGridSource::kTable ? "the table's" : "the table's with the clouds"));
    }
    return true;
}

void install_restir_gi() { rt::add_view_listener(&listener()); }

void uninstall_restir_gi() { rt::remove_view_listener(&listener()); }

void release_restir_gi(Backend& state)
{
    GiState& own = gi_state();
    const VkDevice device = state.context.device();
    for (VkPipeline& pipeline : own.sky) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.temporal) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.spatial) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    if (own.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, own.layout, nullptr);
    }
    if (own.set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
    }
    if (own.timing.pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, own.timing.pool, nullptr);
    }
    for (DeviceBuffer* buffer : {&core::at(own.sky_cells, 0), &core::at(own.sky_cells, 1), &own.sky_read, &own.sky_alias,
                                 &own.lights, &own.reservoirs_a, &own.reservoirs_b, &core::at(own.at, 0),
                                 &core::at(own.at, 1), &core::at(own.more, 0), &core::at(own.more, 1), &own.specular_rays,
                                 &own.sums, &own.counters}) {
        state.memory.destroy_buffer(buffer);
    }
    state.memory.destroy_image(&own.specular_hit);
    const RestirGiOverrides overrides = own.overrides;
    own = GiState{};
    own.overrides = overrides;
}

} // namespace render::vulkan
