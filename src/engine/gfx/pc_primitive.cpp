// The PC primitive drawer: see pc_primitive.h.

#include "engine/gfx/pc_primitive.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "platform/platform_d3d.h"
#include "engine/core/code_address.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

// The game's buffer for the vertices of a primitive drawn without one of its own.
uint8_t g_primitive_vertex_buffer[0x100];

namespace {

extern void** const kVtable;

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DIndexBuffer9* quad_indices() { return g_quad_indices; }
uint32_t& draw_calls() { return reinterpret_cast<uint32_t&>(g_draw_calls); }
uint32_t& drawn_triangles() { return reinterpret_cast<uint32_t&>(g_drawn_triangles); }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

template <typename T>
T& get_at(uint8_t* base, int offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

// RGBA with red and blue swapped (d_3d's BGRA).
uint32_t get_swapped(uint32_t rgba) { return (rgba & 0xFF00FF00u) | (rgba & 0xFF) << 16 | (rgba >> 16 & 0xFF); }

} // namespace

// 0x004C5230
PcPrimitive* PcPrimitive::make_new(int own)
{
    reinterpret_cast<Primitive*>(this)->Primitive::make_new(own);
    vtable = reinterpret_cast<void**>(kVtable);
    reinterpret_cast<MaterialRef*>(material)->make_new();
    uint32_t made = flags | 1;
    vertex_format_ = 2;
    vertices = nullptr;
    vertex_count_ = 0;
    colour = 0x80808080;
    flags = made;
    get_at<uint32_t>(material, 0x20) = 0;
    int r = get_rand();
    get_at<int>(material, 0x00) = r * get_rand(); // (a sort key)
    get_at<uint32_t>(material, 0x08) = 1;
    get_at<uint32_t>(material, 0x30) = 0;
    material[0x0D] = 1;
    material[0x11] = 4;
    get_at<uint32_t>(material, 0x80) = blend_mode_from_name(0x54628ED7);
    get_at<float>(material, 0x40) = get_at<float>(material, 0x44) = get_at<float>(material, 0x48) = 0.5f;
    get_at<uint32_t>(material, 0x90) = 0;
    get_at<uint32_t>(material, 0xE0) = 0;
    material[0x0E] = 0;
    material[0x10] = 1;
    return this;
}

// 0x004C5A20
void PcPrimitive::destroy_body()
{
    vtable = reinterpret_cast<void**>(kVtable);
    if (owns_vertices_ == 1 && vertices)
        memory::Hub::get_pull()->dealloc(vertices);
    reinterpret_cast<MaterialRef*>(material)->destroy_body();
    reinterpret_cast<Primitive*>(this)->Primitive::destroy_body();
}

// 0x004C5ED0
PcPrimitive* PcPrimitive::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004C5A90
void PcPrimitive::set_texture(const uint8_t* texture, int, int)
{
    uint32_t key = texture[8] & 1 ? **reinterpret_cast<uint32_t* const*>(texture + 0x10)
                                  : *reinterpret_cast<const uint32_t*>(texture + 0x10);
    get_at<uint32_t>(material, 0x20) = key ? 4 : 0;
    int r = get_rand();
    get_at<int>(material, 0x00) = r * get_rand();
    get_at<uint32_t>(material, 0x08) = 1;
    get_at<uint32_t>(material, 0x30) = key;
    material[0x0D] = 1;
    material[0x11] = 4;
    get_at<uint32_t>(material, 0x80) = blend_mode_from_name(blend_mode_);
    get_at<float>(material, 0x40) = get_at<float>(material, 0x44) = get_at<float>(material, 0x48) = 0.5f;
    material[0x10] = 1;
    get_at<uint32_t>(material, 0x90) = 0;
    get_at<uint32_t>(material, 0xE0) = 0;
    material[0x0E] = 0;
}

// 0x004C5310
void PcPrimitive::change_colour(uint32_t rgba) { colour = rgba; }

// 0x004C5F00
void PcPrimitive::prepare_all(uint32_t layout_flags, int primitive_type, int count)
{
    vertex_format_ = 2;
    if (layout_flags & 1)
        vertex_format_ = 0x42;
    if (layout_flags & 2)
        vertex_format_ |= 0x100;
    if (layout_flags & 4)
        vertex_format_ |= 0x10;
    if (owns_vertices_ == 1) {
        int size = 0;
        switch (vertex_format_) {
        case 0x2:
            size = 0xC;
            break;
        case 0x42:
            size = 0x10;
            break;
        case 0x102:
            size = 0x14;
            break;
        case 0x142:
            size = 0x18;
            break;
        case 0x152:
            size = 0x24;
            break;
        default:
            break;
        }
        if (size) {
            // (the 0x102 layout's allocation is memory::alloc's inline; the others through it)
            vertices = static_cast<uint8_t*>(memory::alloc(count * size));
            stride = size;
        }
    }
    vertex_count_ = count;
    flags = (flags & ~1u) | 2;
    layout = layout_flags;
    type = primitive_type;
}

// 0x004C5320
void PcPrimitive::get_ready()
{
    if (!(vertex_format_ & 0x100) || get_at<uint32_t>(material, 0x30) != 0)
        flags = (flags & ~2u) | 4;
}

// 0x004C5370
void PcPrimitive::render()
{
    get_at<uint32_t>(material, 0x80) = blend_mode_from_name(blend_mode_);
    reinterpret_cast<MaterialRef*>(material)->hand_in(); // (its set-up)
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t first_vertex = 0, room = 0;
    set_vertex_format(vertex_format_);
    set_pixel_shader(g_pixel_shaders[kPixelShader964], nullptr);
    if (type != 0 && type != 2 && type != 4)
        return;
    void* out = os_layer::lock_dynamic_vertices(vertex_count_, stride, &buffer, &first_vertex, &room);
    std::memcpy(out, vertices, static_cast<uint32_t>(vertex_count_ * stride));
    os_layer::unlock_dynamic_vertices(vertex_count_, stride);
    input_port()->SetStreamSource(0, buffer, 0, stride);
    int primitives;
    if (type == 0) {
        primitives = vertex_count_ / 2;
        input_port()->DrawPrimitive(D3DPT_LINELIST, first_vertex, primitives);
    } else if (type == 2) {
        primitives = vertex_count_ / 3;
        input_port()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, primitives);
    } else {
        input_port()->SetIndices(quad_indices());
        primitives = vertex_count_ / 2;
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertex_count_, 0,
                                           primitives);
    }
    ++draw_calls();
    drawn_triangles() += primitives;
}

