#pragma once

// NOT ORIGINAL - the relit mode in the world the game draws (engine/gfx/rt_relit.h; [Graphics] rt_relit): render_pc's
// side. Once a level's world has stood still and its baked sun is fitted (rt_record.cpp, the same moment), its world
// meshes are read from their vertex buffers' copies in memory (positions, the baked colours, the triangles), traced and
// taken apart (rt_relit.h), and the relit colours written into those same buffers - the colour word of each vertex, the
// way MeshRef::apply_vertex_colors writes the game's colour animations (rt_record::ColourWrite: the ray-traced scene's
// geometry unchanged). Nothing else is drawn differently: no shader, key or pass changes; the ray-traced effects
// (shadows, occlusion, one bounce, reflections) run over the relit colours as they ran over the bake.
//
// - The bake is kept: each buffer's original colours are copied before the first write, written back when the mode
//   is turned off (the console's `rt relit off`), the level opts out, or the bake shows no sun; the baked sun's fit
//   reads them, not the relit ones (original_colours).
// - A vertex whose colour the game animates (a material's colour animation: MeshRef's per-vertex byte not 0) is left
//   to the animation.
// - Meshes shown later (a level's scripts show and hide its parts) are taken apart when they first stand in a still
//   world, with the level's model as it is.
// - When the sun moves (rt_scene.h scene_sun: the console's `rt sun`, rt_sun_source=game or x,y,z) each mesh's sun
//   rays are traced again -
//   at most kRelitRetraceVertices vertices a frame, the meshes rewritten as they are done -, then its sky rays, for
//   the bounce of the new light (the hits' colours the relit ones; kRelitBounceVertices vertices a frame).
// - The analysis is kept on disk: thug2-decomp-relit\<the level's scene checksum>.bin beside the exe, each mesh by a
//   hash of its vertices and indices (and the settings that change the results): a mesh found there isn't traced
//   again, so a level's second load costs only the fit of its sun (rt_sun.h) and the writes.
#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "engine/gfx/rt_scene.h"

namespace render {

struct MeshRef;
struct VertexBufferData;

namespace rt_relit_world {

inline constexpr uint32_t kRelitRetraceVertices = 150000;
inline constexpr uint32_t kRelitBounceVertices = 40000;

// A world mesh as rt_record knows it: its id in the RayTracing, its geometry and the buffer it was drawn from.
struct WorldMesh {
    const MeshRef* mesh = nullptr;
    rt::MeshId id = 0;
    const rt::Geometry* geometry = nullptr;
    const VertexBufferData* source = nullptr;
};

// Each frame (rt_record's record_frame, after the baked sun's fit): the last frame's world meshes, and whether the
// world has stood still long enough.
void update(std::span<const WorldMesh> world, bool stable, rt::RayTracing& tracer);
// A mesh destroyed (rt_record::mesh_released): whatever was kept for it forgotten.
void mesh_released(const MeshRef* mesh);
// The baked colours of a buffer the mode has written into (A8R8G8B8, a word a vertex), or empty: it holds its own.
[[nodiscard]] std::span<const uint32_t> original_colours(const VertexBufferData* buffer);
// How the skater's and the objects' lights follow the relit world (pc_lights.cpp): while the world is relit by a sun
// other than the bake's, or a coloured one, the game's directional lights turned by the rotation that takes the bake's
// sun to the relit one (rows: x' = row . x) and the first (the key light) tinted by the sun's colour. False: as they are
// (the mode off, or relit by the bake's own sun and colour - the game's lights then as the original's, beside a world
// that looks as the original's).
struct ObjectLighting {
    std::array<std::array<float, 3>, 3> rotation{};
    std::array<float, 3> key_colour{1.0F, 1.0F, 1.0F};
};
[[nodiscard]] bool object_lighting(ObjectLighting* out);
// (added by the scene's sun) Whether every mesh's colours are written for the sun the world is relit by now (its sun
// and bounce traced, the writes done): `sun` that sun, `lighting` a number for that lighting (another when anything
// that changes the colours changed). rt_record makes the shadows' agreement grid for that sun from them.
bool settled(std::array<float, 3>* sun, uint64_t* lighting);
// The console's `rt relit` (devtools/rt_check.cpp): the state in a line.
[[nodiscard]] std::string status_text();

} // namespace rt_relit_world
} // namespace render
