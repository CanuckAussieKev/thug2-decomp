// The park editor's map of metapieces: the lists of metapieces, the bucket of each cell, what stands in an
// area, the floor slide, and the floor under placed pieces.

#include <cstdint>

#include "game/park_editing/park.h"

#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/park/park_memory.h"

namespace ParkEditing {

MemSys::PoolPair g_nodePools;

namespace {

MemSys::PoolPair& NodePools() { return g_nodePools; }

// The generator's piece making.
void* GetPieceSource(void* generator, uint32_t name) { return static_cast<CParkAssembler*>(generator)->GetPieceSource(name); }
void* CreatePiece(void* generator, void* source, uint32_t flags)
{
    return static_cast<CParkAssembler*>(generator)->CreatePiece(static_cast<CParkTile*>(source), flags);
}

// One floor slide step for a cell or metapiece at `current`.
int ScrollOffset(uint32_t flags, int current, int height)
{
    if (flags & 1) {
        if (flags & 2)
            return height - current;
        return (current != height) - 1;
    }
    return ((flags & 2) ? 2 : 0) - 1;
}

} // namespace

// --- CMapListNode and CPieceGroup (game code outside this range, or not rewritten yet) ----------------------------

CMapListNode* CMapListNode::Fabricate()
{
    auto* node = static_cast<CMapListNode*>(NodePools().Carve(sizeof(CMapListNode)));
    if (node) {
        node->m_dy = 0;
        node->m_pieceGroup = nullptr;
        node->m_following = nullptr;
    }
    return node;
}

void CMapListNode::Dealloc(void* node) { NodePools().Dealloc(node); }

void __fastcall CMapListNode::FreeNodeChain(CMapListNode* list)
{
    CMapListNode* node = list;
    while (node) {
        CMapListNode* next = node->m_following;
        Dealloc(node);
        node = next;
    }
}

void CMapListNode::AuditSoundness(const CMapListNode* list)
{
    if (!list)
        return;
    for (const CMapListNode* node = list->m_following; node; node = node->m_following) {
        for (const CMapListNode* later = node; later; later = later->m_following) {
        }
    }
}

// --- CParkLayout ------------------------------------------------------------------------------------------------

CPieceGroup* CParkLayout::MakePlacedPiece(CPieceGroup* abstractMeta, uint32_t pieceFlags)
{
    MemSys::EnterHeapScope(ParkHeap());
    void* memory = MemSys::Alloc(sizeof(CPieceGroup));
    CPieceGroup* meta = memory ? static_cast<CPlacedPieceGroup*>(memory)->MakeNew() : nullptr;
    MemSys::LeaveHeapScope();

    int count = abstractMeta->CountPieces();
    meta->Commission(count); // (a failed allocation crashes here, as in the original)
    meta->m_nameKey = abstractMeta->m_nameKey;
    if (abstractMeta->m_flagBits & CPieceGroup::kBuiltShell) {
        pieceFlags |= 0x20;
        meta->m_flagBits |= CPieceGroup::kBuiltShell;
    }
    if (abstractMeta->m_flagBits & CPieceGroup::kNoRails) {
        pieceFlags |= 0x100;
        meta->m_flagBits |= CPieceGroup::kNoRails;
    }
    if (abstractMeta->m_flagBits & CPieceGroup::kFloorLocked)
        meta->m_flagBits |= CPieceGroup::kFloorLocked;
    for (int i = 0; i < count; ++i) {
        SPieceGroupDesc entry;
        static_cast<CPieceBlueprint*>(abstractMeta)->GetEntry(&entry, i); // entry `i`, copied
        void* source = GetPieceSource(sSingleton()->m_builder, reinterpret_cast<uint32_t>(entry.piece));
        void* piece = CreatePiece(m_builder, source, pieceFlags);
        static_cast<CParkTile*>(piece)->SetQuarterTurns(entry.rot, 0); // turn it
        SCellBox pos;
        pos.AssignValue(entry.x, entry.y, entry.z, 0, 0, 0);
        meta->InsertTile(piece, &pos, entry.rot, entry.setsFloor);
    }
    meta->FinishPieces();

    CMapListNode* node = CMapListNode::Fabricate();
    node->m_pieceGroup = meta; // (out of memory crashes, as in the original)
    node->m_following = nullptr;
    if (!m_concrete_metas) {
        m_concrete_metas = node;
    } else {
        CMapListNode* last = m_concrete_metas;
        while (last->m_following)
            last = last->m_following;
        last->m_following = node;
    }
    ++m_numConcreteMetas;
    return meta;
}

bool CParkLayout::HasPiecesOutside(SCellBox area)
{
    for (CMapListNode* node = m_concrete_metas; node; node = node->m_following) {
        CPieceGroup* meta = node->GetPieceGroup()->Cast();
        SCellBox dims = meta->m_cellFootprint;
        bool inside = dims.x >= area.x && dims.z >= area.z && dims.x + dims.w <= area.x + area.w &&
                      dims.z + dims.l <= area.z + area.l;
        if (inside)
            continue;
        uint32_t flags = meta->m_flagBits;
        if (!(flags & CPieceGroup::kBuiltShell) && !(flags & CPieceGroup::kFence) && (flags & CPieceGroup::kInPark))
            return true;
    }
    return false;
}

void CParkLayout::AddMetaToList(CPieceGroup* meta, CMapListNode** list)
{
    for (CMapListNode* node = *list; node; node = node->m_following) {
        if (node->GetPieceGroup() == meta)
            return;
    }
    CMapListNode* node = CMapListNode::Fabricate();
    node->m_pieceGroup = meta; // (out of memory crashes, as in the original)
    node->m_following = *list;
    *list = node;
}

void CParkLayout::RemoveMetaFromList(CPieceGroup* meta, CMapListNode** list)
{
    CMapListNode* previous = nullptr;
    for (CMapListNode* node = *list; node; previous = node, node = node->m_following) {
        if (node->GetPieceGroup() != meta)
            continue;
        if (previous)
            previous->m_following = node->m_following;
        else
            *list = node->m_following;
        CMapListNode::Dealloc(node);
        return;
    }
}

void CParkLayout::AddMetaToCells(CPieceGroup* meta)
{
    for (int i = 0; i < meta->m_cellFootprint.w; ++i) {
        for (int j = 0; j < meta->m_cellFootprint.l; ++j) {
            CMapListNode* node = CMapListNode::Fabricate();
            node->m_pieceGroup = meta; // (out of memory crashes, as in the original)
            CMapListNode*& bucket = m_cellMetas[meta->m_cellFootprint.x + i][meta->m_cellFootprint.z + j];
            node->m_following = bucket;
            bucket = node;
        }
    }
}

void CParkLayout::RemoveMetaFromCells(CPieceGroup* meta)
{
    for (int i = 0; i < meta->m_cellFootprint.w; ++i) {
        for (int j = 0; j < meta->m_cellFootprint.l; ++j) {
            CMapListNode*& bucket = m_cellMetas[meta->m_cellFootprint.x + i][meta->m_cellFootprint.z + j];
            CMapListNode* previous = nullptr;
            for (CMapListNode* node = bucket; node; previous = node, node = node->m_following) {
                if (node->m_pieceGroup != meta)
                    continue;
                if (previous)
                    previous->m_following = node->m_following;
                else
                    bucket = node->m_following;
                CMapListNode::Dealloc(node);
                break;
            }
        }
    }
}

CMapListNode* CParkLayout::GetIntersectingMetas(SCellBox area, int rot, CPieceGroup* meta, uint32_t skipInnerShell)
{
    bool precise = static_cast<uint8_t>(skipInnerShell) != 0;
    CMapListNode* list = nullptr;
    if (!meta) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            for (int z = area.z; z < area.z + area.l; ++z) {
                for (CMapListNode* node = m_cellMetas[x][z]; node; node = node->m_following) {
                    if (precise && (node->GetPieceGroup()->m_flagBits & CPieceGroup::kBuiltShell))
                        continue;
                    if (node->GetPieceGroup()->Cast()->Intersects(area, skipInnerShell))
                        AddMetaToList(node->GetPieceGroup(), &list);
                }
            }
        }
        CMapListNode::AuditSoundness(list);
        return list;
    }

    meta->SetQuarterTurns(rot);
    area.w = meta->m_cellFootprint.w;
    area.h = meta->m_cellFootprint.h;
    area.l = meta->m_cellFootprint.l;
    MapListHolder nearby(GetIntersectingMetas(area, 0, nullptr, false));
    for (uint32_t i = 0; i < meta->m_filledSlots; ++i) {
        SPieceGroupDesc* entry = meta->GetEntry(static_cast<int>(i));
        SCellBox pos;
        pos.AssignValue(entry->x, entry->y, entry->z, 0, 0, 0);
        pos.x = static_cast<uint8_t>(pos.x + area.x);
        pos.y = static_cast<int8_t>(pos.y + area.y);
        pos.z = static_cast<uint8_t>(pos.z + area.z);
        for (CMapListNode* node = nearby.list; node; node = node->m_following) {
            if (node->GetPieceGroup()->Cast()->Intersects(pos, false))
                AddMetaToList(node->GetPieceGroup(), &list);
        }
    }
    meta->SetQuarterTurns(0);
    CMapListNode::AuditSoundness(list);
    return list;
}

