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
#include "engine/gfx/gpu_markers.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_debug_view.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_light_list.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_pass_profile.h"
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
#include "gpu_vulkan/vulkan_restir_di.h"
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
constexpr uint32_t kBindingSpecularReservoirs = 16; // (round WS) 4 a pixel: the specular reservoir - this frame's, the history
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
constexpr uint32_t kBindingCache = 31; // (round MB) the radiance cache (one buffer: a pushed set has 32 descriptors at most)
constexpr uint32_t kBindingCount = 32;
constexpr VkDeviceSize kCacheEntryBytes = 64; // its key, its sums' low and high words, its light
constexpr uint32_t kMaterialSet = 1;

constexpr uint32_t kGroupSide = 8;
constexpr VkDeviceSize kUniformAlignment = 256;
constexpr std::size_t kMatrixSide = 4;
constexpr double kSingular = 1e-300;
constexpr uint32_t kReservoirWords = 2; // uvec4s a pixel
constexpr uint32_t kContinueWords = 2; // (round PERF5) uvec4s a continued path in the queue (after the reservoirs)
constexpr VkDeviceSize kCountersBytes = 32; // the bad samples' count, 3 spare, the continued paths' dispatch + count
constexpr VkDeviceSize kContinueArgs = 16;   // (where the dispatch's x, y, z are)
constexpr uint32_t kContinueGroup = 64;      // (the pass's local size: kGroupSide x kGroupSide - CONTINUE_GROUP)
static_assert(kContinueGroup == kGroupSide * kGroupSide);
constexpr uint32_t kSpecularReservoirWords = 4; // (round WS) uvec4s a pixel: after the temporal pass, the history

// The estimator's settings (fixed - no per-level values): the spatial neighbours, the reservoirs' sample count kept at
// most (one candidate a frame: 20 frames' worth - Ouyang et al. keep 30), the neighbours' radius (pixels), the surface
// tests (ReSTIR DI's), the share of the initial samples drawn from the sky's distribution (the rest from the BSDF).
constexpr uint32_t kSpatialNeighbours = 2;
constexpr uint32_t kMostSpatial = 4;
// (round BUDGET) The trace grid's side in the game and the pixel of a cell each frame takes, in turn (x, y). 2 (a cell
// a 2 x 2 block of pixels, PASS_UPSAMPLE bringing its light to the pixels - every pixel traced once in four frames) took
// ReSTIR GI from ~11 to ~3.3 ms at 1707 x 960 but is NOT used: Ray Reconstruction keeps a cell's outlier spread over
// its block as detail - bright 2 x 2 specks over the whole picture (BO at night, the bank: the converged frame's error
// against the converged one 3.9 -> 12 of 255, the specks plain in the picture; each pixel taking one like cell by its
// weight instead of their average: smaller specks, still there). Ray Reconstruction wants each pixel's own sample. 1:
// the view's pixels - the passes as before, no upsample.
constexpr uint32_t kTraceScale = 1;
constexpr std::array<std::array<uint32_t, 2>, 4> kTraceTurns{{{0, 0}, {1, 1}, {1, 0}, {0, 1}}};
constexpr uint32_t kHistoryCap = 20;
// (round DD) the local lights' candidates a next-event sample resamples (vulkan_light_list.h light_sample_glsl: RIS over
// the presampled pool's picks - RTXDI's ReSTIR GI takes 8 local light samples at its secondary surfaces too)
constexpr uint32_t kLocalCandidates = 8;
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
constexpr uint32_t kFlagCache = 64U; // (round MB) the secondary's bounce from the radiance cache
constexpr uint32_t kFlagSpecularReuse = 128U; // (round WS) the specular reservoir's temporal and spatial reuse
constexpr uint32_t kFlagSpecularOnly = 256U;  // (round WS - the check's) the sums the specular alone
// (round FA - vulkan_debug_view.h) a debug view without GI's diffuse / specular from surfaces, or without the sky (its
// samples that saw the sky, and the sun's or the moon's visibility: no direct light from the disc)
constexpr uint32_t kFlagNoDiffuse = 512U;
constexpr uint32_t kFlagNoSpecular = 1024U;
constexpr uint32_t kFlagNoSky = 2048U;
// (round WS) The specular reservoir's reuse in the game: OFF. The check measures it unbiased and its estimate's noise
// halved (the specular alone, a still or orbiting camera), but in the game under Ray Reconstruction (BO at night, the
// ledge under the sign and the brick wall, 60-frame series) it made the picture noisier - the ledge's specks a frame
// 338 -> 392, each pixel's temporal deviation 2.43 -> 2.83 (0-255), the wall's 0.58 -> 0.70 - for +2.6 ms (GI 6.1 ->
// 8.7 ms at 1280x720): its history is the point's own pixel's (permutation sampling put another view's lobe's samples
// in its tails - x 5 the outliers), so its error carries from frame to frame and Ray Reconstruction keeps it as
// detail. Its own ray each frame (the reservoir of one candidate) until a decorrelated reuse is found.
constexpr bool kSpecularReuseInGame = false;

// (round MB) The radiance cache (NVIDIA's SHaRC - spatially hashed radiance cache, the one Cyberpunk 2077's path tracing
// reads at its secondary vertices; the hash grid of Binder et al. 2019 / Gautron 2020): fixed, no per-level values.
// Its entries at most (a power of 2), the probes a key takes (linear), a cell's least side (world units - the game's
// inch: a few inches near the camera) and the pixels a cell spans where it is seen beyond that (its side grows in powers
// of 2 with the distance - SHaRC's levels; in pixels, not a fixed angle: the update paths are one a block of pixels, so
// every cell seen gets ~(12 / 4)^2 = 9 samples a frame at any resolution - the room check at 160x120 with cells of a fixed
// 1/48 radian was 10 % dark, its cells reached too seldom to keep their entries), the fixed point of the frame's sums (exposed light x this), the samples an
// entry's light averages at most (its history), the frames an entry not updated is kept, the update paths (one a
// CACHE_BLOCK x CACHE_BLOCK block of pixels a frame - SHaRC's 4 - 6 % of the pixels) and the rays a path traces (its
// vertices after the pixel's; the last one's bounce read from the cache - the further bounces over the frames).
constexpr uint32_t kCacheCapacity = kRadianceCacheCapacity; // (vulkan_light_list.h: the cache's readers share them)
constexpr uint32_t kCacheProbes = kRadianceCacheProbes;
constexpr float kCacheCellLeast = kRadianceCacheCellLeast;
constexpr double kCacheCellPixels = 12.0;
constexpr float kCacheScale = 65536.0F;
constexpr float kCacheSamples = 64.0F;
constexpr float kCacheFrames = 16.0F;
constexpr uint32_t kCacheStale = 64;
constexpr uint32_t kCacheBlock = 4;
constexpr uint32_t kCachePath = 3;
constexpr uint32_t kResolveGroup = 64;

constexpr uint32_t kTimingSlots = 16;
constexpr uint32_t kTimingStamps = 3; // a pass's start, its radiance cache's end, its end
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
    std::array<float, 4> sky_dist{};       // x 1 / the grid in use's luminance integral (0: no sky)
    std::array<float, 4> cloud_place{};    // (the clouds' shadow map: vulkan_rt_sky_pass.h CloudShadowMap)
    std::array<float, 4> cloud_light{};
    std::array<float, 4> cache_cells{};    // (round MB) x the angle a radiance cache cell spans from the camera (its least)
    std::array<uint32_t, 4> shared{};      // (round OM) xy ReSTIR DI's primary surfaces of this view, z 1: read them
    // (round OM) xy the light list's presampled pool (vulkan_light_list.h kLightPoolSize), z 1: the local lights from it,
    // w the local group's share of the list's picks (float bits)
    std::array<uint32_t, 4> pool{};
    // (round DO) xy ReSTIR DI's light grid's picks (vulkan_light_list.h kLightGridPicks), z 1: the local lights' candidates
    // from a point's cell inside the grid
    std::array<uint32_t, 4> grid{};
    // (round BUDGET) the trace grid: x, y its cells across and down, z its side in pixels (low byte) and the frame's pixel
    // in each cell (x the second byte, y the third)
    std::array<uint32_t, 4> trace{};
    std::array<uint32_t, 4> results{}; // (round BUDGET) xy the cells' results (PASS_SPATIAL -> PASS_UPSAMPLE)
};
static_assert(sizeof(GiBlock) == (2 * 64) + (24 * 16));

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
    vec4 cache_cells;
    uvec4 surfaces;
    uvec4 pool;
    uvec4 grid;
    uvec4 trace;
    uvec4 results;
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
// (round WF) the shared list sampling's inputs (vulkan_light_list.h light_sample_glsl)
#define LL_COUNT pass.counts.x
#define LL_SUN pass.sun
#define LL_SUN_RADIANCE pass.sun_radiance
#define LL_SKY_DIST pass.sky_dist.x
#define LL_REACH 1.0e6
#define LL_CLOUD_PLACE pass.cloud_place
#define LL_CLOUD_LIGHT pass.cloud_light
#define LL_CACHE
#define LL_EYE pass.eye.xyz
#define LL_CACHE_CELLS pass.cache_cells.x
#define LL_CACHE_ON ((pass.counts.w & 64u) != 0u)
)";

// The passes (PASS_SAMPLE - round PERF2: the surface and the pixel's secondary path, one lobe's; PASS_TEMPORAL: the
// temporal reuse; PASS_SPATIAL: the spatial reuse and the result; PASS_CACHE / PASS_RESOLVE: the radiance cache's). CHECK: the check's variant - the surface from camera rays, the result
// accumulated. CLOUD_MAP: the disc's light through the clouds' shadow map.
constexpr const char* kGiBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 10) buffer ReservoirsA { uvec4 res_a[]; };
layout(std430, set = 0, binding = 11) buffer ReservoirsB { uvec4 res_b[]; };
layout(std430, set = 0, binding = 12) buffer SurfacesNow { vec4 now_at[]; };
layout(std430, set = 0, binding = 13) buffer SurfacesNowMore { uvec4 now_more[]; };
layout(std430, set = 0, binding = 14) readonly buffer SurfacesBefore { vec4 before_at[]; };
layout(std430, set = 0, binding = 15) readonly buffer SurfacesBeforeMore { uvec4 before_more[]; };
// (round WS) the specular reservoirs: 4 words a pixel - this frame's after the temporal pass (0, 1), the history (2, 3)
layout(std430, set = 0, binding = 16) buffer SpecularReservoirs { uvec4 spec_res[]; };
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
layout(std430, set = 0, binding = 24) buffer Counters {
    uint bad_samples;
    uint counter_spare[3];
    uint continue_groups; // (round PERF5) the continued paths' dispatch (x, y, z) and their count (PASS_CONTINUE)
    uint continue_y;
    uint continue_z;
    uint continued;
};
// (round MB) the radiance cache: 4 words an entry - its key (x the check word, 0: free; y the frame it was updated; z
// (round PERF) the samples it has taken since it was claimed, at most CACHE_COUNT_MOST - cache_lookup's trust; w (round
// PERF2) the frames it has been resolved since it was claimed, at most CACHE_COUNT_MOST - the same), this
// frame's sums (low words rgb + the samples; high words rgb), its light (float bits: rgb the bounce in the global unit,
// w its samples)
layout(std430, set = 0, binding = 31) buffer Cache { uvec4 cache[]; };
#define CACHE_KEY(e) cache[(e) * 4u]
#define CACHE_LOW(e) cache[(e) * 4u + 1u]
#define CACHE_HIGH(e) cache[(e) * 4u + 2u]
#define CACHE_LIGHT(e) cache[(e) * 4u + 3u]

const uint FLAG_TEMPORAL = 1u;
const uint FLAG_SPATIAL = 2u;
const uint FLAG_ACCUMULATE = 4u;
const uint FLAG_FORCED = 8u;
const uint FLAG_PERMUTE = 16u;
const uint FLAG_BOILING = 32u;
const uint FLAG_CACHE = 64u;
const uint FLAG_SPECULAR_REUSE = 128u;
const uint FLAG_SPECULAR_ONLY = 256u;
const uint FLAG_NO_DIFFUSE = 512u;
const uint FLAG_NO_SPECULAR = 1024u;
const uint FLAG_NO_SKY = 2048u;
const uint MOST_SPATIAL = 4u;
const uint LIGHT_RAYS = 15u;          // world, objects, alpha-tested, drawn apart (round PG) - the reference's rays
const uint LIGHT_RAYS_ON_OBJECT = 13u; // (an object's pixel: the world's and what is drawn apart - its low-poly body
                                       // doesn't shadow itself)
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

