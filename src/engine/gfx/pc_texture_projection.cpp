// Texture projections on PC: see pc_texture_projection.h. The maths follows the original's x87 code: double precision
// where it keeps values in registers, floats where it stores them.

#include "engine/gfx/pc_texture_projection.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_texture_projection.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/texture_splats.h"
#include "platform/code_address.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/sys/startup_init.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {
uint32_t g_projection_batch_count;
auto& projection_batches()
{
    static std::array<ProjectionBatch, 10> value; // (kBatchLimit)
    return value;
}


constexpr uint32_t kBatchLimit = 10;
constexpr uint32_t kBatchVertices = 0x600;

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DDevice9* draw_device() { return g_draw_device; } // (the same device, kept twice)
uint32_t& batch_count() { return g_projection_batch_count; }
ProjectionBatch* get_batches() { return projection_batches().data(); }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

void transform_coord(float* out, const float* v, const maths::Mat4* m)
{
    d3dx::vec3_transform_coord(out, v, m); // (D3DX)
}

// The vertex stack (0x006FC4A0) the splitter works on.
uint8_t* vertex_stack() { return raw::bytes(&g_vertex_stack); }
void push_vertex(const uint8_t* vertex) { raw::as<VertexStack>(vertex_stack())->stack_on(vertex); }

float* get_uv(uint8_t* vertex) { return raw::field_at<float>(vertex, 0x10); }
uint32_t& get_colour(uint8_t* vertex) { return *raw::field_at<uint32_t>(vertex, 0xC); }
uint32_t opaque(uint32_t colour) { return (colour & 0xFFFFFF) | 0x80000000; }
float to_uv(float coordinate) { return static_cast<float>((static_cast<double>(coordinate) + 1.0f) * 0.5f); }
float from_uv(float uv)
{
    const double value = uv;
    return static_cast<float>(value + value - 1.0f);
}

// A triangle (three vertices of 0x18 bytes) copied: positions, texture coordinates, then the colours made opaque.
void copy_triangle(uint8_t* into, uint8_t* from)
{
    for (int k = 0; k < 3; ++k)
        std::memcpy(into + 0x18 * k, from + 0x18 * k, 12);
    for (int k = 0; k < 3; ++k)
        std::memcpy(get_uv(into + 0x18 * k), get_uv(from + 0x18 * k), 8);
    for (int k = 0; k < 3; ++k)
        get_colour(into + 0x18 * k) = opaque(get_colour(from + 0x18 * k));
}

} // namespace

// 0x004C7610
ProjectionBatch* ProjectionBatch::make_new()
{
    new (&material) MaterialRef;
    return this;
}

// 0x004C73D0
void ProjectionBatch::teardown() { material.~MaterialRef(); }

// 0x004C73E0
ProjectionBatch* get_projection_batch(uint32_t texture, uint32_t blend_name)
{
    const uint32_t count = batch_count();
    for (uint32_t i = 0; i < count; ++i) {
        if (get_batches()[i].texture == texture)
            return &get_batches()[i];
    }
    if (count >= kBatchLimit) {
        debug::printf("hmmm\n");
        return nullptr;
    }
    ProjectionBatch* batch = &get_batches()[count];
    MaterialRef* material = &batch->material;
    core::at(material->pass_flags_, 0) = 0x44; // (the pass's flags)
    const int a = get_rand();
    const int b = get_rand();
    material->checksum = a * b;                                  // a random checksum
    material->pass_count_ = 1;                                   // one pass
    core::at(material->textures, 0) = cell::pointer<uint8_t>(texture); // (the texture's key: its PC texture's address)
    material->two_sided_ = 1;
    material->depth_bias_ = 4;
    core::at(material->blend_modes_, 0) = blend_mode_from_name(blend_name);
    for (int k = 0; k < 3; ++k)
        core::at(core::at(material->pass_color_, 0), k) = 128.0f; // its colour
    core::at(material->address_modes_, 0) = 0x20002;
    core::at(material->mip_lod_bias_, 0) = 0;
    material->alpha_ref_ = 1;
    material_pixel_shader(material, &batch->pixel_shader_);
    batch->texture = texture;
    batch->triangle_count_ = 0;
    ++batch_count();
    return batch;
}

