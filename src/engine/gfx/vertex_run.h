#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "engine/gfx/engine_primitive.h"
#include "engine/memory/mem.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include "base/core/packed_colour.h"
#include "base/maths/vec2.h"
#include "base/maths/vec4.h"

// NOT ORIGINAL grouping - a primitive drawer's vertices (the engine's primitive with its run of vertices: the PC
// primitive drawer, pc_primitive.h, over it): the vertices' memory and their layout, and the writes the engine makes per
// vertex - a position, a colour, texture coordinates - and the layout chosen. These are plain writes into memory the
// drawer owns, made per vertex by the spawned polygons, the chains' lines and the lens flare: inline here, so the
// engine makes no call into the graphics backend per vertex (the original's were direct calls through the jumps
// 0x004A1090 / 0x004A10A0, never through the vtable). The PC drawer's virtual setters (vtable slots 3, 7, 15, 16) are
// these same writes.
namespace render {

namespace vertex_run {

// The flexible vertex formats (D3DFVF_*) the primitives use.
constexpr int kFvfColour = 0x42;
constexpr int kFvfUv = 0x102;
constexpr int kFvfColourUv = 0x142;
constexpr int kFvfColourUvNormal = 0x152;
constexpr uint32_t kFvfNormal = 0x010U; // (render_state.h's)
constexpr uint32_t kFvfTex1 = 0x100U;
// ... and their vertex sizes.
constexpr int kPositionVertexBytes = 0xC;
constexpr int kColourVertexBytes = 0x10;
constexpr int kUvVertexBytes = 0x14;
constexpr int kColourUvVertexBytes = 0x18;
constexpr int kColourUvNormalVertexBytes = 0x24;
constexpr int kRgbMask = 0xFFFFFF;
constexpr uint32_t kHalfAlphaBits = 0x80000000;
constexpr uint32_t kGreenAlphaMask = 0xFF00FF00U;
constexpr int kByteMask = 0xFF;
constexpr int kByte2Shift = 16;

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
constexpr std::size_t kVertexColourBytes = 0x10;
constexpr std::size_t kVertexColourUvBytes = 0x18;
constexpr std::size_t kVertexNormalColourUvBytes = 0x24;

static_assert(sizeof(VertexColour) == kVertexColourBytes && sizeof(VertexColourUv) == kVertexColourUvBytes && sizeof(VertexNormalColourUv) == kVertexNormalColourUvBytes,
              "the Direct3D vertex layouts");

// The vertex `index` in the layout V / its position (any layout, by the stride): engine/gfx/vertex_view.h.
template <typename V>
V& vertex_at(std::span<uint8_t> vertices, int index)
{
    return gpu::record_at<V>(vertices, static_cast<size_t>(index));
}
inline std::span<float, 3> position_at(std::span<uint8_t> vertices, int stride, int index)
{
    return gpu::position_at(vertices, static_cast<size_t>(stride), static_cast<size_t>(index));
}

// RGBA with red and blue swapped (d_3d's BGRA).
inline uint32_t get_swapped(uint32_t rgba) { return (rgba & kGreenAlphaMask) | (rgba & kByteMask) << kByte2Shift | (rgba >> kByte2Shift & kByteMask); }

} // namespace vertex_run

// The engine's primitive with its vertices (+0x20 - +0x3B of the PC primitive drawer, 0x158 bytes).
class VertexRun : public Primitive {
public:
    explicit VertexRun(int own) : Primitive(own) {}

    // NOT ORIGINAL - the vertices' bytes (vertex_count_ of stride_ each).
    [[nodiscard]] std::span<uint8_t> vertex_bytes() const
    {
        return {vertices_, vertices_ ? static_cast<std::size_t>(vertex_count_) * static_cast<std::size_t>(stride_) : 0};
    }