// (round BUDGET) The trace grid: ReSTIR GI's paths, reservoirs and reuse at one pixel of each trace_scale() x
// trace_scale() block of the view (a cell) - the frame's pixel of the cell turning over the block from frame to frame -,
// then PASS_UPSAMPLE takes each pixel's light from the cells round it (their surfaces like its own). Half resolution as
// path-traced games' indirect light is traced (Metro Exodus Enhanced Edition's ray-traced GI, Embark's kajiya: ReSTIR GI
// at half resolution, reconstructed to the full one): a quarter of the paths, the reuse in cells. The check's grid is
// the view's pixels (trace_scale 1).
ivec2 cells() { return ivec2(pass.trace.xy); }
int trace_scale() { return int(pass.trace.z & 255u); }
ivec2 cell_pixel(ivec2 c)
{
    ivec2 within = ivec2(int((pass.trace.z >> 8) & 255u), int((pass.trace.z >> 16) & 255u));
    return min(c * trace_scale() + within, pass.rect.zw - 1);
}
uint cell_index(ivec2 c) { return uint(c.y) * pass.trace.x + uint(c.x); }
layout(buffer_reference, buffer_reference_align = 16, scalar) buffer GiResults { vec4 rows[]; };

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
// vulkan_restir_di.cpp): the point from the depth, the normal from the nearer neighbours' depths, a diffuser (round BR:
// no specular ray - below). Before, such a pixel kept the light store's environment - the outlines of tessellated, skinned and
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
    // (round BR) a diffuser alone - F0 0: no specular ray from it. The ray would start on a surface the scene's copy
    // hasn't (a skinned body, a tessellated outline - why the camera ray missed it) and meet that copy's inside: the
    // skater and the platforms' edges outlined in white. The lights' specular on it stays ReSTIR DI's (rays to the lights).
    return Surface(here, n, n, vec3(0.0), vec3(0.0), 1.0, true, false, false);
}
#endif

// (round OM - vulkan_restir_di.h SharedPrimary) ReSTIR DI's primary surface of this view - the same surface this pass
// would find, in full precision: the material put on it here as primary() does (hit_material: the check's).
layout(buffer_reference, buffer_reference_align = 16, scalar) readonly buffer SharedSurfaces { vec4 rows[]; };
Surface shared_surface(ivec2 p)
{
    ivec2 local = p - pass.rect.xy;
    uint at = (uint(local.y) * uint(pass.rect.z) + uint(local.x)) * 5u;
    SharedSurfaces rows = SharedSurfaces(pass.surfaces.xy);
    vec4 r0 = rows.rows[at];
    vec4 r1 = rows.rows[at + 1u];
    vec4 r2 = rows.rows[at + 2u];
    vec4 r3 = rows.rows[at + 3u];
    vec4 r4 = rows.rows[at + 4u];
    uint flags = floatBitsToUint(r2.w);
    Surface s;
    s.at = r0.xyz;
    s.valid = r0.w > 0.5;
    s.n = r1.xyz;
    s.ng = r2.xyz;
    s.object = (flags & 1u) != 0u;
    if ((pass.counts.w & FLAG_FORCED) != 0u) {
        BrdfMaterial m = brdf_material(r3.rgb * pass.force.z, vec3(pass.force.y), pass.force.x);
        s.albedo = m.albedo;
        s.f0 = m.f0;
        s.rough = m.roughness;
        s.metal = false;
    } else {
        s.albedo = r3.rgb;
        s.f0 = r4.rgb;
        s.rough = r1.w;
        s.metal = (flags & 2u) != 0u;
    }
    return s;
}

// The pixel's surface: the check's camera ray through its centre; the game's the prepass's depth there, its triangle the
// one a camera ray meets at that point (round CW: the one the prepass kept - trace_drawn, a blended layer too) -
// (round PT4d) where none does, the depth's own surface (depth_surface). (Round OM: ReSTIR DI's where it found it.)
// (round PERF4) PRIMARY_SHARED: the variant for a view whose surfaces ReSTIR DI found - shared_surface alone, without
// the camera ray, the prepass-kept test and a second inlined copy of hit_surface's shading (the sample and cache
// passes' code and registers: every copy is in the shader whether it runs or not).
Surface primary(ivec2 p)
{
#ifdef PRIMARY_SHARED
    return shared_surface(p);
#else
    if (pass.surfaces.z != 0u) {
        return shared_surface(p);
    }
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
    if (!trace_drawn(eye, dir, reach, expected, instance, primitive, id, bary, t, to_world) ||
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
#endif
}

uint rays_of(Surface s) { return s.object ? LIGHT_RAYS_ON_OBJECT : LIGHT_RAYS; }

@LIGHT_SAMPLE@
// The secondary vertex's reflected light toward `v` by next-event estimation: one light of the full list (the disc, the
// sky, an emitting triangle, a lamp - picked by power), one ray to it - the reference's light sampling without its MIS
// (no BSDF ray continues from here). (Round WF: the list's sampling shared - vulkan_light_list.h light_sample_glsl.)
vec3 next_event(vec3 p, vec3 n, vec3 ng, vec3 v, BrdfMaterial m, inout uint rng)
{
    vec3 dir;
    vec3 incident = light_sample(p, n, ng, LIGHT_RAYS, rng, dir);
    if (!(max(incident.r, max(incident.g, incident.b)) > 0.0)) {
        return vec3(0.0);
    }
    return path_bsdf_cosine(m, n, v, dir) * incident;
}

// What a ray from `origin` meets: a surface (its point, its normal toward the ray, its reflected light toward the ray's
// origin - next_event -, how far) or nothing (the sky that way). Meshes the light store hides (the bake's shadows drawn
// as geometry) are passed. (Round BR) Not its emission: the light straight from an emitter - on a diffuser and in a
// specular lobe - is ReSTIR DI's, sampled on the emitter (one owner; a path's first vertex on a light is direct light).
// (Round MB) Also its shading normal and its reflectance toward the ray's origin (the share of an even light it sends
// back - the light model's directional albedo, diffuse and specular): what it makes of the light that comes to it off
// other surfaces (the radiance cache's - with_bounces below).
struct Secondary {
    bool hit;
    vec3 at;
    vec3 n;
    vec3 shading;
    vec3 bounce;
    vec3 reflectance;
    float t;
    vec3 beyond; // (round BIAS) the bounce at it (E / pi) - the cache's, else the path continued (continue_path)
    uint skipped; // (round PERF5) the hidden surfaces passed before it (its path's steps so far: continue_path's)
};
// (round BIAS) The radiance cache's misses: SHaRC's query (cache_lookup) takes a cell it has no entry for as a miss and
// the path goes on - read as 0, every newly seen region's bounce was dark for the ~64 frames the cache takes to fill
// (round MV: GI's diffuse there 0.57 - 0.73 x converged). The sample pass's first vertex's bounce (`beyond`) is the
// cache's where its cell has a trusted entry (cache_lookup: CACHE_TRUSTED_SAMPLES); on a miss one ray along the vertex's
// cosine lobe - with the cosine's pdf the light it meets is a one-sample estimate of the bounce, E / pi, the cache's own
// unit - and at the next vertex its direct light + its reflectance x its bounce again: CACHE_MISS_SURE further vertices,
// then Russian roulette by the light the path still carries, up to CACHE_MISS_VERTICES (where the ray leaves the world,
// nothing more: the sky's light is each vertex's direct light) - continue_path, the continued paths' own pass.
const uint CACHE_MISS_VERTICES = 12u; // (at most - Russian roulette ends most paths sooner)
const uint CACHE_MISS_SURE = 2u;      // (the vertices before the roulette)
const uint PATH_HIDDEN_MOST = 4u;     // (hidden surfaces passed on the whole path)
const uint PATH_STEPS = PATH_HIDDEN_MOST + CACHE_MISS_VERTICES + 1u;
// A path's first vertex: the surface a ray from `origin` meets (hidden ones passed), its direct light and reflectance.
// (Round PERF5) One vertex: the continuation past a cache miss is its own pass (continue_path) - with its loop in the same
// shader the sample pass held 241 registers, 128 without.
Secondary trace_secondary(uint mask, vec3 origin, vec3 dir, inout uint rng)
{
    Secondary out_s = Secondary(false, vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), vec3(0.0), GI_REACH,
                                vec3(0.0), 0u);
    float travelled = 0.0;
    uint skipped = 0u;
    for (uint step = 0u; step <= PATH_HIDDEN_MOST; ++step) {
        uint instance;
        uint primitive;
        uint id;
        vec2 bary;
        float t;
        mat4x3 to_world;
        HitSurface h;
        if (!trace_closest(mask, origin, dir, GI_REACH, instance, primitive, id, bary, t, to_world) ||
            !hit_surface(instance, primitive, id, bary, to_world, dir, h)) {
            break;
        }
        if (h.hidden) {
            if (skipped >= PATH_HIDDEN_MOST) {
                break;
            }
            origin = h.position + dir * ray_offset(h.position);
            travelled += t;
            ++skipped;
            continue;
        }
        vec3 ng = h.normal;
        vec3 v = -dir;
        vec3 n = dot(h.shading_normal, v) > 0.0 ? h.shading_normal : ng;
        BrdfMaterial m = hit_material(h);
        out_s.hit = true;
        out_s.at = h.position;
        out_s.n = ng;
        out_s.shading = n;
        out_s.t = travelled + t;
        out_s.bounce = good3(next_event(h.position + ng * ray_offset(h.position), n, ng, v, m, rng));
        out_s.reflectance = good3(brdf_ambient(m, dot(n, v), vec3(1.0), vec3(1.0)));
        break;
    }
    out_s.skipped = skipped;
    return out_s;
}

#ifdef PASS_CONTINUE
// (round PERF5) The path past its first vertex `sec` - a cache miss there: the light beyond it (E / pi at the vertex),
// each further vertex's direct light + its reflectance x its own bounce (the cache's where trusted, else on). The same
// steps, numbers and random sequence as one loop from the first ray did.
vec3 continue_path(Secondary sec, inout uint rng)
{
    vec3 beyond = vec3(0.0);
    vec3 through = vec3(1.0);
    uint vertices = 1u;
    uint skipped = sec.skipped;
    vec3 dir = path_to_world(sec.shading, path_cosine_direction(path_next(rng), path_next(rng)));
    if (dot(dir, sec.n) <= 0.0) {
        return vec3(0.0);
    }
    vec3 origin = sec.at + sec.n * ray_offset(sec.at);
    for (uint step = sec.skipped + 1u; step < PATH_STEPS; ++step) {
        uint instance;
        uint primitive;
        uint id;
        vec2 bary;
        float t;
        mat4x3 to_world;
        HitSurface h;
        if (!trace_closest(LIGHT_RAYS, origin, dir, GI_REACH, instance, primitive, id, bary, t, to_world) ||
            !hit_surface(instance, primitive, id, bary, to_world, dir, h)) {
            break;
        }
        if (h.hidden) {
            if (skipped >= PATH_HIDDEN_MOST) {
                break;
            }
            origin = h.position + dir * ray_offset(h.position);
            ++skipped;
            continue;
        }
        vec3 ng = h.normal;
        vec3 v = -dir;
        vec3 n = dot(h.shading_normal, v) > 0.0 ? h.shading_normal : ng;
        BrdfMaterial m = hit_material(h);
        vec3 direct = good3(next_event(h.position + ng * ray_offset(h.position), n, ng, v, m, rng));
        vec3 reflectance = good3(brdf_ambient(m, dot(n, v), vec3(1.0), vec3(1.0)));
        beyond += through * direct;
        through *= reflectance;
        ++vertices;
        vec3 cached;
        if (cache_lookup(h.position, ng, cached)) {
            beyond += through * cached;
            break;
        }
        float most = max(through.r, max(through.g, through.b));
        if (vertices > CACHE_MISS_VERTICES || !(most > 0.0)) {
            break;
        }
        if (vertices > CACHE_MISS_SURE) {
            // (Russian roulette by the light the path still carries - unbiased: a kept path weighs 1 / its chance)
            float keep = clamp(most, 0.1, 0.95);
            if (path_next(rng) >= keep) {
                break;
            }
            through /= keep;
        }
        vec3 next = path_to_world(n, path_cosine_direction(path_next(rng), path_next(rng)));
        if (dot(next, ng) <= 0.0) {
            break;
        }
        origin = h.position + ng * ray_offset(h.position);
        dir = next;
    }
    return good3(beyond);
}
#endif

