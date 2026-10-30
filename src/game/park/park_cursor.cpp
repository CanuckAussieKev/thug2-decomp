// The park editor's cursor: placing pieces and restart points, gaps, area selection, raising and lowering the
// floor; and the park manager's set-up (0x0059D120-0x0059E0F7, see park_editor.h).

#include "game/park/park_editor.h"
#include "hook/game_code.h"

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kTopDownLevel = 0xCD3682C1;  // the level whose park lives in the top-down heap
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
constexpr uint32_t kClipboardSet = 0x45C6C158; // a piece set's flag: its entries are the clipboards
constexpr uint32_t kMetaHasGap = 0x1000;       // a meta piece's flag

uint32_t& MetaFlags(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0xC); }
uint32_t MetaType(void* meta) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(meta) + 0x38); }
GridDims& MetaArea(void* meta) { return *reinterpret_cast<GridDims*>(static_cast<uint8_t*>(meta) + 0x4); }

// The memory profiler's markers (compiled out of this build: the empty function 0x005480A0).
inline void MemoryMarker(const char* /*what*/) {}

void* NodeMeta(CMapListNode* node)
{
    void* meta = node->GetMeta();
    return static_cast<CMetaPiece*>(meta)->Cast();
}
CMetaPiece* AsMeta(void* meta) { return static_cast<CMetaPiece*>(meta); }
CClipboard* AsClipboard(uint8_t* clipboard) { return reinterpret_cast<CClipboard*>(clipboard); }
CParkGenerator* AsGenerator(void* generator) { return static_cast<CParkGenerator*>(generator); }
uint8_t* NextClipboard(uint8_t* clipboard) { return *reinterpret_cast<uint8_t**>(clipboard + 0x14); }
// A clipboard's area where the cursor would paste it.
void ClipboardArea(uint8_t* clipboard, CCursor* cursor, GridDims* area) { AsClipboard(clipboard)->GetArea(cursor, area); }
// The gap with a half at `area` (and which half), or null.
GapDescriptor* GapAt(CParkManager* manager, GridDims* area, int* half) { return manager->GetGapInfo(area, half); }
void RemoveGap(CParkManager* manager, GapDescriptor* gap) { manager->RemoveGap(gap); }
bool AddGap(CParkManager* manager, GapDescriptor* gap) { return manager->AddGap(gap); }
// The metas within an area (the flags: 0, 0, 1) - the caller owns the list.
CMapListNode* MetasWithin(CParkManager* manager, GridDims area)
{
    return manager->GetIntersectingMetas(area, 0, nullptr, 1);
}
// The area selected.
void SelectedArea(CCursor* cursor, GridDims* area) { cursor->GetAreaSelectDims(area); }
int FloorHeight(CParkManager* manager, GridDims area, char* level)
{
    return manager->GetFloorHeight(area, reinterpret_cast<bool*>(level));
}
CParkEditor* ParkEditor() { return CParkEditor::Create(false); }
bool ParkFull(CParkEditor* editor) { return editor->IsParkFull() != 0; }
void ReleaseParkEditor() { CParkEditor::Release(); }
// (0x005BA1D0: `xor eax, eax; ret` - compiled out of this build, folded with Stubs::ReturnZero)
void SetDefaultHeap(Mem::Heap* /*heap*/) { Stubs::ReturnZero(); }

} // namespace

// 0x0059D120
void CCursor::RemoveClipboardsTooBig()
{
    uint8_t* previous = nullptr;
    uint8_t* clipboard = Clipboards();
    while (clipboard) {
        uint8_t* next = NextClipboard(clipboard);
        GridDims area = {};
        ClipboardArea(clipboard, this, &area);
        uint8_t parkWidth = Manager()->ParkDims().w;
        uint8_t parkLength = Manager()->ParkDims().l;
        if (area.w > parkWidth || area.w > parkLength || area.l > parkWidth || area.l > parkLength) {
            // Too big to paste in this park: unlinked and deleted.
            if (previous)
                *reinterpret_cast<uint8_t**>(previous + 0x14) = next;
            if (clipboard == Clipboards())
                Clipboards() = next;
            AsClipboard(clipboard)->Destroy();
            ClipboardPools().Free(clipboard);
        } else {
            previous = clipboard;
        }
        clipboard = next;
    }
    CurrentClipboard() = Clipboards();
    ChangePieceInSet(0);
}

