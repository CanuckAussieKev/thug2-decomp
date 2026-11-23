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
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/frame_profile.h"
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
constexpr uint32_t kBindingBounds = 19;
constexpr uint32_t kBindingCells = 20;
constexpr uint32_t kBindingSpecularOut = 21; // (round BR)
constexpr uint32_t kBindingLightPool = 22;   // (round PF) the presampled pool (vulkan_light_list.h kLightPoolSize)
constexpr uint32_t kBindingGridPicks = 23;   // (round DO) the light grid's picks (vulkan_light_list.h kLightGridPicks)
constexpr uint32_t kBindingCount = 24;
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
// (round PERF) 8 initial candidates - RTXDI's default (8 local light samples a pixel). 16 cost twice the candidates'
// light evaluations for no measurable gain under the reuse (BO at night, the bank, 60 frames held, 1280 x 720 drawn:
// each pixel's temporal deviation 0.49 -> 0.46 of 255, the specks the same, the mean 48.9 -> 49.1) - the candidates
// and temporal pass 4.1 - 4.9 -> 2.9 ms. The history's cap is in candidates (kHistoryCap x them): the same 20 frames.
// (round BUDGET) 16: with the prepared records and presampled points (light_point_at) a candidate is a few loads and the
// geometry term, and with one spatial neighbour (below) the 8 left the check's lamp drawn in 1 texel in 64 over its
// per-frame error bound (1.69 of the mean; 16: 1.31 - 2 neighbours and their rays had it at 1.15).
constexpr uint32_t kInitialCandidates = 16;
// (round BUDGET) One spatial neighbour - RTXDI's default (numSpatialSamples 1): with the shading's one ray and Z by the
// targets (no ray a neighbour) the pass is one light ray a pixel.
constexpr uint32_t kSpatialNeighbours = 1;
constexpr uint32_t kMostSpatial = 4;
constexpr uint32_t kHistoryCap = 20;
constexpr float kSpatialRadius = 24.0F;
constexpr float kNormalAgreement = 0.9F;
constexpr float kPlaneShare = 0.02F;
// (round PT4c) The light grid round the camera (kGridBody): cells across x and z, up y, a cell's side (world units -
// about 3 m), each cell's slots and the list's candidates each slot is resampled from. 32 x 16 x 32 cells of 128 reach
// about 2000 units from the camera each way across, 1000 up and down; a point beyond takes the list by power.
constexpr uint32_t kGridAcross = 32;
constexpr uint32_t kGridUp = 16;
constexpr float kGridSide = 128.0F;
// (round MV) 512 slots a cell - RTXDI's ReGIR default (lightsPerCell 512). A cell's slots are made again each frame and
// shared by every pixel of the cell (128 units): the error of the draw - how many slots the cell's main lamps got this
// frame - is the same over the whole region and lasts as long as the reservoirs keep it. With 64 that shared error was
// the largest broad error of the picture while the camera moved (BO at night, p1: the lamps' blotches 2.2 - 3.8 of 255
// in display luma against the converged picture, the light grid off 1.5 - 2.7, 512 slots 1.2 - 1.9); its size falls with
// the square root of the slots. Cost: the grid's build +0.6 ms at 1280 x 720 (ReSTIR DI 2.8 -> 3.4 ms).
constexpr uint32_t kGridSlots = 512;
// (round NOISE2) A cell's slots are made by one workgroup of kCellGroup invocations (kGridBody PASS_CELLS: the cell's own
// CDF over the list, drawn by systematic resampling), each kGridSlots / kCellGroup slots.
constexpr uint32_t kCellGroup = 128; // (256: +5 %, 64: +10 % - TR and NO at night)
// (a round of the cell's CDF in shared memory: 4 KB - 16 KB cost the pass a third more: fewer groups an SM holds at once)
constexpr uint32_t kCellCache = 1024;
// (the blocks a round of the cell's CDF is scanned in - one a subgroup: a subgroup 8 wide or more)
constexpr uint32_t kCellBlocks = 32;
static_assert(kCellGroup / 8 <= kCellBlocks);
static_assert(kGridSlots % kCellGroup == 0 && kGridSlots % kLightGridPicks == 0);
// (round BUDGET) vec4s an entry in the bounds buffer: its bounds (2), its prepared record (3 - light_point_at) and its
// presampled points (RTXDI's presampled light samples: each drawn uniformly on the triangle and its emission found there
// once a frame - a candidate takes one, no texel read per pixel). (Round NOISE) 64, stratified 8 x 8 (PASS_BOUNDS): the
// pass costs the entries x the points (BA: 890 x 64 emission reads a frame), the candidates' cost is unchanged.
constexpr VkDeviceSize kPresampledSide = 8;
constexpr VkDeviceSize kPresampled = kPresampledSide * kPresampledSide;
constexpr VkDeviceSize kRecordStride = 5 + kPresampled;
// (round PERF3) The grid makes only the cells read lately (kGridBody PASS_CELLS): every reader marks the cell it reads
// (its state word - vulkan_light_list.cpp light_grid_slot), and a cell is made while it was marked within kGridKeepFrames
// grid frames. All 16384 cells' 512 slots were made every frame (BO at night: 2.5 ms) for the few hundred to few thousand
// any pixel, secondary vertex or froxel reads. The picks' buffer: two header picks, the cells' picks, then a state word
// (uvec4) a cell.
constexpr uint32_t kGridCells = kGridAcross * kGridUp * kGridAcross;
constexpr uint32_t kGridHeaderPicks = 2;
constexpr uint32_t kGridStateWord = 2 * (kGridHeaderPicks + (kGridCells * kLightGridPicks));
constexpr uint32_t kGridKeepFrames = 16;
// Flags (the block's counts.w)
constexpr uint32_t kFlagTemporal = 1U;
constexpr uint32_t kFlagSpatial = 2U;
constexpr uint32_t kFlagAccumulate = 4U;
constexpr uint32_t kFlagPermute = 8U;  // (round PT4d) restir_denoiser_glsl
constexpr uint32_t kFlagBoiling = 16U;
// (round FA - vulkan_debug_view.h) a debug view without the lamps' diffuse / specular: none written into the inputs
constexpr uint32_t kFlagNoDiffuse = 32U;
constexpr uint32_t kFlagNoSpecular = 64U;
// (round PERF6) With frame_profile on: what the passes did each frame (the shaders' di_counts, after the not-finite count
// and its spare words), read back with the pass's time - a burst of slow passes is logged with them (collect_timing).
constexpr uint32_t kFlagCount = 128U;
struct CountName {
    const char* define; // DI_COUNT_<define> in the shaders
    const char* text;   // in the log
};
constexpr std::array<CountName, 14> kCountNames{{{"PIXELS", "pixels"},
                                                 {"OUTSIDE", "outside the grid"},
                                                 {"UNMADE", "in unmade cells"},
                                                 {"CANDIDATES", "candidates"},
                                                 {"SPATIAL_RAYS", "spatial rays"},
                                                 {"LONG_RAYS", "long rays"},
                                                 {"HISTORY", "with history"},
                                                 {"NEIGHBOURS", "neighbours taken"},
                                                 {"CELLS", "cells made"},
                                                 {"CAMERA_CANDIDATES", "camera-ray candidates"},
                                                 {"THROUGH_CANDIDATES", "seen-through candidates"},
                                                 {"BLOCKED_CANDIDATES", "blocking candidates"},
                                                 {"SPATIAL_THROUGH", "spatial seen-through candidates"},
                                                 {"SPATIAL_BLOCKED", "spatial blocking candidates"}}};
constexpr uint32_t kDiCounts = 16; // (kCountNames, then a spare word)
static_assert(kCountNames.size() <= kDiCounts);
constexpr VkDeviceSize kCountersBytes = (4 + kDiCounts) * sizeof(uint32_t);


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
    std::array<float, 4> grid{};           // (round PT4c) the light grid: xyz its corner, w a cell's side
    std::array<uint32_t, 4> grid_cells{};  // its cells across x, y, z; w its slots a cell (0: no grid - the list alone)
    std::array<uint32_t, 4> shared{};      // (round OM) xy the shared primary surface's address, z 1: written here
    std::array<uint32_t, 4> grid_shift{};  // (round PERF3) xyz the light grid's cells' shift on each axis (light_grid_slot)
};
static_assert(sizeof(RestirBlock) == (2 * 64) + (13 * 16));

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
    vec4 grid;
    uvec4 grid_cells;
    uvec4 surfaces;
    uvec4 grid_shift;
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
// (round PT4c) the light grid: each entry's bounds (light_bounds below), each cell's slots (the entry + 1 - 0: none -,
// the slot's unbiased contribution weight)
layout(std430, set = 0, binding = 19) buffer Bounds { vec4 bounds[]; };
// (round BUDGET) an entry's vec4s: its centre and radius, its kind's numbers (PASS_CELLS) - (round NOISE2) at 2e, 2e + 1 -,
// and its record (record_at): (a triangle) its world corners (light_point_at) and PRESAMPLED points on it drawn this frame,
// each its kept uv (x's bits) and its emission there (yzw) - RTXDI's presampled light samples
const uint PRESAMPLED = PRESAMPLED_POINTS; // (kPresampled)
const uint PRESAMPLED_SIDE = PRESAMPLED_POINTS_SIDE; // (round NOISE: the strata on each side)
const uint RECORD_STRIDE = 3u + PRESAMPLED;
// (round NOISE2) the entries' centres and kinds' numbers first, two vec4s an entry, packed (every cell of the light grid
// reads them all: each a cache line of its own with the records between them cost the grid's pass most of its time);
// the records (corners, presampled points) after them
uint record_at(uint e) { return 2u * pass.counts.x + RECORD_STRIDE * e; }
layout(std430, set = 0, binding = 20) buffer Cells { uvec2 cells[]; };
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
// (round BR) the lights' specular over the surface's specular albedo E - the view's specular input (vulkan_surface_light.h
// SurfaceInputs::specular, 0 where nothing wrote; ReSTIR GI adds its environment's to it after)
layout(set = 0, binding = 21, rgba16f) uniform writeonly image2D o_specular;
#endif
layout(std430, set = 0, binding = 18) buffer Counters { uint bad_samples; uint counter_spare[3]; uint di_counts[DI_COUNTS]; };

