#pragma once

// NOT ORIGINAL - the relit mode in the world the game draws (engine/gfx/rt_relit.h; [Graphics] rt_relit): render_pc's
// side. Each new world's first frame the level's relit data is read (engine/gfx/relit_data.h - made offline by the relit
// bake, render_pc/relit_bake.h: nothing is traced or fitted at load) and the world's meshes - read from their vertex
// buffers' copies in memory (positions, the baked colours, the triangles) - matched with it by their content; a level
// without data, with data made from other meshes or whose bake has no sunlight keeps the original's lighting (new_world).
// The relit colours are written into those same buffers - the colour word of each vertex, the
// way MeshRef::apply_vertex_colors writes the game's colour animations (rt_record::ColourWrite: the ray-traced scene's
// geometry unchanged). Nothing else is drawn differently: no shader, key or pass changes; the ray-traced effects
// (shadows, occlusion, one bounce, reflections) run over the relit colours as they ran over the bake.
//
// - The bake is kept: each buffer's original colours are copied before the first write, written back when the mode
//   is turned off (the console's `rt relit off`), the level opts out, or the bake shows no sun; the baked sun's fit
//   reads them, not the relit ones (original_colours).
// - A vertex whose colour the game animates (a material's colour animation: MeshRef's per-vertex byte not 0) is left
//   to the animation.
// - Meshes shown later (a level's scripts show and hide its parts) take their light from the data when they first stand
//   in a still world (the bake took every mesh of the level's scenes apart, shown or hidden).
// - When the sun moves (rt_scene.h scene_sun: the console's `rt sun`, rt_sun_source=game or x,y,z) each mesh's sun
//   rays are traced again and its colours written - a relight. One relight at a time (the rule that keeps a moving
//   light's cost bounded): a new light is taken only once the relight toward the last one is done (every mesh traced
//   and written, the light field made); a light that moves meanwhile - the time of day's running clock - waits, and
//   the newest is taken next. The bounce (its sky rays, the hits' colours the relit ones) changes slowly as the sun
//   moves: it is traced again as a pass of its own, once the sun is 5 degrees from the one it was traced for, and the
//   colours written again when the pass is done. The work a frame is paced by time (about 1.5 ms each of the sun's
//   rays, the bounce's and the writes while the level is shown; the most - kRelitRetraceVertices... - while the
//   loading screen holds the first light), and the rays are asked of the device inside the frame and answered a frame
//   or two later (rt::RayTracing::probe_later): the frame never waits for the device.
// - The level's data: thug2-decomp-relit\<the level's scene checksum>.relit beside the exe (tools/game/relit_bake.ts).
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_relit.h"
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
    // (lighting round A) its material's first pass blends over what is under it (not additive): with black vertex colours
    // such a mesh adds no colour of its own - it is the bake's shadow drawn as geometry (College's court: the fence's
    // shadow lines), which the relit level hides (its light store's colour word says so: rt_cel.h)
    bool blended = false;
    // (lighting round A3 - the original's light cards) drawn additively, its texture still (rt_record.cpp glow_card): a
    // halo, light cone, pool or starburst the artists drew round a lamp - the lamp's evidence for the relit bake, but no
    // light: the relit level hides it (its store's flags kCelHiddenFlags - rt_cel.h kCelHiddenStream), as it hides the bake's shadows drawn as
    // geometry. A lamp shows its own surface (its emitting vertices, by its light) and the light it casts.
    bool glow = false;
};

