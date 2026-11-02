// The PC primitive drawer: see pc_primitive.h.

#include "engine/gfx/pc_primitive.h"

#include "gpu/vertex_view.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <span>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/renderer.h"

namespace render {

namespace {
// The game's buffer for the vertices of a primitive drawn without one of its own.
std::array<uint8_t, 0x100>& primitive_vertex_buffer()
{
    static std::array<uint8_t, 0x100> primitive_vertex_buffer{};
    return primitive_vertex_buffer;
}

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// The vertex layouts by flexible vertex format (D3DFVF): XYZ (0x2) and XYZ | DIFFUSE (0x42) - positions and colours -,
// XYZ | TEX1 (0x102), XYZ | DIFFUSE | TEX1 (0x142) and XYZ | NORMAL | DIFFUSE | TEX1 (0x152). Every layout starts with
// the position.
struct VertexColour {
    std::array<float, 3> pos;
    uint32_t colour; // BGRA
};
struct VertexColourUv {
    std::array<float, 3> pos;
    uint32_t colour;
    float u, v;
};
struct VertexNormalColourUv {
    std::array<float, 3> pos;
    std::array<float, 3> normal;
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(VertexColour) == 0x10 && sizeof(VertexColourUv) == 0x18 && sizeof(VertexNormalColourUv) == 0x24,
              "the Direct3D vertex layouts");

// The vertex `index` in the layout V / its position (any layout, by the stride): gpu/vertex_view.h.
template <typename V>
V& vertex_at(std::span<uint8_t> vertices, int index)
{
    return gpu::record_at<V>(vertices, static_cast<size_t>(index));
}
std::span<float, 3> position_at(std::span<uint8_t> vertices, int stride, int index)
{
    return gpu::position_at(vertices, static_cast<size_t>(stride), static_cast<size_t>(index));
}

// RGBA with red and blue swapped (d_3d's BGRA).
uint32_t get_swapped(uint32_t rgba) { return (rgba & 0xFF00FF00u) | (rgba & 0xFF) << 16 | (rgba >> 16 & 0xFF); }

} // namespace

// 0x004C5230
PcPrimitive::PcPrimitive(int own) : Primitive(own)
{
    // (material_ made first by its constructor)
    const uint32_t made = flag_bits() | 1;
    flag_bits() = made;
    core::at(material_.pass_flags(), 0) = 0;
    const int r = get_rand();
    material_.set_checksum(static_cast<uint32_t>(r * get_rand())); // (a sort key)
    material_.set_pass_count(1);
    core::at(material_.textures(), 0) = nullptr;
    material_.set_two_sided(1);
    material_.set_depth_bias(4);
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(0x54628ED7);
    core::at(core::at(material_.pass_color(), 0), 0) = core::at(core::at(material_.pass_color(), 0), 1) = core::at(core::at(material_.pass_color(), 0), 2) = 0.5f;
    core::at(material_.address_modes(), 0) = 0;
    core::at(material_.mip_lod_bias(), 0) = 0;
    material_.set_has_uv_wibble(false);
    material_.set_alpha_ref(1);
}

// 0x004C5A20
PcPrimitive::~PcPrimitive()
{
    if (owns_vertices() == 1 && vertices_)
        memory::Hub::get_pull()->dealloc(vertices_);
    // (then material_'s destructor)
}

// 0x004C5A90
void PcPrimitive::set_texture(const BitmapTexture* texture, int, int)
{
    TextureImage* pc_texture = image_of(texture);
    core::at(material_.pass_flags(), 0) = pc_texture ? 4 : 0;
    const int r = get_rand();
    material_.set_checksum(static_cast<uint32_t>(r * get_rand()));
    material_.set_pass_count(1);
    core::at(material_.textures(), 0) = pc_texture;
    material_.set_two_sided(1);
    material_.set_depth_bias(4);
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(blend_mode());
    core::at(core::at(material_.pass_color(), 0), 0) = core::at(core::at(material_.pass_color(), 0), 1) = core::at(core::at(material_.pass_color(), 0), 2) = 0.5f;
    material_.set_alpha_ref(1);
    core::at(material_.address_modes(), 0) = 0;
    core::at(material_.mip_lod_bias(), 0) = 0;
    material_.set_has_uv_wibble(false);
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
    if (owns_vertices() == 1) {
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
    flag_bits() = (flag_bits() & ~1u) | 2;
    layout_ = layout_flags;
    type_ = primitive_type;
}

// 0x004C5320
void PcPrimitive::end_setup()
{
    if (!(vertex_format_ & 0x100) || core::at(material_.textures(), 0) != nullptr)
        flag_bits() = (flag_bits() & ~2u) | 4;
}

// 0x004C5370
void PcPrimitive::render()
{
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(blend_mode());
    material_.hand_in(); // (its set-up)
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t first_vertex = 0, room = 0;
    set_vertex_format(vertex_format_);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader964), nullptr);
    if (type_ != 0 && type_ != 2 && type_ != 4)
        return;
    void* out = os_layer::lock_dynamic_vertices(vertex_count_, stride_, &buffer, &first_vertex, &room);
    std::memcpy(out, vertices_, static_cast<uint32_t>(vertex_count_ * stride_));
    os_layer::unlock_dynamic_vertices(vertex_count_, stride_);
    renderer().d3d_device()->SetStreamSource(0, buffer, 0, stride_);
    int primitives = 0;
    if (type_ == 0) {
        primitives = vertex_count_ / 2;
        renderer().d3d_device()->DrawPrimitive(D3DPT_LINELIST, first_vertex, primitives);
    } else if (type_ == 2) {
        primitives = vertex_count_ / 3;
        renderer().d3d_device()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, primitives);
    } else {
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        primitives = vertex_count_ / 2;
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertex_count_, 0,
                                           primitives);
    }
    renderer().count_draw(primitives);
}

