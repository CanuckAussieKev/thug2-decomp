// The park editor's manager and cursor, 0x0059C900-0x0059E0F7 (see park_editor.h).

#include "game/park/park_editor.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/park/park_memory.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kNewPark = 0x1A4E0EF9;
constexpr uint16_t kParkInfoVersion = 0x4E24;
constexpr uint32_t kMetaFloor = 0x8;       // a meta piece's flag: part of the floor
constexpr uint32_t kMetaLocked = 0x100;    // (not destroyed by an area's deletion when asked)
constexpr uint32_t kMetaInCursor = 0x200;  // held by the cursor

uint32_t& MetaFlags(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0xC); }
GridDims& MetaArea(void* meta) { return *reinterpret_cast<GridDims*>(static_cast<uint8_t*>(meta) + 0x4); }

// The scene is rebuilt while this is set (0x0067F398; cleared around the floor's rebuild).
bool& SceneUpdates() { return GameVariable<bool>(0x0067F398); }

// CParkManager (not decompiled yet).
void CallDestroyConcreteMeta(CParkManager* manager, void* meta, int flags)
{
    GameFunction<void(__thiscall*)(CParkManager*, void*, int)>(0x0059AE90)(manager, meta, flags);
}
void RebuildFloorAndPieces(CParkManager* manager, const GridDims* area, int flags)
{
    GameFunction<void(__thiscall*)(CParkManager*, const GridDims*, int)>(0x0059C600)(manager, area, flags);
}
CMapListNode* GetMetasWithin(CParkManager* manager, GridDims area)
{
    return GameFunction<CMapListNode*(__thiscall*)(CParkManager*, GridDims)>(0x00597A00)(manager, area);
}
// The generator's update of the pieces it builds (0x00593610) and its rebuild of the scene (0x00592090).
void UpdatePieces(void* generator) { GameFunction<void(__thiscall*)(void*)>(0x00593610)(generator); }
void RebuildScene(void* generator) { GameFunction<void(__thiscall*)(void*)>(0x00592090)(generator); }
// The engine's scene (0x004F4970).
void* Engine() { return GameFunction<void*(__cdecl*)()>(0x004F4970)(); }

// A meta piece of a list node (0x005480C0: the node's +4; 0x005902E0: the concrete piece - the same pointer).
void* NodeMeta(CMapListNode* node)
{
    void* meta = GameFunction<void*(__thiscall*)(CMapListNode*)>(0x005480C0)(node);
    return GameFunction<void*(__thiscall*)(void*)>(0x005902E0)(meta);
}
void DestroyList(CMapListTemp& temp)
{
    GameFunction<void(__thiscall*)(CMapListTemp*)>(0x00591C60)(&temp);
}

} // namespace


// 0x0059CB10
void CParkManager::RebuildPark(uint32_t how)
{
    GameFunction<void(__thiscall*)(CParkManager*)>(0x00596E50)(this);
    if (!(Flags() & 5) && FloorBuilt())
        GameFunction<void(__thiscall*)(CParkManager*)>(0x00596980)(this);
    GameFunction<void(__thiscall*)(CParkManager*, int)>(0x0059B020)(this, 0);
    ClearStats();
    if (how == kNewPark || !(Flags() & 2)) {
        // An empty grid.
        for (int x = 0; x < kGridSize; ++x) {
            for (int z = 0; z < kGridSize; ++z) {
                uint8_t* cell = Bytes() + 0x352C + (x * kGridSize + z) * 0x10;
                *reinterpret_cast<uint32_t*>(cell) = 0;
                *reinterpret_cast<uint32_t*>(cell + 4) = 0;
                cell[8] = 0;
                *reinterpret_cast<uint32_t*>(cell + 0xC) = 0;
            }
        }
        GameFunction<void(__thiscall*)(CParkManager*)>(0x00596980)(this);
        if (At<uint16_t>(0x13C96) == kParkInfoVersion) {
            const char* unnamed = reinterpret_cast<const char*>(0x0064F32C); // "unnamed park"
            std::memcpy(Bytes() + 0x13CAC, unnamed, std::strlen(unnamed) + 1);
            std::memcpy(Bytes() + 0x1772C, unnamed, std::strlen(unnamed) + 1);
        }
        Flags() &= ~8u;
        how = kNewPark;
    } else {
        GameFunction<void(__thiscall*)(CParkManager*)>(0x0059B750)(this); // rebuilt from the saved map
    }
    SceneUpdates() = false;
    GameFunction<void(__thiscall*)(CParkManager*, GridDims)>(0x0059BED0)(this, ParkDims());
    RebuildFloorAndPieces(this, nullptr, 0);
    SceneUpdates() = true;
    RebuildScene(Generator());
    FloorBuilt() = true;
    if (how == kNewPark)
        Flags() |= 1;
    SceneUpdates() = false;
    GameFunction<void(__thiscall*)(void*)>(0x004F6650)(Engine());
    SceneUpdates() = true;
    RebuildScene(Generator());
}

