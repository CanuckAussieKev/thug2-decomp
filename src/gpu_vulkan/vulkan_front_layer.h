#pragma once

#include <cstdint>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - round FL: see-through surfaces lit per pixel by the light everything else is lit by - UE5 Lumen's
// "Front Layer Translucency" (docs/design/vulkan.md "Round FL: the front layer"). Glass, a soft overlay and rt_water's
// water aren't in the prepass (they write no depth: what is behind shows through them), so the view's light inputs have
// no pixel for them and they kept their vertices' light store - the bake's light, without the lamps the opaque world
// takes per pixel, without the traced sun's shadows. Lumen's answer, here: the nearest see-through surface of each pixel
// - its front layer - gets a G-buffer of its own and is lit like an opaque surface; the layers behind it keep their own
// light.
//   1. Its depth (ViewOrder::kFrontLayer, the inputs made, before the shading): the view's kept draws that read it - the
//      see-through draws lit by the light model, rt_water's water as its rest surface - drawn into a depth of their own,
//      the nearest kept (vulkan_surface_light.h surface_light_front_prepass). A pixel has a front layer where that depth
//      is in front of the prepass's (not within 1 % of its distance - an overlay lying on its wall is the wall's: the
//      inputs' layer test, kLayerLift, lights it as the wall).
//   2. Its light (this pass, compute with ray queries): the point from that depth, its normal from its neighbours' (the
//      layer's own pixels; a flat pane or the water's rest surface), and the light list ReSTIR GI used this frame
//      (vulkan_light_list.h FrameLightList - the disc, the sky's grid, the emitting triangles, the lamps, one list, the
//      same power) sampled by the shared light_sample (kSamples a pixel, each one light and one ray) with the disc left
//      to its own ray (LL_NO_DISC): the disc's visibility traced toward a point of it (its cone; the clouds' shadow map)
//      - the shading puts the sun's or the moon's light through the light model with it, as the shadows pass's
//      visibility on an opaque surface -; the rest of the list on a white diffuser (x cos / pi) + the radiance cache's
//      bounce (SHaRC - every further bounce; (round LAYER) where the cache has no trusted cell - a point in the air, a
//      particle cloud's - one ray along the cosine lobe to the surface it meets, lit there: SHaRC's query rule, the path
//      traced on past a miss; (round PERF) that bounce added to the layer point's own cell - and, so the cells stay
//      current once trusted, every pixel of one tile in kFrontLayerUpdateTiles (in turn) traces and adds it too: SHaRC's
//      update paths for the points no opaque surface's path claims - the water's surface, the mist in the air):
//      a white diffuser's radiance, the lamps input's unit, pre-exposed.
//      Accumulated over frames (the layer's point taken back through the last view's camera, its depth there within 2 %
//      of the distance: at most kHistoryFrames, as a moving average) - Ray Reconstruction denoises the frame, not its
//      transparency layer, where the see-through surfaces are drawn (NVIDIA's DLSS-RR guide 3.4.10).
//   3. Its use (ff_shaders.cpp kLightFrontGlsl, water_shader.cpp): a fragment of a key with kLightFront at the layer's
//      depth takes the layer's light in place of its light store's (the sun's visibility, the rest as the lamps input,
//      the store's environment without weight - the in-place reflections pass owns the glass's and the water's
//      environment specular); the water's body is the texture's mean lit by it, its glitter by the disc's visibility.
//   4. (round SHEETS) Where the layer faces up (the water's rest surface), its other side: the view's ray traced on past
//      it to the surface a camera ray takes there, lit as the mirror's hit, x what the see-through surfaces crossed let
//      through - the water's shading takes it as what is behind the water (not the frame copied before the view's first
//      water draw, which lacks everything drawn after that copy), its distance the path through the water.
// One owner per light: the list's lights reach the layer through this pass alone (the light store's light - sky K,
// bounce B, its lamps - isn't read there; the lamps pass's lit set neither). Only in a view ReSTIR GI lit (its list);
// elsewhere - the fallback tier, no ray tracing, a view not lit per surface - nothing changes.
namespace render::vulkan {

struct Backend;

// The light samples a layer pixel takes a frame (one light, one ray each), and the most frames a pixel's history weighs.
inline constexpr uint32_t kFrontLayerSamples = 2;
// (round PERF) The layer's update paths into the radiance cache (SHaRC's update - ReSTIR GI's cache pass is the opaque
// surfaces'): a frame, every pixel of one 8 x 8 tile in this many traces its bounce and adds it to its cell - each tile
// in turn, every this many frames (a trusted cell is freed after the cache's 64 frames without a sample). Whole tiles,
// not one pixel of each block: a pixel's trace keeps its whole subgroup waiting (1 lane of 16 tracing cost ~0.8 ms of
// the 1 a whole one did at Barcelona's fountain, 1707 x 960).
inline constexpr uint32_t kFrontLayerUpdateTiles = 16;
inline constexpr float kFrontLayerHistoryFrames = 16.0F;

void install_front_layer();
void uninstall_front_layer();
// Its pipelines destroyed (release_ray_tracing: the device idle). The caller holds the lock.
void release_front_layer(Backend& state);
// The pass for the view being drawn (the listener's work; the checks call it too). The caller holds the lock. False: no
// layer lit (no front layer drawn, no light list this frame, ray tracing off).
bool record_front_layer_pass(Backend& state, const rt::ViewDrawn& view);

struct FrontLayerStats {
    uint64_t passes = 0;
    double gpu_ms = 0.0; // (the depth and the light on the device, the mean of the last report; 0: not measured)
    uint32_t width = 0;
    uint32_t height = 0;
};
[[nodiscard]] FrontLayerStats front_layer_stats();

} // namespace render::vulkan