// 0x004C55D0
void PcPrimitive::blit(std::span<const float> positions, std::span<const uint32_t> colours, std::span<const float> uvs,
                       std::span<const float> normals)
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
        vertices_ = primitive_vertex_buffer().data(); // (a buffer of the game's)
    }
    for (int i = 0; i < vertex_count_; ++i) {
        change_place(i, core::at(positions, 3 * i), core::at(positions, 3 * i + 1), core::at(positions, 3 * i + 2));
        if (!colours.empty())
            set_vertex_colour(i, core::at(colours, i));
        if (!uvs.empty())
            set_uv(i, core::at(uvs, 2 * i), core::at(uvs, 2 * i + 1));
        if (!normals.empty())
            set_normal(i, core::at(normals, 3 * i), core::at(normals, 3 * i + 1), core::at(normals, 3 * i + 2));
    }
    render();
}

// 0x004C5B20
void PcPrimitive::change_place(int index, float x, float y, float z)
{
    core::at(position_at(vertex_bytes(), stride_, index), 0) = x;
    core::at(position_at(vertex_bytes(), stride_, index), 1) = y;
    core::at(position_at(vertex_bytes(), stride_, index), 2) = z;
}
// 0x004C5B60
void PcPrimitive::set_x(int index, float x) { core::at(position_at(vertex_bytes(), stride_, index), 0) = x; }
// 0x004C5B80
void PcPrimitive::set_y(int index, float y) { core::at(position_at(vertex_bytes(), stride_, index), 1) = y; }
// 0x004C5BA0
void PcPrimitive::set_z(int index, float z) { core::at(position_at(vertex_bytes(), stride_, index), 2) = z; }

// 0x004C5BC0
void PcPrimitive::set_normal(int index, float x, float y, float z)
{
    std::get<0>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = x;
    std::get<1>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = y;
    std::get<2>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = z;
}
// 0x004C5BF0
void PcPrimitive::set_normal_x(int index, float x) { std::get<0>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = x; }
// 0x004C5C10
void PcPrimitive::set_normal_y(int index, float y) { std::get<1>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = y; }
// 0x004C5C30
void PcPrimitive::set_normal_z(int index, float z) { std::get<2>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal) = z; }

// 0x004C56E0
void PcPrimitive::set_vertex_colour(int index, uint32_t rgba)
{
    const uint32_t bgra = get_swapped(rgba);
    if (vertex_format_ == 0x42)
        vertex_at<VertexColour>(vertex_bytes(), index).colour = bgra;
    else if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertex_bytes(), index).colour = (bgra & 0xFFFFFF) | 0x80000000;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).colour = bgra;
}

// 0x004C5760
void PcPrimitive::set_uv(int index, float u, float v)
{
    if (vertex_format_ == 0x142) {
        vertex_at<VertexColourUv>(vertex_bytes(), index).u = u;
        vertex_at<VertexColourUv>(vertex_bytes(), index).v = v;
    } else if (vertex_format_ == 0x152) {
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).u = u;
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).v = v;
    }
}
// 0x004C57C0
void PcPrimitive::choose_u(int index, float u)
{
    if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertex_bytes(), index).u = u;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).u = u;
}
// 0x004C5800
void PcPrimitive::choose_v(int index, float v)
{
    if (vertex_format_ == 0x142)
        vertex_at<VertexColourUv>(vertex_bytes(), index).v = v;
    else if (vertex_format_ == 0x152)
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).v = v;
}

