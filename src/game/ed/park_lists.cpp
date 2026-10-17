// The park editor's map of metapieces: the lists of metapieces, the bucket of each cell, what stands in an
// area, the floor slide, and the floor under placed pieces.

#include "game/ed/park.h"

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

Mem::PoolPair& NodePools() { return GameVariable<Mem::PoolPair>(0x007CE5F0); }

// The park editor's heap (0x0059E100).
Mem::Heap* ParkHeap() { return GameVariable<Mem::Heap*>(0x007CE688); }

// The generator's piece making (CParkGenerator, not rewritten yet).
void* GetPieceSource(void* generator, uint32_t name)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x00591D90)(generator, name);
}
void* CreatePiece(void* generator, void* source, uint32_t flags)
{
    return GameFunction<void*(__thiscall*)(void*, void*, uint32_t)>(0x005923C0)(generator, source, flags);
}

// One floor slide step for a cell or metapiece at `current`.
int SlideAmount(uint32_t flags, int current, int height)
{
    if (flags & 1) {
        if (flags & 2)
            return height - current;
        return (current != height) - 1;
    }
    return ((flags & 2) ? 2 : 0) - 1;
}

} // namespace

// --- CMapListNode and CMetaPiece (game code outside this range, or not rewritten yet) ----------------------------

CMapListNode* CMapListNode::Create()
{
    auto* node = static_cast<CMapListNode*>(NodePools().Allocate(sizeof(CMapListNode)));
    if (node) {
        node->m_dy = 0;
        node->mp_meta = nullptr;
        node->mp_next = nullptr;
    }
    return node;
}

void CMapListNode::Free(void* node) { NodePools().Free(node); }

void __fastcall CMapListNode::DestroyList(CMapListNode* list)
{
    CMapListNode* node = list;
    while (node) {
        CMapListNode* next = node->mp_next;
        Free(node);
        node = next;
    }
}

void CMapListNode::VerifyIntegrity(const CMapListNode* list)
{
    if (!list)
        return;
    for (const CMapListNode* node = list->mp_next; node; node = node->mp_next) {
        for (const CMapListNode* later = node; later; later = later->mp_next) {
        }
    }
}


// --- CParkManager ------------------------------------------------------------------------------------------------

CMetaPiece* CParkManager::CreateConcreteMeta(CMetaPiece* abstractMeta, uint32_t pieceFlags)
{
    Mem::PushContext(ParkHeap());
    void* memory = Mem::New(sizeof(CMetaPiece));
    // 0x00590B60: the concrete metapiece's constructor
    auto* meta = memory ? GameFunction<CMetaPiece*(__thiscall*)(void*)>(0x00590B60)(memory) : nullptr;
    Mem::PopContext();

    int count = abstractMeta->CountPieces();
    meta->Initialize(count); // (a failed allocation crashes here, as in the original)
    meta->m_name_checksum = abstractMeta->m_name_checksum;
    if (abstractMeta->m_flags & CMetaPiece::kInnerShell) {
        pieceFlags |= 0x20;
        meta->m_flags |= CMetaPiece::kInnerShell;
    }
    if (abstractMeta->m_flags & CMetaPiece::kNoRails) {
        pieceFlags |= 0x100;
        meta->m_flags |= CMetaPiece::kNoRails;
    }
    if (abstractMeta->m_flags & CMetaPiece::kFloorLocked)
        meta->m_flags |= CMetaPiece::kFloorLocked;
    for (int i = 0; i < count; ++i) {
        SMetaDescriptor entry;
        // 0x005911D0: the abstract metapiece's entry `i`, copied
        GameFunction<void(__thiscall*)(CMetaPiece*, SMetaDescriptor*, int)>(0x005911D0)(abstractMeta, &entry, i);
        void* source = GetPieceSource(sInstance()->mp_generator, reinterpret_cast<uint32_t>(entry.piece));
        void* piece = CreatePiece(mp_generator, source, pieceFlags);
        GameFunction<void(__thiscall*)(void*, int, int)>(0x00590260)(piece, entry.rot, 0); // turn it
        GridDims pos;
        pos.Set(entry.x, entry.y, entry.z, 0, 0, 0);
        meta->AddPiece(piece, &pos, entry.rot, entry.isReal);
    }
    meta->FinishPieces();

    CMapListNode* node = CMapListNode::Create();
    node->mp_meta = meta; // (out of memory crashes, as in the original)
    node->mp_next = nullptr;
    if (!mp_concrete_metas) {
        mp_concrete_metas = node;
    } else {
        CMapListNode* last = mp_concrete_metas;
        while (last->mp_next)
            last = last->mp_next;
        last->mp_next = node;
    }
    ++m_numConcreteMetas;
    return meta;
}

bool CParkManager::HasPiecesOutside(GridDims area)
{
    for (CMapListNode* node = mp_concrete_metas; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        GridDims dims = meta->m_cell_area;
        bool inside = dims.x >= area.x && dims.z >= area.z && dims.x + dims.w <= area.x + area.w &&
                      dims.z + dims.l <= area.z + area.l;
        if (inside)
            continue;
        uint32_t flags = meta->m_flags;
        if (!(flags & CMetaPiece::kInnerShell) && !(flags & CMetaPiece::kFence) && (flags & CMetaPiece::kInPark))
            return true;
    }
    return false;
}

