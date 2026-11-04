// Shattering on PC: see shatter.h.

#include "engine/gfx/shatter.h"
#include "engine/gfx/engine_shatter.h"
#include "base/core/cursor.h"
#include "engine/gfx/geom.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <span>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/texture_splats.h"
#include "base/lists/object_set.h"
#include "base/maths/mat4.h"
#include "gpu/vertex_buffers.h"
#include "gpu/vertex_view.h"
#include "platform/crt.h"
#include "base/core/checked_index.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/effects_state.h"
#include "base/maths/vec4.h"
#include <gsl/pointers>

namespace render {

constexpr int kPositionBytes = 12;
constexpr float kAreaScale = 0.0625F;
constexpr float kThird = 3.0F;
constexpr float kMaxSpin = 0.1F;

namespace {

uint16_t triangle_count(const PcShatter* shatter) { return shatter->triangle_count(); }

// A mesh's fields the drawing uses: its vertex format (+0x38) and pixel shader (+0x44).
uint32_t mesh_vertex_format(const MeshRef* mesh) { return mesh->fvf(); }
void* mesh_pixel_shader(const MeshRef* mesh) { return mesh->pixel_shader(); }

} // namespace

// 0x004B8120
PcShatter::PcShatter(int count, MeshRef* source_mesh, uint32_t own)
    : Shatter(count), own_material_(own), mesh_(source_mesh),
      vertices_(static_cast<uint8_t*>(memory::alloc(3 * source_mesh->stride() * count)))
{
    make_positions_and_material(count);
}

void PcShatter::make_positions_and_material(int count)
{
    positions = static_cast<float*>(memory::alloc(kPositionBytes * count));
    if (own_material_ != 0U) {
        void* memory = memory::alloc(sizeof(MaterialRef));
        material = memory ? memory::construct_in<MaterialRef>(memory) : nullptr;
    } else {
        material = nullptr;
    }
}

// 0x004B8FA0
PcShatter::~PcShatter()
{
    memory::Hub::dealloc(vertices_);
    if (MaterialRef* own = material) {
        own->~MaterialRef(); // (the material's destructor body)
        memory::Hub::dealloc(own);
    }
    if (positions) {
        memory::Hub::dealloc(positions);
    }
}

// 0x004B80B0
void draw_shatter(PcShatter* shatter)
{
    const MeshRef* mesh = shatter->mesh();
    mesh->material()->hand_in(); // (the material's set-up)
    set_pixel_shader(mesh_pixel_shader(mesh), mesh->material());
    set_vertex_format(mesh_vertex_format(mesh));
    renderer().count_draw(triangle_count(shatter)); // the frame's draw calls and triangles
    d3d9::state().draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangle_count(shatter), shatter->vertices(), mesh->stride());
}

// 0x004B9550
void kill_all_shatters()
{
    effects_state().shatters().for_each([](Shatter* shatter) {
        effects_state().shatters().erase(shatter);
        gsl::owner<Shatter*> owned = memory::owned_element(shatter); // (the set's)
        delete owned;
    });
}

namespace {

// x' = (z m20 + y m10) + x m00 and so on, each rounded to a float (the original's order of operations).
void get_turn(const maths::Mat4& matrix, double x, double y, double z, std::span<float, 3> out)
{
    for (int k = 0; k < 3; ++k) {
        core::at(out, k) = static_cast<float>((z * core::at(matrix.row, 2)[k]) + (y * core::at(matrix.row, 1)[k]) +
                                              (x * core::at(matrix.row, 0)[k]));
    }
}

int get_rand() { return crt::get_rand(); }                          // (the game's C runtime's)
constexpr double kPerRand = std::bit_cast<float>(0x38000100U);      // 0x00646510 (1 / 32767)
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100U); // 0x0064930C (2 / 32767)

