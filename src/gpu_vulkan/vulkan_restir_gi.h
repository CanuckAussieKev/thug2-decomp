#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the indirect light and the environment's specular by ReSTIR GI (path tracing plan, stage 5;
// docs/design/vulkan.md "ReSTIR GI: the indirect light in real time"). With ReSTIR DI (vulkan_restir_di.h) for the lamps
// and the shadows pass for the sun's or the moon's disc, the second real-time stage of the path tracer: every pixel of a
// view lit per surface (vulkan_surface_light.h) gets the light that reaches it from the sky and off the surfaces around it,
// written into the view's light inputs - the one consumer stays round V's shading. Noisy: Ray Reconstruction denoises it.
//
// Per pixel, two compute passes with ray queries (after the lamps' inputs are made, before the shading):
//   1. the pixel's surface (as ReSTIR DI's: the prepass's depth, a camera ray's triangle - hit_surface: its position,
//      normals and material - kept for the next frame) and ONE sample of the light coming in: a direction drawn from a
//      mix of the sky's distribution (the light list's sky grid - its luminance) and the cosine lobe, traced:
//      - nothing hit: the sky's radiance that way (the grid's - the disc is not in it: the shadows pass owns the sun's and
//        the moon's direct light), a sample at infinity (a direction);
//      - a surface hit (the secondary vertex - hit_surface: its albedo, material): its reflected light by next-event
//        estimation through the light list - one light picked by power among the disc (with the clouds' shadow), the sky
//        (its grid), the emitting triangles and the analytic lamps (the same list, lights and power as the reference's -
//        vulkan_light_list.h; the local group's scan is ReSTIR DI's), one ray to it; not its emission (round BR): the
//        light straight from an emitter - diffuse and specular - is ReSTIR DI's (one owner);
//      resampled toward the target p^ = luminance of the light a white diffuser there reflects toward the eye from it -
//      the reservoir holds the sample's point (or direction), its surface's normal, its reflected light, W and M;
//      - temporal reuse (Ouyang et al. 2021, ReSTIR GI; Bitterli et al. 2020's unbiased combination): the reservoir the
//        point had last frame (through last frame's camera, ReSTIR DI's surface tests), its sample reconnected to this
//        surface: its weight x the reconnection's Jacobian (the solid angle the sample's surface element spans from here
//        over from there: cos / d^2 here over cos / d^2 there; 1 for the sky) - Lin et al. 2022 (GRIS)'s shift; Z the M
//        of every reservoir whose surface could have made the chosen sample (its target there > 0, a ray from there);
//      - the environment's specular by one ray along a GGX visible normal of the surface's lobe, every surface's (round
//        BR: before, those of roughness 0.3 and more took it from the diffuse reservoir's sample - one that lands in the
//        lobe by chance, single bright pixels from frame to frame that Ray Reconstruction passed as sparkle - Kajiya's and
//        Lumen's split: resampling for the diffuse, a ray per pixel for the reflections): its weight F G2/G1, what it meets
//        (its reflected light, the sky past it). (Round WS) That ray is the candidate of a specular reservoir of its own
//        (ReSTIR GI's reuse of the whole BSDF - Ouyang et al. 2021; RTXDI's GI with a BRDF-aware target): target p^ = the
//        luminance of the specular lobe x the sample's light at the surface's own view; last frame's specular reservoir
//        of the point's own pixel (not permuted: a neighbour's view of a lobe puts its samples in this lobe's tails)
//        reconnected here, combined by the generalised balance heuristic (Lin et al. 2022) with RTXDI's GI sample
//        validation (a reconnection Jacobian outside 1/10 .. 10 not taken) and the visibility in the weights' targets;
//        roughness-aware - a history or neighbour only of a like roughness (within 0.1), no reuse at all below roughness
//        0.2 (a near-mirror keeps its own ray, as before), the spatial radius shrinking with the lobe. The reuse is OFF in
//        the game (kSpecularReuseInGame - measured noisier under Ray Reconstruction, +2.6 ms); the check's alone;
//   2. ... and the specular reservoir's spatial reuse: one neighbour (the weights' rays grow with the square of the
//      reservoirs), the same heuristic; the chosen sample's ray from the pixel the result's visibility;
//   2. spatial reuse: the pixel's reservoir and two neighbours' within 24 pixels that pass the surface tests, reconnected
//      the same way; the chosen sample traced from the pixel (ray_light - the one light ray: cut-outs by their alpha,
//      seen-through surfaces by their transmittance); out:
//      - the diffuse: its reflected light x cos / pi x W x the transmittance - a white diffuser's radiance, added into the
//        view's lamps input (and on the traced albedo into lamps_on) - the shading puts it on the surface's albedo as it
//        puts the lamps;
//      - the visibility input's indirect visibility set to 0: the light store's environment term (its sky K and bounce B)
//        has no weight where this pass lit the surface - its sky and bounce are these samples;
//      - the specular: the specular reservoir's chosen sample (round WS - was pass 1's ray) over the surface's specular
//        albedo E (demodulated), added into the specular input
//        (ReSTIR DI wrote the lights' there first): the shading takes E x it in place of its environment's specular; and
//        the specular ray's hit distance for Ray Reconstruction (its specular hit distance guide).
// A pixel's next-frame history is its temporal reservoir, not the spatial one: fed back, the neighbours' samples come back
// into their own spatial reuse - the inputs no longer independent, the unbiased combination's weights not unbiased (the
// check's furnace lost 1.5 % so; 0.4 % with the chains apart).
// Unbiased for a still world (the check: one bounce against the reference); where the world moves, last frame's samples
// carry last frame's light for up to their M cap (20 frames' worth) - the stated bias of ReSTIR GI's reuse; a secondary
// surface's glossy reflection toward a neighbour is taken as toward the pixel that found it (its light is stored).
// Cost (round BR): every pixel traces its specular ray (+ its secondary's light ray) - Training at 853x480 0.94 -> ~1.5 ms.
// (Round MB) Further bounces: a radiance cache (NVIDIA's SHaRC - a hash grid of world cells, each the bounce a white
// diffuser there gets), written by sparse update paths (one pixel of each 4 x 4 block a frame, 3 cosine rays, every
// vertex's direct light by next-event estimation, the last vertex's bounce the cache's own) and read at the secondary
// vertices: their light toward the pixel = their direct light + their reflectance x the cache's bounce - every bounce,
// converging over the frames (docs/design/vulkan.md "Further bounces: a radiance cache"). Run first, before pass 1.
// Not here: the sun's and the moon's direct light (the shadows pass), the lamps' and emitting surfaces' direct light,
// diffuse and specular (ReSTIR DI / the lamps pass), seen-through surfaces' own light (glass, water: their own paths).
namespace render::vulkan {

struct Backend;
struct Image;


// (the check's) A run on the frame's scene with its lighting given here, not the game's: the primary surface from camera
// rays (no depth), the result accumulated per pixel (on the material below: the BSDF x the indirect light, the
// emitters' light left out - the reference's indirect light: everything but the direct light of the disc, the emitters
// and the lamps), no inputs written.
struct RestirGiOverrides {
    bool on = false;
    std::vector<rt::Emitter> lamps;      // the analytic lamps (x their intensity)
    std::vector<uint8_t> lamps_surfaced; // per lamp: its own surface emits (rt_path.h analytic_lamps)
    std::array<float, 3> sky{};          // an even sky's radiance above the horizon (the global unit)
    std::array<float, 3> sun_toward{0.0F, 1.0F, 0.0F};
    std::array<float, 3> sun_light{};    // the sun's light per N.L (0: no sun)
    float sun_radius_degrees = 0.0F;
    float roughness = 0.5F;              // every surface's material: its albedo x albedo_scale, these
    float f0 = 0.04F;
    float albedo_scale = 1.0F;
    uint32_t spatial = 0xFFU; // spatial neighbours (0xFF: the default; 0: none)
    bool temporal = true;
    // (round PT4d) the denoiser's settings (vulkan_light_list.h restir_denoiser_glsl): permutation sampling of the
    // temporal reuse, the boiling filter
    bool permutation = true;
    bool boiling_filter = true;
    bool cache = true; // (round MB) the secondary vertices' further bounces from the radiance cache (false: one bounce)
    // (round BUDGET2) a path ends at a trusted cache cell its footprint covers (false: every first vertex takes its
    // light sample - the checks of the secondary vertices' light sampling)
    bool cache_ends = true;
    // (round DD) the local lights' RIS candidates a next-event sample takes from the pool (0: the game's; 1: one pick
    // by power alone, as before round DD)
    uint32_t local_candidates = 0;
    // (round WS) the specular reservoir's temporal and spatial reuse (false: each frame's own GGX ray alone, as before)
    bool specular_reuse = true;
    bool specular_only = false; // (round WS) the sums the specular alone (its reuse measured apart from the diffuse)
    uint32_t seed = 0;
};
void set_restir_gi_overrides(const RestirGiOverrides& overrides);
void forget_restir_gi_history();
// (round OM - the checks') The passes' frame count started again too: a run's random numbers the same from its start.
void restart_restir_gi_sequence();
// (round OM - the checks') The local lights' next-event picks from the presampled pool (true, the default) or from the
// list itself (as before).
void set_restir_gi_pool(bool on);
// (round DO - the checks') The local lights' candidates from ReSTIR DI's light grid inside it (true, the default -
// vulkan_light_list.h light_grid_reader_glsl) or from the pool alone (as before).
void set_restir_gi_grid(bool on);
// Whether the check's frames are added to the sums.
void set_restir_gi_accumulate(bool on);

struct RestirGiAccumulation {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 0;
    std::vector<std::array<float, 3>> mean;
    std::vector<std::array<float, 3>> last;
    uint32_t not_finite = 0;
};
bool read_restir_gi_accumulation(Backend& state, RestirGiAccumulation* out);
void restart_restir_gi_accumulation();

struct RestirGiStats {
    uint64_t passes = 0;
    uint32_t lights = 0;     // the full list's entries (the disc, the sky, the triangles, the lamps)
    bool sky_ready = false;  // the sky's grid sampled by its luminance (its read-back in)
    uint64_t sky_grids = 0;  // grids made
    double gpu_ms = 0.0;     // the passes' time on the device (the mean of the last report; 0: not measured)
    double cache_ms = 0.0;   // (round MB) ... the radiance cache's share of it (its update paths and its entries' frame)
    uint32_t gpu_width = 0;
    uint32_t gpu_height = 0;
    uint64_t shared_views = 0; // (round OM) views whose primary surfaces were ReSTIR DI's (vulkan_restir_di.h)
};
[[nodiscard]] RestirGiStats restir_gi_stats();

// Whether this view's indirect light will be ReSTIR GI's ([Graphics] indirect_light restir, or auto with Ray
// Reconstruction, in a view lit per surface) - asked by the fallback passes before it runs (the occlusion then writes
// nothing); and whether it lit the view being drawn (the reflections leave the light model's specular to it). The
// caller holds the lock.
[[nodiscard]] bool restir_gi_wanted(Backend& state);
// (round FL) The same asked before the view's inputs exist (a view decided lit per surface at its first world draw: its
// see-through draws are prepared to read the front layer then - vulkan_front_layer.h).
[[nodiscard]] bool restir_gi_chosen(const Backend& state);
[[nodiscard]] bool restir_gi_lit_view(const Backend& state);
struct FrameLightList; // (vulkan_light_list.h)
// (round WF - path tracing, stage 7) The full light list and the radiance cache this frame's view was lit with (false: GI
// didn't light it): the reflections' hits (vulkan_rt_reflections.h) light what they meet by them. The caller holds the lock.
bool restir_gi_frame_lights(const Backend& state, FrameLightList* out);
// (round SKIN2 - the checks') The radiance cache's entries as they are now (the frame's work finished first; kRadianceCacheCapacity
// entries of 8 uvec4 - light_sample_glsl's layout: the key word x the cell's check, y the frame it was claimed, z its
// samples, w its frames) and the last list it was used with (its eye and cell angle key the cells). False: none yet.
// The caller holds the lock.
bool restir_gi_read_cache(Backend& state, std::vector<uint8_t>* entries, FrameLightList* list);
// The specular ray's hit distance (R16F, world units; the colour target's size, the view at its rect) written for the
// view being drawn - Ray Reconstruction's specular hit distance guide; null when this frame's view has none.
[[nodiscard]] Image* restir_gi_specular_hit(Backend& state);

void install_restir_gi();
void uninstall_restir_gi();
void release_restir_gi(Backend& state);
bool record_restir_gi_pass(Backend& state, const rt::ViewDrawn& view);

} // namespace render::vulkan
