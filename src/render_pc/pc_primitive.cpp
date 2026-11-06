// The PC primitive drawer: see pc_primitive.h.

#include "render_pc/pc_primitive.h"

#include <cstdint>
#include <span>
#include <cstring>
#include <array>

#include "engine/gfx/mesh_formats.h"
#include "engine/memory/manager.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/vertex_run.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/render_device.h"
#include "render_pc/render_state.h"
#include "render_pc/simple_material.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/dynamic_vertices.h"
#include "render_pc/shader_slots.h"
#include "platform/crt.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "render_pc/pc_texture.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "base/core/packed_colour.h"
#include "base/maths/vec2.h"
#include "base/maths/vec4.h"

namespace render {

constexpr std::size_t kPrimitiveBufferBytes = 0x100;
constexpr float kGrey = 0.5F;
// Checksums used in this file (the text each hashes).
constexpr uint32_t kBlend = 0x54628ED7U; // Blend

using vertex_run::kColourVertexBytes;
using vertex_run::kColourUvNormalVertexBytes;
using vertex_run::kColourUvVertexBytes;
using vertex_run::kFvfColour;
using vertex_run::kFvfColourUv;
using vertex_run::kFvfColourUvNormal;
using vertex_run::kFvfUv;
using vertex_run::kPositionVertexBytes;
using vertex_run::kUvVertexBytes;
using vertex_run::position_at;
using vertex_run::vertex_at;
using vertex_run::VertexColour;
using vertex_run::VertexColourUv;
using vertex_run::VertexNormalColourUv;

namespace {
// The game's buffer for the vertices of a primitive drawn without one of its own.
std::array<uint8_t, kPrimitiveBufferBytes>& primitive_vertex_buffer()
{
    static std::array<uint8_t, kPrimitiveBufferBytes> primitive_vertex_buffer{};
    return primitive_vertex_buffer;
}

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

} // namespace

// 0x004C5230
PcPrimitive::PcPrimitive(int own) : VertexRun(own)
{
    // (material_ made first by its constructor)
    const uint32_t made = flag_bits() | 1;
    flag_bits() = made;
    core::at(material_.pass_flags(), 0) = 0;
    const int random_value = get_rand();
    material_.set_checksum(static_cast<uint32_t>(random_value * get_rand())); // (a sort key)
    material_.set_pass_count(1);
    core::at(material_.textures(), 0) = nullptr;
    material_.set_two_sided(1);
    material_.set_depth_bias(4);
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(kBlend);
    core::at(core::at(material_.pass_color(), 0), 0) = core::at(core::at(material_.pass_color(), 0), 1) = core::at(core::at(material_.pass_color(), 0), 2) = kGrey;
    core::at(material_.address_modes(), 0) = 0;
    core::at(material_.mip_lod_bias(), 0) = 0;
    material_.set_has_uv_wibble(false);
    material_.set_alpha_ref(1);
}

// 0x004C5A20
PcPrimitive::~PcPrimitive()
{
    if (owns_vertices() == 1 && vertices_) {
        memory::Hub::dealloc(vertices_);
    }
    // (then material_'s destructor)
}

// 0x004C5A90
void PcPrimitive::set_texture(const BitmapTexture* texture, int /*unused1*/, int /*unused2*/)
{
    TextureImage* pc_texture = image_of(texture);
    core::at(material_.pass_flags(), 0) = pc_texture ? 4 : 0;
    const int random_value = get_rand();
    material_.set_checksum(static_cast<uint32_t>(random_value * get_rand()));
    material_.set_pass_count(1);
    core::at(material_.textures(), 0) = pc_texture;
    material_.set_two_sided(1);
    material_.set_depth_bias(4);
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(blend_mode());
    core::at(core::at(material_.pass_color(), 0), 0) = core::at(core::at(material_.pass_color(), 0), 1) = core::at(core::at(material_.pass_color(), 0), 2) = kGrey;
    material_.set_alpha_ref(1);
    core::at(material_.address_modes(), 0) = 0;
    core::at(material_.mip_lod_bias(), 0) = 0;
    material_.set_has_uv_wibble(false);
}

// 0x004C5310
void PcPrimitive::change_colour(uint32_t rgba) { colour_ = rgba; }

// 0x004C5F00
void PcPrimitive::prepare_all(PrimitiveLayout primitive_layout) { lay_out(primitive_layout); }

// 0x004C5320
void PcPrimitive::end_setup()
{
    if (((vertex_format_ & kFvfTex1) == 0U) || core::at(material_.textures(), 0) != nullptr) {
        flag_bits() = (flag_bits() & ~2U) | 4;
    }
}

// 0x004C5370
void PcPrimitive::render()
{
    core::at(material_.blend_modes(), 0) = blend_mode_from_name(blend_mode());
    material_.hand_in(); // (its set-up)
    GpuVertexBuffer* buffer = nullptr;
    uint32_t first_vertex = 0;
    uint32_t room = 0;
    set_vertex_format(vertex_format_);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader964), nullptr);
    if (type_ != 0 && type_ != 2 && type_ != 4) {
        return;
    }
    void* out = os_layer::lock_dynamic_vertices(vertex_count_, stride_, &buffer, {.first_vertex_ = &first_vertex, .room = &room});
    std::memcpy(out, vertices_, static_cast<uint32_t>(vertex_count_ * stride_));
    os_layer::unlock_dynamic_vertices(vertex_count_, stride_);
    gpu().set_stream_source(0, buffer, 0, stride_);
    int primitives = 0;
    if (type_ == 0) {
        primitives = vertex_count_ / 2;
        gpu().draw(GpuPrimitive::kLineList, first_vertex, static_cast<uint32_t>(primitives));
    } else if (type_ == 2) {
        primitives = vertex_count_ / 3;
        gpu().draw(GpuPrimitive::kTriangleList, first_vertex, static_cast<uint32_t>(primitives));
    } else {
        gpu().set_indices(quad_index_buffer());
        primitives = vertex_count_ / 2;
        gpu().draw_indexed(GpuPrimitive::kTriangleList, static_cast<int32_t>(first_vertex), 0,
                           static_cast<uint32_t>(vertex_count_), 0, static_cast<uint32_t>(primitives));
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
        size = kPositionVertexBytes;
        break;
    case kFvfColour:
        size = kColourVertexBytes;
        break;
    case kFvfUv:
        size = kUvVertexBytes;
        break;
    case kFvfColourUv:
        size = kColourUvVertexBytes;
        break;
    case kFvfColourUvNormal:
        size = kColourUvNormalVertexBytes;
        break;
    default:
        break;
    }
    if (size != 0) {
        stride_ = size;
        vertices_ = primitive_vertex_buffer().data(); // (a buffer of the game's)
    }
    for (int i = 0; i < vertex_count_; ++i) {
        change_place(i, {.x = core::at(positions, 3 * i),
                         .y = core::at(positions, (3 * i) + 1),
                         .z = core::at(positions, (3 * i) + 2)});
        if (!colours.empty()) {
            set_vertex_colour(i, colour::Packed{core::at(colours, i)});
        }
        if (!uvs.empty()) {
            set_uv(i, {.x = core::at(uvs, 2 * i), .y = core::at(uvs, (2 * i) + 1)});
        }
        if (!normals.empty()) {
            set_normal(i, {.x = core::at(normals, 3 * i),
                           .y = core::at(normals, (3 * i) + 1),
                           .z = core::at(normals, (3 * i) + 2)});
        }
    }
    render();
}

