#pragma once

#include <cstddef>
#include <cstdint>

// The PC primitive drawer (Nx engine: the engine's class - constructor 0x0049C660, destructor body 0x0049C6A0 - over
// it; lens_flare.cpp's Prim, the chain component's lines): a run of vertices in one of five layouts by flexible
// vertex format, set vertex by vertex through its virtuals and drawn as lines, triangles or quads through the dynamic
// vertex buffer with a material of its own.
namespace render {

// The PC primitive drawer (0x158 bytes, vtable 0x0064A628).
struct PcPrimitive {
    void** vtable;
    uint8_t engine04[4];
    uint32_t flags;          // +0x08: 1 made, 2 set up, 4 ready to draw
    uint32_t blend_mode_;      // +0x0C: a checksum
    int owns_vertices_;        // +0x10: 1: the vertices are its own (freed with it)
    uint8_t engine14[0x10];
    uint8_t* vertices;       // +0x24
    uint32_t vertex_format_;   // +0x28: D3DFVF (2, 0x42, 0x102, 0x142, 0x152)
    uint32_t layout;         // +0x2C: as given to Init (1 colour, 2 texture, 4 normal)
    int type;                // +0x30: 0 lines, 2 triangles, 4 quads (also the sort key, Compare)
    int vertex_count_;         // +0x34
    int stride;              // +0x38
    uint8_t material[0x110]; // +0x3C (constructor 0x004D1A90)
    uint32_t colour;         // +0x14C
    uint8_t engine150[8];

    // 0x004C5230: construct (`owns_vertices_` for the engine's constructor). Returns this.
    PcPrimitive* make_new(int own);
    // 0x004C5A20: the destructor's body.
    void destroy_body();
    // 0x004C5ED0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcPrimitive* teardown(unsigned int free_flags);
    // 0x004C5A90 (virtual [1]): the texture (its key; none: the material's flag 4 off), a new sort key, the
    // material set up again.
    void set_texture(const uint8_t* texture, int unused1, int unused2);
    // 0x004C5310 (virtual [2])
    void change_colour(uint32_t rgba);
    // 0x004C5F00 (virtual [3]): the vertex layout (1 colour, 2 texture, 4 normal), the type and the vertex count; the
    // vertices allocated when they're its own (left uninitialised: their constructor does nothing).
    void prepare_all(uint32_t layout_flags, int primitive_type, int count);
    // 0x004C5320 (virtual [4]): ready to draw unless textured without a texture.
    void get_ready();
    // 0x004C5370 (virtual [5]): drawn (the vertices copied into the dynamic vertex buffer).
    void render();
    // 0x004C55D0 (virtual [6]): drawn from arrays (positions, and optional colours, texture coordinates and normals),
    // through the static buffer 0x00705620.
    void blit(const float* positions, const uint32_t* colours, const float* uvs, const float* normals);
    // 0x004C5B20 / 0x004C5B60 / 0x004C5B80 / 0x004C5BA0 (virtual [7] - [10]): a vertex's position.
    void change_place(int index, float x, float y, float z);
    void set_x(int index, float x);
    void set_y(int index, float y);
    void set_z(int index, float z);
    // 0x004C5BC0 (virtual [11]): a vertex's normal (the 0x152 layout).
    void set_normal(int index, float x, float y, float z);
    // 0x004C5BF0 / 0x004C5C10 / 0x004C5C30 (virtual [12] - [14]): a normal's component (the 0x152 layout).
    void set_normal_x(int index, float x);
    void set_normal_y(int index, float y);
    void set_normal_z(int index, float z);
    // 0x004C56E0 (virtual [15]): a vertex's colour (RGBA in, BGRA stored; the 0x142 layout's alpha 0x80).
    void set_vertex_colour(int index, uint32_t rgba);
    // 0x004C5760 / 0x004C57C0 / 0x004C5800 (virtual [16] - [18]): texture coordinates.
    void set_uv(int index, float u, float v);
    void choose_u(int index, float u);
    void choose_v(int index, float v);
    // 0x004C5C50 / 0x004C5C70 / 0x004C5C90 (virtual [19] - [21]): a vertex's position read back.
    float fetch_pos_x(int index);
    float fetch_pos_y(int index);
    float fetch_pos_z(int index);
    // 0x004C5CB0 / 0x004C5CD0 / 0x004C5CF0 (virtual [22] - [24]): a normal read back (the 0x152 layout).
    float get_normal_x(int index);
    float get_normal_y(int index);
    float get_normal_z(int index);
    // 0x004C5840 (virtual [25]): a vertex's colour as stored (0 without colours).
    uint32_t get_vertex_colour(int index);
    // 0x004C5890 / 0x004C58D0 (virtual [26] / [27]): texture coordinates read back (0 without).
    float get_u(int index);
    float get_v(int index);
    // 0x004C5D10 / 0x004C5D80 / 0x004C5DC0 / 0x004C5E00 (virtual [28] - [31]): every vertex moved.
    void get_slide(float x, float y, float z);
    void translate_x(float x);
    void translate_y(float y);
    void translate_z(float z);
    // 0x004C5910 / 0x004C5980 / 0x004C59D0 (virtual [32] - [34]): every vertex's texture coordinates moved.
    void scroll_uv(float u, float v);
    void scroll_u(float u);
    void scroll_v(float v);
    // 0x004C5340 (virtual [35]): the sort order by type: 1 before `other`, 0 the same, -1 after.
    int weigh(const PcPrimitive* other);
};
static_assert(offsetof(PcPrimitive, vertices) == 0x24 && offsetof(PcPrimitive, stride) == 0x38 &&
                  offsetof(PcPrimitive, material) == 0x3C && offsetof(PcPrimitive, colour) == 0x14C &&
                  sizeof(PcPrimitive) == 0x158,
              "layout must match the original");

// 0x004C5E40 (the engine's primitive state 0x0049C700 jumps here for state 1): the depth test - 1 never, 2 always,
// 3 less or equal, 4 less; anything else less or equal.
void __cdecl set_primitive_depth_test(int state, int value);

} // namespace Nx
