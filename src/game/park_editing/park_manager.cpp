// The park manager (ParkEditing::CParkLayout): making and deleting it, the metapieces in the park, taking it down.

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/park/park_memory.h"
#include "game/park_editing/park_editor.h"

namespace ParkEditing {

namespace {

// The park's cost (0x0059E190 adds a metapiece's, 0x0059E1E0 takes it away).
void AddPieceCost(CPieceGroup* meta) { AddPieceDma(meta); }
void RemovePieceCost(CPieceGroup* meta) { RemovePieceDma(meta); }

// Whether the generator rebuilds its collision (0x00592090 looks): off while every piece is deleted.
bool& GeneratorRebuildsCollision() { return g_collisionAllowed; }

// The park editor's pools (the clipboards', the lists'...) freed (0x0059E4E0).
void DestroyParkPools() { PowerDown(); }

} // namespace

// 0x00598E00
CParkLayout* CParkLayout::MakeNew()
{
    m_buildArea = SCellBox::Make(0, 0, 0, 0, 0, 0);
    m_shellBounds = SCellBox::Make(0, 0, 0, 0, 0, 0);
    for (SCellBox& area : m_blockedAreas)
        area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    void* memory = MemSys::Alloc(0x4A4);
    m_builder = memory ? static_cast<CParkAssembler*>(memory)->MakeNew() : nullptr;
    m_concrete_metas = nullptr;
    m_abstract_metas = nullptr;
    m_numConcreteMetas = 0;
    m_numAbstractMetas = 0;
    std::memset(m_cellMetas, 0, sizeof(m_cellMetas));
    m_parkBuilt = false;
    m_flagBits = 0;
    m_05 = false;
    m_gapRegistry = nullptr;
    m_gapManagerCopy = nullptr;
    // The shell's blocks of each of the 8 kinds.
    for (int i = 1; i <= 8; ++i) {
        char name[128];
        std::snprintf(name, sizeof(name), "floor_wall_block%d", i);
        m_shellBlocks[i - 1][0] = Checksums::HashFromText(name);
        std::snprintf(name, sizeof(name), "wall_block%d", i);
        m_shellBlocks[i - 1][1] = Checksums::HashFromText(name);
        std::snprintf(name, sizeof(name), "floor_block%d", i);
        m_shellBlocks[i - 1][2] = Checksums::HashFromText(name);
    }
    m_shellSize = 0;
    ClearCompressedMap();
    m_riserList = nullptr;
    m_parkName[0] = '\0';
    m_74 = 0;
    m_78 = nullptr;
    return this;
}

// 0x00599BF0
void CParkLayout::SpawnOccurrence()
{
    void* memory = MemSys::Alloc(sizeof(CParkLayout));
    Sole() = memory ? static_cast<CParkLayout*>(memory)->MakeNew() : nullptr;
}

// 0x00599C70
void CParkLayout::DiscardOccurrence()
{
    if (CParkLayout* manager = Sole()) {
        if (manager->m_78) {
            MemSys::FreeMalloc(manager->m_78);
            manager->m_78 = nullptr;
        }
        if (CParkAssembler* generator = manager->m_builder) {
            generator->Teardown();
            MemSys::Dealloc(generator);
        }
        MemSys::Dealloc(manager);
    }
    Sole() = nullptr;
}

// 0x00599A20
void CParkLayout::PlaceMetaPieceInPark(SCellBox pos, CPieceGroup* meta)
{
    meta->m_flagBits |= CPieceGroup::kInPark;
    meta->m_cellFootprint.x = pos.x;
    meta->m_cellFootprint.y = pos.y;
    meta->m_cellFootprint.z = pos.z;

    // Where its pieces go from: the middle of the metapiece in the world (the grid centred on the origin).
    Maths::Vec4 origin;
    origin.x = static_cast<float>((meta->m_cellFootprint.w * 0.5 + pos.x) * kCellWidth -
                                  static_cast<double>(kCellWidth) * kGridCells * 0.5);
    origin.z = static_cast<float>((meta->m_cellFootprint.l * 0.5 + pos.z) * kGridStepAlong -
                                  static_cast<double>(kGridStepAlong) * kGridCells * 0.5);
    origin.y = static_cast<float>(pos.y * static_cast<double>(kGridStepUp));
    if (meta->m_flagBits & CPieceGroup::kRiserBelow)
        origin.y = static_cast<float>(static_cast<double>(origin.y) - kRiserBelowDrop);
    origin.w = 1.0f; // (the original leaves w as it was on the stack)
    for (uint32_t i = 0; i < meta->m_filledSlots; ++i) {
        Maths::Vec4 relative;
        meta->GetRelativePosition(&relative, i);
        CParkTile* piece = meta->GetPiece(i);
        Maths::Vec4 position;
        position.x = static_cast<float>(static_cast<double>(relative.x) + origin.x);
        position.z = static_cast<float>(static_cast<double>(relative.z) + origin.z);
        position.y = static_cast<float>(static_cast<double>(relative.y) + origin.y);
        position.w = 1.0f; // (likewise)
        piece->ChangePlace(&position, 1);
        piece->SetQuarterTurns((piece->m_quarterTurns + meta->m_quarterTurns) & 3, 1);
        m_builder->InsertTile(piece);
        m_builder->LightPiece(piece);
    }
    SetMetaInCells(meta, 1);
    AddMetaToCells(meta);
    for (int player = 1; player < 9; ++player) {
        if (meta->m_nameKey == m_restartPieces[player - 1]) {
            m_builder->PlantRestartPoint(&origin, meta->m_quarterTurns, &pos, player, 0, 0);
            break;
        }
    }
    if (!(meta->m_flagBits & CPieceGroup::kFence) && !(meta->m_flagBits & CPieceGroup::kBuiltShell))
        m_flagBits &= ~(kMapUpToDate | kMapSaved);
    AddPieceCost(meta);
}

// 0x00599880
uint32_t CParkLayout::FreePieceGroupsInRegion(SCellBox area, uint32_t flags)
{
    int metas = 0;
    uint16_t pieces = 0;
    CMapListNode* list = GetIntersectingMetas(area, 0, nullptr, (flags >> 1) & 0xFFFFFF01);
    if (list) {
        for (CMapListNode* node = list; node; node = node->m_following) {
            if ((flags & 4) && (node->GetPlacedPiece()->m_flagBits & CPieceGroup::kFlag20))
                continue;
            ++metas;
            pieces = static_cast<uint16_t>(pieces + node->GetPlacedPiece()->m_filledSlots);
            FreePlacedPiece(node->GetPlacedPiece(), flags);
        }
    }
    CMapListNode::FreeNodeChain(list);
    m_builder->CleanUpOutRefs();
    return static_cast<uint32_t>(static_cast<uint16_t>(metas)) << 16 | pieces;
}

// 0x00599960
void CParkLayout::HighlightMetasWithinArea(SCellBox area, int on)
{
    MapListHolder metas(CollectPiecesInRegion(area));
    for (CMapListNode* node = metas.list; node; node = node->m_following) {
        CPieceGroup* meta = node->GetPieceGroup();
        if (meta && (meta->m_flagBits & CPieceGroup::kFlag1) && meta->FitsInsideRegion(&area))
            node->GetPlacedPiece()->Spotlight(on, 0);
    }
}

// 0x0059AE90
void CParkLayout::FreePlacedPiece(CPieceGroup* meta, uint32_t flags)
{
    if (meta->m_flagBits & CPieceGroup::kCursor)
        return;
    if (CBuildPointer* cursor = CBuildPointer::Sole())
        cursor->ForgetGapAt(meta);
    int gap;
    if (m_gapRegistry->GetGapIndex(meta, &gap)) {
        int half;
        DropGapEntry(m_gapRegistry->GetGapInfo(gap, &half));
    }
    // A change to the park the player made: the compressed map is out of date.
    uint32_t metaFlags = meta->m_flagBits;
    if (!(metaFlags & CPieceGroup::kFence) && (metaFlags & CPieceGroup::kInPark) && !(metaFlags & CPieceGroup::kBuiltShell))
        m_flagBits &= ~(kMapUpToDate | kMapSaved);
    // What stands on it goes too.
    if (!(meta->m_flagBits & CPieceGroup::kBuiltShell) && (flags & 1)) {
        SCellBox above = meta->m_cellFootprint;
        above.y = static_cast<int8_t>(above.h + above.y);
        above.StretchVertically();
        FreePieceGroupsInRegion(above, 3);
    }
    SetMetaInCells(meta, 0);
    RemoveMetaFromCells(meta);
    RemoveMetaFromList(meta, &m_concrete_metas);
    for (int player = 1; player < 9; ++player) {
        if (meta->m_nameKey == m_restartPieces[player - 1]) {
            m_builder->UnplantRestartPoint(&meta->m_cellFootprint, player);
            break;
        }
    }
    for (uint32_t i = 0; i < meta->m_filledSlots; ++i)
        m_builder->DestroyPiece(meta->GetDescriptor(i)->piece);
    RemovePieceCost(meta);
    meta->EraseOne();
    --m_numConcreteMetas;
}

// 0x0059B020
void CParkLayout::DestroyConcreteMetas(int keepRails)
{
    GeneratorRebuildsCollision() = false;
    m_gapRegistry->ClearGapEntries();
    for (CMapListNode* node = m_concrete_metas; node;) {
        CMapListNode* next = node->m_following;
        FreePlacedPiece(node->GetPlacedPiece(), 0);
        node = next;
    }
    m_builder->DestroyPieces(keepRails);
    m_builder->ClearRestarts();
    GeneratorRebuildsCollision() = true;
    if (keepRails)
        m_builder->RebuildCollision();
}

// 0x0059B090
CPieceGroup* CParkLayout::ShiftPlacedPiece(CPieceGroup* meta, int dx, int dy, int dz)
{
    SCellBox pos = meta->m_cellFootprint;
    uint32_t checksum = meta->m_nameKey;
    int rot = meta->m_quarterTurns;
    uint32_t flags = meta->m_flagBits;
    // The gap it's an end of goes with it (the lookup shrinks `pos` to 1x1x1, which the move ignores).
    bool hadGap = false;
    int half;
    SGapDetails* gap = GetGapInfo(&pos, &half);
    SGapDetails moved;
    moved.MakeNew();
    if (gap) {
        moved = *gap;
        hadGap = true;
        DropGapEntry(gap);
        SCellBox& end = moved.m_atPoint[half];
        end.x = static_cast<uint8_t>(end.x + dx);
        end.y = static_cast<int8_t>(end.y + dy);
        end.z = static_cast<uint8_t>(end.z + dz);
    }
    FreePlacedPiece(meta, 0);
    m_builder->CleanUpOutRefs();
    pos.x = static_cast<uint8_t>(pos.x + dx);
    pos.y = static_cast<int8_t>(pos.y + dy);
    pos.z = static_cast<uint8_t>(pos.z + dz);
    CPieceGroup* copy = MakePlacedPiece(FindPieceTemplate(checksum), 0);
    copy->SetQuarterTurns(rot);
    copy->m_flagBits = flags & ~CPieceGroup::kInPark;
    PlaceMetaPieceInPark(pos, copy);
    if (hadGap)
        RegisterGapEntry(&moved);
    return copy;
}

// 0x0059BD40
void CParkLayout::Teardown(int keepRails)
{
    if (!m_parkBuilt)
        return;
    if (!(m_flagBits & (kMapUpToDate | kMapSaved)))
        WriteCompressedMap();
    m_gapRegistry->ClearGapEntries();
    while (m_abstract_metas) {
        CPieceGroup* meta = m_abstract_metas->GetPieceGroup();
        RemoveMetaFromList(meta, &m_abstract_metas);
        if (meta)
            meta->EraseOne();
    }
    m_numAbstractMetas = 0;
    DestroyConcreteMetas(keepRails);
    m_builder->SweepUp(keepRails);
    m_parkBuilt = false;
    m_flagBits = (m_flagBits & ~kMapUpToDate) | kMapSaved;
    m_05 = false;
    if (CGapRegistry* gaps = m_gapRegistry) {
        gaps->Teardown();
        MemSys::Dealloc(gaps);
    }
    m_gapRegistry = nullptr;
    if (m_riserList)
        CMapListNode::FreeNodeChain(m_riserList);
    m_riserList = nullptr;
    DestroyParkPools();
}

} // namespace Ed
