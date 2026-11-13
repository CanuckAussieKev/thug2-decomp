#pragma once

#include <cstdint>

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

namespace mesh_smoothing {

// [Graphics] rt_tessellation=1 (read once).
[[nodiscard]] bool on();

// The mesh body (read_mesh: the file's, before the buffers are made) smoothed when it can be: a vertex format of a
// position, an optional normal and colour and texture coordinate pairs; not skinned, no per-vertex colour animation,
// no bone-scaled positions, not dynamic; its material neither transparent nor cut out.
void smooth_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic);

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