// The vertex stack the splitter works from (0x006FC4A0; not rewritten yet: pushed onto).
void push_vertex(const uint8_t* vertex)
{
    effects_state().vertex_stack().stack_on(vertex);
}
uint8_t* lock_vertices(VertexBuffer* buffer)
{
    uint8_t* data = nullptr;
    buffer->get_lock(0, 0, &data, kLockDiscard);
    return data;
}
uint16_t* lock_indices(IdxStore* buffer)
{
    uint16_t* data = nullptr;
    buffer->get_lock(0, 0, &data, kLockDiscard);
    return data;
}

} // namespace

// 0x004B95E0
void update_shatter(PcShatter* shatter, float step)
{
    const uint32_t stride = shatter->mesh()->stride();
    const gpu::VertexWriter out(shatter->vertices(), stride, 3 * static_cast<size_t>(shatter->triangle_count()));
    const gpu::VertexView in = out.view();
    for (int i = 0; std::cmp_less(i, shatter->triangle_count()); ++i) {
        const auto v0 = static_cast<size_t>(3 * i);
        const size_t v1 = v0 + 1;
        const size_t v2 = v0 + 2;
        const maths::Vec4& centre = core::at(shatter->centre_run(), i);
        const maths::Mat4& matrix = core::at(shatter->rotation_run(), i);
        const auto p0 = in.float3(v0);
        const auto p1 = in.float3(v1);
        const auto p2 = in.float3(v2);
        // (the first vertex's offsets stay in registers, the others' are stored)
        std::array<double, 3> d0{};
        std::array<double, 3> d1{};
        std::array<double, 3> d2{};
        for (int k = 0; k < 3; ++k) {
            core::at(d0, k) = static_cast<double>(core::at(p0, k)) - centre[k];
            core::at(d1, k) = static_cast<float>(static_cast<double>(core::at(p1, k)) - centre[k]);
            core::at(d2, k) = static_cast<float>(static_cast<double>(core::at(p2, k)) - centre[k]);
        }
        std::array<float, 3> r0{};
        std::array<float, 3> r1{};
        std::array<float, 3> r2{};
        get_turn(matrix, core::at(d0, 0), core::at(d0, 1), core::at(d0, 2), r0);
        get_turn(matrix, core::at(d1, 0), core::at(d1, 1), core::at(d1, 2), r1);
        get_turn(matrix, core::at(d2, 0), core::at(d2, 1), core::at(d2, 2), r2);
        static_cast<Shatter*>(shatter)->Shatter::get_step({.index = i, .step = step}); // (the centre moved)
        const maths::Vec4& moved = core::at(shatter->centre_run(), i);
        std::array<float, 3> q0{};
        std::array<float, 3> q1{};
        std::array<float, 3> q2{};
        for (int k = 0; k < 3; ++k) {
            core::at(q0, k) = static_cast<float>(static_cast<double>(core::at(r0, k)) + moved[k]);
            core::at(q1, k) = static_cast<float>(static_cast<double>(core::at(r1, k)) + moved[k]);
            core::at(q2, k) = static_cast<float>(static_cast<double>(core::at(r2, k)) + moved[k]);
        }
        out.set_floats(v0, 0, q0);
        out.set_floats(v1, 0, q1);
        out.set_floats(v2, 0, q2);
    }
    // the normals turned too
    const uint8_t normal_offset = shatter->mesh()->normal_offset();
    if (normal_offset == 0U) {
        return;
    }
    for (int i = 0; std::cmp_less(i, shatter->triangle_count()); ++i) {
        const maths::Mat4& matrix = core::at(shatter->rotation_run(), i);
        for (int corner = 0; corner < 3; ++corner) {
            const auto vertex = static_cast<size_t>((3 * i) + corner);
            const auto normal = in.float3(vertex, normal_offset);
            std::array<float, 3> turned{};
            get_turn(matrix, core::at(normal, 0), core::at(normal, 1), core::at(normal, 2), turned);
            out.set_floats(vertex, normal_offset, turned);
        }
    }
}

