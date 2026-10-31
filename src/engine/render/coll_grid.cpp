// A scene's collision grid. See coll_grid.h.

#include "engine/render/coll_grid.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"

namespace render {

namespace {

// The queries' shared answer (2048 entries, null-terminated) and the mark of the current query (objects keep the
// last one that listed them, so each is listed once).
HitMesh* g_found[2048];
HitMesh** const kFound = g_found;

uint8_t g_stamp;

uint8_t& s_stamp = g_stamp;
constexpr int kMostFound = 0x7FF;

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

int confine(int value, int size)
{
    if (value < 0)
        return 0;
    return value >= size ? size - 1 : value;
}

// An object's box as the grid sees it.
const maths::BBox* cuboid(HitMesh* object) { return object->get_world_bounds(); }

bool listable(HitMesh* object) { return !(object->flag_bits_ & 6); }

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
memory::ListNode* masked(memory::ListNode* node) { return is_head(node) ? nullptr : node; }

int len_of(memory::ListNode* head)
{
    int count = 0;
    for (memory::ListNode* node = head->next; !is_head(node) && node; node = node->next)
        ++count;
    return count;
}

void unlink_list_node(memory::ListNode* node) { memory::ListNode::destroy(node); } // (0x00418EF0)

// One cell's update: the objects taken out and put in, `delta` the change in its count so far. While objects
// have been taken out (delta below 0) new ones fill the gaps at the end of the list; while more have been put in
// (above 0) they wait on `pending`, and one taken out is replaced by the first waiting.
struct CellUpdate {
    CollGridCell* cell;
    memory::ListNode* pending;
    int delta = 0;

    void take_out(HitMesh* object)
    {
        if (delta > 0) {
            memory::ListNode* first = masked(pending->next);
            auto* waiting = static_cast<HitMesh*>(first->data);
            for (int i = 0; i < cell->count; ++i) {
                if (cell->objects[i] == object) {
                    cell->objects[i] = waiting;
                    break;
                }
            }
            unlink_list_node(first);
        } else {
            for (int i = 0; i < cell->count; ++i) {
                if (cell->objects[i] == object) {
                    for (int j = i + 1; j < cell->count; ++j)
                        cell->objects[j - 1] = cell->objects[j];
                    cell->objects[cell->count - 1] = nullptr;
                    break;
                }
            }
        }
        --delta;
    }

    void put_in(HitMesh* object)
    {
        if (delta < 0) {
            cell->objects[cell->count + delta] = object;
        } else {
            memory::ListNode* node = memory::ListNode::create(object);
            node->prev = pending->prev;
            node->next = pending;
            pending->prev->next = node;
            pending->prev = node;
        }
        ++delta;
    }

    bool get_lists(HitMesh* object) const
    {
        for (int i = 0; i < cell->count; ++i) {
            if (cell->objects[i] == object)
                return true;
        }
        return false;
    }