const uint FLAG_TEMPORAL = 1u;
const uint FLAG_SPATIAL = 2u;
const uint FLAG_ACCUMULATE = 4u;
const uint FLAG_PERMUTE = 8u;
const uint FLAG_BOILING = 16u;
const uint FLAG_NO_DIFFUSE = 32u;
const uint FLAG_NO_SPECULAR = 64u;
const uint FLAG_COUNT = 128u; // (round PERF6) the frame's counts kept (frame_profile - di_counts below)
const uint MOST_SPATIAL = 4u;
// (round PERF6) What the passes did this frame, with frame_profile on (read with the pass's time - collect_timing): each
// invocation's own counts summed over its subgroup, one atomic a subgroup. The counts take no random numbers and change
// no sample.
uint g_miss_outside = 0u; // (the pixel's candidates from the whole list: its point outside the grid ...)
uint g_miss_unmade = 0u;  // (... or its cell not made this frame)
uint g_candidates = 0u;
uint g_rays = 0u;
uint g_long_rays = 0u;    // (rays longer than the grid's half width - far lights)
uint g_history = 0u;
uint g_pixel = 0u; // (round BUDGET: a pixel whose candidates were drawn)
void count_one(uint which, uint n)
{
    uint sum = subgroupAdd(n);
    if (subgroupElect() && sum != 0u) {
        atomicAdd(di_counts[which], sum);
    }
}
void count_ray(float reach)
{
    ++g_rays;
    if (reach > 0.5 * float(pass.grid_cells.x) * pass.grid.w) {
        ++g_long_rays;
    }
}
const uint LIGHT_RAYS = 15u;     // world, objects, alpha-tested, drawn apart (round PG) - the reference's light rays
const uint LIGHT_RAYS_ON_OBJECT = 13u; // (an object's pixel: the world's and what is drawn apart - its low-poly body
                                       // doesn't shadow itself)

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
// its albedo (linear) - (round BR) and its material's F0 and roughness (its lights' specular is this pass's too).
struct Surface {
    vec3 at;
    vec3 n;
    vec3 ng;
    vec3 albedo;
    vec3 f0;
    float rough;
    bool valid;
    bool object;
};
Surface no_surface()
{
    return Surface(vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0), vec3(0.0), 1.0, false, false);
}
void put_surface(uint i, Surface s)
{
    now_at[i] = vec4(s.at, s.valid ? 1.0 : 0.0);
    now_more[i] = uvec4(packSnorm2x16(oct_encode(s.n)), packSnorm2x16(oct_encode(s.ng)),
                        packUnorm4x8(vec4(sqrt(clamp(s.albedo, 0.0, 1.0)), clamp(s.rough, 0.0, 1.0))),
                        (s.object ? 1u : 0u) | (packUnorm4x8(vec4(sqrt(clamp(s.f0, 0.0, 1.0)), 0.0)) << 8));
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
    vec3 f = unpackUnorm4x8(more.w >> 8).rgb;
    s.f0 = f * f;
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

#ifndef CHECK
// (round PT4d) The depth's own surface at pixel `p` (its point `here`, seen along `dir`): where the drawn surface isn't the
// one the rays find - its outline drawn smoothed by the tessellation, a skinned body's, grass and litter drawn without a
// ray-traced copy - the point from the depth, its normal from the nearer neighbours' depths (facing the eye), a diffuse
// material (the inputs carry the light a white diffuser gets: the shading puts it on the surface's own material; no
// albedo for the lit-already colours' input). Before, such pixels got no lamps here - and none from the lamps pass (one
// owner) - and ReSTIR GI left the light store's environment there: the outline of every such surface flickered with the
// jitter between lit and not (Ray Reconstruction's halos round the skater, the archways, the grass).
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
        vec3 v = (q - here) * float(o.x + o.y); // (toward +x / +y)
        float apart = abs(dot(q - here, dir));
        int axis = k < 2 ? 0 : 1;
        if (drawn && apart < best[axis]) {
            best[axis] = apart;
            step[axis] = v;
        }
    }
    vec3 n = cross(step[0], step[1]);
    n = dot(n, n) > 0.0 ? normalize(n) : -dir;
    if (dot(n, dir) > 0.0) {
        n = -n;
    }
    // (round BR) a diffuser alone - F0 0: no specular. Its material isn't known (the shading's own would be put back on the
    // specular over a guessed one's E: the skater pale, his outline white).
    return Surface(here, n, n, vec3(0.0), vec3(0.0), 1.0, true, false);
}
#endif

// (round OM - vulkan_restir_di.h SharedPrimary) The view's primary surface for ReSTIR GI: 5 x vec4 a pixel - at + valid,
// shading normal + roughness, triangle normal + flags (1 object, 2 metal), albedo, F0 - the material as the hit surface
// has it (not the check's). (Round NOBIAS3) The albedo row's w: which surface it is (kSharedPrimary*).
const float PRIMARY_NONE = 0.0;
const float PRIMARY_TRACED = 1.0;
const float PRIMARY_DEPTH = 2.0;
layout(buffer_reference, buffer_reference_align = 16, scalar) writeonly buffer SharedSurfaces { vec4 rows[]; };
// (round ALBEDO - `pt frames ... inputs`' di_material) the traced primary's material id + 1 (0: none) in the F0 row's w
uint g_primary_material = 0u;
Surface shared_primary(ivec2 p, Surface s, bool metal, vec3 f0, float rough, float kind)
{
    if (pass.surfaces.z != 0u) {
        ivec2 local = p - pass.rect.xy;
        uint at = (uint(local.y) * uint(pass.rect.z) + uint(local.x)) * 5u;
        SharedSurfaces rows = SharedSurfaces(pass.surfaces.xy);
        uint flags = (s.object ? 1u : 0u) | (metal ? 2u : 0u);
        rows.rows[at] = vec4(s.at, s.valid ? 1.0 : 0.0);
        rows.rows[at + 1u] = vec4(s.n, rough);
        rows.rows[at + 2u] = vec4(s.ng, uintBitsToFloat(flags));
        rows.rows[at + 3u] = vec4(s.albedo, kind);
        rows.rows[at + 4u] = vec4(f0, float(g_primary_material));
    }
    return s;
}

// The pixel's surface: the check's camera ray through its centre; the game's the prepass's depth there, its triangle the
// one a camera ray meets at that point (round CW: the one the prepass kept - trace_drawn, a blended layer too) -
// (round PT4d) where none does, the depth's own surface (depth_surface). (Round OM: written for ReSTIR GI as it is found.)
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
        return shared_primary(p, no_surface(), false, vec3(0.0), 1.0, PRIMARY_NONE);
    }
    vec3 here = world_at(centre, d);
    expected = length(here - eye);
    if (!(expected > 0.0)) {
        return shared_primary(p, no_surface(), false, vec3(0.0), 1.0, PRIMARY_NONE);
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
        return shared_primary(p, no_surface(), false, vec3(0.0), 1.0, PRIMARY_NONE);
#else
        return shared_primary(p, depth_surface(p, here, dir), false, vec3(0.0), 1.0, PRIMARY_DEPTH);
#endif
    }
    Surface s;
    s.at = h.position;
    s.ng = h.normal;
    s.n = dot(h.shading_normal, -dir) > 0.0 ? h.shading_normal : h.normal;
    s.albedo = h.base_colour;
#ifdef CHECK
    s.f0 = vec3(pass.force.y); // (the check's material)
    s.rough = pass.force.x;
#else
    s.f0 = h.material.f0;
    s.rough = h.material.roughness;
#endif
    s.object = (geometry_record(instance).instance_flags & HIT_INSTANCE_OBJECT) != 0u;
    s.valid = true;
    // (GI's: the albedo, the material's F0 and roughness as the surface has them - it puts its check's on them itself)
    Surface written = s;
    written.albedo = h.base_colour;
    g_primary_material = id + 1u;
    shared_primary(p, written, h.kind == HIT_KIND_METAL, h.material.f0, h.material.roughness, PRIMARY_TRACED);
    return s;
}

// (round PERF6) The pixel's surface as PASS_PRIMARY found it, from the shared rows it wrote (full precision: the same
// numbers primary() returned - the check's material put back on it as primary() does).
layout(buffer_reference, buffer_reference_align = 16, scalar) readonly buffer SharedRows { vec4 rows[]; };
Surface primary_found(ivec2 local)
{
    uint at = (uint(local.y) * uint(pass.rect.z) + uint(local.x)) * 5u;
    SharedRows rows = SharedRows(pass.surfaces.xy);
    vec4 a = rows.rows[at];
    vec4 n = rows.rows[at + 1u];
    vec4 ng = rows.rows[at + 2u];
    Surface s;
    s.at = a.xyz;
    s.valid = a.w > 0.5;
    s.n = n.xyz;
    s.rough = n.w;
    s.ng = ng.xyz;
    s.object = (floatBitsToUint(ng.w) & 1u) != 0u;
    s.albedo = rows.rows[at + 3u].rgb;
    s.f0 = rows.rows[at + 4u].rgb;
#ifdef CHECK
    if (s.valid) {
        s.f0 = vec3(pass.force.y);
        s.rough = pass.force.x;
    }
#endif
    return s;
}

#ifdef PASS_PRIMARY
// (round PERF6) The pixels' primary surfaces - the camera rays and their hit surfaces - in a pass of their own (wavefront
// style, as ReSTIR GI's samples, round PERF5): the temporal pass then holds the light points and light rays alone.
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pass.rect.zw))) {
        return;
    }
    put_surface(uint(local.y * pass.rect.z + local.x), primary(pass.rect.xy + local));
    if ((pass.counts.w & FLAG_COUNT) != 0u) {
        count_one(DI_COUNT_CAMERA_CANDIDATES, g_ray_candidates[0]);
    }
}
#endif

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
    vec3 emitted; // (round BUDGET) a triangle's emission at the point (what a reservoir keeps of it)
};
// (round WS) The target: the luminance of what the sample brings through the surface's whole BRDF - a white diffuser's
// lobe (`light` itself) and the material's specular lobe toward the eye (RTXDI's target for a light sample: the
// surface's BRDF x the light, unshadowed). Before, the diffuse lobe's alone: the lights' specular (the same sample
// through the specular lobe - put_result) on a smooth surface came from samples drawn for the diffuse, a lamp landing in
// the narrow lobe by chance then worth many times its share (round BR's finding for GI). Diffuse and specular stay
// unbiased (any target that is > 0 wherever the light is); the specular lobe is now sampled where it is bright.
float light_target(Surface s, vec3 dir, vec3 light)
{
    float diffuse = path_luma(light);
    if (!(max(s.f0.r, max(s.f0.g, s.f0.b)) > 0.0)) {
        return diffuse;
    }
    vec3 v = normalize(pass.eye.xyz - s.at);
    float facing = max(dot(s.n, dir), 1.0e-4);
    vec3 specular = brdf_direct(brdf_material(vec3(0.0), s.f0, s.rough), s.n, v, dir, light / facing);
    float both = diffuse + path_luma(max(specular, vec3(0.0)));
    return isnan(both) || isinf(both) ? diffuse : both;
}
bool light_point(uint e, vec2 uv, Surface s, out LightPoint lp)
{
    lp = LightPoint(vec3(0.0), 0.0, vec3(0.0, 1.0, 0.0), 0.0, 0.0, vec3(0.0));
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
        if (!hit_emission_of(listed.x, listed.y, instance_material(listed.x), bc.yz, tri, emitted)) { // (round PERF2: its corners read once)
            return false;
        }
        lp.emitted = max(emitted, vec3(0.0));
        lp.light = lp.emitted * g;
        lp.target = light_target(s, dir, lp.light);
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
        lp.target = light_target(s, dir, lp.light);
        lp.dir = dir;
        lp.reach = max(d - light.at.w, 0.0);
        lp.pdf = entry.pdf;
        return lp.target > 0.0;
    }
    return false;
}
// (round BUDGET) The same light point from the entry's prepared record (PASS_BOUNDS, once a frame - RTXDI's "prepare
// lights": its corners in the world) and the emission at the point as it was found (`emitted`: a presampled point's, or
// what a reservoir kept of its sample): no corners, transform, store or texel read. The point, direction, reach,
// geometry term and pdf are light_point's exactly. Every resampling target (the candidates, the temporal and spatial
// reuse) is evaluated with it; the chosen sample's light is light_point's own where it is shaded (PASS_SPATIAL), once a
// pixel. A lamp's is light_point's (no texel).
bool light_point_at(uint e, vec2 uv, vec3 emitted, Surface s, out LightPoint lp)
{
    lp = LightPoint(vec3(0.0), 0.0, vec3(0.0, 1.0, 0.0), 0.0, 0.0, vec3(0.0));
    if (e >= pass.counts.x) {
        return false;
    }
    LightEntry entry = light_entries[e];
    if (light_kind(entry.payload) != LIGHT_TRIANGLE) {
        return light_point(e, uv, s, lp);
    }
    vec3 a = bounds[record_at(e)].xyz;
    vec3 b = bounds[record_at(e) + 1u].xyz;
    vec3 c = bounds[record_at(e) + 2u].xyz;
    vec3 across = cross(b - a, c - a);
    float twice = length(across);
    if (!(twice > 0.0)) {
        return false;
    }
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
    lp.emitted = max(emitted, vec3(0.0));
    lp.light = lp.emitted * (cos_here * cos_there / (PATH_PI * d2));
    lp.target = light_target(s, dir, lp.light);
    lp.dir = dir;
    lp.reach = max(d * 0.999 - ray_offset(y), 0.0);
    lp.pdf = entry.pdf / (0.5 * twice);
    return lp.target > 0.0;
}

// What of the light gets from it to the surface (the one light ray - vulkan_rt_scene.h ray_light).
vec3 seen(Surface s, LightPoint lp)
{
    return ray_light_leaving(s.object ? LIGHT_RAYS_ON_OBJECT : LIGHT_RAYS, s.at + s.ng * ray_offset(s.at), s.ng, lp.dir, lp.reach);
}
bool seen_at_all(vec3 through) { return max(through.r, max(through.g, through.b)) > 0.0; }