// 0x004C5C50
float PcPrimitive::fetch_pos_x(int index) { return core::at(position_at(vertex_bytes(), stride_, index), 0); }
// 0x004C5C70
float PcPrimitive::fetch_pos_y(int index) { return core::at(position_at(vertex_bytes(), stride_, index), 1); }
// 0x004C5C90
float PcPrimitive::fetch_pos_z(int index) { return core::at(position_at(vertex_bytes(), stride_, index), 2); }
// 0x004C5CB0
float PcPrimitive::get_normal_x(int index) { return std::get<0>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal); }
// 0x004C5CD0
float PcPrimitive::get_normal_y(int index) { return std::get<1>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal); }
// 0x004C5CF0
float PcPrimitive::get_normal_z(int index) { return std::get<2>(vertex_at<VertexNormalColourUv>(vertex_bytes(), index).normal); }

// 0x004C5840
uint32_t PcPrimitive::get_vertex_colour(int index)
{
    if (vertex_format_ == 0x42)
        return vertex_at<VertexColour>(vertex_bytes(), index).colour;
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertex_bytes(), index).colour;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).colour;
    return 0;
}
// 0x004C5890
float PcPrimitive::get_u(int index)
{
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertex_bytes(), index).u;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).u;
    return 0.0f;
}
// 0x004C58D0
float PcPrimitive::get_v(int index)
{
    if (vertex_format_ == 0x142)
        return vertex_at<VertexColourUv>(vertex_bytes(), index).v;
    if (vertex_format_ == 0x152)
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).v;
    return 0.0f;
}

// 0x004C5D10
void PcPrimitive::get_slide(float x, float y, float z)
{
    for (int i = 0; i < vertex_count_; ++i) {
        core::at(position_at(vertex_bytes(), stride_, i), 0) = x + core::at(position_at(vertex_bytes(), stride_, i), 0);
        core::at(position_at(vertex_bytes(), stride_, i), 1) = y + core::at(position_at(vertex_bytes(), stride_, i), 1);
        core::at(position_at(vertex_bytes(), stride_, i), 2) = z + core::at(position_at(vertex_bytes(), stride_, i), 2);
    }
}
// 0x004C5D80
void PcPrimitive::translate_x(float x)
{
    for (int i = 0; i < vertex_count_; ++i)
        core::at(position_at(vertex_bytes(), stride_, i), 0) = x + core::at(position_at(vertex_bytes(), stride_, i), 0);
}
// 0x004C5DC0
void PcPrimitive::translate_y(float y)
{
    for (int i = 0; i < vertex_count_; ++i)
        core::at(position_at(vertex_bytes(), stride_, i), 1) = y + core::at(position_at(vertex_bytes(), stride_, i), 1);
}
// 0x004C5E00
void PcPrimitive::translate_z(float z)
{
    for (int i = 0; i < vertex_count_; ++i)
        core::at(position_at(vertex_bytes(), stride_, i), 2) = z + core::at(position_at(vertex_bytes(), stride_, i), 2);
}

// 0x004C5910
void PcPrimitive::scroll_uv(float u, float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142) {
            vertex_at<VertexColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexColourUv>(vertex_bytes(), i).u;
            vertex_at<VertexColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexColourUv>(vertex_bytes(), i).v;
        } else if (vertex_format_ == 0x152) {
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u;
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v;
        }
    }
}
// 0x004C5980
void PcPrimitive::scroll_u(float u)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            vertex_at<VertexColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexColourUv>(vertex_bytes(), i).u;
        else if (vertex_format_ == 0x152)
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u;
    }
}
// 0x004C59D0
void PcPrimitive::scroll_v(float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == 0x142)
            vertex_at<VertexColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexColourUv>(vertex_bytes(), i).v;
        else if (vertex_format_ == 0x152)
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v;
    }
}

// 0x004C5340
int PcPrimitive::weigh(const Primitive* other_primitive)
{
    const auto* other = core::down_cast<const PcPrimitive>(other_primitive);
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
    renderer().d3d_device()->SetRenderState(D3DRS_ZFUNC, function);
}

} // namespace render
