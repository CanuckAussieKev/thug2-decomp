// NOT ORIGINAL - the reference path tracer (vulkan_path_reference.h; path tracing plan, stage 1).
#include "gpu_vulkan/vulkan_path_reference.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
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
#include "engine/file/disk.h"
#include "engine/gfx/light_store.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "platform/run_log.h"
#include "platform/screenshot_files.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNoScene = 0x50540001U;
constexpr uint64_t kReportTargets = 0x50540002U;
constexpr uint64_t kReportPipeline = 0x50540003U;
constexpr uint64_t kReportBuffers = 0x50540004U;
constexpr uint64_t kReportNoMaterials = 0x50540005U;
constexpr uint64_t kReportList = 0x50540006U;

constexpr uint32_t kBindingScene = 0;
constexpr uint32_t kBindingGeometry = 1;
constexpr uint32_t kBindingSums = 2;
constexpr uint32_t kBindingBlock = 3;
constexpr uint32_t kBindingEmitters = 4;
constexpr uint32_t kBindingMembership = 5;
constexpr uint32_t kBindingLamps = 6;
constexpr uint32_t kBindingOut = 7;
constexpr uint32_t kBindingTransmittance = 8;
constexpr uint32_t kBindingSkyView = 9;
constexpr uint32_t kBindingLights = 10;     // the light list's alias table (rt_light_list.h AliasEntry)
constexpr uint32_t kBindingSkyCells = 11;   // the sky's grid: its radiance per cell
constexpr uint32_t kBindingSkyAlias = 12;   // the sky's alias tables: the rows, then each row's cells
constexpr uint32_t kBindingCloudMap = 13;   // the clouds' shadow map (the sun's light through them)
constexpr uint32_t kBindingSlots = 14;      // per traced triangle: its place in the emitters' list + 1 (0: not listed)
constexpr uint32_t kBindingCloudNoise = 15; // .. 17: the clouds' noise (the sky's march)
constexpr uint32_t kCloudNoiseTextures = 3;
constexpr uint32_t kBindingCount = 18;
constexpr uint32_t kMaterialSet = 1;
constexpr uint32_t kGroupSide = 8;  // the trace's and the output's work groups: 8 x 8 pixels
constexpr uint32_t kScanGroup = 64; // the emitters' scan: 64 triangles a group
constexpr uint32_t kMostGroupsX = 65535;
constexpr uint32_t kEmitterCapacity = 1U << 20U; // emitting triangles listed at most (16 bytes each)
constexpr uint32_t kHeaderWords = 4;             // the list's header: count, -, samples not finite, -
constexpr uint32_t kEntryWords = 4;
constexpr VkDeviceSize kUniformAlignment = 256;
constexpr std::size_t kLampFloats = 16; // a LightRecord (vulkan_rt_lights.h rt_lamp_glsl)
constexpr std::size_t kMatrixSide = 4;
constexpr double kSingular = 1e-300;
constexpr float kHalfFloatMost = 65504.0F;
constexpr float kNoSun = 2.0F;        // (a cosine no direction reaches: no sun)
constexpr float kTraceReach = 1.0e6F; // a ray's reach (world units)
constexpr float kSameEye = 1.0e-3F;   // the camera stood still: its eye within this (world units) ...
constexpr float kSamePixels = 0.25F;  // ... and its rays within this share of a pixel
constexpr double kDegreesToRadians = 0.017453292519943295;

using Matrix = std::array<std::array<double, kMatrixSide>, kMatrixSide>;
using Vec3d = std::array<double, 3>;

// The block every shader of the pass reads (std140).
struct PathBlock {
    std::array<float, 4> eye{};          // xyz; w unused
    std::array<float, 4> corner{};       // toward the viewport's top-left corner (not normalised)
    std::array<float, 4> across{};       // per pixel right
    std::array<float, 4> down{};         // per pixel down
    std::array<float, 4> sun{};          // xyz toward the sun; w the cosine of its radius (kNoSun: none)
    std::array<float, 4> sun_radiance{}; // rgb the disc's radiance; w its solid angle
    std::array<float, 4> sky{};          // rgb an even sky's radiance above the horizon; w 1: the table's
    std::array<float, 4> sky_table{};    // xyz the table's sun; w the factor on its values
    std::array<float, 4> force{};        // x roughness, y F0, z the albedo's scale, w 1: forced
    std::array<float, 4> output{};       // x the factor on the mean written out (the frame's pre-exposure)
    std::array<uint32_t, 4> counts{};    // x instances, y lamps, z triangles, w the bits' first word
    std::array<uint32_t, 4> frame{};     // x the first sample's index, y samples now, z bounces at most, w strategy
    std::array<uint32_t, 4> size{};      // x width, y height, z seed, w the list's capacity
    std::array<uint32_t, 4> instances{}; // xy the instances' device address (low, high)
    std::array<uint32_t, 4> lists{};     // x the light list's entries, y its first lamp, z 1: the clouds' shadow map
    std::array<float, 4> sky_dist{};     // x 1 / the sky's luminance integral over the sphere (0: no sky)
    std::array<float, 4> cloud_place{};  // (the clouds' shadow map: vulkan_rt_sky_pass.h CloudShadowMap)
    std::array<float, 4> cloud_light{};
    std::array<float, 4>
        cloud_ray{}; // (the sky's march) xy the clouds' offset (metres), z the camera's altitude, w coverage
    std::array<float, 4> cloud_sun{};    // rgb the light on the clouds (exposed), w the march's steps
    std::array<float, 4> cloud_toward{}; // xyz toward that light, w the density's scale
};
static_assert(sizeof(PathBlock) == 21 * 16);

// --- the shaders -------------------------------------------------------------------------------------------------------

// What every shader of the pass declares after the libraries (vulkan_rt_scene.h's tables and hit_surface, rt_path.h).
constexpr const char* kCommon = R"(
layout(std430, set = 0, binding = 2) buffer Sums { vec4 sums[]; };
layout(std140, set = 0, binding = 3) uniform PathBlock {
    vec4 eye;
    vec4 corner;
    vec4 across;
    vec4 down;
    vec4 sun;
    vec4 sun_radiance;
    vec4 sky;
    vec4 sky_table;
    vec4 force;
    vec4 output_scale;
    uvec4 counts;
    uvec4 frame;
    uvec4 size;
    uvec4 instance_address;
    uvec4 lists;
    vec4 sky_dist;
    vec4 cloud_place;
    vec4 cloud_light;
    vec4 cloud_ray;
    vec4 cloud_sun;
    vec4 cloud_toward;
} pass;
layout(std430, set = 0, binding = 4) buffer Emitters {
    uint emitter_count;
    uint emitter_spare;
    uint not_finite;
    uint emitter_pad;
    uvec4 emitter_entries[]; // instance, primitive, power (float bits), -
};
layout(std430, set = 0, binding = 5) readonly buffer Membership { uint membership[]; };
layout(std430, set = 0, binding = 6) readonly buffer Lamps { LightRecord lamps[]; };
struct LightEntry {
    float threshold;
    uint alias;
    float pdf;
    uint payload;
};
layout(std430, set = 0, binding = 10) readonly buffer Lights { LightEntry light_entries[]; };
layout(std430, set = 0, binding = 11) buffer SkyCells { vec4 sky_cells[]; };
layout(std430, set = 0, binding = 12) readonly buffer SkyAlias { uvec2 sky_alias[]; };
layout(std430, set = 0, binding = 14) buffer Slots { uint slots[]; };
layout(buffer_reference, buffer_reference_align = 4, scalar) readonly buffer PathInstanceWords { float words[]; };
const uint STRATEGY_MIS = 0u;
const uint STRATEGY_LIGHTS = 1u;
const uint STRATEGY_BSDF = 2u;
const uint PATH_MASK = 7u;          // world, objects, alpha-tested (vulkan_rt_scene.h's masks)
const uint PATH_CASTS = 4u;         // (rt::kCastsShadow)
const uint PATH_TRANSPARENT = 64u;  // (rt::kTransparent)
const float PATH_REACH = 1.0e6;

// An instance's object-to-world transform (its TLAS record's rows).
mat4x3 instance_transform(uint i)
{
    PathInstanceWords w = PathInstanceWords(pass.instance_address.xy);
    uint at = i * 16u;
    vec4 r0 = vec4(w.words[at], w.words[at + 1u], w.words[at + 2u], w.words[at + 3u]);
    vec4 r1 = vec4(w.words[at + 4u], w.words[at + 5u], w.words[at + 6u], w.words[at + 7u]);
    vec4 r2 = vec4(w.words[at + 8u], w.words[at + 9u], w.words[at + 10u], w.words[at + 11u]);
    return mat4x3(vec3(r0.x, r1.x, r2.x), vec3(r0.y, r1.y, r2.y), vec3(r0.z, r1.z, r2.z), vec3(r0.w, r1.w, r2.w));
}
// An instance's material id (its TLAS record's custom index).
uint instance_material(uint i)
{
    PathInstanceWords w = PathInstanceWords(pass.instance_address.xy);
    return floatBitsToUint(w.words[i * 16u + 12u]) & 0xFFFFFFu;
}
// The triangle `primitive` of `instance`: its place in the emitters' list + 1 (0: not listed).
uint listed_slot(uint instance, uint primitive)
{
    if (instance >= pass.counts.x || primitive >= membership[instance + 1u] - membership[instance]) {
        return 0u;
    }
    return slots[membership[instance] + primitive];
}
)";

