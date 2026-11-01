#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>

#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"

// NOT ORIGINAL - the PC scene format (scenes "x.scn.xbx", models "x.mdl.xbx", skinned models "x.skin.xbx", the same
// records) and the skin's strip breaks ("x.cas.xbx"), read record by record into plain structs.
//
// A scene (little-endian): a 12-byte header (not read), the materials {u32 count, Material...}, the sectors {u32 count,
// Sector...}, the hierarchy {i32 count, count nodes of 0x50 bytes}.
//
// - Material: u32 checksum, name checksum, pass count (at most kMaxPasses), alpha reference; u8 sort flag; float sort
//   key; u8 keep culling, two sided; i32 depth bias; u8 has two extra words (then u32 x2); float specular power
//   (when > 0: float specular colour x3); then each pass: u32 texture checksum, flags; u8 (not kept); float colour x3;
//   u32 blend mode, fixed alpha; u32 U / V addressing; float reflection tiling x2; u32 (+0xC0 of the material);
//   with flag 1 a UV wibble (0x20 bytes); pass 0 with flag 2 the colour animations {u32 count, each {u32 key count,
//   i32 phase, keys of 8 bytes}}; with flag 0x800 a texture animation {i32 key count, u32, i32 loops, u32 phase,
//   each key {i32 time, u32 texture checksum}}; then, with a texture (the first key's with an animation), u32 x2,
//   float mip LOD bias, u32 - without one 16 bytes skipped.
// - Sector: u32 checksum, light group, flags, mesh count; float box x6, sphere x4; with flags & kBillboard u32
//   billboard type, float x3 (not kept), pivot x3, axis x3; then each mesh: float centre x3, radius, min x3, max x3;
//   u32 flags, material checksum, index list count, each list {u32 count, count u16} (dropped: the PC's are the
//   mesh's own); then the mesh's body.
// - Mesh body: 8 index buffers {u16 count, count u16}; u8 stride; u16 vertex count; u8 vertex buffer count (at most
//   kMaxVertexBuffers), each {u8 scaled, u32 size, size bytes, when scaled the bone-scaled positions {i32 count,
//   count records of 0x84 bytes}}; u32 vertex type x2; u8 x3 (offsets in a vertex: the colour's, the normal's, ?);
//   u8 has per-vertex bytes (then vertex count bytes); i32 level count (when > 1: count u32); u32 pixel shader
//   (1: a chunk follows - u32 id, u32 size, size bytes).
// - Skin (.cas): u32 version; when >= 2 u32 bone count; u32 break count, breaks of 12 bytes.
//
// The loaders (pc_material_load.cpp, pc_scene.cpp, pc_mesh_load.cpp, scene_load.cpp, pc_mesh.cpp) read through
// MemoryStream (a scene in memory: its size known) or FileStream (scene_file_stream.h); every count is checked
// against what is left before anything is allocated for it. A record that is cut short or doesn't fit fails: the
// loader stops there, keeping what it had built.
namespace scene_format {

constexpr size_t kHeaderSize = 0xC;
constexpr uint32_t kMaxPasses = 4;        // a material's per-pass arrays
constexpr int kIndexBuffers = 8;          // a mesh's index buffers
constexpr uint32_t kMaxVertexBuffers = 3; // the loaders' arrays
constexpr uint32_t kBillboard = 0x800000; // a sector's flag: billboard fields follow
constexpr size_t kUvWibbleSize = 0x20;
constexpr size_t kColorKeySize = 8;
constexpr size_t kScaledVertexSize = 0x84; // a bone-scaled position record
constexpr size_t kHierarchyNodeSize = 0x50;
constexpr size_t kStripBreakSize = 12;
constexpr uint32_t kPassUvWibble = 1, kPassColorAnims = 2, kPassTextureAnim = 0x800;

// A scene in memory: util::ByteReader with the loaders' reads. A failed read zero-fills its output.
class MemoryStream {
public:
    MemoryStream(const void* data, size_t size) : in_(data, size) {}