// The light a secondary vertex sends toward the ray's origin: its direct light + its reflectance x its bounce (traced
// with go_on: the radiance cache's, or the path continued on its miss).
vec3 with_bounces(Secondary sec) { return sec.hit ? good3(sec.bounce + sec.reflectance * sec.beyond) : vec3(0.0); }
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

// The target at surface `s` of a sample's light from direction `dir`: the luminance of what a white diffuser's lobe takes
// of its reflected light (the reservoirs carry the diffuse light alone - round BR: the specular is every surface's own
// ray, step 3 of the temporal pass).
float target_of(Surface s, vec3 dir, vec3 bounce)
{
    if (dot(s.ng, dir) <= 0.0 || dot(s.n, dir) <= 0.0) {
        return 0.0;
    }
    return good(path_luma(bounce) * white_lobe(s, dir));
}
float target_at(Surface s, vec3 eye, Reservoir r, out vec3 dir, out float reach, out float g)
{
    if (!sample_from(s, r, dir, reach, g)) {
        return 0.0;
    }
    return target_of(s, dir, r.bounce);
}
// What of the light gets from the sample to the surface (the one light ray - vulkan_rt_scene.h ray_light).
vec3 seen(Surface s, vec3 dir, float reach) { return ray_light_leaving(rays_of(s), s.at + s.ng * ray_offset(s.at), s.ng, dir, reach); }
bool seen_at_all(vec3 through) { return max(through.r, max(through.g, through.b)) > 0.0; }

// (round WS) The specular reservoir - ReSTIR GI's reuse of the environment's specular (Ouyang et al. 2021 resample with
// the surface's whole BSDF; RTXDI's GI does the same with a BRDF-aware target): a reservoir of its own beside the diffuse
// one (round BR split them: a sample drawn for the diffuse lobe lands in a GGX lobe by chance). Its candidate is the
// surface's GGX visible-normal ray (pdf: the lobe's reflection pdf); its target p^ = the luminance of the specular lobe
// x the sample's light at the surface's own view (specular_target_of) - the reuse re-evaluates the lobe at the receiving
// surface and view, so a sample only goes where that lobe takes it. Reconnection shift (Lin et al. 2022) with RTXDI's GI
// sample validation (jacobian_kept), the reservoirs combined by the generalised balance heuristic (Lin et al. 2022 - a
// sample's weight its share of every reservoir's M x target at it: one lobe's sample in another's peak can't come out
// x 100s, as it did with 1 / Z); the target unshadowed - the visibility is the result's ray alone (an unbiased estimator
// with the integrand's visibility applied once; no rays in the weights); roughness-aware: a neighbour or
// history only of a like roughness (SPECULAR_ROUGHNESS_AGREEMENT), the spatial radius shrinking with the lobe, and no
// reuse at all below SPECULAR_REUSE_ROUGHNESS - a near-mirror's neighbours look elsewhere (their samples land
// outside its lobe); it takes its own ray and the temporal reuse alone (its history's sample re-evaluated at this frame's
// view: while the view holds, the same reflection - averaged over frames; once it moves, outside the lobe and dropped).
const float SPECULAR_REUSE_ROUGHNESS = 0.2;
const uint SPECULAR_NEIGHBOURS = 1u; // (the weights' rays grow with their square)
const float SPECULAR_ROUGHNESS_AGREEMENT = 0.1;
const float GI_MOST_JACOBIAN = 10.0;
// RTXDI's GI sample validation (RAB_ValidateGISampleWithJacobian - round LC's finding: a secondary point much nearer the
// receiving surface than the one that found it took the neighbour's weight x 100s): a reconnection whose Jacobian is
// outside 1/10 .. 10 is not taken (RTXDI's further clamp of the kept ones to 1/3 .. 3 is not done: a clamp biases).
bool jacobian_kept(float jacobian) { return jacobian <= GI_MOST_JACOBIAN && jacobian >= 1.0 / GI_MOST_JACOBIAN; }
float specular_target_of(Surface s, vec3 v, vec3 dir, vec3 bounce)
{
    if (dot(s.ng, dir) <= 0.0 || dot(s.n, dir) <= 0.0 || !(max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0)) {
        return 0.0;
    }
    return good(path_luma(specular_lobe(s, v, dir) * bounce));
}
float specular_target_at(Surface s, vec3 eye, Reservoir r, out vec3 dir, out float reach, out float g)
{
    if (!sample_from(s, r, dir, reach, g)) {
        return 0.0;
    }
    return specular_target_of(s, view_of(s, eye), dir, r.bounce);
}
// A sample's specular target at `x` in the area measure (its target x the solid angle factor there - the sky's
// directions: 1): the generalised balance heuristic's term; `g` the factor. 0 where `x` can't take it.
float specular_area_target(Surface x, vec3 eye, Reservoir r, out float g)
{
    vec3 dir;
    float reach;
    float t = specular_target_at(x, eye, r, dir, reach, g);
    return t > 0.0 && g > 0.0 ? good(t * g) : 0.0;
}
bool rough_alike(Surface s, Surface other) { return abs(s.rough - other.rough) <= SPECULAR_ROUGHNESS_AGREEMENT; }
// Whether nothing solid stands between surface `x` and a sample (the weights' visibility - any hit; what the seen-through
// surfaces let through is the result's ray alone).
bool unblocked(Surface x, Reservoir r)
{
    vec3 dir;
    float reach;
    float g;
    return sample_from(x, r, dir, reach, g) && !ray_blocked(rays_of(x) & ~RAY_SEEN_THROUGH, x.at + x.ng * ray_offset(x.at), dir, reach);
}
void store_specular(bool history, uint i, Reservoir r)
{
    uvec2 b = pack_light(r.bounce);
    uint at = i * 4u + (history ? 2u : 0u);
    spec_res[at] = uvec4(floatBitsToUint(r.y), packSnorm2x16(oct_encode(r.ny)));
    spec_res[at + 1u] = uvec4(b.x, b.y | (r.kind << 24), floatBitsToUint(r.W), floatBitsToUint(r.M));
}
Reservoir load_specular(bool history, uint i)
{
    uint at = i * 4u + (history ? 2u : 0u);
    return reservoir_of(spec_res[at], spec_res[at + 1u]);
}

#if defined(PASS_CACHE) || defined(PASS_SAMPLE) || defined(PASS_CONTINUE)
// (round MB) The cache's entry for a point (claimed if it has none yet: the probes' first free slot); false when the
// probes are all taken by other cells (the sample is dropped).
bool cache_claim(vec3 at, vec3 ng, out uint entry)
{
    uint slot;
    uint check;
    cache_key(at, ng, slot, check);
    for (uint k = 0u; k < CACHE_PROBES; ++k) {
        uint i = (slot + k) & (CACHE_CAPACITY - 1u);
        uint there = atomicCompSwap(CACHE_KEY(i).x, 0u, check);
        if (there == 0u || there == check) {
            CACHE_KEY(i).y = pass.counts.z;
            entry = i;
            return true;
        }
    }
    entry = 0u;
    return false;
}
// One word of a 64-bit sum: the low word added, its carry into the high word (32-bit atomics alone).
void cache_add_word(uint entry, uint channel, uint value)
{
    if (value == 0u) {
        return;
    }
    uint before = channel == 0u ? atomicAdd(CACHE_LOW(entry).x, value)
                                : (channel == 1u ? atomicAdd(CACHE_LOW(entry).y, value) : atomicAdd(CACHE_LOW(entry).z, value));
    if (before + value < before) {
        if (channel == 0u) {
            atomicAdd(CACHE_HIGH(entry).x, 1u);
        } else if (channel == 1u) {
            atomicAdd(CACHE_HIGH(entry).y, 1u);
        } else {
            atomicAdd(CACHE_HIGH(entry).z, 1u);
        }
    }
}
// A sample of a point's bounce added to its entry's sums: the frame's exposed light in fixed point, rounded at random
// (unbiased however small the light).
void cache_add(vec3 at, vec3 ng, vec3 light, inout uint rng)
{
    uint entry;
    if (!cache_claim(at, ng, entry)) {
        return;
    }
    vec3 fixed_point = clamp(good3(light) * pass.eye.w * CACHE_SCALE, vec3(0.0), vec3(4.0e9));
    uvec3 words = uvec3(fixed_point + vec3(path_next(rng), path_next(rng), path_next(rng)));
    cache_add_word(entry, 0u, words.r);
    cache_add_word(entry, 1u, words.g);
    cache_add_word(entry, 2u, words.b);
    atomicAdd(CACHE_LOW(entry).w, 1u);
}
#endif

#ifdef PASS_CACHE
// The update paths (SHaRC's): one pixel of each CACHE_BLOCK x CACHE_BLOCK block a frame (at random), its surface the
// first vertex; CACHE_PATH rays, each along the cosine lobe of the vertex it leaves; at every surface met its direct
// light by next-event estimation and its reflectance. Back from the last: the light coming into a vertex along its ray
// is what the next vertex sends - its direct light + its reflectance x its own bounce (the next ray's light; past the
// last ray, the cache's) - and with a cosine-distributed ray that light is a one-sample estimate of the vertex's bounce
// (E / pi): added to the vertex's entry. A ray that meets nothing gives 0 (the sky's light is a vertex's direct light).
void main()
{
    ivec2 block = ivec2(gl_GlobalInvocationID.xy);
    uint rng = path_sequence_start(uint(block.x), uint(block.y), pass.counts.z ^ 0x2545F491u, pass.params.w + 7u);
    uint pick = min(uint(path_next(rng) * float(CACHE_BLOCK * CACHE_BLOCK)), CACHE_BLOCK * CACHE_BLOCK - 1u);
    ivec2 local = block * int(CACHE_BLOCK) + ivec2(int(pick % CACHE_BLOCK), int(pick / CACHE_BLOCK));
    if (any(greaterThanEqual(local, cells()))) {
        return;
    }
    Surface s = primary(pass.rect.xy + cell_pixel(local)); // (round BUDGET: a block of cells)
    if (!s.valid) {
        return;
    }
    vec3 at[CACHE_PATH + 1u];
    vec3 facing[CACHE_PATH + 1u];
    vec3 direct[CACHE_PATH];
    vec3 reflectance[CACHE_PATH];
    bool met[CACHE_PATH];
    at[0] = s.at;
    facing[0] = s.ng;
    vec3 n = s.n;
    uint mask = rays_of(s);
    uint traced = 0u;
    for (uint k = 0u; k < CACHE_PATH; ++k) {
        met[k] = false;
        direct[k] = vec3(0.0);
        reflectance[k] = vec3(0.0);
        traced = k + 1u;
        vec3 dir = path_to_world(n, path_cosine_direction(path_next(rng), path_next(rng)));
        if (dot(dir, facing[k]) <= 0.0) {
            break;
        }
        Secondary sec = trace_secondary(mask, at[k] + facing[k] * ray_offset(at[k]), dir, rng);
        if (!sec.hit) {
            break;
        }
        met[k] = true;
        direct[k] = sec.bounce;
        reflectance[k] = sec.reflectance;
        at[k + 1u] = sec.at;
        facing[k + 1u] = sec.n;
        n = sec.shading;
        mask = LIGHT_RAYS;
    }
    vec3 incoming = vec3(0.0);
    for (int k = int(traced) - 1; k >= 0; --k) {
        uint j = uint(k);
        if (met[j]) {
            vec3 beyond = j + 1u < traced ? incoming : cache_bounce(at[j + 1u], facing[j + 1u]);
            incoming = good3(direct[j] + reflectance[j] * beyond);
        } else {
            incoming = vec3(0.0);
        }
        cache_add(at[j], facing[j], incoming, rng);
    }
}
#endif

