// The park editor's park: accessors of the grid, the compressed map's header, the piece sets and the special
// pieces, and the floor-slide bookkeeping.

#include "game/ed/park.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/park/park_memory.h"

namespace ParkEditing {

float g_parkCellSizeX{120.0f};
float g_parkCellSizeY{48.0f};
float g_parkCellSizeZ{120.0f};
CGapRegistry* g_gapManager;
CParkLayout* g_parkManager;

namespace {

// The size of a cell in world units (0x0064F41C, 0x0064F420, 0x0064F424: 120, 48, 120).
float CellSizeX() { return g_parkCellSizeX; }
float CellSizeY() { return g_parkCellSizeY; }
float CellSizeZ() { return g_parkCellSizeZ; }

// The grid's centre cell (world x and z are 0 at the corner of cell 29).
constexpr int kGridCentre = 29;

// strcpy (the original copies with no length limit).
void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}

} // namespace

CParkLayout* CParkLayout::sSingleton() { return g_parkManager; }

CompressedMap* CParkLayout::GetPackedParkData(bool flagClean)
{
    if (flagClean)
        m_flagBits &= ~kMapNotSavedLocal;
    return &m_map;
}

void CParkLayout::SetPackedParkData(const CompressedMap* map, bool flagDirty)
{
    std::memcpy(&m_map, map, sizeof(m_map));
    m_flagBits |= kMapInSync | kMapNewerThanPark;
    if (flagDirty)
        m_flagBits |= kMapNotSavedLocal;
}

bool CParkLayout::HasLocalParkCopy()
{
    return (m_flagBits & (kMapValid | kMapNewerThanPark)) && !(m_flagBits & kMapNotSavedLocal);
}

int CParkLayout::GetShellSize()
{
    if (m_flagBits & kMapInSync)
        return m_map.m_shellSize;
    return 0;
}

const char* CParkLayout::GetParkName()
{
    if (m_map.m_rev == CompressedMap::kVersion)
        return m_map.m_label;
    return nullptr;
}

void CParkLayout::SetParkName(const char* name)
{
    if (m_map.m_rev != CompressedMap::kVersion)
        return;
    CopyText(m_map.m_label, name);
    CopyText(m_parkName, name);
}

void CParkLayout::RebuildParkGeometry(int how)
{
    // CParkAssembler's three updates
    switch (how) {
    case 0:
        m_builder->CleanUpOutRefs(); // take out the removed pieces
        break;
    case 1:
        m_builder->RebuildAll();
        break;
    case 2:
        m_builder->RebuildMarked();
        break;
    }
}

uint32_t CParkLayout::GetShellChecksum()
{
    char name[40];
    if ((m_flagBits & kMapInSync) && m_map.m_shellSize != 0)
        std::snprintf(name, sizeof(name), "sk5ed%d_shell", m_map.m_shellSize + 1); // 0x0064F650
    else
        std::memcpy(name, "sk5ed_shell", 12); // 0x0064F644
    return Checksums::HashFromText(name);
}

CMapListNode* CParkLayout::GetCellChain(int x, int z) { return m_cellMetas[x][z]; }

CPieceFamily* CParkLayout::GetPieceFamily(int index, int* usedBefore)
{
    int used = 0;
    for (int i = 0; i < index; ++i) {
        if (m_sets[i].m_isDisplayed)
            ++used;
    }
    if (usedBefore)
        *usedBefore = used;
    return &m_sets[index];
}

void CParkLayout::GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* theme,
                              uint32_t* unknown18, int* width, int* length)
{
    *width = map->m_w;
    *length = map->m_l;
    *numGaps = map->m_numGaps;
    *numMetas = map->m_numMetas;
    *theme = map->m_shellSize;
    *unknown18 = map->m_styleSet ? map->m_styleSet : 0x1CA1FF20; // Default
}

void CParkLayout::RestoreFloorHeights()
{
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            FloorCell& cell = m_floor[x][z];
            if (cell.m_touched) {
                cell.m_touched = false;
                cell.m_levelY -= cell.m_delta;
            }
            if (cell.m_levelY > 15)
                cell.m_levelY = 15;
            else if (cell.m_levelY < -16)
                cell.m_levelY = -16;
        }
    }
}

int CParkLayout::GetRestartIndex(uint32_t name)
{
    for (int i = 1; i <= kNumRestartPieces; ++i) {
        if (m_restartPieces[i - 1] == name)
            return i;
    }
    return 0;
}

