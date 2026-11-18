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
struct RestirLocalLights {
    VkBuffer emitters = VK_NULL_HANDLE;   // the list (the header + entries above)
    VkBuffer lamps = VK_NULL_HANDLE;      // a LightRecord per lamp
    VkBuffer membership = VK_NULL_HANDLE; // the instances' first triangles (the scan's)
    VkBuffer slots = VK_NULL_HANDLE;      // per triangle its place in the list + 1
    std::vector<float> triangle_powers;   // per listed triangle (the list's order)
    std::vector<rt::Emitter> lamp_list;   // the lamps the records hold (the list's order)
    uint64_t key = 0;                     // changes with the powers or the lamps
    uint64_t layout_key = 0;              // changes when an entry's light does (the list's triangles, the lamps' count)
};
struct Backend;
bool restir_local_lights(Backend& state, VkCommandBuffer commands, const SceneLighting& lighting, RestirLocalLights* out);

// GLSL (after scene_geometry_glsl and hit_surface_glsl; the caller defines LIST_INSTANCE_ADDRESS - the TLAS instances'
// device address as a uvec2 expression - and LIST_INSTANCES, the instances' count, and declares `uint membership[]` (the
// instances' first triangles, LIST_INSTANCES + 1 of them) and `uint slots[]` (per triangle its place in the list + 1)):
//   mat4x3 instance_transform(uint i); uint instance_material(uint i); uint listed_slot(uint instance, uint primitive)
[[nodiscard]] std::string_view light_instances_glsl();
// The scan's body (local_size_x 64, one invocation a triangle; after light_instances_glsl, rt_path.h's kPathGlsl and
// TRIANGLE_LATTICE; the caller defines LIST_TRIANGLES and LIST_CAPACITY and declares the Emitters buffer's
// emitter_count, emitter_spare and uvec4 emitter_entries[]).
[[nodiscard]] std::string_view emitter_scan_glsl();

// (round PT4d) GLSL for ReSTIR DI's and GI's temporal passes (local_size 8 x 8; after rt_path.h's kPathGlsl): what makes
// their output a denoiser's input (NVIDIA RTXDI's settings for a denoiser that follows - Ray Reconstruction):
//   ivec2 restir_permuted(ivec2 before, uint frame) - permutation sampling of the temporal reuse;
//   bool restir_boiling(float weight) - the boiling filter (every invocation of the group calls it: a barrier inside).
// Defined in vulkan_restir_di.cpp; docs/design/vulkan.md "ReSTIR under Ray Reconstruction".
[[nodiscard]] std::string_view restir_denoiser_glsl();

} // namespace render::vulkan