// The emitters' scan (once a run, before its first paths): one invocation a triangle of the frame's instances
// (flattened by the instances' first triangles, membership[0 .. instances]). A triangle of a world instance that casts
// and isn't seen through, whose light store emits at a corner (not hidden, not animated), is measured - its power,
// 2 pi x its area x its emission's mean luminance at the centroids of its TRIANGLE_LATTICE^2 equal sub-triangles
// (hit_surface's `emitted`: the store's emit x the lamp texels) - and listed when it sends any. (The list's header word
// 1 counts the candidates: those emitting at a corner.)
constexpr const char* kScanBody = R"(
layout(local_size_x = 64) in;
float emitter_power(uint instance, uint primitive)
{
    uint id = instance_material(instance);
    mat4x3 to_world = instance_transform(instance);
    float sum = 0.0;
    float area = 0.0;
    const float n = float(TRIANGLE_LATTICE);
    for (uint i = 0u; i < TRIANGLE_LATTICE; ++i) {
        for (uint j = 0u; i + j < TRIANGLE_LATTICE; ++j) {
            // the upright sub-triangle (i, j) and, but on the last row, the one turned over beside it
            for (uint turned = 0u; turned < (i + j + 1u < TRIANGLE_LATTICE ? 2u : 1u); ++turned) {
                float c = turned == 0u ? 1.0 / 3.0 : 2.0 / 3.0;
                HitSurface s;
                if (hit_surface(instance, primitive, id, vec2((float(i) + c) / n, (float(j) + c) / n), to_world,
                                vec3(0.0, -1.0, 0.0), s)) {
                    area = s.area;
                    if (!s.cut_out && !s.hidden) {
                        sum += path_luma(max(s.emitted, vec3(0.0)));
                    }
                }
            }
        }
    }
    return 2.0 * PATH_PI * area * sum / (n * n);
}
void main()
{
    uint g = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * (gl_NumWorkGroups.x * 64u);
    if (g >= pass.counts.z) {
        return;
    }
    uint low = 0u;
    uint high = pass.counts.x;
    while (high - low > 1u) {
        uint middle = (low + high) >> 1;
        if (membership[middle] <= g) {
            low = middle;
        } else {
            high = middle;
        }
    }
    uint instance = low;
    uint primitive = g - membership[instance];
    GeometryRecord record = geometry_record(instance);
    if ((record.flags & GEOMETRY_LIVE) == 0u || (record.flags & GEOMETRY_LIGHT) == 0u ||
        (record.instance_flags & HIT_INSTANCE_OBJECT) != 0u || (record.instance_flags & PATH_CASTS) == 0u ||
        (record.instance_flags & PATH_TRANSPARENT) != 0u) {
        return;
    }
    float most = 0.0;
    for (uint k = 0u; k < 3u; ++k) {
        uint at = geometry_corner(record, primitive, k) * 32u;
        vec4 flags = unpackUnorm4x8(geometry_word(record.light_address, at + 24u)).zyxw;
        if (flags.a < 0.75) {
            return; // (no store at an animated vertex; a hidden mesh)
        }
        vec3 e = stored_light(geometry_word(record.light_address, at + 28u));
        most = max(most, max(e.r, max(e.g, e.b)));
    }
    if (!(most > 0.0)) {
        return;
    }
    atomicAdd(emitter_spare, 1u);
    float power = emitter_power(instance, primitive);
    if (!(power > 0.0) || isinf(power)) {
        return; // (no lamp texel at its points: not listed - a BSDF ray finds it, weighed 1)
    }
    uint slot = atomicAdd(emitter_count, 1u);
    if (slot < pass.size.w) {
        emitter_entries[slot] = uvec4(instance, primitive, floatBitsToUint(power), 0u);
        slots[g] = slot + 1u;
    }
}
)";

// The sky's grid (once a run): each cell's radiance toward its centre in the global unit - the sky-view table x the
// clouds' transmittance + their light (the same march, noise and light as the drawn clouds, faded with distance as the
// composite fades them) over the table's exposure; without drawn clouds the table x the lighting state's kept share of
// the sky (DayLight::cloud_sky); without the table an even sky above the horizon.
constexpr const char* kSkyBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    uvec2 c = gl_GlobalInvocationID.xy;
    if (c.x >= SKY_WIDTH || c.y >= SKY_HEIGHT) {
        return;
    }
    vec3 dir = sky_cell_centre(c);
    vec3 radiance;
#ifdef SKY_TABLE
    vec3 table = max(sky_view_linear(dir, pass.sky_table.xyz), vec3(0.0));
#ifdef CLOUDS
    CloudRay ray;
    ray.camera_altitude = pass.cloud_ray.z;
    ray.offset = pass.cloud_ray.xy;
    ray.direction = dir;
    ray.to_sun = pass.cloud_toward.xyz;
    ray.coverage = pass.cloud_ray.w;
    ray.density_scale = pass.cloud_toward.w;
    ray.steps = int(pass.cloud_sun.w);
    ray.jitter = 0.5;
    ray.sun_light = pass.cloud_sun.rgb;
    vec3 bottom;
    vec3 top;
    cloud_sky_light(bottom, top);
    ray.ambient_bottom = bottom;
    ray.ambient_top = top;
    CloudResult r = march_clouds(ray);
    float fade = exp(-max(r.distance, 0.0) / CLOUD_AERIAL_DISTANCE);
    float kept = mix(1.0, clamp(r.transmittance, 0.0, 1.0), fade);
    radiance = (table * kept + max(r.light, vec3(0.0)) * fade) * pass.sky_table.w;
#else
    radiance = table * pass.sky_table.w;
#endif
#else
    radiance = dir.y > 0.0 ? pass.sky.rgb : vec3(0.0);
#endif
    if (any(isnan(radiance)) || any(isinf(radiance))) {
        radiance = vec3(0.0);
    }
    sky_cells[c.y * SKY_WIDTH + c.x] = vec4(radiance, 0.0);
}
)";

// The trace: `frame.y` paths a pixel added into its sums.
constexpr const char* kTraceBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
const uint LIST_DISC = 0u;
const uint LIST_SKY = 1u;
const uint LIST_TRIANGLES = 2u;

float ray_offset(vec3 p) { return 0.02 + 2.0e-5 * max(abs(p.x), max(abs(p.y), abs(p.z))); }

// The closest hit along a ray (cut-outs by their alpha); false: none before `reach`.
bool trace_closest(vec3 origin, vec3 dir, float reach, out uint instance, out uint primitive, out uint id,
                   out vec2 barycentrics, out float t, out mat4x3 to_world)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsNoneEXT, PATH_MASK, origin, 0.0, dir, reach);
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

bool blocked(vec3 origin, vec3 dir, float reach) { return ray_blocked(PATH_MASK, origin, dir, reach); }

bool in_sun(vec3 dir) { return pass.sun.w <= 1.0 && dot(dir, pass.sun.xyz) >= pass.sun.w; }

// The share of the disc's light the clouds let through to `at` (their shadow map over the lighting state's mean beam -
// the shadows pass's clouds_through); 1 without the map.
float sun_kept(vec3 at)
{
#ifdef CLOUD_MAP
    return cloud_map_kept(at, pass.cloud_place, pass.cloud_light);
#else
    return 1.0;
#endif
}

// The sky's radiance toward `dir` (the sun's disc not in it): its grid's cell.
vec3 sky_radiance(vec3 dir) { return pass.sky_dist.x > 0.0 ? sky_cells[sky_cell_of(dir)].rgb : vec3(0.0); }
// The light list's pdf (solid angle) of `dir` by the sky's sampling: the sky's pick x its cell's luminance / the integral.
float sky_light_pdf(vec3 dir)
{
    if (!(pass.sky_dist.x > 0.0)) {
        return 0.0;
    }
    return light_entries[LIST_SKY].pdf * path_luma(sky_cells[sky_cell_of(dir)].rgb) * pass.sky_dist.x;
}

BrdfMaterial surface_material(HitSurface s)
{
    if (pass.force.w > 0.5) {
        return brdf_material(s.base_colour * pass.force.z, vec3(pass.force.y), pass.force.x);
    }
    return s.material;
}

