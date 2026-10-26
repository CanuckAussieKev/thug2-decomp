#pragma once

// A scene's collision grid (our name): its box cut into 20 x 20 columns on x and z (each reaching from -1e12 to
// 1e12 on y), each cell listing the static collision objects whose boxes touch it. The line and box queries look
// objects up through it.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/geometry.h"

namespace Nx {

class CCollObj;
class CCollStatic;

// A cell (0x30 bytes).
struct CollGridCell {
    // 0x004183B0: an empty box, nothing listed. Returns this.
    CollGridCell* Construct();
    // 0x00418410: the two blocks freed.
    void Destruct();

    Mth::BBox box;          // +0x00
    uint32_t unknown20;     // +0x20
    int count;              // +0x24
    void* unknown28;        // +0x28 (freed through the memory manager)
    CCollObj** objects;     // +0x2C a Malloc block of `count`
};
static_assert(sizeof(CollGridCell) == 0x30 && offsetof(CollGridCell, objects) == 0x2C, "layout must match the original");

class CCollGrid {
public:
    static constexpr int kSize = 20;

    // 0x00418440: the box taken and cut into cells (their corners added up from the minimum in double precision).
    void Init(const Mth::BBox* bounds);
    // 0x004183D0: every cell's list freed and emptied.
    void Clear();
    // 0x00418BD0: the cells filled from `count` static objects (those whose geometry has faces), each listed in
    // the cells its box (slot 21) covers. A cell index below 0 isn't clamped (as in the original).
    void Fill(CCollStatic* statics, int count);
    // 0x00418F60: the lists brought up to date, cell by cell: the objects of `removed` taken out where their boxes
    // touch the cell, those of `added` put in where they touch it, those of `moved` taken out or put in as they
    // now do or don't; each list's block resized once and, at the end, every list moved to a fresh block. (Lists
    // of Lst::Head<CCollObj>.)
    void Update(Mem::ListNode* added, Mem::ListNode* removed, Mem::ListNode* moved);

    // 0x00418710: the objects (neither flag 2 nor 4) of the cells `box` (grown by 0.5 on x and z) covers, each
    // once; null-terminated, in the shared list 0x00691998.
    CCollObj** ObjectsInBox(const Mth::BBox* box);
    // 0x004188C0: the same for the cells the line (made half a unit longer at each end) spans; one cell: its
    // objects as they are; more: each once, 2047 at most. The number found goes to `count` if it's given.
    CCollObj** ObjectsAlongLine(const Mth::Line* line, int* count);

    CollGridCell* Cell(int x, int z) { return &m_cells[x][z]; }

    Mth::BBox m_box;                       // +0x00
    float m_cellWidth;                     // +0x20 on x
    float m_cellDepth;                     // +0x24 on z
    int m_columns;                         // +0x28 on x (20)
    int m_rows;                            // +0x2C on z (20)
    CollGridCell m_cells[kSize][kSize];    // +0x30 [x][z]
};
static_assert(offsetof(CCollGrid, m_cells) == 0x30 && sizeof(CCollGrid) == 0x4B30, "layout must match the original");

// The node and head of the list Update keeps the objects waiting to be put in a cell on (vtables 0x00645D7C /
// 0x00645D80): 0x00418ED0 the node's destructor body (unlinked), 0x00418EF0 its scalar deleting destructor,
// 0x00418F30 the head's.
struct CollGridListNodes {
    void DestroyNodeBody();
    Mem::ListNode* DestroyNode(uint8_t flags);
    Mem::ListNode* DestroyHead(uint8_t flags);
};

} // namespace Nx
