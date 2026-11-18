#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_daylight.h"
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
    // (ReSTIR DI) per emitter of the level, how much it is on now (rt_lights.h lamp_on), in the steps the relit store
    // switches the lamps' own surfaces by: their emission changes with it - the emitters' scan is made again
    uint64_t surfaces_switched = 0;
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

// GLSL (after scene_geometry_glsl and hit_surface_glsl; the caller defines LIST_INSTANCE_ADDRESS - the TLAS instances'
// device address as a uvec2 expression - and LIST_INSTANCES, the instances' count, and declares `uint membership[]` (the
// instances' first triangles, LIST_INSTANCES + 1 of them) and `uint slots[]` (per triangle its place in the list + 1)):
//   mat4x3 instance_transform(uint i); uint instance_material(uint i); uint listed_slot(uint instance, uint primitive)
[[nodiscard]] std::string_view light_instances_glsl();
// The scan's body (local_size_x 64, one invocation a triangle; after light_instances_glsl, rt_path.h's kPathGlsl and
// TRIANGLE_LATTICE; the caller defines LIST_TRIANGLES and LIST_CAPACITY and declares the Emitters buffer's
// emitter_count, emitter_spare and uvec4 emitter_entries[]).
[[nodiscard]] std::string_view emitter_scan_glsl();

} // namespace render::vulkan