// (the offline relit data - engine/gfx/relit_data.h) A new world's first frame (rt_record's new_world_work, whether
// the mode is on or not): the level's data read, the world's meshes matched with it, what the level is lit by decided -
// the level's facts for the settings model (config::set_level_light: relit and the time of day out of effect where the
// level keeps the original's lighting, and why) - and the baked sun the data holds (found false without data: no
// level sun). Its scene number set.
[[nodiscard]] rt::BakedSun new_world(std::span<const WorldMesh> world);
// (the relit bake - render_pc/relit_bake.h) A world mesh as the mode reads it, with its content's hash (the data's key)
// and which vertices the game animates; false: the mode can't relight it.
struct BakeMesh {
    rt::RelitMesh mesh;
    std::vector<uint8_t> animated;
    uint64_t content = 0;
};
bool read_world_mesh(const WorldMesh& world, BakeMesh* out);
// Each frame (rt_record's record_frame, after the new world's work): the last frame's world meshes, and whether the
// world has stood still long enough.
void update(std::span<const WorldMesh> world, bool stable, rt::RayTracing& tracer);
// A mesh destroyed (rt_record::mesh_released): whatever was kept for it forgotten.
void mesh_released(const MeshRef* mesh);
// A vertex buffer destroyed (rt_record::vertex_buffer_released): its entry forgotten. Entries are kept by the buffer's
// address: without this a buffer made later at the same address - the night state's scripts make and free meshes as
// they show and hide parts of the level - was taken for the old one, already written, and drawn with its bake.
void buffer_released(const VertexBufferData* buffer);
// The game wrote a buffer's contents itself (rt_record::vertex_buffer_written, not a relit write): over the relit
// colours - they are written again (the next writes, as a new lighting's).
void buffer_written(const VertexBufferData* buffer);
// (added by the camera effects) The level's main scene - the first world scene that isn't a sky -, its checksum (0:
// none): rt_grade=level's key.
[[nodiscard]] uint32_t level_scene();
// (lighting round A2 - one lamp model) The level's lamps as its relit data solved them (their intensities in the global
// unit, their shapes) when the world now loaded is relit from its data - the lights the lamps pass shines in place of
// the emitters found at load (rt_record.cpp emitter_work); the level's own point lights stay the game's. False: no data
// for this world (the found emitters stay).
bool data_emitters(std::vector<rt::Emitter>* out);
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
// (round relit-misses - every mesh drawn in a relit view lit by the light now) A world mesh of the relit level drawn
// without a light of its own - none in the level's data (a mesh the bake didn't see), one the mode can't relight
// (dynamic, no copy of its vertices or colours), or one shown since whose light isn't written yet: the colour its baked
// colours are drawn with, the light field's ratio at its centre (unlit_object_colour). False: it has its own light (its
// light store), it isn't a world mesh of a relit level now, or the light there is the bake's.
[[nodiscard]] bool unlit_world_colour(const MeshRef* mesh, std::array<uint8_t, 4>* out);
// (added by the scene's sun) Whether every mesh's colours are written for the sun the world is relit by now (its sun
// and bounce traced, the writes done): `sun` that sun, `lighting` a number for that lighting (another when anything
// that changes the colours changed). rt_record makes the shadows' agreement grid for that sun from them.
bool settled(std::array<float, 3>* sun, uint64_t* lighting);
// (added by cel shading - engine/gfx/rt_cel.h) The cel stream of a buffer the mode has relit (its words a vertex,
// vertex for vertex - the light stream the ray-traced passes read too); null: none - the draws band its vertex colour.
[[nodiscard]] GpuVertexBuffer* cel_stream(const VertexBufferData* buffer);
// (the console's `rt probe`) The relit world's vertices within `radius` of `point`: their count and mean baked and
// relit brightness, and the nearest few's parts (sun, sky, bounce, tint, residual) and colours.
[[nodiscard]] std::string describe_near(const std::array<float, 3>& point, float radius);
// (the console's `rt probe`) Whether the mesh `id` is relit: its entry's state and its vertices' mean baked and relit
// brightness, or that the mode keeps none for it.
[[nodiscard]] std::string describe_mesh(rt::MeshId id);
// (the light energy audit's census - the console's `rt census`) What the mode makes of a world mesh: a bucket's name
// ("relit: light stream", "not relit: dynamic ...", "kept bake: <why>"...), and the level's state with its model.
[[nodiscard]] std::string census_bucket(const WorldMesh& world);
[[nodiscard]] std::string census_status();
// (the one lighting path - load readiness) A level's first light: from the frame after a loading screen goes down,
// whether the frame shown should still be the loading screen's picture - while ray tracing relights the level, the
// clock may light it (rt_daylight.h tod_may_run: elsewhere the first frame is the bake, as it should be) and the
// lighting isn't ready: the clock's light published and the level's own scripts for the state it set ended (day_clock.h
// light_ready - the night geometry they show is in the world), and the level relit for it (every world mesh's light
// written for that light, the light field made) or found not to be relit (no sun, opted out, the mode off) - read from
// that state each frame, no count of frames - at most kFirstLightMostSeconds. So the first frame shown of a level loaded
// at night is already night; the frames before it are drawn (the world must stand still to be relit) but covered.
// Called once a frame by draw_world; `loading` true while the loading screen is up (the next level's hold armed).
inline constexpr double kFirstLightMostSeconds = 10.0;
[[nodiscard]] bool hold_first_light(bool loading);
// The console's `rt relit` (devtools/rt_check.cpp): the state in a line.
[[nodiscard]] std::string status_text();

} // namespace rt_relit_world
} // namespace render