CMapListNode* CParkLayout::CollectPiecesInRegion(SCellBox area)
{
    CMapListNode* list = nullptr;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = m_cellMetas[x][z]; node; node = node->m_following)
                AddMetaToList(node->GetPieceGroup(), &list);
        }
    }
    return list;
}

SGapDetails* CParkLayout::GetGapInfo(SCellBox* cell, int* half)
{
    cell->w = 1;
    cell->h = 1;
    cell->l = 1;
    MapListHolder metas(GetIntersectingMetas(*cell, 0, nullptr, true));
    if (!metas.list)
        return nullptr;
    CPieceGroup* meta = metas.list->GetPieceGroup()->Cast();
    int gap;
    // The gap a metapiece is a half of, and its info
    if (!m_gapRegistry->GetGapIndex(meta, &gap))
        return nullptr;
    return m_gapRegistry->GetGapInfo(gap, half);
}

bool CParkLayout::HasFloorLockedMeta(SCellBox area)
{
    area.RaiseCeilingToLimit();
    MapListHolder metas(GetIntersectingMetas(area, 0, nullptr, true));
    for (CMapListNode* node = metas.list; node; node = node->m_following) {
        if (node->GetPieceGroup()->Cast()->m_flagBits & CPieceGroup::kFloorLocked)
            return true;
    }
    return false;
}

