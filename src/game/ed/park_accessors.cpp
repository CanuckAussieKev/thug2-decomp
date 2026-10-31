// The park editor's park: accessors of the grid, the compressed map's header, the piece sets and the special
// pieces, and the floor-slide bookkeeping.

#include "game/ed/park.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/park/park_memory.h"

namespace Ed {

float g_parkCellSizeX{120.0f};
float g_parkCellSizeY{48.0f};
float g_parkCellSizeZ{120.0f};
CGapManager* g_gapManager;
CParkManager* g_parkManager;

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

CParkManager* CParkManager::sInstance() { return g_parkManager; }

CompressedMap* CParkManager::GetCompressedMapBuffer(bool markSaved)
{
    if (markSaved)
        m_flags &= ~kMapNotSavedLocal;
    return &m_map;
}

void CParkManager::SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved)
{
    std::memcpy(&m_map, map, sizeof(m_map));
    m_flags |= kMapInSync | kMapNewerThanPark;
    if (markNotSaved)
        m_flags |= kMapNotSavedLocal;
}

bool CParkManager::IsMapSavedLocally()
{
    return (m_flags & (kMapValid | kMapNewerThanPark)) && !(m_flags & kMapNotSavedLocal);
}

int CParkManager::GetShellSize()
{
    if (m_flags & kMapInSync)
        return m_map.m_shellSize;
    return 0;
}

const char* CParkManager::GetParkName()
{
    if (m_map.m_version == CompressedMap::kVersion)
        return m_map.m_name;
    return nullptr;
}

void CParkManager::SetParkName(const char* name)
{
    if (m_map.m_version != CompressedMap::kVersion)
        return;
    CopyText(m_map.m_name, name);
    CopyText(m_parkName, name);
}

void CParkManager::RebuildParkGeometry(int how)
{
    // CParkGenerator's three updates
    switch (how) {
    case 0:
        mp_generator->CleanUpOutRefs(); // take out the removed pieces
        break;
    case 1:
        mp_generator->RebuildAll();
        break;
    case 2:
        mp_generator->RebuildMarked();
        break;
    }
}

uint32_t CParkManager::GetShellChecksum()
{
    char name[40];
    if ((m_flags & kMapInSync) && m_map.m_shellSize != 0)
        std::snprintf(name, sizeof(name), "sk5ed%d_shell", m_map.m_shellSize + 1); // 0x0064F650
    else
        std::memcpy(name, "sk5ed_shell", 12); // 0x0064F644
    return Crc::GenerateCRCFromString(name);
}

CMapListNode* CParkManager::GetMapListNode(int x, int z) { return m_cellMetas[x][z]; }

CPieceSet* CParkManager::GetPieceSet(int index, int* usedBefore)
{
    int used = 0;
    for (int i = 0; i < index; ++i) {
        if (m_sets[i].m_visible)
            ++used;
    }
    if (usedBefore)
        *usedBefore = used;
    return &m_sets[index];
}

void CParkManager::GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* theme,
                              uint32_t* unknown18, int* width, int* length)
{
    *width = map->m_w;
    *length = map->m_l;
    *numGaps = map->m_numGaps;
    *numMetas = map->m_numMetas;
    *theme = map->m_shellSize;
    *unknown18 = map->m_theme ? map->m_theme : 0x1CA1FF20; // Default
}

void CParkManager::RestoreFloorHeights()
{
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            FloorCell& cell = m_floor[x][z];
            if (cell.m_touched) {
                cell.m_touched = false;
                cell.m_height -= cell.m_delta;
            }
            if (cell.m_height > 15)
                cell.m_height = 15;
            else if (cell.m_height < -16)
                cell.m_height = -16;
        }
    }
}

int CParkManager::GetRestartIndex(uint32_t name)
{
    for (int i = 1; i <= kNumRestartPieces; ++i) {
        if (m_restartPieces[i - 1] == name)
            return i;
    }
    return 0;
}