// 0x0059D240
bool CCursor::PlacePiece()
{
    CParkManager* manager = Manager();
    if (!manager->CanChangeFloorHere(Area(), false))
        return false;
    if (Area().h + FloorHeight(manager, Area(), nullptr) > 17)
        return false;
    // A restart point needs a free slot of its type.
    uint32_t type = MetaType(Meta());
    for (int slot = 1; slot < 9; ++slot) {
        if (type == manager->At<uint32_t>(0x177A4 + (slot - 1) * 4)) {
            if (!AsGenerator(manager->Generator())->HasFreeRestartSlot(slot)) {
                Dbg::Printf(GAME_TEXT(0x0064F85C, "No free slots exist for this type of restart point\n")); // "No free slots exist for this type of restart point\n"
                return false;
            }
            break;
        }
    }
    SetDefaultHeap(GetMainHeap());
    CMetaPiece* source = manager->GetAbstractMetaPiece(MetaType(Meta()), nullptr);
    CMetaPiece* piece = manager->CreateConcreteMeta(source, 0);
    piece->SetRot(Rotation());
    manager->AddMetaPieceToPark(Area(), piece);
    SetDefaultHeap(GetMainHeap());
    manager->RebuildArea(&Area(), false);
    MemoryMarker(GAME_TEXT(0x0064F890, "After placing a piece")); // "After placing a piece"
    return true;
}

// 0x0059D380
bool CCursor::DestroyUnderCursor()
{
    GridDims area = {};
    if (Mode() == 5) {
        if (!CurrentClipboard())
            return false;
        ClipboardArea(CurrentClipboard(), this, &area);
    } else {
        area = Area();
    }
    return Manager()->DestroyMetasInAreaAndRebuild(area, false, false, true);
}

// 0x0059D410
bool CCursor::AddGapHalf()
{
    CParkEditor* editor = ParkEditor();
    if (ParkFull(editor)) {
        ReleaseParkEditor();
        return false;
    }
    CMapListTemp metas = {MetasWithin(Manager(), Area())};
    bool added = false;
    if (metas.list) {
        void* meta = NodeMeta(metas.list);
        Area().PrintContents();
        int half;
        if (!GapAt(Manager(), &Area(), &half) && meta) {
            GapDescriptor& gap = Gap();
            if (gap.m_numCompleteHalves == 1)
                RemoveGap(Manager(), &gap);
            else
                gap.m_numCompleteHalves = 0;
            int n = gap.m_numCompleteHalves;
            gap.m_pos[n] = MetaArea(meta);
            gap.m_rot[n] = 0;
            gap.m_leftExtent[n] = 0;
            gap.m_rightExtent[n] = 0;
            gap.m_score = 100;
            gap.m_numCompleteHalves = n + 1;
            gap.m_tabIndex = -1;
            if (n == 0) {
                // The gap's name: "gap", then "gap2", "gap3"...
                if (GapCounter() > 1)
#pragma warning(suppress : 4996)
                    std::sprintf(gap.m_text, GAME_TEXT(0x0064F8A8, "gap%d"), GapCounter()); // "gap%d"
                else
                    std::memcpy(gap.m_text, "gap", 4);
                ++GapCounter();
            }
            if (AddGap(Manager(), &gap)) {
                Manager()->RebuildFloor(&Area(), 0);
                if (gap.m_numCompleteHalves > 0)
                    Manager()->HighlightGapPieces(true, &gap);
                added = true;
            } else {
                gap.m_numCompleteHalves = 0;
            }
        }
    }
    metas.Destroy();
    ReleaseParkEditor();
    return added;
}

// 0x0059D600
bool CCursor::RemoveGapHalf()
{
    CParkManager* manager = Manager();
    if (Gap().m_numCompleteHalves == 1) {
        RemoveGap(manager, &Gap());
        Gap().m_numCompleteHalves = 0;
        Manager()->RebuildArea(&Area(), false);
        return true;
    }
    int half;
    GapDescriptor* gap = GapAt(manager, &Area(), &half);
    if (!gap)
        return false;
    RemoveGap(Manager(), gap);
    Manager()->RebuildArea(&Area(), false);
    // The pieces that had gaps are told to look again.
    manager = Manager();
    for (auto* node = manager->At<CMapListNode*>(0x7C); node; node = node->mp_next) {
        if (MetaFlags(NodeMeta(node)) & kMetaHasGap)
            AsMeta(NodeMeta(node))->Highlight(1, 1);
    }
    AsGenerator(manager->Generator())->SetPiecesHighlighted(0);
    return true;
}

