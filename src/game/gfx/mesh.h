#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

// A scene mesh (Nx engine, PC): what its vertex and index buffers hold and the operations on it that live here -
// moving and turning it in place, copying it, making it a billboard. Most of its code (drawing, the buffers
// themselves) is elsewhere and not rewritten yet.
namespace Nx {

// A vertex buffer (made by 0x004D5760; unlocked by 0x004D40E0): the size, the vertices (kept in memory) and the
// lock's state.
struct sVertexBuffer {
    uint32_t size;
    uint8_t unknown4[8];
    uint8_t* data;
    uint32_t lockOffset, lockSize, lockFlags;
};
static_assert(offsetof(sVertexBuffer, data) == 0xC && offsetof(sVertexBuffer, lockFlags) == 0x18, "layout must match the original");

// An index buffer (constructor 0x004D4270): the size, the Direct3D buffer and a copy of the indices in memory.
struct sIndexBuffer {
    uint32_t size;
    uint8_t unknown4[8];
    void* buffer; // IDirect3DIndexBuffer9
    uint16_t* indices;
};
static_assert(offsetof(sIndexBuffer, indices) == 0x10, "layout must match the original");

// A billboard (0x24 bytes): how it turns (0 / 1 towards the camera about any axis, 2 about `axis`), its pivot.
struct sBillboard {
    uint32_t type;
    float pivot[4];
    float axis[4];
};

struct sMesh {
    uint32_t flags;            // 1: shares another mesh's buffers; 0x10: a billboard
    float centre[3];           // the bounding sphere
    float radius;
    uint8_t* material;
    uint8_t stride;            // bytes per vertex
    uint8_t pad19;
    uint8_t vertexBufferCount; // +0x1A
    uint8_t viewports;         // +0x1B: a bit per viewport it shows in
    uint8_t unknown1C[4];
    uint16_t loadIndex;        // +0x20
    uint16_t vertexCount;      // +0x22
    uint16_t indexCounts[8];   // +0x24
    uint8_t unknown34[0x18];
    sBillboard* billboard;     // +0x4C
    uint8_t unknown50[4];
    sIndexBuffer* indexBuffers[8]; // +0x54
    uint8_t unknown74[8];
    float depth;               // +0x7C (the render's)
    uint8_t unknown80[0xC];
    uint8_t* perVertexBytes;   // +0x8C: a byte per vertex
    float* transform;          // +0x90: 16 floats, made on first use
    uint8_t* perVertexData;    // +0x94: 0x4C bytes per vertex
    uint8_t unknown98[4];
    int unknown9C;
    uint8_t unknownA0[4];
    uint32_t unknownA4, unknownA8;
    uint32_t positionCount;    // +0xAC
    float* positions;          // +0xB0: three floats each
    uint32_t triangleCount;    // +0xB4
    uint16_t* triangles;       // +0xB8: three indices each
    uint8_t unknownBC[0x10];
    sVertexBuffer* vertexBuffers[3]; // +0xCC

    // 0x004D6D40: `count` more vertex buffers, copies of the first (which is unlocked after).
    void AddVertexBuffers(int count);
    // 0x004D6E00: move it so its transform's position is `position` (vertices, positions and sphere moved by the
    // difference).
    void MoveTo(const Mth::Vector* position);
    // 0x004D6FB0 (Ghidra starts it at 0x004D6FBA): turn the vertices `turns` quarter turns about the transform's
    // position (about y; 1-3 only) - and the sphere's centre once per vertex buffer.
    void TurnY(int turns);
    // 0x004D71F0: a copy - sharing the buffers (flag 1) or with copies of them. Returns the copy.
    sMesh* Clone(bool share);
    // 0x004D7580: make it a billboard of `type` (1: facing the camera; 2: about `axis`, or as 1 when the axis is
    // nearly vertical) about `pivot`: its quad's vertices turned into the billboard's own axes.
    void MakeBillboard(int type, const Mth::Vector* pivot, const Mth::Vector* axis);
};
static_assert(offsetof(sMesh, material) == 0x14 && offsetof(sMesh, vertexBufferCount) == 0x1A &&
                  offsetof(sMesh, indexCounts) == 0x24 && offsetof(sMesh, billboard) == 0x4C &&
                  offsetof(sMesh, indexBuffers) == 0x54 && offsetof(sMesh, depth) == 0x7C &&
                  offsetof(sMesh, perVertexBytes) == 0x8C && offsetof(sMesh, perVertexData) == 0x94 &&
                  offsetof(sMesh, unknownA4) == 0xA4 && offsetof(sMesh, positionCount) == 0xAC &&
                  offsetof(sMesh, triangles) == 0xB8 && offsetof(sMesh, vertexBuffers) == 0xCC &&
                  sizeof(sMesh) == 0xD8,
              "layout must match the original");

} // namespace Nx
