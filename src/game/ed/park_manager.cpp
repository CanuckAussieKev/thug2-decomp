// The park editor's park: accessors of the grid, the compressed map's header, the piece sets and the special
// pieces, and the floor-slide bookkeeping.

#include "game/ed/park_manager.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// The size of a cell in world units (0x0064F41C, 0x0064F420, 0x0064F424: 120, 48, 120).
float CellWidth() { return GameVariable<float>(0x0064F41C); }
float CellHeight() { return GameVariable<float>(0x0064F420); }
float CellLength() { return GameVariable<float>(0x0064F424); }

// The grid's centre cell (world x and z are 0 at the corner of cell 29).
constexpr int kGridCentre = 29;

// strcpy (the original copies with no length limit).
void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}

} // namespace

CParkManager* CParkManager::sInstance() { return GameVariable<CParkManager*>(0x007CE604); }

CompressedMap* CParkManager::GetCompressedMapBuffer(bool markSaved)
{
    if (markSaved)
        m_compressedMapFlags &= ~kMapNotSavedLocal;
    return &m_compressedMap;
}

void CParkManager::SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved)
{
    std::memcpy(&m_compressedMap, map, sizeof(m_compressedMap));
    m_compressedMapFlags |= kMapInSync | kMapNewerThanPark;
    if (markNotSaved)
        m_compressedMapFlags |= kMapNotSavedLocal;
}

bool CParkManager::IsMapSavedLocally()
{
    return (m_compressedMapFlags & (kMapValid | kMapNewerThanPark)) && !(m_compressedMapFlags & kMapNotSavedLocal);
}

int CParkManager::GetTheme()
{
    if (m_compressedMapFlags & kMapInSync)
        return m_compressedMap.m_theme;
    return 0;
}

const char* CParkManager::GetParkName()
{
    if (m_compressedMap.m_version == CompressedMap::kVersion)
        return m_compressedMap.m_parkName;
    return nullptr;
}

void CParkManager::SetParkName(const char* name)
{
    if (m_compressedMap.m_version != CompressedMap::kVersion)
        return;
    CopyText(m_compressedMap.m_parkName, name);
    CopyText(m_parkName, name);
}

void CParkManager::RebuildParkGeometry(int how)
{
    // CParkGenerator's three updates
    switch (how) {
    case 0:
        GameFunction<void(__thiscall*)(void*)>(0x00593610)(mp_generator); // take out the removed pieces
        break;
    case 1:
        GameFunction<void(__thiscall*)(void*)>(0x005947D0)(mp_generator); // rebuild everything
        break;
    case 2:
        GameFunction<void(__thiscall*)(void*)>(0x00594910)(mp_generator); // rebuild the marked pieces
        break;
    }
}

uint32_t CParkManager::GetShellChecksum()
{
    char name[40];
    if ((m_compressedMapFlags & kMapInSync) && m_compressedMap.m_theme != 0)
        std::snprintf(name, sizeof(name), "sk5ed%d_shell", m_compressedMap.m_theme + 1); // 0x0064F650
    else
        std::memcpy(name, "sk5ed_shell", 12); // 0x0064F644
    return Crc::GenerateCRCFromString(name);
}

CMapListNode* CParkManager::GetMapListNode(int x, int z) { return mp_bucketList[x][z]; }