// 0x004C7520
void draw_projection_batches()
{
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    input_port()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD min_filter = 0;
    input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    for (uint32_t i = 0; i < batch_count(); ++i) {
        ProjectionBatch* batch = &get_batches()[i];
        batch->material.hand_in(); // (the material's set-up)
        set_pixel_shader(batch->pixel_shader_, &batch->material);
        draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch->triangle_count_, batch->vertices.data(), 0x18);
        ++memory::bits_as<uint32_t>(g_draw_calls);                              // (the frame's draw calls)
        memory::bits_as<uint32_t>(g_drawn_triangles) += batch->triangle_count_; // (and triangles)
    }
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    batch_count() = 0;
}

// 0x004C7620
void PcTextureProjection::plat_project(void** sectors, void** /*collision*/)
{
    ProjectionBatch* batch = nullptr;
    uint8_t* batch_vertices = nullptr;
    for (; *sectors; ++sectors) {
        auto* geom = static_cast<PcGeom*>(static_cast<PcSector*>(*sectors)->geom_handle_); // (the sector's geom)
        if (!geom)
            continue;
        MeshRef* const* meshes = geom->meshes;
        for (uint32_t m = 0; m < geom->mesh_count_; ++m) {
            MeshRef* mesh = meshes[m];
            if (mesh->flags & 2)
                continue;
            uint8_t* vertices = nullptr;
            core::at(mesh->vertex_buffers_, 0)->get_lock(0, 0, &vertices, 0x10);
            uint16_t* indices = nullptr;
            core::at(mesh->index_buffers_, 0)->get_lock(0, 0, &indices, 0x10);
            uint32_t a = indices[0], b = indices[1];
            for (uint32_t strip = 2; strip < core::at(mesh->index_counts_, 0); ++strip) {
                const uint32_t c = indices[strip];
                const uint32_t ia = a;
                const uint32_t ib = b;
                const uint32_t ic = c;
                a = b;
                b = c;
                if (ia == ib || ia == ic || ib == ic)
                    continue;
                const uint32_t stride = mesh->stride;
                float t0[3], t1[3], t2[3];
                transform_coord(t0, raw::field_at<const float>(vertices, stride * ia), &projection_);
                transform_coord(t1, raw::field_at<const float>(vertices, stride * ib), &projection_);
                transform_coord(t2, raw::field_at<const float>(vertices, stride * ic), &projection_);
                if (t0[2] < -1.0f && t1[2] < -1.0f && t2[2] < -1.0f) // (wholly behind)
                    continue;
                if (!triangle_overlaps_screen(t0[0], t0[1], t1[0], t1[1], t2[0], t2[1]))
                    continue;
                if (!batch_vertices) {
                    uint8_t* key = *raw::field_at<uint8_t*>(bitmap_, 0x10);
                    if (bitmap_[8] & 1)
                        key = *raw::as<uint8_t*>(key);
                    batch = get_projection_batch(cell::of(key), blend_mode_);
                    batch_vertices =
                        raw::bytes(batch) + 0x120; // (null: written near null, as the original)
                }
                // the triangle written at the batch's next slot (the pieces' scratch: the first piece kept goes there)
                uint32_t count = batch->triangle_count_;
                uint8_t* v0 = batch_vertices + count * 0x48;
                uint8_t* v1 = v0 + 0x18;
                uint8_t* v2 = v0 + 0x30;
                std::memcpy(v0, vertices + stride * ia, 12);
                std::memcpy(v1, vertices + stride * ib, 12);
                std::memcpy(v2, vertices + stride * ic, 12);
                get_uv(v0)[0] = to_uv(t0[0]);
                get_uv(v0)[1] = to_uv(t0[1]);
                get_uv(v1)[0] = to_uv(t1[0]);
                get_uv(v1)[1] = to_uv(t1[1]);
                get_uv(v2)[0] = to_uv(t2[0]);
                get_uv(v2)[1] = to_uv(t2[1]);
                const uint32_t colour_offset = core::at(mesh->unknown1c_, 0);
                get_colour(v0) = opaque(*raw::field_at<uint32_t>(vertices, stride * ia + colour_offset));
                get_colour(v1) = opaque(*raw::field_at<uint32_t>(vertices, stride * ib + colour_offset));
                get_colour(v2) = opaque(*raw::field_at<uint32_t>(vertices, stride * ic + colour_offset));
                std::array<uint8_t, 3 * 0x18> whole{};
                uint8_t* pieces = nullptr;
                uint8_t* end = nullptr;
                uint8_t* format = nullptr;
                if (fineness_) {
                    // how fine to split it: the cross product of its edges, squared, over 16 times the fineness
                    const auto* p0 = raw::as<const float>(v0);
                    const auto* p1 = raw::as<const float>(v1);
                    const auto* p2 = raw::as<const float>(v2);
                    const double e1z = static_cast<double>(p1[2]) - p0[2];
                    const double e1y = static_cast<double>(p1[1]) - p0[1];
                    const double e1x = static_cast<double>(p1[0]) - p0[0];
                    auto e2z = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
                    auto e2y = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
                    const double e2x = static_cast<double>(p2[0]) - p0[0];
                    auto cz = static_cast<float>(e2y * e1x - e2x * e1y);
                    auto cy = static_cast<float>(e2x * e1z - e2z * e1x);
                    const double cx = e2z * e1y - e2y * e1z;
                    const double total = static_cast<double>(cy) * cy + static_cast<double>(cz) * cz + cx * cx;
                    g_split_area_limit = static_cast<float>(total / (static_cast<double>(fineness_) * 16.0f));
                    new (vertex_stack()) VertexStack; // (emptied)
                    g_vertex_stack.vertex_size_ = 0x18;
                    push_vertex(v0);
                    push_vertex(v1);
                    push_vertex(v2);
                    pieces = static_cast<uint8_t*>(memory::alloc(0x2000));
                    std::memset(pieces, 0, 0x2000);
                    void* memory = memory::alloc(0xD8);
                    format = memory ? raw::bytes(memory::construct_in<MeshRef>(memory)) : nullptr;
                    format[0x1C] = 0x0C; // (no check: written through null, as the original)
                    format[0x1E] = 0x10;
                    end = pieces;
                    while (split_next_triangle(&end, raw::as<const SplitFormat>(format)))
                        ;
                } else {
                    std::memcpy(whole.data(), v0, sizeof(whole));
                    pieces = whole.data();
                    end = whole.data() + sizeof(whole);
                }
                // the pieces inside the projection's square, each into the batch's next triangle
                bool full = false;
                if (!fineness_ || pieces != end) {
                    for (uint8_t* piece = pieces; piece != end; piece += 0x48) {
                        if (!triangle_overlaps_screen(
                                from_uv(get_uv(piece)[0]), from_uv(get_uv(piece)[1]), from_uv(get_uv(piece + 0x18)[0]),
                                from_uv(get_uv(piece + 0x18)[1]), from_uv(get_uv(piece + 0x30)[0]),
                                from_uv(get_uv(piece + 0x30)[1])))
                            continue;
                        copy_triangle(batch_vertices + count * 0x48, piece);
                        ++count;
                        if (3 * count + 2 >= kBatchVertices) {
                            full = true;
                            break;
                        }
                    }
                }
                batch->triangle_count_ = count;
                if (!full)
                    batch->blend_name_ = blend_mode_;
                if (fineness_) {
                    if (format) {
                        raw::as<MeshRef>(format)->~MeshRef(); // (its destructor body)
                        memory::Hub::get_pull()->dealloc(format);
                    }
                    memory::Hub::get_pull()->dealloc(pieces);
                }
                if (full)
                    return; // (the buffers left locked, as the original)
            }
            core::at(mesh->index_buffers_, 0)->get_unlock(); // (unlocked)
            core::at(mesh->vertex_buffers_, 0)->get_unlock();
        }
    }
}

} // namespace render

namespace render {

// 0x00640D30 (a static initialiser): the ten projection batches (their materials).
void construct_projection_batches()
{
    for (ProjectionBatch& batch : projection_batches())
        batch.make_new();
}

} // namespace render
