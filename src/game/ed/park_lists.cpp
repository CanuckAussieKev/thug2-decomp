// The park editor's map of metapieces: the lists of metapieces, the bucket of each cell, what stands in an
// area, the floor slide, and the floor under placed pieces.

#include "game/ed/park_manager.h"

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
        node->m_slideAmount = 0;
        node->mp_meta = nullptr;
        node->mp_next = nullptr;
    }
    return node;
}

void CMapListNode::Free(void* node) { NodePools().Free(node); }

void CMapListNode::DestroyList()
{
    CMapListNode* node = this;
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

MetaPieceEntry* CMetaPiece::GetEntry(int index)
{
    return GameFunction<MetaPieceEntry*(__thiscall*)(CMetaPiece*, int)>(0x00590340)(this, index);
}
int CMetaPiece::CountPieces() { return GameFunction<int(__thiscall*)(CMetaPiece*)>(0x00590400)(this); }
void CMetaPiece::Initialize(int count) { GameFunction<void(__thiscall*)(CMetaPiece*, int)>(0x00590AD0)(this, count); }
void CMetaPiece::FinishPieces() { GameFunction<void(__thiscall*)(CMetaPiece*)>(0x00591AB0)(this); }
void CMetaPiece::SetRot(int rot) { GameFunction<void(__thiscall*)(CMetaPiece*, int)>(0x005907B0)(this, rot); }
bool CMetaPiece::Intersects(GridDims area, bool precise)
{
    return GameFunction<bool(__thiscall*)(CMetaPiece*, GridDims, bool)>(0x00590CE0)(this, area, precise);
}
void* CMetaPiece::GetPiece(int index)
{
    using Fn = void*(__thiscall*)(CMetaPiece*, int);
    return static_cast<Fn*>(m_vtable)[0](this, index);
}
void CMetaPiece::AddPiece(void* piece, GridDims* pos, int rot, bool isReal)
{
    using Fn = void(__thiscall*)(CMetaPiece*, void*, GridDims*, int, bool);
    static_cast<Fn*>(m_vtable)[2](this, piece, pos, rot, isReal);
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
    meta->m_nameChecksum = abstractMeta->m_nameChecksum;
    if (abstractMeta->m_flags & CMetaPiece::kFlagRiser) {
        pieceFlags |= 0x20;
        meta->m_flags |= CMetaPiece::kFlagRiser;
    }
    if (abstractMeta->m_flags & CMetaPiece::kFlagNoRails) {
        pieceFlags |= 0x100;
        meta->m_flags |= CMetaPiece::kFlagNoRails;
    }
    if (abstractMeta->m_flags & CMetaPiece::kFlagUnslidable)
        meta->m_flags |= CMetaPiece::kFlagUnslidable;
    for (int i = 0; i < count; ++i) {
        MetaPieceEntry entry;
        // 0x005911D0: the abstract metapiece's entry `i`, copied
        GameFunction<void(__thiscall*)(CMetaPiece*, MetaPieceEntry*, int)>(0x005911D0)(abstractMeta, &entry, i);
        void* source = GetPieceSource(sInstance()->mp_generator, entry.piece);
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
    if (!mp_concreteMetaList) {
        mp_concreteMetaList = node;
    } else {
        CMapListNode* last = mp_concreteMetaList;
        while (last->mp_next)
            last = last->mp_next;
        last->mp_next = node;
    }
    ++m_numConcreteMetas;
    return meta;
}

bool CParkManager::HasPiecesOutside(GridDims area)
{
    for (CMapListNode* node = mp_concreteMetaList; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        GridDims dims = meta->m_cellArea;
        bool inside = dims.x >= area.x && dims.z >= area.z && dims.x + dims.w <= area.x + area.w &&
                      dims.z + dims.l <= area.z + area.l;
        if (inside)
            continue;
        uint32_t flags = meta->m_flags;
        if (!(flags & CMetaPiece::kFlagRiser) && !(flags & CMetaPiece::kFlag100) && (flags & CMetaPiece::kFlagInPark))
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

void CParkManager::AddMetaToBuckets(CMetaPiece* meta)
{
    for (int i = 0; i < meta->m_cellArea.w; ++i) {
        for (int j = 0; j < meta->m_cellArea.l; ++j) {
            CMapListNode* node = CMapListNode::Create();
            node->mp_meta = meta; // (out of memory crashes, as in the original)
            CMapListNode*& bucket = mp_bucketList[meta->m_cellArea.x + i][meta->m_cellArea.z + j];
            node->mp_next = bucket;
            bucket = node;
        }
    }
}

void CParkManager::RemoveMetaFromBuckets(CMetaPiece* meta)
{
    for (int i = 0; i < meta->m_cellArea.w; ++i) {
        for (int j = 0; j < meta->m_cellArea.l; ++j) {
            CMapListNode*& bucket = mp_bucketList[meta->m_cellArea.x + i][meta->m_cellArea.z + j];
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

CMapListNode* CParkManager::GetAreaMetaPieces(GridDims area, int rot, CMetaPiece* meta, bool precise)
{
    CMapListNode* list = nullptr;
    if (!meta) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            for (int z = area.z; z < area.z + area.l; ++z) {
                for (CMapListNode* node = mp_bucketList[x][z]; node; node = node->mp_next) {
                    if (precise && (node->GetMeta()->m_flags & CMetaPiece::kFlagRiser))
                        continue;
                    if (node->GetMeta()->Cast()->Intersects(area, precise))
                        AddMetaToList(node->GetMeta(), &list);
                }
            }
        }
        CMapListNode::VerifyIntegrity(list);
        return list;
    }

    meta->SetRot(rot);
    area.w = meta->m_cellArea.w;
    area.h = meta->m_cellArea.h;
    area.l = meta->m_cellArea.l;
    CMapListTemp nearby(GetAreaMetaPieces(area, 0, nullptr, false));
    for (uint32_t i = 0; i < meta->m_numEntries; ++i) {
        MetaPieceEntry* entry = meta->GetEntry(static_cast<int>(i));
        GridDims pos;
        pos.Set(entry->x, entry->y, entry->z, 0, 0, 0);
        pos.x = static_cast<uint8_t>(pos.x + area.x);
        pos.y = static_cast<int8_t>(pos.y + area.y);
        pos.z = static_cast<uint8_t>(pos.z + area.z);
        for (CMapListNode* node = nearby.GetList(); node; node = node->mp_next) {
            if (node->GetMeta()->Cast()->Intersects(pos, false))
                AddMetaToList(node->GetMeta(), &list);
        }
    }
    meta->SetRot(0);
    CMapListNode::VerifyIntegrity(list);
    return list;
}

CMapListNode* CParkManager::GetMetaPiecesAt(GridDims area)
{
    CMapListNode* list = nullptr;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = mp_bucketList[x][z]; node; node = node->mp_next)
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
    CMapListTemp metas(GetAreaMetaPieces(*cell, 0, nullptr, true));
    if (!metas.GetList())
        return nullptr;
    CMetaPiece* meta = metas.GetList()->GetMeta()->Cast();
    int gap;
    // CGapManager: the gap a metapiece is a half of (0x0058CA10), and its info (0x0058CB20)
    if (!GameFunction<bool(__thiscall*)(void*, CMetaPiece*, int*)>(0x0058CA10)(mp_gapManager, meta, &gap))
        return nullptr;
    return GameFunction<GapInfo*(__thiscall*)(void*, int, int*)>(0x0058CB20)(mp_gapManager, gap, half);
}

bool CParkManager::IsColumnUnslidable(GridDims area)
{
    area.MakeInfinitelyHigh();
    CMapListTemp metas(GetAreaMetaPieces(area, 0, nullptr, true));
    for (CMapListNode* node = metas.GetList(); node; node = node->mp_next) {
        if (node->GetMeta()->Cast()->m_flags & CMetaPiece::kFlagUnslidable)
            return true;
    }
    return false;
}

bool CParkManager::SlideColumn(GridDims area, int y, int height, uint32_t flags, CMapListNode** slidList)
{
    area.y = static_cast<int8_t>(y);
    if (flags & 4)
        area.MakeInfinitelyHigh();
    else
        area.MakeInfiniteOnY();
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            FloorHeightEntry& cell = m_floor[x][z];
            if (!cell.m_markAsSlid) {
                int amount = SlideAmount(flags, cell.m_height, height);
                cell.m_height += amount;
                cell.m_slideAmount = amount;
                if (cell.m_height < -16 || cell.m_height > 15)
                    return false;
            }
            cell.m_markAsSlid = true;
        }
    }
    CMapListTemp metas(GetAreaMetaPieces(area, 0, nullptr, true));
    for (CMapListNode* node = metas.GetList(); node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        GridDims dims = meta->m_cellArea;
        if ((meta->m_flags & CMetaPiece::kFlagRiser) || (meta->m_flags & CMetaPiece::kFlagSlid))
            continue;
        meta->m_flags |= CMetaPiece::kFlagSlid;
        CMapListNode* slid = CMapListNode::Create();
        slid->mp_next = *slidList; // (out of memory crashes, as in the original)
        slid->mp_meta = meta;
        *slidList = slid;
        slid->m_slideAmount = SlideAmount(flags, dims.y, height);
        if (!SlideColumn(dims, dims.y, height, flags & ~4u, slidList))
            return false;
    }
    return true;
}

void CParkManager::ApplyMetaToFloor(CMetaPiece* meta, bool add)
{
    for (uint32_t i = 0; i < meta->m_numEntries; ++i) {
        void* piece = meta->GetPiece(static_cast<int>(i)); // (and the cast to a concrete piece: 0x005902E0)
        MetaPieceEntry* entry = meta->GetEntry(static_cast<int>(i));
        if (!entry->isReal)
            continue;
        GridDims dims;
        dims.Set(0, 0, 0, 0, 0, 0);
        GameFunction<void(__thiscall*)(void*, GridDims*)>(0x005904C0)(piece, &dims); // the piece's size in cells
        dims.x = static_cast<uint8_t>(entry->x + meta->m_cellArea.x);
        dims.y = static_cast<int8_t>(entry->y + meta->m_cellArea.y);
        dims.z = static_cast<uint8_t>(entry->z + meta->m_cellArea.z);
        int zEnd = dims.z + dims.l;
        for (int x = dims.x; x < dims.x + dims.w; ++x) {
            for (int z = dims.z; z < zEnd; ++z) {
                FloorHeightEntry& cell = m_floor[x][z];
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
                    CMapListTemp metas(GetAreaMetaPieces(above, 0, nullptr, true));
                    if (!metas.GetList() && cell.m_height > dims.y)
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
REPLACE(0x00595EE0, &CParkManager::AddMetaToBuckets);
REPLACE(0x00595FC0, &CParkManager::RemoveMetaFromBuckets);
REPLACE(0x00597740, &CParkManager::GetAreaMetaPieces);
REPLACE(0x00597A00, &CParkManager::GetMetaPiecesAt);
REPLACE(0x00597AC0, &CParkManager::GetGapInfo);
REPLACE(0x005983D0, &CParkManager::IsColumnUnslidable);
REPLACE(0x005984A0, &CParkManager::SlideColumn);
REPLACE(0x00598150, &CParkManager::ApplyMetaToFloor);
