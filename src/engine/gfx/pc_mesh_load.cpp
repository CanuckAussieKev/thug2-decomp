// The PC meshes' loading (sub_mesh.h): a mesh's index buffers, vertex buffers, colours, levels of detail, material and
// pixel shader from a scene file (in memory or read from a file); the skinned vertices unpacked; the vertex positions
// a skinned model's bone scales move (while set_skin_scaling's are on); the collision positions and triangles.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/scene_file_stream.h"
#include "formats/scene_format.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"
#include "engine/core/bits_as.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {
float g_unpack_z_scale{0.0019569471f};
float g_unpack11_scale{0.0009775171f};
std::array<uint32_t, 5> g_fvf_by_set_count{{0, 0x100, 0x200, 0x300, 0x400}};
} // namespace
std::array<uint32_t, 6> g_vertex_shaders;

namespace {

void* alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// A vertex's bone-scaled position record (0x84 bytes): 12 bytes unused, the three bones' weights, then each bone's
// position (P, Q, R) and the vertex's offset from it (A, B, C), four floats each, and the three bones (-1: none).
struct ScaledVertex {
    std::array<uint32_t, 3> unused;
    std::array<float, 3> weights;
    float p[4], q[4], r[4];
    float a[4], b[4], c[4];
    std::array<int, 3> bones;
};
static_assert(sizeof(ScaledVertex) == scene_format::kScaledVertexSize, "file format: a bone-scaled position record");

bool skin_scaling_on() { return g_skin_scaling_on && memory::bits_as<float*>(core::at(g_skin_settings, 3)); }

// A bone's scale (four floats: x, then z, then y as the loaders apply them, and w); 1 without a bone.
void bone_scale_terms(int bone, float& e0, float& e4, float& e8, float& e12)
{
    if (bone == -1) {
        e0 = e4 = e8 = e12 = 1.0f;
        return;
    }
    const float* scale = memory::bits_as<float*>(core::at(g_skin_settings, 3)) + 4 * bone;
    e0 = scale[0];
    e4 = scale[1];
    e8 = scale[2];
    e12 = scale[3];
}

// A vertex's position as the bone scales move it: each bone's offset scaled - x by the scale's first value, y by its
// third, z by its second - and moved by the bone's position, then blended by the weights. (The original's memory and
// file loaders compute it in x87 code that rounds different sums and products to float; in plain float arithmetic
// they are the same formula.)
void scaled_position(const ScaledVertex& v, float* out)
{
    float e0[4], e1[4], e2[4];
    bone_scale_terms(core::at(v.bones, 0), e0[0], e0[1], e0[2], e0[3]);
    bone_scale_terms(core::at(v.bones, 1), e1[0], e1[1], e1[2], e1[3]);
    bone_scale_terms(core::at(v.bones, 2), e2[0], e2[1], e2[2], e2[3]);
    const float ax = v.p[0] + e0[0] * v.a[0], ay = v.p[1] + e0[2] * v.a[1], az = v.p[2] + e0[1] * v.a[2];
    const float bx = v.q[0] + e1[0] * v.b[0], by = v.q[1] + e1[2] * v.b[1], bz = v.q[2] + e1[1] * v.b[2];
    const float cx = v.r[0] + e2[0] * v.c[0], cy = v.r[1] + e2[2] * v.c[1], cz = v.r[2] + e2[1] * v.c[2];
    const float* s = v.weights.data();
    out[0] = ax * s[0] + bx * s[1] + cx * s[2];
    out[1] = ay * s[0] + by * s[1] + cy * s[2];
    out[2] = az * s[0] + bz * s[1] + cz * s[2];
}

// The vertex formats a scene file's type numbers name (0x00775B64-78; 0: none; past 6: the number itself).
uint32_t vertex_format(uint32_t type)
{
    if (type > 6)
        return type;
    // (type t is the table's word 6 - t)
    return type ? core::at(g_vertex_shaders, 6 - type) : 0;
}

// The bone-scaled positions written into a vertex buffer (0x004D47F0 / 0x004D4350: the memory loader's and the file
// loader's) - only while the skin scaling is on.
void apply_scaled_positions(const std::vector<uint8_t>& records, uint8_t* vertices, uint32_t stride)
{
    const size_t count = records.size() / scene_format::kScaledVertexSize;
    for (size_t i = 0; i < count; ++i) {
        ScaledVertex v{};
        std::memcpy(&v, records.data() + i * scene_format::kScaledVertexSize, sizeof(v));
        if (skin_scaling_on())
            scaled_position(v, raw::as<float>(vertices));
        vertices += stride;
    }
}

// The mesh made from its body record (scene_format.h), as both of the original's loaders do. False if the stream
// failed (nothing is made).
template <typename Stream>
bool read_mesh(MeshRef* mesh, Stream& in, uint32_t material_checksum, SceneRef* scene, VertexArena* arena, bool dynamic)
{
    // Not original - the body read whole and checked first: index lists, vertex buffers, scaled positions,
    // per-vertex bytes and levels past the end, more than 3 vertex buffers (the original's arrays), or scaled
    // positions past their buffer end the load (the original read and wrote past them).
    scene_format::MeshBody body;
    if (!scene_format::read_mesh_body(in, &body) || (core::at(body.vertex_types, 0) && !scene_format::offsets_fit(body))) {
        // (a vertex's offsets past its stride: the colour animations wrote past the buffer, grass / splats read past)
        debug::printf("scene mesh damaged: cut short or doesn't fit\n");
        in.fail();
        return false;
    }
    for (int i = 0; i < scene_format::kIndexBuffers; ++i) {
        core::at(mesh->index_counts_, i) = core::at(body.index_counts, i);
        const uint16_t count = core::at(body.index_counts, i);
        if (!count)
            continue;
        void* memory = alloc_block(sizeof(IdxStore));
        IdxStore* ib = memory ? memory::construct_in<IdxStore>(memory, count * 2u, 0, D3DFMT_INDEX16) : nullptr;
        core::at(mesh->index_buffers_, i) = ib;
        std::memcpy(ib->indices, core::at(body.indices, i).data(), count * 2u);
        ib->get_unlock();
    }
    mesh->stride = body.stride;
    mesh->vertex_count_ = body.vertex_count;
    mesh->vertex_buffer_count_ = body.vertex_buffer_count;
    if (!body.scaled.empty())
        apply_scaled_positions(body.scaled, body.vertices.data(), mesh->stride);
    mesh->fvf = vertex_format(core::at(body.vertex_types, 0));
    *raw::as<uint32_t>(mesh->unknown3c_.data()) = vertex_format(core::at(body.vertex_types, 1));
    core::at(mesh->unknown1c_, 1) = core::at(body.offsets, 0);
    core::at(mesh->unknown1c_, 0) = core::at(body.offsets, 1);
    core::at(mesh->unknown1c_, 2) = core::at(body.offsets, 2);
    mesh->buffer_usage_ = 0;
    mesh->buffer_flags_ = 0;
    if (body.has_vertex_bytes) {
        mesh->per_vertex_bytes_ = static_cast<uint8_t*>(memory::alloc(mesh->vertex_count_));
        std::memcpy(mesh->per_vertex_bytes_, body.vertex_bytes.data(), body.vertex_bytes.size());
        mesh->buffer_flags_ |= 2;
        arena = nullptr; // (written each frame: a buffer of its own)
        mesh->buffer_usage_ = D3DUSAGE_DYNAMIC;
    }
    if (dynamic)
        mesh->buffer_usage_ = D3DUSAGE_DYNAMIC;
    if (body.level_count > 1) {
        mesh->unknown48 = memory::alloc(static_cast<uint32_t>(body.level_count) * 4);
        std::memcpy(mesh->unknown48, body.levels.data(), body.levels.size() * 4);
    }
    auto* material = static_cast<MaterialRef*>(scene->find_material(material_checksum));
    mesh->material = material;
    if (material && (core::at(material->pass_flags_, 0) & 0x10))
        mesh->flags |= 0x100;
    mesh->vertex_buffer_count_ = 1;
    // Not original - the vertices the mesh draws (or, without a vertex format, unpacks) within its buffer: a count
    // past them is cut to what is there (the original read past the buffer).
    const uint32_t sets = material ? material->pass_count_ : 0;
    const uint32_t vertex_bytes = mesh->fvf ? mesh->stride : 0x20 + sets * 8;
    if (!scene_format::vertices_fit(body, vertex_bytes)) {
        uint32_t fit = 0;
        if (body.vertices.size() >= vertex_bytes)
            fit = mesh->stride ? static_cast<uint32_t>((body.vertices.size() - vertex_bytes) / mesh->stride) + 1 : 0;
        debug::printf("scene mesh damaged: %u vertices, %u fit their buffer\n", mesh->vertex_count_, fit);
        mesh->vertex_count_ = static_cast<uint16_t>(fit);
    }
    auto size = static_cast<uint32_t>(body.vertices.size());
    if (mesh->fvf) {
        mesh->create_vertex_buffer(body.vertices.data(), size, arena);
    } else if (material) {
        mesh->buffer_usage_ = D3DUSAGE_DYNAMIC;
        mesh->set_up_skinning(body.vertices.data(), size, nullptr);
    } else {
        // Not original - no material to unpack with (the original read it through null).
        debug::printf("scene mesh damaged: a skinned mesh without its material %08x\n", material_checksum);
        mesh->buffer_usage_ = D3DUSAGE_DYNAMIC;
        mesh->vertex_count_ = 0;
    }
    if (body.shader == 1) {
        mesh->pixel_shader_ = nullptr; // (the chunk skipped)
        if (g_shaders_available)
            material_pixel_shader(material, &mesh->pixel_shader_);
    } else {
        mesh->pixel_shader_ = pixel_shader_from_index(body.shader);
    }
    return true;
}

} // namespace

