#pragma once

// NOT ORIGINAL - the sky scene's scenery under the renderer's own sky (engine/gfx/sky_parts.h is the rule;
// docs/design/vulkan.md "The sky scene's parts"). While the backend draws the sky (rt_scene.h SkyReplacement), each sky
// scene's meshes are read once - their vertices and their first texture's coverage from the copies in memory - and
// told apart: its painted sky, glows and clouds are the backend's sky's to draw and are not drawn; its scenery
// (mountains, a skyline, a tree line) is drawn over that sky in the game's order, lit as the world is (its colour the
// time of day's, engine/gfx/day_clock.cpp). The original's sky (no replacement) draws every mesh as before.
#include <cstdint>

namespace render {

struct SceneRef;
struct MeshRef;

namespace sky_scenery {

// The scene's parts worked out if they aren't (or its meshes changed), and it made the one whose scenery is_scenery
// answers for. False when nothing of it is scenery.
bool prepare(const SceneRef* scene, uint32_t checksum);
// Whether a mesh is scenery of the scene prepared last (draw_scene's ScenePass::keep for the sky's pass).
[[nodiscard]] bool is_scenery(const MeshRef* mesh);

} // namespace sky_scenery
} // namespace render
