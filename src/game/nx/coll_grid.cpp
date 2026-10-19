// A scene's collision grid. See coll_grid.h.

#include "game/nx/coll_grid.h"

#include <cmath>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

void** const kNodeVtable = reinterpret_cast<void**>(0x00645D7C);
void** const kHeadVtable = reinterpret_cast<void**>(0x00645D80);

// The queries' shared answer (2048 entries, null-terminated) and the mark of the current query (objects keep the
// last one that listed them, so each is listed once).
CCollObj** const s_found = reinterpret_cast<CCollObj**>(0x00691998);
uint8_t& s_stamp = GameVariable<uint8_t>(0x00693998);
constexpr int kMostFound = 0x7FF;

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

int Clamp(int value, int size)
{
    if (value < 0)
        return 0;
    return value >= size ? size - 1 : value;
}

// An object's box as the grid sees it (slot 21).
const Mth::BBox* Box(CCollObj* object) { return object->Call<const Mth::BBox*>(21); }

bool Listable(CCollObj* object) { return !(object->m_flags & 6); }

bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
Mem::ListNode* Masked(Mem::ListNode* node) { return IsHead(node) ? nullptr : node; }

int Length(Mem::ListNode* head)
{
    int count = 0;
    for (Mem::ListNode* node = head->next; !IsHead(node) && node; node = node->next)
        ++count;
    return count;
}

void DeleteNode(Mem::ListNode* node) { reinterpret_cast<void*(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1); }

// One cell's update: the objects taken out and put in, `delta` the change in its count so far. While objects
// have been taken out (delta below 0) new ones fill the gaps at the end of the list; while more have been put in
// (above 0) they wait on `pending`, and one taken out is replaced by the first waiting.
struct CellUpdate {
    CollGridCell* cell;
    Mem::ListNode* pending;
    int delta = 0;