// 0x004D5940
void MeshRef::set_up_skinning(const uint8_t* packed, uint32_t /*bytes*/, VertexArena* arena)
{
    const uint32_t sets = material->pass_count_; // (a texture coordinate set a pass)
    auto* out = static_cast<uint8_t*>(alloc_block(vertex_count_ * 0x4Cu));
    per_vertex_data_ = out;
    uint32_t bones = 1;
    const bool suspect = vertex_count_ == 0x280 && core::at(index_counts_, 0) == 0x6A3; // (a debugging check the original kept)
    auto unpack11 = [](uint32_t bits) {
        return static_cast<int>(bits & 0x400 ? (bits & 0x7FF) - 0x800 : bits & 0x7FF);
    };
    for (int i = 0; std::cmp_less(i, vertex_count_); ++i) {
        std::memcpy(out, packed, 12); // the position
        if (suspect && i == 0xF7)
            debug::printf("euh");
        const uint32_t normal = *raw::field_at<const uint32_t>(packed, 0x18);
        auto* f = raw::as<float>(out);
        f[3] = static_cast<float>(unpack11(normal) * static_cast<double>(g_unpack11_scale));
        f[4] = static_cast<float>(unpack11(normal >> 11) * static_cast<double>(g_unpack11_scale));
        const uint32_t z = normal >> 22;
        f[5] = static_cast<float>(static_cast<int>(z & 0x200 ? z - 0x400 : z) * static_cast<double>(g_unpack_z_scale));
        std::memcpy(out + 0x18, packed + 0x1C, 4); // the colour
        std::memcpy(out + 0x1C, packed + 0x20, sets * 8);
        for (int k = 0; k < 4; ++k)
            out[0x3C + k] = static_cast<uint8_t>(*raw::field_at<const int16_t>(packed, 0x10 + 2 * k) / 3);
        const uint32_t weights = *raw::field_at<const uint32_t>(packed, 0xC);
        f[16] = static_cast<float>(unpack11(weights) * static_cast<double>(g_unpack11_scale));
        f[17] = static_cast<float>(unpack11(weights >> 11) * static_cast<double>(g_unpack11_scale));
        const uint32_t w = weights >> 22;
        const double third = static_cast<int>(w & 0x200 ? w - 0x400 : w) * static_cast<double>(g_unpack_z_scale);
        f[18] = static_cast<float>(third);
        if (bones < 2 && !(f[17] == 0.0f))
            bones = 2;
        if (bones < 3 && !(third == 0.0))
            bones = 3;
        out += 0x4C;
        packed += stride;
    }
    unknowna4_ = sets;
    unknowna8_ = bones;
    fvf = core::at(g_fvf_by_set_count, sets) | 0x52;
    stride = static_cast<uint8_t>(sets * 8 + 0x1C);
    buffer_flags_ |= 1;
    core::at(vertex_buffers_, 0) = new_vertex_buffer(stride * vertex_count_, buffer_usage_, buffer_flags_);
    if (arena)
        arena->size += (stride * vertex_count_ + 0x1Fu) & ~0x1Fu;
}

