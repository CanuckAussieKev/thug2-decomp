#pragma once

#include <array>
#include <cstdint>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"

// NOT ORIGINAL - round V: the world lit once per surface (docs/design/vulkan.md "Round V: a prepass and the light per
// surface"). Before, the world was drawn by the original's forward renderer with its light worked out in its shading,
// and the ray-traced shadows, the lamps and the occlusion were full-screen passes multiplying and adding onto finished
// colours: what was drawn after them (a road drawn with a blended first pass, in a scene's later group) lost them, and
// glass, water and glows drawn before them took them. Now, while a view of a relit level is drawn with ray tracing:
//   1. The prepass: the view's world and objects (render_pc's world passes - rt::world_surfaces_drawing; not the sky,
//      not the particles or the weather after them) are kept, in the game's order, each prepared once as its shading
//      will draw it (vulkan_prepared_draw.h), and each that writes depth is emitted at once from that preparation into
//      the depth alone (its pipeline's fragment half FragmentKey::light kLightDepthOnly: its alpha test, nothing else; a
//      blended one keeps only its pixels at least half opaque - kLightDepthHalf - so its translucent edges hide nothing
//      drawn behind; its vertex half the shaded draw's own, so both put every vertex at the same depth).
//      Water render_pc marks is left to rt_water (kept back until the opaque world is drawn, as before).
//   2. At opaque_drawn (ViewOrder::kSurfaceInputs, before the shadows) the view's light inputs are begun from that depth -
//      images of the target's size: the depth (R32F), the visibility (RGBA16F: the sun's visibility - -1 not
//      traced -, the direct light's share, the indirect light's visibility, 1 where made), the lamps' light and the
//      lamps' light on the traced albedo (RGBA16F), the decals (RGBA16F, 1: none) - and the passes write into them instead
//      of over the colour: the sun shadows (vulkan_rt_shadows.h) the visibility and the share, the occlusion
//      (vulkan_rt_effects.h) the indirect light's visibility, the lamps (vulkan_rt_lights.h) their light, the decals
//      (vulkan_scatter.h) their marks - a material's edit: the surface's albedo x them, lit with it.
//   3. At ViewOrder::kSurfaceShading the kept draws are drawn, in their order, with the inputs bound
//      (FragmentKey::light kLightInputs; ff_shaders.cpp kLightInputsGlsl): a fragment that is the surface the inputs were
//      made for (its depth the prepass's) is lit by the light model with them - the sun's visibility in place of its
//      vertex's, the occlusion on its indirect light alone, the lamps' light on its albedo -; a colour lit already (an
//      unlit prop, an animated vertex: kLightShare - only on a draw that doesn't add or multiply) takes them as shares
//      of its light. Every other fragment - glass, a glow, a particle in front - keeps its own light: no pass multiplies
//      it. The relit world and lit objects are lit per pixel there whether or not rt_pbr is on (the inputs need the
//      light model's terms apart). The prepared draws are emitted as prepared - none is translated twice.
// Without ray tracing, relit data, on a multisampled target, with cel shading, or outside a view, nothing changes: the
// draws are made as they come and the passes draw over the colour as before (the original's look keeps its path).
namespace render::vulkan {

struct Backend;
struct DrawCall;
struct Image;

// What a draw is prepared as now (prepare_draw asks): as it comes, or a kept draw shaded with the inputs (its depth
// emitted from the same preparation with a depth-only pipeline - vulkan_prepared_draw.h).
enum class SurfaceDrawRole : uint8_t { kAsItComes, kShaded };

// The view's light inputs while its passes make them (from ViewOrder::kSurfaceInputs to kSurfaceShading): the images
// a pass draws into in place of the colour (RGBA16F, the colour target's size; each COLOR_ATTACHMENT-usable), and the
// prepass's depth copied (R32F).
struct SurfaceInputs {
    Image* visibility = nullptr;
    Image* lamps = nullptr;
    Image* lamps_on = nullptr;
    Image* depth = nullptr;
    Image* decals = nullptr; // (RGBA16F, 1 where none: the decals' pass multiplies their marks in - the albedo's factor)
    Image* guides = nullptr; // (RGBA32UI storage, 0 where none: Ray Reconstruction's guides, written by the surfaces)
    // (path tracing, stage 5 - vulkan_restir_gi.h) the environment's specular light over the surface's specular albedo
    // (RGBA16F, pre-exposed; a 1 where made, 0 where none): the shading takes E x it in place of its own estimate
    Image* specular = nullptr;
};

// The pushed descriptors of a draw that reads the inputs (kLightVisibilityBinding - kLightSpecularBinding).
struct SurfaceInputDescriptors {
    bool on = false;
    std::array<VkDescriptorImageInfo, 7> images{};
    bool guides = false; // (the last, a storage image: pushed with kLightGuides)
};

// The listeners (vulkan_gpu.cpp: added while ray tracing is enabled).
void install_surface_light();
void uninstall_surface_light();
// Its images and pipeline destroyed (release_ray_tracing: the device idle). The caller holds the lock.
void release_surface_light(Backend& state);

// record_draw's: whether the draw was kept for the view's shading (its prepass drawn first when it writes depth) - the
// caller then draws nothing. The caller holds the lock.
[[nodiscard]] bool surface_light_keep(Backend& state, const GpuStateBlock& block, const DrawCall& call);
// The role of the draw record_draw is making now.
[[nodiscard]] SurfaceDrawRole surface_draw_role();
// A kept draw's key, its prepass's and its shaded draw's alike (one vertex shader for both): the relit world's and the
// lit objects' draws lit per pixel. `relit`: the draw has a light store (vulkan_cel_draw.h relit_colour_stream). Called
// before record_draw adds the store's bits (kSurfaceRelit to a draw lit per pixel).
void surface_light_force(const GpuStateBlock& block, bool relit, ShaderKey* shaders);
// Then: a shaded draw's light bits and the inputs to push (`out`).
void surface_light_finish(Backend& state, const GpuStateBlock& block, ShaderKey* shaders, SurfaceInputDescriptors* out);
// The view's inputs while its passes make them; null: none - a pass draws over the colour as before.
[[nodiscard]] const SurfaceInputs* surface_inputs(Backend& state);
// Whether this view is lit per surface (from its first kept draw to its shading) - Ray Reconstruction's albedo is then
// taken after the shading (vulkan_upscale.h).
[[nodiscard]] bool surface_light_view(const Backend& state);
// A vertex or index buffer's memory about to be written again while draws kept for the view's shading read it (the
// game's skinned meshes are written into the same buffer part after part): its content kept for them first - copied on
// the device into a buffer of the view's (`commands`: the stream, where the write's copy follows; null: the write is
// the processor's into mapped memory `mapped`, copied now) and the kept draws pointed at the copy.
void surface_light_buffer_rewritten(Backend& state, VkBuffer buffer, VkDeviceSize bytes, VkCommandBuffer commands,
                                    const void* mapped);
// The view's Ray Reconstruction guides as its surfaces wrote them (RGBA32UI: x the albedo, y the specular albedo -
// RGBA8 each, display-encoded, their alpha the roughness and 1 -, z, w the camera-space normal as halves; 0 where none
// wrote) - for the upscaler's preparation (vulkan_upscale.h); null when this frame's view wrote none.
[[nodiscard]] Image* surface_light_guides(Backend& state);
// (the checks) The guides written without Ray Reconstruction denoising. The caller holds the lock.
void set_surface_light_guides_forced(bool forced);
// (round RG) The view's transparency layer for Ray Reconstruction (vulkan_upscale.h; NVIDIA's DLSS-RR Integration Guide
// 3.4.10): what its kept draws that are transparencies (blended without writing depth: haze cards, light shafts; or
// adding light: glows, light cones, flares) laid over its surfaces, drawn into it instead of the colour while Ray
// Reconstruction denoises the view - the colour target's format and size, premultiplied light x the frame's
// pre-exposure (as the colour) and in alpha how much of what is behind they hide; null when this frame's view drew
// none. The caller holds the lock.
[[nodiscard]] Image* surface_light_layer(Backend& state);
// (the checks) The layer drawn without Ray Reconstruction denoising. The caller holds the lock.
void set_surface_light_layer_forced(bool forced);
// (round RG) Whether the draws being recorded are the layer's (the pipeline key's alpha blend - PipelineKey::layer).
[[nodiscard]] bool surface_light_in_layer();
// A buffer destroyed: kept draws that name it forget it (vulkan_cel_draw.h cel_forget_buffer_locked's twin).
void surface_light_forget_buffer(const void* buffer);
// A texture's image destroyed: prepared draws that sample it no longer look after its layout (its view lives to the
// frame's end, retired).
void surface_light_forget_image(const Image* image);

// The counts (the log, the checks): the views lit per surface, the draws kept, their prepasses, the draws shaded.
struct SurfaceLightStats {
    uint64_t views = 0;
    uint64_t kept = 0;
    uint64_t prepassed = 0;
    uint64_t shaded = 0;
    uint64_t recorded = 0;  // of them recorded again from their state blocks (rt_water's; a preparation that no longer held)
    uint64_t elsewhere = 0; // world draws in a view lit per surface into another target (drawn as they came)
    uint64_t rewritten = 0; // kept draws' buffers kept before they were written again
    uint64_t layered = 0;   // (round RG) of the shaded, drawn into Ray Reconstruction's transparency layer
};
[[nodiscard]] SurfaceLightStats surface_light_stats();

} // namespace render::vulkan
