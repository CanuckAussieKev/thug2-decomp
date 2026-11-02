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
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include <bit>

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
void push_vertex(const ProjectedVertex& vertex)
{
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(ProjectedVertex)>>(vertex);
    g_vertex_stack.stack_on(bytes.data());
}

uint32_t opaque(uint32_t colour) { return (colour & 0xFFFFFF) | 0x80000000; }
float to_uv(float coordinate) { return static_cast<float>((static_cast<double>(coordinate) + 1.0f) * 0.5f); }
float from_uv(float uv)
{
    const double value = uv;
    return static_cast<float>(value + value - 1.0f);
}

using Triangle = std::array<ProjectedVertex, 3>;

// A triangle copied: positions, texture coordinates, then the colours made opaque.
void copy_triangle(ProjectedVertex* into, const Triangle& from)
{
    for (int k = 0; k < 3; ++k)
        into[k].position = core::at(from, k).position;
    for (int k = 0; k < 3; ++k)
        into[k].uv = core::at(from, k).uv;
    for (int k = 0; k < 3; ++k)
        into[k].argb = opaque(core::at(from, k).argb);
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
ProjectionBatch* get_projection_batch(TextureImage* texture, uint32_t blend_name)
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
    core::at(material->textures, 0) = texture;
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
        draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch->triangle_count_, batch->vertices.data(),
                                       sizeof(ProjectedVertex));
        ++g_draw_calls;                              // (the frame's draw calls)
        g_drawn_triangles += batch->triangle_count_; // (and triangles)
    }
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    batch_count() = 0;
}

// 0x004C7620
void PcTextureProjection::plat_project(void** sectors, void** /*collision*/)
{
    ProjectionBatch* batch = nullptr;
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
            const gpu::VertexView mesh_vertices(vertices, mesh->stride);
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
                const std::array<float, 3> a0 = mesh_vertices.float3(ia);
                const std::array<float, 3> a1 = mesh_vertices.float3(ib);
                const std::array<float, 3> a2 = mesh_vertices.float3(ic);
                std::array<float, 3> t0{};
                std::array<float, 3> t1{};
                std::array<float, 3> t2{};
                transform_coord(t0.data(), a0.data(), &projection_);
                transform_coord(t1.data(), a1.data(), &projection_);
                transform_coord(t2.data(), a2.data(), &projection_);
                if (core::at(t0, 2) < -1.0f && core::at(t1, 2) < -1.0f && core::at(t2, 2) < -1.0f) // (wholly behind)
                    continue;
                if (!triangle_overlaps_screen(core::at(t0, 0), core::at(t0, 1), core::at(t1, 0), core::at(t1, 1), core::at(t2, 0), core::at(t2, 1)))
                    continue;
                if (!batch) // (null when all ten are taken: the original wrote near null, and so do we)
                    batch = get_projection_batch(image_of(bitmap_), blend_mode_);
                // the triangle written at the batch's next slot (the pieces' scratch: the first piece kept goes there)
                uint32_t count = batch->triangle_count_;
                ProjectedVertex* slot = &core::at(batch->vertices, 3 * count);
                ProjectedVertex& v0 = slot[0];
                ProjectedVertex& v1 = slot[1];
                ProjectedVertex& v2 = slot[2];
                v0.position = a0;
                v1.position = a1;
                v2.position = a2;
                v0.uv = {to_uv(core::at(t0, 0)), to_uv(core::at(t0, 1))};
                v1.uv = {to_uv(core::at(t1, 0)), to_uv(core::at(t1, 1))};
                v2.uv = {to_uv(core::at(t2, 0)), to_uv(core::at(t2, 1))};
                const uint32_t colour_offset = mesh->color_offset_;
                v0.argb = opaque(mesh_vertices.word(ia, colour_offset));
                v1.argb = opaque(mesh_vertices.word(ib, colour_offset));
                v2.argb = opaque(mesh_vertices.word(ic, colour_offset));
                uint8_t* pieces = nullptr;
                uint8_t* end = nullptr;
                MeshRef* format = nullptr;
                if (fineness_) {
                    // how fine to split it: the cross product of its edges, squared, over 16 times the fineness
                    const auto& p0 = v0.position;
                    const auto& p1 = v1.position;
                    const auto& p2 = v2.position;
                    const double e1z = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                    const double e1y = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
                    const double e1x = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
                    auto e2z = static_cast<float>(static_cast<double>(core::at(p2, 2)) - core::at(p0, 2));
                    auto e2y = static_cast<float>(static_cast<double>(core::at(p2, 1)) - core::at(p0, 1));
                    const double e2x = static_cast<double>(core::at(p2, 0)) - core::at(p0, 0);
                    auto cz = static_cast<float>(e2y * e1x - e2x * e1y);
                    auto cy = static_cast<float>(e2x * e1z - e2z * e1x);
                    const double cx = e2z * e1y - e2y * e1z;
                    const double total = static_cast<double>(cy) * cy + static_cast<double>(cz) * cz + cx * cx;
                    g_split_area_limit = static_cast<float>(total / (static_cast<double>(fineness_) * 16.0f));
                    g_vertex_stack = VertexStack{}; // (emptied)
                    g_vertex_stack.vertex_size_ = sizeof(ProjectedVertex);
                    push_vertex(v0);
                    push_vertex(v1);
                    push_vertex(v2);
                    pieces = static_cast<uint8_t*>(memory::alloc(0x2000));
                    std::memset(pieces, 0, 0x2000);
                    void* memory = memory::alloc(sizeof(MeshRef));
                    format = memory ? memory::construct_in<MeshRef>(memory) : nullptr;
                    format->color_offset_ = offsetof(ProjectedVertex, argb); // (no check: as the original)
                    format->uv_offset_ = offsetof(ProjectedVertex, uv);
                    end = pieces;
                    while (split_next_triangle(&end, format))
                        ;
                }
                // the pieces inside the projection's square, each into the batch's next triangle
                bool full = false;
                auto place = [&](const Triangle& piece) {
                    if (!triangle_overlaps_screen(from_uv(core::at(core::at(piece, 0).uv, 0)), from_uv(core::at(core::at(piece, 0).uv, 1)),
                                                  from_uv(core::at(core::at(piece, 1).uv, 0)), from_uv(core::at(core::at(piece, 1).uv, 1)),
                                                  from_uv(core::at(core::at(piece, 2).uv, 0)), from_uv(core::at(core::at(piece, 2).uv, 1))))
                        return;
                    copy_triangle(&core::at(batch->vertices, 3 * count), piece);
                    ++count;
                    full = 3 * count + 2 >= kBatchVertices;
                };
                if (fineness_) {
                    // the pieces (made by the splitter as bytes, a projected vertex each)
                    const gpu::VertexView made(pieces, sizeof(ProjectedVertex));
                    const auto made_count = static_cast<size_t>(end - pieces) / sizeof(ProjectedVertex);
                    for (size_t first = 0; first + 2 < made_count && !full; first += 3)
                        place({{made.value<ProjectedVertex>(first, 0), made.value<ProjectedVertex>(first + 1, 0),
                                made.value<ProjectedVertex>(first + 2, 0)}});
                } else {
                    place({{v0, v1, v2}});
                }
                batch->triangle_count_ = count;
                if (!full)
                    batch->blend_name_ = blend_mode_;
                if (fineness_) {
                    if (format) {
                        format->~MeshRef(); // (its destructor body)
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