    // The list's block resized for the new count; the waiting objects added at the end.
    void get_finish()
    {
        int count = cell->count + delta;
        uint32_t size = static_cast<uint32_t>(count) * 4;
        if (delta < 0) {
            if (!cell->objects || !memory::Hub::get_pull()->trim_block_end(size, cell->objects, nullptr))
                cell->objects = static_cast<HitMesh**>(memory::regrow(cell->objects, size));
        } else if (delta > 0) {
            if (!cell->objects || !memory::Hub::get_pull()->extend_block_upward(size, cell->objects, nullptr))
                cell->objects = static_cast<HitMesh**>(memory::regrow(cell->objects, size));
            int at = cell->count;
            if (!is_head(pending->next)) {
                for (memory::ListNode* node = pending->next; node;) {
                    memory::ListNode* next = masked(node->next);
                    cell->objects[at++] = static_cast<HitMesh*>(node->data);
                    unlink_list_node(node);
                    node = next;
                }
            }
        }
        cell->count = count;
    }
};

} // namespace

// 0x004183B0
CollGridCell* CollGridCell::make_new()
{
    new (&box) maths::BBox();
    unknown20 = 0;
    count = 0;
    unknown28 = nullptr;
    objects = nullptr;
    return this;
}

// 0x00418410
void CollGridCell::destruct()
{
    if (unknown28)
        memory::Hub::get_pull()->dealloc(unknown28);
    if (objects)
        memory::free_malloc(objects);
}

// 0x00418440 (the corners are kept in double precision from one cell to the next and rounded where stored)
void CollGrid::prepare_all(const maths::BBox* bounds)
{
    box_ = *bounds;
    cell_span_x_ = static_cast<float>((static_cast<double>(bounds->max.x) - bounds->min.x) * 0.05f);
    cell_depth_ = static_cast<float>((static_cast<double>(bounds->max.z) - bounds->min.z) * 0.05f);
    double x = bounds->min.x;
    for (int i = 0; i < kSize; ++i) {
        double z = bounds->min.z;
        for (int j = 0; j < kSize; ++j) {
            maths::BBox& box = cells_[i][j].box;
            box.min = {static_cast<float>(x), -1e12f, static_cast<float>(z), 0.0f};
            box.max = {static_cast<float>(x + cell_span_x_), 1e12f, static_cast<float>(z + cell_depth_), 0.0f};
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
            if (cell.objects) {
                memory::free_malloc(cell.objects);
                cell.objects = nullptr;
            }
            cell.count = 0;
        }
    }
}

// 0x00418BD0 (a cell index is limited to 19, not below 0)
void CollGrid::get_fill(StaticCollider* statics, int count)
{
    auto cells_of = [this](StaticCollider* object, int& x0, int& x1, int& z0, int& z1) {
        const maths::BBox* box = cuboid(object);
        float inverse_width = static_cast<float>(1.0 / cell_span_x_);
        x0 = truncate((static_cast<double>(box->min.x) - box_.min.x) * inverse_width);
        x1 = truncate((static_cast<double>(box->max.x) - box_.min.x) * inverse_width);
        double inverse_depth = 1.0 / cell_depth_;
        z0 = truncate((static_cast<double>(box->min.z) - box_.min.z) * inverse_depth);
        z1 = truncate((static_cast<double>(box->max.z) - box_.min.z) * inverse_depth);
        if (x1 >= kSize)
            x1 = kSize - 1;
        if (z1 >= kSize)
            z1 = kSize - 1;
    };
    // how many each cell gets
    for (int i = 0; i < count; ++i) {
        if (statics[i].geometry_->tri_count_ == 0)
            continue;
        int x0, x1, z0, z1;
        cells_of(&statics[i], x0, x1, z0, z1);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z)
                ++cells_[x][z].count;
        }
    }
    for (auto& column : cells_) {
        for (CollGridCell& cell : column) {
            if (cell.count > 0)
                cell.objects = static_cast<HitMesh**>(memory::alloc_block(static_cast<uint32_t>(cell.count) * 4));
        }
    }
    // how many each cell has so far (a scratch block on the top-down heap)
    memory::enter_heap_scope(memory::top_down_heap());
    auto* filled = static_cast<int*>(memory::alloc(kSize * kSize * sizeof(int)));
    std::memset(filled, 0, kSize * kSize * sizeof(int));
    memory::leave_heap_scope();
    for (int i = 0; i < count; ++i) {
        if (statics[i].geometry_->tri_count_ == 0)
            continue;
        int x0, x1, z0, z1;
        cells_of(&statics[i], x0, x1, z0, z1);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z)
                cells_[x][z].objects[filled[x * kSize + z]++] = &statics[i];
        }
    }
    memory::Hub::get_pull()->dealloc(filled);
}

// 0x00418F60
void CollGrid::tick_frame(memory::ListNode* added, memory::ListNode* removed, memory::ListNode* moved)
{
    memory::ListNode pending;
    pending.make_head();
    int added_count = len_of(added), removed_count = len_of(removed), moved_count = len_of(moved);
    for (int x = 0; x < kSize; ++x) {
        for (int z = 0; z < kSize; ++z) {
            CellUpdate update{&cells_[x][z], &pending};
            CollGridCell* cell = update.cell;
            memory::enter_heap_scope(memory::top_down_heap());
            memory::ListNode* node = masked(removed->next);
            for (int i = 0; i < removed_count; ++i, node = masked(node->next)) {
                auto* object = static_cast<HitMesh*>(node->data);
                if (cell->box.intersects(*cuboid(object)))
                    update.take_out(object);
            }
            node = masked(added->next);
            for (int i = 0; i < added_count; ++i, node = masked(node->next)) {
                auto* object = static_cast<HitMesh*>(node->data);
                if (cell->box.intersects(*cuboid(object)))
                    update.put_in(object);
            }
            node = masked(moved->next);
            for (int i = 0; i < moved_count; ++i, node = masked(node->next)) {
                auto* object = static_cast<HitMesh*>(node->data);
                bool listed = update.get_lists(object);
                if (cell->box.intersects(*cuboid(object)) != listed) {
                    if (listed)
                        update.take_out(object);
                    else
                        update.put_in(object);
                }
            }
            memory::leave_heap_scope();
            update.get_finish();
        }
    }
    // every list moved to a fresh block
    for (auto& column : cells_) {
        for (CollGridCell& cell : column) {
            HitMesh** old = cell.objects;
            if (!old)
                continue;
            if (cell.count != 0) {
                uint32_t size = static_cast<uint32_t>(cell.count) * 4;
                cell.objects = static_cast<HitMesh**>(memory::alloc_block(size));
                std::memcpy(cell.objects, old, size);
            } else {
                cell.objects = nullptr;
            }
            memory::free_malloc(old);
        }
    }
    pending.prev->next = pending.next;
    pending.next->prev = pending.prev;
}