    // 0x004C5F00 (the PC drawer's prepare_all): the vertex layout (1 colour, 2 texture, 4 normal), the type and the
    // vertex count; the vertices allocated when they're its own (left uninitialised: their constructor does nothing).
    void lay_out(PrimitiveLayout primitive_layout)
    {
        const auto layout_flags = primitive_layout.layout_flags_;
        const auto primitive_type = primitive_layout.primitive_type_;
        const auto count = primitive_layout.count;
        vertex_format_ = 2;
        if ((layout_flags & 1) != 0U) {
            vertex_format_ = vertex_run::kFvfColour;
        }
        if ((layout_flags & 2) != 0U) {
            vertex_format_ |= vertex_run::kFvfTex1;
        }
        if ((layout_flags & 4) != 0U) {
            vertex_format_ |= vertex_run::kFvfNormal;
        }
        if (owns_vertices() == 1) {
            int size = 0;
            switch (vertex_format_) {
            case 0x2:
                size = vertex_run::kPositionVertexBytes;
                break;
            case vertex_run::kFvfColour:
                size = vertex_run::kColourVertexBytes;
                break;
            case vertex_run::kFvfUv:
                size = vertex_run::kUvVertexBytes;
                break;
            case vertex_run::kFvfColourUv:
                size = vertex_run::kColourUvVertexBytes;
                break;
            case vertex_run::kFvfColourUvNormal:
                size = vertex_run::kColourUvNormalVertexBytes;
                break;
            default:
                break;
            }
            if (size != 0) {
                // (the 0x102 layout_'s allocation is memory::alloc's inline; the others through it)
                vertices_ = static_cast<uint8_t*>(memory::alloc(count * size));
                stride_ = size;
            }
        }
        vertex_count_ = count;
        flag_bits() = (flag_bits() & ~1U) | 2;
        layout_ = layout_flags;
        type_ = primitive_type;
    }
    // 0x004C5B20 (change_place): a vertex's position.
    void place_vertex(int index, const maths::Vec4& place) const
    {
        const auto x = place.x;
        const auto y = place.y;
        const auto z = place.z;
        core::at(vertex_run::position_at(vertex_bytes(), stride_, index), 0) = x;
        core::at(vertex_run::position_at(vertex_bytes(), stride_, index), 1) = y;
        core::at(vertex_run::position_at(vertex_bytes(), stride_, index), 2) = z;
    }
    // 0x004C56E0 (set_vertex_colour): a vertex's colour (RGBA in, BGRA stored; the 0x142 layout's alpha 0x80).
    void colour_vertex(int index, colour::Packed colour_word) const
    {
        const uint32_t rgba = colour_word.value();
        const uint32_t bgra = vertex_run::get_swapped(rgba);
        if (vertex_format_ == vertex_run::kFvfColour) {
            vertex_run::vertex_at<vertex_run::VertexColour>(vertex_bytes(), index).colour = bgra;
        } else if (vertex_format_ == vertex_run::kFvfColourUv) {
            vertex_run::vertex_at<vertex_run::VertexColourUv>(vertex_bytes(), index).colour =
                (bgra & vertex_run::kRgbMask) | vertex_run::kHalfAlphaBits;
        } else if (vertex_format_ == vertex_run::kFvfColourUvNormal) {
            vertex_run::vertex_at<vertex_run::VertexNormalColourUv>(vertex_bytes(), index).colour = bgra;
        }
    }
    // 0x004C5760 (set_uv): a vertex's texture coordinates.
    void map_vertex(int index, maths::Vec2 uv) const
    {
        const auto u = uv.x;
        const auto v = uv.y;
        if (vertex_format_ == vertex_run::kFvfColourUv) {
            vertex_run::vertex_at<vertex_run::VertexColourUv>(vertex_bytes(), index).u = u;
            vertex_run::vertex_at<vertex_run::VertexColourUv>(vertex_bytes(), index).v = v;
        } else if (vertex_format_ == vertex_run::kFvfColourUvNormal) {
            vertex_run::vertex_at<vertex_run::VertexNormalColourUv>(vertex_bytes(), index).u = u;
            vertex_run::vertex_at<vertex_run::VertexNormalColourUv>(vertex_bytes(), index).v = v;
        }
    }

    // The vertices' memory and layout (the PC drawer reads and sets them as its own).
    uint32_t unknown20_{};   // +0x20
    uint8_t* vertices_{nullptr}; // +0x24
    uint32_t vertex_format_{2};  // +0x28: D3DFVF (2, 0x42, 0x102, 0x142, 0x152)
    uint32_t layout_{};      // +0x2C: as given to prepare_all (1 colour, 2 texture, 4 normal)
    int type_{};             // +0x30: 0 lines, 2 triangles, 4 quads (also the sort key, weigh)
    int vertex_count_{0};    // +0x34
    int stride_{};           // +0x38
};

} // namespace render