// One light picked from the list by its power (the alias table) and sampled from `p` (normal n, the triangle's ng, toward
// the eye v): its light x the BSDF x N.L / its pdf (the pick's x the direction's), the MIS weight against the BSDF's pdf
// in the MIS strategy.
vec3 sample_light(vec3 p, vec3 n, vec3 ng, vec3 v, BrdfMaterial m, inout uint rng)
{
    float u = path_next(rng);
    float u1 = path_next(rng);
    float u2 = path_next(rng);
    float u3 = path_next(rng);
    float u4 = path_next(rng);
    uint count = pass.lists.x;
    if (count == 0u) {
        return vec3(0.0);
    }
    LightEntry e = light_entries[light_pick(u, count)];
    if (!(e.pdf > 0.0)) {
        return vec3(0.0);
    }
    bool mis = pass.frame.w == STRATEGY_MIS;
    uint kind = light_kind(e.payload);
    if (kind == LIGHT_DISC) {
        vec3 dir = path_to_world(pass.sun.xyz, path_cone_direction(pass.sun.w, u1, u2));
        if (dot(dir, ng) <= 0.0) {
            return vec3(0.0);
        }
        vec3 f = path_bsdf_cosine(m, n, v, dir);
        if (max(f.r, max(f.g, f.b)) <= 0.0 || blocked(p, dir, PATH_REACH)) {
            return vec3(0.0);
        }
        float pdf = e.pdf / pass.sun_radiance.w;
        float w = mis ? path_power_heuristic(pdf, path_bsdf_pdf(m, n, v, dir)) : 1.0;
        return f * pass.sun_radiance.rgb * sun_kept(p) * (w / pdf);
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
        if (max(f.r, max(f.g, f.b)) <= 0.0 || blocked(p, dir, PATH_REACH)) {
            return vec3(0.0);
        }
        float w = mis ? path_power_heuristic(pdf, path_bsdf_pdf(m, n, v, dir)) : 1.0;
        return f * radiance * (w / pdf);
    }
    if (kind == LIGHT_TRIANGLE) {
        uvec4 entry = emitter_entries[light_index(e.payload)];
        HitVertices tri;
        if (!hit_vertices(entry.x, entry.y, tri)) {
            return vec3(0.0);
        }
        mat4x3 to_world = instance_transform(entry.x);
        vec3 b = path_triangle_point(u1, u2);
        vec3 y = to_world * vec4(tri.position[0] * b.x + tri.position[1] * b.y + tri.position[2] * b.z, 1.0);
        vec3 to = y - p;
        float d = length(to);
        if (!(d > 0.0)) {
            return vec3(0.0);
        }
        vec3 dir = to / d;
        if (dot(dir, ng) <= 0.0) {
            return vec3(0.0);
        }
        vec3 f = path_bsdf_cosine(m, n, v, dir);
        if (max(f.r, max(f.g, f.b)) <= 0.0) {
            return vec3(0.0);
        }
        uint hit_instance;
        uint hit_primitive;
        uint hit_id;
        vec2 hit_bary;
        float t;
        mat4x3 hit_world;
        if (!trace_closest(p, dir, d * 1.001 + ray_offset(y), hit_instance, hit_primitive, hit_id, hit_bary, t, hit_world) ||
            hit_instance != entry.x || hit_primitive != entry.y) {
            return vec3(0.0);
        }
        HitSurface s;
        if (!hit_surface(hit_instance, hit_primitive, hit_id, hit_bary, hit_world, dir, s) || !(s.area > 0.0)) {
            return vec3(0.0);
        }
        float cos_light = abs(dot(s.normal, dir));
        if (!(cos_light > 0.0)) {
            return vec3(0.0);
        }
        float pdf = e.pdf / s.area * t * t / cos_light;
        float w = mis ? path_power_heuristic(pdf, path_bsdf_pdf(m, n, v, dir)) : 1.0;
        return f * s.emitted * (w / pdf);
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
    if (!(facing > 1.0e-4) || dot(dir, ng) <= 0.0 || blocked(p, dir, max(d - light.at.w, 0.0))) {
        return vec3(0.0);
    }
    // (a lamp's light: rt_lights.h's model - its light on the surface x the light model, as the lamps pass shines it)
    return brdf_direct(m, n, v, dir, light.colour.rgb * (g / facing)) / e.pdf;
}

vec3 trace_path(vec3 origin, vec3 dir, inout uint rng)
{
    vec3 radiance = vec3(0.0);
    vec3 weight = vec3(1.0);
    float last_pdf = 0.0;
    uint strategy = pass.frame.w;
    uint skipped = 0u;
    for (uint depth = 0u; depth <= pass.frame.z; ++depth) {
        uint instance;
        uint primitive;
        uint id;
        vec2 bary;
        float t;
        mat4x3 to_world;
        bool hit = trace_closest(origin, dir, PATH_REACH, instance, primitive, id, bary, t, to_world);
        if (!hit) {
            vec3 seen_sky = sky_radiance(dir);
            bool sun_seen = in_sun(dir);
            vec3 seen_sun = sun_seen ? pass.sun_radiance.rgb * sun_kept(origin) : vec3(0.0);
            if (depth == 0u || strategy == STRATEGY_BSDF) {
                radiance += weight * (seen_sky + seen_sun);
            } else if (strategy == STRATEGY_MIS) {
                float sun_pdf = sun_seen ? light_entries[LIST_DISC].pdf / pass.sun_radiance.w : 0.0;
                radiance += weight * (seen_sky * path_power_heuristic(last_pdf, sky_light_pdf(dir)) +
                                      seen_sun * path_power_heuristic(last_pdf, sun_pdf));
            }
            break;
        }
        HitSurface s;
        if (!hit_surface(instance, primitive, id, bary, to_world, dir, s)) {
            break;
        }
        if (s.hidden && skipped < 4u) {
            // (a mesh the light store hides - the bake's shadows drawn as geometry: not there)
            origin = s.position + dir * ray_offset(s.position);
            ++skipped;
            --depth;
            continue;
        }
        if (max(s.emitted.r, max(s.emitted.g, s.emitted.b)) > 0.0) {
            uint slot = listed_slot(instance, primitive);
            if (depth == 0u || strategy == STRATEGY_BSDF || slot == 0u) {
                radiance += weight * s.emitted;
            } else if (strategy == STRATEGY_MIS) {
                float cos_light = abs(dot(s.normal, dir));
                float light_pdf = cos_light > 0.0 && s.area > 0.0 ?
                    light_entries[LIST_TRIANGLES + slot - 1u].pdf / s.area * t * t / cos_light : 0.0;
                radiance += weight * s.emitted * path_power_heuristic(last_pdf, light_pdf);
            }
        }
        if (depth == pass.frame.z) {
            break;
        }
        vec3 v = -dir;
        vec3 ng = s.normal;
        vec3 n = dot(s.shading_normal, v) > 0.0 ? s.shading_normal : ng;
        BrdfMaterial m = surface_material(s);
        vec3 p = s.position + ng * ray_offset(s.position);
        if (strategy != STRATEGY_BSDF) {
            radiance += weight * sample_light(p, n, ng, v, m, rng);
        }
        float u0 = path_next(rng);
        float u1 = path_next(rng);
        float u2 = path_next(rng);
        vec3 l;
        float pdf;
        if (!path_sample_bsdf(m, n, v, u0, u1, u2, l, pdf) || dot(l, ng) <= 0.0) {
            break;
        }
        weight *= path_bsdf_cosine(m, n, v, l) / pdf;
        last_pdf = pdf;
        origin = p;
        dir = l;
        if (depth + 1u >= ROULETTE_FROM) {
            float keep = clamp(max(weight.r, max(weight.g, weight.b)), ROULETTE_LEAST, ROULETTE_MOST);
            if (path_next(rng) >= keep) {
                break;
            }
            weight /= keep;
        }
    }
    return radiance;
}

void main()
{
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= pass.size.x || p.y >= pass.size.y) {
        return;
    }
    vec3 sum = vec3(0.0);
    uint bad = 0u;
    for (uint i = 0u; i < pass.frame.y; ++i) {
        uint rng = path_sequence_start(p.x, p.y, pass.frame.x + i, pass.size.z);
        float jx = path_next(rng);
        float jy = path_next(rng);
        vec3 dir = normalize(pass.corner.xyz + pass.across.xyz * (float(p.x) + jx) + pass.down.xyz * (float(p.y) + jy));
        vec3 one = trace_path(pass.eye.xyz, dir, rng);
        if (any(isnan(one)) || any(isinf(one))) {
            ++bad;
        } else {
            sum += one;
        }
    }
    uint at = p.y * pass.size.x + p.x;
    sums[at] += vec4(sum, float(pass.frame.y));
    if (bad > 0u) {
        atomicAdd(not_finite, bad);
    }
}
)";

// The output: each pixel's mean x the frame's pre-exposure, into the image copied over the viewport.
constexpr const char* kOutputBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_out;
void main()
{
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= pass.size.x || p.y >= pass.size.y) {
        return;
    }
    vec4 s = sums[p.y * pass.size.x + p.x];
    vec3 mean = s.a > 0.0 ? s.rgb / s.a : vec3(0.0);
    imageStore(o_out, ivec2(p), vec4(min(max(mean * pass.output_scale.x, vec3(0.0)), vec3(65504.0)), 1.0));
}
)";

enum class Shader : uint8_t { kScan, kSky, kTrace, kOutput };
// The sky's variants: an even sky (no table), the sky-view table, the table with the clouds marched.
enum class SkySource : uint8_t { kEven = 0, kTable = 1, kTableClouds = 2 };
constexpr std::size_t kSkySources = 3;

// A shader's source: `sky` picks the sky shader's variant, `cloud_map` the trace's (the clouds' shadow map on the sun).
std::string shader_source(Shader which, SkySource sky, bool cloud_map)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    if (which == Shader::kSky && sky != SkySource::kEven) {
        source += "#define SKY_TABLE\n" + atmosphere_glsl(kBindingTransmittance, kBindingSkyView);
        if (sky == SkySource::kTableClouds) {
            source += "#define CLOUDS\n" + cloud_glsl(kBindingCloudNoise) + rt_sky_cloud_light_glsl();
            source += "const float CLOUD_AERIAL_DISTANCE = " + std::to_string(kCloudAerialDistance) + ";\n";
        }
    }
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
    if (which == Shader::kTrace && cloud_map) {
        source += "#define CLOUD_MAP\n" + cloud_map_glsl(kBindingCloudMap);
    }
    source += "const uint ROULETTE_FROM = " + std::to_string(rt::path::kRouletteFrom) + "u;\n";
    source += "const float ROULETTE_LEAST = " + std::to_string(rt::path::kRouletteLeast) + ";\n";
    source += "const float ROULETTE_MOST = " + std::to_string(rt::path::kRouletteMost) + ";\n";
    source += "const uint SKY_WIDTH = " + std::to_string(rt::lights::kSkyGridWidth) + "u;\n";
    source += "const uint SKY_HEIGHT = " + std::to_string(rt::lights::kSkyGridHeight) + "u;\n";
    source += "const uint TRIANGLE_LATTICE = " + std::to_string(rt::lights::kTriangleLattice) + "u;\n";
    source += kCommon;
    source += rt::lights::kLightListGlsl;
    switch (which) {
    case Shader::kScan:
        source += kScanBody;
        break;
    case Shader::kSky:
        source += kSkyBody;
        break;
    case Shader::kTrace:
        source += kTraceBody;
        break;
    case Shader::kOutput:
        source += kOutputBody;
        break;
    }
    return source;
}

// --- the lighting --------------------------------------------------------------------------------------------------------

struct Lighting {
    std::array<float, 3> sun_toward{0.0F, 1.0F, 0.0F};
    std::array<float, 3> sun_light{};
    float sun_radius_degrees = rt::kSunRadiusDegrees;
    std::array<float, 3> sky{}; // (an even sky's radiance: without the table)
    bool table = false;
    AtmosphereInput table_input;
    float table_factor = 1.0F;      // x the table: 1 / its exposure (x the kept sky without drawn clouds)
    bool clouds = false;            // the sky's grid marches the drawn clouds
    RtSkyFrame cloud_frame;         // (their parameters - the sky pass's last)
    std::vector<rt::Emitter> lamps; // the analytic ones (x their switches) and the frame record's point lights
    uint32_t owned = 0;
    uint32_t points = 0;
    uint64_t cut = 0; // (rt_daylight.h DayLight::cut)
};

// The frame's lighting state (the one every pass reads): the relit world's (rt_cel.h cel_world - its sun per N.L and its
// sky in the global unit), else the clock's (rt_daylight.h, its pre-exposure divided out); the sky-view table's sky
// where it is made (with the drawn clouds marched, else x the clouds' kept share with the clock); the level's lamps
// without a surface of their own; the frame record's point lights where the level's lamps aren't solved (rt_lights.h
// LevelEmitters::solved - as the lamps pass shines them, x the open sky's lamps).
Lighting frame_lighting(const PathOverrides* overrides)
{
    Lighting out;
    if (overrides != nullptr) {
        out.sun_toward = overrides->sun_toward;
        out.sun_light = overrides->sun_light;
        out.sun_radius_degrees = overrides->sun_radius_degrees;
        out.sky = overrides->sky;
        out.lamps = rt::path::analytic_lamps(overrides->lamps, overrides->lamps_surfaced);
        out.owned = static_cast<uint32_t>(overrides->lamps.size() - out.lamps.size());
        return out;
    }
    const rt::DayLight day = rt::day_light();
    const rt::CelWorld world = rt::cel_world();
    const float pre = day.pre_exposure > 0.0F ? day.pre_exposure : 1.0F;
    out.cut = day.cut;
    if (world.relit) {
        out.sun_toward = world.toward;
        out.sun_light = world.sun;
        out.sky = world.sky;
    } else if (day.on) {
        out.sun_toward = day.light;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(out.sun_light, ch) = core::at(day.sun_light, ch) / pre;
            core::at(out.sky, ch) = core::at(day.sky_light, ch) / pre;
        }
    }
    if (day.on) {
        out.sun_radius_degrees = day.light_radius;
    }
    AtmosphereInput input;
    if (atmosphere_view(&input) && input.exposure > 0.0F) {
        out.table = true;
        out.table_input = input;
        out.cloud_frame = rt_sky_frame();
        out.clouds = out.cloud_frame.clouds;
        out.table_factor = (out.clouds || !day.on ? 1.0F : day.cloud_sky) / input.exposure;
    }
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    const float open = rt::open_sky_lamps(day, level.get());
    if (level) {
        const std::vector<rt::Emitter> analytic = rt::path::analytic_lamps(level->emitters, level->surfaced);
        out.owned = static_cast<uint32_t>(level->emitters.size() - analytic.size());
        const float sky_now = rt::lamp_sky_now(day, level.get());
        for (rt::Emitter lamp : analytic) {
            lamp.intensity *= rt::lamp_on(lamp.switch_sky, sky_now, open);
            if (lamp.intensity > 0.0F) {
                out.lamps.push_back(lamp);
            }
        }
    }
    if (!level || !level->solved) {
        for (const rt::Light& light : rt::frame_record().lights) {
            rt::Emitter lamp;
            if (rt::lights::point_emitter(light, std::clamp(open, 0.0F, 1.0F), &lamp) && lamp.intensity > 0.0F) {
                out.lamps.push_back(lamp);
                ++out.points;
            }
        }
    }
    return out;
}

