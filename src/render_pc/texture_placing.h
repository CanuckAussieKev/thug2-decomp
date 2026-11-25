#pragma once

#include <cstdint>
#include <vector>

#include "engine/gfx/facade_relief.h"

// NOT ORIGINAL - (round TRAIN) a scene mesh's texture coordinates that place nothing made world-aligned at load
// (engine/gfx/texture_mapping.h), before its buffers are made: whatever draws or traces it reads the same coordinates.
namespace scene_format {
struct MeshBody;
} // namespace scene_format

namespace render {

struct MaterialRef;

namespace texture_placing {

// The mesh body (read_mesh: the file's - smoothed, relieved -, before the buffers are made) with its first texture
// coordinates made world-aligned where they place nothing (texture_mapping.h): a plain vertex format with texture
// coordinates; not skinned, no per-vertex colour animation, no bone-scaled positions, not dynamic; its material's first
// pass opaque (neither transparent nor cut out) with a texture. The copies a triangle needed follow the vertices before
// (their sources added to `sources`, each one vertex: mesh_smoothing::remember_sources / remember_made). Returns the
// first level of detail's strip as it was when it was made again (empty: kept), for mesh_smoothing::remember_original.
[[nodiscard]] std::vector<uint16_t> place_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material,
                                               bool dynamic, std::vector<relief::VertexSource>* sources);

// The meshes changed since the last call logged (one line; nothing when none were) and the totals cleared.
void log_totals(const char* scene);

} // namespace texture_placing
} // namespace render
