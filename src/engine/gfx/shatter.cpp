// Shattering on PC: see shatter.h.

#include "engine/gfx/shatter.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/texture_splats.h"
#include "engine/lists/key_map.h"
#include "engine/core/code_address.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

lists::KeyMap* g_shatters;

namespace {

extern void** const kShatterVtable;

IDirect3DDevice9* draw_device() { return g_draw_device; }
lists::KeyMap* shatters() { return g_shatters; }

uint16_t triangle_count(const PcShatter* shatter) { return shatter->triangle_count_; }

// A mesh's fields the drawing uses: its vertex format (+0x38) and pixel shader (+0x44).
uint32_t mesh_vertex_format(const MeshRef* mesh)
{
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(mesh) + 0x38);
}
void* mesh_pixel_shader(const MeshRef* mesh)
{
    return *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(mesh) + 0x44);
}

} // namespace

// 0x004B8120
PcShatter* PcShatter::make_new(int count, MeshRef* source_mesh, uint32_t own)
{
    reinterpret_cast<Shatter*>(this)->Shatter::make_new(count);
    mesh = source_mesh;
    vtable = reinterpret_cast<void**>(kShatterVtable);
    vertices = static_cast<uint8_t*>(memory::alloc(3 * source_mesh->stride * count));
    own_material_ = own;
    positions = static_cast<float*>(memory::alloc(12 * count));
    if (own) {
        void* memory = memory::alloc(0x110);
        material = memory ? reinterpret_cast<uint8_t*>(static_cast<MaterialRef*>(memory)->make_new()) : nullptr;
    } else {
        material = nullptr;
    }
    return this;
}

// 0x004B8FA0
void PcShatter::destroy_body()
{
    vtable = reinterpret_cast<void**>(kShatterVtable);
    memory::Hub::get_pull()->dealloc(vertices);
    if (uint8_t* own = material) {
        reinterpret_cast<MaterialRef*>(own)->destroy_body(); // (the material's destructor body)
        memory::Hub::get_pull()->dealloc(own);
    }
    if (positions)
        memory::Hub::get_pull()->dealloc(positions);
    reinterpret_cast<Shatter*>(this)->Shatter::destroy_body();
}

// 0x004B9A30
PcShatter* PcShatter::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004B80B0
void __cdecl draw_shatter(PcShatter* shatter)
{
    MeshRef* mesh = shatter->mesh;
    reinterpret_cast<MaterialRef*>(mesh->material)->hand_in(); // (the material's set-up)
    set_pixel_shader(mesh_pixel_shader(mesh), mesh->material);
    set_vertex_format(mesh_vertex_format(mesh));
    ++g_draw_calls; // the frame's draw calls and triangles
    g_drawn_triangles += triangle_count(shatter);
    draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangle_count(shatter), shatter->vertices, mesh->stride);
}

// 0x004B9550
void __cdecl kill_all_shatters()
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
        uint32_t key = reinterpret_cast<uint32_t>(shatter);
        shatters()->erase_entry(key);
        reinterpret_cast<void*(__thiscall*)(PcShatter*, int)>(shatter->vtable[0])(shatter, 1);
        shatter = next;
    }
}

namespace {

// x' = (z m20 + y m10) + x m00 and so on, each rounded to a float (the original's order of operations).
void get_turn(const maths::Mat4& m, double x, double y, double z, float* out)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(z * (&m.row[2].x)[k] + y * (&m.row[1].x)[k] + x * (&m.row[0].x)[k]);
}
float* get_at(uint8_t* vertex) { return reinterpret_cast<float*>(vertex); }

int get_rand() { return crt::get_rand(); }                          // (the game's C runtime's)
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);      // 0x00646510 (1 / 32767)
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u); // 0x0064930C (2 / 32767)

// The vertex stack the splitter works from (0x006FC4A0; not rewritten yet: pushed onto).
void push_vertex(const uint8_t* vertex)
{
    reinterpret_cast<VertexStack*>(reinterpret_cast<uint8_t*>(&g_vertex_stack))->stack_on(vertex);
}
uint8_t* lock_vertices(VertexBuffer* buffer)
{
    uint8_t* data;
    buffer->get_lock(0, 0, &data, 0x10);
    return data;
}
uint8_t* lock_indices(IdxStore* buffer)
{
    uint16_t* data;
    buffer->get_lock(0, 0, &data, 0x10);
    return reinterpret_cast<uint8_t*>(data);
}

} // namespace

