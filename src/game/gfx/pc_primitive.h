#pragma once

#include <cstddef>
#include <cstdint>

// The PC primitive drawer (Nx engine: the engine's class - constructor 0x0049C660, destructor body 0x0049C6A0 - over
// it; lens_flare.cpp's Prim, the chain component's lines): a run of vertices in one of five layouts by flexible
// vertex format, set vertex by vertex through its virtuals and drawn as lines, triangles or quads through the dynamic
// vertex buffer with a material of its own.
namespace Nx {

// The PC primitive drawer (0x158 bytes, vtable 0x0064A628).
struct CPcPrimitive {
    void** vtable;
    uint8_t engine04[4];
    uint32_t flags;          // +0x08: 1 made, 2 set up, 4 ready to draw
    uint32_t blendMode;      // +0x0C: a checksum
    int ownsVertices;        // +0x10: 1: the vertices are its own (freed with it)
    uint8_t engine14[0x10];
    uint8_t* vertices;       // +0x24
    uint32_t vertexFormat;   // +0x28: D3DFVF (2, 0x42, 0x102, 0x142, 0x152)
    uint32_t layout;         // +0x2C: as given to Init (1 colour, 2 texture, 4 normal)
    int type;                // +0x30: 0 lines, 2 triangles, 4 quads (also the sort key, Compare)
    int vertexCount;         // +0x34
    int stride;              // +0x38
    uint8_t material[0x110]; // +0x3C (constructor 0x004D1A90)
    uint32_t colour;         // +0x14C
    uint8_t engine150[8];

    // 0x004C5230: construct (`ownsVertices` for the engine's constructor). Returns this.
    CPcPrimitive* Construct(int own);
    // 0x004C5A20: the destructor's body.
    void DestroyBody();
    // 0x004C5ED0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcPrimitive* Destroy(unsigned int freeFlags);
    // 0x004C5A90 (virtual [1]): the texture (its key; none: the material's flag 4 off), a new sort key, the
    // material set up again.
    void SetTexture(const uint8_t* texture, int unused1, int unused2);
    // 0x004C5310 (virtual [2])
    void SetColour(uint32_t rgba);
    // 0x004C5F00 (virtual [3]): the vertex layout (1 colour, 2 texture, 4 normal), the type and the vertex count; the
    // vertices allocated when they're its own (left uninitialised: their constructor does nothing).
    void Init(uint32_t layoutFlags, int primitiveType, int count);
    // 0x004C5320 (virtual [4]): ready to draw unless textured without a texture.
    void Ready();
    // 0x004C5370 (virtual [5]): drawn (the vertices copied into the dynamic vertex buffer).
    void Render();
    // 0x004C55D0 (virtual [6]): drawn from arrays (positions, and optional colours, texture coordinates and normals),
    // through the static buffer 0x00705620.
    void Draw(const float* positions, const uint32_t* colours, const float* uvs, const float* normals);
    // 0x004C5B20 / 0x004C5B60 / 0x004C5B80 / 0x004C5BA0 (virtual [7] - [10]): a vertex's position.
    void SetPosition(int index, float x, float y, float z);
    void SetX(int index, float x);
    void SetY(int index, float y);
    void SetZ(int index, float z);
    // 0x004C5BC0 (virtual [11]): a vertex's normal (the 0x152 layout).
    void SetNormal(int index, float x, float y, float z);
    // 0x004C5BF0 / 0x004C5C10 / 0x004C5C30 (virtual [12] - [14]): a normal's component (the 0x152 layout).
    void SetNormalX(int index, float x);
    void SetNormalY(int index, float y);
    void SetNormalZ(int index, float z);
    // 0x004C56E0 (virtual [15]): a vertex's colour (RGBA in, BGRA stored; the 0x142 layout's alpha 0x80).
    void SetVertexColour(int index, uint32_t rgba);
    // 0x004C5760 / 0x004C57C0 / 0x004C5800 (virtual [16] - [18]): texture coordinates.
    void SetUV(int index, float u, float v);
    void SetU(int index, float u);
    void SetV(int index, float v);
    // 0x004C5C50 / 0x004C5C70 / 0x004C5C90 (virtual [19] - [21]): a vertex's position read back.
    float GetX(int index);
    float GetY(int index);
    float GetZ(int index);
    // 0x004C5CB0 / 0x004C5CD0 / 0x004C5CF0 (virtual [22] - [24]): a normal read back (the 0x152 layout).
    float GetNormalX(int index);
    float GetNormalY(int index);
    float GetNormalZ(int index);
    // 0x004C5840 (virtual [25]): a vertex's colour as stored (0 without colours).
    uint32_t GetVertexColour(int index);
    // 0x004C5890 / 0x004C58D0 (virtual [26] / [27]): texture coordinates read back (0 without).
    float GetU(int index);
    float GetV(int index);
    // 0x004C5D10 / 0x004C5D80 / 0x004C5DC0 / 0x004C5E00 (virtual [28] - [31]): every vertex moved.
    void Translate(float x, float y, float z);
    void TranslateX(float x);
    void TranslateY(float y);
    void TranslateZ(float z);
    // 0x004C5910 / 0x004C5980 / 0x004C59D0 (virtual [32] - [34]): every vertex's texture coordinates moved.
    void ScrollUV(float u, float v);
    void ScrollU(float u);
    void ScrollV(float v);
    // 0x004C5340 (virtual [35]): the sort order by type: 1 before `other`, 0 the same, -1 after.
    int Compare(const CPcPrimitive* other);
};
static_assert(offsetof(CPcPrimitive, vertices) == 0x24 && offsetof(CPcPrimitive, stride) == 0x38 &&
                  offsetof(CPcPrimitive, material) == 0x3C && offsetof(CPcPrimitive, colour) == 0x14C &&
                  sizeof(CPcPrimitive) == 0x158,
              "layout must match the original");

// 0x004C5E40 (the engine's primitive state 0x0049C700 jumps here for state 1): the depth test - 1 never, 2 always,
// 3 less or equal, 4 less; anything else less or equal.
void __cdecl SetPrimitiveDepthTest(int state, int value);

} // namespace Nx
