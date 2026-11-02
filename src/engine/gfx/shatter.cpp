// Shattering on PC: see shatter.h.

#include "engine/gfx/shatter.h"
#include "engine/gfx/geom.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/texture_splats.h"
#include "gpu/vertex_view.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace render {

lists::ObjectSet<PcShatter> g_shatters;

namespace {

IDirect3DDevice9* draw_device() { return g_draw_device; }

uint16_t triangle_count(const PcShatter* shatter) { return shatter->triangle_count_; }

// A mesh's fields the drawing uses: its vertex format (+0x38) and pixel shader (+0x44).
uint32_t mesh_vertex_format(const MeshRef* mesh) { return mesh->fvf; }
void* mesh_pixel_shader(const MeshRef* mesh) { return mesh->pixel_shader_; }

} // namespace

// 0x004B8120
PcShatter::PcShatter(int count, MeshRef* source_mesh, uint32_t own)
    : Shatter(count), own_material_(own), mesh(source_mesh),
      vertices(static_cast<uint8_t*>(memory::alloc(3 * source_mesh->stride * count)))
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    positions = static_cast<float*>(memory::alloc(12 * count));
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    if (own) {
        void* memory = memory::alloc(sizeof(MaterialRef));
        material = memory ? memory::construct_in<MaterialRef>(memory) : nullptr;
    } else {
        material = nullptr;
    }
}

// 0x004B8FA0
PcShatter::~PcShatter()
{
    memory::Hub::get_pull()->dealloc(vertices);
    if (MaterialRef* own = material) {
        own->~MaterialRef(); // (the material's destructor body)
        memory::Hub::get_pull()->dealloc(own);
    }
    if (positions)
        memory::Hub::get_pull()->dealloc(positions);
}

// 0x004B80B0
void draw_shatter(PcShatter* shatter)
{
    MeshRef* mesh = shatter->mesh;
    mesh->material->hand_in(); // (the material's set-up)
    set_pixel_shader(mesh_pixel_shader(mesh), mesh->material);
    set_vertex_format(mesh_vertex_format(mesh));
    ++g_draw_calls; // the frame's draw calls and triangles
    g_drawn_triangles += triangle_count(shatter);
    draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangle_count(shatter), shatter->vertices, mesh->stride);
}

// 0x004B9550
void kill_all_shatters()
{
    g_shatters.for_each([](PcShatter* shatter) {
        g_shatters.erase(shatter);
        delete shatter;
    });
}

namespace {

// x' = (z m20 + y m10) + x m00 and so on, each rounded to a float (the original's order of operations).
void get_turn(const maths::Mat4& m, double x, double y, double z, float* out)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(z * (&core::at(m.row, 2).x)[k] + y * (&core::at(m.row, 1).x)[k] + x * (&core::at(m.row, 0).x)[k]);
}

int get_rand() { return crt::get_rand(); }                          // (the game's C runtime's)
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);      // 0x00646510 (1 / 32767)
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u); // 0x0064930C (2 / 32767)

// The vertex stack the splitter works from (0x006FC4A0; not rewritten yet: pushed onto).
void push_vertex(const uint8_t* vertex)
{
    g_vertex_stack.stack_on(vertex);
}
uint8_t* lock_vertices(VertexBuffer* buffer)
{
    uint8_t* data = nullptr;
    buffer->get_lock(0, 0, &data, 0x10);
    return data;
}
uint16_t* lock_indices(IdxStore* buffer)
{
    uint16_t* data = nullptr;
    buffer->get_lock(0, 0, &data, 0x10);
    return data;
}

} // namespace