CMetaPiece* CParkManager::GetAbstractMetaPiece(uint32_t name, int* index)
{
    int position = m_numAbstractMetas - 1;
    for (CMapListNode* node = mp_abstract_metas; node; node = node->mp_next, --position) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        if (meta->m_name_checksum == name) {
            if (index)
                *index = position;
            return meta;
        }
    }
    return nullptr;
}

bool CParkManager::CanResizeTo(GridDims area)
{
    // 0x0059EB30: the park memory check for going from one floor area to another
    return PiecesFit(m_parkBounds.w * m_parkBounds.l, area.w * area.l);
}

int CParkManager::GetFloorHeight(GridDims area, bool* uniform)
{
    if (uniform)
        *uniform = true;
    int first = m_floor[area.x][area.z].m_height;
    int highest = -10000;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            int height = m_floor[x][z].m_height;
            if (uniform && height != first)
                *uniform = false;
            if (height > highest)
                highest = height;
        }
    }
    return highest;
}

Mth::Vector CParkManager::GridCoordinatesToWorld(const GridDims& area, Mth::Vector* dimensions)
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

bool CParkManager::IsAreaBlocked(GridDims area)
{
    for (uint32_t i = 0; i < m_numBlockedAreas; ++i) {
        const GridDims& mask = m_blockedAreas[i];
        if (area.x + area.w > mask.x && area.x < mask.x + mask.w && area.z + area.l > mask.z &&
            area.z < mask.z + mask.l)
            return true;
    }
    return false;
}

namespace {

// CGapManager (0xA8 bytes): the instance in use (sInstance, 0x0058CBB0), constructor, copying and destructor.
void*& GapManagerInstance() { return reinterpret_cast<void*&>(g_gapManager); }

} // namespace

void CParkManager::CreateGapManagerCopy()
{
    void* current = GapManagerInstance();
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    void* memory = Mem::New(0xA8);
    CGapManager* copy = memory ? static_cast<CGapManager*>(memory)->Construct(nullptr) : nullptr;
    mp_gapManagerCopy = copy;
    copy->CopyFrom(static_cast<CGapManager*>(current)); // (null if out of memory)
    Mem::PopContext();
    GapManagerInstance() = current; // (the constructor made the copy current)
}

void CParkManager::UseGapManagerCopy() { GapManagerInstance() = mp_gapManagerCopy; }

void CParkManager::DestroyGapManagerCopy()
{
    CGapManager* copy = mp_gapManagerCopy;
    if (!copy)
        return;
    copy->Destroy();
    Mem::Manager::Get()->Free(copy);
    mp_gapManagerCopy = nullptr;
}

void CParkManager::HighlightGapPieces(bool on, GapInfo* gap)
{
    for (CMapListNode* node = mp_concrete_metas; node; node = node->mp_next) {
        if (node->GetMeta()->Cast()->m_flags & CMetaPiece::kHighlightable)
            node->GetMeta()->Cast()->Highlight(on, 1);
    }
    mp_generator->SetPiecesHighlighted(0);
    if (on) {
        if (gap && gap->m_tabIndex >= 0)
            mp_gapManager->SetCurrentGap(gap->m_tabIndex);
    } else {
        mp_generator->SetPiecesHighlighted(0);
    }
}

void CParkManager::ClearFloorChangeMarks()
{
    for (int x = m_shellBounds.x; x < m_shellBounds.x + m_shellBounds.w; ++x) {
        for (int z = m_shellBounds.z; z < m_shellBounds.z + m_shellBounds.l; ++z) {
            m_floor[x][z].m_touched = false;
            m_floor[x][z].m_delta = 0;
        }
    }
    for (CMapListNode* node = mp_concrete_metas; node; node = node->mp_next)
        node->GetMeta()->Cast()->m_flags &= ~CMetaPiece::kFlag20;
}

} // namespace Ed

using Ed::CParkManager;
