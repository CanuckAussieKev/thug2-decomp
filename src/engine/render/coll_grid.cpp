// A scene's collision grid. See coll_grid.h.

#include "engine/render/coll_grid.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <algorithm>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace render {

constexpr std::size_t kFoundCapacity = 2048; // the objects one grid query can list
constexpr float kCellFraction = 0.05F;       // the bounds are cut into 20 cells each way
constexpr float kUnboundedHeight = 1e12F;    // a cell reaches this far up and down
constexpr float kHalf = 0.5F;

namespace {

// NOT ORIGINAL grouping - the grid queries' shared answer and the current query's mark. Made on first use, never
// destroyed (grid_query()).
struct GridQuery {
    // The queries' shared answer (2048 entries, null-terminated) and the mark of the current query (objects keep the
    // last one that listed them, so each is listed once).
    std::array<HitMesh*, kFoundCapacity> found_{};
    uint8_t stamp_{};
};

GridQuery& grid_query()
{
    static core::NoDestroy<GridQuery> state;
    return state.get();
}

constexpr int kMostFound = 0x7FF;

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

int confine(int value, int size)
{
    if (value < 0) {
        return 0;
    }
    return value >= size ? size - 1 : value;
}

// An object's box as the grid sees it.
const maths::BBox* cuboid(HitMesh* object) { return object->get_world_bounds(); }

bool listable(HitMesh* object) { return !(object->flag_bits() & kUnlistedFlags); }

bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }
memory::ListNode* masked(memory::ListNode* node) { return is_head(node) ? nullptr : node; }

int len_of(memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next(); !is_head(node) && node; node = node->next()) {
        ++count;
    }
    return count;
}

void unlink_list_node(memory::ListNode* node) { memory::ListNode::destroy(node); } // (0x00418EF0)

// One cell's update: the objects taken out and put in, `delta` the change in its count so far. While objects
// have been taken out (delta below 0) new ones fill the gaps at the end of the list; while more have been put in
// (above 0) they wait on `pending`, and one taken out is replaced by the first waiting.
class CellUpdate {
public:
    CellUpdate(CollGridCell* cell, memory::ListNode* pending) : cell_(cell), pending_(pending) {}

    void take_out(HitMesh* object)
    {
        if (delta_ > 0) {
            memory::ListNode* first = masked(pending_->next());
            auto* waiting = static_cast<HitMesh*>(first->data());
            for (int i = 0; i < cell_->count(); ++i) {
                if (core::at(cell_->object_run(), i) == object) {
                    core::at(cell_->object_run(), i) = waiting;
                    break;
                }
            }
            unlink_list_node(first);
        } else {
            for (int i = 0; i < cell_->count(); ++i) {
                if (core::at(cell_->object_run(), i) == object) {
                    for (int j = i + 1; j < cell_->count(); ++j) {
                        core::at(cell_->object_run(), j - 1) = core::at(cell_->object_run(), j);
                    }
                    core::at(cell_->object_run(), cell_->count() - 1) = nullptr;
                    break;
                }
            }
        }
        --delta_;
    }

    void put_in(HitMesh* object)
    {
        if (delta_ < 0) {
            core::at(cell_->object_run(), cell_->count() + delta_) = object;
        } else {
            memory::ListNode* node = memory::ListNode::create(object);
            node->set_prev(pending_->prev());
            node->set_next(pending_);
            pending_->prev()->set_next(node);
            pending_->set_prev(node);
        }
        ++delta_;
    }

    bool get_lists(HitMesh* object) const
    {
        for (int i = 0; i < cell_->count(); ++i) {
            if (core::at(cell_->object_run(), i) == object) {
                return true;
            }
        }
        return false;
    }