void CParkManager::AddMetaToList(CMetaPiece* meta, CMapListNode** list)
{
    for (CMapListNode* node = *list; node; node = node->mp_next) {
        if (node->GetMeta() == meta)
            return;
    }
    CMapListNode* node = CMapListNode::Create();
    node->mp_meta = meta; // (out of memory crashes, as in the original)
    node->mp_next = *list;
    *list = node;
}

void CParkManager::RemoveMetaFromList(CMetaPiece* meta, CMapListNode** list)
{
    CMapListNode* previous = nullptr;
    for (CMapListNode* node = *list; node; previous = node, node = node->mp_next) {
        if (node->GetMeta() != meta)
            continue;
        if (previous)
            previous->mp_next = node->mp_next;
        else
            *list = node->mp_next;
        CMapListNode::Free(node);
        return;
    }
}

void CParkManager::AddMetaToCells(CMetaPiece* meta)
{
    for (int i = 0; i < meta->m_cell_area.w; ++i) {
        for (int j = 0; j < meta->m_cell_area.l; ++j) {
            CMapListNode* node = CMapListNode::Create();
            node->mp_meta = meta; // (out of memory crashes, as in the original)
            CMapListNode*& bucket = m_cellMetas[meta->m_cell_area.x + i][meta->m_cell_area.z + j];
            node->mp_next = bucket;
            bucket = node;
        }
    }
}

void CParkManager::RemoveMetaFromCells(CMetaPiece* meta)
{
    for (int i = 0; i < meta->m_cell_area.w; ++i) {
        for (int j = 0; j < meta->m_cell_area.l; ++j) {
            CMapListNode*& bucket = m_cellMetas[meta->m_cell_area.x + i][meta->m_cell_area.z + j];
            CMapListNode* previous = nullptr;
            for (CMapListNode* node = bucket; node; previous = node, node = node->mp_next) {
                if (node->mp_meta != meta)
                    continue;
                if (previous)
                    previous->mp_next = node->mp_next;
                else
                    bucket = node->mp_next;
                CMapListNode::Free(node);
                break;
            }
        }
    }
}

CMapListNode* CParkManager::GetIntersectingMetas(GridDims area, int rot, CMetaPiece* meta, uint32_t skipInnerShell)
{
    bool precise = static_cast<uint8_t>(skipInnerShell) != 0;
    CMapListNode* list = nullptr;
    if (!meta) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            for (int z = area.z; z < area.z + area.l; ++z) {
                for (CMapListNode* node = m_cellMetas[x][z]; node; node = node->mp_next) {
                    if (precise && (node->GetMeta()->m_flags & CMetaPiece::kInnerShell))
                        continue;
                    if (node->GetMeta()->Cast()->Intersects(area, skipInnerShell))
                        AddMetaToList(node->GetMeta(), &list);
                }
            }
        }
        CMapListNode::VerifyIntegrity(list);
        return list;
    }

    meta->SetRot(rot);
    area.w = meta->m_cell_area.w;
    area.h = meta->m_cell_area.h;
    area.l = meta->m_cell_area.l;
    MapListHolder nearby(GetIntersectingMetas(area, 0, nullptr, false));
    for (uint32_t i = 0; i < meta->m_num_used_entries; ++i) {
        SMetaDescriptor* entry = meta->GetEntry(static_cast<int>(i));
        GridDims pos;
        pos.Set(entry->x, entry->y, entry->z, 0, 0, 0);
        pos.x = static_cast<uint8_t>(pos.x + area.x);
        pos.y = static_cast<int8_t>(pos.y + area.y);
        pos.z = static_cast<uint8_t>(pos.z + area.z);
        for (CMapListNode* node = nearby.list; node; node = node->mp_next) {
            if (node->GetMeta()->Cast()->Intersects(pos, false))
                AddMetaToList(node->GetMeta(), &list);
        }
    }
    meta->SetRot(0);
    CMapListNode::VerifyIntegrity(list);
    return list;
}

CMapListNode* CParkManager::GetAreaMetaPieces(GridDims area)
{
    CMapListNode* list = nullptr;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = m_cellMetas[x][z]; node; node = node->mp_next)
                AddMetaToList(node->GetMeta(), &list);
        }
    }
    return list;
}

GapInfo* CParkManager::GetGapInfo(GridDims* cell, int* half)
{
    cell->w = 1;
    cell->h = 1;
    cell->l = 1;
    MapListHolder metas(GetIntersectingMetas(*cell, 0, nullptr, true));
    if (!metas.list)
        return nullptr;
    CMetaPiece* meta = metas.list->GetMeta()->Cast();
    int gap;
    // CGapManager: the gap a metapiece is a half of (0x0058CA10), and its info (0x0058CB20)
    if (!GameFunction<bool(__thiscall*)(void*, CMetaPiece*, int*)>(0x0058CA10)(mp_gapManager, meta, &gap))
        return nullptr;
    return GameFunction<GapInfo*(__thiscall*)(void*, int, int*)>(0x0058CB20)(mp_gapManager, gap, half);
}