#ifdef PASS_RESOLVE
// (round MB) Each entry's frame: its samples' mean folded into its light, the sums cleared; an entry not updated for
// CACHE_STALE frames freed. The history is a number of frames, not of samples (SHaRC's accumulated frames): the weight
// the light kept has (its samples, CACHE_SAMPLES at most) fades by 1 - 1 / CACHE_FRAMES every frame - an entry few paths
// reach still takes its new samples within that many frames, and the bounces the cache feeds back into itself (the
// update paths' last vertices) settle in a few times that.
void main()
{
    uint i = gl_WorkGroupID.x * RESOLVE_GROUP + gl_LocalInvocationIndex;
    if (i >= CACHE_CAPACITY) {
        return;
    }
    uvec2 key = CACHE_KEY(i).xy;
    if (key.x == 0u) {
        return;
    }
    uvec4 low = CACHE_LOW(i);
    vec4 l = uintBitsToFloat(CACHE_LIGHT(i));
    float kept = min(l.w, CACHE_SAMPLES) * (1.0 - 1.0 / CACHE_FRAMES);
    if (low.w > 0u) {
        uvec4 high = CACHE_HIGH(i);
        vec3 sum = (vec3(high.xyz) * 4294967296.0 + vec3(low.xyz)) / (CACHE_SCALE * max(pass.eye.w, 1.0e-30));
        float k = float(low.w);
        vec3 mean = (l.rgb * kept + sum) / (kept + k);
        CACHE_LIGHT(i) = floatBitsToUint(vec4(good3(mean), kept + k));
        CACHE_KEY(i).z = min(CACHE_KEY(i).z + low.w, CACHE_COUNT_MOST); // (round PERF - cache_lookup's trust)
        CACHE_KEY(i).w = min(CACHE_KEY(i).w + 1u, CACHE_COUNT_MOST);     // (round PERF2 - the same)
        CACHE_LOW(i) = uvec4(0u);
        CACHE_HIGH(i) = uvec4(0u);
    } else if (pass.counts.z - key.y <= CACHE_STALE) {
        CACHE_LIGHT(i) = floatBitsToUint(vec4(l.rgb, kept));
        CACHE_KEY(i).w = min(CACHE_KEY(i).w + 1u, CACHE_COUNT_MOST);
    } else {
        CACHE_KEY(i) = uvec4(0u);
        CACHE_LIGHT(i) = uvec4(0u);
    }
}
#endif

#if defined(PASS_SAMPLE) || defined(PASS_CONTINUE)
// (round PERF5) The sample pass's paths continued past a cache miss at their first vertex - the continued paths' pass
// (continue_path), wavefront style: the long loop out of the sample pass (its registers 241 -> 128) and run densely over
// the queued paths alone. The queue: after the pixels' reservoirs in res_a, 2 words a path (the pixel and its lobe, its
// random sequence's state, the candidate's pdf; its direction); the path's first vertex in the pixel's own reservoir
// words (res_a's 2, the specular's first 2 - both written by the continued pass, the temporal pass reads them after).
const uint CONTINUE_GROUP = 64u; // (local_size_x x local_size_y)
uint continue_base() { return pass.trace.x * pass.trace.y * 2u; }
Secondary no_secondary()
{
    return Secondary(false, vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), vec3(0.0), GI_REACH, vec3(0.0),
                     0u);
}
// The pixel's samples from its candidate (`traced`: the candidate's ray was traced - its pdf over 0 - meeting `sec`, else
// the sky that way): the chosen lobe's reservoir the candidate where its target is over 0, the other none (M 1 - drawn
// from the same mixture, the estimate stays unbiased).
void store_samples(uint i, Surface s, bool traced, bool take_specular, vec3 dir, float pdf, Secondary sec)
{
    bool has_specular = max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0;
    Reservoir r = no_reservoir(1.0);
    Reservoir spec = has_specular ? no_reservoir(1.0) : no_reservoir(0.0);
    if (traced) {
        Reservoir c;
        c.M = 1.0;
        c.W = good(1.0 / pdf);
        if (sec.hit) {
            c.kind = KIND_SURFACE;
            c.y = sec.at;
            c.ny = sec.n;
            c.bounce = with_bounces(sec);
        } else {
            c.kind = KIND_SKY;
            c.y = dir;
            c.ny = -dir;
            c.bounce = good3(sky_radiance(dir));
        }
        float t = take_specular ? specular_target_of(s, view_of(s, pass.eye.xyz), dir, c.bounce) : target_of(s, dir, c.bounce);
        if (t > 0.0 && c.W > 0.0) {
            if (take_specular) {
                spec = c;
            } else {
                r = c;
            }
        }
    }
    store_reservoir(false, i, r);
    store_specular(false, i, spec);
}
#endif
#ifdef PASS_SAMPLE
void queue_continued(uint i, bool take_specular, uint rng, float pdf, vec3 dir, Secondary sec)
{
    res_a[i * 2u] = uvec4(floatBitsToUint(sec.at), floatBitsToUint(sec.n.x));
    res_a[i * 2u + 1u] = uvec4(floatBitsToUint(sec.n.yz), floatBitsToUint(sec.shading.xy));
    spec_res[i * 4u] = uvec4(floatBitsToUint(sec.shading.z), floatBitsToUint(sec.bounce));
    spec_res[i * 4u + 1u] = uvec4(floatBitsToUint(sec.reflectance), sec.skipped);
    uint slot = atomicAdd(continued, 1u);
    if (slot % CONTINUE_GROUP == 0u) {
        atomicAdd(continue_groups, 1u);
    }
    uint at = continue_base() + slot * 2u;
    res_a[at] = uvec4(i | (take_specular ? 0x80000000u : 0u), rng, floatBitsToUint(pdf), 0u);
    res_a[at + 1u] = uvec4(floatBitsToUint(dir), 0u);
}
#endif
#ifdef PASS_CONTINUE
Secondary continued_secondary(uint i)
{
    uvec4 a0 = res_a[i * 2u];
    uvec4 a1 = res_a[i * 2u + 1u];
    uvec4 b0 = spec_res[i * 4u];
    uvec4 b1 = spec_res[i * 4u + 1u];
    return Secondary(true, uintBitsToFloat(a0.xyz), vec3(uintBitsToFloat(a0.w), uintBitsToFloat(a1.xy)),
                     vec3(uintBitsToFloat(a1.zw), uintBitsToFloat(b0.x)), uintBitsToFloat(b0.yzw), uintBitsToFloat(b1.xyz),
                     0.0, vec3(0.0), b1.w);
}
#endif

#ifdef PASS_SAMPLE
// (round PERF2) The chance a pixel's secondary path takes the specular lobe: the lobe's share of the surface's
// reflectance (path_specular_probability - the reference path tracer's lobe choice), held within LOBE_LEAST of 0 and 1
// so neither lobe of a surface with both goes unsampled for long (a pick of 1 in 100 would come back x 100); a metal has
// no diffuse lobe - always the specular.
const float LOBE_LEAST = 0.1;
float lobe_share(Surface s, BrdfMaterial m, float nv)
{
    return s.metal ? 1.0 : clamp(path_specular_probability(m, nv), LOBE_LEAST, 1.0 - LOBE_LEAST);
}
// (round PERF2) Each pixel's new samples - its own pass before the temporal reuse, as RTXDI's ReSTIR GI makes its
// initial samples (the one shader that did both lost the device when edited: the driver faulted on the shader's own
// code). The pixel's surface (put for the passes after), then one secondary path a pixel, its lobe chosen at random by
// its share of the surface's reflectance (lobe_share - one BRDF-sampled path a pixel, as RTXDI's ReSTIR GI and Cyberpunk
// 2077's path tracing trace it): the diffuse lobe's candidate - a direction from the sky's distribution or the cosine
// lobe, its pdf the mix's - or the specular lobe's - one ray along a visible normal of its GGX lobe -, its pdf x the
// lobe's chance. The other reservoir's candidate this frame is none (M 1, no sample: drawn from the same mixture, the
// estimate stays unbiased). Before, both lobes' paths every pixel every frame: twice the rays, light samples and cache
// misses. Stored as this frame's reservoirs (res_a, the specular's first two words) for the temporal pass to take up.
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy); // (round BUDGET: a cell)
    if (any(greaterThanEqual(local, cells()))) {
        return;
    }
    uint i = cell_index(local);
    put_surface(i, primary(pass.rect.xy + cell_pixel(local)));
    // (the surface as every pass after this one reads it back - the lobes' choice made with the same numbers)
    Surface s = surface_of(now_at[i], now_more[i]);
    if (!s.valid) {
        store_reservoir(false, i, no_reservoir(0.0));
        store_specular(false, i, no_reservoir(0.0));
        return;
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z, pass.params.w);
    vec3 v = view_of(s, pass.eye.xyz);
    float nv = max(dot(s.n, v), 1.0e-4);
    BrdfMaterial m = material_of(s);
    float alpha = brdf_alpha(s.rough);
    vec3 origin = s.at + s.ng * ray_offset(s.at);
    uint mask = rays_of(s);

    bool has_specular = max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0;
    float specular_chance = has_specular ? lobe_share(s, m, nv) : 0.0;
    bool take_specular = path_next(rng) < specular_chance;
    float sky_share = pass.sky_dist.x > 0.0 ? pass.tune.w : 0.0;
    vec3 dir = vec3(0.0, 1.0, 0.0);
    float pdf = 0.0;
    if (take_specular) {
        vec3 h = path_to_world(s.n, path_ggx_visible_normal(path_to_local(s.n, v), alpha, path_next(rng), path_next(rng)));
        dir = 2.0 * dot(v, h) * h - v;
        if (dot(dir, s.ng) > 0.0 && dot(dir, s.n) > 0.0) {
            pdf = specular_chance * path_ggx_reflection_pdf(nv, clamp(dot(s.n, h), 0.0, 1.0), alpha);
        }
    } else {
        float pick = path_next(rng);
        float u1 = path_next(rng);
        float u2 = path_next(rng);
        float u3 = path_next(rng);
        float u4 = path_next(rng);
        if (pick < sky_share) {
            uint cell;
            dir = sky_sample(u1, u2, u3, u4, cell);
        } else {
            dir = path_to_world(s.n, path_cosine_direction(u1, u2));
        }
        if (dot(dir, s.ng) > 0.0 && dot(dir, s.n) > 0.0) {
            pdf = (1.0 - specular_chance) *
                  (sky_share * sky_density(dir) + (1.0 - sky_share) * dot(s.n, dir) / PATH_PI);
        }
    }
    if (!(pdf > 0.0)) {
        store_samples(i, s, false, take_specular, dir, pdf, no_secondary());
        return;
    }
    // (round FA) the specular's hit takes its light from candidates of its own - the specular is each frame's own ray, no
    // reuse averages a light grid cell's shared error (vulkan_light_list.h light_sample_glsl: ll_cell_candidates)
    ll_cell_candidates = !take_specular;
    Secondary sec = trace_secondary(mask, origin, dir, rng);
    ll_cell_candidates = true;
    if (sec.hit) {
        vec3 cached;
        if (!cache_lookup(sec.at, sec.n, cached)) {
            // (round PERF5) a cache miss: the path goes on in the continued paths' pass, which stores the samples
            queue_continued(i, take_specular, rng, pdf, dir, sec);
            return;
        }
        sec.beyond = good3(cached);
    }
    store_samples(i, s, true, take_specular, dir, pdf, sec);
}
#endif

#ifdef PASS_CONTINUE
// (round PERF5) The paths the sample pass handed on (queue_continued): each continued past its first vertex's cache miss
// (continue_path), the light it found added to the miss's cell, the pixel's samples stored. One thread a queued path,
// the queue's length the dispatch's (vkCmdDispatchIndirect: CONTINUE_GROUP threads a group).
void main()
{
    uint slot = gl_WorkGroupID.x * CONTINUE_GROUP + gl_LocalInvocationIndex;
    if (slot >= continued) {
        return;
    }
    uint at = continue_base() + slot * 2u;
    uvec4 e0 = res_a[at];
    uvec4 e1 = res_a[at + 1u];
    uint i = e0.x & 0x7FFFFFFFu;
    bool take_specular = (e0.x & 0x80000000u) != 0u;
    uint rng = e0.y;
    Secondary sec = continued_secondary(i);
    ll_cell_candidates = !take_specular;
    sec.beyond = continue_path(sec, rng);
    ll_cell_candidates = true;
    // (round PERF2) the continued path's light is a sample of the cell's bounce (E / pi - what the update paths add), so
    // it goes into the cache too. The update paths start at the pixels and follow the cosine lobe; cells only the sample
    // pass's rays reach (the specular rays' hits, surfaces behind the camera) never had an entry - every ray that met
    // them went on, frame after frame (BO at night, 1707 x 960: 4.6 ms of the pass's 7.1). The sample is the one an
    // update path's vertex adds there (SHaRC's update: every vertex of a path its own cell's sample).
    cache_add(sec.at, sec.n, sec.beyond, rng);
    store_samples(i, surface_of(now_at[i], now_more[i]), true, take_specular, uintBitsToFloat(e1.xyz),
                  uintBitsToFloat(e0.z), sec);
}
#endif