// 0x004B95E0
void update_shatter(PcShatter* shatter, float step)
{
    const uint32_t stride = shatter->mesh->stride;
    const gpu::VertexWriter out(shatter->vertices, stride);
    const gpu::VertexView in = out.view();
    for (int i = 0; std::cmp_less(i, shatter->triangle_count_); ++i) {
        const auto v0 = static_cast<size_t>(3 * i);
        const size_t v1 = v0 + 1;
        const size_t v2 = v0 + 2;
        const maths::Vec4& c = shatter->centres[i];
        const maths::Mat4& m = shatter->rotations[i];
        const auto p0 = in.float3(v0);
        const auto p1 = in.float3(v1);
        const auto p2 = in.float3(v2);
        // (the first vertex's offsets stay in registers, the others' are stored)
        std::array<double, 3> d0{};
        std::array<double, 3> d1{};
        std::array<double, 3> d2{};
        for (int k = 0; k < 3; ++k) {
            core::at(d0, k) = static_cast<double>(core::at(p0, k)) - c[k];
            core::at(d1, k) = static_cast<float>(static_cast<double>(core::at(p1, k)) - c[k]);
            core::at(d2, k) = static_cast<float>(static_cast<double>(core::at(p2, k)) - c[k]);
        }
        std::array<float, 3> r0{};
        std::array<float, 3> r1{};
        std::array<float, 3> r2{};
        get_turn(m, core::at(d0, 0), core::at(d0, 1), core::at(d0, 2), r0.data());
        get_turn(m, core::at(d1, 0), core::at(d1, 1), core::at(d1, 2), r1.data());
        get_turn(m, core::at(d2, 0), core::at(d2, 1), core::at(d2, 2), r2.data());
        static_cast<Shatter*>(shatter)->Shatter::get_step(i, step); // (the centre moved)
        const maths::Vec4& moved = shatter->centres[i];
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
    const uint8_t normal_offset = shatter->mesh->normal_offset_;
    if (!normal_offset)
        return;
    for (int i = 0; std::cmp_less(i, shatter->triangle_count_); ++i) {
        const maths::Mat4& m = shatter->rotations[i];
        for (int v = 0; v < 3; ++v) {
            const auto vertex = static_cast<size_t>((3 * i) + v);
            const auto n = in.float3(vertex, normal_offset);
            std::array<float, 3> turned{};
            get_turn(m, core::at(n, 0), core::at(n, 1), core::at(n, 2), turned.data());
            out.set_floats(vertex, normal_offset, turned);
        }
    }
}

// 0x004BA7A0
void shatter_geom(PcGeom* geom)
{
    MeshRef* const* meshes = geom->meshes;
    const uint32_t mesh_count = geom->mesh_count_;
    for (uint32_t m = 0; m < mesh_count; ++m) {
        MeshRef* mesh = meshes[m];
        if (core::at(mesh->index_counts_, 0) < 3)
            continue;
        const uint32_t stride = mesh->stride;
        g_vertex_stack.vertex_size_ = stride; // (the vertex stack's vertex size)
        VertexBuffer* vertex_buffer = mesh->fetch_vertex_store(0);
        const uint8_t* vertices = lock_vertices(vertex_buffer);
        const gpu::VertexView view(vertices, stride);
        const uint16_t* indices = lock_indices(core::at(mesh->index_buffers_, 0));
        // the strip's triangles onto the stack, counted with what their splitting will make of them
        int count = 0;
        uint32_t a = indices[0], b = indices[1];
        for (uint32_t k = 2; k < core::at(mesh->index_counts_, 0); ++k) {
            const uint32_t c = indices[k];
            if (a != b && a != c && b != c) {
                ++count;
                const auto pa = view.float3(a);
                const auto pb = view.float3(b);
                const auto pc = view.float3(c);
                push_vertex(vertices + stride * a);
                push_vertex(vertices + stride * b);
                push_vertex(vertices + stride * c);
                const double e1x = static_cast<double>(core::at(pb, 0)) - core::at(pa, 0);
                const double e1y = static_cast<double>(core::at(pb, 1)) - core::at(pa, 1);
                const double e1z = static_cast<double>(core::at(pb, 2)) - core::at(pa, 2);
                auto e2zf = static_cast<float>(static_cast<double>(core::at(pc, 2)) - core::at(pa, 2));
                auto e2yf = static_cast<float>(static_cast<double>(core::at(pc, 1)) - core::at(pa, 1));
                const double e2x = static_cast<double>(core::at(pc, 0)) - core::at(pa, 0);
                auto cz = static_cast<float>(e2yf * e1x - e2x * e1y);
                auto cy = static_cast<float>(e2x * e1z - e2zf * e1x);
                const double cx = e2zf * e1y - e2yf * e1z;
                double area = cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz;
                const double limit = g_split_area_limit;
                if (area > limit) {
                    int pieces = 1;
                    do {
                        area *= 0.0625f; // (0x00645A84)
                        pieces <<= 2;
                    } while (area > limit);
                    count += pieces - 1;
                }
            }
            a = b;
            b = c;
        }
        if (!count)
            continue; // (the buffers left locked, as the original)
        auto shatter = memory::make_owned<PcShatter>(count, mesh, 0);
        // the centre they fly from: the mesh's, moved back along the shatter direction (its length the base speed)
        const auto* direction = g_glass_burst_velocity.data();
        const double scale = -static_cast<double>(g_shatter_velocity_scale);
        const double back_x = scale * direction[0];
        auto back_y = static_cast<float>(scale * direction[1]);
        auto back_z = static_cast<float>(scale * direction[2]);
        auto back_w = static_cast<float>(scale * direction[3]);
        auto speed = static_cast<float>(std::sqrt(static_cast<double>(direction[1]) * direction[1] +
                                                  static_cast<double>(direction[2]) * direction[2] +
                                                  static_cast<double>(direction[0]) * direction[0]));
        const std::array<float, 4> from = {{static_cast<float>(core::at(mesh->centre, 0) + back_x),
                               static_cast<float>(static_cast<double>(core::at(mesh->centre, 1)) + back_y),
                               static_cast<float>(static_cast<double>(core::at(mesh->centre, 2)) + back_z),
                               static_cast<float>(static_cast<double>(back_w) + 1.0f)}};
        g_shatters.insert(shatter);
        uint8_t* cursor = shatter->vertices;
        while (split_next_triangle(&cursor, mesh))
            ;
        const gpu::VertexView pieces(shatter->vertices, stride);
        const auto piece_count = static_cast<size_t>(cursor - shatter->vertices) / stride;
        int j = 0;
        for (size_t t = 0; t < piece_count; t += 3, ++j) {
            maths::Vec4& centre = shatter->centres[j];
            const auto q0 = pieces.float3(t);
            const auto q1 = pieces.float3(t + 1);
            const auto q2 = pieces.float3(t + 2);
            for (int k = 0; k < 3; ++k)
                centre[k] = static_cast<float>(
                    (static_cast<double>(core::at(q2, k)) + core::at(q1, k) + core::at(q0, k)) * (1.0f / 3.0f));
            // a velocity away from the centre at a random speed
            auto pace =
                static_cast<float>(get_rand() * static_cast<double>(g_shatter_speed) * kPerRand * speed + speed);
            double dx = static_cast<double>(centre.x) - core::at(from, 0);
            double dy = static_cast<double>(centre.y) - core::at(from, 1);
            auto dz = static_cast<float>(static_cast<double>(centre.z) - core::at(from, 2));
            auto dw = static_cast<float>(static_cast<double>(centre.w) - core::at(from, 3));
            const double length = std::sqrt(dx * dx + static_cast<double>(dz) * dz + dy * dy);
            if (length > 0.0) {
                const double f = pace / length;
                dx *= f;
                dy *= f;
                dz = static_cast<float>(dz * f);
            }
            shatter->velocities[j] = {static_cast<float>(dx), static_cast<float>(dy), dz, dw};
            // a turn about a random axis
            auto a1 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            auto a2 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            auto a3 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            maths::Vec4 axis = {a3, a2, a1, 1.0f};
            const double axis_length =
                std::sqrt(static_cast<double>(a3) * a3 + static_cast<double>(a2) * a2 + static_cast<double>(a1) * a1);
            if (axis_length > 0.0) {
                const double inverse = 1.0 / axis_length;
                axis.x = static_cast<float>(a3 * inverse);
                axis.y = static_cast<float>(a2 * inverse);
                axis.z = static_cast<float>(inverse * a1);
            }
            maths::Mat4& rotation = shatter->rotations[j];
            std::memset(&rotation, 0, sizeof(rotation));
            core::at(rotation.row, 0).x = core::at(rotation.row, 1).y = core::at(rotation.row, 2).z = core::at(rotation.row, 3).w = 1.0f;
            auto angle = static_cast<float>(get_rand() * kPerRand * 0.1f); // (up to 0.1, 0x00645D74)
            maths::Mat4 turn;
            maths::build_axis_angle_matrix(turn, axis, angle);
            // D3DX (linked library code): rotation = rotation * turn
            d3dx::matrix_multiply(&rotation, &rotation, &turn);
        }
        core::at(mesh->index_buffers_, 0)->get_unlock(); // (unlocked)
        mesh->fetch_vertex_store(0)->get_unlock();
    }
}

} // namespace render
