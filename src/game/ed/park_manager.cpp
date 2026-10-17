// The park manager (Ed::CParkManager): making and deleting it, the metapieces in the park, taking it down.

#include "game/ed/park.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// The park's cost (0x0059E190 adds a metapiece's, 0x0059E1E0 takes it away).
void AddPieceCost(CMetaPiece* meta) { GameFunction<void(__cdecl*)(CMetaPiece*)>(0x0059E190)(meta); }
void RemovePieceCost(CMetaPiece* meta) { GameFunction<void(__cdecl*)(CMetaPiece*)>(0x0059E1E0)(meta); }

// Whether the generator rebuilds its collision (0x00592090 looks): off while every piece is deleted.
bool& GeneratorRebuildsCollision() { return GameVariable<bool>(0x0067F398); }

// The park editor's pools (the clipboards', the lists'...) freed (0x0059E4E0).
void DestroyParkPools() { GameFunction<void(__cdecl*)()>(0x0059E4E0)(); }

} // namespace

// 0x00598E00
CParkManager* CParkManager::Construct()
{
    m_parkBounds = GridDims::Make(0, 0, 0, 0, 0, 0);
    m_shellBounds = GridDims::Make(0, 0, 0, 0, 0, 0);
    for (GridDims& area : m_blockedAreas)
        area = GridDims::Make(0, 0, 0, 0, 0, 0);
    void* memory = Mem::New(0x4A4);
    mp_generator = memory ? GameFunction<CParkGenerator*(__thiscall*)(void*)>(0x00593460)(memory) : nullptr;
    mp_concrete_metas = nullptr;
    mp_abstract_metas = nullptr;
    m_numConcreteMetas = 0;
    m_numAbstractMetas = 0;
    std::memset(m_cellMetas, 0, sizeof(m_cellMetas));
    m_parkBuilt = false;
    m_flags = 0;
    m_05 = false;
    mp_gapManager = nullptr;
    mp_gapManagerCopy = nullptr;
    // The shell's blocks of each of the 8 kinds.
    for (int i = 1; i <= 8; ++i) {
        char name[128];
        std::snprintf(name, sizeof(name), "floor_wall_block%d", i);
        m_shellBlocks[i - 1][0] = Crc::GenerateCRCFromString(name);
        std::snprintf(name, sizeof(name), "wall_block%d", i);
        m_shellBlocks[i - 1][1] = Crc::GenerateCRCFromString(name);
        std::snprintf(name, sizeof(name), "floor_block%d", i);
        m_shellBlocks[i - 1][2] = Crc::GenerateCRCFromString(name);
    }
    m_shellSize = 0;
    ClearCompressedMap();
    mp_riserList = nullptr;
    m_parkName[0] = '\0';
    m_74 = 0;
    mp_78 = nullptr;
    return this;
}

// 0x00599BF0
void CParkManager::CreateInstance()
{
    void* memory = Mem::New(sizeof(CParkManager));
    Instance() = memory ? static_cast<CParkManager*>(memory)->Construct() : nullptr;
}

// 0x00599C70
void CParkManager::DeleteInstance()
{
    if (CParkManager* manager = Instance()) {
        if (manager->mp_78) {
            Mem::FreeMalloc(manager->mp_78);
            manager->mp_78 = nullptr;
        }
        if (CParkGenerator* generator = manager->mp_generator) {
            generator->Destroy();
            Mem::Free(generator);
        }
        Mem::Free(manager);
    }
    Instance() = nullptr;
}

// 0x00599A20
void CParkManager::AddMetaPieceToPark(GridDims pos, CMetaPiece* meta)
{
    meta->m_flags |= CMetaPiece::kInPark;
    meta->m_cell_area.x = pos.x;
    meta->m_cell_area.y = pos.y;
    meta->m_cell_area.z = pos.z;

    // Where its pieces go from: the middle of the metapiece in the world (the grid centred on the origin).
    Mth::Vector origin;
    origin.x = static_cast<float>((meta->m_cell_area.w * 0.5 + pos.x) * kCellWidth -
                                  static_cast<double>(kCellWidth) * kGridCells * 0.5);
    origin.z = static_cast<float>((meta->m_cell_area.l * 0.5 + pos.z) * kCellLength -
                                  static_cast<double>(kCellLength) * kGridCells * 0.5);
    origin.y = static_cast<float>(pos.y * static_cast<double>(kCellHeight));
    if (meta->m_flags & CMetaPiece::kRiserBelow)
        origin.y = static_cast<float>(static_cast<double>(origin.y) - kRiserBelowDrop);
    origin.w = 1.0f; // (the original leaves w as it was on the stack)
    for (uint32_t i = 0; i < meta->m_num_used_entries; ++i) {
        Mth::Vector relative;
        meta->GetRelativePosition(&relative, i);
        CPiece* piece = meta->GetPiece(i);
        Mth::Vector position;
        position.x = static_cast<float>(static_cast<double>(relative.x) + origin.x);
        position.z = static_cast<float>(static_cast<double>(relative.z) + origin.z);
        position.y = static_cast<float>(static_cast<double>(relative.y) + origin.y);
        position.w = 1.0f; // (likewise)
        piece->SetPosition(&position, 1);
        piece->SetRot((piece->m_rot + meta->m_rot) & 3, 1);
        mp_generator->AddPiece(piece);
        mp_generator->AddCollision(piece);
    }
    SetMetaInCells(meta, 1);
    AddMetaToCells(meta);
    for (int player = 1; player < 9; ++player) {
        if (meta->m_name_checksum == m_restartPieces[player - 1]) {
            mp_generator->AddRestart(&origin, meta->m_rot, &pos, player, 0, 0);
            break;
        }
    }
    if (!(meta->m_flags & CMetaPiece::kFence) && !(meta->m_flags & CMetaPiece::kInnerShell))
        m_flags &= ~(kMapUpToDate | kMapSaved);
    AddPieceCost(meta);
}

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
        above.MakeInfiniteOnY();
        DestroyMetasInArea(above, 3);
    }
    SetMetaInCells(meta, 0);
    RemoveMetaFromCells(meta);
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