#ifdef PASS_TEMPORAL
// The pixel's reservoir after its sample and the temporal reuse (stored by main, after the boiling filter); `weight` its
// sample's contribution (target x W - what the filter compares). (Round WS) Also its specular reservoir and that one's
// contribution.
Reservoir temporal_reservoir(ivec2 local, uint i, out float weight, out Reservoir spec, out float spec_weight)
{
    weight = 0.0;
    spec = no_reservoir(0.0);
    spec_weight = 0.0;
    Surface s = surface_of(now_at[i], now_more[i]);
    if (!s.valid) {
        return no_reservoir(0.0);
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z ^ 0x68E31DA4u, pass.params.w + 5u);
    vec3 eye = pass.eye.xyz;
    float cap = float(pass.params.z);
    bool has_specular = max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0;

    // 1. (round PERF2) this frame's candidates - the sample pass's (above): the diffuse reservoir's and the specular's,
    // their targets here
    vec3 c_dir;
    float c_reach;
    float c_g;
    Reservoir r = load_a(i);
    float chosen = r.kind != KIND_NONE && r.W > 0.0 ? target_at(s, eye, r, c_dir, c_reach, c_g) : 0.0;
    if (!(chosen > 0.0)) {
        r = no_reservoir(r.M);
    }
    Reservoir spec_candidate = load_specular(false, i);
    float s_chosen = spec_candidate.kind != KIND_NONE && spec_candidate.W > 0.0
                         ? specular_target_at(s, eye, spec_candidate, c_dir, c_reach, c_g) : 0.0;
    if (!(s_chosen > 0.0)) {
        spec_candidate = no_reservoir(spec_candidate.M);
    }

    // 2. the reservoir this point had last frame, its sample reconnected here - (round PT4d) a neighbour's in its 4 x 4
    // block by the frame's permutation (vulkan_light_list.h restir_denoiser_glsl). (Round WS) Its pixel's surface found
    // once for both reservoirs.
    bool has_then = false;
    uint j = 0u;
    Surface then = no_surface();
    bool has_then_spec = false;
    uint j_spec = 0u;
    Surface then_spec = no_surface();
    if ((pass.counts.w & FLAG_TEMPORAL) != 0u) {
        vec4 q = pass.prev_to_pixel * vec4(s.at, 1.0);
        if (q.w > 0.0) {
            // (round BUDGET) the cell that held the point's pixel last frame
            ivec2 before = ivec2(floor((q.xy / q.w - vec2(pass.rect.xy)) / float(trace_scale())));
            // (round WS) the specular reservoir's history is the point's own pixel's: permutation sampling hands a
            // neighbour's - another view of the lobe, its samples in this lobe's tails (the check's floor: x 5 the
            // pixel-frames over 4 x their mean with it, none without)
            if (all(greaterThanEqual(before, ivec2(0))) && all(lessThan(before, cells()))) {
                j_spec = cell_index(before);
                then_spec = surface_of(before_at[j_spec], before_more[j_spec]);
                has_then_spec = alike(s, then_spec);
            }
            if ((pass.counts.w & FLAG_PERMUTE) != 0u) {
                before = restir_permuted(before, pass.counts.z);
            }
            if (all(greaterThanEqual(before, ivec2(0))) && all(lessThan(before, cells()))) {
                j = cell_index(before);
                then = surface_of(before_at[j], before_more[j]);
                has_then = alike(s, then);
            }
        }
    }
    float kept_target = chosen;
    if (has_then) {
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
            // (round PERF3) the one ray either choice traces, then what it decides - one inlined light ray for the two
            bool take_h = path_next(rng) * total < w_h;
            Surface from = s;
            vec3 ray_dir = h_dir;
            float ray_reach = h_reach;
            bool traced = true;
            if (!take_h) {
                from = then;
                float t_g;
                traced = target_at(then, pass.prev_eye.xyz, r, ray_dir, ray_reach, t_g) > 0.0;
            }
            bool visible = traced && seen_at_all(seen(from, ray_dir, ray_reach));
            if (take_h) {
                // its light, seen from here: then this pixel's sample could have been it too
                if (visible) {
                    out_r = h;
                    target = t_h;
                    z = h.M + r.M;
                }
            } else {
                out_r = r;
                target = chosen;
                z = r.M;
                // could last frame's reservoir have made it: its surface's target and a ray from there
                if (visible) {
                    z += h.M;
                }
            }
            out_r.M = r.M + h.M;
            out_r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
            kept_target = target;
        }
        r = out_r;
    }
    weight = r.kind != KIND_NONE && r.W > 0.0 ? good(kept_target * r.W) : 0.0;

    // 3. the surface's specular (round WS: a reservoir of its own - specular_target_of): the candidate the specular lobe's
    // ray of step 1 when the pixel took that lobe (round BR: every surface's - before, those of roughness 0.3 and more took
    // it from the diffuse reservoir's sample, which lands in a GGX lobe by chance), then last frame's specular reservoir
    // reconnected here as the diffuse one is (its target at this view, the Jacobian validated - jacobian_kept)
    if (has_specular) {
        spec = spec_candidate;
        float s_kept = s_chosen;
        if (has_then_spec && (pass.counts.w & FLAG_SPECULAR_REUSE) != 0u && s.rough >= SPECULAR_REUSE_ROUGHNESS &&
            rough_alike(s, then_spec)) {
            // the generalised balance heuristic (Lin et al. 2022): each sample's weight its share of both reservoirs'
            // M x target at it in the area measure (specular_area_target) - the history's lobe looks elsewhere than this
            // one's: with 1 / Z its sample, landing in this lobe's peak from the history's tail, came out x 100s (the
            // check's near-mirror floor -43 % under the boiling filter, +12 % without)
            Reservoir h = load_specular(true, j_spec);
            h.M = min(h.M, cap);
            float g_h_here;
            float g_h_then;
            float a_h_here = h.kind != KIND_NONE && h.W > 0.0 ? specular_area_target(s, eye, h, g_h_here) : 0.0;
            float a_h_then = a_h_here > 0.0 ? specular_area_target(then_spec, pass.prev_eye.xyz, h, g_h_then) : 0.0;
            // (the targets with the visibility: the history's sample seen from here, this pixel's from there - each
            // reservoir holds only samples its surface sees, as its rays found them)
            if (a_h_here > 0.0 && a_h_then > 0.0 && !unblocked(s, h)) {
                a_h_here = 0.0;
            }

            float jacobian = a_h_then > 0.0 ? good(g_h_here / g_h_then) : 0.0;
            if (!jacobian_kept(jacobian)) {
                a_h_here = 0.0;
                a_h_then = 0.0;
            }
            float g_r_here;
            float g_r_then;
            float a_r_here = spec.kind != KIND_NONE && s_chosen > 0.0 ? specular_area_target(s, eye, spec, g_r_here) : 0.0;
            float a_r_then = a_r_here > 0.0 ? specular_area_target(then_spec, pass.prev_eye.xyz, spec, g_r_then) : 0.0;
            if (a_r_then > 0.0 && !jacobian_kept(g_r_then / g_r_here)) {
                a_r_then = 0.0;
            }
            if (a_r_then > 0.0 && !unblocked(then_spec, spec)) {
                a_r_then = 0.0;
            }

            float m_r = a_r_here > 0.0 ? spec.M * a_r_here / (spec.M * a_r_here + h.M * a_r_then) : 0.0;
            float m_h = a_h_then > 0.0 && a_h_here > 0.0 ? h.M * a_h_then / (h.M * a_h_then + spec.M * a_h_here) : 0.0;
            float t_h = a_h_here > 0.0 ? a_h_here / g_h_here : 0.0;
            float w_r = good(m_r * s_chosen * spec.W);
            float w_h = good(m_h * t_h * h.W * jacobian);
            float total = w_r + w_h;
            Reservoir out_r = no_reservoir(spec.M + h.M);
            if (total > 0.0) {
                bool take_h = path_next(rng) * total < w_h;
                out_r = take_h ? h : spec;
                float target = take_h ? t_h : s_chosen;
                out_r.M = spec.M + h.M;
                out_r.W = target > 0.0 ? good(total / target) : 0.0;
                s_kept = target;
            }
            spec = out_r;
        }
        spec_weight = spec.kind != KIND_NONE && spec.W > 0.0 ? good(s_kept * spec.W) : 0.0;
    }
    return r;
}
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    bool inside = all(lessThan(local, cells()));
    uint i = cell_index(local);
    float weight = 0.0;
    float spec_weight = 0.0;
    Reservoir r = no_reservoir(0.0);
    Reservoir spec = no_reservoir(0.0);
    if (inside) {
        r = temporal_reservoir(local, i, weight, spec, spec_weight);
    }
    // (round PT4d) the boiling filter - every invocation of the group takes part (no early return above); (round WS) the
    // specular reservoirs' apart from the diffuse ones (their contributions are of another lobe)
    if ((pass.counts.w & FLAG_BOILING) != 0u && restir_boiling(weight)) {
        r = no_reservoir(0.0);
    }
    // (every invocation has read the group's mean before the second filter writes the shared sums again)
    memoryBarrierShared();
    barrier();
    if ((pass.counts.w & (FLAG_BOILING | FLAG_SPECULAR_REUSE)) == (FLAG_BOILING | FLAG_SPECULAR_REUSE) && restir_boiling(spec_weight)) {
        spec = no_reservoir(0.0);
    }
    if (inside) {
        store_reservoir(false, i, r);
        store_specular(false, i, spec);
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
    if (trace_scale() > 1) {
        // (round BUDGET) the cell's light, demodulated - PASS_UPSAMPLE puts it into the view's pixels
        GiResults results = GiResults(pass.results.xy);
        results.rows[2u * i] = vec4(diffuse, s.valid ? 1.0 : 0.0);
        results.rows[2u * i + 1u] = vec4(specular, hit);
        return;
    }
    if (s.valid) {
        // (the store's environment gone where this lit the surface: its sky and bounce are these samples)
        vec4 vis = imageLoad(io_visibility, p);
        // (round FA - a debug view without the sky: nor the disc's direct light)
        imageStore(io_visibility, p, vec4((pass.counts.w & FLAG_NO_SKY) != 0u ? 0.0 : vis.r, vis.g, 0.0, vis.a));
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
// (round WS) The specular reservoir's spatial reuse and its result: the pixel's reservoir and, for a lobe of
// SPECULAR_REUSE_ROUGHNESS and more, its neighbours' of the same surface and a like roughness within a radius that
// shrinks with the lobe, resampled toward this pixel's specular target (each reconnected here, the Jacobian validated;
// combined by the generalised balance heuristic); the chosen sample traced from the pixel (the one visibility ray). Returns the specular over E (demodulated); `hit` the sample's
// distance (Ray Reconstruction's specular hit distance), `full` its light (the check's).
vec3 specular_spatial(ivec2 local, uint i, Surface s, vec3 e, inout uint rng, out float hit, out vec3 full)
{
    hit = GI_MISS_DISTANCE;
    full = vec3(0.0);
    if (!(max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0)) {
        return vec3(0.0);
    }
    vec3 eye = pass.eye.xyz;
    float cap = float(pass.params.z);
    Reservoir taken[MOST_SPATIAL + 1u];
    Surface where[MOST_SPATIAL + 1u];
    uint n = 1u;
    taken[0] = load_specular(false, i);
    where[0] = s;
    if ((pass.counts.w & (FLAG_SPATIAL | FLAG_SPECULAR_REUSE)) == (FLAG_SPATIAL | FLAG_SPECULAR_REUSE) &&
        s.rough >= SPECULAR_REUSE_ROUGHNESS) {
        float radius_most = pass.tune.x * clamp(s.rough * 2.0, 0.25, 1.0);
        for (uint k = 0u; k < min(SPECULAR_NEIGHBOURS, MOST_SPATIAL); ++k) {
            float angle = 2.0 * PATH_PI * path_next(rng);
            float radius = radius_most * sqrt(path_next(rng));
            ivec2 q = local + ivec2(round(vec2(cos(angle), sin(angle)) * radius));
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, cells())) || q == local) {
                continue;
            }
            uint j = cell_index(q);
            Surface other = surface_of(now_at[j], now_more[j]);
            if (!alike(s, other) || !rough_alike(s, other)) {
                continue;
            }
            taken[n] = load_specular(false, j);
            taken[n].M = min(taken[n].M, cap);
            where[n] = other;
            ++n;
        }
    }
    // the generalised balance heuristic: each sample's weight its share of every reservoir's M x target at it (in the
    // area measure, the reconnection validated), its target here x its W x the Jacobian
    float a[(MOST_SPATIAL + 1u) * (MOST_SPATIAL + 1u)]; // a[j * (MOST_SPATIAL + 1) + k]: sample k's at surface j
    float g[(MOST_SPATIAL + 1u) * (MOST_SPATIAL + 1u)];
    // (with the visibility, as each reservoir's samples are ones its surface sees - a reservoir's own sample was seen
    // from it: an any-hit ray for each cross term)
    for (uint k = 0u; k < n; ++k) {
        bool live = taken[k].kind != KIND_NONE && taken[k].W > 0.0;
        for (uint jj = 0u; jj < n; ++jj) {
            uint at = jj * (MOST_SPATIAL + 1u) + k;
            float gj = 0.0;
            a[at] = live ? specular_area_target(where[jj], eye, taken[k], gj) : 0.0;
            g[at] = gj;
            if (a[at] > 0.0 && jj != k && !unblocked(where[jj], taken[k])) {
                a[at] = 0.0;
            }
        }
    }
    float total = 0.0;
    uint chosen = 0xFFFFFFFFu;
    float target = 0.0;
    for (uint k = 0u; k < n; ++k) {
        uint own = k * (MOST_SPATIAL + 1u) + k;
        uint here = k; // (surface 0's row)
        if (!(a[own] > 0.0) || !(a[here] > 0.0)) {
            continue;
        }
        float jacobian = k == 0u ? 1.0 : good(g[here] / g[own]);
        if (!jacobian_kept(jacobian)) {
            continue;
        }
        float all_m = 0.0;
        for (uint jj = 0u; jj < n; ++jj) {
            uint at = jj * (MOST_SPATIAL + 1u) + k;
            if (jj != k && !jacobian_kept(g[at] / g[own])) {
                continue;
            }
            all_m += taken[jj].M * a[at];
        }
        float t = a[here] / g[here];
        float w = all_m > 0.0 ? good(taken[k].M * a[own] / all_m * t * taken[k].W * jacobian) : 0.0;
        total += w;
        if (w > 0.0 && path_next(rng) * total < w) {
            chosen = k;
            target = t;
        }
    }
    if (chosen == 0xFFFFFFFFu || !(target > 0.0)) {
        return vec3(0.0);
    }
    vec3 chosen_dir;
    float chosen_reach;
    float g_unused;
    if (!sample_from(s, taken[chosen], chosen_dir, chosen_reach, g_unused)) {
        return vec3(0.0);
    }
    // what the seen-through surfaces let through (nothing solid stands in the way: its weight's ray, or its own)
    vec3 through = seen_through(rays_of(s), s.at + s.ng * ray_offset(s.at), chosen_dir, chosen_reach);
    float w_chosen = good(total / target);
    Reservoir c = taken[chosen];
    hit = c.kind == KIND_SURFACE ? length(c.y - s.at) : GI_MISS_DISTANCE;
    full = good3(specular_lobe(s, view_of(s, eye), chosen_dir) * c.bounce * through * w_chosen);
    return good3(full / max(e, vec3(1.0e-4)));
}
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy); // (round BUDGET: a cell)
    if (any(greaterThanEqual(local, cells()))) {
        return;
    }
    ivec2 p = pass.rect.xy + cell_pixel(local);
    uint i = cell_index(local);
    Surface s = surface_of(now_at[i], now_more[i]);
    float cap = float(pass.params.z);
    if (!s.valid) {
        store_reservoir(true, i, no_reservoir(0.0));
        store_specular(true, i, no_reservoir(0.0));
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
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, cells())) || q == local) {
                continue;
            }
            uint j = cell_index(q);
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
    vec3 full = vec3(0.0);
    BrdfMaterial m = material_of(s);
    vec3 v = view_of(s, eye);
    float nv = max(dot(s.n, v), 1.0e-4);
    vec3 e = brdf_specular_albedo(s.f0, nv, s.rough);
    float hit;
    vec3 full_specular;
    vec3 specular = specular_spatial(local, i, s, e, rng, hit, full_specular);
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
#ifdef CHECK
                full = m.albedo * (1.0 - e) * (max(dot(s.n, chosen_dir), 0.0) / PATH_PI) * light;