    // The list's block resized for the new count; the waiting objects added at the end.
    void get_finish()
    {
        const int count = cell_->count() + delta_;
        const uint32_t size = static_cast<uint32_t>(count) * 4;
        if (delta_ < 0) {
            if (!cell_->objects() ||
                !memory::Hub::get_pull()->trim_block_end(size, static_cast<void*>(cell_->objects()), nullptr)) {
                cell_->set_objects(static_cast<HitMesh**>(memory::regrow(static_cast<void*>(cell_->objects()), size)));
            }
        } else if (delta_ > 0) {
            if (!cell_->objects() ||
                !memory::Hub::get_pull()->extend_block_upward(size, static_cast<void*>(cell_->objects()), nullptr)) {
                cell_->set_objects(static_cast<HitMesh**>(memory::regrow(static_cast<void*>(cell_->objects()), size)));
            }
            int at = cell_->count();
            const std::span<HitMesh*> block(cell_->objects(), static_cast<size_t>(count));
            if (!is_head(pending_->next())) {
                for (memory::ListNode* node = pending_->next(); node;) {
                    memory::ListNode* next = masked(node->next());
                    core::at(block, at++) = static_cast<HitMesh*>(node->data());
                    unlink_list_node(node);
                    node = next;
                }
            }
        }
        cell_->count() = count;
    }

private:
    CollGridCell* cell_;
    memory::ListNode* pending_;
    int delta_ = 0;
};

} // namespace

// 0x004183B0
CollGridCell::CollGridCell() = default;

// 0x00418410
CollGridCell::~CollGridCell()
{
    if (unknown28) {
        memory::Hub::dealloc(unknown28);
    }
    if (objects_) {
        memory::free_malloc(static_cast<void*>(objects_));
    }
}

// 0x00418440 (the corners are kept in double precision from one cell to the next and rounded where stored)
void CollGrid::prepare_all(const maths::BBox* bounds)
{
    box_ = *bounds;
    cell_span_x_ = static_cast<float>((static_cast<double>(bounds->max.x) - bounds->min.x) * kCellFraction);
    cell_depth_ = static_cast<float>((static_cast<double>(bounds->max.z) - bounds->min.z) * kCellFraction);
    double x = bounds->min.x;
    for (auto& column : cells_) {
        double z = bounds->min.z;
        for (auto& cell : column) {
            maths::BBox& box = cell.box();
            box.min = {.x = static_cast<float>(x), .y = -kUnboundedHeight, .z = static_cast<float>(z), .w = 0.0F};
            box.max = {.x = static_cast<float>(x + cell_span_x_),
                       .y = kUnboundedHeight,
                       .z = static_cast<float>(z + cell_depth_),
                       .w = 0.0F};
            z += cell_depth_;
        }
        x += cell_span_x_;
    }
    columns_ = kSize;
    rows_ = kSize;
}

// 0x004183D0
void CollGrid::get_clear()
{
    for (auto& column : cells_) {
        for (CollGridCell& cell : column) {
            if (cell.objects()) {
                memory::free_malloc(static_cast<void*>(cell.objects()));
                cell.set_objects(nullptr);
            }
            cell.count() = 0;
        }
    }
}

// 0x00418BD0 (a cell index is limited to 19, not below 0)
void CollGrid::get_fill(std::span<StaticCollider> statics)
{
    // (the cells an object covers: its first and last in x and in z)
    struct Cells {
        int x0{};
        int x1{};
        int z0{};
        int z1{};
    };
    auto cells_of = [this](StaticCollider* object) {
        Cells cells;
        int& x0 = cells.x0;
        int& x1 = cells.x1;
        int& z0 = cells.z0;
        int& z1 = cells.z1;
        const maths::BBox* box = cuboid(object);
        auto inverse_width = static_cast<float>(1.0 / cell_span_x_);
        x0 = truncate((static_cast<double>(box->min.x) - box_.min.x) * inverse_width);
        x1 = truncate((static_cast<double>(box->max.x) - box_.min.x) * inverse_width);
        const double inverse_depth = 1.0 / cell_depth_;
        z0 = truncate((static_cast<double>(box->min.z) - box_.min.z) * inverse_depth);
        z1 = truncate((static_cast<double>(box->max.z) - box_.min.z) * inverse_depth);
        if (x1 >= kSize) {
            x1 = kSize - 1;
        }
        if (z1 >= kSize) {
            z1 = kSize - 1;
        }
        return cells;
    };
    // how many each cell gets
    for (StaticCollider& object : statics) {
        if (object.geometry()->tri_count() == 0) {
            continue;
        }
        const auto [x0, x1, z0, z1] = cells_of(&object);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                ++core::at(core::at(cells_, x), z).count();
            }
        }
    }
    for (auto& column : cells_) {
        for (CollGridCell& cell : column) {
            if (cell.count() > 0) {
                cell.set_objects(static_cast<HitMesh**>(memory::alloc_block(static_cast<uint32_t>(cell.count()) * 4)));
            }
        }
    }
    // how many each cell has so far (a scratch block on the top-down heap)
    memory::enter_heap_scope(memory::top_down_heap());
    auto* filled_block = static_cast<int*>(memory::alloc(kSize * kSize * sizeof(int)));
    std::memset(filled_block, 0, kSize * kSize * sizeof(int));
    memory::leave_heap_scope();
    const std::span<int> filled(filled_block, static_cast<size_t>(kSize) * kSize);
    for (StaticCollider& object : statics) {
        if (object.geometry()->tri_count() == 0) {
            continue;
        }
        const auto [x0, x1, z0, z1] = cells_of(&object);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                core::at(core::at(core::at(cells_, x), z).object_run(), core::at(filled, (x * kSize) + z)++) = &object;
            }
        }
    }
    memory::Hub::dealloc(filled_block);
}

