#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/gfx/facade_relief.h"
#include "formats/relief_format.h"

// NOT ORIGINAL - building relief at load ([Graphics] building_relief; engine/gfx/facade_relief.h, docs/design/vulkan.md
// "Building relief"): a texture dictionary's facade structures (formats/relief_format.h, "<dictionary>.relief") read as
// it loads and kept by texture; a scene mesh drawn with one of those textures gets its building fronts built as relief
// geometry before its buffers are made, so whatever draws it or traces it sees the same geometry. Off, or without the
// files: nothing changes.
namespace scene_format {
struct MeshBody;
} // namespace scene_format

namespace render {

struct MaterialRef;
struct TextureImage;

namespace building_relief {

// [Graphics] building_relief resolved on (read once: it needs the Vulkan renderer and the files).
[[nodiscard]] bool on();

// A dictionary's facade structures, read as it loads.
class ReliefPack {
public:
    // The structures beside the dictionary `name` ("<name>.relief"), when building relief is on and the file is there.
    explicit ReliefPack(const char* name);
    // Whether the dictionary has any (callers skip the rest when not).
    [[nodiscard]] bool active() const { return !file_.entries.empty(); }
    // `texture`'s structure kept when the file has one for these texels (its first level as the dictionary stores
    // it - or, for a dictionary a texture set serves, the original's of that checksum).
    void attach(const TextureImage* texture, uint32_t checksum, std::span<const uint8_t> first_level);
    // One log line for the dictionary; none if inactive.
    void report() const;

private:
    std::string name_;
    std::string set_;
    std::unordered_map<uint32_t, uint64_t> originals_;
    relief_format::File file_;
    uint32_t attached_ = 0;
};

// A texture's structure forgotten (it is being destroyed).
void forget(const TextureImage* texture);

// The mesh body (read_mesh: the file's - or smoothed -, before the buffers are made) with its building fronts built,
// when its material's first texture has a structure: a plain vertex format with texture coordinates; not skinned, no
// per-vertex colour animation, no bone-scaled positions, not dynamic; its material neither transparent nor cut out.
// Returns the first level of detail's strip as it was (empty: left as it was), for mesh_smoothing::remember_original.
// `sources`: each new vertex's sources (facade_relief.h VertexSource) - for mesh_smoothing::remember_sources.
[[nodiscard]] std::vector<uint16_t> relieve_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material,
                                                 bool dynamic, std::vector<relief::VertexSource>* sources);

// The meshes relieved since the last call logged (one line; nothing when none were) and the totals cleared.
void log_totals(const char* scene);

} // namespace building_relief
} // namespace render
