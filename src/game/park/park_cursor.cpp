// The park editor's cursor: placing pieces and restart points, gaps, area selection, raising and lowering the
// floor; and the park manager's set-up (0x0059D120-0x0059E0F7, see park_editor.h).

#include "game/park/park_editor.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/ed/park_editor.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"

namespace ParkEditing {

namespace {

constexpr uint32_t kTopDownLevel = 0xCD3682C1;  // the level whose park lives in the top-down heap
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
constexpr uint32_t kClipboardSet = 0x45C6C158; // a piece set's flag: its entries are the clipboards
constexpr uint32_t kMetaHasGap = 0x1000;       // a meta piece's flag

uint32_t& MetaFlags(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0xC); }
uint32_t MetaType(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0x38); }
SCellBox& MetaArea(void* meta) { return *reinterpret_cast<SCellBox*>(static_cast<uint8_t*>(meta) + 0x4); }

// The memory profiler's markers (compiled out of this build: the empty function 0x005480A0).
inline void MemoryMarker(const char* /*what*/) {}

void* NodeMeta(CMapListNode* node)
{
    void* meta = node->GetPieceGroup();
    return static_cast<CPieceGroup*>(meta)->Cast();
}
CPieceGroup* AsMeta(void* meta) { return static_cast<CPieceGroup*>(meta); }
CClipboard* AsClipboard(uint8_t* clipboard) { return reinterpret_cast<CClipboard*>(clipboard); }
CParkAssembler* AsGenerator(void* generator) { return static_cast<CParkAssembler*>(generator); }
uint8_t* NextClipboard(uint8_t* clipboard) { return *reinterpret_cast<uint8_t**>(clipboard + 0x14); }
// A clipboard's area where the cursor would paste it.
void ClipboardArea(uint8_t* clipboard, CBuildPointer* cursor, SCellBox* area) { AsClipboard(clipboard)->FetchExtent(cursor, area); }
// The gap with a half at `area` (and which half), or null.
GapRecord* GapAt(CParkLayout* manager, SCellBox* area, int* half) { return manager->GetGapInfo(area, half); }
void DropGapEntry(CParkLayout* manager, GapRecord* gap) { manager->DropGapEntry(gap); }
bool RegisterGapEntry(CParkLayout* manager, GapRecord* gap) { return manager->RegisterGapEntry(gap); }
// The metas within an area (the flags: 0, 0, 1) - the caller owns the list.
CMapListNode* MetasWithin(CParkLayout* manager, SCellBox area)
{
    return manager->GetIntersectingMetas(area, 0, nullptr, 1);
}
// The area selected.
void SelectedArea(CBuildPointer* cursor, SCellBox* area) { cursor->GetMarqueeBounds(area); }
int FloorHeight(CParkLayout* manager, SCellBox area, char* level)
{
    return manager->GetGroundLevel(area, reinterpret_cast<bool*>(level));
}
CParkBuilderMode* ParkEditor() { return CParkBuilderMode::Fabricate(false); }
bool ParkFull(CParkBuilderMode* editor) { return editor->IsParkFull() != 0; }
void ReleaseParkEditor() { CParkBuilderMode::Release(); }
// (0x005BA1D0: `xor eax, eax; ret` - compiled out of this build, folded with Stubs::ReturnZero)
void SetDefaultHeap(MemSys::Arena* /*heap*/) { Stubs::ReturnZero(); }

} // namespace

