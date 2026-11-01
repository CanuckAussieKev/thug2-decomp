// The PC meshes' loading (sub_mesh.h): a mesh's index buffers, vertex buffers, colours, levels of detail, material and
// pixel shader from a scene file (in memory or read from a file); the skinned vertices unpacked; the vertex positions
// a skinned model's bone scales move (while set_skin_scaling's are on); the collision positions and triangles.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
#include "engine/gfx/scene_format.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"

namespace render {

float g_unpack_z_scale{0.0019569471f};
float g_unpack11_scale{0.0009775171f};
uint32_t g_fvf_by_set_count[5]{0, 0x100, 0x200, 0x300, 0x400};
uint32_t g_vertex_shaders[6];

namespace {

void* alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// A vertex's bone-scaled position record (0x84 bytes): 12 bytes unused, the three bones' weights, then each bone's
// position (P, Q, R) and the vertex's offset from it (A, B, C), four floats each, and the three bones (-1: none).
struct ScaledVertex {
    uint32_t unused[3];
    float weights[3];
    float p[4], q[4], r[4];
    float a[4], b[4], c[4];
    int bones[3];
};
static_assert(sizeof(ScaledVertex) == scene_format::kScaledVertexSize, "file format: a bone-scaled position record");

bool skin_scaling_on() { return g_skin_scaling_on && reinterpret_cast<float*&>(g_skin_settings[3]); }

// A bone's scale (four floats: x, then z, then y as the loaders apply them, and w); 1 without a bone.
void bone_scale_terms(int bone, float& e0, float& e4, float& e8, float& e12)
{
    if (bone == -1) {
        e0 = e4 = e8 = e12 = 1.0f;
        return;
    }
    const float* scale = reinterpret_cast<float*&>(g_skin_settings[3]) + 4 * bone;
    e0 = scale[0];
    e4 = scale[1];
    e8 = scale[2];
    e12 = scale[3];
}

// The memory loader's arithmetic (the original's x87 code: what it keeps in registers stays there): each bone's
// offset scaled - x by the scale's first value, y by its third, z by its second - and moved by the bone's position,
// then blended by the weights.
void scaled_position_in_memory(ScaledVertex v, float* out)
{
    float e0_0, e0_4, e0_8, e0_c, e1_0, e1_4, e1_8, e1_c, e2_0, e2_4, e2_8, e2_c;
    bone_scale_terms(v.bones[0], e0_0, e0_4, e0_8, e0_c);
    bone_scale_terms(v.bones[1], e1_0, e1_4, e1_8, e1_c);
    bone_scale_terms(v.bones[2], e2_0, e2_4, e2_8, e2_c);
    float ax_ = v.a[0], ay = v.a[1], az = v.a[2], bx_ = v.b[0], by = v.b[1], bz = v.b[2];
    float cx_ = v.c[0], cy = v.c[1], cz = v.c[2];
    float px = v.p[0], py = v.p[1], pz = v.p[2], qx = v.q[0], qy = v.q[1], qz = v.q[2];
    float rx = v.r[0], ry = v.r[1], rz = v.r[2];
    float s0 = v.weights[0], s1 = v.weights[1], s2 = v.weights[2];
    float t0, t1, v2, w1, w2, x1, y1, z1, out_x, out_y, out_z;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul ax_
        fstp ax_
        fmul ay
        fld az
        fmul e0_4
        fstp az
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul bx_
        fstp bx_
        fmul by
        fld bz
        fmul e1_4
        fstp bz
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul cx_
        fstp cx_
        fmul cy
        fld cz
        fmul e2_4
        fld px
        fadd ax_
        fstp ax_
        fld py
        fadd st(0), st(4)
        fstp ay
        fld pz
        fadd az
        fstp az
        fld qx
        fadd bx_
        fstp bx_
        fld qy
        fadd st(0), st(3)
        fstp by
        fld qz
        fadd bz
        fstp bz
        fld rx
        fadd cx_
        fstp cx_
        fld ry
        fadd st(0), st(2)
        fstp cy
        fld rz
        fadd st(0), st(1)
        fstp st(4)
        fstp st(0)
        fstp st(0)
        fstp st(0)
        fld cx_
        fmul s2
        fstp t0
        fld cy
        fmul s2
        fstp t1
        fmul s2
        fld bx_
        fmul s1
        fld by
        fmul s1
        fld bz
        fmul s1
        fstp v2
        fld ax_
        fmul s0
        fld ay
        fmul s0
        fstp w1
        fld az
        fmul s0
        fstp w2
        fadd st(0), st(2)
        fstp x1
        fld w1
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld w2
        fadd v2
        fstp z1
        fld x1
        fadd t0
        fld y1
        fadd t1
        fld z1
        fadd st(0), st(3)
        fstp out_z
        fxch st(1)
        fstp out_x
        fstp out_y
        fstp st(0)
    }
    out[0] = out_x;
    out[1] = out_y;
    out[2] = out_z;
}

// The file loader's (the w components scaled and moved too; other values rounded than the memory loader's).
void scaled_position_in_file(ScaledVertex v, float* out)
{
    float e0_0, e0_4, e0_8, e0_c, e1_0, e1_4, e1_8, e1_c, e2_0, e2_4, e2_8, e2_c;
    bone_scale_terms(v.bones[0], e0_0, e0_4, e0_8, e0_c);
    bone_scale_terms(v.bones[1], e1_0, e1_4, e1_8, e1_c);
    bone_scale_terms(v.bones[2], e2_0, e2_4, e2_8, e2_c);
    float ax_ = v.a[0], ay = v.a[1], az = v.a[2], aw = v.a[3], bx_ = v.b[0], by = v.b[1], bz = v.b[2], bw = v.b[3];
    float cx_ = v.c[0], cy = v.c[1], cz = v.c[2], cw = v.c[3];
    float px = v.p[0], py = v.p[1], pz = v.p[2], pw = v.p[3], qx = v.q[0], qy = v.q[1], qz = v.q[2], qw = v.q[3];
    float rx = v.r[0], ry = v.r[1], rz = v.r[2], rw = v.r[3];
    float s0 = v.weights[0], s1 = v.weights[1], s2 = v.weights[2];
    float cyw, czw, bzw, ayw, azw, x1, y1, z1, out_x, out_y, out_z;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul ax_
        fstp ax_
        fld ay
        fmul st(0), st(1)
        fstp ay
        fstp st(0)
        fld az
        fmul e0_4
        fstp az
        fld aw
        fmul e0_c
        fstp aw
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul bx_
        fstp bx_
        fld by
        fmul st(0), st(1)
        fstp by
        fstp st(0)
        fld bz
        fmul e1_4
        fstp bz
        fld bw
        fmul e1_c
        fstp bw
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul cx_
        fstp cx_
        fmul cy
        fld cz
        fmul e2_4
        fld cw
        fmul e2_c
        fstp cw
        fld px
        fadd ax_
        fstp ax_
        fld py
        fadd ay
        fstp ay
        fld pz
        fadd az
        fstp az
        fld pw
        fadd aw
        fstp aw
        fld qx
        fadd bx_
        fstp bx_
        fld qy
        fadd by
        fstp by
        fld qz
        fadd bz
        fstp bz
        fld qw
        fadd bw
        fstp bw
        fld rx
        fadd cx_
        fstp cx_
        fld ry
        fadd st(0), st(2)
        fstp cy
        fld rz
        fadd st(0), st(1)
        fstp cz
        fstp st(0)
        fstp st(0)
        fld rw
        fadd cw
        fstp cw
        fld cx_
        fmul s2
        fld cy
        fmul s2
        fstp cyw
        fld cz
        fmul s2
        fstp czw
        fld bx_
        fmul s1
        fld by
        fmul s1
        fld bz
        fmul s1
        fstp bzw
        fld ax_
        fmul s0
        fld ay
        fmul s0
        fstp ayw
        fld az
        fmul s0
        fstp azw
        fadd st(0), st(2)
        fstp x1
        fld ayw
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld azw
        fadd bzw
        fstp z1
        fadd x1
        fld y1
        fadd cyw
        fld z1
        fadd czw
        fstp out_z
        fxch st(1)
        fstp out_x
        fstp out_y
    }
    out[0] = out_x;
    out[1] = out_y;
    out[2] = out_z;
}

