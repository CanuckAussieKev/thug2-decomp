#pragma once

#include <cstdint>
#include <vector>

// NOT ORIGINAL - the water's moving surface at load ([Graphics] rt_water_geometry with rt_water; engine/gfx/water_tess.h,
// docs/design/vulkan.md "Moving water"): a water material's flat mesh (water_materials.h is_water) split into a grid as
// its body is read, before its buffers are made - more vertices after its own, a new first level of detail - and
// remembered, so that its draws (MeshRef::render, first level of detail only) tell the backend their vertices may be
// moved by the swell (engine/gfx/water_surfaces.h set_current_waves). The ray tracing's copy of the mesh is the same flat
// grid. Off (rt_water or rt_water_geometry off as the level loads, Direct3D 9): nothing changes.
namespace scene_format {
struct MeshBody;
} // namespace scene_format

namespace render {

struct MaterialRef;
struct IndexBufferData;

namespace water_geometry {

// Whether a level loading now has its water split (a backend wants it: water::geometry_wanted).
[[nodiscard]] bool on();

// The mesh body (read_mesh: the file's, before its buffers are made) split when it is a water material's flat mesh: a
// vertex format of a position, an optional normal and colour and texture coordinate pairs; not skinned, no per-vertex
// colour animation, no bone-scaled positions, not dynamic. Returns the first level of detail's strip as the file had it
// when it was split (empty: left as it was) - for mesh_smoothing::remember_original (the baked sun's samples).
[[nodiscard]] std::vector<uint16_t> split_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic);

// The split meshes, by their first level of detail's index buffer (shared by the copies that share its buffers).
void remember(const IndexBufferData* drawn);
void copy(const IndexBufferData* from, const IndexBufferData* to);
void forget(const IndexBufferData* drawn);
[[nodiscard]] bool split(const IndexBufferData* drawn);

// A mesh about to be drawn (after its material is handed in) - its first level of detail's index buffer and the level
// drawn: the backend told whether its vertices may be moved; then, the draw done, that none may be any more.
void before_draw(const IndexBufferData* first_level, int level);
void after_draw();

// The meshes split since the last call logged (one line; nothing when none were) and the totals cleared.
void log_totals(const char* scene);

} // namespace water_geometry
} // namespace render
