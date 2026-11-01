#pragma once

#include <cstdint>

#include "engine/gfx/mesh_list.h"

// Drawing a scene's meshes (Nx engine, PC).
namespace render {

// Render flags (draw_scene's `flags`).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): bit flags, or-ed together
enum DrawOptionBits : uint8_t {
    kRenderOpaque = 0x1,      // the meshes before the first transparent one
    kRenderTransparent = 0x2, // the rest: transparent, then the flagged ones sorted by depth, then the others
    kRenderOcclusion = 0x4,   // skip meshes hidden by an occluder
    kRenderNoCulling = 0x8,   // no view / viewport tests
    kRenderNearFirst = 0x10,  // opaque meshes with the depth value up to 600 first, the others after
    kDrawStencilOnly = 0x20,  // only the stencil shadow volumes
    kRenderStats = 0x40,      // count the pass (0x00775B80)
};

// 0x004DC700: draw a scene's meshes for viewport `viewport` (a mesh's +0x1B holds a bit per viewport it shows in),
// changing material only between meshes of different materials; shadows over what was drawn when the scene has them
// (flag 0x100).
void draw_scene(SceneRef* scene, uint32_t flags, int viewport);
// 0x004DA690 (qsort's comparison of the sorted list's entries): by the depth value sphere_in_view left
// (0x007CA978), ascending.
int compare_distances(const void* a, const void* b);

} // namespace render