#endif
            }
        }
    }
#ifdef CHECK
    full = (pass.counts.w & FLAG_SPECULAR_ONLY) != 0u ? full_specular : full + full_specular;
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
    store_specular(true, i, load_specular(false, i)); // (round WS: the same - its temporal reservoir)
    // (round FA - the debug views: a term left out adds nothing; a sample that saw the sky is the sky's)
    if ((pass.counts.w & (r.kind == KIND_SKY ? FLAG_NO_SKY : FLAG_NO_DIFFUSE)) != 0u) {
        diffuse = vec3(0.0);
    }
    if ((pass.counts.w & (hit < GI_MISS_DISTANCE ? FLAG_NO_SPECULAR : FLAG_NO_SKY)) != 0u) {
        specular = vec3(0.0);
    }
    put_result(p, i, s, diffuse, specular, hit, full);
}
#endif

#ifdef PASS_UPSAMPLE
// (round BUDGET) Each pixel's indirect light from the trace grid's four cells nearest it (PASS_SPATIAL's results,
// demodulated: the diffuse light before the albedo, the specular over E): the cells whose surface is this pixel's
// (alike - its normal, its plane) and of a like roughness, weighed by a tent over their pixels' distance - a depth- and
// normal-aware upsample, as half-resolution ray-traced GI is brought to the full resolution before the denoiser (Ray
// Reconstruction here). A pixel no like cell is near takes the likest one's (an edge a cell's pixel missed this frame).
// Then the inputs as PASS_SPATIAL wrote them before: the light added to ReSTIR DI's, the store's environment taken out.
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    ivec2 p = pass.rect.xy + local;
    Surface s = primary(p);
    if (!s.valid) {
        imageStore(o_specular_hit, p, vec4(0.0));
        return;
    }
    GiResults results = GiResults(pass.results.xy);
    int scale = trace_scale();
    ivec2 home = local / scale;
    float reach = scale > 1 ? 1.5 : 0.5; // (cells: at the view's own resolution a pixel's cell alone)
    // the pixel's own cell and the three beside it on its side of the cell's traced pixel (the nearest four)
    ivec2 within = local - cell_pixel(home);
    ivec2 toward = ivec2(within.x > 0 || (within.x == 0 && (local.x % scale) * 2 >= scale) ? 1 : -1,
                         within.y > 0 || (within.y == 0 && (local.y % scale) * 2 >= scale) ? 1 : -1);
    float least = pass.tune.z * length(s.at - pass.eye.xyz);
    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    float sum = 0.0;
    float hit = GI_MISS_DISTANCE;
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z ^ 0x3C6EF372u, pass.params.w + 9u);
    float likest = 0.0;
    uint likest_cell = 0xFFFFFFFFu;
    for (int dy = 0; dy <= (scale > 1 ? 1 : 0); ++dy) {
        for (int dx = 0; dx <= (scale > 1 ? 1 : 0); ++dx) {
            ivec2 c = home + ivec2(dx, dy) * toward;
            if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, cells()))) {
                continue;
            }
            uint j = cell_index(c);
            Surface o = surface_of(now_at[j], now_more[j]);
            if (!o.valid) {
                continue;
            }
            float likeness = max(dot(s.n, o.n), 0.0) / (1.0 + abs(dot(o.at - s.at, s.ng)) / max(least, 1.0e-6));
            if (likeness > likest) {
                likest = likeness;
                likest_cell = j;
            }
            float w = max(1.0 - length(vec2(cell_pixel(c) - local)) / (reach * float(scale)), 0.0);
            if (!(w > 0.0) || !alike(s, o) || !rough_alike(s, o)) {
                continue;
            }
            // one like cell, picked by its weight (each pixel's own draw: the cells' noise stays a pixel's, as Ray
            // Reconstruction takes noise - an average gave every cell's outlier to a 2 x 2 block it kept as detail)
            sum += w;
            if (path_next(rng) * sum < w) {
                vec4 e = results.rows[2u * j + 1u];
                diffuse = results.rows[2u * j].rgb;
                specular = e.rgb;
                hit = e.w;
            }
        }
    }
    if (!(sum > 0.0) && likest_cell != 0xFFFFFFFFu) {
        diffuse = results.rows[2u * likest_cell].rgb;
        vec4 e = results.rows[2u * likest_cell + 1u];
        specular = e.rgb;
        hit = e.w;
    }
    // (the store's environment gone where this lit the surface: its sky and bounce are these samples)
    vec4 vis = imageLoad(io_visibility, p);
    // (round FA - a debug view without the sky: nor the disc's direct light)
    imageStore(io_visibility, p, vec4((pass.counts.w & FLAG_NO_SKY) != 0u ? 0.0 : vis.r, vis.g, 0.0, vis.a));
    vec3 exposed = min(good3(diffuse) * pass.eye.w, vec3(65504.0));
    imageStore(io_lamps, p, vec4(min(imageLoad(io_lamps, p).rgb + exposed, vec3(65504.0)), 0.0));
    imageStore(io_lamps_on, p, vec4(min(imageLoad(io_lamps_on, p).rgb + exposed * s.albedo, vec3(65504.0)), 0.0));
    // (round BR: added to the lights' own specular - ReSTIR DI's, written before)
    vec3 specular_in = min(imageLoad(io_specular, p).rgb + good3(specular) * pass.eye.w, vec3(65504.0));
    imageStore(io_specular, p, vec4(specular_in, 1.0));
    imageStore(o_specular_hit, p, vec4(hit, 0.0, 0.0, 0.0));
}
#endif
)";

enum class Shader : uint8_t { kSky, kSample, kTemporal, kSpatial, kCache, kResolve, kContinue, kUpsample };

std::string float_text(float value) { return text::format("%.9g", static_cast<double>(value)); }

std::string shader_source(Shader which, bool check, bool cloud_map, SkyGridSource sky, bool shared = false)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    if (check) {
        source += "#define CHECK\n";
    }
    if (shared) {
        source += "#define PRIMARY_SHARED\n"; // (round PERF4: primary() - ReSTIR DI's surfaces alone)
    }
    if (which == Shader::kSky) {
        source += sky_grid_prelude(sky, kBindingTransmittance, kBindingSkyView, kBindingCloudNoise);
    }
    switch (which) {
    case Shader::kSample: source += "#define PASS_SAMPLE\n"; break;
    case Shader::kTemporal: source += "#define PASS_TEMPORAL\n"; break;
    case Shader::kSpatial: source += "#define PASS_SPATIAL\n"; break;
    case Shader::kCache: source += "#define PASS_CACHE\n"; break;
    case Shader::kResolve: source += "#define PASS_RESOLVE\n"; break;
    case Shader::kContinue: source += "#define PASS_CONTINUE\n"; break; // (round PERF5)
    case Shader::kUpsample: source += "#define PASS_UPSAMPLE\n"; break; // (round BUDGET)
    case Shader::kSky: break;
    }
    source += "const uint CACHE_CAPACITY = " + std::to_string(kCacheCapacity) + "u;\n";
    source += "const uint CACHE_PROBES = " + std::to_string(kCacheProbes) + "u;\n";
    source += "const uint CACHE_STALE = " + std::to_string(kCacheStale) + "u;\n";
    source += "const uint CACHE_COUNT_MOST = 65536u;\n"; // (round PERF: an entry's sample count, capped)
    source += "const uint CACHE_BLOCK = " + std::to_string(kCacheBlock) + "u;\n";
    source += "const uint CACHE_PATH = " + std::to_string(kCachePath) + "u;\n";
    source += "const uint RESOLVE_GROUP = " + std::to_string(kResolveGroup) + "u;\n";
    source += "const float CACHE_CELL_LEAST = " + float_text(kCacheCellLeast) + ";\n";
    source += "const float CACHE_SCALE = " + float_text(kCacheScale) + ";\n";
    source += "const float CACHE_SAMPLES = " + float_text(kCacheSamples) + ";\n";
    source += "const float CACHE_FRAMES = " + float_text(kCacheFrames) + ";\n";
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
    if ((which == Shader::kSample || which == Shader::kCache || which == Shader::kContinue) && cloud_map) {
        source += "#define CLOUD_MAP\n" + cloud_map_glsl(kBindingCloudMap);
    }
    source += "const uint SKY_WIDTH = " + std::to_string(rt::lights::kSkyGridWidth) + "u;\n";
    source += "const uint SKY_HEIGHT = " + std::to_string(rt::lights::kSkyGridHeight) + "u;\n";
    source += kCommon;
    source += light_instances_glsl();
    source += rt::lights::kLightListGlsl;
    // (round OM) the local lights' next-event picks from the presampled pool, read through its address (the pushed set
    // is full: 32 bindings)
    source += light_pool_reader_glsl("pass.pool");
    source += light_grid_reader_glsl("pass.grid"); // (round DO: inside the light grid, the candidates its cell's)
    source += "#define LL_LOCAL_CANDIDATES pass.params.x\n"; // (round DD: the local lights' RIS candidates)
    source += local_light_glsl();
    if (which == Shader::kSky) {
        source += sky_grid_glsl();
    } else {
        // (round WF) the list's sampling and the cache's lookups, shared (vulkan_light_list.h light_sample_glsl)
        std::string body = kGiBody;
        const std::size_t at = body.find("@LIGHT_SAMPLE@");
        body.replace(at, std::string_view("@LIGHT_SAMPLE@").size(), light_sample_glsl());
        source += body;
    }
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

