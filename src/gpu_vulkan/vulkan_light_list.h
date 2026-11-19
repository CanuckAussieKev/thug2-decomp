#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"

// NOT ORIGINAL - the light list's device side, shared by everything that samples it (path tracing, stage 2's list -
// engine/gfx/rt_light_list.h): the reference path tracer (vulkan_path_reference.h) and ReSTIR DI's direct light
// (vulkan_restir_di.h). One lighting state, one lamp record, one scan of the emitting triangles - so the realtime
// estimator and the yardstick sample the same lights with the same power.
//   - scene_lighting: the frame's lighting state as the lights see it (the relit world's sun and sky, else the clock's;
//     the sky-view table; the level's lamps without a surface of their own - rt_path.h analytic_lamps, one owner - and
//     the frame record's point lights where the level's lamps aren't solved);
//   - lamp_record: a lamp as the shaders' LightRecord (vulkan_rt_lights.h rt_lamp_glsl);
//   - light_instances_glsl / emitter_scan_glsl: an instance's transform and material without a hit, a triangle's place
//     in the emitters' list, and the scan that measures each emitting triangle's power (2 pi x area x the mean luminance
//     of hit_surface's `emitted` at kTriangleLattice^2 points) and lists it.
namespace render::vulkan {

struct SceneLighting {
    std::array<float, 3> sun_toward{0.0F, 1.0F, 0.0F};
    std::array<float, 3> sun_light{};
    float sun_radius_degrees = rt::kSunRadiusDegrees;
    std::array<float, 3> sky{}; // (an even sky's radiance: without the table)
    bool table = false;
    AtmosphereInput table_input;
    float table_factor = 1.0F;      // x the table: 1 / its exposure (x the kept sky without drawn clouds)
    // (round PF) the factor less the table's exposure - what the sky's light is, whatever the camera's exposure: what a
    // sky made once from the table (ReSTIR GI's grid) is keyed by (the table is drawn again at each exposure; its light
    // x table_factor stays the same)
    float table_share = 1.0F;
    bool clouds = false;            // the sky's grid marches the drawn clouds
    RtSkyFrame cloud_frame;         // (their parameters - the sky pass's last)
    std::vector<rt::Emitter> lamps; // the analytic ones (x their switches) and the frame record's point lights
    uint32_t owned = 0;
    double owned_power = 0.0; // (reported) the owned lamps' flux by their model, as switched now (rt_light_list.h lamp_power)
    uint32_t points = 0;
    uint64_t cut = 0; // (rt_daylight.h DayLight::cut)
    // (ReSTIR DI; round PX) the emission the world's light store holds (rt_lights.h store_emission): the emitters' scan
    // reads the store, so it is made again when the store's emission was written - not when it was asked for (the lamps'
    // switches, which the relit world writes over frames after)
    uint64_t store_emission = 0;
};

// The frame's lighting state. `every_lamp_analytic`: every lamp of the level analytic, no surface owning one (`pt ref
// ... analytic`); `keep_dark`: lamps switched off are kept (intensity 0) - a list whose lamps keep their places while
// they switch (ReSTIR's reservoirs name a lamp by its place).
[[nodiscard]] SceneLighting scene_lighting(bool every_lamp_analytic, bool keep_dark);
// What a lighting is (the reference starts again when it changes): the cut, the direct light, the sky, the lamps.
[[nodiscard]] uint64_t lighting_key_of(const SceneLighting& lighting);

// A lamp as the shaders' LightRecord (rt_lamp_glsl): at, colour x intensity + reach, (material, core, shape, -), axis +
// half length.
inline constexpr std::size_t kLampRecordFloats = 16;
[[nodiscard]] std::array<float, kLampRecordFloats> lamp_record(const rt::Emitter& lamp);

// The emitters' list on the device: a header (count listed, candidates - emitting at a corner -, the reference's samples
// not finite, -) then one entry a triangle (instance, primitive, power as float bits, -).
inline constexpr uint32_t kEmitterCapacity = 1U << 20U;
inline constexpr uint32_t kEmitterHeaderWords = 4;
inline constexpr uint32_t kEmitterEntryWords = 4;

// The disc of the lighting's direct light as the shaders take it: toward it + the cosine of its radius, and its radiance +
// its solid angle (rt_path.h disc_radiance). False: none (no light, or no direction) - `sun` then holds kNoDisc as its
// cosine, a value no direction reaches.
inline constexpr float kNoDisc = 2.0F;
bool disc_of(const SceneLighting& lighting, std::array<float, 4>* sun, std::array<float, 4>* radiance);

// (path tracing, stage 5: shared by the reference and ReSTIR GI) The sky's grid (engine/gfx/rt_light_list.h
// SkyDistribution's cells): each cell's radiance toward its centre in the global unit - the sky-view table x the drawn
// clouds' transmittance + their light (the same march, noise and light as the drawn clouds, faded with distance as the
// composite fades them) over the table's exposure; without drawn clouds the table x the lighting state's kept share of the
// sky; without the table an even sky above the horizon. One function for what a ray that leaves the world sees and what
// the lights sample.
enum class SkyGridSource : uint8_t { kEven = 0, kTable = 1, kTableClouds = 2 };
inline constexpr std::size_t kSkyGridSources = 3;
[[nodiscard]] SkyGridSource sky_grid_source(const SceneLighting& lighting);
// The values its shader reads from the caller's block (`pass.sky`, `.sky_table`, `.cloud_ray`, `.cloud_sun`,
// `.cloud_toward` - vec4s).
struct SkyGridInputs {
    std::array<float, 4> sky{};          // rgb an even sky's radiance above the horizon; w 1: the table's
    std::array<float, 4> sky_table{};    // xyz the table's sun; w the factor on its values
    std::array<float, 4> cloud_ray{};    // xy the clouds' offset (metres), z the camera's altitude, w coverage
    std::array<float, 4> cloud_sun{};    // rgb the light on the clouds, w the march's steps
    std::array<float, 4> cloud_toward{}; // xyz toward that light, w the density's scale
};
[[nodiscard]] SkyGridInputs sky_grid_inputs(const SceneLighting& lighting);
// The GLSL that goes before the libraries of the grid's shader: the source's defines with the atmosphere's and the clouds'
// functions at those bindings (nothing for kEven).
[[nodiscard]] std::string sky_grid_prelude(SkyGridSource source, uint32_t transmittance_binding, uint32_t sky_view_binding,
                                           uint32_t cloud_noise_binding);
// The grid's body (local_size 8 x 8, one invocation a cell; after rt_light_list.h kLightListGlsl, SKY_WIDTH / SKY_HEIGHT,
// the block and `vec4 sky_cells[]`).
[[nodiscard]] std::string_view sky_grid_glsl();
// The sky's alias tables as the shaders read them (`uvec2 sky_alias[]`: threshold bits, alias - the rows, then each row's
// cells).
[[nodiscard]] std::vector<std::array<uint32_t, 2>> sky_alias_words(const rt::lights::SkyDistribution& sky);

// (path tracing, stage 5) The light list's local group as ReSTIR DI keeps it (vulkan_restir_di.cpp) - the emitting
// triangles its scan listed (their power), the lamps' records - shared with ReSTIR GI's next-event estimation at its
// secondary hits (vulkan_restir_gi.h): one scan, one table of the same lights with the same power. Kept up for
// `lighting`'s lamps (the frame's, or a check's) whether or not the DI passes run this frame: a finished scan taken into
// use, a new one recorded into `commands` when the world or its lamps' own surfaces changed. The caller holds the lock.
// False: none (no scene, no material table, the scan can't be made).
// (round PF - NVIDIA RTXDI's presampled light tiles) The pool of picks of the local group made once a submission
// (local_light_glsl's light_pool_read: kLightPoolWords 32-bit words a pick) - RTXDI's 128 tiles of 1024.
inline constexpr uint32_t kLightPoolSize = 128U * 1024U;
inline constexpr uint32_t kLightPoolWords = 8;
struct RestirLocalLights {
    VkBuffer light_pool = VK_NULL_HANDLE; // (round PF) the presampled pool (kLightPoolSize picks)
    VkBuffer emitters = VK_NULL_HANDLE;   // the list (the header + entries above)
    VkBuffer lamps = VK_NULL_HANDLE;      // a LightRecord per lamp
    VkBuffer membership = VK_NULL_HANDLE; // the instances' first triangles (the scan's)
    VkBuffer slots = VK_NULL_HANDLE;      // per triangle its place in the list + 1
    // (round WF) the local group's alias table (rt_light_list.h AliasEntry: threshold, alias, pdf, payload - the triangles
    // and the lamps by power, ReSTIR DI's) and its entries: what samples the local lights anywhere but a surface - the
    // volumetric fog's in-scattering (vulkan_rt_fog.h)
    VkBuffer table = VK_NULL_HANDLE;
    uint32_t entries = 0;
    std::vector<float> triangle_powers;   // per listed triangle (the list's order)
    std::vector<rt::Emitter> lamp_list;   // the lamps the records hold (the list's order)
    uint64_t key = 0;                     // changes with the powers or the lamps
    uint64_t layout_key = 0;              // changes when an entry's light does (the list's triangles, the lamps' count)
};
struct Backend;
bool restir_local_lights(Backend& state, VkCommandBuffer commands, const SceneLighting& lighting, RestirLocalLights* out);
// (round OM) The emitting triangles the local group's list holds now (0: none, or no scan in yet); the caller holds the lock.
[[nodiscard]] std::size_t restir_listed_triangles_locked(const Backend& state);

// GLSL (after scene_geometry_glsl and hit_surface_glsl; the caller defines LIST_INSTANCE_ADDRESS - the TLAS instances'
// device address as a uvec2 expression - and LIST_INSTANCES, the instances' count, and declares `uint membership[]` (the
// instances' first triangles, LIST_INSTANCES + 1 of them) and `uint slots[]` (per triangle its place in the list + 1)):
//   mat4x3 instance_transform(uint i); uint instance_material(uint i); uint listed_slot(uint instance, uint primitive)
[[nodiscard]] std::string_view light_instances_glsl();
// The scan's body (local_size_x 64, one invocation a triangle; after light_instances_glsl, rt_path.h's kPathGlsl and
// TRIANGLE_LATTICE; the caller defines LIST_TRIANGLES and LIST_CAPACITY and declares the Emitters buffer's
// emitter_count, emitter_spare and uvec4 emitter_entries[]).
[[nodiscard]] std::string_view emitter_scan_glsl();

// (round WF - path tracing, stage 7) GLSL: a local light of the list seen from any point - in a medium as well as on a
// surface (after light_instances_glsl, rt_path.h's kPathGlsl, rt_lamp_glsl, hit_surface_glsl and rt_light_list.h's
// kLightListGlsl; the caller declares `light_entries[]`, `emitter_entries[]` and `lamps[]` as ReSTIR DI's):
//   struct LocalLight { vec3 light; vec3 dir; float reach; float pdf; }
//   bool local_light_at(uint e, uint count, vec2 uv, vec3 at, out LocalLight l)
// `light`: the irradiance the light sends `at` across the way to it, unshadowed and in the global unit (a lamp: pi x its
// record's light x lamp_geometry facing it - a white diffuser's radiance x pi; a triangle: hit_emission's emission at the
// point `uv` picks x the cosine there / the distance squared - per unit of its area, so light / pdf is the estimate); `pdf`
// the list's (the entry's pick, a triangle's / its area). The receiver's response (a BRDF x the cosine, a phase) is the
// caller's: the same lights, the same power and the same emission as ReSTIR DI's light_point, GI's next_event and the
// reference path tracer.
[[nodiscard]] std::string_view local_light_glsl();

// (round WF - path tracing, stage 7) The full list and the radiance cache as ReSTIR GI used them in the frame (vulkan_restir_gi.h
// restir_gi_frame_lights): what the passes after it that light surfaces the prepass doesn't hold sample - one light list
// (its alias table over the disc, the sky's grid and the local group by power), one cache. The buffers live while the
// frame's work does.
inline constexpr uint32_t kRadianceCacheCapacity = 1U << 19U; // (entries: 4 uvec4 each - the key, the sums, the light)
inline constexpr uint32_t kRadianceCacheProbes = 8;
inline constexpr float kRadianceCacheCellLeast = 4.0F; // (world units: the cells' least side)
struct FrameLightList {
    uint64_t frame = 0; // (Backend::frame it was used in; 0: none)
    VkBuffer table = VK_NULL_HANDLE;
    VkBuffer emitters = VK_NULL_HANDLE;
    VkBuffer lamps = VK_NULL_HANDLE;
    VkBuffer membership = VK_NULL_HANDLE;
    VkBuffer slots = VK_NULL_HANDLE;
    VkBuffer sky_cells = VK_NULL_HANDLE;
    VkBuffer sky_alias = VK_NULL_HANDLE;
    VkBuffer cache = VK_NULL_HANDLE;
    uint32_t entries = 0;
    bool cache_on = false;
    std::array<float, 4> sun{};          // toward the disc, w the cosine of its radius (kNoDisc: none)
    std::array<float, 4> sun_radiance{}; // rgb its radiance, w its solid angle
    float sky_dist = 0.0F;               // 1 / the sky grid's luminance integral (0: no sky)
    float cache_cells = 0.0F;            // the angle a cache cell spans from the camera (its least)
    std::array<float, 3> eye{};          // the camera the cache's cells were keyed from
};

// (round WF - path tracing, stage 7) GLSL: the full list sampled from any point - one light by power, one ray (ReSTIR GI's
// next-event estimation at its secondary vertices, moved here so every consumer samples the same way; after
// local_light_glsl, ray_light (vulkan_rt_scene.h cutout_rays_glsl) and, with CLOUD_MAP, cloud_map_glsl; the caller declares
// `sky_cells[]` / `sky_alias[]` and defines LL_COUNT, LL_SUN, LL_SUN_RADIANCE, LL_SKY_DIST, LL_REACH (and with CLOUD_MAP
// LL_CLOUD_PLACE / LL_CLOUD_LIGHT)):
//   float sun_kept(vec3 at); vec3 sky_radiance(vec3 dir); float sky_density(vec3 dir);
//   vec3 light_sample(vec3 p, vec3 n, vec3 ng, uint mask, inout uint rng, out vec3 dir)   - the light along dir x its
//       visibility / its density: x a BSDF x the cosine, or x a phase, the estimate (n = ng = vec3(0): a medium's point)
// With LL_NO_DISC (round FL) a pick of the disc brings nothing: the estimate is of the rest of the list (the caller owns
// the disc's light - its own visibility ray).
// With LL_CACHE (the caller's `cache[]` - ReSTIR GI's radiance cache, 4 uvec4 an entry -, CACHE_CAPACITY, CACHE_PROBES,
// CACHE_CELL_LEAST, LL_EYE, LL_CACHE_CELLS, LL_CACHE_ON):
//   void cache_key(vec3 at, vec3 ng, out uint slot, out uint check); vec3 cache_bounce(vec3 at, vec3 ng)
[[nodiscard]] std::string_view light_sample_glsl();

// (round PT4d) GLSL for ReSTIR DI's and GI's temporal passes (local_size 8 x 8; after rt_path.h's kPathGlsl): what makes
// their output a denoiser's input (NVIDIA RTXDI's settings for a denoiser that follows - Ray Reconstruction):
//   ivec2 restir_permuted(ivec2 before, uint frame) - permutation sampling of the temporal reuse;
//   bool restir_boiling(float weight) - the boiling filter (every invocation of the group calls it: a barrier inside).
// Defined in vulkan_restir_di.cpp; docs/design/vulkan.md "ReSTIR under Ray Reconstruction".
[[nodiscard]] std::string_view restir_denoiser_glsl();

} // namespace render::vulkan
