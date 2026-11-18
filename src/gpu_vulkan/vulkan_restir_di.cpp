// NOT ORIGINAL - the local lights' direct light by ReSTIR DI (vulkan_restir_di.h; path tracing plan, stage 4).
#include "gpu_vulkan/vulkan_restir_di.h"

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
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x52450001U;
constexpr uint64_t kReportBuffers = 0x52450002U;
constexpr uint64_t kReportFirst = 0x52450003U;
constexpr uint64_t kReportList = 0x52450004U;

// The bindings of set 0 (pushed); set 1 the material table.
constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingGeometry = 1;
constexpr uint32_t kBindingBlock = 2;
constexpr uint32_t kBindingLights = 3;
constexpr uint32_t kBindingEmitters = 4;
constexpr uint32_t kBindingLamps = 5;
constexpr uint32_t kBindingReservoirsA = 6;
constexpr uint32_t kBindingReservoirsB = 7;
constexpr uint32_t kBindingNowAt = 8;
constexpr uint32_t kBindingNowMore = 9;
constexpr uint32_t kBindingBeforeAt = 10;
constexpr uint32_t kBindingBeforeMore = 11;
constexpr uint32_t kBindingDepth = 12;
constexpr uint32_t kBindingLampsOut = 13;
constexpr uint32_t kBindingLampsOnOut = 14;
constexpr uint32_t kBindingSums = 15;
constexpr uint32_t kBindingMembership = 16;
constexpr uint32_t kBindingSlots = 17;
constexpr uint32_t kBindingCounters = 18;
constexpr uint32_t kBindingCount = 19;
constexpr uint32_t kMaterialSet = 1;

constexpr uint32_t kGroupSide = 8;
constexpr uint32_t kScanGroup = 64;
constexpr uint32_t kMostGroupsX = 65535;
constexpr uint32_t kListCapacity = 65536; // emitting triangles a realtime list holds at most (read back: 1 MB)
constexpr VkDeviceSize kUniformAlignment = 256;
constexpr std::size_t kMatrixSide = 4;
constexpr double kSingular = 1e-300;

// The estimator's settings (fixed - no per-level values): the initial candidates a pixel, the spatial neighbours, the
// reservoirs' sample count kept at most (x the candidates), the neighbours' radius (pixels), the tests a reused surface
// passes - its normal within acos 0.9, its plane within this share of the distance.
constexpr uint32_t kInitialCandidates = 16;
constexpr uint32_t kSpatialNeighbours = 2;
constexpr uint32_t kMostSpatial = 4;
constexpr uint32_t kHistoryCap = 20;
constexpr float kSpatialRadius = 24.0F;
constexpr float kNormalAgreement = 0.9F;
constexpr float kPlaneShare = 0.02F;
// Flags (the block's counts.w)
constexpr uint32_t kFlagTemporal = 1U;
constexpr uint32_t kFlagSpatial = 2U;
constexpr uint32_t kFlagAccumulate = 4U;

constexpr uint32_t kTimingSlots = 16;
constexpr uint32_t kTimingReport = 600;
constexpr uint32_t kCheckTimingReport = 64;

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;

// The block every shader of the passes reads (std140).
struct RestirBlock {
    std::array<float, 16> to_world{};      // (pixel x, pixel y, depth, 1) -> world, column-major
    std::array<float, 16> prev_to_pixel{}; // world -> last frame's (pixel x, pixel y, depth, w), column-major
    std::array<float, 4> eye{};            // xyz the camera; w the frame's pre-exposure (the inputs' unit)
    std::array<float, 4> prev_eye{};
    std::array<int32_t, 4> rect{};         // the view: x, y, width, height (target pixels)
    std::array<uint32_t, 4> counts{};      // x the list's entries, y lamps, z the frame's number, w flags
    std::array<uint32_t, 4> params{};      // x initial candidates, y spatial neighbours, z M's cap, w the seed
    std::array<float, 4> tune{};           // x spatial radius, y least normal agreement, z most plane share, w -
    std::array<float, 4> force{};          // x roughness, y F0, z the albedo's scale (the check's material)
    std::array<uint32_t, 4> instances{};   // xy the instances' device address, z instances, w triangles
    std::array<uint32_t, 4> list{};        // x the scan's capacity
};
static_assert(sizeof(RestirBlock) == (2 * 64) + (9 * 16));

// --- the shaders -------------------------------------------------------------------------------------------------------

// Declared by every shader after the libraries (vulkan_rt_scene.h's tables and hit_surface, rt_path.h, the lamp model).
constexpr const char* kCommon = R"(
layout(std140, set = 0, binding = 2) uniform RestirBlock {
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
    uvec4 list;
} pass;
struct LightEntry {
    float threshold;
    uint alias;
    float pdf;
    uint payload;
};
layout(std430, set = 0, binding = 3) readonly buffer Lights { LightEntry light_entries[]; };
layout(std430, set = 0, binding = 4) buffer Emitters {
    uint emitter_count;
    uint emitter_spare;
    uint emitter_unused;
    uint emitter_pad;
    uvec4 emitter_entries[]; // instance, primitive, power (float bits), -
};
layout(std430, set = 0, binding = 5) readonly buffer Lamps { LightRecord lamps[]; };
layout(std430, set = 0, binding = 16) readonly buffer Membership { uint membership[]; };
layout(std430, set = 0, binding = 17) buffer Slots { uint slots[]; };
const uvec2 sky_alias[1] = uvec2[1](uvec2(0u)); // (the light list's sky isn't sampled here)
#define LIST_INSTANCE_ADDRESS pass.instances.xy
#define LIST_INSTANCES pass.instances.z
#define LIST_TRIANGLES pass.instances.w
#define LIST_CAPACITY pass.list.x
)";

// The two passes (PASS_TEMPORAL: the surface, the initial candidates, the temporal reuse; PASS_SPATIAL: the spatial
// reuse and the result). CHECK: the check's variant - the surface from camera rays, the result accumulated.
constexpr const char* kRestirBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 6) buffer ReservoirsA { uvec4 res_a[]; };
layout(std430, set = 0, binding = 7) buffer ReservoirsB { uvec4 res_b[]; };
layout(std430, set = 0, binding = 8) buffer SurfacesNow { vec4 now_at[]; };
layout(std430, set = 0, binding = 9) buffer SurfacesNowMore { uvec4 now_more[]; };
layout(std430, set = 0, binding = 10) readonly buffer SurfacesBefore { vec4 before_at[]; };
layout(std430, set = 0, binding = 11) readonly buffer SurfacesBeforeMore { uvec4 before_more[]; };
#ifdef CHECK
layout(std430, set = 0, binding = 15) buffer Sums { vec4 sums[]; };
#else
layout(set = 0, binding = 12, r32f) uniform readonly image2D u_depth;
layout(set = 0, binding = 13, rgba16f) uniform writeonly image2D o_lamps;
layout(set = 0, binding = 14, rgba16f) uniform writeonly image2D o_lamps_on;
#endif
layout(std430, set = 0, binding = 18) buffer Counters { uint bad_samples; uint counter_spare[3]; };