// 0x004C5B20
void PcPrimitive::change_place(int index, const maths::Vec4& place) { place_vertex(index, place); }
// 0x004C5B60
void PcPrimitive::set_x(int index, float x) { core::at(position_at(vertex_bytes(), stride_, index), 0) = x; }
// 0x004C5B80
void PcPrimitive::set_y(int index, float y) { core::at(position_at(vertex_bytes(), stride_, index), 1) = y; }
// 0x004C5BA0
void PcPrimitive::set_z(int index, float z) { core::at(position_at(vertex_bytes(), stride_, index), 2) = z; }

// 0x004C5BC0
void PcPrimitive::set_normal(int index, const maths::Vec4& normal)
{
    const auto x = normal.x;
    const auto y = normal.y;
    const auto z = normal.z;
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
void PcPrimitive::set_vertex_colour(int index, colour::Packed colour_word) { colour_vertex(index, colour_word); }

// 0x004C5760
void PcPrimitive::set_uv(int index, maths::Vec2 uv) { map_vertex(index, uv); }
// 0x004C57C0
void PcPrimitive::choose_u(int index, float u)
{
    if (vertex_format_ == kFvfColourUv) {
        vertex_at<VertexColourUv>(vertex_bytes(), index).u = u;
    } else if (vertex_format_ == kFvfColourUvNormal) {
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).u = u;
    }
}
// 0x004C5800
void PcPrimitive::choose_v(int index, float v)
{
    if (vertex_format_ == kFvfColourUv) {
        vertex_at<VertexColourUv>(vertex_bytes(), index).v = v;
    } else if (vertex_format_ == kFvfColourUvNormal) {
        vertex_at<VertexNormalColourUv>(vertex_bytes(), index).v = v;
    }
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
    if (vertex_format_ == kFvfColour) {
        return vertex_at<VertexColour>(vertex_bytes(), index).colour;
    }
    if (vertex_format_ == kFvfColourUv) {
        return vertex_at<VertexColourUv>(vertex_bytes(), index).colour;
    }
    if (vertex_format_ == kFvfColourUvNormal) {
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).colour;
    }
    return 0;
}
// 0x004C5890
float PcPrimitive::get_u(int index)
{
    if (vertex_format_ == kFvfColourUv) {
        return vertex_at<VertexColourUv>(vertex_bytes(), index).u;
    }
    if (vertex_format_ == kFvfColourUvNormal) {
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).u;
    }
    return 0.0F;
}
// 0x004C58D0
float PcPrimitive::get_v(int index)
{
    if (vertex_format_ == kFvfColourUv) {
        return vertex_at<VertexColourUv>(vertex_bytes(), index).v;
    }
    if (vertex_format_ == kFvfColourUvNormal) {
        return vertex_at<VertexNormalColourUv>(vertex_bytes(), index).v;
    }
    return 0.0F;
}

