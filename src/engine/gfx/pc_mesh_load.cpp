// The PC meshes' loading (sub_mesh.h): a mesh's index buffers, vertex buffers, colours, levels of detail, material and
// pixel shader from a scene file (in memory or read from a file); the skinned vertices unpacked; the vertex positions
// a skinned model's bone scales move (while set_skin_scaling's are on); the collision positions and triangles.

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <utility>
#include <array>
#include <gsl/pointers>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/span_copy.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/scene_file_stream.h"
#include "formats/scene_format.h"
#include "engine/gfx/simple_material.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "gpu/vertex_view.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/mesh_pass.h"

namespace render {

constexpr std::size_t kFvfTableSize = 5; // 0 to 4 sets of texture coordinates
constexpr int kShaderTypeLimit = 6;
constexpr int kPassFlagCopiedToMesh = 0x10;
constexpr int kMeshFlagCopiedFromPass = 0x100;
constexpr unsigned kUnpackedVertexBytes = 0x4CU;
constexpr unsigned kSignBit11 = 0x400; // a normal's components: 11, 11 and 10 bits, two's complement
constexpr unsigned kMask11 = 0x7FF;
constexpr unsigned kRange11 = 0x800;
constexpr unsigned kSignBit10 = 0x200;
constexpr unsigned kRange10 = 0x400;
constexpr int kBitsPer11 = 11;
constexpr int kTraceVertex = 0xF7; // the vertex a debug message is printed for
// The offsets in the packed and unpacked vertex.
constexpr uint32_t kOutNormalOffset = 0xC;
constexpr uint32_t kOutColourOffset = 0x18;
constexpr uint32_t kInColourOffset = 0x1C;
constexpr uint32_t kOutUvOffset = 0x1C;
constexpr uint32_t kInUvOffset = 0x20;
constexpr uint32_t kUvSetStride = 8;
constexpr uint32_t kOutBoneValuesOffset = 0x3C;
constexpr uint32_t kInBoneValuesOffset = 0x10;
constexpr uint32_t kOutWeightsOffset = 0x40;
constexpr uint32_t kFvfBase = 0x52; // D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE
constexpr uint32_t kBaseStride = 0x1C;
constexpr unsigned kAlignMask = 0x1FU;
constexpr unsigned kPositionBytesU = 12U;
constexpr unsigned kTriangleIndexBytes = 6;
constexpr unsigned kTriangleIndexBytesU = 6U;

namespace {
constexpr float kUnpackZScale{0.0019569471F};
constexpr float kUnpack11Scale{0.0009775171F};
std::array<uint32_t, kFvfTableSize>& fvf_by_set_count()
{
    static std::array<uint32_t, kFvfTableSize> fvf_by_set_count{{0, kFvfTex1, kFvfTex2, kFvfTex3, kFvfTex4}};
    return fvf_by_set_count;
}
} // namespace

namespace {

gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// A vertex's bone-scaled position record (0x84 bytes): 12 bytes unused, the three bones' weights, then each bone's
// position (P, Q, R) and the vertex's offset from it (A, B, C), four floats each, and the three bones (-1: none).
struct ScaledVertex {
    std::array<uint32_t, 3> unused;
    std::array<float, 3> weights;
    std::array<float, 4> p;
    std::array<float, 4> q;
    std::array<float, 4> r;
    std::array<float, 4> a;
    std::array<float, 4> b;
    std::array<float, 4> c;
    std::array<int, 3> bones;
};
static_assert(sizeof(ScaledVertex) == scene_format::kScaledVertexSize, "file format: a bone-scaled position record");

bool skin_scaling_on() { return (mesh_pass().skin_scaling_on() != 0U) && !mesh_pass().skin_settings().colours.empty(); }

// A bone's scale (four floats: x, then z, then y as the loaders apply them, and w); 1 without a bone.
void bone_scale_terms(int bone, float& e0, float& e4, float& e8, float& e12)
{
    // (a bone outside the table - the original read the memory after it - scales nothing, like none)
    if (bone < 0 || (4 * static_cast<std::size_t>(bone)) + 4 > mesh_pass().skin_settings().colours.size()) {
        e0 = e4 = e8 = e12 = 1.0F;
        return;
    }
    const std::span<const float> scale = mesh_pass().skin_settings().colours.subspan(4 * static_cast<std::size_t>(bone), 4);
    e0 = core::at(scale, 0);
    e4 = core::at(scale, 1);
    e8 = core::at(scale, 2);
    e12 = core::at(scale, 3);
}

// A vertex's position as the bone scales move it: each bone's offset scaled - x by the scale's first value, y by its
// third, z by its second - and moved by the bone's position, then blended by the weights. (The original's memory and
// file loaders compute it in x87 code that rounds different sums and products to float; in plain float arithmetic
// they are the same formula.)
std::array<float, 3> scaled_position(const ScaledVertex& v)
{
    std::array<float, 4> e0{};
    std::array<float, 4> e1{};
    std::array<float, 4> e2{};
    bone_scale_terms(core::at(v.bones, 0), core::at(e0, 0), core::at(e0, 1), core::at(e0, 2), core::at(e0, 3));
    bone_scale_terms(core::at(v.bones, 1), core::at(e1, 0), core::at(e1, 1), core::at(e1, 2), core::at(e1, 3));
    bone_scale_terms(core::at(v.bones, 2), core::at(e2, 0), core::at(e2, 1), core::at(e2, 2), core::at(e2, 3));
    const float ax = core::at(v.p, 0) + (core::at(e0, 0) * core::at(v.a, 0));
    const float ay = core::at(v.p, 1) + (core::at(e0, 2) * core::at(v.a, 1));
    const float az = core::at(v.p, 2) + (core::at(e0, 1) * core::at(v.a, 2));
    const float bx = core::at(v.q, 0) + (core::at(e1, 0) * core::at(v.b, 0));
    const float by = core::at(v.q, 1) + (core::at(e1, 2) * core::at(v.b, 1));
    const float bz = core::at(v.q, 2) + (core::at(e1, 1) * core::at(v.b, 2));
    const float cx = core::at(v.r, 0) + (core::at(e2, 0) * core::at(v.c, 0));
    const float cy = core::at(v.r, 1) + (core::at(e2, 2) * core::at(v.c, 1));
    const float cz = core::at(v.r, 2) + (core::at(e2, 1) * core::at(v.c, 2));
    const auto& s = v.weights;
    return {(ax * core::at(s, 0)) + (bx * core::at(s, 1)) + (cx * core::at(s, 2)),
            (ay * core::at(s, 0)) + (by * core::at(s, 1)) + (cy * core::at(s, 2)),
            (az * core::at(s, 0)) + (bz * core::at(s, 1)) + (cz * core::at(s, 2))};
}

// The vertex formats a scene file's type numbers name (0x00775B64-78; 0: none; past 6: the number itself).
uint32_t vertex_format(uint32_t type)
{
    if (type > kShaderTypeLimit) {
        return type;
    }
    // (type t is the table's word 6 - t)
    return (type != 0U) ? core::at(mesh_pass().vertex_shaders(), kShaderTypeLimit - type) : 0;
}

// The bone-scaled positions written into a vertex buffer (0x004D47F0 / 0x004D4350: the memory loader's and the file
// loader's) - only while the skin scaling is on.
void apply_scaled_positions(const std::vector<uint8_t>& records, uint8_t* vertices, uint32_t stride)
{
    const size_t count = records.size() / scene_format::kScaledVertexSize;
    const gpu::VertexWriter out(vertices, stride, count);
    for (size_t i = 0; i < count; ++i) {
        ScaledVertex v{};
        std::memcpy(&v, std::span(records).subspan(i * scene_format::kScaledVertexSize, sizeof(v)).data(), sizeof(v));
        if (skin_scaling_on()) {
            out.set_floats<3>(i, 0, scaled_position(v));
        }
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
        core::at(mesh->index_counts(), i) = core::at(body.index_counts, i);
        const uint16_t count = core::at(body.index_counts, i);
        if (!count) {
            continue;
        }
        gsl::owner<void*> memory = alloc_block(sizeof(IdxStore));
        IdxStore* ib = memory ? memory::construct_in<IdxStore>(memory, count * 2U, 0, D3DFMT_INDEX16) : nullptr;
        core::at(mesh->index_buffers(), i) = ib;
        std::memcpy(ib->indices(), core::at(body.indices, i).data(), count * 2U);
        ib->get_unlock();
    }
    mesh->set_stride(body.stride);
    mesh->set_vertex_count(body.vertex_count);
    mesh->set_vertex_buffer_count(body.vertex_buffer_count);
    if (!body.scaled.empty()) {
        apply_scaled_positions(body.scaled, body.vertices.data(), mesh->stride());
    }
    mesh->set_fvf(vertex_format(core::at(body.vertex_types, 0)));
    mesh->set_second_fvf(vertex_format(core::at(body.vertex_types, 1)));
    mesh->set_normal_offset(core::at(body.offsets, 0));
    mesh->set_color_offset(core::at(body.offsets, 1));
    mesh->set_uv_offset(core::at(body.offsets, 2));
    mesh->set_buffer_usage(0);
    mesh->buffer_flags() = 0;
    if (body.has_vertex_bytes) {
        mesh->set_per_vertex_bytes(static_cast<uint8_t*>(memory::alloc(mesh->vertex_count())));
        std::memcpy(mesh->per_vertex_bytes(), body.vertex_bytes.data(), body.vertex_bytes.size());
        mesh->buffer_flags() |= 2;
        arena = nullptr; // (written each frame: a buffer of its own)
        mesh->set_buffer_usage(D3DUSAGE_DYNAMIC);
    }
    if (dynamic) {
        mesh->set_buffer_usage(D3DUSAGE_DYNAMIC);
    }
    if (body.level_count > 1) {
        mesh->set_unknown48(memory::alloc(static_cast<uint32_t>(body.level_count) * 4));
        std::memcpy(mesh->unknown48(), body.levels.data(), body.levels.size() * 4);
    }
    auto* material = static_cast<MaterialRef*>(scene->find_material(material_checksum));
    mesh->set_material(material);
    if (material && (core::at(material->pass_flags(), 0) & kPassFlagCopiedToMesh)) {
        mesh->flags() |= kMeshFlagCopiedFromPass;
    }
    mesh->set_vertex_buffer_count(1);
    // Not original - the vertices the mesh draws (or, without a vertex format, unpacks) within its buffer: a count
    // past them is cut to what is there (the original read past the buffer).
    const uint32_t sets = material ? material->pass_count() : 0;
    const uint32_t vertex_bytes = mesh->fvf() ? mesh->stride() : 0x20 + (sets * 8);
    if (!scene_format::vertices_fit(body, vertex_bytes)) {
        uint32_t fit = 0;
        if (body.vertices.size() >= vertex_bytes) {
            fit = mesh->stride() ? static_cast<uint32_t>((body.vertices.size() - vertex_bytes) / mesh->stride()) + 1 : 0;
        }
        debug::printf("scene mesh damaged: %u vertices, %u fit their buffer\n", mesh->vertex_count(), fit);
        mesh->set_vertex_count(static_cast<uint16_t>(fit));
    }
    auto size = static_cast<uint32_t>(body.vertices.size());
    if (mesh->fvf()) {
        mesh->create_vertex_buffer(body.vertices.data(), size, arena);
    } else if (material) {
        mesh->set_buffer_usage(D3DUSAGE_DYNAMIC);
        mesh->set_up_skinning(body.vertices.data(), size, nullptr);
    } else {
        // Not original - no material to unpack with (the original read it through null).
        debug::printf("scene mesh damaged: a skinned mesh without its material %08x\n", material_checksum);
        mesh->set_buffer_usage(D3DUSAGE_DYNAMIC);
        mesh->set_vertex_count(0);
    }
    if (body.shader == 1) {
        mesh->pixel_shader() = nullptr; // (the chunk skipped)
        if (renderer().shaders_available()) {
            material_pixel_shader(material, &mesh->pixel_shader());
        }
    } else {
        mesh->pixel_shader() = pixel_shader_from_index(body.shader);
    }
    return true;
}

} // namespace

// 0x004D5940
void MeshRef::set_up_skinning(const uint8_t* packed, uint32_t bytes, VertexArena* arena)
{
    const uint32_t sets = material_->pass_count(); // (a texture coordinate set a pass)
    auto* out_bytes = static_cast<uint8_t*>(alloc_block(vertex_count_ * kUnpackedVertexBytes));
    per_vertex_data_ = out_bytes;
    // Each vertex: the position, the normal (float3s), the colour, the texture coordinates (a float2 a set), the
    // four bone indexes (bytes, at 0x3C) and the weights (float3 at 0x40) - from the packed vertex's.
    // (the packed vertices: the mesh body's vertex bytes - the last vertex's texture coordinates may run past its
    // stride into them)
    const gpu::VertexView in(std::span<const uint8_t>(packed, bytes), stride_);
    const gpu::VertexWriter out(out_bytes, 0x4C, vertex_count_);
    uint32_t bones = 1;
    const bool suspect = vertex_count_ == 0x280 && core::at(index_counts_, 0) == 0x6A3; // (a debugging check the original kept)
    auto unpack11 = [](uint32_t bits) {
        return static_cast<int>(bits & kSignBit11 ? (bits & kMask11) - kRange11 : bits & kMask11);
    };
    for (int i = 0; std::cmp_less(i, vertex_count_); ++i) {
        const auto n = static_cast<size_t>(i);
        out.set_floats<3>(n, 0, in.float3(n, 0)); // the position
        if (suspect && i == kTraceVertex) {
            debug::printf("euh");
        }
        const uint32_t normal = in.word(n, 0x18);
        const uint32_t z = normal >> 22;
        out.set_floats<3>(n, kOutNormalOffset,
                          {static_cast<float>(unpack11(normal) * static_cast<double>(kUnpack11Scale)),
                           static_cast<float>(unpack11(normal >> kBitsPer11) * static_cast<double>(kUnpack11Scale)),
                           static_cast<float>(static_cast<int>(((z & kSignBit10) != 0U) ? z - kRange10 : z) *
                                              static_cast<double>(kUnpackZScale))});
        out.set_word(n, kOutColourOffset, in.word(n, kInColourOffset)); // the colour
        for (uint32_t set = 0; set < sets; ++set) {
            out.set_floats<2>(n, kOutUvOffset + (kUvSetStride * set), in.float2(n, kInUvOffset + (kUvSetStride * set)));
        }
        for (size_t k = 0; k < 4; ++k) {
            out.set_value(n, kOutBoneValuesOffset + k,
                          static_cast<uint8_t>(in.value<int16_t>(n, kInBoneValuesOffset + (2 * k)) / 3));
        }
        const uint32_t weights = in.word(n, 0xC);
        const uint32_t w = weights >> 22;
        const double third = static_cast<int>(((w & 0x200) != 0U) ? w - 0x400 : w) * static_cast<double>(kUnpackZScale);
        const auto second = static_cast<float>(unpack11(weights >> 11) * static_cast<double>(kUnpack11Scale));
        out.set_floats<3>(n, kOutWeightsOffset,
                          {static_cast<float>(unpack11(weights) * static_cast<double>(kUnpack11Scale)), second,
                           static_cast<float>(third)});
        if (bones < 2 && !(second == 0.0F)) {
            bones = 2;
        }
        if (bones < 3 && !(third == 0.0)) {
            bones = 3;
        }
    }
    unknowna4_ = sets;
    unknowna8_ = bones;
    fvf_ = core::at(fvf_by_set_count(), sets) | kFvfBase;
    stride_ = static_cast<uint8_t>((sets * kUvSetStride) + kBaseStride);
    buffer_flags_ |= 1;
    gsl::owner<VertexBuffer*> made = new_vertex_buffer(stride_ * vertex_count_, buffer_usage_, buffer_flags_); // (the mesh's)
    core::at(vertex_buffers_, 0) = made;
    if (arena) {
        arena->size += ((stride_ * vertex_count_) + kAlignMask) & ~kAlignMask;
    }
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
    if ((position_count_ != 0U) || (core::at(index_counts_, 0) == 0U)) {
        return;
    }
    VertexBuffer* vb = core::at(vertex_buffers_, 0);
    const gpu::VertexView vertices(vb->data(), stride_, vertex_count_); // (locked read-only, inline)
    vb->set_lock_offset(0);
    vb->set_lock_size(vb->size());
    vb->set_lock_flags(D3DLOCK_READONLY);
    position_count_ = vertex_count_;
    positions_ = static_cast<float*>(alloc_block(vertex_count_ * kPositionBytesU));
    const std::span<float> position_run(positions_, 3U * vertex_count_);
    for (uint32_t i = 0; i < vertex_count_; ++i) {
        std::ranges::copy(vertices.float3(i), position_run.subspan(3U * i, 3).begin());
    }
    core::at(vertex_buffers_, 0)->get_unlock();
    const std::span<uint16_t> strip = core::at(index_buffers_, 0)->index_run();
    auto* found_block = static_cast<uint16_t*>(memory::alloc((core::at(index_counts_, 0) - 2U) * kTriangleIndexBytes));
    const std::span<uint16_t> found(found_block, (core::at(index_counts_, 0) - 2U) * 3U);
    uint32_t count = 0;
    for (uint32_t k = 0; k < core::at(index_counts_, 0) - 2U; ++k) {
        const uint16_t a = core::at(strip, k);
        const uint16_t b = core::at(strip, k + 1);
        const uint16_t c = core::at(strip, k + 2);
        if (a == b || b == c || c == a) {
            continue;
        }
        const std::span<uint16_t> out = found.subspan(3U * count, 3);
        if ((k & 1) != 0U) { // (every other triangle of a strip winds the other way)
            core::at(out, 0) = b;
            core::at(out, 1) = a;
        } else {
            core::at(out, 0) = a;
            core::at(out, 1) = b;
        }
        core::at(out, 2) = c;
        ++count;
    }
    core::at(index_buffers_, 0)->get_unlock();
    if (count != 0U) {
        triangles_ = static_cast<uint16_t*>(alloc_block(count * kTriangleIndexBytesU));
        core::copy_into(std::span<const uint16_t>(found.first(3U * count)),
                        std::span<uint16_t>(triangles_, 3U * count));
        triangle_count_ = count;
    }
    memory::Hub::dealloc(found_block);
}

} // namespace render
