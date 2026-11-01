#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/window2d.h"

// The PC billboards (the manager at 0x00775B80): the billboard meshes gathered by material into batches, each with
// three lists by how its meshes turn (0 towards the camera, 1 about the vertical, 2 about an axis of their own),
// drawn by turning each mesh's quad on the CPU into its vertex buffer; and the small things beside them - the 2D
// windows' pool, the device flags, a primitive count by type.
namespace render {

struct MaterialRef;
struct MeshRef;

// A batched billboard (8 bytes): how it turns (its billboard's type) and its mesh.
struct BillboardRec {
    uint32_t type;
    MeshRef* mesh;
};

// A material's batch (0x14 bytes).
struct BillboardBatch {
    uint32_t vertex_bytes_; // (the material's passes + 2) x 0x20
    MaterialRef* material;
    memory::ListNode* lists[3]; // heads (data -1) of nodes over entries

    // 0x004CEB30: construct for a material: three empty lists.
    explicit BillboardBatch(MaterialRef* material);
    // 0x004CE1D0: a mesh added to the list of its billboard's type (at the end).
    void add_row(MeshRef* mesh);
    // 0x004CE250: a mesh's entry taken out (its node destroyed, the entry freed).
    void erase_record(MeshRef* mesh);
    // 0x004CE2D0: drawn: the material set up, projection x view (transposed, untranslated) into c0-c3 and
    // the camera's axes into c4-c6, then each shown mesh of each list turned and drawn (the third list's turned
    // about their own axes); the vertex format unlocked after.
    void render();
};
static_assert(sizeof(BillboardBatch) == 0x14, "layout must match the original");

// The billboard manager (0x00775B80).
struct BillboardSet {
    int count;
    BillboardBatch* batches[0x100]; // sorted by the materials' draw order (+0x1C), nulls last
    float view[4];                  // +0x404 the view's forward (its third column), w 1
    float right[4];                 // +0x414 forward x up, normalised
    float up[4];                    // +0x424 right x forward
    float flat_forward_[4];         // +0x434 the forward along the ground, w 1
    float flat_right_[4];           // +0x444 and right

    // 0x004CDA60: the batch of a material, or null.
    BillboardBatch* get_find(const MaterialRef* material);
    // 0x004CDAA0 (choose_camera's): the axes from the view matrix (0x00786BD0).
    void setup_axes();
    // 0x004CEBE0: a billboard mesh added (a mesh with a textured material only), its batch made if new.
    void append_mesh(MeshRef* mesh);
    // 0x004CE9F0 (the mesh's destructor's): a mesh taken out; an emptied batch destroyed (the array re-sorted and
    // one shorter whether or not it was found in it, as the original).
    void remove_mesh(MeshRef* mesh);
    // 0x004CEAB0: the batches drawn: with `which` & 1 the materials without flag 0x40 (+0x20), with & 2 those with.
    void render(unsigned int which);
};
static_assert(offsetof(BillboardSet, view) == 0x404 && offsetof(BillboardSet, flat_right_) == 0x444,
              "layout must match the original");

// 0x004CD9D0 (the batches' qsort comparison; code Ghidra never made a function of): by the materials' +0x1C
// (none: 0), nulls after everything.
int __cdecl compare_batches(const void* a, const void* b);
// 0x004CDD00: a billboard mesh drawn - its vertices turned
// about its pivot (x along `x_axis`, y `y_axis`, z `zAxis`) into its vertex buffer (a per-vertex table's value for
// the word after the position where it has one), drawn indexed with the device's stream, shaders, indices and
// format put back after (the stream's offset as 0, the shaders not released, as the original); fog's start moved to a
// quarter of the way back from its end while drawn.
void draw_billboard_mesh(MeshRef* mesh, const float* y_axis, const float* x_axis, const float* z_axis);
// 0x004CD980: the primitives for that many indices (1 points,
// 2 lines, 3 a line strip, 4 triangles, 5 and 6 strips / fans; 0 else).
uint32_t primitive_count(uint32_t type, uint32_t count);

// (A batch list's node and head are memory::ListNodes: their destructors 0x004CE9D0 / 0x004CE960 / 0x004CE9A0 are
// ListNode's uncouple / destroy.)

// The 2D window (0x18 bytes, vtable 0x0064A88C; the free ones linked through +0x14 from 0x007013D0).
class PcWindow final : public ScreenWindow {
public:
    using ScreenWindow::ScreenWindow;
    // 0x004CD860 (0x004CD870 the scalar deleting destructor): the engine's.
    ~PcWindow() override = default;
    // (Vtable slot 1: nothing - a shared empty function.)
    void tick_engine() override {}
};
static_assert(sizeof(PcWindow) == 0x18, "layout must match the original");
// 0x004CD8A0: ten windows (640 x 480) made onto the free list (null for any without memory - written through, as
// the original).
void __cdecl create_window_pool();

// 0x004CD940 (apply_render_state's): the flag 0x00775B7C set. 0x004CD960 / 0x004CD970: 0x00786EB4 set / cleared.
void __cdecl mark_billboards_ready();
void __cdecl set_flag_786e_b4();
void __cdecl clear_flag_786e_b4();

extern uint8_t g_billboard_flag;
extern BillboardSet g_billboard_pool;

} // namespace render