// What a run's lighting is (a change starts it again - the lighting's cut, another light switched): the cut, the direct
// light, the sky, the lamps.
uint64_t lighting_key_of(const Lighting& lighting)
{
    uint64_t key = 1469598103934665603ULL;
    const auto mix = [&key](const void* data, std::size_t bytes) {
        const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), bytes);
        for (const uint8_t byte : from) {
            key = (key ^ byte) * 1099511628211ULL;
        }
    };
    mix(&lighting.cut, sizeof(lighting.cut));
    mix(lighting.sun_toward.data(), sizeof(lighting.sun_toward));
    mix(lighting.sun_light.data(), sizeof(lighting.sun_light));
    mix(&lighting.sun_radius_degrees, sizeof(lighting.sun_radius_degrees));
    mix(lighting.sky.data(), sizeof(lighting.sky));
    mix(&lighting.table, sizeof(lighting.table));
    mix(&lighting.clouds, sizeof(lighting.clouds));
    mix(&lighting.table_factor, sizeof(lighting.table_factor));
    for (const rt::Emitter& lamp : lighting.lamps) {
        mix(lamp.position.data(), sizeof(lamp.position));
        mix(&lamp.intensity, sizeof(lamp.intensity));
    }
    return key;
}

std::array<float, kLampFloats> lamp_record(const rt::Emitter& lamp)
{
    return {core::at(lamp.position, 0),
            core::at(lamp.position, 1),
            core::at(lamp.position, 2),
            lamp.size,
            core::at(lamp.colour, 0) * lamp.intensity,
            core::at(lamp.colour, 1) * lamp.intensity,
            core::at(lamp.colour, 2) * lamp.intensity,
            lamp.reach,
            0.0F,
            lamp.core,
            static_cast<float>(static_cast<uint32_t>(lamp.shape)),
            0.0F,
            core::at(lamp.axis, 0),
            core::at(lamp.axis, 1),
            core::at(lamp.axis, 2),
            lamp.half_length};
}

// The world's part of the scene (its instances' meshes and materials, in order, at their places among the frame's
// instances - the emitters' list names a triangle by its instance's place): what a reference is of. The objects (the
// skater, a menu's props) may come and go while the game is paused - their instances don't start a run again.
uint64_t scene_key_of(const rt::FrameRecord& record)
{
    uint64_t key = 1469598103934665603ULL;
    uint64_t place = 0;
    for (const rt::Instance& instance : record.instances) {
        ++place;
        if ((instance.flags & rt::kWorld) == 0) {
            continue;
        }
        key = (key ^ place) * 1099511628211ULL;
        key = (key ^ instance.mesh) * 1099511628211ULL;
        key = (key ^ instance.material) * 1099511628211ULL;
    }
    return key;
}

// --- the state ---------------------------------------------------------------------------------------------------------

// The camera's rays as the trace takes them.
struct RayFrame {
    std::array<float, 3> eye{};
    std::array<float, 3> corner{};
    std::array<float, 3> across{};
    std::array<float, 3> down{};
};

// A run's light list: gathered on the device in its first frame (the emitting triangles with their power, the world's
// bounds, the sky's grid), read back and made into the alias tables in the next, then traced.
enum class ListPhase : uint8_t { kGather, kRead, kReady };

struct PathState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> trace{}; // without / with the clouds' shadow map
    std::array<VkPipeline, kSkySources> sky{};
    VkPipeline scan = VK_NULL_HANDLE;
    VkPipeline output = VK_NULL_HANDLE;
    bool failed = false;
    DeviceBuffer sums;
    DeviceBuffer emitters;
    DeviceBuffer membership; // (processor-written: the instances' first triangles)
    DeviceBuffer slots;      // (cleared on the device; the scan writes it)
    DeviceBuffer lamps;
    DeviceBuffer lights;    // (processor-written: the light list's alias table)
    DeviceBuffer sky_cells; // (the sky shader writes it)
    DeviceBuffer sky_alias; // (processor-written)
    Image out;
    Image realtime;
    uint32_t width = 0;
    uint32_t height = 0;
    // the run: what it is frozen at, how far it is
    bool running = false;
    uint64_t request_serial = 0;
    uint64_t overrides_serial = 0;
    RayFrame rays;
    std::array<uint32_t, 4> rect{};
    uint64_t scene_key = 0;
    uint64_t lighting_key = 0;
    Lighting lighting;
    SkySource sky_source = SkySource::kEven;
    bool cloud_map = false;
    rt::lights::LightWeights weights = rt::lights::LightWeights::kPower;
    ListPhase phase = ListPhase::kGather;
    uint32_t list_count = 0;
    uint32_t first_lamp = 0;
    uint32_t listed = 0; // (emitting triangles in the list)
    float sky_inverse = 0.0F;
    uint32_t instances = 0;
    uint32_t triangles = 0;
    uint32_t lamp_count = 0;
    uint32_t lamps_owned = 0; // (the level's lamps sampled as their surfaces instead)
    uint32_t samples = 0;
    uint32_t wanted = 0;
    float ev100 = 0.0F;
    bool manual_set = false;
    bool realtime_taken = false;
    float realtime_exposure = 1.0F; // the frame's pre-exposure the realtime copy holds
    bool saved = false;
    std::string sky_text;
    std::string list_text;
    std::string saved_stem;
    uint64_t passes = 0;
    bool restart_asked = false;
    PathOverrides overrides;
};

PathState& path_state()
{
    static PathState made;
    return made;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class PathListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return kPathReferenceOrder; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_path_reference_pass(state, view);
        }
    }
};