// (round PT4c) The light grid's cell holding a point (NO_CELL outside it or with no grid).
const uint NO_CELL = 0xFFFFFFFFu;
uint grid_cell_of(vec3 p)
{
    if (pass.grid_cells.w == 0u) {
        return NO_CELL;
    }
    vec3 at = floor((p - pass.grid.xyz) / pass.grid.w);
    if (any(lessThan(at, vec3(0.0))) || any(greaterThanEqual(at, vec3(pass.grid_cells.xyz)))) {
        g_miss_outside = 1u;
        return NO_CELL;
    }
    // (round PERF3 - vulkan_light_list.cpp light_grid_slot) the cell by its place in the world; one made this frame (else
    // the list by power, as outside the grid), marked as wanted
    uvec3 c = (uvec3(at) + pass.grid_shift.xyz) % pass.grid_cells.xyz;
    uint cell = (c.y * pass.grid_cells.z + c.z) * pass.grid_cells.x + c.x;
    uvec4 state = grid_words[GRID_STATE_WORD + cell];
    if (state.y != pass.counts.z) {
        grid_words[GRID_STATE_WORD + cell].y = pass.counts.z;
    }
    g_miss_unmade = state.x == pass.counts.z ? 0u : 1u;
    return state.x == pass.counts.z ? cell : NO_CELL;
}

// A reservoir: its sample (the list's entry + 1 - 0: none -, the point on it), its weight W (the unbiased contribution
// weight: 1 / the sample's effective pdf) and its sample count M.
struct Reservoir {
    uint entry;
    vec2 uv;
    float W;
    float M;
    vec3 light; // (round BUDGET) the emission at its point as it was found (light_point_at)
};
// (round BUDGET: two words a pixel - the second the emission)
Reservoir reservoir_of(uvec4 v, uvec4 more)
{
    return Reservoir(v.x, unpackUnorm2x16(v.y), uintBitsToFloat(v.z), uintBitsToFloat(v.w), uintBitsToFloat(more.xyz));
}
uvec4 words_of(Reservoir r) { return uvec4(r.entry, packUnorm2x16(r.uv), floatBitsToUint(r.W), floatBitsToUint(r.M)); }
uvec4 more_words_of(Reservoir r) { return uvec4(floatBitsToUint(r.light), 0u); }
Reservoir no_reservoir(float m) { return Reservoir(0u, vec2(0.0), 0.0, m, vec3(0.0)); }
// (a point's numbers as the reservoir keeps them: what it is drawn with is what it is found again with)
vec2 kept_uv(vec2 uv) { return round(clamp(uv, 0.0, 1.0) * 65535.0) / 65535.0; }
float good(float x) { return isnan(x) || isinf(x) ? 0.0 : x; }

#ifdef PASS_TEMPORAL
// (round BUDGET) The pixel's initial candidates (round PERF6's PASS_CANDIDATES, now in the temporal pass again - neither
// traces a ray: the picks no longer go through memory): `kept` the pick's entry + 1, point, the weights' sum and its
// target, `rng` the pixel's random state after them, `kept_light` the pick's emission.
void initial_candidates(ivec2 local, Surface s, out uvec4 kept, out uint rng_after, out vec3 kept_light)
{
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z, pass.params.w);
    // 1. the initial candidates, resampled toward the target - (round PT4c) from the pixel's cell of the light grid (its
    // point moved by up to half a cell first: no cell edges in the picture), each slot a light with its unbiased
    // contribution weight; outside the grid from the whole list by power
    uint count = pass.counts.x;
    uint cell = grid_cell_of(s.at + (vec3(path_next(rng), path_next(rng), path_next(rng)) - 0.5) * pass.grid.w);
    uint candidates = pass.params.x;
    uint picked = 0u;
    vec2 picked_uv = vec2(0.0);
    vec3 picked_light = vec3(0.0);
    float wsum = 0.0;
    float chosen = 0.0;
    for (uint k = 0u; k < candidates; ++k) {
        float u = path_next(rng);
        vec2 uv = kept_uv(vec2(path_next(rng), path_next(rng)));
        float pick = path_next(rng);
        bool evaluate = false;
        uint e = 0u;
        float slot_weight = 1.0; // (x the list's pdf of the entry: the slot's weight over the list's)
        if (count != 0u) {
            if (cell != NO_CELL) {
                uvec2 slot = cells[cell * pass.grid_cells.w + min(uint(u * float(pass.grid_cells.w)), pass.grid_cells.w - 1u)];
                if (slot.x != 0u) {
                    e = slot.x - 1u;
                    slot_weight = uintBitsToFloat(slot.y) * light_entries[e].pdf;
                    evaluate = true;
                }
            } else {
                e = light_pick(u, count);
                evaluate = true;
            }
        }
        // (round BUDGET) a triangle's point one of its presampled ones (PASS_BOUNDS: each drawn uniformly on it, as the
        // point here was - the same pdf), its emission found there once for every pixel
        vec3 emitted = vec3(0.0);
        if (evaluate && e < count && light_kind(light_entries[e].payload) == LIGHT_TRIANGLE) {
            vec4 point = bounds[record_at(e) + 3u + min(uint(uv.x * float(PRESAMPLED)), PRESAMPLED - 1u)];
            uv = unpackUnorm2x16(floatBitsToUint(point.x));
            emitted = point.yzw;
        }
        LightPoint lp;
        bool got = evaluate && light_point_at(e, uv, emitted, s, lp);
        g_candidates += got ? 1u : 0u;
        if (got && lp.pdf > 0.0) {
            float w = good(lp.target / lp.pdf * slot_weight);
            wsum += w;
            if (w > 0.0 && pick * wsum < w) {
                picked = e + 1u;
                picked_uv = uv;
                picked_light = lp.emitted;
                chosen = lp.target;
            }
        }
    }
    kept = uvec4(picked, packUnorm2x16(picked_uv), floatBitsToUint(wsum), floatBitsToUint(chosen));
    rng_after = rng;
    kept_light = picked_light;
    g_pixel = 1u;
}

// The pixel's reservoir after the initial candidates (initial_candidates' pick) and the temporal reuse; `weight` its sample's
// contribution (target x W - what the boiling filter compares).
Reservoir temporal_reservoir(ivec2 local, uint i, out float weight)
{
    weight = 0.0;
    Surface s = primary_found(local); // (round PERF6: PASS_PRIMARY's)
    float cap = float(pass.params.z);
    if (!s.valid) {
        return no_reservoir(0.0);
    }
    uvec4 kept;
    uint rng;
    vec3 kept_light;
    initial_candidates(local, s, kept, rng, kept_light);
    uint candidates = pass.params.x;
    Reservoir r = Reservoir(kept.x, unpackUnorm2x16(kept.y), 0.0, float(candidates), kept_light);
    float wsum = uintBitsToFloat(kept.z);
    float chosen = uintBitsToFloat(kept.w);
    r.W = r.entry > 0u && chosen > 0.0 ? good(wsum / (r.M * chosen)) : 0.0;
    float kept_target = chosen;
    // (round BUDGET) No light ray in this pass: RTXDI's temporal resampling with its basic bias correction - the
    // initial candidates unshadowed (their pick's visibility is the shading's one ray, PASS_SPATIAL, which also drops a
    // sample its surface doesn't see from the reservoir it keeps - the history read here), the history's sample weighed
    // by its target here, Z by the targets alone. Every target from the prepared records (light_point_at).
    // 2. the reservoir this point had last frame - (round PT4d) a neighbour's in its 4 x 4 block by the frame's permutation
    bool has_then = false;
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
                has_then = alike(s, then);
                if (has_then) {
                    Reservoir h = reservoir_of(res_b[2u * j], res_b[2u * j + 1u]);
                    h.M = min(h.M, cap);
                    LightPoint lp;
                    float t_h = h.entry > 0u && h.W > 0.0 && light_point_at(h.entry - 1u, h.uv, h.light, s, lp) ? lp.target : 0.0;
                    float w_r = chosen * r.W * r.M;
                    float w_h = good(t_h * h.W * h.M);
                    float total = w_r + w_h;
                    Reservoir out_r = no_reservoir(r.M + h.M);
                    if (total > 0.0) {
                        float z = 0.0;
                        float target = 0.0;
                        if (path_next(rng) * total < w_h) {
                            // last frame's sample: this pixel's candidates could have made it (its target here is over 0)
                            out_r.entry = h.entry;
                            out_r.uv = h.uv;
                            out_r.light = h.light;
                            target = t_h;
                            z = h.M + r.M;
                        } else {
                            // this frame's: last frame's surface could have made it where its target there is over 0
                            out_r.entry = r.entry;
                            out_r.uv = r.uv;
                            out_r.light = r.light;
                            target = chosen;
                            z = r.M;
                            if (light_point_at(r.entry - 1u, r.uv, r.light, then, lp)) {
                                z += h.M;
                            }
                        }
                        out_r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
                        kept_target = target;
                    }
                    r = out_r;
                }
            }
        }
    }
    g_history = has_then ? 1u : 0u;
    weight = r.entry > 0u && r.W > 0.0 ? good(kept_target * r.W) : 0.0;
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
    if ((pass.counts.w & FLAG_COUNT) != 0u) { // (round PERF6)
        count_one(DI_COUNT_PIXELS, g_pixel); // (round BUDGET: the candidates' counts - their pass is this one)
        count_one(DI_COUNT_OUTSIDE, g_miss_outside);
        count_one(DI_COUNT_UNMADE, g_miss_unmade);
        count_one(DI_COUNT_CANDIDATES, g_candidates);
        count_one(DI_COUNT_HISTORY, g_history);
        count_one(DI_COUNT_THROUGH_CANDIDATES, g_ray_candidates[1]);
        count_one(DI_COUNT_BLOCKED_CANDIDATES, g_ray_candidates[2]);
    }
    // (round PT4d) the boiling filter - every invocation of the group takes part (no early return above)
    if ((pass.counts.w & FLAG_BOILING) != 0u && restir_boiling(weight)) {
        r = no_reservoir(0.0);
    }
    if (inside) {
        res_a[2u * i] = words_of(r);
        res_a[2u * i + 1u] = more_words_of(r);
    }
}
#endif