// 0x004C55D0
void PcPrimitive::blit(const float* positions, const uint32_t* colours, const float* uvs, const float* normals)
{
    int size = 0;
    switch (vertex_format_) {
    case 0x2:
        size = 0xC;
        break;
    case 0x42:
        size = 0x10;
        break;
    case 0x102:
        size = 0x14;
        break;
    case 0x142:
        size = 0x18;
        break;
    case 0x152:
        size = 0x24;
        break;
    default:
        break;
    }
    if (size) {
        stride = size;
        vertices = g_primitive_vertex_buffer; // (a buffer of the game's)
    }
    using Vec3 = void(__thiscall*)(PcPrimitive*, int, float, float, float);
    for (int i = 0; i < vertex_count_; ++i) {
        reinterpret_cast<Vec3>(vtable[7])(this, i, positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        if (colours)
            reinterpret_cast<void(__thiscall*)(PcPrimitive*, int, uint32_t)>(vtable[15])(this, i, colours[i]);
        if (uvs)
            reinterpret_cast<void(__thiscall*)(PcPrimitive*, int, float, float)>(vtable[16])(this, i, uvs[2 * i],
                                                                                             uvs[2 * i + 1]);
        if (normals)
            reinterpret_cast<Vec3>(vtable[11])(this, i, normals[3 * i], normals[3 * i + 1], normals[3 * i + 2]);
    }
    reinterpret_cast<void(__thiscall*)(PcPrimitive*)>(vtable[5])(this);
}

// 0x004C5B20
void PcPrimitive::change_place(int index, float x, float y, float z)
{
    get_at<float>(vertices, stride * index) = x;
    get_at<float>(vertices, stride * index + 4) = y;
    get_at<float>(vertices, stride * index + 8) = z;
}
// 0x004C5B60
void PcPrimitive::set_x(int index, float x) { get_at<float>(vertices, stride * index) = x; }
// 0x004C5B80
void PcPrimitive::set_y(int index, float y) { get_at<float>(vertices, stride * index + 4) = y; }
// 0x004C5BA0
void PcPrimitive::set_z(int index, float z) { get_at<float>(vertices, stride * index + 8) = z; }

// 0x004C5BC0
void PcPrimitive::set_normal(int index, float x, float y, float z)
{
    get_at<float>(vertices, index * 0x24 + 0xC) = x;
    get_at<float>(vertices, index * 0x24 + 0x10) = y;
    get_at<float>(vertices, index * 0x24 + 0x14) = z;
}
// 0x004C5BF0
void PcPrimitive::set_normal_x(int index, float x) { get_at<float>(vertices, index * 0x24 + 0xC) = x; }
// 0x004C5C10
void PcPrimitive::set_normal_y(int index, float y) { get_at<float>(vertices, index * 0x24 + 0x10) = y; }
// 0x004C5C30
void PcPrimitive::set_normal_z(int index, float z) { get_at<float>(vertices, index * 0x24 + 0x14) = z; }

// 0x004C56E0
void PcPrimitive::set_vertex_colour(int index, uint32_t rgba)
{
    uint32_t bgra = get_swapped(rgba);
    if (vertex_format_ == 0x42)
        get_at<uint32_t>(vertices, index * 0x10 + 0xC) = bgra;
    else if (vertex_format_ == 0x142)
        get_at<uint32_t>(vertices, index * 0x18 + 0xC) = (bgra & 0xFFFFFF) | 0x80000000;
    else if (vertex_format_ == 0x152)
        get_at<uint32_t>(vertices, index * 0x24 + 0x18) = bgra;
}

// 0x004C5760
void PcPrimitive::set_uv(int index, float u, float v)
{
    if (vertex_format_ == 0x142) {
        get_at<float>(vertices, index * 0x18 + 0x10) = u;
        get_at<float>(vertices, index * 0x18 + 0x14) = v;
    } else if (vertex_format_ == 0x152) {
        get_at<float>(vertices, index * 0x24 + 0x1C) = u;
        get_at<float>(vertices, index * 0x24 + 0x20) = v;
    }
}
// 0x004C57C0
void PcPrimitive::choose_u(int index, float u)
{
    if (vertex_format_ == 0x142)
        get_at<float>(vertices, index * 0x18 + 0x10) = u;
    else if (vertex_format_ == 0x152)
        get_at<float>(vertices, index * 0x24 + 0x1C) = u;
}
// 0x004C5800
void PcPrimitive::choose_v(int index, float v)
{
    if (vertex_format_ == 0x142)
        get_at<float>(vertices, index * 0x18 + 0x14) = v;
    else if (vertex_format_ == 0x152)
        get_at<float>(vertices, index * 0x24 + 0x20) = v;
}

// 0x004C5C50
float PcPrimitive::fetch_pos_x(int index) { return get_at<float>(vertices, stride * index); }
// 0x004C5C70
float PcPrimitive::fetch_pos_y(int index) { return get_at<float>(vertices, stride * index + 4); }
// 0x004C5C90
float PcPrimitive::fetch_pos_z(int index) { return get_at<float>(vertices, stride * index + 8); }
// 0x004C5CB0
float PcPrimitive::get_normal_x(int index) { return get_at<float>(vertices, index * 0x24 + 0xC); }
// 0x004C5CD0
float PcPrimitive::get_normal_y(int index) { return get_at<float>(vertices, index * 0x24 + 0x10); }
// 0x004C5CF0
float PcPrimitive::get_normal_z(int index) { return get_at<float>(vertices, index * 0x24 + 0x14); }

// 0x004C5840
uint32_t PcPrimitive::get_vertex_colour(int index)
{
    if (vertex_format_ == 0x42)
        return get_at<uint32_t>(vertices, index * 0x10 + 0xC);
    if (vertex_format_ == 0x142)
        return get_at<uint32_t>(vertices, index * 0x18 + 0xC);
    if (vertex_format_ == 0x152)
        return get_at<uint32_t>(vertices, index * 0x24 + 0x18);
    return 0;
}
// 0x004C5890
float PcPrimitive::get_u(int index)
{
    if (vertex_format_ == 0x142)
        return get_at<float>(vertices, index * 0x18 + 0x10);
    if (vertex_format_ == 0x152)
        return get_at<float>(vertices, index * 0x24 + 0x1C);
    return 0.0f;
}
// 0x004C58D0
float PcPrimitive::get_v(int index)
{
    if (vertex_format_ == 0x142)
        return get_at<float>(vertices, index * 0x18 + 0x14);
    if (vertex_format_ == 0x152)
        return get_at<float>(vertices, index * 0x24 + 0x20);
    return 0.0f;
}

// 0x004C5D10
void PcPrimitive::get_slide(float x, float y, float z)
{
    for (int i = 0; i < vertex_count_; ++i) {
        get_at<float>(vertices, stride * i) = x + get_at<float>(vertices, stride * i);
        get_at<float>(vertices, stride * i + 4) = y + get_at<float>(vertices, stride * i + 4);
        get_at<float>(vertices, stride * i + 8) = z + get_at<float>(vertices, stride * i + 8);
    }
}
// 0x004C5D80
void PcPrimitive::translate_x(float x)
{
    for (int i = 0; i < vertex_count_; ++i)
        get_at<float>(vertices, stride * i) = x + get_at<float>(vertices, stride * i);
}
// 0x004C5DC0
void PcPrimitive::translate_y(float y)
{
    for (int i = 0; i < vertex_count_; ++i)
        get_at<float>(vertices, stride * i + 4) = y + get_at<float>(vertices, stride * i + 4);
}
// 0x004C5E00
void PcPrimitive::translate_z(float z)
{
    for (int i = 0; i < vertex_count_; ++i)
        get_at<float>(vertices, stride * i + 8) = z + get_at<float>(vertices, stride * i + 8);
}

// 0x004C5910
void PcPrimitive::scroll_uv(float u, float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142) {
            get_at<float>(vertices, i * 0x18 + 0x10) = u + get_at<float>(vertices, i * 0x18 + 0x10);
            get_at<float>(vertices, i * 0x18 + 0x14) = v + get_at<float>(vertices, i * 0x18 + 0x14);
        } else if (vertex_format_ == 0x152) {
            get_at<float>(vertices, i * 0x24 + 0x1C) = u + get_at<float>(vertices, i * 0x24 + 0x1C);
            get_at<float>(vertices, i * 0x24 + 0x20) = v + get_at<float>(vertices, i * 0x24 + 0x20);
        }
    }
}
// 0x004C5980
void PcPrimitive::scroll_u(float u)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            get_at<float>(vertices, i * 0x18 + 0x10) = u + get_at<float>(vertices, i * 0x18 + 0x10);
        else if (vertex_format_ == 0x152)
            get_at<float>(vertices, i * 0x24 + 0x1C) = u + get_at<float>(vertices, i * 0x24 + 0x1C);
    }
}
// 0x004C59D0
void PcPrimitive::scroll_v(float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            get_at<float>(vertices, i * 0x18 + 0x14) = v + get_at<float>(vertices, i * 0x18 + 0x14);
        else if (vertex_format_ == 0x152)
            get_at<float>(vertices, i * 0x24 + 0x20) = v + get_at<float>(vertices, i * 0x24 + 0x20);
    }
}

