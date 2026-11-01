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
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

// The game's buffer for the vertices of a primitive drawn without one of its own.
static uint8_t g_primitive_vertex_buffer[0x100];

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DIndexBuffer9* quad_indices() { return g_quad_indices; }
uint32_t& draw_calls() { return reinterpret_cast<uint32_t&>(g_draw_calls); }
uint32_t& drawn_triangles() { return reinterpret_cast<uint32_t&>(g_drawn_triangles); }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// The vertex layouts by flexible vertex format (D3DFVF): XYZ (0x2) and XYZ | DIFFUSE (0x42) - positions and colours -,
// XYZ | TEX1 (0x102), XYZ | DIFFUSE | TEX1 (0x142) and XYZ | NORMAL | DIFFUSE | TEX1 (0x152). Every layout starts with
// the position.
struct VertexColour {
    float pos[3];
    uint32_t colour; // BGRA
};
struct VertexColourUv {
    float pos[3];
    uint32_t colour;
    float u, v;
};
struct VertexNormalColourUv {
    float pos[3];
    float normal[3];
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(VertexColour) == 0x10 && sizeof(VertexColourUv) == 0x18 && sizeof(VertexNormalColourUv) == 0x24,
              "the Direct3D vertex layouts");

// The vertex `index` in the layout V / its position (any layout, by the stride).
template <typename V>
V& vertex_at(uint8_t* vertices, int index)
{
    return reinterpret_cast<V*>(vertices)[index];
}
float* position_at(uint8_t* vertices, int stride, int index)
{
    return reinterpret_cast<float*>(vertices + stride * index);
}

// RGBA with red and blue swapped (d_3d's BGRA).
uint32_t get_swapped(uint32_t rgba) { return (rgba & 0xFF00FF00u) | (rgba & 0xFF) << 16 | (rgba >> 16 & 0xFF); }

} // namespace

// 0x004C5230
PcPrimitive::PcPrimitive(int own) : Primitive(own)
{
    // (material_ made first by its constructor)
    uint32_t made = flag_bits_ | 1;
    vertex_format_ = 2;
    vertices_ = nullptr;
    vertex_count_ = 0;
    colour_ = 0x80808080;
    flag_bits_ = made;
    material_.pass_flags_[0] = 0;
    int r = get_rand();
    material_.checksum = static_cast<uint32_t>(r * get_rand()); // (a sort key)
    material_.pass_count_ = 1;
    material_.textures[0] = nullptr;
    material_.two_sided_ = 1;
    material_.depth_bias_ = 4;
    material_.blend_modes_[0] = blend_mode_from_name(0x54628ED7);
    material_.pass_color_[0][0] = material_.pass_color_[0][1] = material_.pass_color_[0][2] = 0.5f;
    material_.address_modes_[0] = 0;
    material_.mip_lod_bias_[0] = 0;
    material_.has_uv_wibble_ = false;
    material_.alpha_ref_ = 1;
}

// 0x004C5A20
PcPrimitive::~PcPrimitive()
{
    if (owns_vertices_ == 1 && vertices_)
        memory::Hub::get_pull()->dealloc(vertices_);
    // (then material_'s destructor)
}

// 0x004C5A90
void PcPrimitive::set_texture(const uint8_t* texture, int, int)
{
    // (the engine texture's +0x10: its PC texture, or with flag 1 a pointer to it)
    uint8_t* pc_texture = texture[8] & 1 ? **reinterpret_cast<uint8_t** const*>(texture + 0x10)
                                         : *reinterpret_cast<uint8_t* const*>(texture + 0x10);
    material_.pass_flags_[0] = pc_texture ? 4 : 0;
    int r = get_rand();
    material_.checksum = static_cast<uint32_t>(r * get_rand());
    material_.pass_count_ = 1;
    material_.textures[0] = pc_texture;
    material_.two_sided_ = 1;
    material_.depth_bias_ = 4;
    material_.blend_modes_[0] = blend_mode_from_name(blend_mode_);
    material_.pass_color_[0][0] = material_.pass_color_[0][1] = material_.pass_color_[0][2] = 0.5f;
    material_.alpha_ref_ = 1;
    material_.address_modes_[0] = 0;
    material_.mip_lod_bias_[0] = 0;
    material_.has_uv_wibble_ = false;
}

// 0x004C5310
void PcPrimitive::change_colour(uint32_t rgba) { colour_ = rgba; }

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
            // (the 0x102 layout_'s allocation is memory::alloc's inline; the others through it)
            vertices_ = static_cast<uint8_t*>(memory::alloc(count * size));
            stride_ = size;
        }
    }
    vertex_count_ = count;
    flag_bits_ = (flag_bits_ & ~1u) | 2;
    layout_ = layout_flags;
    type_ = primitive_type;
}