PathListener& listener()
{
    static PathListener instance;
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

// The layouts, and the pipelines a run needs (each made on first use): the scan, the sky's for its source, the trace with
// or without the clouds' shadow map, the output.
bool ensure_pipelines(Backend& state, SkySource sky, bool cloud_map, std::string* error)
{
    PathState& own = path_state();
    const VkDevice device = state.context.device();
    if (own.set_layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if (binding == kBindingScene) {
                type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            } else if (binding == kBindingBlock) {
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if (binding == kBindingOut) {
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            } else if (binding == kBindingTransmittance || binding == kBindingSkyView || binding == kBindingCloudMap ||
                       (binding >= kBindingCloudNoise && binding < kBindingCloudNoise + kCloudNoiseTextures)) {
                type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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
        own.scan = make_compute(device, own.layout, shader_source(Shader::kScan, SkySource::kEven, false), error);
        if (own.scan == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.output == VK_NULL_HANDLE) {
        own.output = make_compute(device, own.layout, shader_source(Shader::kOutput, SkySource::kEven, false), error);
        if (own.output == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& sky_pipeline = core::at(own.sky, static_cast<std::size_t>(sky));
    if (sky_pipeline == VK_NULL_HANDLE) {
        sky_pipeline = make_compute(device, own.layout, shader_source(Shader::kSky, sky, false), error);
        if (sky_pipeline == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& trace = core::at(own.trace, cloud_map ? 1U : 0U);
    if (trace == VK_NULL_HANDLE) {
        trace = make_compute(device, own.layout, shader_source(Shader::kTrace, SkySource::kEven, cloud_map), error);
    }
    return trace != VK_NULL_HANDLE;
}

// --- the camera ----------------------------------------------------------------------------------------------------------

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

// The view's rays: the camera's eye and, over `rect` (the viewport as shown), the ray toward its top-left corner and its
// steps a pixel right and down - from the view and the projection with this frame's jitter taken out (the jitter is in
// pixels of the drawn rect, view.rect: vulkan_motion.cpp's unjittered). False: the matrices can't be inverted.
bool rays_of(const rt::ViewDrawn& view, const std::array<uint32_t, 4>& rect, RayFrame* out)
{
    const double drawn_width = std::max(1U, core::at(view.rect, 2));
    const double drawn_height = std::max(1U, core::at(view.rect, 3));
    const double jitter_x = 2.0 * static_cast<double>(core::at(view.jitter, 0)) / drawn_width;
    const double jitter_y = -2.0 * static_cast<double>(core::at(view.jitter, 1)) / drawn_height;
    Matrix projection{};
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            core::at(core::at(projection, row), column) = core::at(core::at(view.camera.projection, row), column);
        }
        core::at(core::at(projection, row), 0) -= jitter_x * core::at(core::at(projection, row), 3);
        core::at(core::at(projection, row), 1) -= jitter_y * core::at(core::at(projection, row), 3);
    }
    Matrix clip{}; // row vectors: clip = world x view x projection
    for (std::size_t row = 0; row < kMatrixSide; ++row) {
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                sum += static_cast<double>(core::at(core::at(view.camera.view, row), k)) *
                       core::at(core::at(projection, k), column);
            }
            core::at(core::at(clip, row), column) = sum;
        }
    }
    Matrix world{};
    if (!invert(clip, &world)) {
        return false;
    }
    // a point of the plane half way through the depth range at (x, y) of the viewport's clip space: its world position
    const auto at = [&world](double x, double y) {
        const std::array<double, 4> in{x, y, 0.5, 1.0};
        std::array<double, 4> out{};
        for (std::size_t column = 0; column < kMatrixSide; ++column) {
            for (std::size_t k = 0; k < kMatrixSide; ++k) {
                core::at(out, column) += core::at(in, k) * core::at(core::at(world, k), column);
            }
        }
        const double w = std::abs(core::at(out, 3)) > kSingular ? core::at(out, 3) : 1.0;
        return Vec3d{core::at(out, 0) / w, core::at(out, 1) / w, core::at(out, 2) / w};
    };
    const Vec3d top_left = at(-1.0, 1.0);
    const Vec3d top_right = at(1.0, 1.0);
    const Vec3d bottom_left = at(-1.0, -1.0);
    const double width = std::max(1U, core::at(rect, 2));
    const double height = std::max(1U, core::at(rect, 3));
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double eye = core::at(view.camera.position, axis);
        core::at(out->eye, axis) = static_cast<float>(eye);
        core::at(out->corner, axis) = static_cast<float>(core::at(top_left, axis) - eye);
        core::at(out->across, axis) =
            static_cast<float>((core::at(top_right, axis) - core::at(top_left, axis)) / width);
        core::at(out->down, axis) =
            static_cast<float>((core::at(bottom_left, axis) - core::at(top_left, axis)) / height);
    }
    return true;
}

float length_of(const std::array<float, 3>& v)
{
    return std::sqrt((core::at(v, 0) * core::at(v, 0)) + (core::at(v, 1) * core::at(v, 1)) +
                     (core::at(v, 2) * core::at(v, 2)));
}

// Whether two views' rays are the same camera: the eye within kSameEye, the corner within kSamePixels of a pixel.
bool same_rays(const RayFrame& one, const RayFrame& other)
{
    float eye = 0.0F;
    float corner = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        eye = std::max(eye, std::abs(core::at(one.eye, axis) - core::at(other.eye, axis)));
        corner = std::max(corner, std::abs(core::at(one.corner, axis) - core::at(other.corner, axis)) +
                                      std::abs(core::at(one.across, axis) - core::at(other.across, axis)) * 1000.0F +
                                      std::abs(core::at(one.down, axis) - core::at(other.down, axis)) * 1000.0F);
    }
    return eye <= kSameEye && corner <= kSamePixels * std::max(length_of(one.across), 1e-12F);
}

// --- the captures --------------------------------------------------------------------------------------------------------

void put_bytes(std::vector<uint8_t>& out, const void* data, std::size_t bytes)
{
    const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), bytes);
    out.insert(out.end(), from.begin(), from.end());
}
void put_text(std::vector<uint8_t>& out, const char* text) { put_bytes(out, text, std::strlen(text) + 1); }
template <typename T>
void put_value(std::vector<uint8_t>& out, T value)
{
    put_bytes(out, &value, sizeof(value));
}
void put_attribute(std::vector<uint8_t>& out, const char* name, const char* type, const std::vector<uint8_t>& value)
{
    put_text(out, name);
    put_text(out, type);
    put_value(out, static_cast<int32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

// An OpenEXR file (scanlines, no compression, 32-bit float B, G, R - the format's alphabetical order) of linear RGB.
std::vector<uint8_t> exr_bytes(uint32_t width, uint32_t height, const std::vector<std::array<float, 3>>& rgb)
{
    std::vector<uint8_t> out;
    put_value(out, static_cast<int32_t>(20000630)); // (the magic number)
    put_value(out, static_cast<int32_t>(2));        // (version 2, single part scanlines)
    std::vector<uint8_t> channels;
    for (const char* name : {"B", "G", "R"}) {
        put_text(channels, name);
        put_value(channels, static_cast<int32_t>(2)); // FLOAT
        put_value(channels, static_cast<int32_t>(0)); // pLinear, reserved
        put_value(channels, static_cast<int32_t>(1)); // x sampling
        put_value(channels, static_cast<int32_t>(1)); // y sampling
    }
    channels.push_back(0);
    put_attribute(out, "channels", "chlist", channels);
    put_attribute(out, "compression", "compression", {0});
    std::vector<uint8_t> window;
    put_value(window, static_cast<int32_t>(0));
    put_value(window, static_cast<int32_t>(0));
    put_value(window, static_cast<int32_t>(width) - 1);
    put_value(window, static_cast<int32_t>(height) - 1);
    put_attribute(out, "dataWindow", "box2i", window);
    put_attribute(out, "displayWindow", "box2i", window);
    put_attribute(out, "lineOrder", "lineOrder", {0});
    std::vector<uint8_t> aspect;
    put_value(aspect, 1.0F);
    put_attribute(out, "pixelAspectRatio", "float", aspect);
    std::vector<uint8_t> centre;
    put_value(centre, 0.0F);
    put_value(centre, 0.0F);
    put_attribute(out, "screenWindowCenter", "v2f", centre);
    put_attribute(out, "screenWindowWidth", "float", aspect);
    out.push_back(0); // (the header's end)
    const std::size_t line_bytes = static_cast<std::size_t>(width) * 3 * sizeof(float);
    const std::size_t table_at = out.size();
    const std::size_t first_line = table_at + (static_cast<std::size_t>(height) * sizeof(uint64_t));
    for (uint32_t y = 0; y < height; ++y) {
        put_value(out, static_cast<uint64_t>(first_line + (static_cast<std::size_t>(y) * (8 + line_bytes))));
    }
    for (uint32_t y = 0; y < height; ++y) {
        put_value(out, static_cast<int32_t>(y));
        put_value(out, static_cast<int32_t>(line_bytes));
        for (const std::size_t channel : {std::size_t{2}, std::size_t{1}, std::size_t{0}}) {
            for (uint32_t x = 0; x < width; ++x) {
                const std::size_t at = (static_cast<std::size_t>(y) * width) + x;
                put_value(out, at < rgb.size() ? core::at(core::at(rgb, at), channel) : 0.0F);
            }
        }
    }
    return out;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes)
{
    fs::DiskStream* const out = fs::disk().open_stream(path.c_str(), "wb", fs::StreamSharing::kAll);
    if (out == nullptr) {
        return false;
    }
    const std::size_t written = fs::disk().stream_write(out, bytes.data(), 1, bytes.size());
    return fs::disk().stream_close(out) == 0 && written == bytes.size();
}

// A picture of linear light (the global unit) through the tone curve at `ev100` (as the frame's pass maps it), as PNG.
bool write_png(const std::string& path, uint32_t width, uint32_t height, const std::vector<std::array<float, 3>>& rgb,
               float ev100)
{
    ToneParameters tone = tone_parameters_locked();
    tone.scale *= exposure_at_ev100(ev100);
    const GradeLut* lut = grade_lut_locked();
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t i = 0; i < rgb.size() && (i * 3) + 2 < pixels.size(); ++i) {
        const std::array<float, 3> shown = tone_map_reference(tone, lut, core::at(rgb, i));
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(pixels, (i * 3) + ch) =
                static_cast<uint8_t>(std::lround(std::clamp(core::at(shown, ch), 0.0F, 1.0F) * 255.0F));
        }
    }
    std::vector<uint8_t> png;
    return sys::screenshot::encode_png(pixels.data(), static_cast<int>(width), static_cast<int>(height), &png) &&
           write_file(path, png);
}

float half_to_float_value(uint16_t half) { return half_to_float(half); }

// The reference and the realtime frame saved (the frame's work finished first).
void save_captures(Backend& state, PathState& own)
{
    PathAccumulation reference;
    if (!read_path_accumulation(state, &reference)) {
        get_log("pt: the reference couldn't be read back - nothing saved");
        own.saved = true;
        return;
    }
    std::vector<std::array<float, 3>> realtime;
    std::vector<uint8_t> texels;
    if (own.realtime_taken &&
        state.memory.read_image_level(&own.realtime, 0,
                                      &(texels = std::vector<uint8_t>(static_cast<std::size_t>(own.width) * own.height *
                                                                      4 * sizeof(uint16_t))))) {
        realtime.resize(static_cast<std::size_t>(own.width) * own.height);
        for (std::size_t i = 0; i < realtime.size(); ++i) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(texels, ((i * 4) + ch) * sizeof(uint16_t)), sizeof(half));
                core::at(core::at(realtime, i), ch) = half_to_float_value(half) / own.realtime_exposure;
            }
        }
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const sys::screenshot::Stamp stamp{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                       local.tm_hour,        local.tm_min,     local.tm_sec};
    std::string stem = sys::screenshot::file_stem(stamp); // thug2_YYYY-MM-DD_HH-MM-SS
    stem.replace(0, std::string("thug2").size(), "pt");
    const std::string folder = game_folder_path("Screenshots");
    fs::disk().make_folder(folder.c_str());
    const std::string base = folder + "\\" + stem;
    bool ok = write_file(base + "_reference.exr", exr_bytes(reference.width, reference.height, reference.mean)) &&
              write_png(base + "_reference.png", reference.width, reference.height, reference.mean, own.ev100);
    if (!realtime.empty()) {
        ok = write_file(base + "_realtime.exr", exr_bytes(own.width, own.height, realtime)) &&
             write_png(base + "_realtime.png", own.width, own.height, realtime, own.ev100) && ok;
    }
    // the two's means (luma) and their ratio - what the realtime tier's light is against the reference
    double reference_sum = 0.0;
    double realtime_sum = 0.0;
    for (std::size_t i = 0; i < reference.mean.size(); ++i) {
        reference_sum += rt::path::luma(core::at(reference.mean, i));
        if (i < realtime.size()) {
            realtime_sum += rt::path::luma(core::at(realtime, i));
        }
    }
    const double pixels = std::max<double>(1.0, static_cast<double>(reference.mean.size()));
    get_log(
        "pt: saved %s_reference / _realtime (.exr, .png) %s - %ux%u, %u samples a pixel, EV100 %.2f; %u emitting "
        "triangles listed, %u lamps sampled analytically (%u by their surfaces), sky %s, %s; %u samples not finite; "
        "mean luma reference %.5g, realtime %.5g (x %.4f of the reference)",
        base.c_str(), ok ? "" : "(a file couldn't be written)", reference.width, reference.height, reference.samples,
        static_cast<double>(own.ev100), own.listed, own.lamp_count, own.lamps_owned, own.sky_text.c_str(),
        own.weights == rt::lights::LightWeights::kPower ? "lights by power" : "lights evenly by kind",
        reference.not_finite, reference_sum / pixels, realtime_sum / pixels,
        reference_sum > 0.0 ? realtime_sum / reference_sum : 0.0);
    own.saved = true;
    own.saved_stem = base;
}

void publish_status(const PathState& own, const std::string& note)
{
    rt::path::PathReferenceStatus status;
    status.serial = own.request_serial;
    status.running = own.running;
    status.samples = own.samples;
    status.wanted = own.wanted;
    status.ev100 = own.ev100;
    status.saved = own.saved_stem;
    status.note = note;
    rt::path::set_path_reference_status(std::move(status));
}

