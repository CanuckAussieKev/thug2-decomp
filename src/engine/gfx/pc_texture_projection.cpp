// Texture projections on PC: see pc_texture_projection.h. The maths follows the original's x87 code: double precision
// where it keeps values in registers, floats where it stores them.

#include "engine/gfx/pc_texture_projection.h"
#include "engine/core/cursor.h"
#include "engine/gfx/geom.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <cstring>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/projected_vertex.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/texture_splats.h"
#include "engine/maths/mat4.h"
#include "platform/crt.h"
#include "engine/sys/startup_init.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/effects_state.h"
#include <bit>

namespace render {

namespace {
auto& projection_batches()
{
    static std::array<ProjectionBatch, 10> value; // (kBatchLimit)
    return value;
}

constexpr uint32_t kBatchLimit = 10;
constexpr uint32_t kBatchVertices = 0x600;

uint32_t& batch_count()
{
    static uint32_t projection_batch_count{};
    return projection_batch_count;
}
std::span<ProjectionBatch> get_batches() { return projection_batches(); }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

void transform_coord(float* out, const float* v, const maths::Mat4* m)
{
    d3dx::vec3_transform_coord(out, v, m); // (D3DX)
}

// The vertex stack (0x006FC4A0) the splitter works on.
void push_vertex(const ProjectedVertex& vertex)
{
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(ProjectedVertex)>>(vertex);
    effects_state().vertex_stack().stack_on(bytes.data());
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
void copy_triangle(std::span<ProjectedVertex> into, const Triangle& from)
{
    for (int k = 0; k < 3; ++k)
        core::at(into, k).position = core::at(from, k).position;
    for (int k = 0; k < 3; ++k)
        core::at(into, k).uv = core::at(from, k).uv;
    for (int k = 0; k < 3; ++k)
        core::at(into, k).argb = opaque(core::at(from, k).argb);
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
        if (core::at(get_batches(), i).texture == texture)
            return &core::at(get_batches(), i);
    }
    if (count >= kBatchLimit) {
        debug::printf("hmmm\n");
        return nullptr;
    }
    ProjectionBatch* batch = &core::at(get_batches(), count);
    MaterialRef* material = &batch->material;
    core::at(material->pass_flags(), 0) = 0x44; // (the pass's flags)
    const int a = get_rand();
    const int b = get_rand();
    material->set_checksum(a * b);                                  // a random checksum
    material->set_pass_count(1);                                   // one pass
    core::at(material->textures_value(), 0) = texture;
    material->set_two_sided(1);
    material->set_depth_bias(4);
    core::at(material->blend_modes(), 0) = blend_mode_from_name(blend_name);
    for (int k = 0; k < 3; ++k)
        core::at(core::at(material->pass_color(), 0), k) = 128.0f; // its colour
    core::at(material->address_modes(), 0) = 0x20002;
    core::at(material->mip_lod_bias(), 0) = 0;
    material->set_alpha_ref(1);
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
    renderer().d3d_device()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD min_filter = 0;
    renderer().d3d_device()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    for (uint32_t i = 0; i < batch_count(); ++i) {
        ProjectionBatch* batch = &core::at(get_batches(), i);
        batch->material.hand_in(); // (the material's set-up)
        set_pixel_shader(batch->pixel_shader_, &batch->material);
        renderer().draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch->triangle_count_, batch->vertices.data(),
                                       sizeof(ProjectedVertex));
        renderer().count_draw(batch->triangle_count_); // the frame's draw calls and triangles
    }
    renderer().d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    batch_count() = 0;
}

// 0x004C7620
void PcTextureProjection::plat_project(void** sectors, void** /*collision*/)
{
    ProjectionBatch* batch = nullptr;
    for (void* const sector : core::null_terminated(sectors)) {
        auto* geom = static_cast<PcGeom*>(static_cast<PcSector*>(sector)->geom_handle()); // (the sector's geom)
        if (!geom)
            continue;
        for (MeshRef* const mesh : geom->mesh_run()) {
            if (mesh->flags_value() & 2)
                continue;
            uint8_t* vertices = nullptr;
            core::at(mesh->vertex_buffers(), 0)->get_lock(0, 0, &vertices, 0x10);
            const gpu::VertexView mesh_vertices(vertices, mesh->stride_value(), mesh->vertex_count());
            uint16_t* locked_indices = nullptr;
            core::at(mesh->index_buffers(), 0)->get_lock(0, 0, &locked_indices, 0x10);
            const std::span<const uint16_t> indices(locked_indices, core::at(mesh->index_counts(), 0));
            // (a strip of fewer than two indices has no triangle: the first two are only read, as the original did)
            uint32_t a = indices.size() > 0 ? core::at(indices, 0) : 0u, b = indices.size() > 1 ? core::at(indices, 1) : 0u;
            for (uint32_t strip = 2; strip < core::at(mesh->index_counts(), 0); ++strip) {
                const uint32_t c = core::at(indices, strip);
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
                transform_coord(t0.data(), a0.data(), &projection());
                transform_coord(t1.data(), a1.data(), &projection());
                transform_coord(t2.data(), a2.data(), &projection());
                if (core::at(t0, 2) < -1.0f && core::at(t1, 2) < -1.0f && core::at(t2, 2) < -1.0f) // (wholly behind)
                    continue;
                if (!triangle_overlaps_screen(core::at(t0, 0), core::at(t0, 1), core::at(t1, 0), core::at(t1, 1), core::at(t2, 0), core::at(t2, 1)))
                    continue;
                if (!batch) // (null when all ten are taken: the original wrote near null, and so do we)
                    batch = get_projection_batch(image_of(bitmap()), blend_mode());
                // the triangle written at the batch's next slot (the pieces' scratch: the first piece kept goes there)
                uint32_t count = batch->triangle_count_;
                // (a batch left full by the last projection: the original wrote the triangle past its vertices)
                if (3 * count + 2 >= kBatchVertices)
                    return; // (the buffers left locked, as for a batch filled by this one)
                ProjectedVertex& v0 = core::at(batch->vertices, 3 * count);
                ProjectedVertex& v1 = core::at(batch->vertices, (3 * count) + 1);
                ProjectedVertex& v2 = core::at(batch->vertices, (3 * count) + 2);
                v0.position = a0;
                v1.position = a1;
                v2.position = a2;
                v0.uv = {to_uv(core::at(t0, 0)), to_uv(core::at(t0, 1))};
                v1.uv = {to_uv(core::at(t1, 0)), to_uv(core::at(t1, 1))};
                v2.uv = {to_uv(core::at(t2, 0)), to_uv(core::at(t2, 1))};
                const uint32_t colour_offset = mesh->color_offset();
                v0.argb = opaque(mesh_vertices.word(ia, colour_offset));
                v1.argb = opaque(mesh_vertices.word(ib, colour_offset));
                v2.argb = opaque(mesh_vertices.word(ic, colour_offset));
                uint8_t* pieces = nullptr;
                core::Cursor<uint8_t> end;
                MeshRef* format = nullptr;
                if (fineness()) {
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
                    effects_state().set_split_area_limit(static_cast<float>(total / (static_cast<double>(fineness()) * 16.0f)));
                    effects_state().vertex_stack() = VertexStack{}; // (emptied)
                    effects_state().vertex_stack().vertex_size_ = sizeof(ProjectedVertex);
                    push_vertex(v0);
                    push_vertex(v1);
                    push_vertex(v2);
                    pieces = static_cast<uint8_t*>(memory::alloc(0x2000));
                    std::memset(pieces, 0, 0x2000);
                    void* memory = memory::alloc(sizeof(MeshRef));
                    format = memory ? memory::construct_in<MeshRef>(memory) : nullptr;
                    format->set_color_offset(offsetof(ProjectedVertex, argb)); // (no check: as the original)
                    format->set_uv_offset(offsetof(ProjectedVertex, uv));
                    end = core::Cursor<uint8_t>(std::span<uint8_t>(pieces, 0x2000));
                    while (split_next_triangle(end, format))
                        ;
                }
                // the pieces inside the projection's square, each into the batch's next triangle
                bool full = false;
                auto place = [&](const Triangle& piece) {
                    if (!triangle_overlaps_screen(from_uv(core::at(core::at(piece, 0).uv, 0)), from_uv(core::at(core::at(piece, 0).uv, 1)),
                                                  from_uv(core::at(core::at(piece, 1).uv, 0)), from_uv(core::at(core::at(piece, 1).uv, 1)),
                                                  from_uv(core::at(core::at(piece, 2).uv, 0)), from_uv(core::at(core::at(piece, 2).uv, 1))))
                        return;
                    copy_triangle(std::span<ProjectedVertex>(batch->vertices).subspan(3 * static_cast<size_t>(count), 3), piece);
                    ++count;
                    full = 3 * count + 2 >= kBatchVertices;
                };
                if (fineness()) {
                    // the pieces (made by the splitter as bytes, a projected vertex each)
                    const auto made_count = static_cast<size_t>(end.position()) / sizeof(ProjectedVertex);
                    const gpu::VertexView made(pieces, sizeof(ProjectedVertex), made_count);
                    for (size_t first = 0; first + 2 < made_count && !full; first += 3)
                        place({{made.value<ProjectedVertex>(first, 0), made.value<ProjectedVertex>(first + 1, 0),
                                made.value<ProjectedVertex>(first + 2, 0)}});
                } else {
                    place({{v0, v1, v2}});
                }
                batch->triangle_count_ = count;
                if (!full)
                    batch->blend_name_ = blend_mode();
                if (fineness()) {
                    if (format) {
                        format->~MeshRef(); // (its destructor body)
                        memory::Hub::get_pull()->dealloc(format);
                    }
                    memory::Hub::get_pull()->dealloc(pieces);
                }
                if (full)
                    return; // (the buffers left locked, as the original)
            }
            core::at(mesh->index_buffers(), 0)->get_unlock(); // (unlocked)
            core::at(mesh->vertex_buffers(), 0)->get_unlock();
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
