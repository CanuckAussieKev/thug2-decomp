#pragma once

// A scene's collision grid (our name): its box cut into 20 x 20 columns on x and z (each reaching from -1e12 to
// 1e12 on y), each cell listing the static collision objects whose boxes touch it. The line and box queries look
// objects up through it.

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "base/core/api.h"
#include "base/core/layout_32bit.h"
#include "engine/memory/arena_alloc.h"
#include "base/maths/shape_tests.h"
#include "base/core/checked_index.h"

namespace render {

class HitMesh;
class StaticCollider;

constexpr std::size_t kCollGridCellBytes = 0x30;
constexpr std::size_t kCollGridCellObjectsOffset = 0x2C;

// A cell (0x30 bytes).
struct CollGridCell {
    // 0x004183B0: an empty box, nothing listed.
    CollGridCell();
    // 0x00418410: the two blocks freed.
    ~CollGridCell();
    CollGridCell(const CollGridCell&) = delete;
    CollGridCell& operator=(const CollGridCell&) = delete;

    // The fields other code reads and sets.
    maths::BBox& box() { return box_; }
    [[nodiscard]] const maths::BBox& box() const { return box_; }
    int& count() { return count_; }
    [[nodiscard]] int count() const { return count_; }
    [[nodiscard]] HitMesh** objects() const { return objects_; }
    void set_objects(HitMesh** value) { objects_ = value; }

private:
    maths::BBox box_;    // +0x00
    uint32_t unknown20{0};      // +0x20
    int count_{0};               // +0x24
    void* unknown28{nullptr};   // +0x28 (freed through the memory manager)
    HitMesh** objects_{nullptr}; // +0x2C a Malloc block of `count`

public:

    // The listed objects as a run of `count`.
    [[nodiscard]] std::span<HitMesh*> object_run() const { return {objects_, static_cast<std::size_t>(count_)}; }
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(sizeof(CollGridCell) == kCollGridCellBytes && offsetof(CollGridCell, objects_) == kCollGridCellObjectsOffset),
                      "layout must match the original");
    }
};

constexpr std::size_t kCollGridCellsOffset = 0x30;
constexpr std::size_t kCollGridBytes = 0x4B30;

class CollGrid {
public:
    static constexpr int kSize = 20;

    // 0x00418440: the box taken and cut into cells (their corners added up from the minimum in double precision).
    void prepare_all(const maths::BBox* bounds);
    // 0x004183D0: every cell's list freed and emptied.
    void get_clear();
    // 0x00418BD0: the cells filled from `count` static objects (those whose geometry has faces), each listed in
    // the cells its box (slot 21) covers. A cell index below 0 isn't clamped (as in the original).
    void get_fill(std::span<StaticCollider> statics);
    // 0x00418F60: the lists brought up to date, cell by cell: the objects of `removed` taken out where their boxes
    // touch the cell, those of `added` put in where they touch it, those of `moved` taken out or put in as they
    // now do or don't; each list's block resized once and, at the end, every list moved to a fresh block. (Lists
    // of lists::Foremost<HitMesh>.)
    THUG2_API void tick_frame(memory::ListNode* added, memory::ListNode* removed, memory::ListNode* moved);

    // 0x00418710: the objects (neither flag 2 nor 4) of the cells `box` (grown by 0.5 on x and z) covers, each
    // once; null-terminated, in the shared list 0x00691998.
    std::span<HitMesh* const> objects_in_box(const maths::BBox* box);
    // 0x004188C0: the same for the cells the line (made half a unit longer at each end) spans; one cell: its
    // objects as they are; more: each once, 2047 at most. The number found goes to `count` if it's given.
    std::span<HitMesh* const> objects_along_line(const maths::Seg* line, int* count);

    CollGridCell* get_cell(int x, int z) { return &core::at(core::at(cells_, x), z); }

    // The fields other code reads and sets.
    maths::BBox& box() { return box_; }
    [[nodiscard]] const maths::BBox& box() const { return box_; }

private:
    maths::BBox box_;                  // +0x00
    float cell_span_x_{};              // +0x20 on x
    float cell_depth_{};               // +0x24 on z
    int columns_{};                    // +0x28 on x (20)
    int rows_{};                       // +0x2C on z (20)
    std::array<std::array<CollGridCell, kSize>, kSize> cells_; // +0x30 [x][z]
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(CollGrid, cells_) == kCollGridCellsOffset && sizeof(CollGrid) == kCollGridBytes), "layout must match the original");
    }
};

// (The list Update keeps the objects waiting to be put in a cell on: its node's and head's destructors 0x00418ED0 /
// 0x00418EF0 / 0x00418F30 are memory::ListNode's uncouple / destroy.)

} // namespace render