const uint FLAG_TEMPORAL = 1u;
const uint FLAG_SPATIAL = 2u;
const uint FLAG_ACCUMULATE = 4u;
const uint MOST_SPATIAL = 4u;
const uint LIGHT_RAYS = 7u;      // world, objects, alpha-tested (the reference's light rays)
const uint LIGHT_RAYS_ON_OBJECT = 5u; // (an object's pixel: the world's alone - its low-poly body doesn't shadow itself)

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

// A pixel's surface: where it is, its shading normal (toward the eye) and its triangle's, whether it is an object's,
// its albedo (linear).
struct Surface {
    vec3 at;
    vec3 n;
    vec3 ng;
    vec3 albedo;
    bool valid;
    bool object;
};
Surface no_surface() { return Surface(vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), false, false); }
void put_surface(uint i, Surface s)
{
    now_at[i] = vec4(s.at, s.valid ? 1.0 : 0.0);
    now_more[i] = uvec4(packSnorm2x16(oct_encode(s.n)), packSnorm2x16(oct_encode(s.ng)),
                        packUnorm4x8(vec4(sqrt(clamp(s.albedo, 0.0, 1.0)), 0.0)), s.object ? 1u : 0u);
}
Surface surface_of(vec4 at, uvec4 more)
{
    Surface s;
    s.at = at.xyz;
    s.valid = at.w > 0.5;
    s.n = oct_decode(unpackSnorm2x16(more.x));
    s.ng = oct_decode(unpackSnorm2x16(more.y));
    vec3 a = unpackUnorm4x8(more.z).rgb;
    s.albedo = a * a;
    s.object = (more.w & 1u) != 0u;
    return s;
}

// Whether `other` is the same surface as `s` near enough to share its light (its normal within acos tune.y, its plane
// within tune.z of the distance from the eye).
bool alike(Surface s, Surface other)
{
    if (!other.valid || dot(s.n, other.n) < pass.tune.y) {
        return false;
    }
    float distance = length(s.at - pass.eye.xyz);
    return abs(dot(other.at - s.at, s.ng)) <= pass.tune.z * distance && abs(dot(s.at - other.at, other.ng)) <= pass.tune.z * distance;
}

// The closest surface along a ray (cut-outs by their alpha).
bool trace_closest(vec3 origin, vec3 dir, float reach, out uint instance, out uint primitive, out uint id,
                   out vec2 barycentrics, out float t, out mat4x3 to_world)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsNoneEXT, LIGHT_RAYS, origin, 0.0, dir, reach);
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

// The pixel's surface: the check's camera ray through its centre; the game's the prepass's depth there, its triangle the
// one a camera ray meets at that point (none: no lamps here, as the lamps pass had none).
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
    if (!trace_closest(eye, dir, reach, instance, primitive, id, bary, t, to_world) ||
        (expected >= 0.0 && abs(t - expected) > 1.0 + 0.02 * expected)) {
        return no_surface();
    }
    HitSurface h;
    if (!hit_surface(instance, primitive, id, bary, to_world, dir, h) || h.hidden) {
        return no_surface();
    }
    Surface s;
    s.at = h.position;
    s.ng = h.normal;
    s.n = dot(h.shading_normal, -dir) > 0.0 ? h.shading_normal : h.normal;
    s.albedo = h.base_colour;
    s.object = (geometry_record(instance).instance_flags & HIT_INSTANCE_OBJECT) != 0u;
    s.valid = true;
    return s;
}

// A light sample - the list's entry `e` and a point on it (`uv`: uniform on an emitting triangle; an analytic lamp's own
// point) - seen from a surface: the light a white diffuser there would get from it unshadowed (`light`: the emission at
// that point - a triangle's lamp texels, its store's emission there), the target (its luminance: what the sample brings,
// visibility aside), the way to it and how far to trace, and its pdf in the list's measure (a lamp: its pick; a triangle:
// its pick / its area - a density over its area, as `light` is).
// (round PT4b) The target was a triangle's mean radiance (its listed power / 2 pi A) x the geometry, the exact emission
// only for the shading: on a lamp drawn in a texture (light on its lamp texels alone - often 1 texel in 10 or 100) a
// sample between the texels kept its weight and brought nothing, one on them brought 1 / their share of the mean - and
// the reuse kept and spread both for frames (M up to 20 x the candidates): black and white blotches with hard edges
// that Ray Reconstruction takes for the picture (Boston's walls blown white, the speckled pools). The target is now the
// sample's own emission: RIS resamples toward what a sample brings, a sample's weight is what it is worth.
struct LightPoint {
    vec3 light;
    float target;
    vec3 dir;
    float reach;
    float pdf;
};
bool light_point(uint e, vec2 uv, Surface s, out LightPoint lp)
{
    lp = LightPoint(vec3(0.0), 0.0, vec3(0.0, 1.0, 0.0), 0.0, 0.0);
    if (e >= pass.counts.x) {
        return false;
    }
    LightEntry entry = light_entries[e];
    uint kind = light_kind(entry.payload);
    uint index = light_index(entry.payload);
    if (kind == LIGHT_TRIANGLE) {
        uvec4 listed = emitter_entries[index];
        HitVertices tri;
        if (!hit_vertices(listed.x, listed.y, tri)) {
            return false;
        }
        mat4x3 to_world = instance_transform(listed.x);
        vec3 a = to_world * vec4(tri.position[0], 1.0);
        vec3 b = to_world * vec4(tri.position[1], 1.0);
        vec3 c = to_world * vec4(tri.position[2], 1.0);
        vec3 across = cross(b - a, c - a);
        float twice = length(across);
        if (!(twice > 0.0)) {
            return false;
        }
        float area = 0.5 * twice;
        vec3 bc = path_triangle_point(uv.x, uv.y);
        vec3 y = a * bc.x + b * bc.y + c * bc.z;
        vec3 to = y - s.at;
        float d2 = dot(to, to);
        float d = sqrt(d2);
        if (!(d > 0.0)) {
            return false;
        }
        vec3 dir = to / d;
        float cos_here = dot(s.n, dir);
        float cos_there = abs(dot(across / twice, dir));
        if (!(cos_here > 0.0) || dot(s.ng, dir) <= 0.0 || !(cos_there > 0.0)) {
            return false;
        }
        float g = cos_here * cos_there / (PATH_PI * d2);
        vec3 emitted;
        if (!hit_emission(listed.x, listed.y, instance_material(listed.x), bc.yz, emitted)) {
            return false;
        }
        lp.light = max(emitted, vec3(0.0)) * g;
        lp.target = path_luma(lp.light);
        lp.dir = dir;
        lp.reach = max(d * 0.999 - ray_offset(y), 0.0);
        lp.pdf = entry.pdf / area;
        return lp.target > 0.0;
    }
    if (kind == LIGHT_LAMP) {
        LightRecord light = lamps[index];
        vec3 from;
        float g = lamp_geometry(light, s.at, s.n, from);
        if (!(g > 0.0)) {
            return false;
        }
        vec3 to = from - s.at;
        float d = length(to);
        vec3 dir = to / max(d, 1.0e-6);
        if (dot(dir, s.ng) <= 0.0 || !(dot(s.n, dir) > 1.0e-4)) {
            return false;
        }
        lp.light = max(light.colour.rgb, vec3(0.0)) * g;
        lp.target = path_luma(lp.light);
        lp.dir = dir;
        lp.reach = max(d - light.at.w, 0.0);
        lp.pdf = entry.pdf;
        return lp.target > 0.0;
    }
    return false;
}

