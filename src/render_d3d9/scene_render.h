#pragma once

#include <cstdint>

#include "render_d3d9/mesh_list.h"

// Drawing a scene's meshes (Nx engine, PC).
namespace render {

// Render flags (draw_scene's `flags`).
constexpr int kRenderOpaque = 0x1;      // the meshes before the first transparent one
constexpr int kRenderTransparent = 0x2; // the rest: transparent, then the flagged ones sorted by depth, then the others
constexpr int kRenderOcclusion = 0x4;   // skip meshes hidden by an occluder
constexpr int kRenderNoCulling = 0x8;   // no view / viewport tests
constexpr int kRenderNearFirst = 0x10;  // opaque meshes with the depth value up to 600 first, the others after
constexpr int kDrawStencilOnly = 0x20;  // only the stencil shadow volumes
constexpr int kRenderStats = 0x40;      // count the pass (0x00775B80)

// Which of a scene's meshes are drawn (the pass flags) and into which viewport.
struct ScenePass {
    uint32_t flags{};
    int viewport{};
};

// 0x004DC700: draw a scene's meshes for viewport `viewport` (a mesh's +0x1B holds a bit per viewport it shows in),
// changing material only between meshes of different materials; shadows over what was drawn when the scene has them
// (flag 0x100).
void draw_scene(SceneRef* scene, ScenePass scene_pass);
// 0x004DA690 (qsort's comparison of the sorted list's entries): by the depth value sphere_in_view left
// (0x007CA978), ascending.
int compare_distances(const void* a, const void* b);

} // namespace render