// 0x00418710
HitMesh** CollGrid::objects_in_box(const maths::BBox* box)
{
    uint8_t stamp = ++s_stamp;
    double inverse_width = 1.0 / cell_span_x_;
    int x0 = truncate((static_cast<double>(box->min.x) - box_.min.x - 0.5f) * inverse_width);
    float inverse_depth = static_cast<float>(1.0 / cell_depth_);
    int z0 = truncate((static_cast<double>(box->min.z) - box_.min.z - 0.5f) * inverse_depth);
    int x1 = truncate((static_cast<double>(box->max.x) - box_.min.x + 0.5f) * inverse_width);
    int z1 = truncate((static_cast<double>(box->max.z) - box_.min.z + 0.5f) * inverse_depth);
    x0 = confine(x0, columns_);
    z0 = confine(z0, rows_);
    x1 = confine(x1, columns_);
    z1 = confine(z1, rows_);
    int found = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            CollGridCell& cell = cells_[x][z];
            for (int i = 0; i < cell.count; ++i) {
                HitMesh* object = cell.objects[i];
                if (listable(object) && object->visit_stamp_ != stamp) {
                    kFound[found++] = object;
                    object->visit_stamp_ = stamp;
                }
            }
        }
    }
    kFound[found] = nullptr;
    return kFound;
}

// 0x004188C0 (the line's direction: its float parts, the x part unrounded; made a unit vector when it has a
// length, its x part again unrounded; the longer line's w left unset by the original)
HitMesh** CollGrid::objects_along_line(const maths::Seg* line, int* count)
{
    double dx = static_cast<double>(line->end.x) - line->start.x;
    float dy = line->end.y - line->start.y;
    float dz = line->end.z - line->start.z;
    float dxf = static_cast<float>(dx);
    double length =
        std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dxf) * dxf);
    if (length > 0.0) {
        float inverse = static_cast<float>(1.0 / static_cast<float>(length));
        dx = static_cast<double>(dxf) * inverse;
        dy = static_cast<float>(static_cast<double>(dy) * inverse);
        dz = static_cast<float>(static_cast<double>(dz) * inverse);
    }
    double ex = dx * 0.5f, ey = static_cast<double>(dy) * 0.5f;
    float ez = static_cast<float>(static_cast<double>(dz) * 0.5f);
    maths::Seg longer;
    longer.start = {static_cast<float>(line->start.x - ex), static_cast<float>(line->start.y - ey),
                    static_cast<float>(static_cast<double>(line->start.z) - ez), 0.0f};
    longer.end = {static_cast<float>(ex + line->end.x), static_cast<float>(ey + line->end.y),
                  static_cast<float>(static_cast<double>(ez) + line->end.z), 0.0f};

    double inverse_width = 1.0 / cell_span_x_;
    int x0 = truncate((static_cast<double>(longer.start.x) - box_.min.x) * inverse_width);
    float inverse_depth = static_cast<float>(1.0 / cell_depth_);
    int z0 = truncate((static_cast<double>(longer.start.z) - box_.min.z) * inverse_depth);
    x0 = confine(x0, columns_);
    z0 = confine(z0, rows_);
    int x1 = truncate((static_cast<double>(longer.end.x) - box_.min.x) * inverse_width);
    int z1 = truncate((static_cast<double>(longer.end.z) - box_.min.z) * inverse_depth);
    x1 = confine(x1, columns_);
    z1 = confine(z1, rows_);
    if (x1 < x0) {
        int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    if (z1 < z0) {
        int swap = z0;
        z0 = z1;
        z1 = swap;
    }
    int found = 0;
    if (x0 == x1 && z0 == z1) {
        CollGridCell& cell = cells_[x0][z0];
        for (int i = 0; i < cell.count; ++i) {
            if (listable(cell.objects[i]))
                kFound[found++] = cell.objects[i];
        }
    } else {
        uint8_t stamp = ++s_stamp;
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                CollGridCell& cell = cells_[x][z];
                for (int i = 0; i < cell.count; ++i) {
                    HitMesh* object = cell.objects[i];
                    if (listable(object) && object->visit_stamp_ != stamp && found < kMostFound) {
                        kFound[found++] = object;
                        object->visit_stamp_ = stamp;
                    }
                }
            }
        }
    }
    kFound[found] = nullptr;
    if (count)
        *count = found;
    return kFound;
}

} // namespace render
