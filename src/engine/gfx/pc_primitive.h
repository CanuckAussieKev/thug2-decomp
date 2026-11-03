#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/surface_material.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"

// The PC primitive drawer (Nx engine: the engine's class - constructor 0x0049C660, destructor body 0x0049C6A0 - over
// it; lens_flare.cpp's Prim, the chain component's lines): a run of vertices in one of five layouts by flexible
// vertex format, set vertex by vertex through its virtuals and drawn as lines, triangles or quads through the dynamic
// vertex buffer with a material of its own.
namespace render {

constexpr uint32_t kNeutralColour = 0x80808080U; // grey: the colour that leaves a texture as it is
constexpr std::size_t kPrimitiveUnknown150Bytes = 8;

constexpr std::size_t kPcPrimitiveVerticesOffset = 0x24;
constexpr std::size_t kPcPrimitiveStrideOffset = 0x38;
constexpr std::size_t kPcPrimitiveMaterialOffset = 0x3C;
constexpr std::size_t kPcPrimitiveColourOffset = 0x14C;
constexpr std::size_t kPcPrimitiveBytes = 0x158;

// The PC primitive drawer (0x158 bytes, vtable 0x0064A628).
class PcPrimitive final : public Primitive {
public:
    PcPrimitive(const PcPrimitive&) = delete;
    PcPrimitive& operator=(const PcPrimitive&) = delete;
    // 0x004C5230: the engine's constructor with `own`, then the material made and set up untextured with the default
    // blend mode, the 0x2 layout, no vertices, grey.
    explicit PcPrimitive(int own);
    // 0x004C5A20 (0x004C5ED0 the scalar deleting destructor): its own vertices freed, the material destroyed.
    ~PcPrimitive() override;

    // 0x004C5A90 (vtable slot 1): the texture (its key; none: the material's flag 4 off), a new sort key, the
    // material set up again.
    void set_texture(const BitmapTexture* texture, int unused1, int unused2) override;
    // 0x004C5310 (vtable slot 2)
    void change_colour(uint32_t rgba) override;
    // 0x004C5F00 (vtable slot 3): the vertex layout (1 colour, 2 texture, 4 normal), the type and the vertex count; the
    // vertices allocated when they're its own (left uninitialised: their constructor does nothing).
    void prepare_all(PrimitiveLayout primitive_layout) override;
    // 0x004C5320 (vtable slot 4): ready to draw unless textured without a texture.
    void end_setup() override;
    // 0x004C5370 (vtable slot 5): drawn (the vertices copied into the dynamic vertex buffer).
    void render() override;
    // 0x004C55D0 (vtable slot 6): drawn from arrays (positions, and optional colours, texture coordinates and normals),
    // through the static buffer 0x00705620.
    void blit(std::span<const float> positions, std::span<const uint32_t> colours, std::span<const float> uvs,
              std::span<const float> normals) override;
    // 0x004C5B20 / 0x004C5B60 / 0x004C5B80 / 0x004C5BA0 (vtable slots 7 - 10): a vertex's position.
    void change_place(int index, const maths::Vec4& place) override;
    void set_x(int index, float x) override;
    void set_y(int index, float y) override;
    void set_z(int index, float z) override;
    // 0x004C5BC0 (vtable slot 11): a vertex's normal (the 0x152 layout).
    void set_normal(int index, const maths::Vec4& normal) override;
    // 0x004C5BF0 / 0x004C5C10 / 0x004C5C30 (vtable slots 12 - 14): a normal's component (the 0x152 layout).
    void set_normal_x(int index, float x) override;
    void set_normal_y(int index, float y) override;
    void set_normal_z(int index, float z) override;
    // 0x004C56E0 (vtable slot 15): a vertex's colour (RGBA in, BGRA stored; the 0x142 layout's alpha 0x80).
    void set_vertex_colour(int index, colour::Packed colour_word) override;
    // 0x004C5760 / 0x004C57C0 / 0x004C5800 (vtable slots 16 - 18): texture coordinates.
    void set_uv(int index, maths::Vec2 uv) override;
    void choose_u(int index, float u) override;
    void choose_v(int index, float v) override;
    // 0x004C5C50 / 0x004C5C70 / 0x004C5C90 (vtable slots 19 - 21): a vertex's position read back.
    float fetch_pos_x(int index) override;
    float fetch_pos_y(int index) override;
    float fetch_pos_z(int index) override;
    // 0x004C5CB0 / 0x004C5CD0 / 0x004C5CF0 (vtable slots 22 - 24): a normal read back (the 0x152 layout).
    float get_normal_x(int index) override;
    float get_normal_y(int index) override;
    float get_normal_z(int index) override;
    // 0x004C5840 (vtable slot 25): a vertex's colour as stored (0 without colours).
    uint32_t get_vertex_colour(int index) override;
    // 0x004C5890 / 0x004C58D0 (vtable slots 26 / 27): texture coordinates read back (0 without).
    float get_u(int index) override;
    float get_v(int index) override;
    // 0x004C5D10 / 0x004C5D80 / 0x004C5DC0 / 0x004C5E00 (vtable slots 28 - 31): every vertex moved.
    void get_slide(const maths::Vec4& slide) override;
    void translate_x(float x) override;
    void translate_y(float y) override;
    void translate_z(float z) override;
    // 0x004C5910 / 0x004C5980 / 0x004C59D0 (vtable slots 32 - 34): every vertex's texture coordinates moved.
    void scroll_uv(maths::Vec2 uv) override;
    void scroll_u(float u) override;
    void scroll_v(float v) override;
    // 0x004C5340 (vtable slot 35): the sort order by type: 1 before `other` (a PC primitive too), 0 the same, -1 after.
    int weigh(const Primitive* other) override;

private:
    uint32_t unknown20_{};   // +0x20

public:
    // NOT ORIGINAL - the vertices' bytes (vertex_count_ of stride_ each).
    [[nodiscard]] std::span<uint8_t> vertex_bytes() const
    {
        return {vertices_, vertices_ ? static_cast<std::size_t>(vertex_count_) * static_cast<std::size_t>(stride_) : 0};
    }

private:
    uint8_t* vertices_{nullptr}; // +0x24
    uint32_t vertex_format_{2};  // +0x28: D3DFVF (2, 0x42, 0x102, 0x142, 0x152)
    uint32_t layout_{};      // +0x2C: as given to prepare_all (1 colour, 2 texture, 4 normal)
    int type_{};             // +0x30: 0 lines, 2 triangles, 4 quads (also the sort key, weigh)
    int vertex_count_{0};    // +0x34
    int stride_{};           // +0x38
    MaterialRef material_;   // +0x3C (its constructor 0x004D1A90)
    uint32_t colour_{kNeutralColour}; // +0x14C
    std::array<uint8_t, kPrimitiveUnknown150Bytes> unknown150_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcPrimitive, vertices_) == kPcPrimitiveVerticesOffset && offsetof(PcPrimitive, stride_) == kPcPrimitiveStrideOffset &&
                          offsetof(PcPrimitive, material_) == kPcPrimitiveMaterialOffset && offsetof(PcPrimitive, colour_) == kPcPrimitiveColourOffset &&
                          sizeof(PcPrimitive) == kPcPrimitiveBytes,
                      "layout must match the original");
    }
};

// The primitives' depth test: the state's index and its value.
struct DepthTest {
    int state{};
    int value{};
};

// 0x004C5E40 (the engine's primitive state 0x0049C700 jumps here for state 1): the depth test - 1 never, 2 always,
// 3 less or equal, 4 less; anything else less or equal.
void set_primitive_depth_test(DepthTest depth_test);

} // namespace render