// 0x0059CC70
bool CParkManager::RebuildArea(const GridDims* area, bool checkMemory)
{
    RebuildFloorAndPieces(this, area, 0);
    if (!checkMemory)
        return true;
    MemoryStats stats;
    GetMemoryStats(&stats);
    // The park editor's singleton (a reference taken and dropped): is the park over its budget?
    void* editor = GameFunction<void*(__cdecl*)(bool)>(0x00590160)(false);
    bool full = GameFunction<bool(__thiscall*)(void*)>(0x0058DDC0)(editor);
    GameFunction<void(__cdecl*)()>(0x0058D290)();
    return !full;
}

// 0x0059CD20
bool CParkManager::DestroyMetasInAreaAndRebuild(GridDims area, bool skipRestarts, bool onlyWithin, bool skipLocked)
{
    bool destroyed = false;
    CMapListTemp metas = {GetMetasWithin(this, area)};
    if (metas.list) {
        for (CMapListNode* node = metas.list; node; node = node->mp_next) {
            void* meta = NodeMeta(node);
            if (MetaFlags(meta) & kMetaFloor)
                continue;
            if (skipRestarts && GameFunction<bool(__thiscall*)(void*)>(0x005902F0)(meta)) // a restart point
                continue;
            if (onlyWithin && !GameFunction<bool(__thiscall*)(void*, const GridDims*)>(0x00590750)(meta, &area))
                continue;
            if (skipLocked && (MetaFlags(meta) & kMetaLocked))
                continue;
            CallDestroyConcreteMeta(this, meta, 1);
            destroyed = true;
        }
        // The area's corner and size in the world (0x00595A90), and the scene updated over it.
        float size[3];
        float corner[4]; // (a whole Mth::Vector: w = 1 is written too)
        const float* position = GameFunction<const float*(__thiscall*)(CParkManager*, float*, const GridDims*, float*)>(
            0x00595A90)(Instance(), corner, &area, size);
        float x0 = position[0];
        float z0 = position[2];
        float z1 = static_cast<float>(static_cast<double>(size[2]) + z0);
        float x1 = static_cast<float>(static_cast<double>(size[0]) + x0);
        GameFunction<void(__thiscall*)(void*, float, float, float, float, int)>(0x004F74B0)(Engine(), x0, z0, x1, z1, 1);
        UpdatePieces(Generator());
        if (destroyed) {
            RebuildFloorAndPieces(this, &area, 0);
            DestroyList(metas);
            return true;
        }
    }
    DestroyList(metas);
    return false;
}

// 0x0059CEC0
void CParkManager::ClearArea(GridDims area)
{
    CMapListTemp metas = {GetMetasWithin(this, area)};
    for (CMapListNode* node = metas.list; node; node = node->mp_next) {
        void* meta = NodeMeta(node);
        if (MetaFlags(meta) & kMetaFloor)
            continue;
        GridDims at = MetaArea(meta);
        GameFunction<void(__thiscall*)(CParkManager*, void*, int, int, int)>(0x0059B090)(this, meta, 0, -at.y, 0);
        for (int x = at.x; x < at.x + at.w; ++x) {
            for (int z = at.z; z < at.z + at.l; ++z)
                CellHeight(x, z) = 0;
        }
    }
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z)
            CellHeight(x, z) = 0;
    }
    Flags() &= ~5u;
    RebuildFloorAndPieces(this, &area, 0);
    DestroyList(metas);
}

// 0x0059D080
bool CParkManager::ChangeFloorHeight(GridDims area, int dir)
{
    // The area's highest floor, and whether it's all at one height (0x005959D0).
    char level = 0;
    int height = GameFunction<int(__thiscall*)(CParkManager*, GridDims, char*)>(0x005959D0)(this, area, &level);
    bool changed = GameFunction<bool(__thiscall*)(CParkManager*, GridDims, int, int, bool, bool)>(0x0059C520)(
        this, area, area.y, height, dir > 0, level == 0);
    if (!changed)
        return false;
    if (RebuildArea(&area, true)) {
        GameFunction<void(__thiscall*)(CParkManager*, int, int)>(0x00595CB0)(this, 0, 0);
        return true;
    }
    GameFunction<void(__thiscall*)(CParkManager*)>(0x0059B280)(this); // undone
    RebuildArea(&area, false);
    return false;
}

