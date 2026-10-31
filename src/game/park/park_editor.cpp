// The park editor's manager and cursor, 0x0059C900-0x0059E0F7 (see park_editor.h).

#include "game/park/park_editor.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/park_editing/park_editor.h"
#include "game/park/park_memory.h"
#include "game/park_editing/rail_editor.h"

namespace ParkEditing {

namespace {

constexpr uint32_t kNewPark = 0x1A4E0EF9;
constexpr uint16_t kParkInfoVersion = 0x4E24;
constexpr uint32_t kMetaFloor = 0x8;       // a meta piece's flag: part of the floor
constexpr uint32_t kMetaLocked = 0x100;    // (not destroyed by an area's deletion when asked)
constexpr uint32_t kMetaInCursor = 0x200;  // held by the cursor

uint32_t& MetaFlags(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0xC); }
SCellBox& MetaArea(void* meta) { return *reinterpret_cast<SCellBox*>(static_cast<uint8_t*>(meta) + 0x4); }

// The scene is rebuilt while this is set (0x0067F398; cleared around the floor's rebuild).
bool& SceneUpdates() { return g_collisionAllowed; }

void CallDestroyConcreteMeta(CParkLayout* manager, void* meta, int flags)
{
    manager->FreePlacedPiece(static_cast<CPieceGroup*>(meta), static_cast<uint32_t>(flags));
}
void RebuildFloorAndPieces(CParkLayout* manager, const SCellBox* area, int flags)
{
    manager->RegenerateGround(const_cast<SCellBox*>(area), flags);
}
CMapListNode* GetMetasWithin(CParkLayout* manager, SCellBox area) { return manager->CollectPiecesInRegion(area); }
// The generator's update of the pieces it builds and its rebuild of the scene.
void UpdatePieces(void* generator) { static_cast<CParkAssembler*>(generator)->CleanUpOutRefs(); }
void RebuildScene(void* generator) { static_cast<CParkAssembler*>(generator)->RebuildCollision(); }
// The engine's scene (0x004F4970).
ParkEditing::CRailAuthoring* Engine() { return ParkEditing::CRailAuthoring::Sole(); }

// A meta piece of a list node (0x005480C0: the node's +4; then the concrete piece - the same pointer).
void* NodeMeta(CMapListNode* node)
{
    void* meta = node->GetPieceGroup();
    return static_cast<CPieceGroup*>(meta)->Cast();
}
void FreeNodeChain(CCellChainScratch& temp) { temp.Teardown(); }

} // namespace

// 0x0059CB10
void CParkLayout::RebuildPark(uint32_t how)
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
            const char* unnamed = "unnamed park"; // "unnamed park"
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
    Engine()->Reassemble();
    SceneUpdates() = true;
    RebuildScene(Generator());
}

// 0x0059CC70
bool CParkLayout::RebuildArea(const SCellBox* area, bool checkMemory)
{
    RebuildFloorAndPieces(this, area, 0);
    if (!checkMemory)
        return true;
    MemoryStats stats;
    GetMemoryStats(&stats);
    // The park editor's singleton (a reference taken and dropped): is the park over its budget?
    bool full = CParkBuilderMode::Fabricate(false)->IsParkFull() != 0;
    CParkBuilderMode::Release();
    return !full;
}

// 0x0059CD20
bool CParkLayout::DestroyMetasInAreaAndRebuild(SCellBox area, bool skipRestarts, bool onlyWithin, bool skipLocked)
{
    bool destroyed = false;
    CCellChainScratch metas = {GetMetasWithin(this, area)};
    if (metas.list) {
        for (CMapListNode* node = metas.list; node; node = node->m_following) {
            void* meta = NodeMeta(node);
            if (MetaFlags(meta) & kMetaFloor)
                continue;
            if (skipRestarts && static_cast<CPieceGroup*>(meta)->IsSpawnMarker())
                continue;
            if (onlyWithin && !static_cast<CPieceGroup*>(meta)->FitsInsideRegion(&area))
                continue;
            if (skipLocked && (MetaFlags(meta) & kMetaLocked))
                continue;
            CallDestroyConcreteMeta(this, meta, 1);
            destroyed = true;
        }
        // The area's corner and size in the world (0x00595A90), and the scene updated over it.
        Maths::Vec4 size; // (x, y and z written)
        Maths::Vec4 corner = Sole()->CellToWorldPos(area, &size);
        float x0 = corner.x;
        float z0 = corner.z;
        float z1 = static_cast<float>(static_cast<double>(size.z) + z0);
        float x1 = static_cast<float>(static_cast<double>(size.x) + x0);
        Engine()->DeletePoints(x0, z0, x1, z1, 1);
        UpdatePieces(Generator());
        if (destroyed) {
            RebuildFloorAndPieces(this, &area, 0);
            FreeNodeChain(metas);
            return true;
        }
    }
    FreeNodeChain(metas);
    return false;
}