    void TakeOut(CCollObj* object)
    {
        if (delta > 0) {
            Mem::ListNode* first = Masked(pending->next);
            auto* waiting = static_cast<CCollObj*>(first->data);
            for (int i = 0; i < cell->count; ++i) {
                if (cell->objects[i] == object) {
                    cell->objects[i] = waiting;
                    break;
                }
            }
            DeleteNode(first);
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

    void PutIn(CCollObj* object)
    {
        if (delta < 0) {
            cell->objects[cell->count + delta] = object;
        } else {
            auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
            if (node) {
                node->vtable = kNodeVtable;
                node->data = object;
                node->priority = 0;
                node->next = node;
                node->prev = node;
            }
            node->prev = pending->prev;
            node->next = pending;
            pending->prev->next = node;
            pending->prev = node;
        }
        ++delta;
    }

    bool Lists(CCollObj* object) const
    {
        for (int i = 0; i < cell->count; ++i) {
            if (cell->objects[i] == object)
                return true;
        }
        return false;
    }

    // The list's block resized for the new count; the waiting objects added at the end.
    void Finish()
    {
        int count = cell->count + delta;
        uint32_t size = static_cast<uint32_t>(count) * 4;
        if (delta < 0) {
            if (!cell->objects || !Mem::Manager::Get()->ReallocateShrink(size, cell->objects, nullptr))
                cell->objects = static_cast<CCollObj**>(Mem::Realloc(cell->objects, size));
        } else if (delta > 0) {
            if (!cell->objects || !Mem::Manager::Get()->ReallocateUp(size, cell->objects, nullptr))
                cell->objects = static_cast<CCollObj**>(Mem::Realloc(cell->objects, size));
            int at = cell->count;
            if (!IsHead(pending->next)) {
                for (Mem::ListNode* node = pending->next; node;) {
                    Mem::ListNode* next = Masked(node->next);
                    cell->objects[at++] = static_cast<CCollObj*>(node->data);
                    DeleteNode(node);
                    node = next;
                }
            }
        }
        cell->count = count;
    }
};

} // namespace

// 0x004183B0
CollGridCell* CollGridCell::Construct()
{
    new (&box) Mth::BBox();
    unknown20 = 0;
    count = 0;
    unknown28 = nullptr;
    objects = nullptr;
    return this;
}

// 0x00418410
void CollGridCell::Destruct()
{
    if (unknown28)
        Mem::Manager::Get()->Free(unknown28);
    if (objects)
        Mem::FreeMalloc(objects);
}

// 0x00418440 (the corners are kept in double precision from one cell to the next and rounded where stored)
void CCollGrid::Init(const Mth::BBox* bounds)
{
    m_box = *bounds;
    m_cellWidth = static_cast<float>((static_cast<double>(bounds->max.x) - bounds->min.x) * 0.05f);
    m_cellDepth = static_cast<float>((static_cast<double>(bounds->max.z) - bounds->min.z) * 0.05f);
    double x = bounds->min.x;
    for (int i = 0; i < kSize; ++i) {
        double z = bounds->min.z;
        for (int j = 0; j < kSize; ++j) {
            Mth::BBox& box = m_cells[i][j].box;
            box.min = {static_cast<float>(x), -1e12f, static_cast<float>(z), 0.0f};
            box.max = {static_cast<float>(x + m_cellWidth), 1e12f, static_cast<float>(z + m_cellDepth), 0.0f};
            z += m_cellDepth;
        }
        x += m_cellWidth;
    }
    m_columns = kSize;
    m_rows = kSize;
}

// 0x004183D0
void CCollGrid::Clear()
{
    for (auto& column : m_cells) {
        for (CollGridCell& cell : column) {
            if (cell.objects) {
                Mem::FreeMalloc(cell.objects);
                cell.objects = nullptr;
            }
            cell.count = 0;
        }
    }
}

// 0x00418BD0 (a cell index is limited to 19, not below 0)
void CCollGrid::Fill(CCollStatic* statics, int count)
{
    auto cellsOf = [this](CCollStatic* object, int& x0, int& x1, int& z0, int& z1) {
        const Mth::BBox* box = Box(object);
        float inverseWidth = static_cast<float>(1.0 / m_cellWidth);
        x0 = Truncate((static_cast<double>(box->min.x) - m_box.min.x) * inverseWidth);
        x1 = Truncate((static_cast<double>(box->max.x) - m_box.min.x) * inverseWidth);
        double inverseDepth = 1.0 / m_cellDepth;
        z0 = Truncate((static_cast<double>(box->min.z) - m_box.min.z) * inverseDepth);
        z1 = Truncate((static_cast<double>(box->max.z) - m_box.min.z) * inverseDepth);
        if (x1 >= kSize)
            x1 = kSize - 1;
        if (z1 >= kSize)
            z1 = kSize - 1;
    };
    // how many each cell gets
    for (int i = 0; i < count; ++i) {
        if (statics[i].m_geometry->m_numFaces == 0)
            continue;
        int x0, x1, z0, z1;
        cellsOf(&statics[i], x0, x1, z0, z1);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z)
                ++m_cells[x][z].count;
        }
    }
    for (auto& column : m_cells) {
        for (CollGridCell& cell : column) {
            if (cell.count > 0)
                cell.objects = static_cast<CCollObj**>(Mem::Malloc(static_cast<uint32_t>(cell.count) * 4));
        }
    }
    // how many each cell has so far (a scratch block on the top-down heap)
    Mem::PushContext(Mem::TopDownHeap());
    auto* filled = static_cast<int*>(Mem::New(kSize * kSize * sizeof(int)));
    std::memset(filled, 0, kSize * kSize * sizeof(int));
    Mem::PopContext();
    for (int i = 0; i < count; ++i) {
        if (statics[i].m_geometry->m_numFaces == 0)
            continue;
        int x0, x1, z0, z1;
        cellsOf(&statics[i], x0, x1, z0, z1);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z)
                m_cells[x][z].objects[filled[x * kSize + z]++] = &statics[i];
        }
    }
    Mem::Manager::Get()->Free(filled);
}