// 0x004BA7A0
void shatter_geom(PcGeom* geom)
{
    for (MeshRef* const mesh : geom->mesh_run()) {
        if (core::at(mesh->index_counts(), 0) < 3) {
            continue;
        }
        const uint32_t stride = mesh->stride();
        effects_state().vertex_stack().set_vertex_size(stride); // (the vertex stack's vertex size)
        VertexBuffer* vertex_buffer = mesh->fetch_vertex_store(0);
        const uint8_t* vertices = lock_vertices(vertex_buffer);
        const gpu::VertexView view(vertices, stride, mesh->vertex_count());
        const std::span<const uint16_t> indices(lock_indices(core::at(mesh->index_buffers(), 0)), core::at(mesh->index_counts(), 0));
        // the strip's triangles onto the stack, counted with what their splitting will make of them
        int count = 0;
        uint32_t first = core::at(indices, 0);
        uint32_t second = core::at(indices, 1);
        for (uint32_t k = 2; k < core::at(mesh->index_counts(), 0); ++k) {
            const uint32_t third = core::at(indices, k);
            if (first != second && first != third && second != third) {
                ++count;
                const auto pa = view.float3(first);
                const auto pb = view.float3(second);
                const auto pc = view.float3(third);
                push_vertex(view.vertex_bytes(first).data());
                push_vertex(view.vertex_bytes(second).data());
                push_vertex(view.vertex_bytes(third).data());
                const double e1x = static_cast<double>(core::at(pb, 0)) - core::at(pa, 0);
                const double e1y = static_cast<double>(core::at(pb, 1)) - core::at(pa, 1);
                const double e1z = static_cast<double>(core::at(pb, 2)) - core::at(pa, 2);
                auto e2zf = static_cast<float>(static_cast<double>(core::at(pc, 2)) - core::at(pa, 2));
                auto e2yf = static_cast<float>(static_cast<double>(core::at(pc, 1)) - core::at(pa, 1));
                const double e2x = static_cast<double>(core::at(pc, 0)) - core::at(pa, 0);
                auto cz = static_cast<float>((e2yf * e1x) - (e2x * e1y));
                auto cy = static_cast<float>((e2x * e1z) - (e2zf * e1x));
                const double cx = (e2zf * e1y) - (e2yf * e1z);
                double area = (cx * cx) + (static_cast<double>(cy) * cy) + (static_cast<double>(cz) * cz);
                const double limit = effects_state().split_area_limit();
                if (area > limit) {
                    int pieces = 1;
                    for (bool again = true; again; again = area > limit) {
                        area *= kAreaScale; // (0x00645A84)
                        pieces <<= 2;
                    }
                    count += pieces - 1;
                }
            }
            first = second;
            second = third;
        }
        if (count == 0) {
            continue; // (the buffers left locked, as the original)
        }
        auto* shatter = memory::make_owned<PcShatter>(count, mesh, 0);
        // the centre they fly from: the mesh's, moved back along the shatter direction (its length the base speed)
        const maths::Vec4& direction = effects_state().glass_burst_velocity();
        const double scale = -static_cast<double>(effects_state().shatter_velocity_scale());
        const double back_x = scale * direction[0];
        auto back_y = static_cast<float>(scale * direction[1]);
        auto back_z = static_cast<float>(scale * direction[2]);
        auto back_w = static_cast<float>(scale * direction[3]);
        auto speed = static_cast<float>(std::sqrt((static_cast<double>(direction[1]) * direction[1]) +
                                                  (static_cast<double>(direction[2]) * direction[2]) +
                                                  (static_cast<double>(direction[0]) * direction[0])));
        const std::array<float, 4> from = {
            {static_cast<float>(core::at(mesh->centre(), 0) + back_x),
             static_cast<float>(static_cast<double>(core::at(mesh->centre(), 1)) + back_y),
             static_cast<float>(static_cast<double>(core::at(mesh->centre(), 2)) + back_z),
             static_cast<float>(static_cast<double>(back_w) + 1.0F)}};
        effects_state().shatters().insert(shatter);
        core::Cursor<uint8_t> cursor(std::span<uint8_t>(shatter->vertices(), 3 * static_cast<size_t>(stride) * static_cast<size_t>(count)));
        while (split_next_triangle(cursor, mesh)) {
            ;
        }
        const auto piece_count = static_cast<size_t>(cursor.position()) / stride;
        const gpu::VertexView pieces(shatter->vertices(), stride, piece_count);
        int piece = 0;
        for (size_t vertex_index = 0; vertex_index < piece_count; vertex_index += 3, ++piece) {
            maths::Vec4& centre = core::at(shatter->centre_run(), piece);
            const auto q0 = pieces.float3(vertex_index);
            const auto q1 = pieces.float3(vertex_index + 1);
            const auto q2 = pieces.float3(vertex_index + 2);
            for (int k = 0; k < 3; ++k) {
                centre[k] = static_cast<float>(
                    (static_cast<double>(core::at(q2, k)) + core::at(q1, k) + core::at(q0, k)) * (1.0F / kThird));
            }
            // a velocity away from the centre at a random speed
            auto pace = static_cast<float>(
                (get_rand() * static_cast<double>(effects_state().shatter_speed()) * kPerRand * speed) + speed);
            double dx = static_cast<double>(centre.x) - core::at(from, 0);
            double dy = static_cast<double>(centre.y) - core::at(from, 1);
            auto dz = static_cast<float>(static_cast<double>(centre.z) - core::at(from, 2));
            auto dw = static_cast<float>(static_cast<double>(centre.w) - core::at(from, 3));
            const double length = std::sqrt((dx * dx) + (static_cast<double>(dz) * dz) + (dy * dy));
            if (length > 0.0) {
                const double step_scale = pace / length;
                dx *= step_scale;
                dy *= step_scale;
                dz = static_cast<float>(dz * step_scale);
            }
            core::at(shatter->velocity_run(),
                     piece) = {.x = static_cast<float>(dx), .y = static_cast<float>(dy), .z = dz, .w = dw};
            // a turn about a random axis
            auto a1 = static_cast<float>((get_rand() * kPerRandTwice) - 1.0F);
            auto a2 = static_cast<float>((get_rand() * kPerRandTwice) - 1.0F);
            auto a3 = static_cast<float>((get_rand() * kPerRandTwice) - 1.0F);
            maths::Vec4 axis = {.x = a3, .y = a2, .z = a1, .w = 1.0F};
            const double axis_length = std::sqrt((static_cast<double>(a3) * a3) + (static_cast<double>(a2) * a2) +
                                                 (static_cast<double>(a1) * a1));
            if (axis_length > 0.0) {
                const double inverse = 1.0 / axis_length;
                axis.x = static_cast<float>(a3 * inverse);
                axis.y = static_cast<float>(a2 * inverse);
                axis.z = static_cast<float>(inverse * a1);
            }
            maths::Mat4& rotation = core::at(shatter->rotation_run(), piece);
            std::memset(&rotation, 0, sizeof(rotation));
            core::at(rotation.row, 0).x = core::at(rotation.row, 1).y = core::at(rotation.row, 2).z =
                core::at(rotation.row, 3).w = 1.0F;
            auto angle = static_cast<float>(get_rand() * kPerRand * kMaxSpin); // (up to 0.1, 0x00645D74)
            maths::Mat4 turn;
            maths::build_axis_angle_matrix(turn, axis, angle);
            // D3DX (linked library code): rotation = rotation * turn
            maths::matrix_multiply(&rotation, &rotation, &turn);
        }
        core::at(mesh->index_buffers(), 0)->get_unlock(); // (unlocked)
        mesh->fetch_vertex_store(0)->get_unlock();
    }
}

} // namespace render