// The vertex formats a scene file's type numbers name (0x00775B64-78; 0: none; past 6: the number itself).
uint32_t vertex_format(uint32_t type)
{
    if (type > 6)
        return type;
    // (type t is the table's word 6 - t)
    return type ? g_vertex_shaders[6 - type] : 0;
}

// The bone-scaled positions written into a vertex buffer (0x004D47F0 / 0x004D4350: the memory loader's and the file loader's: their arithmetic differs in what x87 keeps in
// registers) - only while the skin scaling is on.
void apply_scaled_positions(const std::vector<uint8_t>& records, uint8_t* vertices, uint32_t stride, bool in_memory)
{
    size_t count = records.size() / scene_format::kScaledVertexSize;
    for (size_t i = 0; i < count; ++i) {
        ScaledVertex v;
        std::memcpy(&v, records.data() + i * scene_format::kScaledVertexSize, sizeof(v));
        if (skin_scaling_on()) {
            if (in_memory)
                scaled_position_in_memory(v, reinterpret_cast<float*>(vertices));
            else
                scaled_position_in_file(v, reinterpret_cast<float*>(vertices));
        }
        vertices += stride;
    }
}

// The mesh made from its body record (scene_format.h), as both of the original's loaders do; `in_memory` picks the
// memory loader's arithmetic for the scaled positions. False if the stream failed (nothing is made).
template <typename Stream>
bool read_mesh(MeshRef* mesh, Stream& in, uint32_t material_checksum, SceneRef* scene, VertexArena* arena, bool dynamic)
{
    // Not original - the body read whole and checked first: index lists, vertex buffers, scaled positions,
    // per-vertex bytes and levels past the end, more than 3 vertex buffers (the original's arrays), or scaled
    // positions past their buffer end the load (the original read and wrote past them).
    scene_format::MeshBody body;
    if (!scene_format::read_mesh_body(in, &body) || (body.vertex_types[0] && !scene_format::offsets_fit(body))) {
        // (a vertex's offsets past its stride: the colour animations wrote past the buffer, grass / splats read past)
        debug::printf("scene mesh damaged: cut short or doesn't fit\n");
        in.fail();
        return false;
    }
    for (int i = 0; i < scene_format::kIndexBuffers; ++i) {
        mesh->index_counts_[i] = body.index_counts[i];
        uint16_t count = body.index_counts[i];
        if (!count)
            continue;
        void* memory = alloc_block(sizeof(IdxStore));
        IdxStore* ib = memory ? static_cast<IdxStore*>(memory)->make_new(count * 2u, 0, D3DFMT_INDEX16) : nullptr;
        mesh->index_buffers_[i] = ib;
        std::memcpy(ib->indices, body.indices[i].data(), count * 2u);
        ib->get_unlock();
    }
    mesh->stride = body.stride;
    mesh->vertex_count_ = body.vertex_count;
    mesh->vertex_buffer_count_ = body.vertex_buffer_count;
    if (!body.scaled.empty())
        apply_scaled_positions(body.scaled, body.vertices.data(), mesh->stride, Stream::kInMemory);
    mesh->fvf = vertex_format(body.vertex_types[0]);
    *reinterpret_cast<uint32_t*>(mesh->unknown3c_) = vertex_format(body.vertex_types[1]);
    mesh->unknown1c_[1] = body.offsets[0];
    mesh->unknown1c_[0] = body.offsets[1];
    mesh->unknown1c_[2] = body.offsets[2];
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
    if (material && (material->pass_flags_[0] & 0x10))
        mesh->flags |= 0x100;
    mesh->vertex_buffer_count_ = 1;
    // Not original - the vertices the mesh draws (or, without a vertex format, unpacks) within its buffer: a count
    // past them is cut to what is there (the original read past the buffer).
    uint32_t sets = material ? material->pass_count_ : 0;
    uint32_t vertex_bytes = mesh->fvf ? mesh->stride : 0x20 + sets * 8;
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
    uint32_t sets = reinterpret_cast<MaterialRef*>(material)->pass_count_; // (a texture coordinate set a pass)
    auto* out = static_cast<uint8_t*>(alloc_block(vertex_count_ * 0x4Cu));
    per_vertex_data_ = out;
    uint32_t bones = 1;
    bool suspect = vertex_count_ == 0x280 && index_counts_[0] == 0x6A3; // (a debugging check the original kept)
    auto unpack11 = [](uint32_t bits) {
        return static_cast<int>(bits & 0x400 ? (bits & 0x7FF) - 0x800 : bits & 0x7FF);
    };
    for (int i = 0; i < vertex_count_; ++i) {
        std::memcpy(out, packed, 12); // the position
        if (suspect && i == 0xF7)
            debug::printf("euh");
        uint32_t normal = *reinterpret_cast<const uint32_t*>(packed + 0x18);
        auto* f = reinterpret_cast<float*>(out);
        f[3] = static_cast<float>(unpack11(normal) * static_cast<double>(g_unpack11_scale));
        f[4] = static_cast<float>(unpack11(normal >> 11) * static_cast<double>(g_unpack11_scale));
        uint32_t z = normal >> 22;
        f[5] = static_cast<float>(static_cast<int>(z & 0x200 ? z - 0x400 : z) * static_cast<double>(g_unpack_z_scale));
        std::memcpy(out + 0x18, packed + 0x1C, 4); // the colour
        std::memcpy(out + 0x1C, packed + 0x20, sets * 8);
        for (int k = 0; k < 4; ++k)
            out[0x3C + k] = static_cast<uint8_t>(*reinterpret_cast<const int16_t*>(packed + 0x10 + 2 * k) / 3);
        uint32_t weights = *reinterpret_cast<const uint32_t*>(packed + 0xC);
        f[16] = static_cast<float>(unpack11(weights) * static_cast<double>(g_unpack11_scale));
        f[17] = static_cast<float>(unpack11(weights >> 11) * static_cast<double>(g_unpack11_scale));
        uint32_t w = weights >> 22;
        double third = static_cast<int>(w & 0x200 ? w - 0x400 : w) * static_cast<double>(g_unpack_z_scale);
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
    fvf = g_fvf_by_set_count[sets] | 0x52;
    stride = static_cast<uint8_t>(sets * 8 + 0x1C);
    buffer_flags_ |= 1;
    vertex_buffers_[0] = new_vertex_buffer(stride * vertex_count_, buffer_usage_, buffer_flags_);
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
    if (position_count_ || !index_counts_[0])
        return;
    VertexBuffer* vb = vertex_buffers_[0];
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
    vertex_buffers_[0]->get_unlock();
    const uint16_t* strip = index_buffers_[0]->indices;
    auto* found = static_cast<uint16_t*>(memory::alloc((index_counts_[0] - 2u) * 6));
    uint32_t count = 0;
    uint16_t* out = found;
    for (uint32_t k = 0; k < index_counts_[0] - 2u; ++k, ++strip) {
        uint16_t a = strip[0], b = strip[1], c = strip[2];
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
    index_buffers_[0]->get_unlock();
    if (count) {
        triangles = static_cast<uint16_t*>(alloc_block(count * 6u));
        std::memcpy(triangles, found, count * 6u);
        triangle_count_ = count;
    }
    memory::Hub::get_pull()->dealloc(found);
}

} // namespace render