void stop_run(PathState& own)
{
    if (own.manual_set) {
        set_manual_ev100(false, 0.0F);
        own.manual_set = false;
    }
    own.running = false;
}

// A storage buffer of at least `bytes` (made again larger when it isn't).
bool ensure_buffer(Backend& state, DeviceBuffer* buffer, VkDeviceSize bytes, VkBufferUsageFlags usage,
                   BufferPlace place)
{
    if (buffer->buffer != VK_NULL_HANDLE && buffer->bytes >= bytes) {
        return true;
    }
    retire_buffer(state, buffer);
    return state.memory.create_buffer(bytes, usage, place, buffer);
}

bool ensure_image(Backend& state, Image* image, uint32_t width, uint32_t height, VkImageUsageFlags usage,
                  VkImageLayout resting)
{
    if (image->image != VK_NULL_HANDLE && image->extent.width == width && image->extent.height == height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = kHdrFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = width, .height = height},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    image->resting = resting;
    prepare_image(state, image);
    return true;
}

void barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags from_access, VkPipelineStageFlags to,
             VkAccessFlags to_access)
{
    const VkMemoryBarrier memory{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = from_access,
                                 .dstAccessMask = to_access};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}

// The disc of the lighting's direct light as the block takes it: toward it + the cosine of its radius, and its radiance
// + its solid angle. False: none (no light, or no direction).
bool disc_of(const Lighting& lighting, std::array<float, 4>* sun, std::array<float, 4>* radiance)
{
    const float most =
        std::max({core::at(lighting.sun_light, 0), core::at(lighting.sun_light, 1), core::at(lighting.sun_light, 2)});
    const auto cos_radius =
        static_cast<float>(std::cos(static_cast<double>(lighting.sun_radius_degrees) * kDegreesToRadians));
    const float toward_length = length_of(lighting.sun_toward);
    if (!(most > 0.0F) || !(toward_length > 0.0F) || !(cos_radius < 1.0F)) {
        *sun = {0.0F, 1.0F, 0.0F, kNoSun};
        *radiance = {0.0F, 0.0F, 0.0F, 1.0F};
        return false;
    }
    *sun = {core::at(lighting.sun_toward, 0) / toward_length, core::at(lighting.sun_toward, 1) / toward_length,
            core::at(lighting.sun_toward, 2) / toward_length, cos_radius};
    const rt::brdf::Rgb disc = rt::path::disc_radiance(lighting.sun_light, cos_radius);
    *radiance = {core::at(disc, 0), core::at(disc, 1), core::at(disc, 2), rt::path::cone_solid_angle(cos_radius)};
    return true;
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

// The run's light list made (its gather finished first: the work waited for): the emitting triangles with their power,
// the world's radius, the sky's grid read back; the alias tables built (rt_light_list.h) and uploaded.
bool build_run_list(Backend& state, PathState& own)
{
    if (!finish_work(state)) {
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!state.memory.read_buffer(own.emitters, 0, kHeaderWords * sizeof(uint32_t), &bytes) ||
        bytes.size() < kHeaderWords * sizeof(uint32_t)) {
        return false;
    }
    std::array<uint32_t, kHeaderWords> words{};
    std::memcpy(words.data(), bytes.data(), sizeof(words));
    const uint32_t listed = std::min(core::at(words, 0), kEmitterCapacity);
    rt::lights::LightSet set;
    set.triangle_powers.resize(listed);
    if (listed > 0) {
        if (!state.memory.read_buffer(own.emitters, kHeaderWords * sizeof(uint32_t),
                                      static_cast<VkDeviceSize>(listed) * kEntryWords * sizeof(uint32_t), &bytes) ||
            bytes.size() < static_cast<std::size_t>(listed) * kEntryWords * sizeof(uint32_t)) {
            return false;
        }
        for (std::size_t i = 0; i < listed; ++i) {
            std::memcpy(&core::at(set.triangle_powers, i), &core::at(bytes, ((i * kEntryWords) + 2) * sizeof(uint32_t)),
                        sizeof(float));
        }
    }
    const std::size_t cells = static_cast<std::size_t>(rt::lights::kSkyGridWidth) * rt::lights::kSkyGridHeight;
    if (!state.memory.read_buffer(own.sky_cells, 0, cells * 4 * sizeof(float), &bytes) ||
        bytes.size() < cells * 4 * sizeof(float)) {
        return false;
    }
    std::vector<std::array<float, 4>> radiance(cells);
    std::memcpy(radiance.data(), bytes.data(), cells * 4 * sizeof(float));
    const rt::lights::SkyDistribution sky =
        rt::lights::build_sky_distribution(radiance, rt::lights::kSkyGridWidth, rt::lights::kSkyGridHeight);
    std::array<float, 4> sun{};
    std::array<float, 4> disc{};
    if (disc_of(own.lighting, &sun, &disc)) {
        set.disc_light = own.lighting.sun_light;
        set.disc_cos = core::at(sun, 3);
    }
    set.sky_integral = sky.integral;
    set.lamps = own.lighting.lamps;
    const rt::lights::LightList list = rt::lights::build_light_list(set, own.weights);
    std::vector<std::array<uint32_t, 2>> alias;
    alias.reserve(sky.rows.size() + sky.cells.size());
    for (const std::vector<rt::lights::AliasEntry>* table : {&sky.rows, &sky.cells}) {
        for (const rt::lights::AliasEntry& entry : *table) {
            uint32_t threshold = 0;
            std::memcpy(&threshold, &entry.threshold, sizeof(threshold));
            alias.push_back({threshold, entry.alias});
        }
    }
    if (!upload_buffer(state, &own.lights, list.table.entries.data(),
                       list.table.entries.size() * sizeof(rt::lights::AliasEntry)) ||
        !upload_buffer(state, &own.sky_alias, alias.data(), alias.size() * sizeof(alias.front()))) {
        return false;
    }
    own.list_count = static_cast<uint32_t>(list.table.entries.size());
    own.first_lamp = list.first_lamp;
    own.listed = listed;
    own.sky_inverse = sky.integral > 0.0 ? static_cast<float>(1.0 / sky.integral) : 0.0F;
    const auto share = [&list](std::size_t kind) { return 100.0 * core::at(list.kind_pick, kind); };
    own.list_text =
        text::format("light list (%s): %u lights - the disc (power %.4g), the sky (%.4g, integral %.4g), %u emitting "
                     "triangles%s (of %u emitting at a corner; %.4g), "
                     "%u lamps (%u point lights; %.4g); picked %.1f / %.1f / %.1f / %.1f %%",
                     own.weights == rt::lights::LightWeights::kPower ? "by power" : "the kinds evenly", own.list_count,
                     core::at(list.kind_power, 0), core::at(list.kind_power, 1), sky.integral, listed,
                     core::at(words, 0) > kEmitterCapacity ? " (the list full)" : "", core::at(words, 1),
                     core::at(list.kind_power, 2),
                     static_cast<uint32_t>(set.lamps.size()), own.lighting.points, core::at(list.kind_power, 3),
                     share(0), share(1), share(2), share(3));
    return true;
}

} // namespace

void set_path_overrides(const PathOverrides& overrides)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    PathState& own = path_state();
    own.overrides = overrides;
    ++own.overrides_serial;
}

void restart_path_reference()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    path_state().restart_asked = true;
}

uint64_t path_reference_passes() { return path_state().passes; }