// 0x004C5D10
void PcPrimitive::get_slide(const maths::Vec4& slide)
{
    const auto x = slide.x;
    const auto y = slide.y;
    const auto z = slide.z;
    for (int i = 0; i < vertex_count_; ++i) {
        core::at(position_at(vertex_bytes(), stride_, i), 0) = x + core::at(position_at(vertex_bytes(), stride_, i), 0);
        core::at(position_at(vertex_bytes(), stride_, i), 1) = y + core::at(position_at(vertex_bytes(), stride_, i), 1);
        core::at(position_at(vertex_bytes(), stride_, i), 2) = z + core::at(position_at(vertex_bytes(), stride_, i), 2);
    }
}
// 0x004C5D80
void PcPrimitive::translate_x(float x)
{
    for (int i = 0; i < vertex_count_; ++i) {
        core::at(position_at(vertex_bytes(), stride_, i), 0) = x + core::at(position_at(vertex_bytes(), stride_, i), 0);
    }
}
// 0x004C5DC0
void PcPrimitive::translate_y(float y)
{
    for (int i = 0; i < vertex_count_; ++i) {
        core::at(position_at(vertex_bytes(), stride_, i), 1) = y + core::at(position_at(vertex_bytes(), stride_, i), 1);
    }
}
// 0x004C5E00
void PcPrimitive::translate_z(float z)
{
    for (int i = 0; i < vertex_count_; ++i) {
        core::at(position_at(vertex_bytes(), stride_, i), 2) = z + core::at(position_at(vertex_bytes(), stride_, i), 2);
    }
}

// 0x004C5910
void PcPrimitive::scroll_uv(maths::Vec2 uv)
{
    const auto u = uv.x;
    const auto v = uv.y;
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == kFvfColourUv) {
            vertex_at<VertexColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexColourUv>(vertex_bytes(), i).u;
            vertex_at<VertexColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexColourUv>(vertex_bytes(), i).v;
        } else if (vertex_format_ == kFvfColourUvNormal) {
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u;
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v;
        }
    }
}
// 0x004C5980
void PcPrimitive::scroll_u(float u)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == kFvfColourUv) {
            vertex_at<VertexColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexColourUv>(vertex_bytes(), i).u;
        } else if (vertex_format_ == kFvfColourUvNormal) {
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u = u + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).u;
        }
    }
}
// 0x004C59D0
void PcPrimitive::scroll_v(float v)
{
    for (int i = 0; i < vertex_count_; ++i) {
        if (vertex_format_ == kFvfColourUv) {
            vertex_at<VertexColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexColourUv>(vertex_bytes(), i).v;
        } else if (vertex_format_ == kFvfColourUvNormal) {
            vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v = v + vertex_at<VertexNormalColourUv>(vertex_bytes(), i).v;
        }
    }
}

// 0x004C5340
int PcPrimitive::weigh(const Primitive* other_primitive)
{
    const auto* other = core::down_cast<const PcPrimitive>(other_primitive);
    if (type_ < other->type_) {
        return 1;
    }
    return type_ <= other->type_ ? 0 : -1;
}

// 0x004C5E40
void set_primitive_depth_test(DepthTest depth_test)
{
    const auto state = depth_test.state;
    const auto value = depth_test.value;
    uint32_t function = gpu_value::kCompareLessEqual;
    if (state == 1) {
        switch (value) {
        case 1:
            function = gpu_value::kCompareNever;
            break;
        case 2:
            function = gpu_value::kCompareAlways;
            break;
        case 3:
            function = gpu_value::kCompareLessEqual;
            break;
        case 4:
            function = gpu_value::kCompareLess;
            break;
        default:
            break;
        }
    }
    gpu().set_render_state(GpuRenderState::kZFunc, function);
}

} // namespace render
