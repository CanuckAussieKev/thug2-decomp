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

#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"

namespace render {

struct MeshRef;
struct VertexBufferData;

namespace rt_relit_world {

inline constexpr uint32_t kRelitRetraceVertices = 150000;
inline constexpr uint32_t kRelitBounceVertices = 40000;
// (added by the time of day - engine/gfx/rt_daylight.h) While the clock lights the level, the colours written at most
// this many vertices a frame (a new time spread over frames).
inline constexpr uint32_t kRelitWriteVertices = 120000;

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
// (added by the camera effects) The level's main scene - the first world scene that isn't a sky -, its checksum (0:
// none): rt_grade=level's key.
[[nodiscard]] uint32_t level_scene();
// The baked colours of a buffer the mode has written into (A8R8G8B8, a word a vertex), or empty: it holds its own.
[[nodiscard]] std::span<const uint32_t> original_colours(const VertexBufferData* buffer);
// How the skater's and the objects' lights follow the relit world (pc_lights.cpp): while the world is relit by a sun
// other than the bake's, a coloured one, or the clock's light, the game's directional lights turned by the rotation that
// takes the bake's sun to the relit one (rows: x' = row . x) and the first (the key light) times the direct light now
// over the bake's (the sun's colour; the lighting state's light_colour - the moon's at night); the others and the
// ambient take the light field's indirect ratio where the thing is (pc_lights.cpp). False: as they are (the mode off, or
// relit by the bake's own sun and colour - the game's lights then as the original's, beside a world that looks as the
// original's).
struct ObjectLighting {
    std::array<std::array<float, 3>, 3> rotation{};
    std::array<float, 3> key_colour{1.0F, 1.0F, 1.0F};
};
[[nodiscard]] bool object_lighting(ObjectLighting* out);
// (the one lighting path) The colour an unlit object at `position` - its own baked vertex colours, which the mode doesn't
// relight (the tables, chairs and props drawn as instances without the game's lights) - is drawn with: the level's
// light there now over the bake's (engine/gfx/rt_light_field.h light_ratio_at, whole) as the instance colour's bytes
// (128 = 1). False: none (the level not relit, no field yet, or the light there the bake's) - drawn as the original.
[[nodiscard]] bool unlit_object_colour(const std::array<float, 3>& position, std::array<uint8_t, 4>* out);
// (added by the scene's sun) Whether every mesh's colours are written for the sun the world is relit by now (its sun
// and bounce traced, the writes done): `sun` that sun, `lighting` a number for that lighting (another when anything
// that changes the colours changed). rt_record makes the shadows' agreement grid for that sun from them.
bool settled(std::array<float, 3>* sun, uint64_t* lighting);
// (added by cel shading - engine/gfx/rt_cel.h) The cel stream of a buffer the mode has relit, while rt_cel is on (its
// three words a vertex, vertex for vertex); null: none - the draws band its vertex colour.
[[nodiscard]] GpuVertexBuffer* cel_stream(const VertexBufferData* buffer);
// (the console's `rt probe`) The relit world's vertices within `radius` of `point`: their count and mean baked and
// relit brightness, and the nearest few's parts (sun, sky, bounce, tint, residual) and colours.
[[nodiscard]] std::string describe_near(const std::array<float, 3>& point, float radius);
// (the one lighting path - load readiness) A level's first light: from the frame after a loading screen goes down,
// whether the frame shown should still be the loading screen's picture - while ray tracing relights the level, the
// clock may light it (rt_daylight.h tod_may_run: elsewhere the first frame is the bake, as it should be) and the
// lighting isn't ready: the clock's light published (day_clock.h light_ready), the level relit for it (every mesh's
// colours written, the light field made) or found not to be relit (no sun, opted out, the mode off) - at most
// kFirstLightMostSeconds. So the first frame shown of a level loaded at night is already night; the frames before it
// are drawn (the world must stand still to be relit) but covered. Called once a frame by draw_world; `loading` true
// while the loading screen is up (the next level's hold armed).
inline constexpr double kFirstLightMostSeconds = 10.0;
[[nodiscard]] bool hold_first_light(bool loading);
// The console's `rt relit` (devtools/rt_check.cpp): the state in a line.
[[nodiscard]] std::string status_text();

} // namespace rt_relit_world
} // namespace render