// 0x0059CEC0
void CParkLayout::ClearArea(SCellBox area)
{
    CCellChainScratch metas = {GetMetasWithin(this, area)};
    for (CMapListNode* node = metas.list; node; node = node->m_following) {
        void* meta = NodeMeta(node);
        if (MetaFlags(meta) & kMetaFloor)
            continue;
        SCellBox at = MetaArea(meta);
        ShiftPlacedPiece(static_cast<CPieceGroup*>(meta), 0, -at.y, 0);
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
    FreeNodeChain(metas);
}

// 0x0059D080
bool CParkLayout::RaiseOrLowerFloor(SCellBox area, int dir)
{
    // The area's highest floor, and whether it's all at one height.
    bool level = false;
    int height = GetGroundLevel(area, &level);
    bool changed = RaiseOrLowerFloor(area, area.y, height, dir > 0, !level);
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
void __cdecl CBuildPointer::Teardown()
{
    CBuildPointer* cursor = Sole();
    if (!cursor)
        return;
    if (cursor->Meta()) {
        MetaFlags(cursor->Meta()) &= ~kMetaInCursor;
        CallDestroyConcreteMeta(cursor->Hub(), cursor->Meta(), 0);
    }
    cursor->DestroyClipboards(); // ~CBuildPointer
    MemSys::Hub::Pull()->Dealloc(cursor);
    Sole() = nullptr;
}

// 0x0059C950
void CBuildPointer::StepPieceChoice(int dir)
{
    PieceSet* set = Hub()->GetPieceFamily(SetIndex(), &MenuSetNumber());
    int selected = set->m_selected + dir;
    if (selected < 0)
        selected = set->m_entryTotal - 1;
    else if (selected >= set->m_entryTotal)
        selected = 0;
    set->m_selected = selected;
    if (set->m_isClipboard)
        SelectClipboard(selected);
    else
        ChangePiece(set->m_entries[selected].checksum);
}

// 0x0059C9B0
void CBuildPointer::StepPieceFamily(int dir)
{
    // The number of visible sets before the current one.
    auto countVisible = [this]() {
        int visible = 0;
        for (int i = 0; i < SetIndex(); ++i) {
            if (Sets(Hub())[i].m_isDisplayed)
                ++visible;
        }
        MenuSetNumber() = visible;
    };
    for (int tries = Hub()->NumSets(); tries > 0; --tries) {
        SetIndex() += dir;
        if (SetIndex() < 0)
            SetIndex() = Hub()->NumSets() - 1;
        if (SetIndex() >= Hub()->NumSets())
            SetIndex() = 0;
        countVisible();
        if (Sets(Hub())[SetIndex()].m_isDisplayed)
            break;
    }
    countVisible();
    PieceSet& set = Sets(Hub())[SetIndex()];
    if (!set.m_isClipboard) {
        ChangePiece(set.m_entries[set.m_selected].checksum);
        return;
    }
    int selected = set.m_selected;
    for (uint8_t* clipboard = Clipboards(); clipboard; clipboard = *reinterpret_cast<uint8_t**>(clipboard + 0x14))
        reinterpret_cast<CClipboard*>(clipboard)->FreeAllPieceGroups();
    uint8_t* clipboard = Clipboards();
    for (int i = 0; i < selected && clipboard; ++i)
        clipboard = *reinterpret_cast<uint8_t**>(clipboard + 0x14);
    CurrentClipboard() = clipboard;
    if (Meta()) {
        MetaFlags(Meta()) &= ~kMetaInCursor;
        CallDestroyConcreteMeta(Hub(), Meta(), 0);
        UpdatePieces(Hub()->Generator());
        Meta() = nullptr;
    }
    if (Mode() != 5) {
        Mode() = 5;
        OnModeChanged();
    }
}

} // namespace Ed