// 0x0059B020
void CParkManager::DestroyConcreteMetas(int keepRails)
{
    GeneratorRebuildsCollision() = false;
    mp_gapManager->RemoveAllGaps();
    for (CMapListNode* node = mp_concrete_metas; node;) {
        CMapListNode* next = node->mp_next;
        DestroyConcreteMeta(node->GetConcreteMeta(), 0);
        node = next;
    }
    mp_generator->DestroyPieces(keepRails);
    mp_generator->ClearRestarts();
    GeneratorRebuildsCollision() = true;
    if (keepRails)
        mp_generator->RebuildCollision();
}

// 0x0059B090
CMetaPiece* CParkManager::RelocateMetaPiece(CMetaPiece* meta, int dx, int dy, int dz)
{
    GridDims pos = meta->m_cell_area;
    uint32_t checksum = meta->m_name_checksum;
    int rot = meta->m_rot;
    uint32_t flags = meta->m_flags;
    // The gap it's an end of goes with it (the lookup shrinks `pos` to 1x1x1, which the move ignores).
    bool hadGap = false;
    int half;
    GapInfo* gap = GetGapInfo(&pos, &half);
    GapInfo moved;
    GameFunction<GapInfo*(__thiscall*)(GapInfo*)>(0x0058CBC0)(&moved);
    if (gap) {
        moved = *gap;
        hadGap = true;
        RemoveGap(gap);
        GridDims& end = moved.m_pos[half];
        end.x = static_cast<uint8_t>(end.x + dx);
        end.y = static_cast<int8_t>(end.y + dy);
        end.z = static_cast<uint8_t>(end.z + dz);
    }
    DestroyConcreteMeta(meta, 0);
    mp_generator->CleanUpOutRefs();
    pos.x = static_cast<uint8_t>(pos.x + dx);
    pos.y = static_cast<int8_t>(pos.y + dy);
    pos.z = static_cast<uint8_t>(pos.z + dz);
    CMetaPiece* copy = CreateConcreteMeta(GetAbstractMeta(checksum), 0);
    copy->SetRot(rot);
    copy->m_flags = flags & ~CMetaPiece::kInPark;
    AddMetaPieceToPark(pos, copy);
    if (hadGap)
        AddGap(&moved);
    return copy;
}

// 0x0059BD40
void CParkManager::Destroy(int keepRails)
{
    if (!m_parkBuilt)
        return;
    if (!(m_flags & (kMapUpToDate | kMapSaved)))
        WriteCompressedMap();
    mp_gapManager->RemoveAllGaps();
    while (mp_abstract_metas) {
        CMetaPiece* meta = mp_abstract_metas->GetMeta();
        RemoveMetaFromList(meta, &mp_abstract_metas);
        if (meta)
            meta->Delete();
    }
    m_numAbstractMetas = 0;
    DestroyConcreteMetas(keepRails);
    mp_generator->CleanUp(keepRails);
    m_parkBuilt = false;
    m_flags = (m_flags & ~kMapUpToDate) | kMapSaved;
    m_05 = false;
    if (CGapManager* gaps = mp_gapManager) {
        gaps->Destroy();
        Mem::Free(gaps);
    }
    mp_gapManager = nullptr;
    if (mp_riserList)
        CMapListNode::DestroyList(mp_riserList);
    mp_riserList = nullptr;
    DestroyParkPools();
}

} // namespace Ed

REPLACE(0x00598E00, &Ed::CParkManager::Construct);
REPLACE(0x00599BF0, &Ed::CParkManager::CreateInstance);
REPLACE(0x00599C70, &Ed::CParkManager::DeleteInstance);
REPLACE(0x00599A20, &Ed::CParkManager::AddMetaPieceToPark);
REPLACE(0x0059B020, &Ed::CParkManager::DestroyConcreteMetas);
REPLACE(0x0059B090, &Ed::CParkManager::RelocateMetaPiece);
REPLACE(0x0059BD40, &Ed::CParkManager::Destroy);
REPLACE(0x00599880, &Ed::CParkManager::DestroyMetasInArea);
REPLACE(0x00599960, &Ed::CParkManager::HighlightMetasWithinArea);
REPLACE(0x0059AE90, &Ed::CParkManager::DestroyConcreteMeta);