// 0x00418F60
void CollGrid::tick_frame(memory::ListNode* added, memory::ListNode* removed, memory::ListNode* moved)
{
    memory::ListNode pending{};
    pending.make_head();
    const int added_count = len_of(added);
    const int removed_count = len_of(removed);
    const int moved_count = len_of(moved);
    for (auto& column : cells_) {
        for (auto& grid_cell : column) {
            CellUpdate update(&grid_cell, &pending);
            const CollGridCell* cell = &grid_cell;
            memory::enter_heap_scope(memory::top_down_heap());
            const memory::ListNode* node = masked(removed->next());
            for (int i = 0; i < removed_count; ++i, node = masked(node->next())) {
                auto* object = static_cast<HitMesh*>(node->data());
                if (cell->box().intersects(*cuboid(object))) {
                    update.take_out(object);
                }
            }
            node = masked(added->next());
            for (int i = 0; i < added_count; ++i, node = masked(node->next())) {
                auto* object = static_cast<HitMesh*>(node->data());
                if (cell->box().intersects(*cuboid(object))) {
                    update.put_in(object);
                }
            }
            node = masked(moved->next());
            for (int i = 0; i < moved_count; ++i, node = masked(node->next())) {
                auto* object = static_cast<HitMesh*>(node->data());
                const bool listed = update.get_lists(object);
                if (cell->box().intersects(*cuboid(object)) != listed) {
                    if (listed) {
                        update.take_out(object);
                    } else {
                        update.put_in(object);
                    }
                }
            }
            memory::leave_heap_scope();
            update.get_finish();
        }
    }
    // every list moved to a fresh block
    for (auto& column : cells_) {
        for (CollGridCell& cell : column) {
            HitMesh** old = cell.objects();
            if (!old) {
                continue;
            }
            if (cell.count() != 0) {
                const uint32_t size = static_cast<uint32_t>(cell.count()) * 4;
                cell.set_objects(static_cast<HitMesh**>(memory::alloc_block(size)));
                std::ranges::copy(std::span<HitMesh* const>(old, static_cast<size_t>(cell.count())), cell.objects());
            } else {
                cell.set_objects(nullptr);
            }
            memory::free_malloc(static_cast<void*>(old));
        }
    }
    pending.prev()->set_next(pending.next());
    pending.next()->set_prev(pending.prev());
}

// 0x00418710
std::span<HitMesh* const> CollGrid::objects_in_box(const maths::BBox* box)
{
    const uint8_t stamp = ++grid_query().stamp_;
    const double inverse_width = 1.0 / cell_span_x_;
    int x0 = truncate((static_cast<double>(box->min.x) - box_.min.x - kQueryBoxMargin) * inverse_width);
    auto inverse_depth = static_cast<float>(1.0 / cell_depth_);
    int z0 = truncate((static_cast<double>(box->min.z) - box_.min.z - kQueryBoxMargin) * inverse_depth);
    int x1 = truncate((static_cast<double>(box->max.x) - box_.min.x + kQueryBoxMargin) * inverse_width);
    int z1 = truncate((static_cast<double>(box->max.z) - box_.min.z + kQueryBoxMargin) * inverse_depth);
    x0 = confine(x0, columns_);
    z0 = confine(z0, rows_);
    x1 = confine(x1, columns_);
    z1 = confine(z1, rows_);
    int found = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            const CollGridCell& cell = core::at(core::at(cells_, x), z);
            for (int i = 0; i < cell.count(); ++i) {
                HitMesh* object = core::at(cell.object_run(), i);
                if (listable(object) && object->visit_stamp() != stamp) {
                    core::at(grid_query().found_, found++) = object;
                    object->set_visit_stamp(stamp);
                }
            }
        }
    }
    core::at(grid_query().found_, found) = nullptr;
    return std::span<HitMesh* const>(grid_query().found_).first(static_cast<size_t>(found));
}