// 0x00418F60
void CCollGrid::Update(Mem::ListNode* added, Mem::ListNode* removed, Mem::ListNode* moved)
{
    Mem::ListNode pending;
    pending.vtable = kHeadVtable;
    pending.data = reinterpret_cast<void*>(-1);
    pending.priority = 0;
    pending.next = &pending;
    pending.prev = &pending;
    int addedCount = Length(added), removedCount = Length(removed), movedCount = Length(moved);
    for (int x = 0; x < kSize; ++x) {
        for (int z = 0; z < kSize; ++z) {
            CellUpdate update{&m_cells[x][z], &pending};
            CollGridCell* cell = update.cell;
            Mem::PushContext(Mem::TopDownHeap());
            Mem::ListNode* node = Masked(removed->next);
            for (int i = 0; i < removedCount; ++i, node = Masked(node->next)) {
                auto* object = static_cast<CCollObj*>(node->data);
                if (cell->box.Intersects(*Box(object)))
                    update.TakeOut(object);
            }
            node = Masked(added->next);
            for (int i = 0; i < addedCount; ++i, node = Masked(node->next)) {
                auto* object = static_cast<CCollObj*>(node->data);
                if (cell->box.Intersects(*Box(object)))
                    update.PutIn(object);
            }
            node = Masked(moved->next);
            for (int i = 0; i < movedCount; ++i, node = Masked(node->next)) {
                auto* object = static_cast<CCollObj*>(node->data);
                bool listed = update.Lists(object);
                if (cell->box.Intersects(*Box(object)) != listed) {
                    if (listed)
                        update.TakeOut(object);
                    else
                        update.PutIn(object);
                }
            }
            Mem::PopContext();
            update.Finish();
        }
    }
    // every list moved to a fresh block
    for (auto& column : m_cells) {
        for (CollGridCell& cell : column) {
            CCollObj** old = cell.objects;
            if (!old)
                continue;
            if (cell.count != 0) {
                uint32_t size = static_cast<uint32_t>(cell.count) * 4;
                cell.objects = static_cast<CCollObj**>(Mem::Malloc(size));
                std::memcpy(cell.objects, old, size);
            } else {
                cell.objects = nullptr;
            }
            Mem::FreeMalloc(old);
        }
    }
    pending.prev->next = pending.next;
    pending.next->prev = pending.prev;
}

// 0x00418710
CCollObj** CCollGrid::ObjectsInBox(const Mth::BBox* box)
{
    uint8_t stamp = ++s_stamp;
    double inverseWidth = 1.0 / m_cellWidth;
    int x0 = Truncate((static_cast<double>(box->min.x) - m_box.min.x - 0.5f) * inverseWidth);
    float inverseDepth = static_cast<float>(1.0 / m_cellDepth);
    int z0 = Truncate((static_cast<double>(box->min.z) - m_box.min.z - 0.5f) * inverseDepth);
    int x1 = Truncate((static_cast<double>(box->max.x) - m_box.min.x + 0.5f) * inverseWidth);
    int z1 = Truncate((static_cast<double>(box->max.z) - m_box.min.z + 0.5f) * inverseDepth);
    x0 = Clamp(x0, m_columns);
    z0 = Clamp(z0, m_rows);
    x1 = Clamp(x1, m_columns);
    z1 = Clamp(z1, m_rows);
    int found = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            CollGridCell& cell = m_cells[x][z];
            for (int i = 0; i < cell.count; ++i) {
                CCollObj* object = cell.objects[i];
                if (Listable(object) && object->m_visitStamp != stamp) {
                    s_found[found++] = object;
                    object->m_visitStamp = stamp;
                }
            }
        }
    }
    s_found[found] = nullptr;
    return s_found;
}

