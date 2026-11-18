#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the reference path tracer (path tracing plan, stage 1; vulkan_path_reference.cpp): an unbiased,
// progressive integrator over the frame's own acceleration structures, materials and light store - the yardstick the
// realtime tier (the shadows, the lamps, the bounce, the reflections) and the later ReSTIR stages are measured against.
// The console's `pt ref <samples> [EV100]` (engine/gfx/rt_path.h) runs it with the game paused.
//
// Each frame while it runs (a listener at ViewOrder kPathReference, after the lens, before the tone mapping): a compute
// pass with ray queries traces `samples per frame` paths through every pixel of the viewport (as it is shown - after the
// upscaler) from the camera frozen when it began, adding them into fp32 sums without bound while the camera and the scene
// stay as they are (another camera, another scene: it starts again); their mean (the global unit) x the frame's
// pre-exposure is written over the viewport, so the tone mapping shows it at the manual EV (vulkan_hdr.h set_manual_ev100:
// the given EV100, else the metered one when it began). Once the samples are in, the reference and the realtime frame
// (copied out before the first reference was written over it) are saved beside the exe in Screenshots\ as
// pt_<stamp>_reference / _realtime: a linear EXR each (32-bit float RGB, the global unit - physical light, the pre-exposure
// divided out) and a PNG each through the tone curve at that EV.
//
// A path (rt_path.h's sampling, rt_brdf.h's light model, vulkan_rt_scene.h hit_surface at every hit):
//   - camera rays jittered within each pixel; each hit's surface (hit_surface: the relit world's albedo, the objects'
//     texels, the material's roughness and F0, the light store's emission); cut-outs by their alpha everywhere (the camera's
//     rays, the bounces, the shadow rays); the instances world, objects and alpha-tested (masks 0x07: what every light ray
//     sees today - the transparent ones untraced, the plan's P2);
//   - next-event estimation from each hit to one light of the run's light list (engine/gfx/rt_light_list.h - path
//     tracing stage 2), picked by its power from an alias table: the sun's or the moon's disc (sampled over its real
//     solid angle, its radiance pi x the lighting's direct light per N.L / that, x the clouds' shadow map where the
//     clouds draw one), the sky (its grid - the sky-view table x the drawn clouds' transmittance + their light, else x the
//     clouds' kept share; without the table an even sky - sampled by its luminance), an emitting triangle (a point
//     uniformly on it) or an analytic lamp (rt_lights.h's lamp model; the frame record's point lights where the level's
//     lamps aren't solved) - the lamps whose own surface emits are sampled as that surface, never also as their analytic
//     light (rt_path.h analytic_lamps: one owner, so a lamp is never counted twice);
//   - the BSDF sampled for the next direction; what it hits by chance (the sky, the sun's disc, an emitting triangle) is
//     weighed against the light list's pdf for it (the same pick x direction pdf) by the power heuristic (MIS); Russian
//     roulette from the third bounce;
//   - the light list: made once a run (its lighting frozen with its camera; another cut, a lamp switched, starts it
//     again) - in its first frame the device scans every triangle of the world (its bounds; each emitting one's power:
//     2 pi x area x its emission's mean luminance at rt_light_list.h kTrianglePoints points, the emission hit_surface
//     gives) and fills the sky's grid; in the next the processor reads them back, builds the alias tables and uploads
//     them; the paths start then.
// Retires nothing: the realtime passes are untouched.
namespace render::vulkan {

struct Backend;

inline constexpr int kPathReferenceOrder = 971; // (rt::ViewOrder: after the lens 970, before the tone mapping 972)

// (the check's) The light strategies a run uses: both with MIS (the reference), the lights alone, the BSDF alone.
enum class PathStrategy : uint8_t { kMis = 0, kLightsOnly = 1, kBsdfOnly = 2 };

// (the check's) A run on the frame's scene with its lighting given here, not the game's: accumulates while `on` until
// read (no captures, no manual EV).
struct PathOverrides {
    bool on = false;
    PathStrategy strategy = PathStrategy::kMis;
    uint32_t bounces = rt::path::kMostBounces;
    uint32_t samples_per_frame = 1;
    uint32_t seed = 0;
    std::array<float, 3> sky{};                     // an even sky's radiance above the horizon (the global unit)
    std::array<float, 3> sun_toward{0.0F, 1.0F, 0.0F};
    std::array<float, 3> sun_light{};               // the sun's light per N.L (0: no sun)
    float sun_radius_degrees = 0.0F;
    bool force_material = false;                    // every hit's material: these values, its albedo x albedo_scale
    float roughness = 0.5F;
    float f0 = 0.04F;
    float albedo_scale = 1.0F;
    std::vector<rt::Emitter> lamps;                 // the level's lamps (each x its intensity)
    std::vector<uint8_t> lamps_surfaced;            // per lamp: its own surface emits (rt_path.h analytic_lamps)
    rt::lights::LightWeights light_weights = rt::lights::LightWeights::kPower; // how the list picks its lights
};
void set_path_overrides(const PathOverrides& overrides);

// What has been accumulated (the frame's work finished first; the caller holds the lock): each pixel's mean radiance
// (the global unit), the samples a pixel, the emitting triangles found and the samples that were not finite (left out).
struct PathAccumulation {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t samples = 0;
    std::vector<std::array<float, 3>> mean;
    uint32_t emitters = 0;
    uint32_t not_finite = 0;
};
bool read_path_accumulation(Backend& state, PathAccumulation* out);
// The sums started again at the next pass (a new seed's run).
void restart_path_reference();
[[nodiscard]] uint64_t path_reference_passes();

// The listener added / removed with ray tracing (vulkan_rt_scene.cpp create_ray_tracing / release_ray_tracing), and the
// pass's objects released (the caller holds the lock).
void install_path_reference();
void uninstall_path_reference();
void release_path_reference(Backend& state);
// The pass for a viewport (the listener's work). The caller holds the lock. False: not running, or it can't run here
// (logged once).
bool record_path_reference_pass(Backend& state, const rt::ViewDrawn& view);

} // namespace render::vulkan