// 0x004C5320
void PcPrimitive::end_setup()
{
    if (!(vertex_format_ & 0x100) || material_.textures[0] != nullptr)
        flag_bits_ = (flag_bits_ & ~2u) | 4;
}

// 0x004C5370
void PcPrimitive::render()
{
    material_.blend_modes_[0] = blend_mode_from_name(blend_mode_);
    material_.hand_in(); // (its set-up)
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t first_vertex = 0, room = 0;
    set_vertex_format(vertex_format_);
    set_pixel_shader(g_pixel_shaders[kPixelShader964], nullptr);
    if (type_ != 0 && type_ != 2 && type_ != 4)
        return;
    void* out = os_layer::lock_dynamic_vertices(vertex_count_, stride_, &buffer, &first_vertex, &room);
    std::memcpy(out, vertices_, static_cast<uint32_t>(vertex_count_ * stride_));
    os_layer::unlock_dynamic_vertices(vertex_count_, stride_);
    input_port()->SetStreamSource(0, buffer, 0, stride_);
    int primitives = 0;
    if (type_ == 0) {
        primitives = vertex_count_ / 2;
        input_port()->DrawPrimitive(D3DPT_LINELIST, first_vertex, primitives);
    } else if (type_ == 2) {
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
        stride_ = size;
        vertices_ = g_primitive_vertex_buffer; // (a buffer of the game's)
    }
    for (int i = 0; i < vertex_count_; ++i) {
        change_place(i, positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        if (colours)
            set_vertex_colour(i, colours[i]);
        if (uvs)
            set_uv(i, uvs[2 * i], uvs[2 * i + 1]);
        if (normals)
            set_normal(i, normals[3 * i], normals[3 * i + 1], normals[3 * i + 2]);
    }
    render();
}

// 0x004C5B20
void PcPrimitive::change_place(int index, float x, float y, float z)
{
    position_at(vertices_, stride_, index)[0] = x;
    position_at(vertices_, stride_, index)[1] = y;
    position_at(vertices_, stride_, index)[2] = z;
}
// 0x004C5B60
void PcPrimitive::set_x(int index, float x) { position_at(vertices_, stride_, index)[0] = x; }
// 0x004C5B80
void PcPrimitive::set_y(int index, float y) { position_at(vertices_, stride_, index)[1] = y; }
// 0x004C5BA0
void PcPrimitive::set_z(int index, float z) { position_at(vertices_, stride_, index)[2] = z; }

// 0x004C5BC0
void PcPrimitive::set_normal(int index, float x, float y, float z)
{
    vertex_at<VertexNormalColourUv>(vertices_, index).normal[0] = x;
    vertex_at<VertexNormalColourUv>(vertices_, index).normal[1] = y;
    vertex_at<VertexNormalColourUv>(vertices_, index).normal[2] = z;
}
// 0x004C5BF0
void PcPrimitive::set_normal_x(int index, float x) { vertex_at<VertexNormalColourUv>(vertices_, index).normal[0] = x; }
// 0x004C5C10
void PcPrimitive::set_normal_y(int index, float y) { vertex_at<VertexNormalColourUv>(vertices_, index).normal[1] = y; }
// 0x004C5C30
void PcPrimitive::set_normal_z(int index, float z) { vertex_at<VertexNormalColourUv>(vertices_, index).normal[2] = z; }

// 0x004C56E0
void PcPrimitive::set_vertex_colour(int index, uint32_t rgba)
{
    uint32_t bgra = get_swapped(rgba);
    if (vertex_format_ == 0x42)
        vertex_at<VertexColour>(vertices_, index).colour = bgra;
    else if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertices_, index).colour = (bgra & 0xFFFFFF) | 0x80000000;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertices_, index).colour = bgra;
}

// 0x004C5760
void PcPrimitive::set_uv(int index, float u, float v)
{
    if (vertex_format_ == 0x142) {
        vertex_at<VertexColourUv>(vertices_, index).u = u;
        vertex_at<VertexColourUv>(vertices_, index).v = v;
    } else if (vertex_format_ == 0x152) {
        vertex_at<VertexNormalColourUv>(vertices_, index).u = u;
        vertex_at<VertexNormalColourUv>(vertices_, index).v = v;
    }
}
// 0x004C57C0
void PcPrimitive::choose_u(int index, float u)
{
    if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertices_, index).u = u;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertices_, index).u = u;
}
// 0x004C5800
void PcPrimitive::choose_v(int index, float v)
{
    if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertices_, index).v = v;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertices_, index).v = v;
}