CPieceGroup* CParkLayout::GetAbstractMetaPiece(uint32_t name, int* index)
{
    int position = m_numAbstractMetas - 1;
    for (CMapListNode* node = mp_abstract_metas; node; node = node->m_following, --position) {
        CPieceGroup* meta = node->GetPieceGroup()->Cast();
        if (meta->m_nameKey == name) {
            if (index)
                *index = position;
            return meta;
        }
    }
    return nullptr;
}

bool CParkLayout::CanResizeTo(SCellBox area)
{
    // 0x0059EB30: the park memory check for going from one floor area to another
    return PiecesFit(m_parkBounds.w * m_parkBounds.l, area.w * area.l);
}

int CParkLayout::GetGroundLevel(SCellBox area, bool* uniform)
{
    if (uniform)
        *uniform = true;
    int first = m_floor[area.x][area.z].m_levelY;
    int highest = -10000;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            int height = m_floor[x][z].m_levelY;
            if (uniform && height != first)
                *uniform = false;
            if (height > highest)
                highest = height;
        }
    }
    return highest;
}

Maths::Vec4 CParkLayout::CellToWorldPos(const SCellBox& area, Maths::Vec4* dimensions)
{
    float x = static_cast<float>(area.x - kGridCentre) * CellSizeX();
    float y = static_cast<float>(area.y) * CellSizeY();
    float z = static_cast<float>(area.z - kGridCentre) * CellSizeZ();
    if (dimensions) {
        dimensions->x = static_cast<float>(area.w) * CellSizeX();
        dimensions->y = static_cast<float>(area.h) * CellSizeY();
        dimensions->z = static_cast<float>(area.l) * CellSizeZ();
    }
    return {x, y, z, 1.0f};
}

bool CParkLayout::IsAreaBlocked(SCellBox area)
{
    for (uint32_t i = 0; i < m_numBlockedAreas; ++i) {
        const SCellBox& mask = m_blockedAreas[i];
        if (area.x + area.w > mask.x && area.x < mask.x + mask.w && area.z + area.l > mask.z &&
            area.z < mask.z + mask.l)
            return true;
    }
    return false;
}

namespace {

// CGapRegistry (0xA8 bytes): the instance in use (sSingleton, 0x0058CBB0), constructor, copying and destructor.
void*& GapManagerInstance() { return reinterpret_cast<void*&>(g_gapManager); }

} // namespace

void CParkLayout::CreateGapManagerCopy()
{
    void* current = GapManagerInstance();
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap);
    void* memory = MemSys::Alloc(0xA8);
    CGapRegistry* copy = memory ? static_cast<CGapRegistry*>(memory)->MakeNew(nullptr) : nullptr;
    mp_gapManagerCopy = copy;
    copy->CopyFrom(static_cast<CGapRegistry*>(current)); // (null if out of memory)
    MemSys::LeaveHeapScope();
    GapManagerInstance() = current; // (the constructor made the copy current)
}

void CParkLayout::UseGapManagerCopy() { GapManagerInstance() = mp_gapManagerCopy; }

void CParkLayout::DestroyGapManagerCopy()
{
    CGapRegistry* copy = mp_gapManagerCopy;
    if (!copy)
        return;
    copy->Teardown();
    MemSys::Hub::Pull()->Dealloc(copy);
    mp_gapManagerCopy = nullptr;
}

void CParkLayout::HighlightGapPieces(bool on, SGapDetails* gap)
{
    for (CMapListNode* node = mp_concrete_metas; node; node = node->m_following) {
        if (node->GetPieceGroup()->Cast()->m_flagBits & CPieceGroup::kHighlightable)
            node->GetPieceGroup()->Cast()->Spotlight(on, 1);
    }
    m_builder->SetPiecesHighlighted(0);
    if (on) {
        if (gap && gap->m_tabIndex >= 0)
            mp_gapManager->SetCurrentGap(gap->m_tabIndex);
    } else {
        m_builder->SetPiecesHighlighted(0);
    }
}

void CParkLayout::ClearFloorChangeMarks()
{
    for (int x = m_shellBounds.x; x < m_shellBounds.x + m_shellBounds.w; ++x) {
        for (int z = m_shellBounds.z; z < m_shellBounds.z + m_shellBounds.l; ++z) {
            m_floor[x][z].m_touched = false;
            m_floor[x][z].m_delta = 0;
        }
    }
    for (CMapListNode* node = mp_concrete_metas; node; node = node->m_following)
        node->GetPieceGroup()->Cast()->m_flagBits &= ~CPieceGroup::kFlag20;
}

} // namespace Ed

using ParkEditing::CParkLayout;