// (round MB) The angle one pixel spans at the view's centre (radians): the radiance cache's cells are this many pixels
// wide where they are seen, whatever the resolution - so its update paths (one a block of pixels) reach each about as often.
double pixel_angle(const Matrix& to_world, const rt::ViewDrawn& view)
{
    const auto apply = [&to_world](double x, double y) {
        std::array<double, kMatrixSide> out{};
        const std::array<double, kMatrixSide> in{x, y, 0.5, 1.0};
        for (std::size_t row = 0; row < kMatrixSide; ++row) {
            for (std::size_t column = 0; column < kMatrixSide; ++column) {
                core::at(out, row) += core::at(core::at(to_world, row), column) * core::at(in, column);
            }
        }
        const double w = std::abs(core::at(out, 3)) > kSingular ? core::at(out, 3) : 1.0;
        return std::array<double, 3>{core::at(out, 0) / w, core::at(out, 1) / w, core::at(out, 2) / w};
    };
    const double cx = core::at(view.rect, 0) + (0.5 * core::at(view.rect, 2));
    const double cy = core::at(view.rect, 1) + (0.5 * core::at(view.rect, 3));
    const std::array<double, 3> a = apply(cx, cy);
    const std::array<double, 3> b = apply(cx + 1.0, cy);
    double apart = 0.0;
    double away = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        const double eye = core::at(view.camera.position, k);
        apart += (core::at(b, k) - core::at(a, k)) * (core::at(b, k) - core::at(a, k));
        away += (core::at(a, k) - eye) * (core::at(a, k) - eye);
    }
    return away > 0.0 ? std::sqrt(apart / away) : 0.0;
}

// --- the state ---------------------------------------------------------------------------------------------------------

struct Timing {
    VkQueryPool pool = VK_NULL_HANDLE;
    bool unavailable = false;
    double period_ns = 1.0;
    std::array<uint64_t, kTimingSlots> pending{};
    double sum_ms = 0.0;
    double cache_sum_ms = 0.0;
    uint32_t measured = 0;
};

struct GiState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    std::array<VkPipeline, kSkyGridSources> sky{};
    std::array<VkPipeline, 8> sample{}; // (round PERF2) each pixel's secondary path [shared x 4 + check x 2 + cloud]
    std::array<VkPipeline, 4> temporal{}; // [check x 2 + cloud map]
    std::array<VkPipeline, 4> continued{}; // (round PERF5) the paths continued past a cache miss [check x 2 + cloud map]
    std::array<VkPipeline, 2> spatial{};  // [check]
    std::array<VkPipeline, 2> upsample{}; // (round BUDGET) the game's [shared]
    std::array<VkPipeline, 8> cache_update{}; // (round MB) the cache's update paths [shared x 4 + check x 2 + cloud]
    bool shared_variant_failed = false;          // (round PERF4: primary_pipeline)
    VkPipeline resolve = VK_NULL_HANDLE;  // ... and its entries' frame
    bool failed = false;
    // (round MB) the radiance cache (cleared when the history starts again)
    DeviceBuffer cache;
    bool cache_clear = true;
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
    float local_share = 0.0F; // (round OM) the list's local group's share of its picks (the pool's pdf x it)
    bool pool_off = false;    // (round OM - the checks') the local lights picked from the list itself, as before
    bool grid_off = false;    // (round DO - the checks') the local lights' candidates from the pool alone, as before
    // per cell of the trace grid (round BUDGET; the results: PASS_SPATIAL's light for PASS_UPSAMPLE)
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t scale = 0;
    DeviceBuffer results;
    DeviceBuffer reservoirs_a;
    DeviceBuffer reservoirs_b;
    std::array<DeviceBuffer, 2> at{};
    std::array<DeviceBuffer, 2> more{};
    DeviceBuffer specular_reservoirs;
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
    FrameLightList frame_list; // (round WF) the full list and the cache this frame (restir_gi_frame_lights)
    uint64_t hit_frame = 0; // (the frame whose view wrote specular_hit)
    uint64_t shared_views = 0; // (round OM) views whose primary surfaces were ReSTIR DI's
    uint64_t passes = 0;
    Timing timing;
    double last_ms = 0.0;
    double last_cache_ms = 0.0;
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