// What of the light gets from it to the surface (the one light ray - vulkan_rt_scene.h ray_light).
vec3 seen(Surface s, LightPoint lp)
{
    return ray_light(s.object ? LIGHT_RAYS_ON_OBJECT : LIGHT_RAYS, s.at + s.ng * ray_offset(s.at), lp.dir, lp.reach);
}
bool seen_at_all(vec3 through) { return max(through.r, max(through.g, through.b)) > 0.0; }

// A reservoir: its sample (the list's entry + 1 - 0: none -, the point on it), its weight W (the unbiased contribution
// weight: 1 / the sample's effective pdf) and its sample count M.
struct Reservoir {
    uint entry;
    vec2 uv;
    float W;
    float M;
};
Reservoir reservoir_of(uvec4 v) { return Reservoir(v.x, unpackUnorm2x16(v.y), uintBitsToFloat(v.z), uintBitsToFloat(v.w)); }
uvec4 words_of(Reservoir r) { return uvec4(r.entry, packUnorm2x16(r.uv), floatBitsToUint(r.W), floatBitsToUint(r.M)); }
// (a point's numbers as the reservoir keeps them: what it is drawn with is what it is found again with)
vec2 kept_uv(vec2 uv) { return round(clamp(uv, 0.0, 1.0) * 65535.0) / 65535.0; }
float good(float x) { return isnan(x) || isinf(x) ? 0.0 : x; }

#ifdef PASS_TEMPORAL
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    uint i = uint(local.y * pass.rect.z + local.x);
    Surface s = primary(pass.rect.xy + local);
    put_surface(i, s);
    float cap = float(pass.params.z);
    if (!s.valid) {
        res_a[i] = uvec4(0u, 0u, 0u, floatBitsToUint(0.0));
        return;
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z, pass.params.w);
    // 1. the initial candidates, resampled toward the target
    uint count = pass.counts.x;
    Reservoir r = Reservoir(0u, vec2(0.0), 0.0, float(pass.params.x));
    float wsum = 0.0;
    float chosen = 0.0;
    LightPoint chosen_point;
    for (uint k = 0u; k < pass.params.x; ++k) {
        float u = path_next(rng);
        vec2 uv = kept_uv(vec2(path_next(rng), path_next(rng)));
        float pick = path_next(rng);
        if (count == 0u) {
            continue;
        }
        LightPoint lp;
        uint e = light_pick(u, count);
        if (!light_point(e, uv, s, lp) || !(lp.pdf > 0.0)) {
            continue;
        }
        float w = good(lp.target / lp.pdf);
        wsum += w;
        if (w > 0.0 && pick * wsum < w) {
            r.entry = e + 1u;
            r.uv = uv;
            chosen = lp.target;
            chosen_point = lp;
        }
    }
    r.W = chosen > 0.0 ? good(wsum / (r.M * chosen)) : 0.0;
    // (visibility reuse: a light the surface doesn't see is dropped - its W 0, its M kept)
    if (r.W > 0.0 && !seen_at_all(seen(s, chosen_point))) {
        r.W = 0.0;
    }
    // 2. the reservoir this point had last frame
    if ((pass.counts.w & FLAG_TEMPORAL) != 0u) {
        vec4 q = pass.prev_to_pixel * vec4(s.at, 1.0);
        if (q.w > 0.0) {
            ivec2 before = ivec2(floor(q.xy / q.w)) - pass.rect.xy;
            if (all(greaterThanEqual(before, ivec2(0))) && all(lessThan(before, pass.rect.zw))) {
                uint j = uint(before.y * pass.rect.z + before.x);
                Surface then = surface_of(before_at[j], before_more[j]);
                if (alike(s, then)) {
                    Reservoir h = reservoir_of(res_b[j]);
                    h.M = min(h.M, cap);
                    LightPoint hp;
                    float t_h = h.entry > 0u && h.W > 0.0 && light_point(h.entry - 1u, h.uv, s, hp) ? hp.target : 0.0;
                    float w_r = chosen * r.W * r.M;
                    float w_h = good(t_h * h.W * h.M);
                    float total = w_r + w_h;
                    Reservoir out_r = Reservoir(0u, vec2(0.0), 0.0, r.M + h.M);
                    if (total > 0.0) {
                        float z = 0.0;
                        float target = 0.0;
                        if (path_next(rng) * total < w_h) {
                            // its light, seen from here: then this pixel's candidates could have made it too
                            if (seen_at_all(seen(s, hp))) {
                                out_r.entry = h.entry;
                                out_r.uv = h.uv;
                                target = t_h;
                                z = h.M + r.M;
                            }
                        } else {
                            out_r.entry = r.entry;
                            out_r.uv = r.uv;
                            target = chosen;
                            z = r.M;
                            // could last frame's reservoir have made it: its surface's target and a ray from there
                            LightPoint tp;
                            if (light_point(r.entry - 1u, r.uv, then, tp) && seen_at_all(seen(then, tp))) {
                                z += h.M;
                            }
                        }
                        out_r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
                    }
                    r = out_r;
                }
            }
        }
    }
    res_a[i] = words_of(r);
}
#endif