#ifdef PASS_SPATIAL
void put_result(ivec2 p, uint i, vec3 lamps, vec3 on_albedo, vec3 specular, vec3 full)
{
#ifdef CHECK
    if ((pass.counts.w & FLAG_ACCUMULATE) != 0u) {
        sums[i] += vec4(full, 1.0);
        sums[uint(pass.rect.z * pass.rect.w) + i] = vec4(full, path_luma(lamps)); // (w: the lamps' light alone)
    }
#else
    // (round FA - the debug views: a term left out writes nothing into the frame)
    bool diffuse_on = (pass.counts.w & FLAG_NO_DIFFUSE) == 0u;
    imageStore(o_lamps, p, vec4(diffuse_on ? lamps : vec3(0.0), 0.0));
    imageStore(o_lamps_on, p, vec4(diffuse_on ? on_albedo : vec3(0.0), 0.0));
    imageStore(o_specular, p, vec4((pass.counts.w & FLAG_NO_SPECULAR) == 0u ? specular : vec3(0.0), 1.0));
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
        res_b[2u * i] = uvec4(0u, 0u, 0u, floatBitsToUint(0.0));
        res_b[2u * i + 1u] = uvec4(0u);
        put_result(p, i, vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
        return;
    }
    uint rng = path_sequence_start(uint(local.x), uint(local.y), pass.counts.z ^ 0x9E3779B9u, pass.params.w + 1u);
    // the pixel's reservoir and its neighbours' that are the same surface
    Reservoir taken[MOST_SPATIAL + 1u];
    Surface where[MOST_SPATIAL + 1u];
    uint n = 1u;
    taken[0] = reservoir_of(res_a[2u * i], res_a[2u * i + 1u]);
    where[0] = s;
    if ((pass.counts.w & FLAG_SPATIAL) != 0u) {
        // (round NOISE) RTXDI's disocclusion boost: a pixel without history (its reservoir no more than this frame's
        // candidates - newly revealed, beside a moving skater) takes MOST_SPATIAL neighbours, not one: its first frames
        // otherwise show one pixel's 16 candidates, the bright specks Ray Reconstruction keeps as stars for a moment
        uint neighbours = taken[0].M <= float(pass.params.x) ? MOST_SPATIAL : min(pass.params.y, MOST_SPATIAL);
        for (uint k = 0u; k < neighbours; ++k) {
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
            taken[n] = reservoir_of(res_a[2u * j], res_a[2u * j + 1u]);
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
        if (taken[k].entry == 0u || !(taken[k].W > 0.0) || !light_point_at(taken[k].entry - 1u, taken[k].uv, taken[k].light, s, lp)) {
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
    Reservoir r = no_reservoir(min(m_sum, cap));
    vec3 lamps = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec3 full = vec3(0.0);
    if (chosen != 0xFFFFFFFFu) {
        // (round BUDGET) Z by the targets alone (RTXDI's basic bias correction): the samples of every reservoir whose
        // surface has the chosen light's target over 0 there (its prepared record) - no ray a neighbour. Then the one
        // light ray of the pixel: the chosen sample seen from here, shaded with what gets through.
        // (round NOISE) The reservoir keeps its sample whether it is seen or not (RTXDI's discardInvisibleSamples off -
        // its default): its weight is the unshadowed target's, the one the reuse resamples by, and only so is the
        // estimate unbiased. Dropping a sample this surface doesn't see (its W 0) while keeping its M - round BUDGET's
        // visibility reuse - told the next frames that the reservoir's M candidates had brought nothing: the pixel's light
        // fell to this frame's candidates over the whole history (16 of up to 320) and climbed back over the history's
        // length. Wherever the candidates often meet an occluded light (emitters beyond a wall, lamps round a corner) the
        // light sat 25 - 40 % low and whole lights came and went over seconds (Barcelona's fountain: ReSTIR DI 0.59 of the
        // reference's local direct light, kept 0.77 - its opaque surfaces 0.66 - 0.86 -> 0.93 - 0.98). Reset to M 0
        // instead it was ~1.5 x too bright (the history then holds only seen samples).
        float z = 0.0;
        for (uint k = 0u; k < n; ++k) {
            LightPoint toward;
            if (k == chosen || k == 0u || light_point_at(taken[chosen].entry - 1u, taken[chosen].uv, taken[chosen].light, where[k], toward)) {
                z += taken[k].M;
            }
        }
        LightPoint exact;
        bool shown = light_point(taken[chosen].entry - 1u, taken[chosen].uv, s, exact); // (its own light: the texel's)
        vec3 through = shown ? seen(s, chosen_point) : vec3(0.0);
        count_ray(chosen_point.reach);
        if (shown) {
            r.entry = taken[chosen].entry;
            r.uv = taken[chosen].uv;
            r.light = exact.emitted; // (its emission now: a lamp switched off is dropped by its target)
            r.W = z > 0.0 && target > 0.0 ? good(total / (z * target)) : 0.0;
        }
        if (seen_at_all(through)) {
            if (r.W > 0.0) {
                lamps = exact.light * through * r.W;
                // (round BR) the light's specular reflection: the same sample through the material's specular lobe, over
                // its specular albedo E (the shading puts its own E back - ReSTIR GI's convention). The lights' specular
                // is this pass's as their diffuse light is: sampled on the light, not found by a reflected ray (an
                // emitter's small bright face met by chance - ReSTIR GI's sparkle under Ray Reconstruction).
                vec3 v = normalize(pass.eye.xyz - s.at);
                float facing = max(dot(s.n, exact.dir), 1.0e-4);
                vec3 incoming = exact.light / facing * through * r.W;
                vec3 e = brdf_specular_albedo(s.f0, max(dot(s.n, v), 1.0e-4), s.rough);
                specular = max(s.f0.r, max(s.f0.g, s.f0.b)) <= 0.0 ? vec3(0.0) : brdf_direct(brdf_material(vec3(0.0), s.f0, s.rough), s.n, v, exact.dir, incoming) /
                           max(e, vec3(1.0e-4));
#ifdef CHECK
                // (the check: the diffuse light on the material plus the specular as the shading takes it - E x it)
                BrdfMaterial m = brdf_material(s.albedo * pass.force.z, vec3(pass.force.y), pass.force.x);
                full = brdf_direct(m, s.n, v, exact.dir, incoming) -
                       brdf_direct(brdf_material(vec3(0.0), s.f0, s.rough), s.n, v, exact.dir, incoming) + e * specular;
#endif
            }
        }
    }
    if (any(isnan(lamps)) || any(isinf(lamps)) || any(isnan(specular)) || any(isinf(specular)) || any(isnan(full)) ||
        any(isinf(full))) {
        atomicAdd(bad_samples, 1u);
        lamps = vec3(0.0);
        specular = vec3(0.0);
        full = vec3(0.0);
        r.W = 0.0;
    }
    res_b[2u * i] = words_of(r);
    res_b[2u * i + 1u] = more_words_of(r);
    if ((pass.counts.w & FLAG_COUNT) != 0u) { // (round PERF6)
        count_one(DI_COUNT_SPATIAL_RAYS, g_rays);
        count_one(DI_COUNT_LONG_RAYS, g_long_rays);
        count_one(DI_COUNT_NEIGHBOURS, n - 1u);
        count_one(DI_COUNT_SPATIAL_THROUGH, g_ray_candidates[1]);
        count_one(DI_COUNT_SPATIAL_BLOCKED, g_ray_candidates[2]);
    }
    vec3 exposed = min(lamps * pass.eye.w, vec3(65504.0));
    put_result(p, i, exposed, min(exposed * s.albedo, vec3(65504.0)), min(specular * pass.eye.w, vec3(65504.0)), full);
}
#endif
)";

// (round PT4c) The light grid (ReGIR - Boksansky et al. 2021, "Rendering many lights with grid-based reservoirs", Ray
// Tracing Gems II ch. 23): the initial candidates of a pixel come from its cell's slots instead of the whole list by
// power. Without it a level's far lamps took nearly every candidate (Boston at night: 58 lamps and 242 emitting
// triangles over the whole level, a courtyard's few found by 1 candidate in tens - the reservoirs then held a nearby
// light for frames and lost it again: the slow boiling Ray Reconstruction keeps as picture).
//   PASS_BOUNDS: each entry's bounds (one invocation an entry): bounds[2e] = its centre and radius, bounds[2e + 1] its
//     kind's numbers - a triangle (1): (round NOISE2) its power / 2 pi^2 (the light at a distance d face-on: x / d^2, in
//     light_point's unit) and power / 2 pi A (its mean radiance); a lamp (2 a point or panel, 3 a line): its colour's
//     luminance, its core, its reach (the lamp model - lamp_geometry with every cosine 1).
//   PASS_CELLS: each cell's slots (one workgroup a cell): (round NOISE2) every entry's weight for the cell's box widened
//     by half a cell (a pixel's point is moved by up to half a cell before its cell is found - cell_weight) summed into
//     the cell's CDF, the slots drawn from it by systematic resampling, each kept with its unbiased contribution weight
//     W = the weights' sum / its weight (round PT4c to NOISE: RIS over 8 picks by power a slot toward the most the entry
//     could bring to the box, W = sum w / (candidates x bound) - each slot drawn on its own). The weight is above 0
//     wherever a pixel of the cell can get light from the entry (the lamp's reach, any triangle with power), so a pixel's
//     candidates from the slots (weight = its target x W) cover every light it can see: unbiased (RIS with candidates of
//     their own contribution weights - generalised RIS, Lin et al. 2022).
constexpr const char* kGridBody = R"(
#ifdef PASS_BOUNDS
layout(local_size_x = 64) in;
void main()
{
    uint e = gl_GlobalInvocationID.x;
    if (e >= pass.counts.x) {
        return;
    }
    LightEntry entry = light_entries[e];
    uint kind = light_kind(entry.payload);
    uint index = light_index(entry.payload);
    vec4 place = vec4(0.0);
    vec4 numbers = vec4(0.0);
    vec4 record[3] = vec4[3](vec4(0.0), vec4(0.0), vec4(0.0));
    if (kind == LIGHT_TRIANGLE) {
        uvec4 listed = emitter_entries[index];
        HitVertices tri;
        if (hit_vertices(listed.x, listed.y, tri)) {
            mat4x3 to_world = instance_transform(listed.x);
            vec3 a = to_world * vec4(tri.position[0], 1.0);
            vec3 b = to_world * vec4(tri.position[1], 1.0);
            vec3 c = to_world * vec4(tri.position[2], 1.0);
            vec3 centre = (a + b + c) / 3.0;
            float area = 0.5 * length(cross(b - a, c - a));
            float flux = uintBitsToFloat(listed.z);
            if (area > 0.0 && flux > 0.0) {
                place = vec4(centre, sqrt(max(dot(a - centre, a - centre), max(dot(b - centre, b - centre), dot(c - centre, c - centre)))));
                // (round NOISE2) in the light's own unit - a white diffuser's radiance, light_point's `light` (the
                // triangle's L A cos cos / pi d^2; a lamp's colour x lamp_geometry): the listed power is 2 pi A L, so at
                // most L A / (pi d^2) = power / (2 pi^2 d^2) face-on, and L (= power / (2 pi A)) next to it. Before, both
                // were pi x that while a lamp's numbers were in its own unit: the cells weighed the lamps at a third of
                // the triangles (cell_weight)
                numbers = vec4(flux / (2.0 * PATH_PI * PATH_PI), flux / (2.0 * PATH_PI * area), 0.0, 1.0);
                // (round BUDGET) the record light_point_at reads: the corners in the world - and the frame's presampled
                // points on it, each drawn uniformly (as a candidate's point was) and its emission found there (hit_surface's
                // `emitted`, light_point's): every pixel's candidates on the triangle take one of them
                record[0] = vec4(a, 0.0);
                record[1] = vec4(b, 0.0);
                record[2] = vec4(c, 0.0);
                // (seeded by the triangle itself, not its place in the list: the scan lists them in any order)
                uint rng = path_sequence_start(listed.x, listed.y, pass.counts.z, pass.params.w ^ 0x51ED270Bu);
                uint material_id = instance_material(listed.x);
                // (round NOISE) stratified: point k in its own cell of a PRESAMPLED_SIDE x PRESAMPLED_SIDE grid over the unit
                // square (path_triangle_point keeps areas: equal parts of the triangle), at random within it. A candidate
                // takes a point at random, so each is still uniform on the triangle, but the frame's set covers the
                // triangle evenly: its share of a lamp drawn in a few texels no longer swings with the draw (16 points
                // drawn independently caught 0 - 4 of a lamp covering a tenth of its triangle - every pixel's light from
                // it rose and fell together for as long as the reservoirs kept the samples)
                for (uint k = 0u; k < PRESAMPLED; ++k) {
                    vec2 cell = vec2(float(k % PRESAMPLED_SIDE), float(k / PRESAMPLED_SIDE));
                    vec2 uv = round(clamp((cell + vec2(path_next(rng), path_next(rng))) / float(PRESAMPLED_SIDE), 0.0, 1.0) *
                                    65535.0) / 65535.0;
                    vec3 bc = path_triangle_point(uv.x, uv.y);
                    vec3 emitted;
                    if (!hit_emission_of(listed.x, listed.y, material_id, bc.yz, tri, emitted)) {
                        emitted = vec3(0.0);
                    }
                    bounds[record_at(e) + 3u + k] = vec4(uintBitsToFloat(packUnorm2x16(uv)), max(emitted, vec3(0.0)));
                }
            }
        }
    } else if (kind == LIGHT_LAMP) {
        LightRecord light = lamps[index];
        uint shape = uint(light.more.z + 0.5);
        bool line = shape == 2u && light.shape.w > 0.0;
        place = vec4(light.at.xyz, line ? light.shape.w : 0.0);
        numbers = vec4(path_luma(max(light.colour.rgb, vec3(0.0))), light.more.y, light.colour.w, line ? 3.0 : 2.0);
    }
    bounds[2u * e] = place;
    bounds[2u * e + 1u] = numbers;
    bounds[record_at(e)] = record[0];
    bounds[record_at(e) + 1u] = record[1];
    bounds[record_at(e) + 2u] = record[2];
}
#endif

#ifdef PASS_CELLS
layout(local_size_x = CELL_GROUP) in;
layout(std430, set = 0, binding = 18) buffer Counters { uint bad_samples; uint counter_spare[3]; uint di_counts[DI_COUNTS]; };
const uint FLAG_COUNT = 128u;
// (round NOISE2) How much of the cell's light entry `e` is expected to bring - the cell's sampling weight (ReGIR's target
// for the cell; the slots' distribution is this over its sum). It needs to be above 0 wherever a point of the box can get
// light from the entry (the estimate stays unbiased: the slots' weight is 1 / their probability), and close to the
// light's share there for the estimate to be good. A light tree's cluster importance (Conty Estevez and Kulla 2018,
// "Importance sampling of many lights with adaptive tree splitting"): the light's power over the squared distance from
// the box's centre plus a cell's side squared (the box's points spread round it) - in the light's unit at the receiver
// (light_point's `light`: a triangle's L A / (pi d^2) face-on, a lamp's colour x its falloff). Before, the most the entry
// could bring to
// the box's nearest point: a tiny bright triangle inside or next to the box (a sign's letter, a lit window's pane) then
// weighed its full radiance, as much as a street lamp lighting the whole cell - with the cells drawn by that weight, the
// cell's slots went to the letters and the lamp lighting the street got a few.
float cell_weight(uint e, vec3 low, vec3 high)
{
    vec4 place = bounds[2u * e];
    vec4 numbers = bounds[2u * e + 1u];
    uint kind = uint(numbers.w + 0.5);
    if (kind == 0u) {
        return 0.0;
    }
    vec3 centre = 0.5 * (low + high);
    float side = 0.5 * (high.x - low.x); // (the box is two cells wide)
    float d = max(length(place.xyz - centre) - place.w, 0.0);
    float d2 = d * d + side * side;
    if (kind == 1u) {
        return numbers.x / d2;
    }
    // a lamp: none where its reach ends before the box's nearest point (the lamp model's window: it brings nothing there)
    float core = numbers.y;
    float reach = numbers.z;
    float nearest = max(length(place.xyz - clamp(place.xyz, low, high)) - place.w, 0.0);
    if (!(core > 0.0) || !(reach > 0.0) || !(nearest < reach)) {
        return 0.0;
    }
    float x = nearest / reach;
    float window = 1.0 - x * x * x * x;
    float falloff = kind == 3u ? core / sqrt(d2 + core * core) : core * core / (core * core + d2);
    return numbers.x * falloff * window * window;
}

// (round NOISE2) One workgroup a cell. The cell's slots are drawn from the cell's own distribution - every entry's weight
// over their sum (cell_weight; the cell's exact CDF, as a many-lights light grid samples each cell's lights - UE5's
// MegaLights) - by systematic resampling: one random offset for the cell, slot k the entry under (k + offset) / slots of
// the CDF. A pixel takes a slot at random, so each candidate is still a pick by weight / sum (unbiased), but a light owns
// floor or ceil of its share of the slots, not a random number of them. Before, every slot was RIS over 8 picks of the
// whole list by power, drawn anew each frame: a light's slots in a cell swung from frame to frame by their binomial
// spread (a lamp with 4 of 512: +-50 %), every pixel of the cell (128 units, and the neighbours its jitter reaches) took
// its candidates from the same slots, and the temporal reuse held each frame's swing for ~20 frames: the lamps' slow
// breathing in every level (Training's lamps view: cells wandering 7 - 8 % (median) / 13 - 15 % (most) over 2.5 s in Ray
// Reconstruction's input).
// The CDF is made CELL_CACHE entries at a time in shared memory: each subgroup the prefix sums of its own block of them
// (subgroup scans, no barrier between), the blocks' offsets after - three barriers a round, not one a chunk (the pass is
// latency-bound: thousands of short-lived groups). A list longer than CELL_CACHE takes its total first, then a round each
// CELL_CACHE entries.
shared float g_prefix[CELL_CACHE];
shared float g_block[CELL_BLOCKS + 1u];
shared uint g_wanted;
void main()
{
    uint slots = pass.grid_cells.w;
    uint cells_total = pass.grid_cells.x * pass.grid_cells.y * pass.grid_cells.z;
    uint cell = gl_WorkGroupID.x;
    uint t = gl_LocalInvocationID.x;
    if (cell >= cells_total || slots == 0u) {
        return;
    }
    // (round PERF3) the header; then only a cell read within GRID_KEEP_FRAMES (its state word's y - light_grid_slot) is
    // made (pass.list.z: every cell - the check's); x the frame it was made in, 0 for one not made
    if (cell == 0u && t == 0u) {
        light_pool[0] = uvec4(floatBitsToUint(pass.grid));
        light_pool[1] = uvec4(pass.grid_cells.xyz, LIGHT_GRID_PICKS);
        light_pool[2] = uvec4(pass.counts.z, pass.grid_shift.xyz);
        light_pool[3] = uvec4(0u);
    }
    if (t == 0u) {
        uint wanted = light_pool[GRID_STATE_WORD + cell].y;
        g_wanted = pass.list.z != 0u || (wanted != 0u && pass.counts.z - wanted <= GRID_KEEP_FRAMES) ? 1u : 0u;
        light_pool[GRID_STATE_WORD + cell].x = g_wanted != 0u ? pass.counts.z : 0u;
        if (g_wanted != 0u && (pass.counts.w & FLAG_COUNT) != 0u) {
            atomicAdd(di_counts[DI_COUNT_CELLS], 1u); // (round PERF6)
        }
    }
    barrier();
    if (g_wanted == 0u) {
        return;
    }
    // (the cell's place: its index on each axis less the shift, modulo the cells across)
    uvec3 c = uvec3(cell % pass.grid_cells.x, cell / (pass.grid_cells.x * pass.grid_cells.z),
                    (cell / pass.grid_cells.x) % pass.grid_cells.z);
    c = (c + pass.grid_cells.xyz - pass.grid_shift.xyz) % pass.grid_cells.xyz;
    float side = pass.grid.w;
    vec3 low = pass.grid.xyz + (vec3(c) - 0.5) * side;
    vec3 high = low + 2.0 * side;
    uint count = pass.counts.x;
    uint rounds = (count + CELL_CACHE - 1u) / CELL_CACHE;
    uint blocks = min(gl_NumSubgroups, CELL_BLOCKS);
    // (a long list: its total first - summed in another order than the rounds' prefix sums, so a point the rounding puts
    // past their end leaves its slot empty: a weight 0, a bias of the order of the float's precision)
    float total = 0.0;
    if (rounds > 1u) {
        float own = 0.0;
        for (uint e = t; e < count; e += CELL_GROUP) {
            own += cell_weight(e, low, high);
        }
        float part = subgroupAdd(own);
        if (subgroupElect() && gl_SubgroupID < CELL_BLOCKS) {
            g_block[gl_SubgroupID] = part;
        }
        barrier();
        for (uint b = 0u; b < blocks; ++b) {
            total += g_block[b];
        }
        barrier();
    }
    uint rng = path_sequence_start(cell % 4096u, cell / 4096u, pass.counts.z, pass.params.w + 7u);
    float offset = path_next(rng);
    // (the readers' picks: every stride-th slot from a random first one - pick j's point on the CDF then uniform over
    // [j, j + 1) / LIGHT_GRID_PICKS, the picks a systematic draw of the whole CDF; slots 0, stride, 2 stride... alone would
    // be the CDF's first 1 / slots of each of those parts)
    uint stride = slots / LIGHT_GRID_PICKS;
    uint first_pick = min(uint(path_next(rng) * float(stride)), stride - 1u);
    uvec2 found[SLOTS_EACH];
    for (uint s = 0u; s < SLOTS_EACH; ++s) {
        found[s] = uvec2(0u, floatBitsToUint(0.0));
    }
    float before = 0.0;
    for (uint r = 0u; r < rounds; ++r) {
        uint first = r * CELL_CACHE;
        uint here = min(count - first, CELL_CACHE);
        // the round's entries in `blocks` blocks of `size` (a multiple of the subgroup's width), each subgroup scanning its
        // own: g_prefix[i] the inclusive prefix sum within the block, g_block[b] the block's sum
        uint size = ((here + blocks - 1u) / blocks + gl_SubgroupSize - 1u) / gl_SubgroupSize * gl_SubgroupSize;
        if (gl_SubgroupID < blocks) {
            float run = 0.0;
            uint start = gl_SubgroupID * size;
            for (uint i = start + gl_SubgroupInvocationID; i < start + size; i += gl_SubgroupSize) {
                float w = i < here ? cell_weight(first + i, low, high) : 0.0;
                float inclusive = run + subgroupInclusiveAdd(w);
                if (i < CELL_CACHE) {
                    g_prefix[i] = inclusive;
                }
                run += subgroupAdd(w); // (the last lane's inclusive sum but for the rounding)
            }
            if (subgroupElect()) {
                g_block[gl_SubgroupID] = run;
            }
        }
        barrier();
        // the blocks' offsets (one order of summing for the offsets and the round's total)
        if (t == 0u) {
            float sum = 0.0;
            for (uint b = 0u; b < blocks; ++b) {
                float part = g_block[b];
                g_block[b] = sum;
                sum += part;
            }
            g_block[CELL_BLOCKS] = sum;
        }
        barrier();
        float round_sum = g_block[CELL_BLOCKS];
        if (rounds == 1u) {
            total = round_sum;
        }
        // each invocation's slots in this round: the block whose part holds the point, then the entry in it
        for (uint s = 0u; s < SLOTS_EACH && total > 0.0; ++s) {
            uint k = t + s * CELL_GROUP;
            float at = (float(k) + offset) / float(slots) * total - before;
            if (found[s].x != 0u || at < 0.0 || !(at < round_sum)) {
                continue;
            }
            uint b = 0u;
            while (b + 1u < blocks && g_block[b + 1u] <= at) {
                ++b;
            }
            float inside = at - g_block[b];
            uint lo = b * size;
            uint hi = min((b + 1u) * size, here);
            if (lo >= hi) {
                continue;
            }
            hi -= 1u;
            while (lo < hi) {
                uint mid = (lo + hi) / 2u;
                if (g_prefix[mid] > inside) {
                    hi = mid;
                } else {
                    lo = mid + 1u;
                }
            }
            float w = cell_weight(first + lo, low, high);
            if (w > 0.0) {
                found[s] = uvec2(first + lo + 1u, floatBitsToUint(total / w));
            }
        }
        before += round_sum;
        barrier();
    }
    for (uint s = 0u; s < SLOTS_EACH; ++s) {
        uint k = t + s * CELL_GROUP;
        cells[cell * slots + k] = found[s];
        // (round DO) every stride-th slot with a point, for the list's readers at any surface (vulkan_light_list.h
        // light_grid_reader_glsl) - from first_pick on: a systematic draw of the same CDF; the pick's pdf the slot's - its
        // list pdf over (W x the entry's), so light / pdf is the slot's contribution weight's estimate (a triangle's per
        // unit of its area)
        if (k % stride == first_pick) {
            uint point_rng = path_sequence_start(k, cell, pass.counts.z, pass.params.w + 11u);
            vec2 uv = vec2(path_next(point_rng), path_next(point_rng));
            LightPick p = no_light_pick();
            if (found[s].x > 0u) {
                uint picked = found[s].x - 1u;
                float W = uintBitsToFloat(found[s].y);
                p = light_pick_at(picked, count, uv);
                float scale = W * light_entries[picked].pdf;
                p.pdf = scale > 0.0 && p.pdf > 0.0 ? p.pdf / scale : 0.0;
            }
            light_pool_write(2u + cell * LIGHT_GRID_PICKS + k / stride, p);
        }
    }
}
#endif
)";

// (round PF) The presampled pool's pass (one invocation a slot): a pick of the local group by power and a uniform point
// on a triangle - local_light_at's - kept for the passes that resample many candidates (vulkan_light_list.h
// light_pool_read).
constexpr const char* kPoolBody = R"(
layout(local_size_x = 64) in;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= LIGHT_POOL_SIZE) {
        return;
    }
    uint rng = path_sequence_start(i % 4096u, i / 4096u, pass.counts.z, pass.params.w);
    float u = path_next(rng);
    vec2 uv = vec2(path_next(rng), path_next(rng));
    uint count = pass.counts.x;
    light_pool_write(i, count == 0u ? no_light_pick() : light_pick_at(light_pick(u, count), count, uv));
}
)";