// 0x0059D120
void CBuildPointer::RemoveClipboardsTooBig()
{
    uint8_t* previous = nullptr;
    uint8_t* clipboard = Clipboards();
    while (clipboard) {
        uint8_t* next = NextClipboard(clipboard);
        SCellBox area = {};
        ClipboardArea(clipboard, this, &area);
        uint8_t parkWidth = Hub()->ParkDims().w;
        uint8_t parkLength = Hub()->ParkDims().l;
        if (area.w > parkWidth || area.w > parkLength || area.l > parkWidth || area.l > parkLength) {
            // Too big to paste in this park: unlinked and deleted.
            if (previous)
                *reinterpret_cast<uint8_t**>(previous + 0x14) = next;
            if (clipboard == Clipboards())
                Clipboards() = next;
            AsClipboard(clipboard)->Teardown();
            ClipboardPools().Dealloc(clipboard);
        } else {
            previous = clipboard;
        }
        clipboard = next;
    }
    CurrentClipboard() = Clipboards();
    StepPieceChoice(0);
}

// 0x0059D240
bool CBuildPointer::PlacePiece()
{
    CParkLayout* manager = Hub();
    if (!manager->CanChangeFloorHere(Area(), false))
        return false;
    if (Area().h + FloorHeight(manager, Area(), nullptr) > 17)
        return false;
    // A restart point needs a free slot of its type.
    uint32_t type = MetaType(Meta());
    for (int slot = 1; slot < 9; ++slot) {
        if (type == manager->At<uint32_t>(0x177A4 + (slot - 1) * 4)) {
            if (!AsGenerator(manager->Generator())->HasFreeRestartSlot(slot)) {
                DebugOut::Printf("No free slots exist for this type of restart point\n"); // "No free slots exist for this type of restart point\n"
                return false;
            }
            break;
        }
    }
    SetDefaultHeap(GetMainHeap());
    CPieceGroup* source = manager->GetAbstractMetaPiece(MetaType(Meta()), nullptr);
    CPieceGroup* piece = manager->MakePlacedPiece(source, 0);
    piece->SetQuarterTurns(Rotation());
    manager->PlaceMetaPieceInPark(Area(), piece);
    SetDefaultHeap(GetMainHeap());
    manager->RebuildArea(&Area(), false);
    MemoryMarker("After placing a piece"); // "After placing a piece"
    return true;
}

// 0x0059D380
bool CBuildPointer::DestroyUnderCursor()
{
    SCellBox area = {};
    if (Mode() == 5) {
        if (!CurrentClipboard())
            return false;
        ClipboardArea(CurrentClipboard(), this, &area);
    } else {
        area = Area();
    }
    return Hub()->DestroyMetasInAreaAndRebuild(area, false, false, true);
}

// 0x0059D410
bool CBuildPointer::AddGapHalf()
{
    CParkBuilderMode* editor = ParkEditor();
    if (ParkFull(editor)) {
        ReleaseParkEditor();
        return false;
    }
    CCellChainScratch metas = {MetasWithin(Hub(), Area())};
    bool added = false;
    if (metas.list) {
        void* meta = NodeMeta(metas.list);
        Area().DumpToLog();
        int half;
        if (!GapAt(Hub(), &Area(), &half) && meta) {
            GapRecord& gap = Gap();
            if (gap.m_numCompleteHalves == 1)
                DropGapEntry(Hub(), &gap);
            else
                gap.m_numCompleteHalves = 0;
            int n = gap.m_numCompleteHalves;
            gap.m_atPoint[n] = MetaArea(meta);
            gap.m_quarterTurns[n] = 0;
            gap.m_leftExtent[n] = 0;
            gap.m_rightExtent[n] = 0;
            gap.m_pointsHeld = 100;
            gap.m_numCompleteHalves = n + 1;
            gap.m_tabIndex = -1;
            if (n == 0) {
                // The gap's name: "gap", then "gap2", "gap3"...
                if (GapCounter() > 1)
#pragma warning(suppress : 4996)
                    std::sprintf(gap.m_text, "gap%d", GapCounter()); // "gap%d"
                else
                    std::memcpy(gap.m_text, "gap", 4);
                ++GapCounter();
            }
            if (RegisterGapEntry(Hub(), &gap)) {
                Hub()->RegenerateGround(&Area(), 0);
                if (gap.m_numCompleteHalves > 0)
                    Hub()->HighlightGapPieces(true, &gap);
                added = true;
            } else {
                gap.m_numCompleteHalves = 0;
            }
        }
    }
    metas.Teardown();
    ReleaseParkEditor();
    return added;
}