// 0x004188C0 (the line's direction: its float parts, the x part unrounded; made a unit vector when it has a
// length, its x part again unrounded; the longer line's w left unset by the original)
std::span<HitMesh* const> CollGrid::objects_along_line(const maths::Seg* line, int* count)
{
    double dx = static_cast<double>(line->end.x) - line->start.x;
    float dy = line->end.y - line->start.y;
    float dz = line->end.z - line->start.z;
    auto dxf = static_cast<float>(dx);
    const double length =
        std::sqrt((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy) + (static_cast<double>(dxf) * dxf));
    if (length > 0.0) {
        auto inverse = static_cast<float>(1.0 / static_cast<float>(length));
        dx = static_cast<double>(dxf) * inverse;
        dy = static_cast<float>(static_cast<double>(dy) * inverse);
        dz = static_cast<float>(static_cast<double>(dz) * inverse);
    }
    const double ex = dx * 0.5F;
    const double ey = static_cast<double>(dy) * 0.5F;
    auto ez = static_cast<float>(static_cast<double>(dz) * kHalf);
    maths::Seg longer{};
    longer.start = {.x = static_cast<float>(line->start.x - ex),
                    .y = static_cast<float>(line->start.y - ey),
                    .z = static_cast<float>(static_cast<double>(line->start.z) - ez),
                    .w = 0.0F};
    longer.end = {.x = static_cast<float>(ex + line->end.x),
                  .y = static_cast<float>(ey + line->end.y),
                  .z = static_cast<float>(static_cast<double>(ez) + line->end.z),
                  .w = 0.0F};

    const double inverse_width = 1.0 / cell_span_x_;
    int x0 = truncate((static_cast<double>(longer.start.x) - box_.min.x) * inverse_width);
    auto inverse_depth = static_cast<float>(1.0 / cell_depth_);
    int z0 = truncate((static_cast<double>(longer.start.z) - box_.min.z) * inverse_depth);
    x0 = confine(x0, columns_);
    z0 = confine(z0, rows_);
    int x1 = truncate((static_cast<double>(longer.end.x) - box_.min.x) * inverse_width);
    int z1 = truncate((static_cast<double>(longer.end.z) - box_.min.z) * inverse_depth);
    x1 = confine(x1, columns_);
    z1 = confine(z1, rows_);
    if (x1 < x0) {
        const int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    if (z1 < z0) {
        const int swap = z0;
        z0 = z1;
        z1 = swap;
    }
    int found = 0;
    if (x0 == x1 && z0 == z1) {
        const CollGridCell& cell = core::at(core::at(cells_, x0), z0);
        for (int i = 0; i < cell.count(); ++i) {
            if (listable(core::at(cell.object_run(), i))) {
                core::at(grid_query().found_, found++) = core::at(cell.object_run(), i);
            }
        }
    } else {
        const uint8_t stamp = ++grid_query().stamp_;
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                const CollGridCell& cell = core::at(core::at(cells_, x), z);
                for (int i = 0; i < cell.count(); ++i) {
                    HitMesh* object = core::at(cell.object_run(), i);
                    if (listable(object) && object->visit_stamp() != stamp && found < kMostFound) {
                        core::at(grid_query().found_, found++) = object;
                        object->set_visit_stamp(stamp);
                    }
                }
            }
        }
    }
    core::at(grid_query().found_, found) = nullptr;
    if (count) {
        *count = found;
    }
    return std::span<HitMesh* const>(grid_query().found_).first(static_cast<size_t>(found));
}

} // namespace render
