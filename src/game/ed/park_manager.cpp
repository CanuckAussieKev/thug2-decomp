// The park manager (Ed::CParkManager): the metapieces in the park.

#include "game/ed/park.h"

#include "hook/hook.h"

namespace Ed {

namespace {

// The park's cost (0x0059E190 adds a metapiece's, 0x0059E1E0 takes it away).
void RemovePieceCost(CMetaPiece* meta) { GameFunction<void(__cdecl*)(CMetaPiece*)>(0x0059E1E0)(meta); }

} // namespace

// 0x00599880
uint32_t CParkManager::DestroyMetasInArea(GridDims area, uint32_t flags)
{
    int metas = 0;
    uint16_t pieces = 0;
    CMapListNode* list = GetIntersectingMetas(area, 0, nullptr, (flags >> 1) & 0xFFFFFF01);
    if (list) {
        for (CMapListNode* node = list; node; node = node->mp_next) {
            if ((flags & 4) && (node->GetConcreteMeta()->m_flags & CMetaPiece::kFlag20))
                continue;
            ++metas;
            pieces = static_cast<uint16_t>(pieces + node->GetConcreteMeta()->m_num_used_entries);
            DestroyConcreteMeta(node->GetConcreteMeta(), flags);
        }
    }
    CMapListNode::DestroyList(list);
    mp_generator->CleanUpOutRefs();
    return static_cast<uint32_t>(static_cast<uint16_t>(metas)) << 16 | pieces;
}

// 0x00599960
void CParkManager::HighlightMetasWithinArea(GridDims area, int on)
{
    MapListHolder metas(GetAreaMetaPieces(area));
    for (CMapListNode* node = metas.list; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta();
        if (meta && (meta->m_flags & CMetaPiece::kFlag1) && meta->IsCompletelyWithinArea(&area))
            node->GetConcreteMeta()->Highlight(on, 0);
    }
}

// 0x0059AE90
void CParkManager::DestroyConcreteMeta(CMetaPiece* meta, uint32_t flags)
{
    if (meta->m_flags & CMetaPiece::kCursor)
        return;
    if (CCursor* cursor = CCursor::Instance())
        cursor->ForgetGapAt(meta);
    int gap;
    if (mp_gapManager->GetGapIndex(meta, &gap)) {
        int half;
        RemoveGap(mp_gapManager->GetGapInfo(gap, &half));
    }
    // A change to the park the player made: the compressed map is out of date.
    uint32_t metaFlags = meta->m_flags;
    if (!(metaFlags & CMetaPiece::kFence) && (metaFlags & CMetaPiece::kInPark) && !(metaFlags & CMetaPiece::kInnerShell))
        m_flags &= ~(kMapUpToDate | kMapSaved);
    // What stands on it goes too.
    if (!(meta->m_flags & CMetaPiece::kInnerShell) && (flags & 1)) {
        GridDims above = meta->m_cell_area;
        above.y = static_cast<int8_t>(above.h + above.y);
        above.MakeInfinitelyHigh();
        DestroyMetasInArea(above, 3);
    }
    SetMetaInCells(meta, 0);
    RemoveMetaFromFloor(meta);
    RemoveMetaFromList(meta, &mp_concrete_metas);
    for (int player = 1; player < 9; ++player) {
        if (meta->m_name_checksum == m_restartPieces[player - 1]) {
            mp_generator->RemoveRestart(&meta->m_cell_area, player);
            break;
        }
    }
    for (uint32_t i = 0; i < meta->m_num_used_entries; ++i)
        mp_generator->DestroyPiece(meta->GetDescriptor(i)->piece);
    RemovePieceCost(meta);
    meta->Delete();
    --m_numConcreteMetas;
}

} // namespace Ed

REPLACE(0x00599880, &Ed::CParkManager::DestroyMetasInArea);
REPLACE(0x00599960, &Ed::CParkManager::HighlightMetasWithinArea);
REPLACE(0x0059AE90, &Ed::CParkManager::DestroyConcreteMeta);