// 0x0059D600
bool CBuildPointer::RemoveGapHalf()
{
    CParkLayout* manager = Hub();
    if (Gap().m_numCompleteHalves == 1) {
        DropGapEntry(manager, &Gap());
        Gap().m_numCompleteHalves = 0;
        Hub()->RebuildArea(&Area(), false);
        return true;
    }
    int half;
    GapRecord* gap = GapAt(manager, &Area(), &half);
    if (!gap)
        return false;
    DropGapEntry(Hub(), gap);
    Hub()->RebuildArea(&Area(), false);
    // The pieces that had gaps are told to look again.
    manager = Hub();
    for (auto* node = manager->At<CMapListNode*>(0x7C); node; node = node->m_following) {
        if (MetaFlags(NodeMeta(node)) & kMetaHasGap)
            AsMeta(NodeMeta(node))->Spotlight(1, 1);
    }
    AsGenerator(manager->Generator())->SetPiecesHighlighted(0);
    return true;
}

// 0x0059D6B0
bool CBuildPointer::ChangeGap(int turn, int left, int right)
{
    int half;
    GapRecord* gap = GapAt(Hub(), &Area(), &half);
    if (!gap)
        return false;
    // A gap being made: only its own first half.
    if (Gap().m_numCompleteHalves == 1 &&
        (gap->m_atPoint[0].x != Gap().m_atPoint[0].x || gap->m_atPoint[0].y != Gap().m_atPoint[0].y || gap->m_atPoint[0].z != Gap().m_atPoint[0].z))
        return false;
    GapRecord copy = *gap;
    bool changed = false;
    if (turn) {
        copy.m_quarterTurns[half] -= turn;
        copy.m_leftExtent[half] = 0;
        copy.m_rightExtent[half] = 0;
        changed = true;
    }
    if (left) {
        copy.m_leftExtent[half] += left;
        if (copy.m_leftExtent[half] < 0)
            copy.m_leftExtent[half] = 0;
        else if (copy.m_leftExtent[half] > 6)
            copy.m_leftExtent[half] = 6;
        changed = true;
    }
    if (right) {
        copy.m_rightExtent[half] += right;
        if (copy.m_rightExtent[half] < 0)
            copy.m_rightExtent[half] = 0;
        if (copy.m_rightExtent[half] > 6)
            copy.m_rightExtent[half] = 6;
    } else if (!changed) {
        return true;
    }
    DropGapEntry(Hub(), gap);
    copy.m_quarterTurns[half] &= 3;
    SCellBox park = Hub()->ParkDims();
    const SCellBox& at = copy.m_atPoint[half];
    int* m_leftExtent = &copy.m_leftExtent[half];
    int* m_rightExtent = &copy.m_rightExtent[half];
    // Each extent is kept inside the park: the one behind the gap's start, the other past its end.
    switch (copy.m_quarterTurns[half]) {
    case 0:
    case 2: {
        int* behind = copy.m_quarterTurns[half] == 0 ? m_rightExtent : m_leftExtent;
        int* ahead = copy.m_quarterTurns[half] == 0 ? m_leftExtent : m_rightExtent;
        if (at.z - *behind < park.z)
            --*behind;
        if (at.z + at.l + *ahead > park.z + park.l)
            --*ahead;
        break;
    }
    case 1:
    case 3: {
        int* behind = copy.m_quarterTurns[half] == 1 ? m_rightExtent : m_leftExtent;
        int* ahead = copy.m_quarterTurns[half] == 1 ? m_leftExtent : m_rightExtent;
        if (at.x - *behind < park.x)
            --*behind;
        if (at.x + at.w + *ahead > park.x + park.w)
            --*ahead;
        break;
    }
    }
    RegisterGapEntry(Hub(), &copy);
    Hub()->RebuildArea(&park, false);
    if (Gap().m_numCompleteHalves == 1)
        Gap() = copy;
    return true;
}

