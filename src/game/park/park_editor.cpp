// The park editor's manager and cursor, 0x0059C900-0x0059E0F7 (see park_editor.h).

#include "game/park/park_editor.h"
#include "hook/game_code.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/ed/park_editor.h"
#include "game/park/park_memory.h"
#include "game/ed/rail_editor.h"
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
bool& SceneUpdates() { return g_collisionAllowed; }

void CallDestroyConcreteMeta(CParkManager* manager, void* meta, int flags)
{
    manager->DestroyConcreteMeta(static_cast<CMetaPiece*>(meta), static_cast<uint32_t>(flags));
}
void RebuildFloorAndPieces(CParkManager* manager, const GridDims* area, int flags)
{
    manager->RebuildFloor(const_cast<GridDims*>(area), flags);
}
CMapListNode* GetMetasWithin(CParkManager* manager, GridDims area) { return manager->GetAreaMetaPieces(area); }
// The generator's update of the pieces it builds and its rebuild of the scene.
void UpdatePieces(void* generator) { static_cast<CParkGenerator*>(generator)->CleanUpOutRefs(); }
void RebuildScene(void* generator) { static_cast<CParkGenerator*>(generator)->RebuildCollision(); }
// The engine's scene (0x004F4970).
Ed::CRailEditorComponent* Engine() { return Ed::CRailEditorComponent::Instance(); }

// A meta piece of a list node (0x005480C0: the node's +4; then the concrete piece - the same pointer).
void* NodeMeta(CMapListNode* node)
{
    void* meta = node->GetMeta();
    return static_cast<CMetaPiece*>(meta)->Cast();
}
void DestroyList(CMapListTemp& temp) { temp.Destroy(); }

} // namespace


// 0x0059CB10
void CParkManager::RebuildPark(uint32_t how)
{
    SetUpLights();
    if (!(Flags() & 5) && FloorBuilt())
        WriteCompressedMap();
    DestroyConcreteMetas(0);
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
        WriteCompressedMap();
        if (At<uint16_t>(0x13C96) == kParkInfoVersion) {
            const char* unnamed = GAME_TEXT(0x0064F32C, "unnamed park"); // "unnamed park"
            std::memcpy(Bytes() + 0x13CAC, unnamed, std::strlen(unnamed) + 1);
            std::memcpy(Bytes() + 0x1772C, unnamed, std::strlen(unnamed) + 1);
        }
        Flags() &= ~8u;
        how = kNewPark;
    } else {
        ReadFromCompressedMap(); // rebuilt from the saved map
    }
    SceneUpdates() = false;
    RebuildInnerShell(ParkDims());
    RebuildFloorAndPieces(this, nullptr, 0);
    SceneUpdates() = true;
    RebuildScene(Generator());
    FloorBuilt() = true;
    if (how == kNewPark)
        Flags() |= 1;
    SceneUpdates() = false;
    Engine()->Rebuild();
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
    bool full = CParkEditor::Create(false)->IsParkFull() != 0;
    CParkEditor::Release();
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
            if (skipRestarts && static_cast<CMetaPiece*>(meta)->IsRestartPiece())
                continue;
            if (onlyWithin && !static_cast<CMetaPiece*>(meta)->IsCompletelyWithinArea(&area))
                continue;
            if (skipLocked && (MetaFlags(meta) & kMetaLocked))
                continue;
            CallDestroyConcreteMeta(this, meta, 1);
            destroyed = true;
        }
        // The area's corner and size in the world (0x00595A90), and the scene updated over it.
        Mth::Vector size; // (x, y and z written)
        Mth::Vector corner = Instance()->GridCoordinatesToWorld(area, &size);
        float x0 = corner.x;
        float z0 = corner.z;
        float z1 = static_cast<float>(static_cast<double>(size.z) + z0);
        float x1 = static_cast<float>(static_cast<double>(size.x) + x0);
        Engine()->DeletePoints(x0, z0, x1, z1, 1);
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
        RelocateMetaPiece(static_cast<CMetaPiece*>(meta), 0, -at.y, 0);
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
    // The area's highest floor, and whether it's all at one height.
    bool level = false;
    int height = GetFloorHeight(area, &level);
    bool changed = ChangeFloorHeight(area, area.y, height, dir > 0, !level);
    if (!changed)
        return false;
    if (RebuildArea(&area, true)) {
        HighlightGapPieces(false, nullptr);
        return true;
    }
    UndoFloorChange(); // undone
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
    cursor->DestroyClipboards(); // ~CCursor
    Mem::Manager::Get()->Free(cursor);
    Instance() = nullptr;
}

// 0x0059C950
void CCursor::ChangePieceInSet(int dir)
{
    PieceSet* set = Manager()->GetPieceSet(SetIndex(), &MenuSetNumber());
    int selected = set->m_selected + dir;
    if (selected < 0)
        selected = set->m_numEntries - 1;
    else if (selected >= set->m_numEntries)
        selected = 0;
    set->m_selected = selected;
    if (set->m_isClipboard)
        SelectClipboard(selected);
    else
        ChangePiece(set->m_entries[selected].checksum);
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
        ChangePiece(set.m_entries[set.m_selected].checksum);
        return;
    }
    int selected = set.m_selected;
    for (uint8_t* clipboard = Clipboards(); clipboard; clipboard = *reinterpret_cast<uint8_t**>(clipboard + 0x14))
        reinterpret_cast<CClipboard*>(clipboard)->DestroyMetas();
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
        OnModeChanged();
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