// 0x004B95E0
void __cdecl update_shatter(PcShatter* shatter, float step)
{
    uint32_t stride = shatter->mesh->stride;
    uint8_t* v0 = shatter->vertices;
    uint8_t* v1 = v0 + stride;
    uint8_t* v2 = v0 + 2 * stride;
    for (int i = 0; i < shatter->triangle_count_; ++i, v0 += 3 * stride, v1 += 3 * stride, v2 += 3 * stride) {
        const maths::Vec4& c = shatter->centres[i];
        const maths::Mat4& m = shatter->rotations[i];
        // (the first vertex's offsets stay in registers, the others' are stored)
        double d0[3], d1[3], d2[3];
        for (int k = 0; k < 3; ++k) {
            d0[k] = static_cast<double>(get_at(v0)[k]) - (&c.x)[k];
            d1[k] = static_cast<float>(static_cast<double>(get_at(v1)[k]) - (&c.x)[k]);
            d2[k] = static_cast<float>(static_cast<double>(get_at(v2)[k]) - (&c.x)[k]);
        }
        float r0[3], r1[3], r2[3];
        get_turn(m, d0[0], d0[1], d0[2], r0);
        get_turn(m, d1[0], d1[1], d1[2], r1);
        get_turn(m, d2[0], d2[1], d2[2], r2);
        reinterpret_cast<Shatter*>(shatter)->Shatter::get_step(i, step); // (the centre moved)
        const maths::Vec4& moved = shatter->centres[i];
        for (int k = 0; k < 3; ++k) {
            get_at(v0)[k] = static_cast<float>(static_cast<double>(r0[k]) + (&moved.x)[k]);
            get_at(v1)[k] = static_cast<float>(static_cast<double>(r1[k]) + (&moved.x)[k]);
            get_at(v2)[k] = static_cast<float>(static_cast<double>(r2[k]) + (&moved.x)[k]);
        }
    }
    // the normals turned too
    uint8_t normal_offset = shatter->mesh->unknown1c_[1];
    if (!normal_offset)
        return;
    uint8_t* n0 = shatter->vertices + normal_offset;
    for (int i = 0; i < shatter->triangle_count_; ++i, n0 += 3 * stride) {
        const maths::Mat4& m = shatter->rotations[i];
        for (int v = 0; v < 3; ++v) {
            float* n = get_at(n0 + v * stride);
            float turned[3];
            get_turn(m, n[0], n[1], n[2], turned);
            std::memcpy(n, turned, 12);
        }
    }
}

