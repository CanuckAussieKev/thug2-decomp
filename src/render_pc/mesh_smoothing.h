#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/gfx/facade_relief.h"
#include "engine/gfx/smooth_tess.h"

// NOT ORIGINAL - smoothed curved surfaces at load ([Graphics] rt_tessellation; engine/gfx/smooth_tess.h, docs/design/
// vulkan.md "Smoothed curves"): a scene mesh's body is given its curved triangles split - more vertices after its own,
// a new first level of detail - before its buffers are made, so whatever draws it or traces it sees the same smoothed
// geometry. Off (the default): nothing changes.
namespace scene_format {
struct MeshBody;
} // namespace scene_format

namespace render {

struct MaterialRef;
struct IndexBufferData;

namespace mesh_smoothing {

// The vertex layout a format and the body's offsets describe (a position, an optional normal and colour, texture
// coordinate pairs - in that order, nothing else), or false. (Also the water's grid's: water_geometry.h.)
[[nodiscard]] bool layout_of(uint32_t fvf, const scene_format::MeshBody& body, tess::VertexLayout* out);

// [Graphics] rt_tessellation=1 (read once).
[[nodiscard]] bool on();

// The mesh body (read_mesh: the file's, before the buffers are made) smoothed when it can be: a vertex format of a
// position, an optional normal and colour and texture coordinate pairs; not skinned, no per-vertex colour animation,
// no bone-scaled positions, not dynamic; its material neither transparent nor cut out.
// Returns the first level of detail's strip as the file had it when the body was smoothed (empty: left as it was), for
// remember_original; `made` (when given) gets each new vertex's sources among the original ones (remember_made).
[[nodiscard]] std::vector<uint16_t> smooth_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material,
                                                bool dynamic, std::vector<relief::VertexSource>* made = nullptr);

// A smoothed mesh's original strip, by its first level of detail's index buffer (shared by the copies that share its
// buffers; copied with those that copy them): what the baked sun's samples read (rt_record), so the level's sun and the
// bake's agreement are found on the original surfaces, as without smoothing (the original vertices are still first).
// Empty: not smoothed. Forgotten when the buffer is destroyed.
void remember_original(const IndexBufferData* drawn, std::vector<uint16_t> original);
[[nodiscard]] std::span<const uint16_t> original_strip(const IndexBufferData* drawn);
void copy_original(const IndexBufferData* from, const IndexBufferData* to);
void forget_original(const IndexBufferData* drawn);

// (added by building relief) The made vertices' sources of a relieved mesh (the last `sources.size()` of its
// `vertex_count`), by its first level of detail's index buffer, kept, copied and forgotten with its original strip: what
// the relit light's analysis blends their light from (rt_relit_world.cpp, rt_relit.h RelitMesh::sources). Empty: none.
void remember_sources(const IndexBufferData* drawn, uint32_t vertex_count, std::vector<relief::VertexSource> sources);
[[nodiscard]] std::span<const relief::VertexSource> sources_of(const IndexBufferData* drawn);

// (round LV - the relit data whatever the geometry settings) A mesh the load's geometry passes changed (smoothing,
// building relief): how many of its vertices are the file's own (the first) and, for each vertex after them, the
// earlier vertices it was made from (smoothing's: original ones; relief's: those of the mesh it relieved) - every made
// vertex, or none. By its first level of detail's index buffer, kept, copied and forgotten with its original strip.
// What lets a level's relit data made with other geometry settings be used: the file's vertices are the same, the made
// ones take their sources' (rt_relit_world.cpp).
struct MadeVertices {
    uint32_t original = 0; // the file's own vertices (0: the mesh is the file's)
    std::span<const relief::VertexSource> made;
};
void remember_made(const IndexBufferData* drawn, uint32_t original, std::vector<relief::VertexSource> made);
[[nodiscard]] MadeVertices made_of(const IndexBufferData* drawn);

// The heights a material's mesh is displaced by ([Graphics] rt_displacement above 0): the lookup fills `out` (its lookup
// and context; the scale is the setting's) and says whether the material has a height map. The seam for the generated
// height maps (whoever makes them installs one, before the levels load); without one, rt_tess_test_heights=1 gives
// every material a test pattern (ridges along u), else nothing is displaced.
using HeightForMaterial = bool (*)(const MaterialRef* material, tess::HeightSource* out);
void set_height_lookup(HeightForMaterial lookup);

// The meshes smoothed since the last call logged (one line; nothing when none were) and the totals cleared.
void log_totals(const char* scene);

} // namespace mesh_smoothing
} // namespace render