// 0x004D5CC0
bool MeshRef::read_from_ram_block(scene_format::MemoryStream& in, uint32_t material_checksum, SceneRef* scene,
                                  VertexArena* arena)
{
    return read_mesh(this, in, material_checksum, scene, arena, false);
}

// 0x004D6100
bool MeshRef::ingest_from_file(scene_format::FileStream& in, uint32_t material_checksum, SceneRef* scene,
                               VertexArena* arena, bool dynamic)
{
    return read_mesh(this, in, material_checksum, scene, arena, dynamic);
}

// 0x004D6530
void MeshRef::build_collision()
{
    if (position_count_ || !core::at(index_counts_, 0))
        return;
    VertexBuffer* vb = core::at(vertex_buffers_, 0);
    const uint8_t* vertex = vb->data; // (locked read-only, inline)
    vb->lock_offset_ = 0;
    vb->lock_size_ = vb->size;
    vb->lock_flags_ = D3DLOCK_READONLY;
    position_count_ = vertex_count_;
    positions = static_cast<float*>(alloc_block(vertex_count_ * 12u));
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        std::memcpy(positions + 3 * i, vertex, 12);
        vertex += stride;
    }
    core::at(vertex_buffers_, 0)->get_unlock();
    const uint16_t* strip = core::at(index_buffers_, 0)->indices;
    auto* found = static_cast<uint16_t*>(memory::alloc((core::at(index_counts_, 0) - 2u) * 6));
    uint32_t count = 0;
    uint16_t* out = found;
    for (uint32_t k = 0; k < core::at(index_counts_, 0) - 2u; ++k, ++strip) {
        const uint16_t a = strip[0];
        const uint16_t b = strip[1];
        const uint16_t c = strip[2];
        if (a == b || b == c || c == a)
            continue;
        if (k & 1) { // (every other triangle of a strip winds the other way)
            out[0] = b;
            out[1] = a;
        } else {
            out[0] = a;
            out[1] = b;
        }
        out[2] = c;
        out += 3;
        ++count;
    }
    core::at(index_buffers_, 0)->get_unlock();
    if (count) {
        triangles = static_cast<uint16_t*>(alloc_block(count * 6u));
        std::memcpy(triangles, found, count * 6u);
        triangle_count_ = count;
    }
    memory::Hub::get_pull()->dealloc(found);
}

} // namespace render
