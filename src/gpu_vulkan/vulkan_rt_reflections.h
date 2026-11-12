#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_rt_scene.h"

// NOT ORIGINAL - ray-traced reflections (docs/design/vulkan.md, R5's reflections; [Graphics] rt_reflections - gpu_vulkan.h's
// RtReflectionSettings). The game marks its shiny surfaces with an environment-mapped material pass (a picture of the sky
// sphere-mapped onto the surface); render_pc's frame record gives each material that pass's strength as its
// reflectivity (rt::Material::reflectivity). After a viewport's opaque world is drawn (rt::opaque_view_drawn, after R4's
// shadows and R5's ambient occlusion), one compute pass and one full-screen pass over the colour target:
//   1. trace (8 x 8 pixels a group): each pixel's world position rebuilt from the depth (sample 0 of a multisampled
//      one), then a ray from the camera through it finds which surface the pixel shows - its instance, triangle and
//      material (kept only where it ends within a small tolerance of the depth: else something the TLAS doesn't have,
//      or has a frame late, is in front). Where that material is reflective: the triangle's own normal (its vertices
//      read through the frame's geometry table, vulkan_rt_scene.h), one ray along the mirror direction (no roughness: a
//      perfect mirror is one deterministic ray, so there is no noise to denoise - R5's denoiser isn't used), and what it
//      hits shaded: its material's texture (R3's table) at the hit's texture coordinates, at a level of detail from the
//      ray's footprint (the pixel's spread over both legs of the path), times its vertex colour doubled as the game's
//      pixel shaders do (the level's lighting is baked into the vertex colours), or for objects the frame's ambient and
//      directional lights; alpha-tested surfaces tested along the ray (R3's alpha test). No recursion, no fog. A ray
//      that hits nothing leaves the pixel alone (the game's own environment map already shows the sky there);
//   2. apply: colour = mix(colour, reflection, reflectivity x strength) where something was hit (every sample of a
//      multisampled pixel the pixel's value; alpha kept); the other pixels aren't touched.
// - Which pixels: a ray per pixel to identify the surface rather than a per-pixel material target (the raster draws
//   would all need another attachment, and their pipelines a second output): one ray query against the TLAS costs
//   little next to the reflection rays, and the pass does nothing at all while the frame has no reflective instance
//   (SceneView::reflective_instances).
// - The instance mask: 0x01 opaque world, 0x02 objects and 0x04 alpha-tested are seen and reflected; transparent
//   instances (0x80) are not (their pixels' depth is what is behind them, and glass isn't drawn into the depth).
// - The hit's normal is the triangle's (flat): the game's meshes have no normals for most of the world.
namespace render::vulkan {

struct Backend;

inline constexpr uint32_t kReflectionMask = 0x01 | 0x02 | 0x04; // (world, objects, alpha-tested)
inline constexpr float kReflectionDistance = 20000.0F;         // how far a reflection ray looks (world units)

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT when
// rt_reflections is on, ray tracing is enabled and the format can be sampled, else none (make_depth_surface).
[[nodiscard]] VkImageUsageFlags rt_reflections_depth_usage(const Backend& state, VkFormat format);

// The passes over the device's bound targets for a viewport (the listener's work, also called by the check program).
// The caller holds the lock. Returns whether they were recorded (false: skipped - off, no scene, nothing reflective,
// the targets can't be read; the reason logged once).
bool record_rt_reflection_pass(Backend& state, const rt::ViewDrawn& view);

// The passes recorded, and the views skipped because the frame had nothing reflective (for the check and the log).
[[nodiscard]] uint64_t rt_reflection_passes();
[[nodiscard]] uint64_t rt_reflection_views_without_mirrors();

// For the check: step 1's image of the viewport last drawn with `rect` read back (the frame's work finished first):
// width x height texels of rgb the reflection, a its weight (0: none).
struct RtReflectionTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_rt_reflection_image(Backend& state, const std::array<uint32_t, 4>& rect, RtReflectionTexels* out);

// For the check: rays traced now through the current TLAS (every instance taken as opaque, the frame's work finished
// first) and what each hit read through the geometry table - the attributes interpolated with the hit's barycentrics,
// the position from the triangle's vertices taken to the world. Takes the lock.
struct SurfaceRay {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};
    float t_max = 0.0F;
    uint32_t cull_mask = kMaskEverything;
};
struct SurfaceHit {
    bool hit = false;
    bool live = false; // the geometry table had the instance (GeometryRecord kGeometryLive)
    float t = 0.0F;
    uint32_t instance = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    uint32_t geometry_flags = 0; // the instance's GeometryRecord::flags
    std::array<float, 2> barycentrics{};
    std::array<float, 2> uv{};
    std::array<float, 4> colour{}; // 0 - 1 (A8R8G8B8's bytes / 255)
    std::array<float, 3> position{}; // world
};
TraceResult trace_surfaces_now(std::span<const SurfaceRay> rays, std::vector<SurfaceHit>* hits);

// The passes' objects and images released (the device's shutdown; the caller holds the lock).
void release_rt_reflections(Backend& state);

} // namespace render::vulkan