// 0x004188C0 (the line's direction: its float parts, the x part unrounded; made a unit vector when it has a
// length, its x part again unrounded; the longer line's w left unset by the original)
CCollObj** CCollGrid::ObjectsAlongLine(const Mth::Line* line, int* count)
{
    double dx = static_cast<double>(line->end.x) - line->start.x;
    float dy = line->end.y - line->start.y;
    float dz = line->end.z - line->start.z;
    float dxf = static_cast<float>(dx);
    double length = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dxf) * dxf);
    if (length > 0.0) {
        float inverse = static_cast<float>(1.0 / static_cast<float>(length));
        dx = static_cast<double>(dxf) * inverse;
        dy = static_cast<float>(static_cast<double>(dy) * inverse);
        dz = static_cast<float>(static_cast<double>(dz) * inverse);
    }
    double ex = dx * 0.5f, ey = static_cast<double>(dy) * 0.5f;
    float ez = static_cast<float>(static_cast<double>(dz) * 0.5f);
    Mth::Line longer;
    longer.start = {static_cast<float>(line->start.x - ex), static_cast<float>(line->start.y - ey),
                    static_cast<float>(static_cast<double>(line->start.z) - ez), 0.0f};
    longer.end = {static_cast<float>(ex + line->end.x), static_cast<float>(ey + line->end.y),
                  static_cast<float>(static_cast<double>(ez) + line->end.z), 0.0f};

    double inverseWidth = 1.0 / m_cellWidth;
    int x0 = Truncate((static_cast<double>(longer.start.x) - m_box.min.x) * inverseWidth);
    float inverseDepth = static_cast<float>(1.0 / m_cellDepth);
    int z0 = Truncate((static_cast<double>(longer.start.z) - m_box.min.z) * inverseDepth);
    x0 = Clamp(x0, m_columns);
    z0 = Clamp(z0, m_rows);
    int x1 = Truncate((static_cast<double>(longer.end.x) - m_box.min.x) * inverseWidth);
    int z1 = Truncate((static_cast<double>(longer.end.z) - m_box.min.z) * inverseDepth);
    x1 = Clamp(x1, m_columns);
    z1 = Clamp(z1, m_rows);
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
        CollGridCell& cell = m_cells[x0][z0];
        for (int i = 0; i < cell.count; ++i) {
            if (Listable(cell.objects[i]))
                s_found[found++] = cell.objects[i];
        }
    } else {
        uint8_t stamp = ++s_stamp;
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                CollGridCell& cell = m_cells[x][z];
                for (int i = 0; i < cell.count; ++i) {
                    CCollObj* object = cell.objects[i];
                    if (Listable(object) && object->m_visitStamp != stamp && found < kMostFound) {
                        s_found[found++] = object;
                        object->m_visitStamp = stamp;
                    }
                }
            }
        }
    }
    s_found[found] = nullptr;
    if (count)
        *count = found;
    return s_found;
}

// 0x00418ED0
void CollGridListNodes::DestroyNodeBody() { reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(kNodeVtable); }

// 0x00418EF0
Mem::ListNode* CollGridListNodes::DestroyNode(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return reinterpret_cast<Mem::ListNode*>(this);
}

// 0x00418F30
Mem::ListNode* CollGridListNodes::DestroyHead(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return reinterpret_cast<Mem::ListNode*>(this);
}

} // namespace Nx

REPLACE(0x004183B0, &Nx::CollGridCell::Construct);
REPLACE(0x00418410, &Nx::CollGridCell::Destruct);
REPLACE(0x00418440, &Nx::CCollGrid::Init);
REPLACE(0x004183D0, &Nx::CCollGrid::Clear);
REPLACE(0x00418BD0, &Nx::CCollGrid::Fill);
REPLACE(0x00418F60, &Nx::CCollGrid::Update);
REPLACE(0x00418710, &Nx::CCollGrid::ObjectsInBox);
REPLACE(0x004188C0, &Nx::CCollGrid::ObjectsAlongLine);
REPLACE(0x00418ED0, &Nx::CollGridListNodes::DestroyNodeBody);
REPLACE(0x00418EF0, &Nx::CollGridListNodes::DestroyNode);
REPLACE(0x00418F30, &Nx::CollGridListNodes::DestroyHead);
