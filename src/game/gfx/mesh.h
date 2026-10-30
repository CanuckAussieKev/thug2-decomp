#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game_global.h"

// A scene mesh (Nx engine, PC): what its vertex and index buffers hold and the operations on it that live here -
// moving and turning it in place, copying it, making it a billboard. Most of its code (drawing, the buffers
// themselves) is elsewhere and not rewritten yet.
namespace Nx {

// A vertex buffer (0x24 bytes, constructor 0x004D3FF0; made by 0x004D5760): the size, the Direct3D buffer - its own,
// or a share of an arena's (flag 4) -, a copy of the vertices in memory (none with flag 1) written to the card when
// unlocked, and the lock's state. Every one is on a list (0x007865E4) of C runtime nodes, so they can be made again
// after the device is reset.
struct sVertexBuffer {
    uint32_t size;
    uint32_t usage;
    void* buffer;            // +0x08: IDirect3DVertexBuffer9
    uint8_t* data;           // +0x0C
    uint32_t lockOffset, lockSize, lockFlags; // +0x10 (lockFlags -1: not locked)
    uint32_t flags;          // +0x1C: 1 no copy in memory; 4 a share of an arena's buffer
    uint32_t arenaOffset;    // +0x20: where in the arena's buffer

    // 0x004D3FF0: construct - a buffer of its own (without flag 4: CreateVertexBuffer, default pool), the copy (but
    // with flag 1), on the list. Returns this.
    sVertexBuffer* Construct(uint32_t bytes, uint32_t usageFlags, uint32_t bufferFlags);
    // 0x004D3BE0: taken off the list (its node freed).
    void Unlist();
    // 0x004D4090: locked - *out the copy at `offset` (or, with D3DLOCK_DISCARD, the card's buffer itself: its Lock's
    // result returned; else 0).
    long Lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t lockFlags);
    // 0x004D40E0: unlocked - the copy (from its start) written into the locked part of the card's buffer (shared
    // ones at their arena offset), or the card's buffer unlocked after a discarding lock; nothing after a read-only
    // one or for a share without a buffer.
    long Unlock();
    // 0x004D41E0: made again on the card after a device reset (write-only; a share adds a reference) and the copy
    // written into it.
    void Recreate();
    // 0x004D6720: destroyed - the card's buffer released, off the list, the copy and itself freed (the C runtime's).
    // The buffer's reference count after the release.
    uint32_t Destroy();
};
static_assert(offsetof(sVertexBuffer, data) == 0xC && offsetof(sVertexBuffer, lockFlags) == 0x18 &&
                  sizeof(sVertexBuffer) == 0x24,
              "layout must match the original");

struct sVertexArena; // (pc_scene.h)
struct sScene;       // (mesh_list.h)

// An index buffer (0x18 bytes, constructor 0x004D4270): the size, the Direct3D buffer and a copy of the indices in
// memory, written to the card when unlocked; on a list (0x007865E8) as the vertex buffers are.
struct sIndexBuffer {
    uint32_t size;
    uint32_t usage;
    uint32_t format;
    void* buffer; // IDirect3DIndexBuffer9
    uint16_t* indices;
    uint32_t unknown14; // (cleared when destroyed)