    bool take(void* out, size_t bytes)
    {
        if (in_.bytes(out, bytes))
            return true;
        std::memset(out, 0, bytes);
        return false;
    }
    bool skip(size_t bytes) { return in_.skip(bytes); }
    // Whether `bytes` more are there to read.
    [[nodiscard]] bool fits(uint64_t bytes) const { return in_.ok() && bytes <= in_.remaining(); }
    [[nodiscard]] bool ok() const { return in_.ok(); }
    void fail() { in_.seek(in_.size() + 1); }
    [[nodiscard]] size_t position() const { return in_.position(); }
    [[nodiscard]] const uint8_t* cursor() const { return in_.cursor(); }

private:
    util::ByteReader in_;
};

template <typename Value, typename Stream>
Value take(Stream& in)
{
    Value value{};
    in.take(&value, sizeof(value));
    return value;
}

// --- materials ---

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ColorAnimTrack {
    uint32_t key_count = 0;
    int phase = 0;
    std::vector<uint8_t> keys; // key_count keys of kColorKeySize bytes
};
// NOLINTEND(readability-identifier-naming)
struct TextureFrame {
    int time = 0;
    uint32_t texture = 0; // checksum
};
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct MaterialPass {
    uint32_t texture = 0; // checksum (with a texture animation, its first key's; 0: none)
    uint32_t flags = 0;
    std::array<float, 3> color = {};
    uint32_t blend_mode = 0, fixed_alpha = 0;
    uint32_t address_u = 0, address_v = 0;
    std::array<float, 2> reflection_tiling = {};
    uint32_t unknown_c0 = 0;
    std::array<uint8_t, kUvWibbleSize> uv_wibble = {}; // with kPassUvWibble
    // with kPassTextureAnim
    int texture_key_count = 0;
    int texture_loops = 0;
    uint32_t texture_phase = 0;
    std::vector<TextureFrame> texture_keys;
    float mip_lod_bias = 0.0f; // with a texture
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Material {
    uint32_t checksum = 0, name_hash = 0, pass_count = 0, alpha_ref = 0;
    uint8_t sort_flag = 0;
    float sort_key = 0.0f;
    uint8_t keep_culling = 0, two_sided = 0;
    int depth_bias = 0;
    bool has_extra = false;
    std::array<uint32_t, 2> extra = {};
    float specular_power = 0.0f;
    std::array<float, 3> specular = {};
    std::array<MaterialPass, kMaxPasses> passes;
    std::vector<ColorAnimTrack> color_anims; // pass 0's, with kPassColorAnims
};
// NOLINTEND(readability-identifier-naming)

// False if cut short, or more than kMaxPasses passes.
template <typename Stream>
bool read_material(Stream& in, Material* m)
{
    *m = Material{};
    m->checksum = take<uint32_t>(in);
    m->name_hash = take<uint32_t>(in);
    m->pass_count = take<uint32_t>(in);
    if (!in.ok() || m->pass_count > kMaxPasses)
        return false;
    m->alpha_ref = take<uint32_t>(in);
    m->sort_flag = take<uint8_t>(in);
    m->sort_key = take<float>(in);
    m->keep_culling = take<uint8_t>(in);
    m->two_sided = take<uint8_t>(in);
    m->depth_bias = take<int>(in);
    m->has_extra = take<uint8_t>(in) != 0;
    if (m->has_extra)
        in.take(m->extra.data(), sizeof(m->extra));
    m->specular_power = take<float>(in);
    if (m->specular_power > 0.0f)
        in.take(m->specular.data(), sizeof(m->specular));
    for (uint32_t index = 0; index < m->pass_count && in.ok(); ++index) {
        MaterialPass& pass = core::at(m->passes, index);
        pass.texture = take<uint32_t>(in);
        pass.flags = take<uint32_t>(in);
        take<uint8_t>(in);
        in.take(pass.color.data(), sizeof(pass.color));
        pass.blend_mode = take<uint32_t>(in);
        pass.fixed_alpha = take<uint32_t>(in);
        pass.address_u = take<uint32_t>(in);
        pass.address_v = take<uint32_t>(in);
        in.take(pass.reflection_tiling.data(), sizeof(pass.reflection_tiling));
        pass.unknown_c0 = take<uint32_t>(in);
        if (pass.flags & kPassUvWibble)
            in.take(pass.uv_wibble.data(), sizeof(pass.uv_wibble));
        if (index == 0 && (pass.flags & kPassColorAnims)) {
            auto anims = take<uint32_t>(in);
            if (!in.fits(uint64_t{anims} * 8)) // (each at least its key count and phase)
                return false;
            m->color_anims.resize(anims);
            for (ColorAnimTrack& track : m->color_anims) {
                track.key_count = take<uint32_t>(in);
                track.phase = take<int>(in);
                if (!in.fits(uint64_t{track.key_count} * kColorKeySize))
                    return false;
                track.keys.resize(track.key_count * kColorKeySize);
                in.take(track.keys.data(), track.keys.size());
            }
        }
        if (pass.flags & kPassTextureAnim) {
            pass.texture_key_count = take<int>(in);
            take<uint32_t>(in);
            pass.texture_loops = take<int>(in);
            pass.texture_phase = take<uint32_t>(in);
            if (pass.texture_key_count < 0 || !in.fits(static_cast<uint64_t>(pass.texture_key_count) * 8))
                return false;
            pass.texture_keys.resize(static_cast<size_t>(pass.texture_key_count));
            for (TextureFrame& key : pass.texture_keys) {
                key.time = take<int>(in);
                key.texture = take<uint32_t>(in);
            }
            if (!pass.texture_keys.empty())
                pass.texture = core::at(pass.texture_keys, 0).texture;
        }
        if (!pass.texture) {
            in.skip(0x10);
            continue;
        }
        take<uint32_t>(in);
        take<uint32_t>(in);
        pass.mip_lod_bias = take<float>(in);
        take<uint32_t>(in);
    }
    return in.ok();
}

// --- sectors ---

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Sector {
    uint32_t checksum = 0, light_group = 0, flags = 0, mesh_count = 0;
    std::array<float, 6> box = {};
    std::array<float, 4> sphere = {};
    uint32_t billboard_type = 0; // with kBillboard
    std::array<float, 3> billboard_unused = {};
    std::array<float, 3> pivot = {};
    std::array<float, 3> axis = {};
};
// NOLINTEND(readability-identifier-naming)
// A sector's mesh's own fields (before its body).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct SectorMesh {
    std::array<float, 3> centre = {};
    float radius = 0.0f;
    std::array<float, 3> min = {};
    std::array<float, 3> max = {};
    uint32_t flags = 0, material = 0, index_lists = 0;
};
// NOLINTEND(readability-identifier-naming)

// False if cut short (or the meshes, at least their fields each, can't be there).
template <typename Stream>
bool read_sector(Stream& in, Sector* s)
{
    *s = Sector{};
    s->checksum = take<uint32_t>(in);
    s->light_group = take<uint32_t>(in);
    s->flags = take<uint32_t>(in);
    s->mesh_count = take<uint32_t>(in);
    in.take(s->box.data(), sizeof(s->box));
    in.take(s->sphere.data(), sizeof(s->sphere));
    if (s->flags & kBillboard) {
        s->billboard_type = take<uint32_t>(in);
        in.take(s->billboard_unused.data(), sizeof(s->billboard_unused));
        in.take(s->pivot.data(), sizeof(s->pivot));
        in.take(s->axis.data(), sizeof(s->axis));
    }
    return in.ok() && in.fits(uint64_t{s->mesh_count} * 0x34);
}

// The mesh's fields and its index lists (skipped). False if cut short.
template <typename Stream>
bool read_sector_mesh(Stream& in, SectorMesh* mesh)
{
    *mesh = SectorMesh{};
    in.take(mesh->centre.data(), sizeof(mesh->centre));
    mesh->radius = take<float>(in);
    in.take(mesh->min.data(), sizeof(mesh->min));
    in.take(mesh->max.data(), sizeof(mesh->max));
    mesh->flags = take<uint32_t>(in);
    mesh->material = take<uint32_t>(in);
    mesh->index_lists = take<uint32_t>(in);
    for (uint32_t list = 0; list < mesh->index_lists && in.ok(); ++list) {
        auto count = take<uint32_t>(in);
        if (!in.fits(uint64_t{count} * 2))
            return false;
        in.skip(count * 2u);
    }
    return in.ok();
}

// --- mesh bodies ---

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct MeshBody {
    std::array<uint16_t, kIndexBuffers> index_counts = {};
    std::array<std::vector<uint16_t>, kIndexBuffers> indices;
    uint8_t stride = 0;
    uint16_t vertex_count = 0;
    uint8_t vertex_buffer_count = 0;
    std::vector<uint8_t> vertices; // the first vertex buffer's bytes (the others are read past: never used)
    std::vector<uint8_t> scaled;   // its bone-scaled positions: records of kScaledVertexSize bytes
    std::array<uint32_t, 2> vertex_types = {};
    std::array<uint8_t, 3> offsets = {}; // as stored: the colour's, the normal's, ?
    bool has_vertex_bytes = false;
    std::vector<uint8_t> vertex_bytes; // vertex_count bytes
    int level_count = 0;
    std::vector<uint32_t> levels; // when level_count > 1
    uint32_t shader = 0;
};
// NOLINTEND(readability-identifier-naming)

// False if cut short, more than kMaxVertexBuffers buffers, or the scaled positions don't fit their buffer (each a
// stride apart, 12 bytes written).
template <typename Stream>
bool read_mesh_body(Stream& in, MeshBody* body)
{
    for (int i = 0; i < kIndexBuffers; ++i) {
        auto count = take<uint16_t>(in);
        core::at(body->index_counts, i) = count;
        core::at(body->indices, i).clear();
        if (!count)
            continue;
        if (!in.fits(count * 2u))
            return false;
        core::at(body->indices, i).resize(count);
        in.take(core::at(body->indices, i).data(), count * 2u);
    }
    body->stride = take<uint8_t>(in);
    body->vertex_count = take<uint16_t>(in);
    body->vertex_buffer_count = take<uint8_t>(in);
    if (!in.ok() || body->vertex_buffer_count > kMaxVertexBuffers)
        return false;
    body->vertices.clear();
    body->scaled.clear();
    for (uint32_t v = 0; v < body->vertex_buffer_count; ++v) {
        auto scaled = take<uint8_t>(in);
        auto size = take<uint32_t>(in);
        if (!in.fits(size))
            return false;
        if (v == 0) {
            body->vertices.resize(size);
            in.take(body->vertices.data(), size);
        } else {
            in.skip(size);
        }
        if (!scaled)
            continue;
        const int count = take<int>(in);
        if (count < 0 || !in.fits(static_cast<uint64_t>(count) * kScaledVertexSize))
            return false;
        if (count && static_cast<uint64_t>(count - 1) * body->stride + 12 > size)
            return false;
        if (v == 0) {
            body->scaled.resize(static_cast<size_t>(count) * kScaledVertexSize);
            in.take(body->scaled.data(), body->scaled.size());
        } else {
            in.skip(static_cast<size_t>(count) * kScaledVertexSize);
        }
    }
    core::at(body->vertex_types, 0) = take<uint32_t>(in);
    core::at(body->vertex_types, 1) = take<uint32_t>(in);
    in.take(body->offsets.data(), sizeof(body->offsets));
    body->has_vertex_bytes = take<uint8_t>(in) != 0;
    body->vertex_bytes.clear();
    if (body->has_vertex_bytes) {
        if (!in.fits(body->vertex_count))
            return false;
        body->vertex_bytes.resize(body->vertex_count);
        in.take(body->vertex_bytes.data(), body->vertex_bytes.size());
    }
    body->level_count = take<int>(in);
    body->levels.clear();
    if (body->level_count > 1) {
        if (!in.fits(static_cast<uint64_t>(body->level_count) * 4))
            return false;
        body->levels.resize(static_cast<size_t>(body->level_count));
        in.take(body->levels.data(), body->levels.size() * 4);
    }
    body->shader = take<uint32_t>(in);
    if (body->shader == 1) {
        take<uint32_t>(in);
        auto size = take<uint32_t>(in);
        if (!in.skip(size))
            return false;
    }
    return in.ok();
}

// The vertex bytes a mesh's first buffer must hold: `vertex_count` vertices a stride apart, each `vertex_bytes` long
// (its stride with a vertex format; the packed skinning fields, 0x20 + 8 a texture set, without one).
inline bool vertices_fit(const MeshBody& body, uint32_t vertex_bytes)
{
    return body.vertex_count == 0 ||
           static_cast<uint64_t>(body.vertex_count - 1) * body.stride + vertex_bytes <= body.vertices.size();
}

// Whether a mesh with a vertex format has its offsets inside a vertex: the normal (12 bytes), the colour (4 - the
// colour animations write it each frame, splats and projections read it) and the texture coordinates (8 - grass).
inline bool offsets_fit(const MeshBody& body)
{
    return core::at(body.offsets, 0) + 12u <= body.stride && core::at(body.offsets, 1) + 4u <= body.stride &&
           core::at(body.offsets, 2) + 8u <= body.stride;
}

// --- skins (.cas) ---

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Skin {
    uint32_t version = 0, bone_count = 0, break_count = 0;
    std::vector<uint8_t> breaks; // break_count records of kStripBreakSize bytes
};
// NOLINTEND(readability-identifier-naming)

template <typename Stream>
bool read_skin(Stream& in, Skin* skin)
{
    *skin = Skin{};
    skin->version = take<uint32_t>(in);
    if (skin->version >= 2)
        skin->bone_count = take<uint32_t>(in);
    skin->break_count = take<uint32_t>(in);
    if (!in.fits(uint64_t{skin->break_count} * kStripBreakSize))
        return false;
    skin->breaks.resize(skin->break_count * kStripBreakSize);
    in.take(skin->breaks.data(), skin->breaks.size());
    return in.ok();
}

// --- a whole scene (the format check's walk; the loaders build as they read) ---

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct SceneCounts {
    uint32_t materials = 0, sectors = 0, meshes = 0, hierarchy = 0;
    uint64_t indices = 0, vertex_bytes = 0;
};
// NOLINTEND(readability-identifier-naming)

template <typename Stream>
bool walk_scene(Stream& in, SceneCounts* counts)
{
    *counts = SceneCounts{};
    in.skip(kHeaderSize);
    auto materials = take<uint32_t>(in);
    Material material;
    for (uint32_t i = 0; i < materials; ++i) {
        if (!read_material(in, &material))
            return false;
        ++counts->materials;
    }
    auto sectors = take<uint32_t>(in);
    Sector sector;
    SectorMesh mesh;
    MeshBody body;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!read_sector(in, &sector))
            return false;
        for (uint32_t j = 0; j < sector.mesh_count; ++j) {
            if (!read_sector_mesh(in, &mesh) || !read_mesh_body(in, &body))
                return false;
            ++counts->meshes;
            for (const uint16_t count : body.index_counts)
                counts->indices += count;
            counts->vertex_bytes += body.vertices.size();
        }
        ++counts->sectors;
    }
    const int nodes = take<int>(in);
    if (!in.ok())
        return false;
    if (nodes > 0) {
        if (!in.fits(static_cast<uint64_t>(nodes) * kHierarchyNodeSize))
            return false;
        in.skip(static_cast<size_t>(nodes) * kHierarchyNodeSize);
        counts->hierarchy = static_cast<uint32_t>(nodes);
    }
    return in.ok();
}

} // namespace scene_format