// 0x0059D6B0
bool CCursor::ChangeGap(int turn, int left, int right)
{
    int half;
    GapDescriptor* gap = GapAt(Manager(), &Area(), &half);
    if (!gap)
        return false;
    // A gap being made: only its own first half.
    if (Gap().m_numCompleteHalves == 1 &&
        (gap->m_pos[0].x != Gap().m_pos[0].x || gap->m_pos[0].y != Gap().m_pos[0].y || gap->m_pos[0].z != Gap().m_pos[0].z))
        return false;
    GapDescriptor copy = *gap;
    bool changed = false;
    if (turn) {
        copy.m_rot[half] -= turn;
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
    RemoveGap(Manager(), gap);
    copy.m_rot[half] &= 3;
    GridDims park = Manager()->ParkDims();
    const GridDims& at = copy.m_pos[half];
    int* m_leftExtent = &copy.m_leftExtent[half];
    int* m_rightExtent = &copy.m_rightExtent[half];
    // Each extent is kept inside the park: the one behind the gap's start, the other past its end.
    switch (copy.m_rot[half]) {
    case 0:
    case 2: {
        int* behind = copy.m_rot[half] == 0 ? m_rightExtent : m_leftExtent;
        int* ahead = copy.m_rot[half] == 0 ? m_leftExtent : m_rightExtent;
        if (at.z - *behind < park.z)
            --*behind;
        if (at.z + at.l + *ahead > park.z + park.l)
            --*ahead;
        break;
    }
    case 1:
    case 3: {
        int* behind = copy.m_rot[half] == 1 ? m_rightExtent : m_leftExtent;
        int* ahead = copy.m_rot[half] == 1 ? m_leftExtent : m_rightExtent;
        if (at.x - *behind < park.x)
            --*behind;
        if (at.x + at.w + *ahead > park.x + park.w)
            --*ahead;
        break;
    }
    }
    AddGap(Manager(), &copy);
    Manager()->RebuildArea(&park, false);
    if (Gap().m_numCompleteHalves == 1)
        Gap() = copy;
    return true;
}

// 0x0059D8E0
bool CCursor::DestroySelection()
{
    if (!Selecting())
        return false;
    GridDims area = {};
    SelectedArea(this, &area);
    bool destroyed = Manager()->DestroyMetasInAreaAndRebuild(area, true, true, false);
    MemoryMarker(GAME_TEXT(0x0064F8B0, "After Area deletion")); // "After Area deletion"
    Selecting() = true;
    At<bool>(0x55) = false;
    return destroyed;
}

// 0x0059D980
void CCursor::FlattenSelection()
{
    if (!Selecting())
        return;
    GridDims area = {};
    SelectedArea(this, &area);
    Manager()->ClearArea(area);
    Selecting() = true;
    At<bool>(0x55) = false;
}

// 0x0059D9F0
bool CCursor::RaiseArea(GridDims area, int height, int dir, char level)
{
    if (area.w > 10 || area.l > 10)
        return false;
    if (!Manager()->ChangeFloorHeight(area, area.y, height, dir > 0, level == 0))
        return false;
    Move(0, 0);
    if (Manager()->RebuildArea(&area, true)) {
        Manager()->HighlightGapPieces(false, nullptr);
        return true;
    }
    Manager()->UndoFloorChange(); // undone
    Manager()->RebuildArea(&area, false);
    return false;
}

// 0x0059DAA0
bool CCursor::RaiseLower(int dir)
{
    GridDims area = Area();
    if (Mode() == 4) {
        if (Selecting())
            SelectedArea(this, &area);
    } else if (Mode() == 5) {
        if (!CurrentClipboard())
            return false;
        ClipboardArea(CurrentClipboard(), this, &area);
    }
    GridDims park = CParkManager::Instance()->ParkDims();
    if (area.w > park.w || area.l > park.l)
        return false;
    // (the original has the level flag written over the copy's first byte)
    char level = 0;
    int height = FloorHeight(Manager(), area, &level);
    if (height + dir <= -16)
        return false;
    int top = area.h + height + dir;
    // The pieces standing on the area (any height) widen it, and the highest sets the top.
    GridDims above = {area.x, area.y, area.z, area.w, 16, area.l};
    CMapListNode* list = MetasWithin(CParkManager::Instance(), above);
    for (CMapListNode* node = list; node; node = node->mp_next) {
        void* meta = node->GetMeta();
        GridDims at = MetaArea(meta);
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
    CMapListNode::DestroyList(list);
    if (top > 17)
        return false;
    if (Mode() == 5 && CurrentClipboard())
        AsClipboard(CurrentClipboard())->DestroyMetas();
    bool raised = RaiseArea(area, height, dir, level);
    if (Mode() == 4)
        At<bool>(0x55) = false;
    return raised;
}

// 0x0059DCD0
void CParkManager::Initialize(uint32_t level)
{
    if (At<bool>(0x4))
        return;
    At<bool>(0x4) = true;
    Init(level); // the park heap and pools
    for (int x = 0; x < kGridSize; ++x) {
        for (int z = 0; z < kGridSize; ++z) {
            uint8_t* cell = Bytes() + 0x352C + (x * kGridSize + z) * 0x10;
            cell[8] = 0;
            *reinterpret_cast<uint32_t*>(cell + 0xC) = 0;
        }
    }
    // The park's gap manager (0xA8 bytes), in the top-down heap for that level.
    Mem::Manager* memory = Mem::Manager::Get();
    if (level == kTopDownLevel)
        memory->PushContext(memory->m_topDownHeap);
    void* block = Mem::New(0xA8);
    mp_gapManager = block ? static_cast<CGapManager*>(block)->Construct(this) : nullptr;
    if (level == kTopDownLevel)
        Mem::Manager::Get()->PopContext();
    int theme = (Flags() & 2) ? At<uint16_t>(0x13C98) : 0;
    mp_generator->LoadPieces(level, theme);
    SetUpAbstractMetas();
    SetUpRoadMask();
    SetDefaultBounds();
    RebuildPark(0);
    if (level == kParkEditorLevel)
        mp_generator->BuildGoalNodeArray();

    // The palette's sets (Ed_piece_sets): each a run of the standard meta pieces (Ed_standard_metapieces) from its
    // `first` up to the next set's, or the clipboards.
    Script::CArray* sets = Script::GetArray(GAME_TEXT(0x0064F8E4, "Ed_piece_sets")); // "Ed_piece_sets"
    NumSets() = static_cast<int>(sets->m_size);
    Script::CArray* standard = Script::GetArray(GAME_TEXT(0x0064F690, "Ed_standard_metapieces")); // "Ed_standard_metapieces"
    int end = static_cast<int>(standard->m_size);
    for (int i = NumSets() - 1; i >= 0; --i) {
        PieceSet& set = Sets(this)[i];
        auto* description = reinterpret_cast<Script::CStruct*>(sets->GetElement(static_cast<uint32_t>(i)));
        set.m_name = nullptr;
        description->GetString(GAME_TEXT(0x0064D0C4, "name"), &set.m_name, 1); // "name"
        set.m_nameChecksum = Crc::GenerateCRCFromString(set.m_name);
        set.m_selected = 0;
        set.m_numEntries = 0;
        uint32_t first = 0;
        if (description->GetChecksum(reinterpret_cast<const char*>(0x0064A144), &first, 0)) { // "first"
            // `first`'s place in the standard pieces (the manager's list runs from the last to the first).
            int index = 0;
            int position = At<int>(0x88) - 1;
            for (auto* node = At<CMapListNode*>(0x80); node; node = node->mp_next, --position) {
                if (MetaType(NodeMeta(node)) == first) {
                    index = position;
                    break;
                }
            }
            set.m_numEntries = end - index;
            end = index;
            for (int entry = 0; entry < set.m_numEntries; ++entry) {
                int piece = index + entry;
                void* meta = nullptr;
                int at = At<int>(0x88) - 1;
                for (auto* node = At<CMapListNode*>(0x80); node; node = node->mp_next, --at) {
                    void* candidate = NodeMeta(node);
                    if (at == piece) {
                        meta = candidate;
                        break;
                    }
                }
                set.m_entries[entry].checksum = MetaType(meta); // (no check for a missing piece, as in the original)
                auto* pieceDescription =
                    reinterpret_cast<Script::CStruct*>(standard->GetElement(static_cast<uint32_t>(piece)));
                if (!pieceDescription->GetString(GAME_TEXT(0x0064F8D8, "text_name"), // "text_name"
                                                 &set.m_entries[entry].name, 0))
                    set.m_entries[entry].name = nullptr;
            }
        }
        set.m_isClipboard = false;
        if (description->ContainsFlag(kClipboardSet)) {
            set.m_isClipboard = true;
            set.m_numEntries = 6;
            const char* text = Script::GetString(0x4E677220u);
            for (int k = 0; k < 6; ++k) {
                char name[100];
                std::snprintf(name, sizeof(name), GAME_TEXT(0x0064F8CC, "Clipboard%d"), k); // "Clipboard%d"
                set.m_entries[k].name = text;
                set.m_entries[k].checksum = Crc::GenerateCRCFromString(name);
            }
        }
        set.m_visible = !description->ContainsFlag(GAME_TEXT(0x0064F8C4, "hidden")); // "hidden"
    }
}

} // namespace Ed

REPLACE(0x0059D120, &Ed::CCursor::RemoveClipboardsTooBig);
REPLACE(0x0059D240, &Ed::CCursor::PlacePiece);
REPLACE(0x0059D380, &Ed::CCursor::DestroyUnderCursor);
REPLACE(0x0059D410, &Ed::CCursor::AddGapHalf);
REPLACE(0x0059D600, &Ed::CCursor::RemoveGapHalf);
REPLACE(0x0059D6B0, &Ed::CCursor::ChangeGap);
REPLACE(0x0059D8E0, &Ed::CCursor::DestroySelection);
REPLACE(0x0059D980, &Ed::CCursor::FlattenSelection);
REPLACE(0x0059D9F0, &Ed::CCursor::RaiseArea);
REPLACE(0x0059DAA0, &Ed::CCursor::RaiseLower);
REPLACE(0x0059DCD0, &Ed::CParkManager::Initialize);