bool CParkLayout::BuildRiserList(SCellBox area, int y, int height, uint32_t flags, CMapListNode** slidList)
{
    area.y = static_cast<int8_t>(y);
    if (flags & 4)
        area.RaiseCeilingToLimit();
    else
        area.StretchVertically();
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            FloorCell& cell = m_floor[x][z];
            if (!cell.m_touched) {
                int amount = ScrollOffset(flags, cell.m_levelY, height);
                cell.m_levelY += amount;
                cell.m_delta = amount;
                if (cell.m_levelY < -16 || cell.m_levelY > 15)
                    return false;
            }
            cell.m_touched = true;
        }
    }
    MapListHolder metas(GetIntersectingMetas(area, 0, nullptr, true));
    for (CMapListNode* node = metas.list; node; node = node->m_following) {
        CPieceGroup* meta = node->GetPieceGroup()->Cast();
        SCellBox dims = meta->m_cellFootprint;
        if ((meta->m_flagBits & CPieceGroup::kBuiltShell) || (meta->m_flagBits & CPieceGroup::kFlag20))
            continue;
        meta->m_flagBits |= CPieceGroup::kFlag20;
        CMapListNode* slid = CMapListNode::Fabricate();
        slid->m_following = *slidList; // (out of memory crashes, as in the original)
        slid->m_pieceGroup = meta;
        *slidList = slid;
        slid->m_dy = ScrollOffset(flags, dims.y, height);
        if (!BuildRiserList(dims, dims.y, height, flags & ~4u, slidList))
            return false;
    }
    return true;
}

void CParkLayout::SetMetaInCells(CPieceGroup* meta, int add)
{
    for (uint32_t i = 0; i < meta->m_filledSlots; ++i) {
        void* piece = meta->GetPiece(static_cast<int>(i)); // (and the cast to a concrete piece: 0x005902E0)
        SPieceGroupDesc* entry = meta->GetEntry(static_cast<int>(i));
        if (!entry->setsFloor)
            continue;
        SCellBox dims;
        dims.AssignValue(0, 0, 0, 0, 0, 0);
        static_cast<CParkTile*>(piece)->GetFootprintSize(&dims); // the piece's size in cells
        dims.x = static_cast<uint8_t>(entry->x + meta->m_cellFootprint.x);
        dims.y = static_cast<int8_t>(entry->y + meta->m_cellFootprint.y);
        dims.z = static_cast<uint8_t>(entry->z + meta->m_cellFootprint.z);
        int zEnd = dims.z + dims.l;
        for (int x = dims.x; x < dims.x + dims.w; ++x) {
            for (int z = dims.z; z < zEnd; ++z) {
                FloorCell& cell = m_floor[x][z];
                if (add) {
                    int top = dims.y + dims.h;
                    if (cell.m_levelY < top)
                        cell.m_levelY = top;
                    if (cell.m_levelY > 15)
                        cell.m_levelY = 15;
                    if (cell.m_levelY < -16)
                        cell.m_levelY = -16;
                } else {
                    // nothing left above the piece: the floor comes down to its bottom
                    SCellBox above = dims;
                    above.y = static_cast<int8_t>(dims.h + dims.y);
                    above.StretchVertically();
                    MapListHolder metas(GetIntersectingMetas(above, 0, nullptr, true));
                    if (!metas.list && cell.m_levelY > dims.y)
                        cell.m_levelY = dims.y;
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

using ParkEditing::CParkLayout;
