#pragma once

// NOT ORIGINAL - the night lighting's light sources ([Graphics] rt_lights - gpu_vulkan/vulkan_rt_lights.h draws them;
// docs/design/vulkan.md "Lights at night"). API-neutral: the CPU half, which render_pc feeds and the Vulkan pass reads.
//
// A level at night (or indoors) looks flat because its lamps, signs and lit windows light nothing: the game draws them
// as bright textures and bakes their light into the vertex colours around them, with no shadows and nothing for the
// skater. Two kinds of light source are turned into real lights for the ray-traced pass:
//   - the level's own lights: the dynamic point lights of the frame record (rt::Light::Kind::kPoint - the light nodes a
//     level places, engine/gfx/scene_lights.h, and any a script makes), as the game gives them;
//   - emitters: the level's emissive surfaces found once a world from its meshes (render_pc/rt_record.cpp gathers the
//     triangles, cluster_emitters below joins them):
//       glow     - world meshes drawn additively (their first pass blend mode 1 or 2: add, add by the fixed alpha): the
//                  halos round lamps, neon tubes, light cones and pools - what the artists drew where light is. A flat
//                  patch facing up (a pool of light on the ground) is lifted by its size: the light falls from above;
//       self-lit - in a dark level only (its world's median vertex brightness at most kDarkLevelLuma), opaque triangles
//                  whose vertex colours are all far brighter than the level's (self_lit_triangle): lamp heads, lit
//                  windows, signs. A flat patch facing up is left out (that is a baked pool of light, not a lamp).
//     Triangles are joined into clusters kEmitterJoin across (one light per lamp, not per triangle; a long tube gives a
//     light every kEmitterJoin or so); each cluster is a light at its area-weighted centre, its colour the vertex and
//     pass colours' area-weighted mean (the pass multiplies in its texture's mean colour), its intensity from that
//     colour's brightness and its area, its reach from its size.
// A view lights with at most kMaxViewLights of them: select_view_lights keeps those whose reach touches the view's
// frustum, the most important first (intensity x how much of the reach the camera is inside of).
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "engine/gfx/rt_scene.h"

namespace render::rt {

enum class EmitterKind : uint8_t { kGlow = 0, kSelfLit = 1, kPoint = 2 };

// A triangle of an emissive surface (world space), its colour (the vertices' x the pass's, 1 = the game's white; mean
// of the corners) and its material.
struct EmitterTriangle {
    std::array<std::array<float, 3>, 3> corners{};
    std::array<float, 3> colour{};
    MaterialId material = 0;
    EmitterKind kind = EmitterKind::kGlow;
};

// A light the pass shines (an emitter found at load, or the frame's point light).
struct Emitter {
    std::array<float, 3> position{};
    float size = 0.0F;    // its radius (world units): a shadow ray stops this short of it, its falloff's core
    std::array<float, 3> colour{}; // hue, the brightest channel 1
    float intensity = 0.0F;        // x colour: the light at the core's edge, face-on
    float reach = 0.0F;            // where its light ends (a smooth window to 0)
    MaterialId material = 0;       // whose texture's mean tints it (0: none)
    EmitterKind kind = EmitterKind::kGlow;
};

inline constexpr float kEmitterJoin = 96.0F;        // a triangle joins a cluster whose first is this near (world units)
inline constexpr float kEmitterReachPerSize = 12.0F; // reach = size x this, within:
inline constexpr float kEmitterMinReach = 400.0F;
inline constexpr float kEmitterMaxReach = 1500.0F;
inline constexpr float kEmitterMinSize = 4.0F;
inline constexpr float kEmitterUnitSide = 32.0F; // a cluster this many units square has intensity x 1 (its colour's)
inline constexpr float kEmitterMinArea = 0.25F;  // ... the intensity's area factor kept within these
inline constexpr float kEmitterMaxArea = 2.0F;
inline constexpr float kFlatUpShare = 0.85F;     // a cluster facing up this much (area-weighted |n.y|) ...
inline constexpr float kFlatHeightShare = 0.25F; // ... no taller than this share of its width is flat
inline constexpr std::size_t kMaxEmitters = 4096;
// Self-lit: a level is dark when its world's median vertex brightness (luma of the bytes, 128 the game's white) is at
// most kDarkLevelLuma; a triangle there is self-lit when its dimmest corner is at least kSelfLitLuma and
// kSelfLitOverLevel times the level's median.
inline constexpr float kDarkLevelLuma = 64.0F;
inline constexpr float kSelfLitLuma = 112.0F;
inline constexpr float kSelfLitOverLevel = 2.5F;
// A light's falloff core (light_falloff): its size, and at least this share of its reach.
inline constexpr float kCoreShare = 0.25F;
inline constexpr std::size_t kMaxViewLights = 256;

[[nodiscard]] constexpr bool dark_level(float median_luma) { return median_luma <= kDarkLevelLuma; }
[[nodiscard]] constexpr bool self_lit_triangle(float dimmest_corner_luma, float median_luma)
{
    return dark_level(median_luma) && dimmest_corner_luma >= kSelfLitLuma &&
           dimmest_corner_luma >= kSelfLitOverLevel * median_luma;
}

// The triangles joined into lights (the same triangles in the same order give the same lights): at most kMaxEmitters,
// the strongest (intensity x reach) first.
[[nodiscard]] std::vector<Emitter> cluster_emitters(std::span<const EmitterTriangle> triangles);

// The emitters of the world now loaded (render_pc sets them in a new world's first frame while emitters_wanted()).
struct LevelEmitters {
    uint64_t serial = 0; // a new value for each world's set (0: none yet)
    std::vector<Emitter> emitters;
    std::size_t glow_triangles = 0;
    std::size_t self_lit_triangles = 0;
    float median_luma = 0.0F;
    bool dark = false;
};
void set_level_emitters(LevelEmitters emitters);
[[nodiscard]] std::shared_ptr<const LevelEmitters> level_emitters();
void set_emitters_wanted(bool wanted); // (the pass, when on)
[[nodiscard]] bool emitters_wanted();

// A view's lights: the frame record's point lights and the level's emitters whose reach touches the frustum of
// `clip_from_world` (D3D's row-vector view x projection), the most important `max` of them (at most kMaxViewLights).
struct ViewLightRequest {
    std::array<float, 3> camera{};
    std::array<std::array<float, 4>, 4> view_projection{};
    std::size_t max = 64;
    bool points = true;          // the frame record's point lights
    bool emitters = true;        // the level's emitters
    float emitter_scale = 1.0F;  // x their intensity
    float point_scale = 1.0F;    // x the point lights'
};
[[nodiscard]] std::vector<Emitter> select_view_lights(const FrameRecord& record, const LevelEmitters* level,
                                                      const ViewLightRequest& request);
// A light's importance to a view from `camera` (select_view_lights' order).
[[nodiscard]] float light_importance(const Emitter& light, const std::array<float, 3>& camera);
// Whether a sphere touches the frustum (the planes of a row-vector view x projection, D3D's 0 - w depth).
[[nodiscard]] bool sphere_in_frustum(const std::array<std::array<float, 4>, 4>& view_projection,
                                     const std::array<float, 3>& centre, float radius);

// The light's falloff at `distance` (the pass's - vulkan_rt_lights.cpp's GLSL is the same): core^2 / (core^2 + d^2) - an
// inverse square softened by the core (max(size, kCoreShare x reach)): 1 at the light, 1/2 at the core's edge -, times
// (1 - (d / reach)^4)^2, a smooth window to 0 at `reach`.
[[nodiscard]] float light_falloff(float distance, float size, float reach);

} // namespace render::rt