bool CParkManager::HasFloorLockedMeta(GridDims area)
{
    area.MakeInfinitelyHigh();
    MapListHolder metas(GetIntersectingMetas(area, 0, nullptr, true));
    for (CMapListNode* node = metas.list; node; node = node->mp_next) {
        if (node->GetMeta()->Cast()->m_flags & CMetaPiece::kFloorLocked)
            return true;
    }
    return false;
}

bool CParkManager::BuildRiserList(GridDims area, int y, int height, uint32_t flags, CMapListNode** slidList)
{
    area.y = static_cast<int8_t>(y);
    if (flags & 4)
        area.MakeInfinitelyHigh();
    else
        area.MakeInfiniteOnY();
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            FloorCell& cell = m_floor[x][z];
            if (!cell.m_touched) {
                int amount = SlideAmount(flags, cell.m_height, height);
                cell.m_height += amount;
                cell.m_delta = amount;
                if (cell.m_height < -16 || cell.m_height > 15)
                    return false;
            }
            cell.m_touched = true;
        }
    }
    MapListHolder metas(GetIntersectingMetas(area, 0, nullptr, true));
    for (CMapListNode* node = metas.list; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        GridDims dims = meta->m_cell_area;
        if ((meta->m_flags & CMetaPiece::kInnerShell) || (meta->m_flags & CMetaPiece::kFlag20))
            continue;
        meta->m_flags |= CMetaPiece::kFlag20;
        CMapListNode* slid = CMapListNode::Create();
        slid->mp_next = *slidList; // (out of memory crashes, as in the original)
        slid->mp_meta = meta;
        *slidList = slid;
        slid->m_dy = SlideAmount(flags, dims.y, height);
        if (!BuildRiserList(dims, dims.y, height, flags & ~4u, slidList))
            return false;
    }
    return true;
}

void CParkManager::SetMetaInCells(CMetaPiece* meta, int add)
{
    for (uint32_t i = 0; i < meta->m_num_used_entries; ++i) {
        void* piece = meta->GetPiece(static_cast<int>(i)); // (and the cast to a concrete piece: 0x005902E0)
        SMetaDescriptor* entry = meta->GetEntry(static_cast<int>(i));
        if (!entry->isReal)
            continue;
        GridDims dims;
        dims.Set(0, 0, 0, 0, 0, 0);
        GameFunction<void(__thiscall*)(void*, GridDims*)>(0x005904C0)(piece, &dims); // the piece's size in cells
        dims.x = static_cast<uint8_t>(entry->x + meta->m_cell_area.x);
        dims.y = static_cast<int8_t>(entry->y + meta->m_cell_area.y);
        dims.z = static_cast<uint8_t>(entry->z + meta->m_cell_area.z);
        int zEnd = dims.z + dims.l;
        for (int x = dims.x; x < dims.x + dims.w; ++x) {
            for (int z = dims.z; z < zEnd; ++z) {
                FloorCell& cell = m_floor[x][z];
                if (add) {
                    int top = dims.y + dims.h;
                    if (cell.m_height < top)
                        cell.m_height = top;
                    if (cell.m_height > 15)
                        cell.m_height = 15;
                    if (cell.m_height < -16)
                        cell.m_height = -16;
                } else {
                    // nothing left above the piece: the floor comes down to its bottom
                    GridDims above = dims;
                    above.y = static_cast<int8_t>(dims.h + dims.y);
                    above.MakeInfiniteOnY();
                    MapListHolder metas(GetIntersectingMetas(above, 0, nullptr, true));
                    if (!metas.list && cell.m_height > dims.y)
                        cell.m_height = dims.y;
                }
                for (int level = dims.y; level < dims.h + dims.y; ++level) {
                    uint32_t bit = 1u << ((level + 0x11) & 31);
                    if (add)
                        cell.m_heightsUsed |= bit;
                    else
                        cell.m_heightsUsed &= ~bit;
                }
            }
        }
    }
}

} // namespace Ed

using Ed::CParkManager;

REPLACE(0x00595670, &CParkManager::CreateConcreteMeta);
REPLACE(0x005958B0, &CParkManager::HasPiecesOutside);
REPLACE(0x00595DF0, &CParkManager::AddMetaToList);
REPLACE(0x00595E80, &CParkManager::RemoveMetaFromList);
REPLACE(0x00595EE0, &CParkManager::AddMetaToCells);
REPLACE(0x00595FC0, &CParkManager::RemoveMetaFromCells);
REPLACE(0x00597740, &CParkManager::GetIntersectingMetas);
REPLACE(0x00597A00, &CParkManager::GetAreaMetaPieces);
REPLACE(0x00597AC0, &CParkManager::GetGapInfo);
REPLACE(0x005983D0, &CParkManager::HasFloorLockedMeta);
REPLACE(0x005984A0, &CParkManager::BuildRiserList);
REPLACE(0x00598150, &CParkManager::SetMetaInCells);