CPieceSet* CParkManager::GetPieceSet(int index, int* usedBefore)
{
    int used = 0;
    for (int i = 0; i < index; ++i) {
        if (m_sets[i].m_isUsed)
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
    *theme = map->m_theme;
    *unknown18 = map->m_unknown18 ? map->m_unknown18 : 0x1CA1FF20; // Default
}

void CParkManager::UndoFloorSlide()
{
    for (int x = 0; x < kFloorMaxWidth; ++x) {
        for (int z = 0; z < kFloorMaxLength; ++z) {
            FloorHeightEntry& cell = m_floor[x][z];
            if (cell.m_markAsSlid) {
                cell.m_markAsSlid = false;
                cell.m_height -= cell.m_slideAmount;
            }
            if (cell.m_height > 15)
                cell.m_height = 15;
            else if (cell.m_height < -16)
                cell.m_height = -16;
        }
    }
}

int CParkManager::GetSpecialPieceIndex(uint32_t name)
{
    for (int i = 1; i <= kNumSpecialPieces; ++i) {
        if (m_specialPieceNames[i - 1] == name)
            return i;
    }
    return 0;
}

CMetaPiece* CParkManager::GetAbstractMetaPiece(uint32_t name, int* index)
{
    int position = m_numAbstractMetas - 1;
    for (CMapListNode* node = mp_abstractMetaList; node; node = node->mp_next, --position) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        if (meta->m_nameChecksum == name) {
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
    return GameFunction<bool(__cdecl*)(int, int)>(0x0059EB30)(m_parkNearBounds.w * m_parkNearBounds.l,
                                                             area.w * area.l);
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
    float x = static_cast<float>(area.x - kGridCentre) * CellWidth();
    float y = static_cast<float>(area.y) * CellHeight();
    float z = static_cast<float>(area.z - kGridCentre) * CellLength();
    if (dimensions) {
        dimensions->x = static_cast<float>(area.w) * CellWidth();
        dimensions->y = static_cast<float>(area.h) * CellHeight();
        dimensions->z = static_cast<float>(area.l) * CellLength();
    }
    return {x, y, z, 1.0f};
}

bool CParkManager::IntersectsRoadMask(GridDims area)
{
    for (uint32_t i = 0; i < m_roadMaskLength; ++i) {
        const GridDims& mask = m_roadMask[i];
        if (area.x + area.w > mask.x && area.x < mask.x + mask.w && area.z + area.l > mask.z &&
            area.z < mask.z + mask.l)
            return true;
    }
    return false;
}

namespace {

// CGapManager (0xA8 bytes): the instance in use (sInstance, 0x0058CBB0), constructor, copying and destructor.
void*& GapManagerInstance() { return GameVariable<void*>(0x007CE5D4); }

} // namespace

void CParkManager::CreateGapManagerCopy()
{
    void* current = GapManagerInstance();
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    void* memory = Mem::New(0xA8);
    void* copy = memory ? GameFunction<void*(__thiscall*)(void*, int)>(0x0058CCC0)(memory, 0) : nullptr;
    mp_gapManagerCopy = copy;
    GameFunction<void(__thiscall*)(void*, void*)>(0x0058CF30)(copy, current); // (null if out of memory)
    Mem::PopContext();
    GapManagerInstance() = current; // (the constructor made the copy current)
}

void CParkManager::UseGapManagerCopy() { GapManagerInstance() = mp_gapManagerCopy; }

void CParkManager::DestroyGapManagerCopy()
{
    void* copy = mp_gapManagerCopy;
    if (!copy)
        return;
    GameFunction<void(__thiscall*)(void*)>(0x0058CF20)(copy); // the destructor
    Mem::Manager::Get()->Free(copy);
    mp_gapManagerCopy = nullptr;
}

void CParkManager::HighlightGapPieces(bool on, GapInfo* gap)
{
    for (CMapListNode* node = mp_concreteMetaList; node; node = node->mp_next) {
        if (node->GetMeta()->Cast()->m_flags & CMetaPiece::kFlagInGap) // 0x00591A50: highlight its pieces
            GameFunction<void(__thiscall*)(CMetaPiece*, bool, int)>(0x00591A50)(node->GetMeta()->Cast(), on, 1);
    }
    auto refresh = GameFunction<void(__thiscall*)(void*, int)>(0x00592290); // CParkGenerator
    refresh(mp_generator, 0);
    if (on) {
        if (gap && gap->m_tabIndex >= 0) // 0x0058CB70: the gap manager's current gap
            GameFunction<void(__thiscall*)(void*, int)>(0x0058CB70)(mp_gapManager, gap->m_tabIndex);
    } else {
        refresh(mp_generator, 0);
    }
}

void CParkManager::ResetSlideMarks()
{
    for (int x = m_parkFarBounds.x; x < m_parkFarBounds.x + m_parkFarBounds.w; ++x) {
        for (int z = m_parkFarBounds.z; z < m_parkFarBounds.z + m_parkFarBounds.l; ++z) {
            m_floor[x][z].m_markAsSlid = false;
            m_floor[x][z].m_slideAmount = 0;
        }
    }
    for (CMapListNode* node = mp_concreteMetaList; node; node = node->mp_next)
        node->GetMeta()->Cast()->m_flags &= ~CMetaPiece::kFlagSlid;
}

} // namespace Ed

using Ed::CParkManager;

REPLACE(0x00595030, &CParkManager::GetCompressedMapBuffer);
REPLACE(0x00595050, &CParkManager::SetCompressedMapBuffer);
REPLACE(0x00595090, &CParkManager::IsMapSavedLocally);
REPLACE(0x005950B0, &CParkManager::GetTheme);
REPLACE(0x005950D0, &CParkManager::GetParkName);
REPLACE(0x005950F0, &CParkManager::SetParkName);
REPLACE(0x00595140, &CParkManager::RebuildParkGeometry);
REPLACE(0x00595170, &CParkManager::GetShellChecksum);
REPLACE(0x005951E0, &CParkManager::GetMapListNode);
REPLACE(0x00595200, &CParkManager::sInstance);
REPLACE(0x00595210, &CParkManager::GetPieceSet);
REPLACE(0x00595250, &CParkManager::GetMapInfo);
REPLACE(0x005952B0, &CParkManager::UndoFloorSlide);
REPLACE(0x00595300, &CParkManager::GetSpecialPieceIndex);
REPLACE(0x00595620, &CParkManager::GetAbstractMetaPiece);
REPLACE(0x00595990, &CParkManager::CanResizeTo);
REPLACE(0x005959D0, &CParkManager::GetFloorHeight);
REPLACE(0x00595A90, &CParkManager::GridCoordinatesToWorld);
REPLACE(0x00595B30, &CParkManager::IntersectsRoadMask);
REPLACE(0x00595BC0, &CParkManager::CreateGapManagerCopy);
REPLACE(0x00595C70, &CParkManager::UseGapManagerCopy);
REPLACE(0x00595C80, &CParkManager::DestroyGapManagerCopy);
REPLACE(0x00595CB0, &CParkManager::HighlightGapPieces);
REPLACE(0x00595D40, &CParkManager::ResetSlideMarks);