// 0x004BA7A0
void __cdecl shatter_geom(uint8_t* geom)
{
    auto** meshes = *reinterpret_cast<MeshRef***>(geom + 0x3C);
    uint32_t mesh_count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t m = 0; m < mesh_count; ++m) {
        MeshRef* mesh = meshes[m];
        if (mesh->index_counts_[0] < 3)
            continue;
        uint32_t stride = mesh->stride;
        g_vertex_stack.vertex_size_ = stride; // (the vertex stack's vertex size)
        VertexBuffer* vertex_buffer = mesh->fetch_vertex_store(0);
        uint8_t* vertices = lock_vertices(vertex_buffer);
        auto* indices = reinterpret_cast<uint16_t*>(lock_indices(mesh->index_buffers_[0]));
        // the strip's triangles onto the stack, counted with what their splitting will make of them
        int count = 0;
        uint32_t a = indices[0], b = indices[1];
        for (uint32_t k = 2; k < mesh->index_counts_[0]; ++k) {
            uint32_t c = indices[k];
            if (a != b && a != c && b != c) {
                ++count;
                const float* pa = get_at(vertices + stride * a);
                const float* pb = get_at(vertices + stride * b);
                const float* pc = get_at(vertices + stride * c);
                push_vertex(vertices + stride * a);
                push_vertex(vertices + stride * b);
                push_vertex(vertices + stride * c);
                double e1x = static_cast<double>(pb[0]) - pa[0], e1y = static_cast<double>(pb[1]) - pa[1];
                double e1z = static_cast<double>(pb[2]) - pa[2];
                float e2zf = static_cast<float>(static_cast<double>(pc[2]) - pa[2]);
                float e2yf = static_cast<float>(static_cast<double>(pc[1]) - pa[1]);
                double e2x = static_cast<double>(pc[0]) - pa[0];
                float cz = static_cast<float>(e2yf * e1x - e2x * e1y);
                float cy = static_cast<float>(e2x * e1z - e2zf * e1x);
                double cx = e2zf * e1y - e2yf * e1z;
                double area = cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz;
                double limit = g_split_area_limit;
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
        void* memory = memory::alloc(sizeof(PcShatter));
        PcShatter* shatter = memory ? static_cast<PcShatter*>(memory)->make_new(count, mesh, 0) : nullptr;
        // the centre they fly from: the mesh's, moved back along the shatter direction (its length the base speed)
        const float* direction = reinterpret_cast<const float*>(&g_glass_burst_velocity);
        double scale = -static_cast<double>(g_shatter_velocity_scale);
        double back_x = scale * direction[0];
        float back_y = static_cast<float>(scale * direction[1]);
        float back_z = static_cast<float>(scale * direction[2]);
        float back_w = static_cast<float>(scale * direction[3]);
        float speed = static_cast<float>(std::sqrt(static_cast<double>(direction[1]) * direction[1] +
                                                   static_cast<double>(direction[2]) * direction[2] +
                                                   static_cast<double>(direction[0]) * direction[0]));
        float from[4] = {static_cast<float>(mesh->centre[0] + back_x),
                         static_cast<float>(static_cast<double>(mesh->centre[1]) + back_y),
                         static_cast<float>(static_cast<double>(mesh->centre[2]) + back_z),
                         static_cast<float>(static_cast<double>(back_w) + 1.0f)};
        uint32_t key = reinterpret_cast<uint32_t>(shatter);
        g_shatters->store_entry(key, shatter);
        uint8_t* cursor = shatter->vertices;
        while (split_next_triangle(&cursor, reinterpret_cast<const SplitFormat*>(mesh)))
            ;
        int j = 0;
        for (uint8_t* t = shatter->vertices; t < cursor; t += 3 * stride, ++j) {
            maths::Vec4& centre = shatter->centres[j];
            for (int k = 0; k < 3; ++k)
                (&centre.x)[k] = static_cast<float>(
                    (static_cast<double>(get_at(t + 2 * stride)[k]) + get_at(t + stride)[k] + get_at(t)[k]) *
                    (1.0f / 3.0f));
            // a velocity away from the centre at a random speed
            float pace =
                static_cast<float>(get_rand() * static_cast<double>(g_shatter_speed) * kPerRand * speed + speed);
            double dx = static_cast<double>(centre.x) - from[0];
            double dy = static_cast<double>(centre.y) - from[1];
            float dz = static_cast<float>(static_cast<double>(centre.z) - from[2]);
            float dw = static_cast<float>(static_cast<double>(centre.w) - from[3]);
            double length = std::sqrt(dx * dx + static_cast<double>(dz) * dz + dy * dy);
            if (length > 0.0) {
                double f = pace / length;
                dx *= f;
                dy *= f;
                dz = static_cast<float>(dz * f);
            }
            shatter->velocities[j] = {static_cast<float>(dx), static_cast<float>(dy), dz, dw};
            // a turn about a random axis
            float a1 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            float a2 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            float a3 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
            maths::Vec4 axis = {a3, a2, a1, 1.0f};
            double axis_length =
                std::sqrt(static_cast<double>(a3) * a3 + static_cast<double>(a2) * a2 + static_cast<double>(a1) * a1);
            if (axis_length > 0.0) {
                double inverse = 1.0 / axis_length;
                axis.x = static_cast<float>(a3 * inverse);
                axis.y = static_cast<float>(a2 * inverse);
                axis.z = static_cast<float>(inverse * a1);
            }
            maths::Mat4& rotation = shatter->rotations[j];
            std::memset(&rotation, 0, sizeof(rotation));
            rotation.row[0].x = rotation.row[1].y = rotation.row[2].z = rotation.row[3].w = 1.0f;
            float angle = static_cast<float>(get_rand() * kPerRand * 0.1f); // (up to 0.1, 0x00645D74)
            maths::Mat4 turn;
            maths::build_axis_angle_matrix(turn, axis, angle);
            // D3DX (linked library code): rotation = rotation * turn
            d3dx::matrix_multiply(&rotation, &rotation, &turn);
        }
        mesh->index_buffers_[0]->get_unlock(); // (unlocked)
        mesh->fetch_vertex_store(0)->get_unlock();
    }
}

namespace {
static VtableSlot g_shatter_vtable_slots[] = {&render::PcShatter::teardown};
void** const kShatterVtable = reinterpret_cast<void**>(g_shatter_vtable_slots);
} // namespace

} // namespace render