    // 0x004D4270: construct - CreateIndexBuffer (write-only, default pool), the copy, on the list. Returns this.
    sIndexBuffer* Construct(uint32_t bytes, uint32_t usageFlags, uint32_t indexFormat);
    // 0x004D3C40: taken off the list (its node freed).
    void Unlist();
    // 0x004D42F0: locked - *out the copy (all of it, whatever the offset). 0.
    long Lock(uint32_t offset, uint32_t bytes, uint16_t** out, uint32_t lockFlags);
    // 0x004D4300: unlocked - the whole copy written to the card. 0.
    long Unlock();
    // 0x004D6790: destroyed, as the vertex buffers are. The reference count after the release.
    uint32_t Destroy();
};
static_assert(offsetof(sIndexBuffer, indices) == 0x10 && sizeof(sIndexBuffer) == 0x18, "layout must match the original");

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
    uint8_t currentVertexBuffer; // +0x19
    uint8_t vertexBufferCount; // +0x1A
    uint8_t viewports;         // +0x1B: a bit per viewport it shows in
    uint8_t unknown1C[4];      // +0x1C: the colour's offset in a vertex, the normal's, ?, the bone (-1: none)
    uint16_t loadIndex;        // +0x20
    uint16_t vertexCount;      // +0x22
    uint16_t indexCounts[8];   // +0x24
    uint8_t unknown34[4];
    uint32_t fvf;              // +0x38: its vertex format (D3DFVF; 0x10 the normals: lit)
    uint8_t unknown3C[8];
    void* pixelShader;         // +0x44
    void* unknown48;           // +0x48 (from the heap)
    sBillboard* billboard;     // +0x4C
    uint32_t primitiveType;    // +0x50 (5 when constructed)
    sIndexBuffer* indexBuffers[8]; // +0x54
    uint32_t bufferUsage;      // +0x74: the vertex buffers' usage and flags
    uint32_t bufferFlags;      // +0x78
    float depth;               // +0x7C (the render's)
    float color[3];            // +0x80: its colour (doubled when used: 0.5 leaves the passes as they are)
    uint8_t* perVertexBytes;   // +0x8C: a byte per vertex
    float* transform;          // +0x90: 16 floats, made on first use
    uint8_t* perVertexData;    // +0x94: 0x4C bytes per vertex
    int unknown98;             // +0x98: its one bone (-1: none)
    int unknown9C;             // +0x9C: the frame (0x00786AC4) it was last skinned in
    uint32_t unknownA0;        // +0xA0: and for which instance (0x007825B8)
    uint32_t unknownA4, unknownA8; // +0xA4: texture coordinate sets; +0xA8: bones a vertex (1-3)
    uint32_t positionCount;    // +0xAC
    float* positions;          // +0xB0: three floats each
    uint32_t triangleCount;    // +0xB4
    uint16_t* triangles;       // +0xB8: three indices each
    sVertexBuffer* extraBuffers[3];  // +0xBC: destroyed with it (each beside its vertex buffer)
    sVertexBuffer* lastBuffer;       // +0xC8: destroyed with it
    sVertexBuffer* vertexBuffers[3]; // +0xCC

    // 0x004D4BE0: construct - no buffers, one vertex buffer to come, shown (0x40) in every viewport, no bone,
    // primitive type 5. Returns this.
    sMesh* Construct();
    // 0x004D69E0: the destructor's body - off the billboards (0x10), its transform freed, and unless it shares
    // another mesh's buffers (1) its index and vertex buffers destroyed (the device's stream let go of if it is one),
    // its per-vertex data and billboard freed; its positions and triangles freed; all of it cleared.
    void DestroyBody();
    // 0x004D4CA0: each vertex's colour from the material's colour animations (its per-vertex byte: 1-based; 0 none),
    // written into the current vertex buffer, unlocked after. Unlock's result (0 without per-vertex bytes).
    long ApplyVertexColors();
    // 0x004D4D30: its position (its transform's last row; 0, 0, 0, 1 without one).
    void GetPosition(float* out) const;
    // 0x004D57D0: its first strip's long runs of degenerate triangles (more than 5) cut to the four or five a join
    // needs, and the strip written to the card.
    void CleanStrip();
    // 0x004D5900: the bounding sphere (the two stack arguments after are unused).
    void SetBoundingSphere(const float* centre, float radius, int unused1, int unused2);
    // 0x004D5C30: its first vertex buffer made (a share of `arena`'s, whose size grows by it, 32-byte aligned) and
    // filled with `bytes` of `vertices`.
    void CreateVertexBuffer(const void* vertices, uint32_t bytes, sVertexArena* arena);
    // 0x004D4D70 / 0x004D4E50 / 0x004D5020: its vertices (perVertexData) skinned on the CPU with one, two or three
    // bones each (the bone matrices 0x007825B0) into `out` (a stride apart): position, normal, colour, texture sets.
    void SkinOneBone(uint8_t* out);
    void SkinTwoBones(uint8_t* out);
    void SkinThreeBones(uint8_t* out);
    // 0x004D5290: drawn - unless drawing is off (0x007D1EC0) or it is past the clip distance: distance fog, its bone's
    // matrix (+0x98), its colours, vertex format and material (shaders, or pass by pass), skinned once a frame per
    // instance, its next vertex buffer, its level of detail by distance (+0x48), counted (0x00786EBC / C0).
    void Render();
    // 0x004D5CC0 / 0x004D6100: loaded from a scene file (the cursor moved on) / read from a file: its eight index
    // buffers, its vertex buffers (their positions moved by the skin scaling where flagged), vertex formats, the
    // offsets in a vertex, per-vertex colour bytes (dynamic, no arena share), levels of detail, its material from
    // `scene` and its pixel shader; the first vertex buffer made (or, without a format, unpacked for skinning).
    // `dynamic` (the file's only): its buffers dynamic.
    void LoadFromMemory(uint8_t** cursor, uint32_t materialChecksum, sScene* scene, sVertexArena* arena);
    void LoadFromFile(void* file, uint32_t materialChecksum, sScene* scene, sVertexArena* arena, bool dynamic);
    // 0x004D5940: the packed skinned vertices unpacked into perVertexData (normals and weights in 11/11/10 bits,
    // bones / 3), the bones a vertex counted, its format, stride and a vertex buffer without a copy made for them.
    void SetUpSkinning(const uint8_t* packed, uint32_t bytes, sVertexArena* arena);
    // 0x004D6530: its collision data made once: the positions from its first vertex buffer, the non-degenerate
    // triangles of its first strip.
    void BuildCollision();
    // 0x004D3FC0 / 0x004D3FD0: a vertex buffer (0-2).
    sVertexBuffer* GetVertexBuffer(int index);
    void SetVertexBuffer(int index, sVertexBuffer* buffer);
    // 0x004D3E50: its first vertex buffer moved into the arena (a share at the next free offset, 32-byte aligned;
    // the arena's buffer gains a reference) unless it has a buffer of its own already.
    void MoveIntoArena(sVertexArena* arena);
    // 0x004D3D10: its pixel shader set with the material's pass colours (0x00786ECC, 16 bytes a pass) times its own
    // colour (+0x80, doubled) - or the instance's (0x00786F20, /128) while one is on (0x00786F1C) - for the passes
    // without the top bit of their flags; the constants put back after.
    void SetPixelShaderColored();
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
                  offsetof(sMesh, pixelShader) == 0x44 && offsetof(sMesh, bufferFlags) == 0x78 &&
                  offsetof(sMesh, extraBuffers) == 0xBC && sizeof(sMesh) == 0xD8,
              "layout must match the original");

// 0x004D47F0 / 0x004D4350: a vertex buffer's positions from records of three weighted bones each (a bone's position
// and the vertex's offset from it, scaled by the skin scaling's bone scales) - only while the scaling is on; the
// records are read past either way.
void __cdecl LoadScaledPositionsFromMemory(uint8_t** cursor, uint8_t* vertices, uint32_t stride);
void __cdecl LoadScaledPositionsFromFile(void* file, uint8_t* vertices, uint32_t stride);
// 0x004D5760: a new vertex buffer (from the C runtime's heap; null if none).
sVertexBuffer* __cdecl NewVertexBuffer(uint32_t bytes, uint32_t usage, uint32_t flags);
// 0x004D67F0 / 0x004D68C0: before / after a device reset - every vertex buffer's, scene arena's and index buffer's
// card buffer let go of (the vertex buffers keep the old pointers, to find the shares by), then made again and
// filled from the copies (the shares moved to their arena's new buffer).
void __cdecl ReleaseBuffers();
void __cdecl RecreateBuffers();

GAME_GLOBAL_EXTERN(void*, g_vertexBufferList);
GAME_GLOBAL_EXTERN(void*, g_indexBufferList);
} // namespace Nx
