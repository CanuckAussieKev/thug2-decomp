// The park editor's cursor: placing pieces and restart points, gaps, area selection, raising and lowering the
// floor; and the park manager's set-up (0x0059D120-0x0059E0F7, see park_editor.h).

#include "game/park/park_editor.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
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
    void* meta = GameFunction<void*(__thiscall*)(CMapListNode*)>(0x005480C0)(node);
    return GameFunction<void*(__thiscall*)(void*)>(0x005902E0)(meta);
}
uint8_t* NextClipboard(uint8_t* clipboard) { return *reinterpret_cast<uint8_t**>(clipboard + 0x14); }
// A clipboard's area where the cursor would paste it (0x0058BF10).
void ClipboardArea(uint8_t* clipboard, CCursor* cursor, GridDims* area)
{
    GameFunction<void(__thiscall*)(uint8_t*, CCursor*, GridDims*)>(0x0058BF10)(clipboard, cursor, area);
}
// The gap manager's (0x00597AC0): the gap with a half at `area` (and which half), or null.
GapDescriptor* GapAt(CParkManager* manager, GridDims* area, int* half)
{
    return GameFunction<GapDescriptor*(__thiscall*)(CParkManager*, GridDims*, int*)>(0x00597AC0)(manager, area, half);
}
void RemoveGap(CParkManager* manager, GapDescriptor* gap)
{
    GameFunction<void(__thiscall*)(CParkManager*, GapDescriptor*)>(0x00597FC0)(manager, gap);
}
bool AddGap(CParkManager* manager, GapDescriptor* gap)
{
    return GameFunction<bool(__thiscall*)(CParkManager*, GapDescriptor*)>(0x00597BD0)(manager, gap);
}
// The metas within an area (0x00597740; the flags: 0, 0, 1) - the caller owns the list.
CMapListNode* MetasWithin(CParkManager* manager, GridDims area)
{
    return GameFunction<CMapListNode*(__thiscall*)(CParkManager*, GridDims, int, int, int)>(0x00597740)(manager, area, 0,
                                                                                                    0, 1);
}
// The area selected (0x00597110).
void SelectedArea(CCursor* cursor, GridDims* area)
{
    GameFunction<void(__thiscall*)(CCursor*, GridDims*)>(0x00597110)(cursor, area);
}
int FloorHeight(CParkManager* manager, GridDims area, char* level)
{
    return GameFunction<int(__thiscall*)(CParkManager*, GridDims, char*)>(0x005959D0)(manager, area, level);
}
void* ParkEditor() { return GameFunction<void*(__cdecl*)(bool)>(0x00590160)(false); }
bool ParkFull(void* editor) { return GameFunction<bool(__thiscall*)(void*)>(0x0058DDC0)(editor); }
void ReleaseParkEditor() { GameFunction<void(__cdecl*)()>(0x0058D290)(); }
void SetDefaultHeap(Mem::Heap* heap) { GameFunction<void(__cdecl*)(Mem::Heap*)>(0x005BA1D0)(heap); }

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
            GameFunction<void(__thiscall*)(uint8_t*)>(0x0058C980)(clipboard); // ~CClipboard
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
    if (!GameFunction<bool(__thiscall*)(CParkManager*, GridDims, int)>(0x00599CD0)(manager, Area(), 0))
        return false;
    if (Area().h + FloorHeight(manager, Area(), nullptr) > 17)
        return false;
    // A restart point needs a free slot of its type.
    uint32_t type = MetaType(Meta());
    for (int slot = 1; slot < 9; ++slot) {
        if (type == manager->At<uint32_t>(0x177A4 + (slot - 1) * 4)) {
            if (!GameFunction<bool(__thiscall*)(void*, int)>(0x00591F50)(manager->Generator(), slot)) {
                Dbg::Printf(reinterpret_cast<const char*>(0x0064F85C)); // "No free slots exist for this type of restart point\n"
                return false;
            }
            break;
        }
    }
    SetDefaultHeap(GetMainHeap());
    void* source = GameFunction<void*(__thiscall*)(CParkManager*, uint32_t, int)>(0x00595620)(manager, MetaType(Meta()), 0);
    void* piece = GameFunction<void*(__thiscall*)(CParkManager*, void*, int)>(0x00595670)(manager, source, 0);
    GameFunction<void(__thiscall*)(void*, int)>(0x005907B0)(piece, Rotation());
    GameFunction<void(__thiscall*)(CParkManager*, GridDims, void*)>(0x00599A20)(manager, Area(), piece);
    SetDefaultHeap(GetMainHeap());
    manager->RebuildArea(&Area(), false);
    MemoryMarker(reinterpret_cast<const char*>(0x0064F890)); // "After placing a piece"
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
    void* editor = ParkEditor();
    if (ParkFull(editor)) {
        ReleaseParkEditor();
        return false;
    }
    CMapListTemp metas = {MetasWithin(Manager(), Area())};
    bool added = false;
    if (metas.list) {
        void* meta = NodeMeta(metas.list);
        GameFunction<void(__thiscall*)(GridDims*)>(0x00590210)(&Area()); // (prints the area)
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
                    std::sprintf(gap.m_text, reinterpret_cast<const char*>(0x0064F8A8), GapCounter()); // "gap%d"
                else
                    std::memcpy(gap.m_text, "gap", 4);
                ++GapCounter();
            }
            if (AddGap(Manager(), &gap)) {
                GameFunction<void(__thiscall*)(CParkManager*, const GridDims*, int)>(0x0059C600)(Manager(), &Area(), 0);
                if (gap.m_numCompleteHalves > 0)
                    GameFunction<void(__thiscall*)(CParkManager*, int, GapDescriptor*)>(0x00595CB0)(Manager(), 1, &gap);
                added = true;
            } else {
                gap.m_numCompleteHalves = 0;
            }
        }
    }
    GameFunction<void(__thiscall*)(CMapListTemp*)>(0x00591C60)(&metas);
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
    // The pieces that had gaps are told to look again (0x00591A50).
    manager = Manager();
    for (auto* node = manager->At<CMapListNode*>(0x7C); node; node = node->mp_next) {
        if (MetaFlags(NodeMeta(node)) & kMetaHasGap)
            GameFunction<void(__thiscall*)(void*, int, int)>(0x00591A50)(NodeMeta(node), 1, 1);
    }
    GameFunction<void(__thiscall*)(void*, int)>(0x00592290)(manager->Generator(), 0);
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
    MemoryMarker(reinterpret_cast<const char*>(0x0064F8B0)); // "After Area deletion"
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
    if (!GameFunction<bool(__thiscall*)(CParkManager*, GridDims, int, int, bool, bool)>(0x0059C520)(
            Manager(), area, area.y, height, dir > 0, level == 0))
        return false;
    GameFunction<void(__thiscall*)(CCursor*, int, int)>(0x00598B60)(this, 0, 0);
    if (Manager()->RebuildArea(&area, true)) {
        GameFunction<void(__thiscall*)(CParkManager*, int, int)>(0x00595CB0)(Manager(), 0, 0);
        return true;
    }
    GameFunction<void(__thiscall*)(CParkManager*)>(0x0059B280)(Manager()); // undone
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
        void* meta = GameFunction<void*(__thiscall*)(CMapListNode*)>(0x005480C0)(node);
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
    GameFunction<void(__thiscall*)(CMapListNode*)>(0x00591BE0)(list); // the list freed
    if (top > 17)
        return false;
    if (Mode() == 5 && CurrentClipboard())
        GameFunction<void(__thiscall*)(uint8_t*)>(0x0058C200)(CurrentClipboard());
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
    // The park's map (0xA8 bytes, 0x0058CCC0), in the top-down heap for that level.
    Mem::Manager* memory = Mem::Manager::Get();
    if (level == kTopDownLevel)
        memory->PushContext(memory->m_topDownHeap);
    void* block = Mem::New(0xA8);
    At<void*>(0x177C4) = block ? GameFunction<void*(__thiscall*)(void*, CParkManager*)>(0x0058CCC0)(block, this) : nullptr;
    if (level == kTopDownLevel)
        Mem::Manager::Get()->PopContext();
    int theme = (Flags() & 2) ? At<uint16_t>(0x13C98) : 0;
    GameFunction<void(__thiscall*)(void*, uint32_t, int)>(0x00594FD0)(Generator(), level, theme);
    GameFunction<void(__thiscall*)(CParkManager*)>(0x005960D0)(this);
    GameFunction<void(__thiscall*)(CParkManager*)>(0x00596D50)(this);
    GameFunction<void(__thiscall*)(CParkManager*)>(0x00596CD0)(this);
    RebuildPark(0);
    if (level == kParkEditorLevel)
        GameFunction<void(__thiscall*)(void*)>(0x00592C10)(Generator());

    // The palette's sets (Ed_piece_sets): each a run of the standard meta pieces (Ed_standard_metapieces) from its
    // `first` up to the next set's, or the clipboards.
    Script::CArray* sets = Script::GetArray(reinterpret_cast<const char*>(0x0064F8E4)); // "Ed_piece_sets"
    NumSets() = static_cast<int>(sets->m_size);
    Script::CArray* standard = Script::GetArray(reinterpret_cast<const char*>(0x0064F690)); // "Ed_standard_metapieces"
    int end = static_cast<int>(standard->m_size);
    for (int i = NumSets() - 1; i >= 0; --i) {
        PieceSet& set = Sets(this)[i];
        auto* description = reinterpret_cast<Script::CStruct*>(sets->GetElement(static_cast<uint32_t>(i)));
        set.m_name = nullptr;
        description->GetString(reinterpret_cast<const char*>(0x0064D0C4), &set.m_name, 1); // "name"
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
                if (!pieceDescription->GetString(reinterpret_cast<const char*>(0x0064F8D8), // "text_name"
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
                std::snprintf(name, sizeof(name), reinterpret_cast<const char*>(0x0064F8CC), k); // "Clipboard%d"
                set.m_entries[k].name = text;
                set.m_entries[k].checksum = Crc::GenerateCRCFromString(name);
            }
        }
        set.m_visible = !description->ContainsFlag(reinterpret_cast<const char*>(0x0064F8C4)); // "hidden"
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
