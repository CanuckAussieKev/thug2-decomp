#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the direct light of the local lights by ReSTIR DI (path tracing plan, stage 4; docs/design/vulkan.md
// "ReSTIR DI: the lamps' light in real time"). The first real-time stage of the path tracer: the lamps and the emitting
// surfaces (the light list's local group - engine/gfx/rt_light_list.h, vulkan_light_list.h: the same lights with the same
// power as the reference path tracer's) light every pixel of a view lit per surface (vulkan_surface_light.h), its result
// written into the view's lamps inputs - the one consumer stays round V's shading. Noisy: Ray Reconstruction denoises it.
//
// Per pixel, two compute passes with ray queries (after the inputs are begun, before the lamps pass - which then draws
// nothing: one owner):
//   1. the pixel's surface - the prepass's depth gives the point, a camera ray its triangle (hit_surface: position, the
//      triangle's and the shading normal, albedo; where the ray doesn't end at the depth's point the pixel gets no lamps,
//      as the lamps pass's did) - kept for the next frame (position, normals) beside its reservoir;
//      - initial candidates: params' count of lights picked from the alias table by power (a point uniform on an emitting
//        triangle, the lamp's own point for an analytic lamp), resampled (RIS, Talbot et al. 2005 / Bitterli et al. 2020)
//        toward the target p^ = the luminance of the light a white diffuser would get from it unshadowed (a triangle's at
//        its mean radiance: its listed power / 2 pi area - exact emission only where shaded) - the BRDF left out: the
//        inputs carry the lamps' light, the surface's shading puts it on its own material;
//      - one ray to the chosen light (vulkan_rt_scene.h ray_light: cut-outs by their alpha, seen-through surfaces by their
//        transmittance - the one light ray); a light it doesn't reach is dropped (W = 0: visibility reuse);
//      - temporal reuse: the reservoir the pixel's point had last frame (the point taken back through last frame's camera -
//        the world stands where it stood; a surface that moved fails the tests below and starts afresh), kept when that
//        surface's normal agrees (cos >= 0.9) and its plane is within 2 % of the distance; its M capped at 20 x the
//        candidates;
//   2. spatial reuse: the pixel's reservoir and those of `spatial` neighbours within 24 pixels that pass the same tests;
//      the light chosen traced from the pixel and its weight 1 / (Z p^) with Z the samples of the reservoirs that could
//      have made it - each neighbour's target and a ray from its surface (Bitterli et al. 2020's unbiased combination);
//      likewise the temporal step (a ray from last frame's surface). Unbiased for a scene that stands still; where the
//      scene moves, last frame's reservoir was made against last frame's occluders (the stated bias - small, a frame's
//      worth). The result (the light x its visibility x W) goes into the lamps input (a white diffuser's radiance, the
//      frame's pre-exposed unit) and on the traced albedo into lamps_on; the reservoir is kept for the next frame.
// The light list (local only: the sun's or the moon's disc and the sky keep their own owners - the shadows pass's
// visibility and the light store's sky with the occlusion - see the doc): the emitting triangles found by the shared
// scan (once a world, a lighting cut or a switch of the lamps' own surfaces; read back a few frames later without a
// wait, the old list used meanwhile), the analytic lamps every frame (positions and intensities as they are now - a moving
// or switching lamp needs nothing rebuilt), the alias table rebuilt on the processor when their power changes.
namespace render::vulkan {

struct Backend;

// (the check's) A run on the frame's scene with its lighting given here, not the game's: the primary surface from camera
// rays (no depth), the result accumulated per pixel (the full light model on the material below - the reference's
// direct light), no inputs written.
struct RestirOverrides {
    bool on = false;
    std::vector<rt::Emitter> lamps;      // the analytic lamps (x their intensity)
    std::vector<uint8_t> lamps_surfaced; // per lamp: its own surface emits (one owner: rt_path.h analytic_lamps)
    float roughness = 0.5F;              // every surface's material: its albedo x albedo_scale, these
    float f0 = 0.04F;
    float albedo_scale = 1.0F;
    uint32_t candidates = 0;  // initial candidates a pixel (0: the default)
    uint32_t spatial = 0xFFU; // spatial neighbours (0xFF: the default; 0: none)
    bool temporal = true;
    uint32_t seed = 0;
};
// (kept as they are: the reservoirs carry on - the lamps may move between frames; forget_restir_history drops them)
void set_restir_overrides(const RestirOverrides& overrides);
void forget_restir_history();
// Whether the check's frames are added to the sums (the frames between, e.g. another camera's, still reuse).
void set_restir_accumulate(bool on);

// What a check run has accumulated (the frame's work finished first; the caller holds the lock).
struct RestirAccumulation {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 0;
    std::vector<std::array<float, 3>> mean;
    std::vector<std::array<float, 3>> last; // the last frame's estimate alone
    uint32_t not_finite = 0;
};
bool read_restir_accumulation(Backend& state, RestirAccumulation* out);
// The sums started again at the next pass (the reservoirs kept).
void restart_restir_accumulation();

// The counts (the log, the checks).
struct RestirStats {
    uint64_t passes = 0;
    uint32_t listed = 0;      // emitting triangles in the list now
    uint32_t candidates = 0;  // (the scan's: emitting at a corner)
    uint32_t lamps = 0;       // analytic lamps
    uint64_t lists = 0;       // lists adopted (scans read back)
    bool list_ready = false;  // a scan's list in use
    double gpu_ms = 0.0;      // the passes' time on the device, the mean of the last report (0: not measured)
    uint32_t gpu_width = 0;
    uint32_t gpu_height = 0;
};
[[nodiscard]] RestirStats restir_stats();

// Whether ReSTIR lit the lamps of the view being drawn (the lamps pass then draws none - vulkan_rt_lights.h). The caller
// holds the lock.
[[nodiscard]] bool restir_lit_view(const Backend& state);

// The listener added / removed with ray tracing (vulkan_rt_scene.cpp), and the pass's objects released (the caller holds
// the lock).
void install_restir_di();
void uninstall_restir_di();
void release_restir_di(Backend& state);
// The passes for a viewport (the listener's work). The caller holds the lock. False: not this view (another tier, no
// inputs, no scene).
bool record_restir_di_pass(Backend& state, const rt::ViewDrawn& view);

} // namespace render::vulkan