bool read_path_accumulation(Backend& state, PathAccumulation* out)
{
    *out = PathAccumulation{};
    PathState& own = path_state();
    if (own.sums.buffer == VK_NULL_HANDLE || own.width == 0 || own.height == 0 || !finish_work(state)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(own.width) * own.height;
    std::vector<uint8_t> bytes;
    if (!state.memory.read_buffer(own.sums, 0, pixels * 4 * sizeof(float), &bytes) ||
        bytes.size() < pixels * 4 * sizeof(float)) {
        return false;
    }
    out->width = own.width;
    out->height = own.height;
    out->mean.resize(pixels);
    float samples = 0.0F;
    for (std::size_t i = 0; i < pixels; ++i) {
        std::array<float, 4> sum{};
        std::memcpy(sum.data(), &core::at(bytes, i * sizeof(sum)), sizeof(sum));
        const float count = std::max(core::at(sum, 3), 1.0F);
        samples = std::max(samples, core::at(sum, 3));
        core::at(out->mean, i) = {core::at(sum, 0) / count, core::at(sum, 1) / count, core::at(sum, 2) / count};
    }
    out->samples = static_cast<uint32_t>(samples);
    std::vector<uint8_t> header;
    if (own.emitters.buffer != VK_NULL_HANDLE &&
        state.memory.read_buffer(own.emitters, 0, kHeaderWords * sizeof(uint32_t), &header) &&
        header.size() >= kHeaderWords * sizeof(uint32_t)) {
        std::array<uint32_t, kHeaderWords> words{};
        std::memcpy(words.data(), header.data(), sizeof(words));
        out->emitters = std::min(core::at(words, 0), kEmitterCapacity);
        out->not_finite = core::at(words, 2);
    }
    return true;
}

bool record_path_reference_pass(Backend& state, const rt::ViewDrawn& view)
{
    PathState& own = path_state();
    const rt::path::PathReferenceRequest request = rt::path::path_reference_request();
    const bool checking = own.overrides.on;
    if (!checking && request.samples == 0) {
        if (own.running || own.manual_set) {
            stop_run(own);
            publish_status(own, "off");
        }
        return false;
    }
    if (own.failed) {
        return false;
    }
    if (!state.context.ray_tracing_enabled() || rt::ray_tracing() == nullptr) {
        if (first_report(state, kReportNoScene)) {
            get_log("pt: no ray tracing on this device - no reference");
        }
        publish_status(own, "no ray tracing");
        return false;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.geometry == VK_NULL_HANDLE || scene.instances_address == 0) {
        if (first_report(state, kReportNoScene)) {
            get_log("pt: no scene built yet - the reference waits");
        }
        publish_status(own, "no scene yet");
        return false;
    }
    if (!state.materials.ready() || state.materials.descriptor_set() == VK_NULL_HANDLE) {
        if (first_report(state, kReportNoMaterials)) {
            get_log("pt: no material table - no reference");
        }
        return false;
    }
    // (the image the bound target draws into: a render target's, a texture's level 0 - as the tone mapping finds it)
    Surface* const target = core::at(state.targets, 0);
    Image* colour = nullptr;
    if (target != nullptr && target->kind == SurfaceKind::kRenderTarget) {
        colour = &target->image;
    } else if (target != nullptr && target->kind == SurfaceKind::kTextureLevel && target->level == 0) {
        colour = &target->texture->image;
    }
    if (colour == nullptr || colour->format != kHdrFormat || colour->samples != VK_SAMPLE_COUNT_1_BIT ||
        (colour->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) !=
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        if (first_report(state, kReportTargets)) {
            get_log(
                "pt: the view's target can't take the reference (%s, format %d, %d samples, usage 0x%x) - none there",
                colour == nullptr ? "no colour image" : "an image",
                colour != nullptr ? static_cast<int>(colour->format) : 0,
                colour != nullptr ? static_cast<int>(colour->samples) : 0, colour != nullptr ? colour->usage : 0U);
        }
        publish_status(own, "the target isn't single-sampled float");
        return false;
    }
    // the viewport as shown (after the upscaler), inside the target
    std::array<uint32_t, 4> rect = core::at(view.output_rect, 2) > 0 ? view.output_rect : view.rect;
    core::at(rect, 0) = std::min(core::at(rect, 0), colour->extent.width);
    core::at(rect, 1) = std::min(core::at(rect, 1), colour->extent.height);
    core::at(rect, 2) = std::min(core::at(rect, 2), colour->extent.width - core::at(rect, 0));
    core::at(rect, 3) = std::min(core::at(rect, 3), colour->extent.height - core::at(rect, 1));
    if (core::at(rect, 2) == 0 || core::at(rect, 3) == 0) {
        return false;
    }
    RayFrame rays;
    if (!rays_of(view, rect, &rays)) {
        return false;
    }
    const uint64_t key = scene_key_of(rt::frame_record());
    const Lighting lighting = frame_lighting(checking ? &own.overrides : nullptr);
    const uint64_t lighting_key = lighting_key_of(lighting);
    const rt::lights::LightWeights weights =
        checking ? own.overrides.light_weights
                 : (request.even_lights ? rt::lights::LightWeights::kKindsEvenly : rt::lights::LightWeights::kPower);
    // the clouds' shadow map on the disc's light (made this frame for this viewport - the shadows pass's)
    CloudShadowMap cloud_map;
    const bool map_now = !checking && cloud_shadow_map(state, &cloud_map);

    // a new run: another request, the camera, the scene or the lighting changed, the check's restart
    // (once saved, a run is kept - shown - until another request or `pt off`)
    const bool asked_again = !own.running || own.request_serial != (checking ? own.overrides_serial : request.serial);
    const bool moved = own.rect != rect || own.scene_key != key || !same_rays(own.rays, rays);
    const bool relit = own.lighting_key != lighting_key;
    const bool fresh = asked_again || own.restart_asked || (!own.saved && (moved || relit));
    std::string note;
    if (fresh) {
        if (own.running && own.request_serial == (checking ? own.overrides_serial : request.serial) &&
            !own.restart_asked) {
            note = own.scene_key != key ? "the scene changed - started again"
                   : relit              ? "the lighting changed - started again"
                                        : "the camera moved - started again";
            get_log("pt: %s", note.c_str());
        }
        own.running = true;
        own.restart_asked = false;
        own.request_serial = checking ? own.overrides_serial : request.serial;
        own.rect = rect;
        own.rays = rays;
        own.scene_key = key;
        own.lighting_key = lighting_key;
        own.lighting = lighting;
        own.weights = weights;
        own.sky_source =
            !lighting.table ? SkySource::kEven : (lighting.clouds ? SkySource::kTableClouds : SkySource::kTable);
        own.cloud_map = map_now;
        own.phase = ListPhase::kGather;
        own.list_count = 0;
        own.width = core::at(rect, 2);
        own.height = core::at(rect, 3);
        own.samples = 0;
        own.wanted = checking ? 0xFFFFFFFFU : request.samples;
        own.saved = false;
        own.saved_stem.clear();
        own.realtime_taken = false;
    }
    std::string error;
    if (!ensure_pipelines(state, own.sky_source, own.cloud_map, &error)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("pt: the reference's shaders couldn't be made (%s)", error.c_str());
        }
        return false;
    }
    if (own.material_layout != state.materials.layout()) {
        return false;
    }
    if (fresh) {
        const std::size_t pixels = static_cast<std::size_t>(own.width) * own.height;
        const VkBufferUsageFlags storage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // the instances' first triangles (each one's place in the slots), as the TLAS holds them now - the scene key
        // starts the run again when the world's instances move among them
        std::vector<uint32_t> triangles;
        scene_instance_triangles(state, &triangles);
        own.instances = static_cast<uint32_t>(triangles.size());
        std::vector<uint32_t> firsts(triangles.size() + 1, 0);
        uint64_t total = 0;
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            core::at(firsts, i) = static_cast<uint32_t>(total);
            total += core::at(triangles, i);
        }
        total = std::min<uint64_t>(total, 0x3FFFFFFFULL);
        firsts.back() = static_cast<uint32_t>(total);
        own.triangles = static_cast<uint32_t>(total);
        std::vector<float> lamp_floats;
        lamp_floats.reserve(std::max<std::size_t>(own.lighting.lamps.size(), 1) * kLampFloats);
        for (const rt::Emitter& lamp : own.lighting.lamps) {
            const std::array<float, kLampFloats> record = lamp_record(lamp);
            lamp_floats.insert(lamp_floats.end(), record.begin(), record.end());
        }
        own.lamp_count = static_cast<uint32_t>(own.lighting.lamps.size());
        own.lamps_owned = own.lighting.owned;
        const std::array<rt::lights::AliasEntry, 2> no_lights{};
        const std::array<uint32_t, 2> no_alias{};
        const std::size_t cells = static_cast<std::size_t>(rt::lights::kSkyGridWidth) * rt::lights::kSkyGridHeight;
        const bool made =
            ensure_buffer(state, &own.sums, pixels * 4 * sizeof(float), storage, BufferPlace::kDevice) &&
            ensure_buffer(state, &own.emitters,
                          (kHeaderWords + (static_cast<VkDeviceSize>(kEntryWords) * kEmitterCapacity)) *
                              sizeof(uint32_t),
                          storage, BufferPlace::kDevice) &&
            ensure_buffer(state, &own.slots, std::max<VkDeviceSize>(total, 1) * sizeof(uint32_t), storage,
                          BufferPlace::kDevice) &&
            ensure_buffer(state, &own.sky_cells, cells * 4 * sizeof(float), storage, BufferPlace::kDevice) &&
            upload_buffer(state, &own.membership, firsts.data(), firsts.size() * sizeof(uint32_t)) &&
            upload_buffer(state, &own.lamps, lamp_floats.data(), lamp_floats.size() * sizeof(float)) &&
            upload_buffer(state, &own.lights, no_lights.data(), sizeof(no_lights)) &&
            upload_buffer(state, &own.sky_alias, no_alias.data(), sizeof(no_alias)) &&
            ensure_image(state, &own.out, own.width, own.height,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_LAYOUT_GENERAL) &&
            ensure_image(state, &own.realtime, own.width, own.height,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        if (!made) {
            own.running = false;
            if (first_report(state, kReportBuffers)) {
                get_log("pt: the reference's buffers (%ux%u) couldn't be made", own.width, own.height);
            }
            publish_status(own, "out of memory");
            return false;
        }
        const Lighting& lit = own.lighting;
        own.sky_text = lit.table ? text::format("the sky-view table (x %.4g)%s", static_cast<double>(lit.table_factor),
                                                lit.clouds ? " with the clouds marched" : "")
                                 : text::format("even, %.4g %.4g %.4g", static_cast<double>(core::at(lit.sky, 0)),
                                                static_cast<double>(core::at(lit.sky, 1)),
                                                static_cast<double>(core::at(lit.sky, 2)));
        if (!checking) {
            float ev = request.ev100;
            if (!request.ev_given && !last_metered_ev100(state, &ev)) {
                ev = camera_ev100(0.0F); // (no meter read yet: the camera's fixed day)
            }
            own.ev100 = ev;
            set_manual_ev100(true, ev);
            own.manual_set = true;
            get_log("pt: reference started - %ux%u, %u samples a pixel, EV100 %.2f; sun %.4g %.4g %.4g per N.L toward "
                    "(%.3f, %.3f, %.3f), radius %.3f degrees%s; sky %s; %u lamps sampled analytically (%u point "
                    "lights), %u "
                    "by their own surfaces; %u instances, %u triangles",
                    own.width, own.height, own.wanted, static_cast<double>(ev),
                    static_cast<double>(core::at(lit.sun_light, 0)), static_cast<double>(core::at(lit.sun_light, 1)),
                    static_cast<double>(core::at(lit.sun_light, 2)), static_cast<double>(core::at(lit.sun_toward, 0)),
                    static_cast<double>(core::at(lit.sun_toward, 1)), static_cast<double>(core::at(lit.sun_toward, 2)),
                    static_cast<double>(lit.sun_radius_degrees), own.cloud_map ? ", the clouds' shadow map on it" : "",
                    own.sky_text.c_str(), own.lamp_count, lit.points, own.lamps_owned, own.instances, own.triangles);
        }
    }
    // the light list: gathered in the run's first frame, built in its second (its work waited for), then traced
    if (own.phase == ListPhase::kRead) {
        if (!build_run_list(state, own)) {
            own.running = false;
            if (first_report(state, kReportList)) {
                get_log("pt: the light list couldn't be read back - no reference");
            }
            publish_status(own, "no light list");
            return false;
        }
        own.phase = ListPhase::kReady;
        if (!checking) {
            get_log("pt: %s", own.list_text.c_str());
        }
    }

    // the block
    const Lighting& lit = own.lighting;
    PathBlock block;
    block.eye = {core::at(own.rays.eye, 0), core::at(own.rays.eye, 1), core::at(own.rays.eye, 2), 0.0F};
    block.corner = {core::at(own.rays.corner, 0), core::at(own.rays.corner, 1), core::at(own.rays.corner, 2), 0.0F};
    block.across = {core::at(own.rays.across, 0), core::at(own.rays.across, 1), core::at(own.rays.across, 2), 0.0F};
    block.down = {core::at(own.rays.down, 0), core::at(own.rays.down, 1), core::at(own.rays.down, 2), 0.0F};
    (void)disc_of(lit, &block.sun, &block.sun_radiance);
    block.sky = {core::at(lit.sky, 0), core::at(lit.sky, 1), core::at(lit.sky, 2), lit.table ? 1.0F : 0.0F};
    block.sky_table = {core::at(lit.table_input.sun, 0), core::at(lit.table_input.sun, 1),
                       core::at(lit.table_input.sun, 2), lit.table_factor};
    if (checking && own.overrides.force_material) {
        block.force = {own.overrides.roughness, own.overrides.f0, own.overrides.albedo_scale, 1.0F};
    }
    block.output = {rt::frame_pre_exposure(), 0.0F, 0.0F, 0.0F};
    block.counts = {own.instances, own.lamp_count, own.triangles, 0U};
    const bool ready = own.phase == ListPhase::kReady && (!own.cloud_map || map_now);
    const uint32_t samples_now =
        ready && own.samples < own.wanted
            ? std::min(checking ? std::max(own.overrides.samples_per_frame, 1U) : 1U, own.wanted - own.samples)
            : 0U;
    const uint32_t bounces = checking ? own.overrides.bounces : std::min(request.bounces, rt::path::kMostBounces);
    block.frame = {own.samples, samples_now, bounces, checking ? static_cast<uint32_t>(own.overrides.strategy) : 0U};
    block.size = {own.width, own.height, checking ? own.overrides.seed : static_cast<uint32_t>(own.request_serial),
                  kEmitterCapacity};
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), 0U, 0U};
    block.lists = {own.list_count, own.first_lamp, own.cloud_map ? 1U : 0U, 0U};
    block.sky_dist = {own.sky_inverse, 0.0F, 0.0F, 0.0F};
    if (own.cloud_map && map_now) {
        block.cloud_place = cloud_map.place;
        block.cloud_light = cloud_map.light;
    }
    const RtSkyFrame& clouds = lit.cloud_frame;
    block.cloud_ray = {static_cast<float>(core::at(clouds.offset, 0)), static_cast<float>(core::at(clouds.offset, 1)),
                       static_cast<float>(clouds.camera_altitude), clouds.coverage};
    block.cloud_sun = {core::at(clouds.sun_light, 0), core::at(clouds.sun_light, 1), core::at(clouds.sun_light, 2),
                       static_cast<float>(std::max(clouds.steps, 1U))};
    block.cloud_toward = {core::at(clouds.cloud_toward, 0), core::at(clouds.cloud_toward, 1),
                          core::at(clouds.cloud_toward, 2), 1.0F};
    std::array<uint8_t, sizeof(PathBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> block_span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!block_span) {
        return false;
    }

    VkCommandBuffer commands = outside_pass(state);
    barrier(commands,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    // the realtime frame, before the reference is first written over it
    if (!checking && !own.realtime_taken) {
        Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Memory::transition(commands, &own.realtime, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkImageSubresourceLayers layers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
        const VkImageCopy region{.srcSubresource = layers,
                                 .srcOffset = {.x = static_cast<int32_t>(core::at(rect, 0)),
                                               .y = static_cast<int32_t>(core::at(rect, 1)),
                                               .z = 0},
                                 .dstSubresource = layers,
                                 .dstOffset = {.x = 0, .y = 0, .z = 0},
                                 .extent = {.width = own.width, .height = own.height, .depth = 1}};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.realtime.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        Memory::transition(commands, &own.realtime, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        own.realtime_taken = true;
        own.realtime_exposure = rt::frame_pre_exposure();
    }
    const bool gather = own.phase == ListPhase::kGather;
    if (fresh) {
        vkCmdFillBuffer(commands, own.sums.buffer, 0, VK_WHOLE_SIZE, 0);
    }
    if (gather) {
        // the list's header and the slots
        vkCmdFillBuffer(commands, own.emitters.buffer, 0, kHeaderWords * sizeof(uint32_t), 0);
        vkCmdFillBuffer(commands, own.slots.buffer, 0, VK_WHOLE_SIZE, 0);
    }
    barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    if (own.out.layout != VK_IMAGE_LAYOUT_GENERAL) {
        Memory::transition(commands, &own.out, VK_IMAGE_LAYOUT_GENERAL);
    }
    // the descriptors
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const auto whole = [](const DeviceBuffer& buffer) {
        return VkDescriptorBufferInfo{.buffer = buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    };
    const VkDescriptorBufferInfo geometry_info{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const VkDescriptorBufferInfo sums_info = whole(own.sums);
    const VkDescriptorBufferInfo block_info{
        .buffer = block_span->buffer, .offset = block_span->offset, .range = sizeof(PathBlock)};
    const VkDescriptorBufferInfo emitters_info = whole(own.emitters);
    const VkDescriptorBufferInfo membership_info = whole(own.membership);
    const VkDescriptorBufferInfo lamps_info = whole(own.lamps);
    const VkDescriptorBufferInfo lights_info = whole(own.lights);
    const VkDescriptorBufferInfo sky_cells_info = whole(own.sky_cells);
    const VkDescriptorBufferInfo sky_alias_info = whole(own.sky_alias);
    const VkDescriptorBufferInfo slots_info = whole(own.slots);
    const VkDescriptorImageInfo out_info{
        .sampler = VK_NULL_HANDLE, .imageView = own.out.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo transmittance_info = atmosphere_descriptor(AtmosphereTable::kTransmittance);
    const VkDescriptorImageInfo sky_view_info = atmosphere_descriptor(AtmosphereTable::kSkyView);
    std::array<VkDescriptorImageInfo, kCloudNoiseTextures> noise_info{};
    for (uint32_t i = 0; i < kCloudNoiseTextures; ++i) {
        core::at(noise_info, i) = cloud_noise_descriptor(i);
    }
    std::vector<VkWriteDescriptorSet> writes;
    const auto write = [&writes](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
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
    const auto bind_common = [&]() {
        writes.clear();
        write(kBindingScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, &scene_info);
        write(kBindingGeometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &geometry_info, nullptr, nullptr);
        write(kBindingSums, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sums_info, nullptr, nullptr);
        write(kBindingBlock, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &block_info, nullptr, nullptr);
        write(kBindingEmitters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &emitters_info, nullptr, nullptr);
        write(kBindingMembership, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &membership_info, nullptr, nullptr);
        write(kBindingLamps, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lamps_info, nullptr, nullptr);
        write(kBindingOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &out_info, nullptr);
        write(kBindingLights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lights_info, nullptr, nullptr);
        write(kBindingSkyCells, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sky_cells_info, nullptr, nullptr);
        write(kBindingSkyAlias, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sky_alias_info, nullptr, nullptr);
        write(kBindingSlots, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &slots_info, nullptr, nullptr);
    };
    const auto push = [&]() {
        vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                                  static_cast<uint32_t>(writes.size()), writes.data());
    };
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    const uint64_t serial = state.frames.serial();
    if (gather) {
        // 1. (the run's first frame) the emitting triangles with their power and the world's bounds; the sky's grid
        bind_common();
        push();
        if (own.triangles > 0) {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.scan);
            const uint32_t groups = (own.triangles + kScanGroup - 1) / kScanGroup;
            const uint32_t groups_x = std::min(groups, kMostGroupsX);
            vkCmdDispatch(commands, groups_x, (groups + groups_x - 1) / groups_x, 1);
        }
        bind_common();
        if (own.sky_source != SkySource::kEven) {
            write(kBindingTransmittance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &transmittance_info,
                  nullptr);
            write(kBindingSkyView, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sky_view_info, nullptr);
            atmosphere_used(serial);
        }
        if (own.sky_source == SkySource::kTableClouds) {
            for (uint32_t i = 0; i < kCloudNoiseTextures; ++i) {
                write(kBindingCloudNoise + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr,
                      &core::at(noise_info, i), nullptr);
            }
            cloud_noise_used(serial);
        }
        push();
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          core::at(own.sky, static_cast<std::size_t>(own.sky_source)));
        vkCmdDispatch(commands, (rt::lights::kSkyGridWidth + kGroupSide - 1) / kGroupSide,
                      (rt::lights::kSkyGridHeight + kGroupSide - 1) / kGroupSide, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        own.phase = ListPhase::kRead;
    }
    // 2. the paths
    if (samples_now > 0) {
        bind_common();
        if (own.cloud_map) {
            write(kBindingCloudMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &cloud_map.image, nullptr);
        }
        push();
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.trace, own.cloud_map ? 1U : 0U));
        vkCmdDispatch(commands, (own.width + kGroupSide - 1) / kGroupSide, (own.height + kGroupSide - 1) / kGroupSide,
                      1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        own.samples += samples_now;
    }
    // 3. the mean over the viewport (once there are samples: before, the realtime frame stays)
    if (own.samples > 0) {
        bind_common();
        push();
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.output);
        vkCmdDispatch(commands, (own.width + kGroupSide - 1) / kGroupSide, (own.height + kGroupSide - 1) / kGroupSide,
                      1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkImageSubresourceLayers layers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
        const VkImageCopy region{.srcSubresource = layers,
                                 .srcOffset = {.x = 0, .y = 0, .z = 0},
                                 .dstSubresource = layers,
                                 .dstOffset = {.x = static_cast<int32_t>(core::at(rect, 0)),
                                               .y = static_cast<int32_t>(core::at(rect, 1)),
                                               .z = 0},
                                 .extent = {.width = own.width, .height = own.height, .depth = 1}};
        vkCmdCopyImage(commands, own.out.image, VK_IMAGE_LAYOUT_GENERAL, colour->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    }
    state.bound = BoundState{};
    colour->used = serial;
    own.out.used = serial;
    own.realtime.used = serial;
    ++own.passes;
    if (!checking && own.samples >= own.wanted && !own.saved) {
        save_captures(state, own);
    }
    publish_status(own, note);
    return true;
}

void install_path_reference() { rt::add_view_listener(&listener()); }

void uninstall_path_reference() { rt::remove_view_listener(&listener()); }

void release_path_reference(Backend& state)
{
    PathState& own = path_state();
    const VkDevice device = state.context.device();
    for (VkPipeline& pipeline : own.trace) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
    for (VkPipeline& pipeline : own.sky) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
    for (VkPipeline* pipeline : {&own.scan, &own.output}) {
        if (*pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }
    if (own.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, own.layout, nullptr);
    }
    if (own.set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
    }
    for (DeviceBuffer* buffer : {&own.sums, &own.emitters, &own.membership, &own.slots, &own.lamps, &own.lights,
                                 &own.sky_cells, &own.sky_alias}) {
        state.memory.destroy_buffer(buffer);
    }
    state.memory.destroy_image(&own.out);
    state.memory.destroy_image(&own.realtime);
    if (own.manual_set) {
        set_manual_ev100(false, 0.0F);
    }
    const PathOverrides overrides = own.overrides;
    own = PathState{};
    own.overrides = overrides;
}

} // namespace render::vulkan
