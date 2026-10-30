#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "hook/game_global.h"

// The PC billboards (the manager at 0x00775B80): the billboard meshes gathered by material into batches, each with
// three lists by how its meshes turn (0 towards the camera, 1 about the vertical, 2 about an axis of their own),
// drawn by turning each mesh's quad on the CPU into its vertex buffer; and the small things beside them - the 2D
// windows' pool, the device flags, a primitive count by type.
namespace Nx {

struct sMesh;

// A batched billboard (8 bytes): how it turns (its billboard's type) and its mesh.
struct sBillboardEntry {
    uint32_t type;
    sMesh* mesh;
};

// A material's batch (0x14 bytes).
struct sBillboardBatch {
    uint32_t vertexBytes;     // (the material's passes + 2) x 0x20
    uint8_t* material;
    Mem::ListNode* lists[3];  // heads (data -1, vtable 0x0064A898) of nodes (vtable 0x0064A894) over entries

    // 0x004CEB30: construct for a material: three empty lists. Returns this.
    sBillboardBatch* Construct(uint8_t* material);
    // 0x004CE1D0: a mesh added to the list of its billboard's type (at the end).
    void AddEntry(sMesh* mesh);
    // 0x004CE250: a mesh's entry taken out (its node destroyed, the entry freed).
    void RemoveEntry(sMesh* mesh);
    // 0x004CE2D0: drawn: the material set up, projection x view (transposed, untranslated) into c0-c3 and
    // the camera's axes into c4-c6, then each shown mesh of each list turned and drawn (the third list's turned
    // about their own axes); the vertex format unlocked after.
    void Render();
};
static_assert(sizeof(sBillboardBatch) == 0x14, "layout must match the original");

// The billboard manager (0x00775B80).
struct sBillboardManager {
    int count;
    sBillboardBatch* batches[0x100]; // sorted by the materials' draw order (+0x1C), nulls last
    float view[4];           // +0x404 the view's forward (its third column), w 1
    float right[4];          // +0x414 forward x up, normalised
    float up[4];             // +0x424 right x forward
    float flatForward[4];    // +0x434 the forward along the ground, w 1
    float flatRight[4];      // +0x444 and right

    // 0x004CDA60: the batch of a material, or null.
    sBillboardBatch* Find(const uint8_t* material);
    // 0x004CDAA0 (SetCamera's): the axes from the view matrix (0x00786BD0).
    void SetupAxes();
    // 0x004CEBE0: a billboard mesh added (a mesh with a textured material only), its batch made if new.
    void AddMesh(sMesh* mesh);
    // 0x004CE9F0 (the mesh's destructor's): a mesh taken out; an emptied batch destroyed (the array re-sorted and
    // one shorter whether or not it was found in it, as the original).
    void RemoveMesh(sMesh* mesh);
    // 0x004CEAB0: the batches drawn: with `which` & 1 the materials without flag 0x40 (+0x20), with & 2 those with.
    void Render(unsigned int which);
};
static_assert(offsetof(sBillboardManager, view) == 0x404 && offsetof(sBillboardManager, flatRight) == 0x444,
              "layout must match the original");

// 0x004CD9D0 (the batches' qsort comparison; code Ghidra never made a function of): by the materials' +0x1C
// (none: 0), nulls after everything.
int __cdecl CompareBatches(const void* a, const void* b);
// 0x004CDD00 (EBX the mesh, three stack arguments; through a stub): a billboard mesh drawn - its vertices turned
// about its pivot (x along `xAxis`, y `yAxis`, z `zAxis`) into its vertex buffer (a per-vertex table's value for
// the word after the position where it has one), drawn indexed with the device's stream, shaders, indices and
// format put back after (the stream's offset as 0, the shaders not released, as the original); fog's start moved to a
// quarter of the way back from its end while drawn.
void DrawBillboardMesh(sMesh* mesh, const float* yAxis, const float* xAxis, const float* zAxis);
void DrawBillboardMeshStub();
// 0x004CD980 (EAX the type, ECX the index count; through a stub): the primitives for that many indices (1 points,
// 2 lines, 3 a line strip, 4 triangles, 5 and 6 strips / fans; 0 else).
uint32_t PrimitiveCount(uint32_t type, uint32_t count);
void PrimitiveCountStub();

// A batch list's node (Mem::ListNode with vtable 0x0064A894; the heads' 0x0064A898).
struct sBillboardNode : Mem::ListNode {
    // 0x004CE9D0: the body both destroyers share - the node's vtable set, unlinked (linked to itself).
    void Unlink();
    // 0x004CE960 (a node's virtual [0]) / 0x004CE9A0 (a head's): unlinked, and freed with flags & 1. Return this.
    sBillboardNode* Destroy(unsigned int freeFlags);
    sBillboardNode* DestroyHead(unsigned int freeFlags);
};

// The 2D window (0x18 bytes, vtable 0x0064A88C; the engine's - constructor 0x004A1580 (x, y, width, height),
// vtable 0x00649E58 - over it; the free ones linked through +0x14 from 0x007013D0).
struct CPcWindow {
    void** vtable;
    int x, y, width, height;
    CPcWindow* next;

    // 0x004CD860: the destructor's body (the engine's 0x004A15B0).
    void DestroyBody();
    // 0x004CD870 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcWindow* Destroy(unsigned int freeFlags);
};
static_assert(sizeof(CPcWindow) == 0x18, "layout must match the original");
// 0x004CD8A0: ten windows (640 x 480) made onto the free list (null for any without memory - written through, as
// the original).
void __cdecl CreateWindowPool();

// 0x004CD940 (ApplyRenderState's): the flag 0x00775B7C set. 0x004CD960 / 0x004CD970: 0x00786EB4 set / cleared.
void __cdecl MarkBillboardsReady();
void __cdecl SetFlag786EB4();
void __cdecl ClearFlag786EB4();

GAME_GLOBAL_EXTERN(uint8_t, g_billboardFlag);
GAME_GLOBAL_EXTERN(sBillboardManager, g_billboardManager);

GAME_GLOBAL_EXTERN(sBillboardManager, g_billboardManager);
} // namespace Nx