// 0x004C5340
int PcPrimitive::weigh(const PcPrimitive* other)
{
    if (type < other->type)
        return 1;
    return type <= other->type ? 0 : -1;
}

// 0x004C5E40
void __cdecl set_primitive_depth_test(int state, int value)
{
    DWORD function = D3DCMP_LESSEQUAL;
    if (state == 1) {
        switch (value) {
        case 1:
            function = D3DCMP_NEVER;
            break;
        case 2:
            function = D3DCMP_ALWAYS;
            break;
        case 3:
            function = D3DCMP_LESSEQUAL;
            break;
        case 4:
            function = D3DCMP_LESS;
            break;
        default:
            break;
        }
    }
    input_port()->SetRenderState(D3DRS_ZFUNC, function);
}

namespace {
static VtableSlot g_vtable_slots[] = {&render::PcPrimitive::teardown,      &render::PcPrimitive::set_texture,
                                      &render::PcPrimitive::change_colour, &render::PcPrimitive::prepare_all,
                                      &render::PcPrimitive::get_ready,     &render::PcPrimitive::render,
                                      &render::PcPrimitive::blit,          &render::PcPrimitive::change_place,
                                      &render::PcPrimitive::set_x,         &render::PcPrimitive::set_y,
                                      &render::PcPrimitive::set_z,         &render::PcPrimitive::set_normal,
                                      &render::PcPrimitive::set_normal_x,  &render::PcPrimitive::set_normal_y,
                                      &render::PcPrimitive::set_normal_z,  &render::PcPrimitive::set_vertex_colour,
                                      &render::PcPrimitive::set_uv,        &render::PcPrimitive::choose_u,
                                      &render::PcPrimitive::choose_v,      &render::PcPrimitive::fetch_pos_x,
                                      &render::PcPrimitive::fetch_pos_y,   &render::PcPrimitive::fetch_pos_z,
                                      &render::PcPrimitive::get_normal_x,  &render::PcPrimitive::get_normal_y,
                                      &render::PcPrimitive::get_normal_z,  &render::PcPrimitive::get_vertex_colour,
                                      &render::PcPrimitive::get_u,         &render::PcPrimitive::get_v,
                                      &render::PcPrimitive::get_slide,     &render::PcPrimitive::translate_x,
                                      &render::PcPrimitive::translate_y,   &render::PcPrimitive::translate_z,
                                      &render::PcPrimitive::scroll_uv,     &render::PcPrimitive::scroll_u,
                                      &render::PcPrimitive::scroll_v,      &render::PcPrimitive::weigh};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);
} // namespace

} // namespace render