#ifdef PASS_SPATIAL
void put_result(ivec2 p, uint i, vec3 lamps, vec3 on_albedo, vec3 full)
{
#ifdef CHECK
    if ((pass.counts.w & FLAG_ACCUMULATE) != 0u) {
        sums[i] += vec4(full, 1.0);
        sums[uint(pass.rect.z * pass.rect.w) + i] = vec4(full, 1.0);
    }
#else
    imageStore(o_lamps, p, vec4(lamps, 0.0));
    imageStore(o_lamps_on, p, vec4(on_albedo, 0.0));
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
        res_b[i] = uvec4(0u, 0u, 0u, floatBitsToUint(0.0));
        put_result(p, i, vec3(0.0), vec3(0.0), vec3(0.0));
        return;
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z ^ 0x9E3779B9u, pass.params.w + 1u);
    // the pixel's reservoir and its neighbours' that are the same surface
    Reservoir taken[MOST_SPATIAL + 1u];
    Surface where[MOST_SPATIAL + 1u];
    uint n = 1u;
    taken[0] = reservoir_of(res_a[i]);
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
            taken[n] = reservoir_of(res_a[j]);
            taken[n].M = min(taken[n].M, cap);
            where[n] = other;
            ++n;
        }
    }
    // resampled toward this pixel's target
    float total = 0.0;
    float m_sum = 0.0;
    uint chosen = 0xFFFFFFFFu;
    float target = 0.0;
    LightPoint chosen_point;
    for (uint k = 0u; k < n; ++k) {
        m_sum += taken[k].M;
        LightPoint lp;
        if (taken[k].entry == 0u || !(taken[k].W > 0.0) || !light_point(taken[k].entry - 1u, taken[k].uv, s, lp)) {
            continue;
        }
        float w = good(lp.target * taken[k].W * taken[k].M);
        total += w;
        if (w > 0.0 && path_next(rng) * total < w) {
            chosen = k;
            target = lp.target;
            chosen_point = lp;
        }
    }
    Reservoir r = Reservoir(0u, vec2(0.0), 0.0, min(m_sum, cap));
    vec3 lamps = vec3(0.0);
    vec3 full = vec3(0.0);
    if (chosen != 0xFFFFFFFFu) {
        vec3 through = seen(s, chosen_point);
        if (seen_at_all(through)) {
            // Z: the samples of every reservoir that could have made this light - its own surface's target and a ray
            float z = 0.0;
            for (uint k = 0u; k < n; ++k) {
                if (k == chosen || k == 0u) {
                    z += taken[k].M; // (its own: it was seen there; this pixel's: seen just now)
                    continue;
                }
                LightPoint other;
                if (light_point(taken[chosen].entry - 1u, taken[chosen].uv, where[k], other) &&
                    seen_at_all(seen(where[k], other))) {
                    z += taken[k].M;
                }
            }
            r.entry = taken[chosen].entry;
            r.uv = taken[chosen].uv;
            r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
            if (r.W > 0.0) {
                LightPoint exact = chosen_point;
                lamps = exact.light * through * r.W;
#ifdef CHECK
                BrdfMaterial m = brdf_material(s.albedo * pass.force.z, vec3(pass.force.y), pass.force.x);
                float facing = max(dot(s.n, exact.dir), 1.0e-4);
                full = brdf_direct(m, s.n, normalize(pass.eye.xyz - s.at), exact.dir, exact.light / facing * through) * r.W;
#endif
            }
        }
    }
    if (any(isnan(lamps)) || any(isinf(lamps)) || any(isnan(full)) || any(isinf(full))) {
        atomicAdd(bad_samples, 1u);
        lamps = vec3(0.0);
        full = vec3(0.0);
        r.W = 0.0;
    }
    res_b[i] = words_of(r);
    vec3 exposed = min(lamps * pass.eye.w, vec3(65504.0));
    put_result(p, i, exposed, min(exposed * s.albedo, vec3(65504.0)), full);
}
#endif
)";

enum class Shader : uint8_t { kScan, kTemporal, kSpatial };

std::string shader_source(Shader which, bool check)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    if (check) {
        source += "#define CHECK\n";
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
    source += rt_lamp_glsl();
    source += "const uint SKY_WIDTH = 1u;\nconst uint SKY_HEIGHT = 1u;\n";
    source += "const uint TRIANGLE_LATTICE = " + std::to_string(rt::lights::kTriangleLattice) + "u;\n";
    source += kCommon;
    source += light_instances_glsl();
    source += rt::lights::kLightListGlsl;
    source += which == Shader::kScan ? std::string(emitter_scan_glsl()) : std::string(kRestirBody);
    return source;
}

// --- the matrices --------------------------------------------------------------------------------------------------------

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

// Gauss-Jordan with partial pivoting; false when singular.
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

// (pixel x, pixel y, depth, 1) -> world (column vectors) for the view (the shadows' and the lamps' passes' mapping).
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
    const Matrix row_vectors = multiply(view_m, projection); // (D3D's: clip = world x view x projection)
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

struct RestirState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkPipeline scan = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> temporal{}; // the game's, the check's
    std::array<VkPipeline, 2> spatial{};
    bool failed = false;
    // the emitting triangles: the list in use and the one a scan fills, read back without a wait
    std::array<DeviceBuffer, 2> emitters{};
    int list_now = -1;
    DeviceBuffer membership;
    DeviceBuffer slots;
    DeviceBuffer read_back;
    uint64_t list_key = 0;    // the world and the lamps' surfaces the list in use was scanned from
    uint64_t scan_key = 0;    // ... and the scan in flight's
    uint64_t scan_serial = 0; // its submission (0: none in flight)
    int scan_buffer = 0;
    std::vector<float> triangle_powers;
    uint32_t candidates = 0;
    uint64_t lists = 0;
    // the alias table and the lamps (made again when their power changes)
    DeviceBuffer lights;
    DeviceBuffer lamps;
    uint64_t table_key = 0;
    uint32_t entries = 0;
    uint32_t lamp_count = 0;
    double power = 0.0; // (the table's total: 0 - every lamp off, nothing emitting - nothing to light)
    uint64_t layout_key = 0; // which light an entry is (the list's triangles, the lamps' count): history across it only
    std::vector<rt::Emitter> local_lamps; // (the lamps the records were made from - restir_local_lights)
    // per pixel
    uint32_t width = 0;
    uint32_t height = 0;
    DeviceBuffer reservoirs_a;
    DeviceBuffer reservoirs_b;
    std::array<DeviceBuffer, 2> at{};
    std::array<DeviceBuffer, 2> more{};
    DeviceBuffer sums;
    DeviceBuffer counters;
    uint64_t frames = 0;
    bool history = false;       // last frame's reservoirs and surfaces are this view's
    uint64_t history_layout = 0;
    bool history_check = false; // (made by the check's variant)
    std::array<float, 16> prev_to_pixel{};
    std::array<float, 3> prev_eye{};
    bool accumulate = true; // (the check's: this frame's estimate added to the sums)
    bool restart = false;
    // the view
    bool lit_view = false;
    uint64_t passes = 0;
    Timing timing;
    double last_ms = 0.0;
    uint32_t timed_width = 0;
    uint32_t timed_height = 0;
    RestirOverrides overrides;
};