// (round FA) The debug view's terms as the pass's flags (none in the check).
uint32_t debug_flags(bool check)
{
    const uint32_t terms = check ? debug_term::kAll : debug_terms();
    return ((terms & debug_term::kGiDiffuse) == 0U ? kFlagNoDiffuse : 0U) |
           ((terms & debug_term::kGiSpecular) == 0U ? kFlagNoSpecular : 0U) |
           ((terms & debug_term::kSky) == 0U ? kFlagNoSky : 0U);
}

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
    VkPipeline& sample = core::at(own.sample, (check ? 2U : 0U) + (cloud_map ? 1U : 0U));
    if (sample == VK_NULL_HANDLE) {
        sample = make_compute(device, own.layout, shader_source(Shader::kSample, check, cloud_map, sky), error);
        if (sample == VK_NULL_HANDLE) {
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
    VkPipeline& continued = core::at(own.continued, (check ? 2U : 0U) + (cloud_map ? 1U : 0U));
    if (continued == VK_NULL_HANDLE) {
        continued = make_compute(device, own.layout, shader_source(Shader::kContinue, check, cloud_map, sky), error);
        if (continued == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& cache = core::at(own.cache_update, (check ? 2U : 0U) + (cloud_map ? 1U : 0U));
    if (cache == VK_NULL_HANDLE) {
        cache = make_compute(device, own.layout, shader_source(Shader::kCache, check, cloud_map, sky), error);
        if (cache == VK_NULL_HANDLE) {
            return false;
        }
    }
    if (own.resolve == VK_NULL_HANDLE) {
        own.resolve = make_compute(device, own.layout, shader_source(Shader::kResolve, false, false, sky), error);
        if (own.resolve == VK_NULL_HANDLE) {
            return false;
        }
    }
    VkPipeline& spatial = core::at(own.spatial, check ? 1U : 0U);
    if (spatial == VK_NULL_HANDLE) {
        spatial = make_compute(device, own.layout, shader_source(Shader::kSpatial, check, false, sky), error);
        if (spatial == VK_NULL_HANDLE) {
            return false;
        }
    }
    // (round BUDGET) the game's upsample with a trace grid coarser than the pixels (the check's light stays at its pixels)
    VkPipeline& upsample = core::at(own.upsample, 0);
    if (!check && kTraceScale > 1 && upsample == VK_NULL_HANDLE) {
        upsample = make_compute(device, own.layout, shader_source(Shader::kUpsample, false, false, sky), error);
        if (upsample == VK_NULL_HANDLE) {
            return false;
        }
    }
    return true;
}

// (round PERF4) The sample or cache pass's pipeline for a view: the PRIMARY_SHARED variant when it reads ReSTIR DI's
// primary surfaces (made on its first use), else the one ensure_pipelines made (its own camera ray).
VkPipeline primary_pipeline(Backend& state, Shader which, bool check, bool cloud_map, SkyGridSource sky, bool shared)
{
    GiState& own = gi_state();
    std::array<VkPipeline, 8>& pipelines = which == Shader::kSample ? own.sample : own.cache_update;
    const std::size_t base = (check ? 2U : 0U) + (cloud_map ? 1U : 0U);
    if (shared && !own.shared_variant_failed) {
        VkPipeline& variant = core::at(pipelines, 4U + base);
        if (variant == VK_NULL_HANDLE) {
            std::string error;
            variant = make_compute(state.context.device(), own.layout,
                                   shader_source(which, check, cloud_map, sky, true), &error);
            if (variant == VK_NULL_HANDLE) {
                own.shared_variant_failed = true;
                get_log("restir gi: the shared-surface variant couldn't be made (%s) - the passes find their own",
                        error.c_str());
            }
        }
        if (variant != VK_NULL_HANDLE) {
            return variant;
        }
    }
    return core::at(pipelines, base);
}

// (round BUDGET) The upsample's pipeline: the PRIMARY_SHARED variant for a view whose surfaces ReSTIR DI found, else the
// one ensure_pipelines made (its own camera ray a pixel).
VkPipeline upsample_pipeline(Backend& state, bool shared)
{
    GiState& own = gi_state();
    if (shared && !own.shared_variant_failed) {
        VkPipeline& variant = core::at(own.upsample, 1);
        if (variant == VK_NULL_HANDLE) {
            std::string error;
            variant = make_compute(state.context.device(), own.layout,
                                   shader_source(Shader::kUpsample, false, false, SkyGridSource::kEven, true), &error);
            if (variant == VK_NULL_HANDLE) {
                own.shared_variant_failed = true;
                get_log("restir gi: the shared-surface upsample couldn't be made (%s) - it finds its own", error.c_str());
            }
        }
        if (variant != VK_NULL_HANDLE) {
            return variant;
        }
    }
    return core::at(own.upsample, 0);
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
    // (round PF) the table's light in the global unit, not its exposure: the camera's exposure moves every frame at night,
    // and the grid was made again every other frame for the same sky (its alias tables built on the main thread - 17 %
    // of its time)
    key = mix(key, std::bit_cast<uint32_t>(lighting.table_share));
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
    constexpr std::size_t kTrianglesKind = 2;
    constexpr std::size_t kLampsKind = 3;
    own.local_share = static_cast<float>(core::at(list.kind_pick, kTrianglesKind) + core::at(list.kind_pick, kLampsKind));
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
                                     .queryCount = kTimingSlots * kTimingStamps,
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
    // (the passes' start, the radiance cache's end - round MB -, the passes' end)
    std::array<uint64_t, kTimingStamps> ticks{};
    if (vkGetQueryPoolResults(state.context.device(), timing.pool, slot * kTimingStamps, kTimingStamps, sizeof(ticks),
                              ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS ||
        ticks.back() < ticks.front() || ticks.at(1) < ticks.front()) {
        return;
    }
    constexpr double kNsPerMs = 1e6;
    timing.sum_ms += static_cast<double>(ticks.back() - ticks.front()) * timing.period_ns / kNsPerMs;
    timing.cache_sum_ms += static_cast<double>(ticks.at(1) - ticks.front()) * timing.period_ns / kNsPerMs;
    if (++timing.measured >= (check ? kCheckTimingReport : kTimingReport)) {
        own.last_ms = timing.sum_ms / timing.measured;
        own.last_cache_ms = timing.cache_sum_ms / timing.measured;
        if (!check) {
            get_log("restir gi: %.3f ms a frame on the device over the last %u (%ux%u; %u lights listed; the radiance "
                    "cache %.3f ms of it)",
                    own.last_ms, timing.measured, own.timed_width, own.timed_height, own.entries, own.last_cache_ms);
        }
        timing.sum_ms = 0.0;
        timing.cache_sum_ms = 0.0;
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

bool restir_gi_chosen(const Backend& state)
{
    const RtGiSettings settings = rt_gi_settings_locked();
    const bool restir = settings.indirect == RtIndirectLight::kRestir ||
                        (settings.indirect == RtIndirectLight::kAuto && ray_reconstruction_active());
    return restir && !gi_state().failed && state.context.ray_tracing_enabled();
}

bool restir_gi_wanted(Backend& state) { return restir_gi_chosen(state) && surface_inputs(state) != nullptr; }

bool restir_gi_lit_view(const Backend& /*state*/) { return gi_state().lit_view; }

bool restir_gi_frame_lights(const Backend& state, FrameLightList* out)
{
    const GiState& own = gi_state();
    if (!own.lit_view || own.frame_list.frame != state.frame || own.frame_list.frame == 0) {
        return false;
    }
    *out = own.frame_list;
    return true;
}

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

void set_restir_gi_pool(bool on)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().pool_off = !on;
}

void set_restir_gi_grid(bool on)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().grid_off = !on;
}

void restart_restir_gi_sequence()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().frames = 0;
    gi_state().history = false;
    gi_state().cache_clear = true;
}

void forget_restir_gi_history()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    gi_state().history = false;
    gi_state().cache_clear = true;
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
                         .cache_ms = own.last_cache_ms,
                         .gpu_width = own.timed_width,
                         .gpu_height = own.timed_height,
                         .shared_views = own.shared_views};
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
    // the per-cell buffers (a new size: no history) - (round BUDGET) the trace grid's cells: the game's a 2 x 2 block of
    // pixels each, the check's a pixel
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const uint32_t scale = check ? 1U : kTraceScale;
    const uint32_t cells_x = (width + scale - 1) / scale;
    const uint32_t cells_y = (height + scale - 1) / scale;
    const std::size_t cells = static_cast<std::size_t>(cells_x) * cells_y;
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (own.width != width || own.height != height || own.scale != scale) {
        own.history = false;
        own.width = width;
        own.height = height;
        own.scale = scale;
        own.restart = true;
    }
    // (round PERF5: res_a holds the continued paths' queue after the cells' reservoirs - kContinueWords a path)
    bool made = ensure_buffer(state, &own.reservoirs_a, cells * 16 * (kReservoirWords + kContinueWords), storage,
                              BufferPlace::kDevice) &&
                ensure_buffer(state, &own.reservoirs_b, cells * 16 * kReservoirWords, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.specular_reservoirs, cells * 16 * kSpecularReservoirWords, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.results, scale > 1 ? cells * 2 * 16 : 16, storage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              BufferPlace::kDevice) &&
                ensure_buffer(state, &own.counters, kCountersBytes, storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                              BufferPlace::kDevice) &&
                (!check || ensure_buffer(state, &own.sums, pixels * 2 * 16, storage, BufferPlace::kDevice)) &&
                (check || ensure_image(state, &own.specular_hit, VK_FORMAT_R16_SFLOAT, extent,
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    for (std::size_t k = 0; k < 2 && made; ++k) {
        made = ensure_buffer(state, &core::at(own.at, k), cells * 16, storage, BufferPlace::kDevice) &&
               ensure_buffer(state, &core::at(own.more, k), cells * 16, storage, BufferPlace::kDevice);
    }
    // (round MB) the radiance cache - a new one starts empty
    if (made && own.cache.buffer == VK_NULL_HANDLE) {
        own.cache_clear = true;
    }
    made = made && ensure_buffer(state, &own.cache, VkDeviceSize{kCacheCapacity} * kCacheEntryBytes, storage,
                                 BufferPlace::kDevice);
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
                           (!check || own.overrides.boiling_filter ? kFlagBoiling : 0U) |
                           (!check || own.overrides.cache ? kFlagCache : 0U) |
                           ((check ? own.overrides.specular_reuse : kSpecularReuseInGame) ? kFlagSpecularReuse : 0U) |
                           (check && own.overrides.specular_only ? kFlagSpecularOnly : 0U) | debug_flags(check);
    block.counts = {own.entries, static_cast<uint32_t>(local.lamp_list.size()), static_cast<uint32_t>(own.frames), flags};
    const uint32_t candidates =
        check && own.overrides.local_candidates != 0U ? own.overrides.local_candidates : kLocalCandidates;
    block.params = {candidates, std::min(spatial, kMostSpatial), kHistoryCap, check ? own.overrides.seed : 0x6C8E9CF5U};
    block.tune = {kSpatialRadius / static_cast<float>(scale), kNormalAgreement, kPlaneShare, kSkyShare}; // (radius: cells)
    block.force = {own.overrides.roughness, own.overrides.f0, own.overrides.albedo_scale, check ? 1.0F : 0.0F};
    {
        // (round BUDGET) the trace grid and the frame's pixel of each cell; the cells' results
        const std::array<uint32_t, 2>& turn = core::at(kTraceTurns, static_cast<std::size_t>(own.frames % kTraceTurns.size()));
        const uint32_t within = scale > 1 ? (core::at(turn, 0) << 8U) | (core::at(turn, 1) << 16U) : 0U;
        block.trace = {cells_x, cells_y, scale | within, 0U};
        const VkBufferDeviceAddressInfo results_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = own.results.buffer};
        const VkDeviceAddress results = vkGetBufferDeviceAddress(state.context.device(), &results_info);
        block.results = {static_cast<uint32_t>(results & 0xFFFFFFFFULL), static_cast<uint32_t>(results >> 32U), 0U, 0U};
    }
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), scene.instances, 0U};
    (void)disc_of(lighting, &block.sun, &block.sun_radiance);
    block.cache_cells = {static_cast<float>(kCacheCellPixels * pixel_angle(to_world, view)), 0.0F, 0.0F, 0.0F};
    // (round OM) the local lights' next-event picks from ReSTIR DI's presampled pool (vulkan_light_list.h) - made for this
    // submission from the same local group the list holds
    if (!own.pool_off && local.light_pool != VK_NULL_HANDLE && local.entries > 0 && own.local_share > 0.0F) {
        const VkBufferDeviceAddressInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = local.light_pool};
        const VkDeviceAddress pool = vkGetBufferDeviceAddress(state.context.device(), &pool_info);
        block.pool = {static_cast<uint32_t>(pool & 0xFFFFFFFFULL), static_cast<uint32_t>(pool >> 32U), 1U,
                      std::bit_cast<uint32_t>(own.local_share)};
        // (round DO) ... and inside ReSTIR DI's light grid, from the cells' picks (when DI made them this frame)
        if (!own.grid_off && local.grid_picks != VK_NULL_HANDLE) {
            const VkBufferDeviceAddressInfo grid_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = local.grid_picks};
            const VkDeviceAddress grid = vkGetBufferDeviceAddress(state.context.device(), &grid_info);
            block.grid = {static_cast<uint32_t>(grid & 0xFFFFFFFFULL), static_cast<uint32_t>(grid >> 32U), 1U, 0U};
        }
    }
    // (round OM) ReSTIR DI's primary surfaces when they are this view's: the same frame, rectangle, camera and matrix
    const SharedPrimary shared = restir_shared_primary(state);
    if (shared.address != 0 && shared.check == check && shared.rect == block.rect && shared.to_world == block.to_world &&
        shared.eye == std::array<float, 3>{core::at(block.eye, 0), core::at(block.eye, 1), core::at(block.eye, 2)}) {
        block.shared = {static_cast<uint32_t>(shared.address & 0xFFFFFFFFULL), static_cast<uint32_t>(shared.address >> 32U),
                        1U, 0U};
        if (++own.shared_views == 1) {
            get_log("restir gi: the view's primary surfaces read from ReSTIR DI's (one camera ray a pixel for both)");
        }
    }
    block.sky_dist = {own.sky_integral > 0.0 ? static_cast<float>(1.0 / own.sky_integral) : 0.0F, 0.0F, 0.0F, 0.0F};
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
        vkCmdResetQueryPool(commands, own.timing.pool, slot * kTimingStamps, kTimingStamps);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timing.pool, slot * kTimingStamps);
    }
    if (own.restart) {
        if (check) {
            vkCmdFillBuffer(commands, own.sums.buffer, 0, VK_WHOLE_SIZE, 0);
        }
        vkCmdFillBuffer(commands, own.counters.buffer, 0, VK_WHOLE_SIZE, 0);
        own.restart = false;
    }
    if (own.cache_clear) {
        vkCmdFillBuffer(commands, own.cache.buffer, 0, VK_WHOLE_SIZE, 0);
        own.cache_clear = false;
    }
    // (round PERF5) the continued paths' queue emptied, its dispatch (0, 1, 1) - after the last frame's reads of it and the
    // counters' fill
    barrier(commands, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    constexpr std::array<uint32_t, 4> kNoPaths{0U, 1U, 1U, 0U};
    vkCmdUpdateBuffer(commands, own.counters.buffer, kContinueArgs, sizeof(kNoPaths), kNoPaths.data());
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
    const std::array<VkDescriptorBufferInfo, 19> buffers{
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
        whole(own.specular_reservoirs.buffer),
        whole(check ? own.sums.buffer : own.counters.buffer),
        whole(own.counters.buffer),
        whole(own.cache.buffer)};
    constexpr std::array<uint32_t, 19> kBufferBindings{
        kBindingGeometry,    kBindingBlock,       kBindingLights,     kBindingEmitters,  kBindingLamps,
        kBindingMembership,  kBindingSlots,       kBindingSkyCells,   kBindingSkyAlias,  kBindingReservoirsA,
        kBindingReservoirsB, kBindingNowAt,       kBindingNowMore,    kBindingBeforeAt,  kBindingBeforeMore,
        kBindingSpecularReservoirs, kBindingSums,       kBindingCounters,   kBindingCache};
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
    const uint32_t groups_x = (cells_x + kGroupSide - 1) / kGroupSide; // (round BUDGET: the trace grid's cells)
    const uint32_t groups_y = (cells_y + kGroupSide - 1) / kGroupSide;
    if ((flags & kFlagCache) != 0U) {
        // (round MB) the radiance cache: its update paths (one a block of pixels), then each entry's frame - before the
        // temporal pass reads it at its secondary vertices
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiCache); // (round PF)
        // (round BUDGET: blocks of cells - one update path a CACHE_BLOCK x CACHE_BLOCK block of the trace grid)
        const uint32_t blocks_x = (cells_x + kCacheBlock - 1) / kCacheBlock;
        const uint32_t blocks_y = (cells_y + kCacheBlock - 1) / kCacheBlock;
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          primary_pipeline(state, Shader::kCache, check, map_now, sky_source,
                                           core::at(block.shared, 2) != 0U));
        vkCmdDispatch(commands, (blocks_x + kGroupSide - 1) / kGroupSide, (blocks_y + kGroupSide - 1) / kGroupSide, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.resolve);
        vkCmdDispatch(commands, kCacheCapacity / kResolveGroup, 1, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, own.timing.pool, (slot * kTimingStamps) + 1);
    }
    {
        // (round PERF2) each pixel's secondary path - its own pass before the reuse (RTXDI's ReSTIR GI: the initial
        // samples, then the temporal resampling)
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiSamples);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          primary_pipeline(state, Shader::kSample, check, map_now, sky_source,
                                           core::at(block.shared, 2) != 0U));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    {
        // (round PERF5) the paths continued past a cache miss at their first vertex - their own pass over the queue the
        // sample pass made (one thread a path; its length the dispatch's), which stores those pixels' samples
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiContinued);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          core::at(own.continued, (check ? 2U : 0U) + (map_now ? 1U : 0U)));
        vkCmdDispatchIndirect(commands, own.counters.buffer, kContinueArgs);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    {
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiTemporal); // (round PF)
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                          core::at(own.temporal, (check ? 2U : 0U) + (map_now ? 1U : 0U)));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    {
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiSpatial);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.spatial, check ? 1U : 0U));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
    if (!check && scale > 1) {
        // (round BUDGET) the cells' light into the view's pixels (the inputs)
        const FrameStep step(state.frames, commands, gpu_markers::section::kGiUpsample);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, upsample_pipeline(state, core::at(block.shared, 2) != 0U));
        vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (slot * kTimingStamps) + 2);
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
    // (round WF) the list and the cache as this frame used them - the passes after that light what the prepass doesn't hold
    own.frame_list = FrameLightList{
        .frame = check ? 0 : state.frame,
        .table = own.lights.buffer,
        .emitters = local.emitters,
        .lamps = local.lamps,
        .membership = local.membership,
        .slots = local.slots,
        .sky_cells = core::at(own.sky_cells, static_cast<std::size_t>(own.sky_now)).buffer,
        .sky_alias = own.sky_alias.buffer,
        .cache = own.cache.buffer,
        .entries = own.entries,
        .cache_on = (flags & kFlagCache) != 0,
        .sun = block.sun,
        .sun_radiance = block.sun_radiance,
        .sky_dist = core::at(block.sky_dist, 0),
        .cache_cells = core::at(block.cache_cells, 0),
        .eye = {core::at(view.camera.position, 0), core::at(view.camera.position, 1), core::at(view.camera.position, 2)},
        .pool = block.pool, // (round SK: the pool its next-event estimation resampled from - the list's other consumers' too)
        .grid = block.grid}; // (round DO: ... and the light grid's picks)
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
    for (VkPipeline& pipeline : own.sample) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.temporal) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.continued) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.upsample) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.spatial) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    for (VkPipeline& pipeline : own.cache_update) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    if (own.resolve != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, own.resolve, nullptr);
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
                                 &core::at(own.at, 1), &core::at(own.more, 0), &core::at(own.more, 1), &own.specular_reservoirs,
                                 &own.sums, &own.counters, &own.cache, &own.results}) {
        state.memory.destroy_buffer(buffer);
    }
    state.memory.destroy_image(&own.specular_hit);
    const RestirGiOverrides overrides = own.overrides;
    own = GiState{};
    own.overrides = overrides;
}

} // namespace render::vulkan