// 0x0059D8E0
bool CBuildPointer::DestroySelection()
{
    if (!Selecting())
        return false;
    SCellBox area = {};
    SelectedArea(this, &area);
    bool destroyed = Hub()->DestroyMetasInAreaAndRebuild(area, true, true, false);
    MemoryMarker("After Area deletion"); // "After Area deletion"
    Selecting() = true;
    At<bool>(0x55) = false;
    return destroyed;
}

// 0x0059D980
void CBuildPointer::FlattenSelection()
{
    if (!Selecting())
        return;
    SCellBox area = {};
    SelectedArea(this, &area);
    Hub()->ClearArea(area);
    Selecting() = true;
    At<bool>(0x55) = false;
}

// 0x0059D9F0
bool CBuildPointer::RaiseArea(SCellBox area, int height, int dir, char level)
{
    if (area.w > 10 || area.l > 10)
        return false;
    if (!Hub()->RaiseOrLowerFloor(area, area.y, height, dir > 0, level == 0))
        return false;
    ShiftBy(0, 0);
    if (Hub()->RebuildArea(&area, true)) {
        Hub()->HighlightGapPieces(false, nullptr);
        return true;
    }
    Hub()->UndoFloorChange(); // undone
    Hub()->RebuildArea(&area, false);
    return false;
}

// 0x0059DAA0
bool CBuildPointer::RaiseLower(int dir)
{
    SCellBox area = Area();
    if (Mode() == 4) {
        if (Selecting())
            SelectedArea(this, &area);
    } else if (Mode() == 5) {
        if (!CurrentClipboard())
            return false;
        ClipboardArea(CurrentClipboard(), this, &area);
    }
    SCellBox park = CParkLayout::Sole()->ParkDims();
    if (area.w > park.w || area.l > park.l)
        return false;
    // (the original has the level flag written over the copy's first byte)
    char level = 0;
    int height = FloorHeight(Hub(), area, &level);
    if (height + dir <= -16)
        return false;
    int top = area.h + height + dir;
    // The pieces standing on the area (any height) widen it, and the highest sets the top.
    SCellBox above = {area.x, area.y, area.z, area.w, 16, area.l};
    CMapListNode* list = MetasWithin(CParkLayout::Sole(), above);
    for (CMapListNode* node = list; node; node = node->m_following) {
        void* meta = node->GetPieceGroup();
        SCellBox at = MetaArea(meta);
        if (at.h > area.h)
            top = at.h + height + dir;
        if (at.x < area.x)
            area.x = at.x;
        if (at.w > area.w)
            area.w = at.w;
        if (at.z < area.z)
            area.z = at.z;
        if (at.l > area.l)
            area.l = at.l;
    }
    CMapListNode::FreeNodeChain(list);
    if (top > 17)
        return false;
    if (Mode() == 5 && CurrentClipboard())
        AsClipboard(CurrentClipboard())->FreeAllPieceGroups();
    bool raised = RaiseArea(area, height, dir, level);
    if (Mode() == 4)
        At<bool>(0x55) = false;
    return raised;
}