RestirState& restir_state()
{
    static RestirState made;
    return made;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class RestirListener final : public rt::ViewListener {
public:
    // (before the lamps pass, which draws nothing where this lit the view - after the inputs are begun)
    [[nodiscard]] int order() const override { return rt::ViewOrder::kLights - 1; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_restir_di_pass(state, view);
        }
    }
};

RestirListener& listener()
{
    static RestirListener instance;
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

// The layouts and the emitters' scan (the local lights' upkeep - restir_local_lights - needs no more).
bool ensure_scan(Backend& state, std::string* error)
{
    RestirState& own = restir_state();
    const VkDevice device = state.context.device();
    if (own.set_layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if (binding == kBindingScene) {
                type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            } else if (binding == kBindingBlock) {
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if (binding == kBindingDepth || binding == kBindingLampsOut || binding == kBindingLampsOnOut) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            core::at(bindings, binding) = {.binding = binding,
                                           .descriptorType = type,
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
    }
    if (own.scan == VK_NULL_HANDLE) {
        own.scan = make_compute(device, own.layout, shader_source(Shader::kScan, false), error);
    }
    return own.scan != VK_NULL_HANDLE;
}

bool ensure_pipelines(Backend& state, bool check, std::string* error)
{
    RestirState& own = restir_state();
    const VkDevice device = state.context.device();
    if (!ensure_scan(state, error)) {
        return false;
    }
    const std::size_t variant = check ? 1U : 0U;
    if (core::at(own.temporal, variant) == VK_NULL_HANDLE) {
        core::at(own.temporal, variant) = make_compute(device, own.layout, shader_source(Shader::kTemporal, check), error);
        if (core::at(own.temporal, variant) == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (core::at(own.spatial, variant) == VK_NULL_HANDLE) {
        core::at(own.spatial, variant) = make_compute(device, own.layout, shader_source(Shader::kSpatial, check), error);
    }
    return core::at(own.spatial, variant) != VK_NULL_HANDLE;
}

bool ensure_buffer(Backend& state, DeviceBuffer* buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, BufferPlace place)
{
    if (buffer->buffer != VK_NULL_HANDLE && buffer->bytes >= bytes) {
        return true;
    }
    retire_buffer(state, buffer);
    return state.memory.create_buffer(bytes, usage, place, buffer);
}

// A buffer the processor writes once and the device reads (made again; an earlier frame may still read the old one).
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

// The world the emitters' list names triangles of: its instances (world ones come first in the frame record - their
// places are the list's), and how much the lamps' own surfaces are on (their emission switches with them) and the cut.
uint64_t world_key_of(const rt::FrameRecord& record, const SceneLighting& lighting)
{
    uint64_t key = kFnvBasis;
    uint32_t place = 0;
    for (const rt::Instance& instance : record.instances) {
        if ((instance.flags & rt::kWorld) != 0U) {
            key = mix(mix(mix(mix(key, place), instance.mesh), instance.material), instance.flags);
        }
        ++place;
    }
    return mix(mix(key, lighting.cut), lighting.surfaces_switched);
}

// The scan of the frame's world recorded (its list into the buffer not in use, read back into read_back).
bool record_scan(Backend& state, RestirState& own, VkCommandBuffer commands, const SceneView& scene, uint64_t key)
{
    std::vector<uint32_t> triangles;
    scene_instance_triangles(state, &triangles);
    std::vector<uint32_t> firsts(triangles.size() + 1, 0);
    uint64_t total = 0;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        core::at(firsts, i) = static_cast<uint32_t>(total);
        total += core::at(triangles, i);
    }
    total = std::min<uint64_t>(total, 0x3FFFFFFFULL);
    firsts.back() = static_cast<uint32_t>(total);
    const int into = own.list_now == 0 ? 1 : 0;
    DeviceBuffer& list = core::at(own.emitters, static_cast<std::size_t>(into));
    const VkBufferUsageFlags storage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const VkDeviceSize list_bytes =
        (kEmitterHeaderWords + (static_cast<VkDeviceSize>(kEmitterEntryWords) * kListCapacity)) * sizeof(uint32_t);
    if (!ensure_buffer(state, &list, list_bytes, storage, BufferPlace::kDevice) ||
        !ensure_buffer(state, &own.slots, std::max<VkDeviceSize>(total, 1) * sizeof(uint32_t), storage,
                       BufferPlace::kDevice) ||
        !upload_buffer(state, &own.membership, firsts.data(), firsts.size() * sizeof(uint32_t)) ||
        !ensure_buffer(state, &own.read_back, list_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack)) {
        return false;
    }
    vkCmdFillBuffer(commands, list.buffer, 0, kEmitterHeaderWords * sizeof(uint32_t), 0);
    vkCmdFillBuffer(commands, own.slots.buffer, 0, VK_WHOLE_SIZE, 0);
    barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    RestirBlock block;
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), static_cast<uint32_t>(triangles.size()),
                       static_cast<uint32_t>(total)};
    block.list = {kListCapacity, 0U, 0U, 0U};
    std::array<uint8_t, sizeof(RestirBlock)> bytes{};
    std::memcpy(bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> span = state.ring.copy(bytes, kUniformAlignment);
    if (!span) {
        return false;
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const VkDescriptorBufferInfo geometry_info{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const VkDescriptorBufferInfo block_info{.buffer = span->buffer, .offset = span->offset, .range = sizeof(RestirBlock)};
    const VkDescriptorBufferInfo list_info{.buffer = list.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo membership_info{.buffer = own.membership.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo slots_info{.buffer = own.slots.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 6> writes{};
    const auto write = [&writes](std::size_t at, uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* info,
                                 const void* next) {
        VkWriteDescriptorSet& one = core::at(writes, at);
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.pNext = next;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = type;
        one.pBufferInfo = info;
    };
    write(0, kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, &scene_info);
    write(1, kBindingGeometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &geometry_info, nullptr);
    write(2, kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &block_info, nullptr);
    write(3, kBindingEmitters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &list_info, nullptr);
    write(4, kBindingMembership, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &membership_info, nullptr);
    write(5, kBindingSlots, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &slots_info, nullptr);
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    if (total > 0) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.scan);
        const auto groups = static_cast<uint32_t>((total + kScanGroup - 1) / kScanGroup);
        const uint32_t groups_x = std::min(groups, kMostGroupsX);
        vkCmdDispatch(commands, groups_x, (groups + groups_x - 1) / groups_x, 1);
    }
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = list_bytes};
    vkCmdCopyBuffer(commands, list.buffer, own.read_back.buffer, 1, &region);
    barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_HOST_READ_BIT);
    own.scan_key = key;
    own.scan_serial = state.frames.serial();
    own.scan_buffer = into;
    return true;
}

// A finished scan's list taken into use (its submission has run): the triangles' powers read.
void adopt_scan(Backend& state, RestirState& own)
{
    vmaInvalidateAllocation(state.memory.allocator(), own.read_back.allocation, 0, VK_WHOLE_SIZE);
    const auto* words = static_cast<const uint32_t*>(own.read_back.mapped);
    if (words == nullptr) {
        own.scan_serial = 0;
        return;
    }
    const std::span<const uint32_t> all(words, (kEmitterHeaderWords + (static_cast<std::size_t>(kEmitterEntryWords) * kListCapacity)));
    const uint32_t listed = std::min(core::at(all, 0), kListCapacity);
    own.candidates = core::at(all, 1);
    own.triangle_powers.resize(listed);
    for (std::size_t i = 0; i < listed; ++i) {
        core::at(own.triangle_powers, i) =
            std::bit_cast<float>(core::at(all, kEmitterHeaderWords + (i * kEmitterEntryWords) + 2));
    }
    own.list_now = own.scan_buffer;
    own.list_key = own.scan_key;
    own.scan_serial = 0;
    own.table_key = 0; // (made again with the new triangles)
    ++own.lists;
    if (core::at(all, 0) > kListCapacity && first_report(state, kReportList)) {
        get_log("restir: the level has %u emitting triangles - the list holds the first %u", core::at(all, 0), kListCapacity);
    }
}

// The alias table over the list's triangles and the lamps (the local group, by power - rt_light_list.h) and the lamps'
// records, uploaded when they change.
bool update_table(Backend& state, RestirState& own, const std::vector<rt::Emitter>& lamps)
{
    uint64_t key = mix(kFnvBasis, own.lists);
    std::vector<float> records;
    records.reserve(std::max<std::size_t>(lamps.size(), 1) * kLampRecordFloats);
    for (const rt::Emitter& lamp : lamps) {
        const std::array<float, kLampRecordFloats> record = lamp_record(lamp);
        for (const float value : record) {
            key = mix(key, std::bit_cast<uint32_t>(value));
        }
        records.insert(records.end(), record.begin(), record.end());
    }
    key = mix(key, lamps.size());
    if (key == own.table_key && own.lights.buffer != VK_NULL_HANDLE && own.lamps.buffer != VK_NULL_HANDLE) {
        return true;
    }
    rt::lights::LightSet set;
    set.triangle_powers = own.list_now >= 0 ? own.triangle_powers : std::vector<float>{};
    set.lamps = lamps;
    const rt::lights::LightList list = rt::lights::build_light_list(set, rt::lights::LightWeights::kPower);
    if (!upload_buffer(state, &own.lights, list.table.entries.data(),
                       list.table.entries.size() * sizeof(rt::lights::AliasEntry)) ||
        !upload_buffer(state, &own.lamps, records.data(), records.size() * sizeof(float))) {
        return false;
    }
    own.entries = static_cast<uint32_t>(list.table.entries.size());
    own.power = list.table.total;
    own.lamp_count = static_cast<uint32_t>(lamps.size());
    own.table_key = key;
    own.layout_key = mix(mix(mix(kFnvBasis, own.lists), set.triangle_powers.size()), lamps.size());
    return true;
}

bool ensure_timing(Backend& state, RestirState& own)
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

// A slot's last pair read back when its frame has run; every kTimingReport passes the mean logged.
void collect_timing(Backend& state, RestirState& own, uint32_t slot, bool check)
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
            get_log("restir: %.3f ms a frame on the device over the last %u (%ux%u; %u emitting triangles, %u lamps)",
                    own.last_ms, timing.measured, own.timed_width, own.timed_height,
                    static_cast<uint32_t>(own.triangle_powers.size()), own.lamp_count);
        }
        timing.sum_ms = 0.0;
        timing.measured = 0;
    }
}

// Whether this view's lamps are ReSTIR's: rt_lights on, the direct light restir (or auto with Ray Reconstruction to
// denoise it), and the view lit per surface.
bool wanted(Backend& state)
{
    const RtLightSettings& settings = rt_light_settings_locked();
    if (!settings.on) {
        return false;
    }
    const bool restir = settings.direct == RtDirectLight::kRestir ||
                        (settings.direct == RtDirectLight::kAuto && ray_reconstruction_active());
    return restir && surface_inputs(state) != nullptr;
}

// The local lights kept up for this frame (see restir_local_lights): a finished scan taken into use, a new one recorded when
// the world or its lamps' own surfaces changed, the alias table and the lamps' records made again when their power
// changed. False: the scan's pipeline or the table couldn't be made.
bool keep_local_lights(Backend& state, RestirState& own, VkCommandBuffer commands, const SceneView& scene,
                       const SceneLighting& lighting)
{
    std::string error;
    if (own.failed || !ensure_scan(state, &error)) {
        if (!own.failed) {
            own.failed = true;
            if (first_report(state, kReportPipeline)) {
                get_log("restir: the emitters' scan couldn't be made (%s) - the lamps pass lights the lamps",
                        error.c_str());
            }
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
        return false;
    }
    const rt::FrameRecord& record = rt::frame_record();
    const uint64_t world_key = world_key_of(record, lighting);
    // the emitters' list: a finished scan taken into use; a new one when the world or its lamps' surfaces changed
    if (own.scan_serial != 0 && state.frames.completed() >= own.scan_serial) {
        if (own.scan_key == world_key) {
            adopt_scan(state, own);
        } else {
            own.scan_serial = 0; // (the world changed while it ran: scanned again)
        }
    }
    if (own.list_now >= 0 && own.list_key != world_key) {
        own.list_now = -1; // (its places name another world's triangles: none until the new scan is in)
        own.triangle_powers.clear();
        own.table_key = 0;
    }
    if (own.scan_serial == 0 && own.list_key != world_key) {
        barrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        if (!record_scan(state, own, commands, scene, world_key)) {
            if (first_report(state, kReportBuffers)) {
                get_log("restir: the emitters' list couldn't be made - the lamps alone");
            }
            own.list_key = world_key; // (not again every frame)
        }
    }
    if (!update_table(state, own, lighting.lamps)) {
        return false;
    }
    const std::array<uint32_t, 4> empty_list{};
    if (own.list_now < 0 && core::at(own.emitters, 0).buffer == VK_NULL_HANDLE &&
        !upload_buffer(state, &core::at(own.emitters, 0), empty_list.data(), sizeof(empty_list))) {
        return false;
    }
    if (own.membership.buffer == VK_NULL_HANDLE &&
        !upload_buffer(state, &own.membership, empty_list.data(), sizeof(empty_list))) {
        return false;
    }
    if (own.slots.buffer == VK_NULL_HANDLE && !upload_buffer(state, &own.slots, empty_list.data(), sizeof(empty_list))) {
        return false;
    }
    own.local_lamps = lighting.lamps;
    return true;
}

} // namespace

bool restir_local_lights(Backend& state, VkCommandBuffer commands, const SceneLighting& lighting, RestirLocalLights* out)
{
    RestirState& own = restir_state();
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE || scene.instances_address == 0 ||
        !state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE ||
        !keep_local_lights(state, own, commands, scene, lighting)) {
        return false;
    }
    const DeviceBuffer& list = core::at(own.emitters, static_cast<std::size_t>(std::max(own.list_now, 0)));
    out->emitters = list.buffer;
    out->lamps = own.lamps.buffer;
    out->membership = own.membership.buffer;
    out->slots = own.slots.buffer;
    out->triangle_powers = own.list_now >= 0 ? own.triangle_powers : std::vector<float>{};
    out->lamp_list = own.local_lamps;
    out->key = own.table_key;
    out->layout_key = own.layout_key;
    return true;
}

