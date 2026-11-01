// Shattering on PC: see shatter.h.

#include "engine/gfx/shatter.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/d3dx.h"

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
#include "engine/lists/key_map.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

lists::KeyMap* g_shatters;

namespace {

IDirect3DDevice9* draw_device() { return g_draw_device; }
lists::KeyMap* shatters() { return g_shatters; }

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
        void* memory = memory::alloc(0x110);
        material = memory ? raw::bytes(memory::construct_in<MaterialRef>(memory)) : nullptr;
    } else {
        material = nullptr;
    }
}

// 0x004B8FA0
PcShatter::~PcShatter()
{
    memory::Hub::get_pull()->dealloc(vertices);
    if (uint8_t* own = material) {
        raw::as<MaterialRef>(own)->~MaterialRef(); // (the material's destructor body)
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
    lists::KeyMap* table = shatters();
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    if (!table->step_iterator() || !table->iter_item_)
        return;
    auto* shatter = static_cast<PcShatter*>(table->iter_item_->value);
    while (shatter) {
        PcShatter* next = nullptr;
        if (shatters()->step_iterator() && shatters()->iter_item_)
            next = static_cast<PcShatter*>(shatters()->iter_item_->value);
        auto key = cell::of(shatter);
        shatters()->erase_entry(key);
        delete shatter;
        shatter = next;
    }
}

namespace {

// x' = (z m20 + y m10) + x m00 and so on, each rounded to a float (the original's order of operations).
void get_turn(const maths::Mat4& m, double x, double y, double z, float* out)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(z * (&core::at(m.row, 2).x)[k] + y * (&core::at(m.row, 1).x)[k] + x * (&core::at(m.row, 0).x)[k]);
}
float* get_at(uint8_t* vertex) { return raw::as<float>(vertex); }

int get_rand() { return crt::get_rand(); }                          // (the game's C runtime's)
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);      // 0x00646510 (1 / 32767)
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u); // 0x0064930C (2 / 32767)

// The vertex stack the splitter works from (0x006FC4A0; not rewritten yet: pushed onto).
void push_vertex(const uint8_t* vertex)
{
    raw::as<VertexStack>(raw::bytes(&g_vertex_stack))->stack_on(vertex);
}
uint8_t* lock_vertices(VertexBuffer* buffer)
{
    uint8_t* data = nullptr;
    buffer->get_lock(0, 0, &data, 0x10);
    return data;
}
uint8_t* lock_indices(IdxStore* buffer)
{
    uint16_t* data = nullptr;
    buffer->get_lock(0, 0, &data, 0x10);
    return raw::bytes(data);
}

} // namespace

// 0x004B95E0
void update_shatter(PcShatter* shatter, float step)
{
    const uint32_t stride = shatter->mesh->stride;
    uint8_t* v0 = shatter->vertices;
    uint8_t* v1 = v0 + stride;
    uint8_t* v2 = v0 + 2 * stride;
    for (int i = 0; std::cmp_less(i, shatter->triangle_count_);
         ++i, v0 += 3 * stride, v1 += 3 * stride, v2 += 3 * stride) {
        const maths::Vec4& c = shatter->centres[i];
        const maths::Mat4& m = shatter->rotations[i];
        // (the first vertex's offsets stay in registers, the others' are stored)
        std::array<double, 3> d0{};
        std::array<double, 3> d1{};
        std::array<double, 3> d2{};
        for (int k = 0; k < 3; ++k) {
            core::at(d0, k) = static_cast<double>(get_at(v0)[k]) - (&c.x)[k];
            core::at(d1, k) = static_cast<float>(static_cast<double>(get_at(v1)[k]) - (&c.x)[k]);
            core::at(d2, k) = static_cast<float>(static_cast<double>(get_at(v2)[k]) - (&c.x)[k]);
        }
        std::array<float, 3> r0{};
        std::array<float, 3> r1{};
        std::array<float, 3> r2{};
        get_turn(m, core::at(d0, 0), core::at(d0, 1), core::at(d0, 2), r0.data());
        get_turn(m, core::at(d1, 0), core::at(d1, 1), core::at(d1, 2), r1.data());
        get_turn(m, core::at(d2, 0), core::at(d2, 1), core::at(d2, 2), r2.data());
        static_cast<Shatter*>(shatter)->Shatter::get_step(i, step); // (the centre moved)
        const maths::Vec4& moved = shatter->centres[i];
        for (int k = 0; k < 3; ++k) {
            get_at(v0)[k] = static_cast<float>(static_cast<double>(core::at(r0, k)) + (&moved.x)[k]);
            get_at(v1)[k] = static_cast<float>(static_cast<double>(core::at(r1, k)) + (&moved.x)[k]);
            get_at(v2)[k] = static_cast<float>(static_cast<double>(core::at(r2, k)) + (&moved.x)[k]);
        }
    }
    // the normals turned too
    const uint8_t normal_offset = core::at(shatter->mesh->unknown1c_, 1);
    if (!normal_offset)
        return;
    uint8_t* n0 = shatter->vertices + normal_offset;
    for (int i = 0; std::cmp_less(i, shatter->triangle_count_); ++i, n0 += 3 * stride) {
        const maths::Mat4& m = shatter->rotations[i];
        for (int v = 0; v < 3; ++v) {
            float* n = get_at(n0 + v * stride);
            std::array<float, 3> turned{};
            get_turn(m, n[0], n[1], n[2], turned.data());
            std::memcpy(n, turned.data(), 12);
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
        uint8_t* vertices = lock_vertices(vertex_buffer);
        auto* indices = raw::as<uint16_t>(lock_indices(core::at(mesh->index_buffers_, 0)));
        // the strip's triangles onto the stack, counted with what their splitting will make of them
        int count = 0;
        uint32_t a = indices[0], b = indices[1];
        for (uint32_t k = 2; k < core::at(mesh->index_counts_, 0); ++k) {
            const uint32_t c = indices[k];
            if (a != b && a != c && b != c) {
                ++count;
                const float* pa = get_at(vertices + stride * a);
                const float* pb = get_at(vertices + stride * b);
                const float* pc = get_at(vertices + stride * c);
                push_vertex(vertices + stride * a);
                push_vertex(vertices + stride * b);
                push_vertex(vertices + stride * c);
                const double e1x = static_cast<double>(pb[0]) - pa[0];
                const double e1y = static_cast<double>(pb[1]) - pa[1];
                const double e1z = static_cast<double>(pb[2]) - pa[2];
                auto e2zf = static_cast<float>(static_cast<double>(pc[2]) - pa[2]);
                auto e2yf = static_cast<float>(static_cast<double>(pc[1]) - pa[1]);
                const double e2x = static_cast<double>(pc[0]) - pa[0];
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
        auto* shatter = new PcShatter(count, mesh, 0);
        // the centre they fly from: the mesh's, moved back along the shatter direction (its length the base speed)
        const auto* direction = raw::as<const float>(&g_glass_burst_velocity);
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
        auto key = cell::of(shatter);
        g_shatters->store_entry(key, shatter);
        uint8_t* cursor = shatter->vertices;
        while (split_next_triangle(&cursor, raw::as<const SplitFormat>(mesh)))
            ;
        int j = 0;
        for (uint8_t* t = shatter->vertices; t < cursor; t += 3 * stride, ++j) {
            maths::Vec4& centre = shatter->centres[j];
            for (int k = 0; k < 3; ++k)
                (&centre.x)[k] = static_cast<float>(
                    (static_cast<double>(get_at(t + 2 * stride)[k]) + get_at(t + stride)[k] + get_at(t)[k]) *
                    (1.0f / 3.0f));
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