enum class Shader : uint8_t { kScan, kPrimary, kTemporal, kSpatial, kBounds, kCells, kPool };

std::string shader_source(Shader which, bool check)
{
    std::string source = "#version 460\n#extension GL_EXT_ray_query : require\n";
    // (round PERF6) the frame's counts (kCountNames)
    source += "#extension GL_KHR_shader_subgroup_basic : require\n#extension GL_KHR_shader_subgroup_arithmetic : require\n";
    source += "#define DI_COUNTS " + std::to_string(kDiCounts) + "u\n";
    source += "#define PRESAMPLED_POINTS " + std::to_string(kPresampled) + "u\n"; // (round BUDGET)
    source += "#define PRESAMPLED_POINTS_SIDE " + std::to_string(kPresampledSide) + "u\n"; // (round NOISE)
    for (std::size_t k = 0; k < kCountNames.size(); ++k) {
        source += "#define DI_COUNT_" + std::string(core::at(kCountNames, k).define) + " " + std::to_string(k) + "u\n";
    }
    // (the candidates the rays meet - vulkan_rt_scene.cpp RAY_CANDIDATE_SEEN: camera rays, seen-through tests, blocking tests)
    source += "uint g_ray_candidates[3] = uint[3](0u, 0u, 0u);\n#define RAY_CANDIDATE_SEEN(kind) ++g_ray_candidates[kind]\n";
    if (check) {
        source += "#define CHECK\n";
    }
    switch (which) {
    case Shader::kPrimary: source += "#define PASS_PRIMARY\n"; break; // (round PERF6)
    case Shader::kTemporal: source += "#define PASS_TEMPORAL\n"; break;
    case Shader::kSpatial: source += "#define PASS_SPATIAL\n"; break;
    case Shader::kBounds: source += "#define PASS_BOUNDS\n"; break;
    case Shader::kCells: source += "#define PASS_CELLS\n"; break;
    case Shader::kScan: break;
    case Shader::kPool: break;
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
    if (which == Shader::kTemporal) {
        source += restir_denoiser_glsl();
    }
    source += rt_lamp_glsl();
    source += "const uint SKY_WIDTH = 1u;\nconst uint SKY_HEIGHT = 1u;\n";
    source += "const uint TRIANGLE_LATTICE = " + std::to_string(rt::lights::kTriangleLattice) + "u;\n";
    source += kCommon;
    source += light_instances_glsl();
    source += rt::lights::kLightListGlsl;
    if (which == Shader::kPool) {
        source += "#define LIGHT_POOL_SIZE " + std::to_string(kLightPoolSize) + "u\n#define LIGHT_POOL_WRITE\n";
        source += "layout(std430, set = 0, binding = " + std::to_string(kBindingLightPool) +
                  ") writeonly buffer LightPool { uvec4 light_pool[]; };\n";
        source += local_light_glsl();
        source += kPoolBody;
    } else if (which == Shader::kScan) {
        source += emitter_scan_glsl();
    } else if (which == Shader::kBounds || which == Shader::kCells) {
        if (which == Shader::kCells) {
            // (round DO) the grid's picks for the list's other readers (vulkan_light_list.h kLightGridPicks) - the pool's
            // words
            source += "#define LIGHT_POOL_SIZE 1u\n#define LIGHT_POOL_WRITE\n#define LIGHT_GRID_PICKS " +
                      std::to_string(kLightGridPicks) + "u\n#define GRID_STATE_WORD " + std::to_string(kGridStateWord) +
                      "u\n#define GRID_KEEP_FRAMES " + std::to_string(kGridKeepFrames) + "u\n#define CELL_GROUP " +
                      std::to_string(kCellGroup) + "u\n#define SLOTS_EACH " + std::to_string(kGridSlots / kCellGroup) + "u\n#define CELL_CACHE " + std::to_string(kCellCache) + "u\n#define CELL_BLOCKS " + std::to_string(kCellBlocks) + "u\n";
            source += "layout(std430, set = 0, binding = " + std::to_string(kBindingGridPicks) +
                      ") buffer LightPool { uvec4 light_pool[]; };\n";
            source += local_light_glsl();
        }
        source += kGridBody;
    } else {
        // (round PERF3) the grid's cells' state words (grid_cell_of)
        source += "#define GRID_STATE_WORD " + std::to_string(kGridStateWord) + "u\nlayout(std430, set = 0, binding = " +
                  std::to_string(kBindingGridPicks) + ") buffer GridWords { uvec4 grid_words[]; };\n";
        source += kRestirBody;
    }
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
    // (round PERF6) with frame_profile on: each slot's counts (read_back's), what the processor did that frame, and the
    // usual time and counts (an average over the last ~32 passes) a slow pass is logged against
    DeviceBuffer counts;
    std::array<bool, kTimingSlots> counted{};
    struct HostFacts {
        uint64_t lists = 0;   // lists adopted so far
        uint64_t tables = 0;  // alias tables made so far
        bool scanning = false; // a scan in flight
        bool history = false;  // the temporal reuse on
        uint32_t entries = 0;
    };
    std::array<HostFacts, kTimingSlots> facts{};
    double usual_ms = 0.0;
    std::array<double, kDiCounts> usual_counts{};
    uint32_t followed = 0;
    uint32_t slow_logged = 0;
    uint64_t tables = 0;
};

struct RestirState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkPipeline scan = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> primary{};  // (round PERF6) the game's, the check's
    std::array<VkPipeline, 2> temporal{}; // the game's, the check's
    std::array<VkPipeline, 2> spatial{};
    VkPipeline bounds_pass = VK_NULL_HANDLE; // (round PT4c) the light grid's
    VkPipeline cells_pass = VK_NULL_HANDLE;
    VkPipeline pool_pass = VK_NULL_HANDLE; // (round PF) the presampled pool's
    DeviceBuffer light_pool;
    uint64_t pool_serial = 0;    // the submission its picks were last made in
    uint64_t pool_table = 0;     // ... from this table
    uint32_t pool_seed = 0;
    DeviceBuffer bounds;
    DeviceBuffer cells;
    DeviceBuffer grid_picks;     // (round DO) the grid's picks for the list's other readers
    uint64_t grid_serial = 0;    // the submission the grid (and its picks) were last made in
    uint64_t grid_table = 0;     // ... and the table they were made from
    bool failed = false;
    // the emitting triangles: the list in use and the one a scan fills, read back without a wait
    std::array<DeviceBuffer, 2> emitters{};
    int list_now = -1;
    DeviceBuffer membership;
    // per triangle its place in a list + 1: one for each list buffer, so a scan in flight fills the idle one's while the
    // list in use keeps its own (each names its own list's places)
    std::array<DeviceBuffer, 2> slots{};
    DeviceBuffer read_back;
    uint64_t list_key = 0;    // the world, its emission and the lamps' surfaces the list in use was scanned from
    uint64_t scan_key = 0;    // ... and the scan in flight's
    // which triangles the list names (the world's instances): its places hold while this does; the emission the
    // triangles carry changes under them (the relit store's first light, written over frames) without changing it
    uint64_t list_structure = 0;
    uint64_t scan_structure = 0;
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
    DeviceBuffer shared; // (round OM) the view's primary surface for ReSTIR GI (kSharedSurfaceBytes a pixel)
    SharedPrimary shared_view;
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

bool& shared_primary_on()
{
    static bool on = true;
    return on;
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
            } else if (binding == kBindingDepth || binding == kBindingLampsOut || binding == kBindingLampsOnOut ||
                       binding == kBindingSpecularOut) {
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
    if (own.scan != VK_NULL_HANDLE && own.pool_pass == VK_NULL_HANDLE) {
        own.pool_pass = make_compute(device, own.layout, shader_source(Shader::kPool, false), error);
    }
    return own.scan != VK_NULL_HANDLE && own.pool_pass != VK_NULL_HANDLE;
}

bool ensure_pipelines(Backend& state, bool check, std::string* error)
{
    RestirState& own = restir_state();
    const VkDevice device = state.context.device();
    if (!ensure_scan(state, error)) {
        return false;
    }
    for (auto [pipeline, which] : {std::pair{&own.bounds_pass, Shader::kBounds}, std::pair{&own.cells_pass, Shader::kCells}}) {
        if (*pipeline == VK_NULL_HANDLE) {
            *pipeline = make_compute(device, own.layout, shader_source(which, false), error);
            if (*pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    const std::size_t variant = check ? 1U : 0U;
    if (core::at(own.primary, variant) == VK_NULL_HANDLE) {
        core::at(own.primary, variant) = make_compute(device, own.layout, shader_source(Shader::kPrimary, check), error);
        if (core::at(own.primary, variant) == VK_NULL_HANDLE) {
            return false;
        }
    }
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
// places are the list's): which triangles a list's places name. world_key_of adds the emission the world's light store
// holds (round PX) and the cut: what the powers were measured from - a list of an older emission keeps its triangles.
uint64_t world_structure_of(const rt::FrameRecord& record)
{
    uint64_t key = kFnvBasis;
    uint32_t place = 0;
    for (const rt::Instance& instance : record.instances) {
        if ((instance.flags & rt::kWorld) != 0U) {
            key = mix(mix(mix(mix(key, place), instance.mesh), instance.material), instance.flags);
        }
        ++place;
    }
    return key;
}

uint64_t world_key_of(uint64_t structure, const SceneLighting& lighting)
{
    return mix(mix(structure, lighting.cut), lighting.store_emission);
}

// The scan of the frame's world recorded (its list into the buffer not in use, read back into read_back).
bool record_scan(Backend& state, RestirState& own, VkCommandBuffer commands, const SceneView& scene, uint64_t key,
                 uint64_t structure)
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
    DeviceBuffer& slots = core::at(own.slots, static_cast<std::size_t>(into));
    const VkBufferUsageFlags storage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const VkDeviceSize list_bytes =
        (kEmitterHeaderWords + (static_cast<VkDeviceSize>(kEmitterEntryWords) * kListCapacity)) * sizeof(uint32_t);
    if (!ensure_buffer(state, &list, list_bytes, storage, BufferPlace::kDevice) ||
        !ensure_buffer(state, &slots, std::max<VkDeviceSize>(total, 1) * sizeof(uint32_t), storage,
                       BufferPlace::kDevice) ||
        !upload_buffer(state, &own.membership, firsts.data(), firsts.size() * sizeof(uint32_t)) ||
        !ensure_buffer(state, &own.read_back, list_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack)) {
        return false;
    }
    vkCmdFillBuffer(commands, list.buffer, 0, kEmitterHeaderWords * sizeof(uint32_t), 0);
    vkCmdFillBuffer(commands, slots.buffer, 0, VK_WHOLE_SIZE, 0);
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
    const VkDescriptorBufferInfo slots_info{.buffer = slots.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
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
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiGrid); // (round PF)
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
    own.scan_structure = structure;
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
    own.list_structure = own.scan_structure;
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
    ++own.timing.tables;
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

// (round PERF6) A counted pass's numbers against the usual: a pass much slower than usual is logged with its counts and
// what the processor did that frame ("restir slow pass: ..."); every kTimingReport passes the usual counts.
void note_counts(Backend& state, RestirState& own, uint32_t slot, uint64_t serial, double ms)
{
    Timing& timing = own.timing;
    if (timing.counts.mapped == nullptr) {
        return;
    }
    vmaInvalidateAllocation(state.memory.allocator(), timing.counts.allocation, 0, VK_WHOLE_SIZE);
    std::array<uint32_t, kDiCounts> counts{};
    const std::span<const uint8_t> all(static_cast<const uint8_t*>(timing.counts.mapped),
                                       static_cast<std::size_t>(kTimingSlots * kCountersBytes));
    std::memcpy(counts.data(), all.subspan((slot * kCountersBytes) + (4 * sizeof(uint32_t)), sizeof(counts)).data(),
                sizeof(counts));
    const Timing::HostFacts& facts = core::at(timing.facts, slot);
    constexpr double kSlowShare = 1.4;
    constexpr double kSlowMs = 1.0;
    constexpr double kFollow = 1.0 / 32.0;
    constexpr uint32_t kSettle = 60;
    constexpr uint32_t kMostLogged = 200;
    if (timing.followed >= kSettle && ms > timing.usual_ms * kSlowShare && ms > timing.usual_ms + kSlowMs &&
        timing.slow_logged < kMostLogged) {
        ++timing.slow_logged;
        std::string text;
        for (std::size_t k = 0; k < kCountNames.size(); ++k) {
            text += text::format("%s%s %u (%.0f)", k == 0 ? "" : ", ", core::at(kCountNames, k).text, core::at(counts, k),
                                 core::at(timing.usual_counts, k));
        }
        get_log("restir slow pass (submission %llu): %.2f ms (usual %.2f) - %s; lists %llu, tables %llu, entries %u%s%s",
                static_cast<unsigned long long>(serial), ms, timing.usual_ms, text.c_str(),
                static_cast<unsigned long long>(facts.lists), static_cast<unsigned long long>(facts.tables), facts.entries,
                facts.scanning ? ", a scan in flight" : "", facts.history ? "" : ", no history");
    }
    ++timing.followed;
    timing.usual_ms = timing.usual_ms > 0.0 ? timing.usual_ms + ((ms - timing.usual_ms) * kFollow) : ms;
    for (std::size_t k = 0; k < kDiCounts; ++k) {
        double& usual = core::at(timing.usual_counts, k);
        usual = timing.followed == 1 ? core::at(counts, k) : usual + ((core::at(counts, k) - usual) * kFollow);
    }
    if (timing.followed % kTimingReport == 0) {
        std::string text;
        for (std::size_t k = 0; k < kCountNames.size(); ++k) {
            text += text::format("%s%s %.0f", k == 0 ? "" : ", ", core::at(kCountNames, k).text, core::at(timing.usual_counts, k));
        }
        get_log("restir counts a pass (usual, %.2f ms): %s", timing.usual_ms, text.c_str());
    }
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
    const double ms = static_cast<double>(ticks.back() - ticks.front()) * timing.period_ns / kNsPerMs;
    timing.sum_ms += ms;
    if (core::at(timing.counted, slot)) {
        core::at(timing.counted, slot) = false;
        note_counts(state, own, slot, written, ms);
    }
    if (++timing.measured >= (check ? kCheckTimingReport : kTimingReport)) {
        own.last_ms = timing.sum_ms / timing.measured;
        if (!check) {
            get_log("restir: %.3f ms a frame on the device over the last %u (%ux%u; %u emitting triangles of %u emitting at a "
                    "corner, %u lamps)",
                    own.last_ms, timing.measured, own.timed_width, own.timed_height,
                    static_cast<uint32_t>(own.triangle_powers.size()), own.candidates, own.lamp_count);
        }
        timing.sum_ms = 0.0;
        timing.measured = 0;
    }
}

// (round FA) The debug view's terms as the pass's flags (none in the check).
uint32_t debug_flags(bool check)
{
    const uint32_t terms = check ? debug_term::kAll : debug_terms();
    return ((terms & debug_term::kLampsDiffuse) == 0U ? kFlagNoDiffuse : 0U) |
           ((terms & debug_term::kLampsSpecular) == 0U ? kFlagNoSpecular : 0U);
}

// Whether this view's lamps are ReSTIR's: rt_lights on, the direct light restir (or auto with Ray Reconstruction to
// denoise it), and the view lit per surface.
bool wanted(Backend& state)
{
    const RtLightSettings& settings = rt_light_settings_locked();
    if (!settings.on) {
        return false;
    }
    // (round GRIT: auto is ReSTIR with or without Ray Reconstruction - one lighting path; without it the upscaler's own
    // temporal accumulation takes the noise. Before, auto fell back to the lamps pass: another light, most lamps unlit)
    const bool restir = settings.direct != RtDirectLight::kPasses;
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
    // (round NOBIAS3) the scan's places are the built instances' triangles (scene_instance_triangles) and its emission is
    // read through the instances' light streams: both are what the TLAS and its geometry table hold now, not only what
    // the record asks for - a mesh built or a stream given after the scan is scanned again
    const uint64_t structure = mix(world_structure_of(record), scene.live_key);
    const uint64_t world_key = mix(world_key_of(structure, lighting), scene.light_key);
    // the emitters' list: a finished scan taken into use; a new one when the world or its lamps' surfaces changed. A scan
    // of the same triangles under an older emission is taken in (its powers a little old until the next scan's): the list
    // never empties while only the emission under its triangles changes - the relit store's first light is written over
    // seconds, and a gap of a scan's frames without a list took every emitting surface's light (BO's street lamps) away.
    if (own.scan_serial != 0 && state.frames.completed() >= own.scan_serial) {
        if (own.scan_structure == structure) {
            adopt_scan(state, own);
        } else {
            own.scan_serial = 0; // (the world changed while it ran: scanned again)
        }
    }
    if (own.list_now >= 0 && own.list_structure != structure) {
        own.list_now = -1; // (its places name another world's triangles: none until the new scan is in)
        own.triangle_powers.clear();
        own.table_key = 0;
    }
    if (own.scan_serial == 0 && own.list_key != world_key) {
        barrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        if (!record_scan(state, own, commands, scene, world_key, structure)) {
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
    if (core::at(own.slots, 0).buffer == VK_NULL_HANDLE &&
        !upload_buffer(state, &core::at(own.slots, 0), empty_list.data(), sizeof(empty_list))) {
        return false;
    }
    own.local_lamps = lighting.lamps;
    return true;
}

} // namespace

namespace {

// (round PF) The presampled pool made for this submission's passes (once a submission, again when the table changes):
// the earlier readers waited for, the picks made, the readers after it wait for them. False: no buffer.
constexpr uint32_t kPoolSeedStep = 0x9E3779B9U; // (each pool's own random sequence: the golden ratio's step)
bool keep_light_pool(Backend& state, RestirState& own, VkCommandBuffer commands, const SceneView& scene,
                     const DeviceBuffer& list)
{
    constexpr VkDeviceSize kBytes = VkDeviceSize{kLightPoolSize} * kLightPoolWords * sizeof(uint32_t);
    // (round OM: ReSTIR GI reads it through its address)
    if (!ensure_buffer(state, &own.light_pool, kBytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, BufferPlace::kDevice)) {
        return false;
    }
    if (own.entries == 0 || (own.pool_serial == state.frames.serial() && own.pool_table == own.table_key)) {
        return true;
    }
    RestirBlock block;
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), 0U, 0U};
    block.counts = {own.entries, own.lamp_count, static_cast<uint32_t>(state.frames.serial()), 0U};
    block.params = {0U, 0U, 0U, ++own.pool_seed * kPoolSeedStep};
    std::array<uint8_t, sizeof(RestirBlock)> bytes{};
    std::memcpy(bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> span = state.ring.copy(bytes, kUniformAlignment);
    if (!span) {
        return true; // (the pool as it was)
    }
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    const auto whole = [](const DeviceBuffer& buffer) {
        return VkDescriptorBufferInfo{.buffer = buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    };
    const VkDescriptorBufferInfo geometry_info{.buffer = scene.geometry, .offset = 0, .range = scene.geometry_bytes};
    const VkDescriptorBufferInfo block_info{.buffer = span->buffer, .offset = span->offset, .range = sizeof(RestirBlock)};
    const std::array<VkDescriptorBufferInfo, 6> buffers{whole(own.lights), whole(list),       whole(own.lamps),
                                                        whole(own.membership), whole(core::at(own.slots, static_cast<std::size_t>(std::max(own.list_now, 0)))),
                                                        whole(own.light_pool)};
    const std::array<uint32_t, 6> buffer_bindings{kBindingLights,     kBindingEmitters, kBindingLamps,
                                                  kBindingMembership, kBindingSlots,    kBindingLightPool};
    std::array<VkWriteDescriptorSet, 9> writes{};
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
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        write(3 + i, core::at(buffer_bindings, i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &core::at(buffers, i), nullptr);
    }
    const FrameStep step(state.frames, commands, gpu_markers::section::kLightPool);
    // (the last submission's readers of the pool, and the scan's list / the scene's build, before the picks)
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.pool_pass);
    vkCmdDispatch(commands, (kLightPoolSize + kScanGroup - 1) / kScanGroup, 1, 1);
    barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
    own.pool_serial = state.frames.serial();
    own.pool_table = own.table_key;
    return true;
}

} // namespace

std::size_t restir_listed_triangles_locked(const Backend& state)
{
    static_cast<void>(state);
    const RestirState& own = restir_state();
    return own.list_now >= 0 ? own.triangle_powers.size() : 0;
}

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
    if (!keep_light_pool(state, own, commands, scene, list)) {
        return false;
    }
    out->light_pool = own.light_pool.buffer;
    // (round DO) the grid's picks when ReSTIR DI made them this frame from this table (else the readers take the pool)
    out->grid_picks = own.grid_serial == state.frames.serial() && own.grid_table == own.table_key ? own.grid_picks.buffer
                                                                                                    : VK_NULL_HANDLE;
    out->emitters = list.buffer;
    out->lamps = own.lamps.buffer;
    out->membership = own.membership.buffer;
    out->slots = core::at(own.slots, static_cast<std::size_t>(std::max(own.list_now, 0))).buffer;
    out->table = own.lights.buffer;
    out->entries = own.entries;
    out->triangle_powers = own.list_now >= 0 ? own.triangle_powers : std::vector<float>{};
    out->lamp_list = own.local_lamps;
    out->key = own.table_key;
    out->layout_key = own.layout_key;
    return true;
}

std::string_view restir_denoiser_glsl()
{
    // (round PT4d) ReSTIR's output as a denoiser's input. Ray Reconstruction (like every temporal denoiser) averages noise
    // that changes from frame to frame; ReSTIR's reuse holds a sample in a pixel's history for up to its 20 frames and
    // hands it to the neighbours - the error is correlated over frames and over patches, and the denoiser keeps it as
    // picture (Boston's courtyard: RR left 3-5 of display std where the lamps pass, noiseless, had 0.4). NVIDIA's RTXDI
    // has two settings for exactly this, on in its denoised configurations; both followed here as RTXDI defines them.
    static constexpr std::string_view kSource = R"(
// Permutation sampling (RTXDI's temporal resampling, enablePermutationSampling): the reservoir taken from last frame is
// another pixel's of the same 4 x 4 block - the block's pixels mirrored in x and y after an offset that changes every frame
// (the same for every pixel). A pixel's history is then no single chain carried for frames but a different neighbour's
// each frame: its error changes from frame to frame, the noise a temporal denoiser averages. Bias: none beyond the reuse's
// own - the neighbour's surface passes the same tests and its sample is taken with the same correction (its own surface's
// target and ray).
ivec2 restir_permuted(ivec2 before, uint frame)
{
    uint bits = path_pcg(frame * 0x9E3779B9u + 0x7F4A7C15u);
    ivec2 offset = ivec2(int(bits & 3u), int((bits >> 2u) & 3u));
    return ((before + offset) ^ ivec2(3)) - offset;
}
// The boiling filter (RTXDI's, its default strength 0.2): a reservoir whose contribution (its target x W) is over
// 10 / 0.2 - 9 = 41 x the mean of the group's (8 x 8 pixels) non-zero contributions is dropped. Such a sample - found by
// chance, its weight dwarfing its neighbours' - is kept by the temporal reuse and spread by the spatial reuse for frames:
// a bright patch that grows and fades (the "boiling"). Bias: the dropped outliers' energy (darker where a rare very bright
// light is the true answer) - RTXDI's stated trade.
const float RESTIR_BOILING_STRENGTH = 0.2;
shared float restir_boil_weight[64];
shared uint restir_boil_count[64];
bool restir_boiling(float weight)
{
    uint t = gl_LocalInvocationIndex;
    restir_boil_weight[t] = weight > 0.0 ? weight : 0.0;
    restir_boil_count[t] = weight > 0.0 ? 1u : 0u;
    memoryBarrierShared();
    barrier();
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (t < stride) {
            restir_boil_weight[t] += restir_boil_weight[t + stride];
            restir_boil_count[t] += restir_boil_count[t + stride];
        }
        memoryBarrierShared();
        barrier();
    }
    float mean = restir_boil_weight[0] / float(max(restir_boil_count[0], 1u));
    return weight > mean * (10.0 / RESTIR_BOILING_STRENGTH - 9.0);
}
)";
    return kSource;
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
    out->last_light.resize(pixels);
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
        core::at(out->last_light, i) = core::at(last, 3);
    }
    out->frames = static_cast<uint32_t>(frames);
    std::vector<uint8_t> counters;
    if (state.memory.read_buffer(own.counters, 0, sizeof(uint32_t), &counters) && counters.size() >= sizeof(uint32_t)) {
        std::memcpy(&out->not_finite, counters.data(), sizeof(uint32_t));
    }
    return true;
}

bool read_restir_primary(Backend& state, RestirPrimaryReadback* out)
{
    *out = RestirPrimaryReadback{};
    RestirState& own = restir_state();
    if (own.shared.buffer == VK_NULL_HANDLE || own.width == 0 || own.height == 0 || !finish_work(state)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(own.width) * own.height;
    constexpr std::size_t kRowFloats = kSharedSurfaceBytes / sizeof(float);
    constexpr std::size_t kAlbedoRow = 3; // (at, normal, triangle normal, albedo + kind, F0)
    std::vector<uint8_t> bytes;
    if (!state.memory.read_buffer(own.shared, 0, pixels * kSharedSurfaceBytes, &bytes) ||
        bytes.size() < pixels * kSharedSurfaceBytes) {
        return false;
    }
    out->width = own.width;
    out->height = own.height;
    constexpr std::size_t kMaterialFloat = (4 * 4) + 3; // (the F0 row's w)
    out->albedo_kind.resize(pixels);
    out->material.resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::memcpy(core::at(out->albedo_kind, i).data(),
                    &core::at(bytes, ((i * kRowFloats) + (kAlbedoRow * 4)) * sizeof(float)), 4 * sizeof(float));
        std::memcpy(&core::at(out->material, i), &core::at(bytes, ((i * kRowFloats) + kMaterialFloat) * sizeof(float)),
                    sizeof(float));
    }
    return true;
}

RestirStats restir_stats()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    const RestirState& own = restir_state();
    return RestirStats{.passes = own.passes,
                       .lighting = own.lit_view,
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
    own.shared_view = SharedPrimary{}; // (round OM: none for this view until its first pass is recorded)
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
        lighting.store_emission = rt::store_emission(); // (the world's emission as the game's frame reads it)
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
    // (round BUDGET: a reservoir two words - the second its sample's emission)
    bool made = ensure_buffer(state, &own.reservoirs_a, pixels * 32, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.reservoirs_b, pixels * 32, storage, BufferPlace::kDevice) &&
                ensure_buffer(state, &own.counters, kCountersBytes, storage, BufferPlace::kDevice) &&
                (!check || ensure_buffer(state, &own.sums, pixels * 2 * 16, storage, BufferPlace::kDevice)) &&
                ensure_buffer(state, &own.bounds, std::max<VkDeviceSize>(own.entries, 1) * kRecordStride * 16, storage,
                              BufferPlace::kDevice) &&
                ensure_buffer(state, &own.cells,
                              static_cast<VkDeviceSize>(kGridAcross) * kGridUp * kGridAcross * kGridSlots * 2 * sizeof(uint32_t),
                              storage, BufferPlace::kDevice) &&
                // (round DO: read by the list's other readers through its address - its header, then the picks)
                // (round PERF3: two header picks, and a state word a cell after the picks - kGridStateWord)
                ensure_buffer(state, &own.grid_picks,
                              ((static_cast<VkDeviceSize>(kGridStateWord) + kGridCells) * 4 * sizeof(uint32_t)),
                              storage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, BufferPlace::kDevice);
    for (std::size_t k = 0; k < 2 && made; ++k) {
        made = ensure_buffer(state, &core::at(own.at, k), pixels * 16, storage, BufferPlace::kDevice) &&
               ensure_buffer(state, &core::at(own.more, k), pixels * 16, storage, BufferPlace::kDevice);
    }
    // (round OM) the shared primary surface, read by ReSTIR GI through its address - (round PERF6) and by this view's
    // temporal pass (PASS_PRIMARY writes it): made whether or not GI may read it
    made = made && ensure_buffer(state, &own.shared, pixels * kSharedSurfaceBytes,
                                 storage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, BufferPlace::kDevice);
    const bool sharing = shared_primary_on() && made;
    if (!made) {
        if (first_report(state, kReportBuffers)) {
            get_log("restir: the reservoirs (%ux%u) couldn't be made - the lamps pass lights the lamps", width, height);
        }
        own.history = false;
        return false;
    }
    if (inputs != nullptr && (inputs->depth == nullptr || inputs->lamps == nullptr || inputs->lamps_on == nullptr ||
                            inputs->specular == nullptr)) {
        return false;
    }

    // (round PERF6) the frame's counts with frame_profile on, read back with the pass's time
    const bool counting = !check && frame_profile::on() && ensure_timing(state, own) &&
                          ensure_buffer(state, &own.timing.counts, kTimingSlots * kCountersBytes,
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack);

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
    const uint32_t flags = (history ? kFlagTemporal : 0U) | (spatial > 0 ? kFlagSpatial : 0U) |
                           (check && own.accumulate ? kFlagAccumulate : 0U) |
                           (!check || own.overrides.permutation ? kFlagPermute : 0U) |
                           (!check || own.overrides.boiling_filter ? kFlagBoiling : 0U) | debug_flags(check) |
                           (counting ? kFlagCount : 0U);
    block.counts = {own.entries, own.lamp_count, static_cast<uint32_t>(own.frames), flags};
    block.params = {candidates, std::min(spatial, kMostSpatial), kHistoryCap * candidates,
                    check ? own.overrides.seed : 0x2545F491U};
    block.tune = {kSpatialRadius, kNormalAgreement, kPlaneShare, 0.0F};
    block.force = {own.overrides.roughness, own.overrides.f0, own.overrides.albedo_scale, check ? 1.0F : 0.0F};
    block.instances = {static_cast<uint32_t>(scene.instances_address & 0xFFFFFFFFULL),
                       static_cast<uint32_t>(scene.instances_address >> 32U), scene.instances, 0U};
    block.list = {kListCapacity, 0U, 0U, 0U};
    // (round PT4c) the light grid round the camera, its corner on a cell's boundary (cells keep their place in the world
    // while the camera moves within one)
    const bool grid = (!check || own.overrides.grid) && own.entries > 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const uint32_t across = axis == 1 ? kGridUp : kGridAcross;
        const float corner = std::floor(core::at(view.camera.position, axis) / kGridSide) - static_cast<float>(across / 2);
        core::at(block.grid, axis) = corner * kGridSide;
        // (round PERF3) the cells by their place in the world: an axis's index is the world cell's modulo the cells
        // across - the corner's cell modulo them is the shift (light_grid_slot)
        const auto wrapped = static_cast<int64_t>(corner) % static_cast<int64_t>(across);
        core::at(block.grid_shift, axis) = static_cast<uint32_t>(wrapped < 0 ? wrapped + across : wrapped);
    }
    core::at(block.list, 2) = check ? 1U : 0U; // (round PERF3: the check makes every cell)
    core::at(block.grid, 3) = kGridSide;
    block.grid_cells = {kGridAcross, kGridUp, kGridAcross, grid ? kGridSlots : 0U};
    VkDeviceAddress shared_address = 0;
    {
        const VkBufferDeviceAddressInfo address_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = own.shared.buffer};
        shared_address = vkGetBufferDeviceAddress(state.context.device(), &address_info);
        block.shared = {static_cast<uint32_t>(shared_address & 0xFFFFFFFFULL), static_cast<uint32_t>(shared_address >> 32U),
                        1U, 0U};
    }
    std::array<uint8_t, sizeof(RestirBlock)> block_bytes{};
    std::memcpy(block_bytes.data(), &block, sizeof(block));
    const std::optional<RingSpan> block_span = state.ring.copy(block_bytes, kUniformAlignment);
    if (!block_span) {
        return false;
    }
    if (sharing) { // (round OM: what ReSTIR GI's passes of this view may read)
        own.shared_view = {.address = shared_address,
                           .serial = state.frames.serial(),
                           .rect = block.rect,
                           .to_world = block.to_world,
                           .eye = {core::at(block.eye, 0), core::at(block.eye, 1), core::at(block.eye, 2)},
                           .check = check};
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
    if (counting) {
        vkCmdFillBuffer(commands, own.counters.buffer, 4 * sizeof(uint32_t), kDiCounts * sizeof(uint32_t), 0);
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
        for (Image* image : {inputs->depth, inputs->lamps, inputs->lamps_on, inputs->specular}) {
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
    const std::array<VkDescriptorBufferInfo, 16> buffers{
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
        whole(own.counters),
        whole(own.bounds),
        whole(own.cells),
        whole(own.grid_picks)};
    constexpr std::array<uint32_t, 16> kBufferBindings{kBindingGeometry,    kBindingBlock,      kBindingLights,
                                                       kBindingEmitters,    kBindingLamps,      kBindingReservoirsA,
                                                       kBindingReservoirsB, kBindingNowAt,      kBindingNowMore,
                                                       kBindingBeforeAt,    kBindingBeforeMore, kBindingSums,
                                                       kBindingCounters,    kBindingBounds,     kBindingCells,
                                                       kBindingGridPicks};
    std::array<VkDescriptorImageInfo, 4> images{};
    if (inputs != nullptr) {
        images = {VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->depth->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                  VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->lamps->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                  VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->lamps_on->view,
                                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                  VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE, .imageView = inputs->specular->view,
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
        add(kBindingSpecularOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &core::at(images, 3), nullptr);
    }
    const VkDescriptorSet material_set = state.materials.descriptor_set();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, kMaterialSet, 1, &material_set, 0,
                            nullptr);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    const std::size_t variant = check ? 1U : 0U;
    const uint32_t groups_x = (width + kGroupSide - 1) / kGroupSide;
    const uint32_t groups_y = (height + kGroupSide - 1) / kGroupSide;
    if (own.entries > 0) {
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiGrid); // (round PF)
        // (round PT4c) the light grid: the entries' bounds - (round BUDGET) and their prepared records, which every pass
        // reads, grid or not -, then every cell's slots
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.bounds_pass);
        vkCmdDispatch(commands, (own.entries + kScanGroup - 1) / kScanGroup, 1, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    if (grid) {
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiGrid);
        // (round NOISE2) one workgroup a cell
        static_assert(kGridCells <= kMostGroupsX);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.cells_pass);
        vkCmdDispatch(commands, kGridCells, 1, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        own.grid_serial = state.frames.serial(); // (round DO: the grid's picks this frame's, of this table)
        own.grid_table = own.table_key;
    }
    {
        // (round PERF6) the primary surfaces in their own pass (the camera rays), then the candidates and the reuse
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiPrimary);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.primary, variant));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    {
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiTemporal); // (round PF)
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.temporal, variant));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    {
        const FrameStep step(state.frames, commands, gpu_markers::section::kDiSpatial);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.spatial, variant));
        vkCmdDispatch(commands, groups_x, groups_y, 1);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
    if (timed) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timing.pool, (slot * 2) + 1);
        core::at(own.timing.pending, slot) = state.frames.serial();
        own.timed_width = width;
        own.timed_height = height;
    }
    if (timed && counting) { // (round PERF6: after the spatial pass's barrier)
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = slot * kCountersBytes, .size = kCountersBytes};
        vkCmdCopyBuffer(commands, own.counters.buffer, own.timing.counts.buffer, 1, &region);
        barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_HOST_READ_BIT);
        core::at(own.timing.counted, slot) = true;
        core::at(own.timing.facts, slot) = {.lists = own.lists,
                                            .tables = own.timing.tables,
                                            .scanning = own.scan_serial != 0,
                                            .history = history,
                                            .entries = own.entries};
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    if (inputs != nullptr) {
        for (Image* image : {inputs->depth, inputs->lamps, inputs->lamps_on, inputs->specular}) {
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

SharedPrimary restir_shared_primary(const Backend& state)
{
    const SharedPrimary& shared = restir_state().shared_view;
    return shared.serial == state.frames.serial() ? shared : SharedPrimary{};
}

void set_restir_shared_primary(bool on) { shared_primary_on() = on; }

void install_restir_di() { rt::add_view_listener(&listener()); }

void uninstall_restir_di() { rt::remove_view_listener(&listener()); }

void release_restir_di(Backend& state)
{
    RestirState& own = restir_state();
    const VkDevice device = state.context.device();
    for (std::array<VkPipeline, 2>* set : {&own.primary, &own.temporal, &own.spatial}) {
        for (VkPipeline& pipeline : *set) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
    }
    for (VkPipeline pipeline : {own.scan, own.bounds_pass, own.cells_pass, own.pool_pass}) {
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
    for (DeviceBuffer* buffer : {&core::at(own.emitters, 0), &core::at(own.emitters, 1), &own.membership, &core::at(own.slots, 0),
                                 &core::at(own.slots, 1),
                                 &own.read_back, &own.lights, &own.lamps, &own.reservoirs_a, &own.reservoirs_b,
                                 &core::at(own.at, 0), &core::at(own.at, 1), &core::at(own.more, 0), &core::at(own.more, 1),
                                 &own.sums, &own.counters, &own.bounds, &own.cells, &own.grid_picks, &own.light_pool, &own.shared, &own.timing.counts}) {
        state.memory.destroy_buffer(buffer);
    }
    const RestirOverrides overrides = own.overrides;
    own = RestirState{};
    own.overrides = overrides;
}

} // namespace render::vulkan
