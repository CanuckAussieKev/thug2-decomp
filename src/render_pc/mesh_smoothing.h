#pragma once

#include <cstdint>
#include <span>
#include <vector>

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

// [Graphics] rt_tessellation=1 (read once).
[[nodiscard]] bool on();

// The mesh body (read_mesh: the file's, before the buffers are made) smoothed when it can be: a vertex format of a
// position, an optional normal and colour and texture coordinate pairs; not skinned, no per-vertex colour animation,
// no bone-scaled positions, not dynamic; its material neither transparent nor cut out.
// Returns the first level of detail's strip as the file had it when the body was smoothed (empty: left as it was), for
// remember_original.
[[nodiscard]] std::vector<uint16_t> smooth_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material,
                                                bool dynamic);

// A smoothed mesh's original strip, by its first level of detail's index buffer (shared by the copies that share its
// buffers; copied with those that copy them): what the baked sun's samples read (rt_record), so the level's sun and the
// bake's agreement are found on the original surfaces, as without smoothing (the original vertices are still first).
// Empty: not smoothed. Forgotten when the buffer is destroyed.
void remember_original(const IndexBufferData* drawn, std::vector<uint16_t> original);
[[nodiscard]] std::span<const uint16_t> original_strip(const IndexBufferData* drawn);
void copy_original(const IndexBufferData* from, const IndexBufferData* to);
void forget_original(const IndexBufferData* drawn);

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
