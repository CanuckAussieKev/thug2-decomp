#pragma once

#include <cstdint>
#include <vector>

#include "engine/gfx/water_bodies.h"

// NOT ORIGINAL - the level's water bodies for the physical waves (engine/gfx/water_bodies.h, water_waves.h;
// docs/design/vulkan.md "Water"): each water material's mesh noted as the level loads (its triangles - a level's meshes
// are in world space), all of them measured together against the level's collision at the first water draw after the
// meshes changed (fetch along the wind, depth, sky openness), and each mesh's draw handing the backend its body
// (engine/gfx/water_surfaces.h set_current_body) - its waves worked out again when the weather's wind changes. Nothing
// is done while no backend wants water (Direct3D 9, rt_water off).
namespace scene_format {
struct MeshBody;
} // namespace scene_format

namespace render {

struct MaterialRef;
struct IndexBufferData;

namespace water_bodies {

// The mesh body's triangles (read_mesh: the file's, before anything splits it) when it is a water material's static mesh
// (empty otherwise) - for remember.
[[nodiscard]] std::vector<water::Triangle> triangles_of(const scene_format::MeshBody& body, uint32_t fvf, const MaterialRef* material,
                                                         bool dynamic);

// The water meshes, by their first level of detail's index buffer (shared by the copies that share its buffers).
void remember(const IndexBufferData* drawn, std::vector<water::Triangle> triangles);
void copy(const IndexBufferData* from, const IndexBufferData* to);
void forget(const IndexBufferData* drawn);

// A mesh about to be drawn: the backend told its body (measured first when the level's water changed). A mesh drawn as
// an instance's (`instanced`: a moving object's - its triangles in its model's space) is taken out of the measure and
// drawn still: where it is in the level isn't known at load.
void before_draw(const IndexBufferData* first_level, bool instanced);

} // namespace water_bodies
} // namespace render