// 0x0059DCD0
void CParkLayout::Commission(uint32_t level)
{
    if (At<bool>(0x4))
        return;
    At<bool>(0x4) = true;
    PrepareAll(level); // the park heap and pools
    for (int x = 0; x < kGridSize; ++x) {
        for (int z = 0; z < kGridSize; ++z) {
            uint8_t* cell = Bytes() + 0x352C + (x * kGridSize + z) * 0x10;
            cell[8] = 0;
            *reinterpret_cast<uint32_t*>(cell + 0xC) = 0;
        }
    }
    // The park's gap manager (0xA8 bytes), in the top-down heap for that level.
    MemSys::Hub* memory = MemSys::Hub::Pull();
    if (level == kTopDownLevel)
        memory->EnterHeapScope(memory->m_topDownHeap);
    void* block = MemSys::Alloc(0xA8);
    mp_gapManager = block ? static_cast<CGapRegistry*>(block)->MakeNew(this) : nullptr;
    if (level == kTopDownLevel)
        MemSys::Hub::Pull()->LeaveHeapScope();
    int theme = (Flags() & 2) ? At<uint16_t>(0x13C98) : 0;
    m_builder->LoadPieces(level, theme);
    SetUpAbstractMetas();
    SetUpRoadMask();
    SetDefaultBounds();
    RebuildPark(0);
    if (level == kParkEditorLevel)
        m_builder->BuildGoalNodeArray();

    // The palette's sets (Ed_piece_sets): each a run of the standard meta pieces (Ed_standard_metapieces) from its
    // `first` up to the next set's, or the clipboards.
    Scripts::CArray* sets = Scripts::FetchList("Ed_piece_sets"); // "Ed_piece_sets"
    NumSets() = static_cast<int>(sets->m_extent);
    Scripts::CArray* standard = Scripts::FetchList("Ed_standard_metapieces"); // "Ed_standard_metapieces"
    int end = static_cast<int>(standard->m_extent);
    for (int i = NumSets() - 1; i >= 0; --i) {
        PieceSet& set = Sets(this)[i];
        auto* description = reinterpret_cast<Scripts::CStruct*>(sets->FetchPart(static_cast<uint32_t>(i)));
        set.m_label = nullptr;
        description->FetchText("name", &set.m_label, 1); // "name"
        set.m_nameChecksum = Checksums::HashFromText(set.m_label);
        set.m_selected = 0;
        set.m_entryTotal = 0;
        uint32_t first = 0;
        if (description->ReadHashValue("first", &first, 0)) { // "first"
            // `first`'s place in the standard pieces (the manager's list runs from the last to the first).
            int index = 0;
            int position = At<int>(0x88) - 1;
            for (auto* node = At<CMapListNode*>(0x80); node; node = node->m_following, --position) {
                if (MetaType(NodeMeta(node)) == first) {
                    index = position;
                    break;
                }
            }
            set.m_entryTotal = end - index;
            end = index;
            for (int entry = 0; entry < set.m_entryTotal; ++entry) {
                int piece = index + entry;
                void* meta = nullptr;
                int at = At<int>(0x88) - 1;
                for (auto* node = At<CMapListNode*>(0x80); node; node = node->m_following, --at) {
                    void* candidate = NodeMeta(node);
                    if (at == piece) {
                        meta = candidate;
                        break;
                    }
                }
                set.m_entries[entry].checksum = MetaType(meta); // (no check for a missing piece, as in the original)
                auto* pieceDescription =
                    reinterpret_cast<Scripts::CStruct*>(standard->FetchPart(static_cast<uint32_t>(piece)));
                if (!pieceDescription->FetchText("text_name", // "text_name"
                                                 &set.m_entries[entry].name, 0))
                    set.m_entries[entry].name = nullptr;
            }
        }
        set.m_isClipboard = false;
        if (description->HasBareFlag(kClipboardSet)) {
            set.m_isClipboard = true;
            set.m_entryTotal = 6;
            const char* text = Scripts::FetchText(0x4E677220u);
            for (int k = 0; k < 6; ++k) {
                char name[100];
                std::snprintf(name, sizeof(name), "Clipboard%d", k); // "Clipboard%d"
                set.m_entries[k].name = text;
                set.m_entries[k].checksum = Checksums::HashFromText(name);
            }
        }
        set.m_isDisplayed = !description->HasBareFlag("hidden"); // "hidden"
    }
}

} // namespace Ed