// 0x004C5C50
float PcPrimitive::fetch_pos_x(int index) { return position_at(vertices_, stride_, index)[0]; }
// 0x004C5C70
float PcPrimitive::fetch_pos_y(int index) { return position_at(vertices_, stride_, index)[1]; }
// 0x004C5C90
float PcPrimitive::fetch_pos_z(int index) { return position_at(vertices_, stride_, index)[2]; }
// 0x004C5CB0
float PcPrimitive::get_normal_x(int index) { return vertex_at<VertexNormalColourUv>(vertices_, index).normal[0]; }
// 0x004C5CD0
float PcPrimitive::get_normal_y(int index) { return vertex_at<VertexNormalColourUv>(vertices_, index).normal[1]; }
// 0x004C5CF0
float PcPrimitive::get_normal_z(int index) { return vertex_at<VertexNormalColourUv>(vertices_, index).normal[2]; }

// 0x004C5840
uint32_t PcPrimitive::get_vertex_colour(int index)
{
    if (vertex_format_ == 0x42)
        return vertex_at<VertexColour>(vertices_, index).colour;
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertices_, index).colour;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertices_, index).colour;
    return 0;
}
// 0x004C5890
float PcPrimitive::get_u(int index)
{
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertices_, index).u;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertices_, index).u;
    return 0.0f;
}
// 0x004C58D0
float PcPrimitive::get_v(int index)
{
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertices_, index).v;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertices_, index).v;
    return 0.0f;
}

// 0x004C5D10
void PcPrimitive::get_slide(float x, float y, float z)
{
    for (int i = 0; i < vertex_count_; ++i) {
        position_at(vertices_, stride_, i)[0] = x + position_at(vertices_, stride_, i)[0];
        position_at(vertices_, stride_, i)[1] = y + position_at(vertices_, stride_, i)[1];
        position_at(vertices_, stride_, i)[2] = z + position_at(vertices_, stride_, i)[2];
    }
}
// 0x004C5D80
void PcPrimitive::translate_x(float x)
{
    for (int i = 0; i < vertex_count_; ++i)
        position_at(vertices_, stride_, i)[0] = x + position_at(vertices_, stride_, i)[0];
}
// 0x004C5DC0
void PcPrimitive::translate_y(float y)
{
    for (int i = 0; i < vertex_count_; ++i)
        position_at(vertices_, stride_, i)[1] = y + position_at(vertices_, stride_, i)[1];
}
// 0x004C5E00
void PcPrimitive::translate_z(float z)
{
    for (int i = 0; i < vertex_count_; ++i)
        position_at(vertices_, stride_, i)[2] = z + position_at(vertices_, stride_, i)[2];
}

// 0x004C5910
void PcPrimitive::scroll_uv(float u, float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142) {
            vertex_at<VertexColourUv>(vertices_, i).u = u + vertex_at<VertexColourUv>(vertices_, i).u;
            vertex_at<VertexColourUv>(vertices_, i).v = v + vertex_at<VertexColourUv>(vertices_, i).v;
        } else if (vertex_format_ == 0x152) {
            vertex_at<VertexNormalColourUv>(vertices_, i).u = u + vertex_at<VertexNormalColourUv>(vertices_, i).u;
            vertex_at<VertexNormalColourUv>(vertices_, i).v = v + vertex_at<VertexNormalColourUv>(vertices_, i).v;
        }
    }
}
// 0x004C5980
void PcPrimitive::scroll_u(float u)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            vertex_at<VertexColourUv>(vertices_, i).u = u + vertex_at<VertexColourUv>(vertices_, i).u;
        else if (vertex_format_ == 0x152)
            vertex_at<VertexNormalColourUv>(vertices_, i).u = u + vertex_at<VertexNormalColourUv>(vertices_, i).u;
    }
}
// 0x004C59D0
void PcPrimitive::scroll_v(float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            vertex_at<VertexColourUv>(vertices_, i).v = v + vertex_at<VertexColourUv>(vertices_, i).v;
        else if (vertex_format_ == 0x152)
            vertex_at<VertexNormalColourUv>(vertices_, i).v = v + vertex_at<VertexNormalColourUv>(vertices_, i).v;
    }
}

// 0x004C5340
int PcPrimitive::weigh(const Primitive* other_primitive)
{
    const auto* other = static_cast<const PcPrimitive*>(other_primitive);
    if (type_ < other->type_)
        return 1;
    return type_ <= other->type_ ? 0 : -1;
}

// 0x004C5E40
void set_primitive_depth_test(int state, int value)
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

} // namespace render