// 0x0059C900
void __cdecl CCursor::Destroy()
{
    CCursor* cursor = Instance();
    if (!cursor)
        return;
    if (cursor->Meta()) {
        MetaFlags(cursor->Meta()) &= ~kMetaInCursor;
        CallDestroyConcreteMeta(cursor->Manager(), cursor->Meta(), 0);
    }
    GameFunction<void(__thiscall*)(CCursor*)>(0x005988A0)(cursor); // ~CCursor
    Mem::Manager::Get()->Free(cursor);
    Instance() = nullptr;
}

// 0x0059C950
void CCursor::ChangePieceInSet(int dir)
{
    auto* set = GameFunction<PieceSet*(__thiscall*)(CParkManager*, int, int*)>(0x00595210)(Manager(), SetIndex(),
                                                                                         &MenuSetNumber());
    int selected = set->m_selected + dir;
    if (selected < 0)
        selected = set->m_numEntries - 1;
    else if (selected >= set->m_numEntries)
        selected = 0;
    set->m_selected = selected;
    if (set->m_isClipboard)
        GameFunction<void(__thiscall*)(CCursor*, int)>(0x00595450)(this, selected); // select that clipboard
    else
        GameFunction<void(__thiscall*)(CCursor*, uint32_t)>(0x0059BC00)(this, set->m_entries[selected].checksum);
}

// 0x0059C9B0
void CCursor::ChangeSet(int dir)
{
    // The number of visible sets before the current one.
    auto countVisible = [this]() {
        int visible = 0;
        for (int i = 0; i < SetIndex(); ++i) {
            if (Sets(Manager())[i].m_visible)
                ++visible;
        }
        MenuSetNumber() = visible;
    };
    for (int tries = Manager()->NumSets(); tries > 0; --tries) {
        SetIndex() += dir;
        if (SetIndex() < 0)
            SetIndex() = Manager()->NumSets() - 1;
        if (SetIndex() >= Manager()->NumSets())
            SetIndex() = 0;
        countVisible();
        if (Sets(Manager())[SetIndex()].m_visible)
            break;
    }
    countVisible();
    PieceSet& set = Sets(Manager())[SetIndex()];
    if (!set.m_isClipboard) {
        GameFunction<void(__thiscall*)(CCursor*, uint32_t)>(0x0059BC00)(this, set.m_entries[set.m_selected].checksum);
        return;
    }
    int selected = set.m_selected;
    for (uint8_t* clipboard = Clipboards(); clipboard; clipboard = *reinterpret_cast<uint8_t**>(clipboard + 0x14))
        GameFunction<void(__thiscall*)(uint8_t*)>(0x0058C200)(clipboard);
    uint8_t* clipboard = Clipboards();
    for (int i = 0; i < selected && clipboard; ++i)
        clipboard = *reinterpret_cast<uint8_t**>(clipboard + 0x14);
    CurrentClipboard() = clipboard;
    if (Meta()) {
        MetaFlags(Meta()) &= ~kMetaInCursor;
        CallDestroyConcreteMeta(Manager(), Meta(), 0);
        UpdatePieces(Manager()->Generator());
        Meta() = nullptr;
    }
    if (Mode() != 5) {
        Mode() = 5;
        GameFunction<void(__thiscall*)(CCursor*)>(0x005954D0)(this);
    }
}

} // namespace Ed

REPLACE(0x0059C900, Ed::CCursor::Destroy);
REPLACE(0x0059C950, &Ed::CCursor::ChangePieceInSet);
REPLACE(0x0059C9B0, &Ed::CCursor::ChangeSet);
REPLACE(0x0059CB10, &Ed::CParkManager::RebuildPark);
REPLACE(0x0059CC70, &Ed::CParkManager::RebuildArea);
REPLACE(0x0059CD20, &Ed::CParkManager::DestroyMetasInAreaAndRebuild);
REPLACE(0x0059CEC0, &Ed::CParkManager::ClearArea);
REPLACE(0x0059D080, static_cast<bool (Ed::CParkManager::*)(Ed::GridDims, int)>(&Ed::CParkManager::ChangeFloorHeight));