void set_restir_overrides(const RestirOverrides& overrides)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    restir_state().overrides = overrides;
}

void forget_restir_history()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    restir_state().history = false;
}

void set_restir_accumulate(bool on)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    restir_state().accumulate = on;
}

void restart_restir_accumulation()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    restir_state().restart = true;
}

bool read_restir_accumulation(Backend& state, RestirAccumulation* out)
{
    *out = RestirAccumulation{};
    RestirState& own = restir_state();
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

RestirStats restir_stats()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    const RestirState& own = restir_state();
    return RestirStats{.passes = own.passes,
                       .listed = static_cast<uint32_t>(own.triangle_powers.size()),
                       .candidates = own.candidates,
                       .lamps = own.lamp_count,
                       .lists = own.lists,
                       .list_ready = own.list_now >= 0,
                       .gpu_ms = own.last_ms,
                       .gpu_width = own.timed_width,
                       .gpu_height = own.timed_height};
}

bool restir_lit_view(const Backend& /*state*/) { return restir_state().lit_view; }

bool record_restir_di_pass(Backend& state, const rt::ViewDrawn& view)
{
    RestirState& own = restir_state();
    own.lit_view = false;
    const bool check = own.overrides.on;
    if (own.failed || (!check && !wanted(state)) || !state.context.ray_tracing_enabled() || rt::ray_tracing() == nullptr) {
        return false;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE || scene.instances_address == 0 ||
        !state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE) {
        return false;
    }
    std::string error;
    if (!ensure_pipelines(state, check, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("restir: the passes couldn't be made (%s) - the lamps pass lights the lamps", error.c_str());
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
        return false;
    }
    // the view's rectangle and its images
    const SurfaceInputs* const inputs = check ? nullptr : surface_inputs(state);
    std::array<uint32_t, 4> rect = view.rect;
    if (inputs != nullptr) {
        const Extent extent{.width = inputs->lamps->extent.width, .height = inputs->lamps->extent.height};
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

    // the lighting: the game's (lamps kept in their places while they switch), or the check's
    SceneLighting lighting;
    if (check) {
        lighting.lamps = rt::path::analytic_lamps(own.overrides.lamps, own.overrides.lamps_surfaced);
    } else {
        lighting = scene_lighting(false, true);
    }
    VkCommandBuffer commands = outside_pass(state);
    if (!keep_local_lights(state, own, commands, scene, lighting)) {
        return false;
    }
    if (!(own.power > 0.0) && !check) {
        // (no lamp on and nothing emitting - by day: the inputs were begun with no lamps; nothing to trace)
        own.history = false;
        own.lit_view = true;
        return true;
    }
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
    bool made = ensure_buffer(state, &own.reservoirs_a, pixels * 16, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.reservoirs_b, pixels * 16, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.counters, 16, storage, BufferPlace::kDevice) &&
                (!check || ensure_buffer(state, &own.sums, pixels * 2 * 16, storage, BufferPlace::kDevice));
    for (std::size_t k = 0; k < 2 && made; ++k) {
        made = ensure_buffer(state, &core::at(own.at, k), pixels * 16, storage, BufferPlace::kDevice) &&
               ensure_buffer(state, &core::at(own.more, k), pixels * 16, storage, BufferPlace::kDevice);
    }
    if (!made) {
        if (first_report(state, kReportBuffers)) {
            get_log("restir: the reservoirs (%ux%u) couldn't be made - the lamps pass lights the lamps", width, height);
        }
        own.history = false;
        return false;
    }
    if (inputs != nullptr && (inputs->depth == nullptr || inputs->lamps == nullptr || inputs->lamps_on == nullptr)) {
        return false;
    }

    // the block
    ++own.frames;
    const std::size_t now = own.frames & 1U;
    const std::size_t before = now ^ 1U;
    const bool history = own.history && invertible && own.history_layout == own.layout_key && own.history_check == check &&
                         (!check || own.overrides.temporal);
    RestirBlock block;
    block.to_world = column_major(to_world);
    block.prev_to_pixel = own.prev_to_pixel;
    block.eye = {core::at(view.camera.position, 0), core::at(view.camera.position, 1), core::at(view.camera.position, 2),
                 rt::frame_pre_exposure()};
    block.prev_eye = {core::at(own.prev_eye, 0), core::at(own.prev_eye, 1), core::at(own.prev_eye, 2), 0.0F};
    block.rect = {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
                  static_cast<int32_t>(width), static_cast<int32_t>(height)};
    const uint32_t candidates =
        check && own.overrides.candidates > 0 ? own.overrides.candidates : kInitialCandidates;
    const uint32_t spatial = check && own.overrides.spatial != 0xFFU ? own.overrides.spatial : kSpatialNeighbours;
    const uint32_t flags = (history ? kFlagTemporal : 0U) | (spatial > 0 ? kFlagSpatial : 0U) | (check && own.accumulate ? kFlagAccumulate : 0U);
    block.counts = {own.entries, own.lamp_count, static_cast<uint32_t>(own.frames), flags};
    block.params = {candidates, std::min(spatial, kMostSpatial), kHistoryCap * candidates,
                    check ? own.overrides.seed : 0x2545F491U};
    block.tune = {kSpatialRadius, kNormalAgreement, kPlaneShare, 0.0F};
    block.force = {own.overrides.roughness, own.overrides.f0, own.overrides.albedo_scale, check ? 1.0F : 0.0F};
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), scene.instances, 0U};
    block.list = {kListCapacity, 0U, 0U, 0U};
    std::array<uint8_t, sizeof(RestirBlock)> block_bytes{};
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
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_UNIFORM_READ_BIT);
    if (inputs != nullptr) {
        for (Image* image : {inputs->depth, inputs->lamps, inputs->lamps_on}) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
        }
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const auto whole = [](const DeviceBuffer& buffer) {
        return VkDescriptorBufferInfo{.buffer = buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    };
    const DeviceBuffer& list = core::at(own.emitters, static_cast<std::size_t>(std::max(own.list_now, 0)));
    const std::array<VkDescriptorBufferInfo, 13> buffers{
        VkDescriptorBufferInfo{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes},
        VkDescriptorBufferInfo{.buffer = block_span->buffer, .offset = block_span->offset, .range = sizeof(RestirBlock)},
        whole(own.lights),
        whole(list),
        whole(own.lamps),
        whole(own.reservoirs_a),
        whole(own.reservoirs_b),
        whole(core::at(own.at, now)),
        whole(core::at(own.more, now)),
        whole(core::at(own.at, before)),
        whole(core::at(own.more, before)),
        whole(check ? own.sums : own.counters),
        whole(own.counters)};
    constexpr std::array<uint32_t, 13> kBufferBindings{kBindingGeometry,    kBindingBlock,      kBindingLights,
                                                       kBindingEmitters,    kBindingLamps,      kBindingReservoirsA,
                                                       kBindingReservoirsB, kBindingNowAt,      kBindingNowMore,
                                                       kBindingBeforeAt,    kBindingBeforeMore, kBindingSums,
                                                       kBindingCounters};
    std::array<VkDescriptorImageInfo, 3> images{};
    if (inputs != nullptr) {
        images = {VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->depth->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                  VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->lamps->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                  VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->lamps_on->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL}};
    }
    std::vector<VkWriteDescriptorSet> writes;
    const auto add = [&writes](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                               const VkDescriptorImageInfo* image, const void* next) {
        VkWriteDescriptorSet one{};
        one.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        one.pNext = next;
        one.dstBinding = binding;
        one.descriptorCount = 1;
        one.descriptorType = type;
        one.pBufferInfo = buffer;
        one.pImageInfo = image;
        writes.push_back(one);
    };
    add(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, &scene_info);
    for (std::size_t k = 0; k < buffers.size(); ++k) {
        add(core::at(kBufferBindings, k),
            core::at(kBufferBindings, k) == kBindingBlock ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            &core::at(buffers, k), nullptr, nullptr);
    }
    if (inputs != nullptr) {
        add(kBindingDepth, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &core::at(images, 0), nullptr);
        add(kBindingLampsOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &core::at(images, 1), nullptr);
        add(kBindingLampsOnOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &core::at(images, 2), nullptr);
    }
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    const std::size_t variant = check ? 1U : 0U;
    const uint32_t groups_x = (width + kGroupSide - 1) / kGroupSide;
    const uint32_t groups_y = (height + kGroupSide - 1) / kGroupSide;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.temporal, variant));
    vkCmdDispatch(commands, groups_x, groups_y, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.spatial, variant));
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
        for (Image* image : {inputs->depth, inputs->lamps, inputs->lamps_on}) {
            image->used = serial;
        }
    }
    // this frame's surfaces and reservoirs are the next's history
    own.history = true;
    own.history_layout = own.layout_key;
    own.history_check = check;
    if (invertible) {
        own.prev_to_pixel = column_major(to_pixel);
    }
    own.prev_eye = view.camera.position;
    ++own.passes;
    own.lit_view = !check;
    if (own.passes == 1 && first_report(state, kReportFirst)) {
        get_log("restir: lamps by ReSTIR DI - %ux%u, %u candidates a pixel, %u spatial neighbours; %u lamps, the emitting "
                "triangles %s",
                width, height, candidates, spatial, own.lamp_count, own.list_now >= 0 ? "listed" : "being scanned");
    }
    return true;
}

void install_restir_di() { rt::add_view_listener(&listener()); }

void uninstall_restir_di() { rt::remove_view_listener(&listener()); }

void release_restir_di(Backend& state)
{
    RestirState& own = restir_state();
    const VkDevice device = state.context.device();
    for (std::array<VkPipeline, 2>* set : {&own.temporal, &own.spatial}) {
        for (VkPipeline& pipeline : *set) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
    }
    if (own.scan != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, own.scan, nullptr);
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
    for (DeviceBuffer* buffer : {&core::at(own.emitters, 0), &core::at(own.emitters, 1), &own.membership, &own.slots,
                                 &own.read_back, &own.lights, &own.lamps, &own.reservoirs_a, &own.reservoirs_b,
                                 &core::at(own.at, 0), &core::at(own.at, 1), &core::at(own.more, 0), &core::at(own.more, 1),
                                 &own.sums, &own.counters}) {
        state.memory.destroy_buffer(buffer);
    }
    const RestirOverrides overrides = own.overrides;
    own = RestirState{};
    own.overrides = overrides;
}

} // namespace render::vulkan
