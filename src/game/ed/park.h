#pragma once

// The park editor (Create-A-Park): the park manager (Ed::CParkManager, the one instance at 0x007CE604) holds the
// park being edited - the metapieces placed on a 58 x 58 grid of cells, the floor heights, the piece sets the
// menus show and the compressed map that is saved; the cursor (Ed::CCursor, 0x007CE608) is what the player moves
// over the grid. Positions on the grid are GridDims (a cell and a size, in cells; y counts floor steps).
//
// The pieces: an abstract metapiece is a kind of piece (one per entry of the piece sets, named by a checksum); a
// concrete metapiece is one placed in the park (or under the cursor), made from an abstract one. Both are kept in
// singly linked lists of CMapListNode.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game.h"

namespace Ed {

class CParkGenerator; // the scene side of the park (0x4A4 bytes, 0x00593460 constructs it): no source yet
class CCursor;

// A box of cells: the corner (x, y, z) and the size (w, h, l). y is signed (floor steps below 0 are dug in).
struct GridDims {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t h;
    uint8_t l;

    // 0x005901B0 (the constructor; inline here)
    static GridDims Make(uint8_t x, int8_t y, uint8_t z, uint8_t w, uint8_t h, uint8_t l) { return {x, y, z, w, h, l}; }
    // 0x005901E0: byte `index` (x y z w h l)
    uint8_t& operator[](int index) { return reinterpret_cast<uint8_t*>(this)[index]; }
    // 0x005901F0: make the box reach the top of the world (h = 127 - y)
    void MakeInfinitelyHigh() { h = static_cast<uint8_t>(0x7F - y); }
};
static_assert(sizeof(GridDims) == 6);

// One piece of a metapiece (12 bytes): its cell offset and rotation within the metapiece, and the piece.
struct SMetaDescriptor {
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t rot;
    uint32_t unknown4;
    void* piece; // +0x08: the scene piece (its +0x08 flags)
};
static_assert(sizeof(SMetaDescriptor) == 12);

// A metapiece (abstract or concrete; the concrete one's vtable is used here). Vtable slot 0: the descriptor of
// piece `index` (__thiscall, returns a piece); slot 1: the scalar deleting destructor.
class CMetaPiece {
public:
    // The flags (m_flags).
    enum : uint32_t {
        kFlag1 = 0x1,               // (only these are highlighted by an area selection)
        kInPark = 0x4,              // placed in the park (not the cursor's own piece)
        kInnerShell = 0x8,          // part of the floor / shell the park builds itself (risers, walls)
        kFlag20 = 0x20,             // (kept when an area is cleared with flag 4)
        kSector = 0x80,             // a menu entry that is a sector (a whole set) rather than one piece
        kFence = 0x100,             // a fence piece of the inner shell
        kCursor = 0x200,            // the cursor's piece
        kFlag400 = 0x400,
        kGap = 0x800,               // the gap tool
        kHighlightable = 0x1000,    // highlighted while a gap is placed
        kAreaSelect = 0x8000,       // the clipboard's area select tool
        kRailPlacement = 0x10000,   // the rail tool
        kRiserBelow = 0x20000,
    };

    void** m_vtable;
    GridDims m_cell_area;            // +0x04
    uint8_t pad0A[2];
    uint32_t m_flags;                // +0x0C
    int m_rot;                       // +0x10: quarter turns
    SMetaDescriptor m_desc[2];       // +0x14: the first two pieces
    SMetaDescriptor* mp_extra_desc;  // +0x2C: the others (piece 2 is mp_extra_desc[0])
    uint32_t m_max_entries;          // +0x30
    uint32_t m_num_used_entries;     // +0x34
    uint32_t m_name_checksum;        // +0x38: the abstract metapiece's name

    // 0x005902E0 (returns this): a metapiece as a concrete one
    CMetaPiece* CastToConcreteMeta() { return this; }
    // 0x00590340: descriptor `index`
    SMetaDescriptor* GetDescriptor(uint32_t index)
    {
        return index < 2 ? &m_desc[index] : &mp_extra_desc[index - 2];
    }
    // 0x00591A50: highlight (or not) each of its pieces that can be
    void Highlight(int on, int flag) { GameFunction<void(__thiscall*)(CMetaPiece*, int, int)>(0x00591A50)(this, on, flag); }
    // 0x00591800: put its pieces at `position` turned by `angle` degrees
    void SetPosition(Mth::Vector position, float angle)
    {
        GameFunction<void(__thiscall*)(CMetaPiece*, Mth::Vector, float)>(0x00591800)(this, position, angle);
    }
    // 0x005907B0: turn to `rot` quarter turns
    void SetRot(int rot) { GameFunction<void(__thiscall*)(CMetaPiece*, int)>(0x005907B0)(this, rot); }
    // 0x005909C0: piece `index`'s position relative to the metapiece (returns `out`)
    Mth::Vector* GetRelativePosition(Mth::Vector* out, uint32_t index)
    {
        return GameFunction<Mth::Vector*(__thiscall*)(CMetaPiece*, Mth::Vector*, uint32_t)>(0x005909C0)(this, out, index);
    }
    // 0x00590750: whether the metapiece lies within `area`
    bool IsCompletelyWithinArea(GridDims* area)
    {
        return GameFunction<bool(__thiscall*)(CMetaPiece*, GridDims*)>(0x00590750)(this, area);
    }
    // Vtable slot 1: the scalar deleting destructor
    void Delete() { reinterpret_cast<void(__thiscall*)(CMetaPiece*, int)>(m_vtable[1])(this, 1); }
};
static_assert(offsetof(CMetaPiece, m_flags) == 0x0C);
static_assert(offsetof(CMetaPiece, m_num_used_entries) == 0x34);
static_assert(sizeof(CMetaPiece) == 0x3C);

// A node of a metapiece list.
struct CMapListNode {
    void* unknown0;
    CMetaPiece* mp_meta; // +0x04
    CMapListNode* mp_next; // +0x08

    // 0x005480C0 (one of many getters folded together there)
    CMetaPiece* GetMeta() { return mp_meta; }
    CMetaPiece* GetConcreteMeta() { return mp_meta->CastToConcreteMeta(); }
    // 0x00591BE0: free a list (the nodes, not the metapieces; null is fine)
    static void DestroyList(CMapListNode* list) { GameFunction<void(__thiscall*)(CMapListNode*)>(0x00591BE0)(list); }
};

// The holder of a list returned by the manager's lookups (0x004E1970 makes it, 0x00591C60 frees the list).
struct MapListHolder {
    CMapListNode* list;
    explicit MapListHolder(CMapListNode* l) : list(l) {}
    ~MapListHolder()
    {
        if (list)
            CMapListNode::DestroyList(list);
    }
    MapListHolder(const MapListHolder&) = delete;
    MapListHolder& operator=(const MapListHolder&) = delete;
};

// A gap being placed (0x54 bytes; 0x0058CBC0 initialises it): the gap tool's state.
struct GapInfo {
    uint8_t unknown00[0x48];
    int m_numPlaced;   // +0x48: how many of its ends are placed (1 while the second is being placed)
    int m_gapIndex;    // +0x4C: the gap manager's index (-1 none)
    int unknown50;
};
static_assert(sizeof(GapInfo) == 0x54);

// A copied area (0x18 bytes, pooled: the pool pair 0x007CE5B4): the metapieces (and rails) copied from it.
struct CClipboard {
    GridDims m_area;           // +0x00
    uint8_t m_min_x, m_min_z;  // +0x06
    uint8_t m_max_x, m_max_z;  // +0x08
    uint8_t pad0A[2];
    void* mp_metas;            // +0x0C: its entries (linked by +0x14)
    void* mp_rails;            // +0x10
    CClipboard* mp_next;       // +0x14: the cursor's list

    // 0x0058BDF0
    CClipboard* Construct() { return GameFunction<CClipboard*(__thiscall*)(CClipboard*)>(0x0058BDF0)(this); }
    // 0x0058C430 (reached through 0x0058C980): the destructor
    void Destroy() { GameFunction<void(__thiscall*)(CClipboard*)>(0x0058C980)(this); }
    // 0x0058B7E0
    bool IsEmpty() { return GameFunction<bool(__thiscall*)(CClipboard*)>(0x0058B7E0)(this); }
    // 0x0058C590: copy the metapieces in `area`; false when they don't fit
    bool CopySelectionToClipboard(GridDims area)
    {
        return GameFunction<bool(__thiscall*)(CClipboard*, GridDims)>(0x0058C590)(this, area);
    }
    // 0x0058BE30: the floor height under the copy at the cursor
    int GetHeight(CCursor* cursor) { return GameFunction<int(__thiscall*)(CClipboard*, CCursor*)>(0x0058BE30)(this, cursor); }
    // 0x0058BEB0: highlight the metapieces the copy would replace
    void HighlightIntersectingMetas(CCursor* cursor, int on)
    {
        GameFunction<void(__thiscall*)(CClipboard*, CCursor*, int)>(0x0058BEB0)(this, cursor, on);
    }
    // 0x0058C130: show the copy at `position`, turned by `angle`, `height` steps up
    void ShowMetas(Mth::Vector position, float angle, int height)
    {
        GameFunction<void(__thiscall*)(CClipboard*, Mth::Vector, float, int)>(0x0058C130)(this, position, angle, height);
    }
    // 0x0058C200: remove what ShowMetas made
    void DestroyMetas() { GameFunction<void(__thiscall*)(CClipboard*)>(0x0058C200)(this); }
};
static_assert(sizeof(CClipboard) == 0x18);

// A piece set: the entries of one of the piece menu's sets (0x154 bytes).
struct CPieceSet {
    struct Entry {
        uint32_t checksum;  // the abstract metapiece (or sector)
        const char* name;   // shown in the menu (null: "---")
    };
    uint8_t unknown000[8];
    Entry m_entries[40];    // +0x008
    int m_numEntries;       // +0x148
    int m_selected;         // +0x14C
    bool m_visible;         // +0x150: has a menu entry
    bool m_isClipboard;     // +0x151: the clipboard set
    uint8_t pad152[2];
};
static_assert(sizeof(CPieceSet) == 0x154);

// A cell of the floor (16 bytes).
struct FloorCell {
    int m_height;   // in floor steps
    int unknown4;
    uint8_t unknown8;
    uint8_t pad9[3];
    int unknownC;
};
static_assert(sizeof(FloorCell) == 16);

constexpr int kFloorSize = 58; // the grid is 58 x 58 cells

// The park as saved (0x3A98 bytes): a header, the floor heights, then the metapieces (8 bytes each) and the gaps.
struct CompressedMap {
    uint8_t unknown00[6];
    uint16_t m_version;      // +0x06: 0x4E24 (gaps 0x30 bytes); older parks have 0x2C-byte gaps
    uint16_t m_08;           // +0x08
    uint8_t unknown0A[2];
    uint8_t m_x, m_z, m_w, m_l; // +0x0C: the park's bounds
    uint16_t m_numMetas;     // +0x10
    uint16_t m_numGaps;      // +0x12
    uint8_t m_maxPlayers;    // +0x14
    uint8_t pad15[3];
    uint32_t m_theme;        // +0x18
    char m_name[0x40];       // +0x1C
    int8_t m_floorHeights[kFloorSize * kFloorSize]; // +0x5C
    uint8_t m_rest[0x3A98 - 0x5C - kFloorSize * kFloorSize];
};
static_assert(offsetof(CompressedMap, m_floorHeights) == 0x5C);
static_assert(sizeof(CompressedMap) == 0x3A98);

// The gap manager (the singleton 0x007CE5D4; the park manager keeps it at +0x177C4): the gaps placed in the park.
class CGapManager {
public:
    // 0x0058CA10: the gap `meta` is an end of (its index into `index`)
    bool GetGapIndex(CMetaPiece* meta, int* index)
    {
        return GameFunction<bool(__thiscall*)(CGapManager*, CMetaPiece*, int*)>(0x0058CA10)(this, meta, index);
    }
    // 0x0058CB20: gap `index`, and which end (`half`)
    GapInfo* GetGapInfo(int index, int* half)
    {
        return GameFunction<GapInfo*(__thiscall*)(CGapManager*, int, int*)>(0x0058CB20)(this, index, half);
    }
};

// The park generator (the scene side): what's used of it here.
class CParkGenerator {
public:
    // 0x005933F0: take away player `player`'s restart point at `area`
    void RemoveRestart(GridDims* area, int player)
    {
        GameFunction<void(__thiscall*)(CParkGenerator*, GridDims*, int)>(0x005933F0)(this, area, player);
    }
    // 0x005935F0: delete a piece
    void DestroyPiece(void* piece) { GameFunction<void(__thiscall*)(CParkGenerator*, void*)>(0x005935F0)(this, piece); }
    // 0x00593610: finish removing what was destroyed
    void CleanUpOutRefs() { GameFunction<void(__thiscall*)(CParkGenerator*)>(0x00593610)(this); }
    // 0x00592290: show or hide the highlight of every piece
    void SetPiecesHighlighted(int on) { GameFunction<void(__thiscall*)(CParkGenerator*, int)>(0x00592290)(this, on); }
    // 0x00591D20 (sets +0x20)
    void SetMaxPlayers(int count) { GameFunction<void(__thiscall*)(CParkGenerator*, int)>(0x00591D20)(this, count); }
};

class CParkManager {
public:
    // m_flags (meanings partly guessed from where they're set)
    enum : uint32_t {
        kMapUpToDate = 0x1, // the compressed map holds the park as it is built
        kMapLoaded = 0x2,   // a park was read in (file or memory card)
        kMapSaved = 0x4,    // the compressed map holds the park (which may be gone)
        kFlag8 = 0x8,
    };

    // The one instance.
    static CParkManager*& Instance() { return GameVariable<CParkManager*>(0x007CE604); }

    // 0x00597740: the metapieces intersecting `area` (or, given `shape`, the pieces of `shape` turned by `rot` at
    // `area`); `skipInnerShell` (low byte) leaves out the park's own floor and walls. A new list the caller frees.
    CMapListNode* GetIntersectingMetas(GridDims area, int rot, CMetaPiece* shape, uint32_t skipInnerShell)
    {
        return GameFunction<CMapListNode*(__thiscall*)(CParkManager*, GridDims, int, CMetaPiece*, uint32_t)>(0x00597740)(
            this, area, rot, shape, skipInnerShell);
    }
    // 0x00597A00: every metapiece with a piece in a cell of `area` (a new list the caller frees)
    CMapListNode* GetAreaMetaPieces(GridDims area)
    {
        return GameFunction<CMapListNode*(__thiscall*)(CParkManager*, GridDims)>(0x00597A00)(this, area);
    }
    // 0x005959D0: the highest floor in `area`; `isFlat` (if given) says whether it's all one height
    int GetFloorHeight(GridDims area, bool* isFlat)
    {
        return GameFunction<int(__thiscall*)(CParkManager*, GridDims, bool*)>(0x005959D0)(this, area, isFlat);
    }
    // 0x00597AC0: the gap at the metapiece in cell `pos` (made 1x1x1), and which of its ends (`half`)
    GapInfo* GetGapInfo(GridDims* pos, int* half)
    {
        return GameFunction<GapInfo*(__thiscall*)(CParkManager*, GridDims*, int*)>(0x00597AC0)(this, pos, half);
    }
    // 0x00595A90: the world position of `area`'s corner (w = 1), and its size (if given). Returns `out`.
    Mth::Vector* GridDimsToWorld(Mth::Vector* out, GridDims* area, Mth::Vector* size)
    {
        return GameFunction<Mth::Vector*(__thiscall*)(CParkManager*, Mth::Vector*, GridDims*, Mth::Vector*)>(0x00595A90)(
            this, out, area, size);
    }
    // 0x00595CB0: highlight the pieces a gap can attach to (and the gap itself)
    void HighlightGapPieces(int on, GapInfo* gap)
    {
        GameFunction<void(__thiscall*)(CParkManager*, int, GapInfo*)>(0x00595CB0)(this, on, gap);
    }
    // 0x00595210: piece set `index` and its menu index
    CPieceSet* GetPieceSet(int index, int* menuIndex)
    {
        return GameFunction<CPieceSet*(__thiscall*)(CParkManager*, int, int*)>(0x00595210)(this, index, menuIndex);
    }
    // 0x00595670: a new concrete metapiece made from `source`
    CMetaPiece* CreateConcreteMeta(CMetaPiece* source, uint32_t flags)
    {
        return GameFunction<CMetaPiece*(__thiscall*)(CParkManager*, CMetaPiece*, uint32_t)>(0x00595670)(this, source,
                                                                                                    flags);
    }
    // The abstract metapiece named `checksum` (null if none) - inline in the original.
    CMetaPiece* GetAbstractMeta(uint32_t checksum)
    {
        for (CMapListNode* node = mp_abstract_metas; node; node = node->mp_next) {
            CMetaPiece* meta = node->GetConcreteMeta();
            if (meta->m_name_checksum == checksum)
                return meta;
        }
        return nullptr;
    }

    // 0x00599880: delete the metapieces intersecting `area` (`flags`: 2 not the inner shell, 4 not kFlag20 ones;
    // passed on to DestroyConcreteMeta). Returns the count of metapieces << 16 | the count of their pieces.
    uint32_t DestroyMetasInArea(GridDims area, uint32_t flags);
    // 0x00599960: highlight (or not) the metapieces wholly within `area`
    void HighlightMetasWithinArea(GridDims area, int on);
    // 0x0059AE90: take `meta` out of the park and delete it (the cursor's own piece stays). `flags` 1: also
    // delete what stands on it (2: not the inner shell).
    void DestroyConcreteMeta(CMetaPiece* meta, uint32_t flags);

    // 0x00598150: enter `meta` into (or take it out of) the lists of the cells it covers
    void SetMetaInCells(CMetaPiece* meta, int add)
    {
        GameFunction<void(__thiscall*)(CParkManager*, CMetaPiece*, int)>(0x00598150)(this, meta, add);
    }
    // 0x00595FC0: the floor under `meta` without it
    void RemoveMetaFromFloor(CMetaPiece* meta)
    {
        GameFunction<void(__thiscall*)(CParkManager*, CMetaPiece*)>(0x00595FC0)(this, meta);
    }
    // 0x00595EE0: the floor under `meta` with it
    void AddMetaToFloor(CMetaPiece* meta) { GameFunction<void(__thiscall*)(CParkManager*, CMetaPiece*)>(0x00595EE0)(this, meta); }
    // 0x00595E80: unlink `meta` from `list`
    void RemoveMetaFromList(CMetaPiece* meta, CMapListNode** list)
    {
        GameFunction<void(__thiscall*)(CParkManager*, CMetaPiece*, CMapListNode**)>(0x00595E80)(this, meta, list);
    }
    // 0x00597FC0: delete a gap
    void RemoveGap(GapInfo* gap) { GameFunction<void(__thiscall*)(CParkManager*, GapInfo*)>(0x00597FC0)(this, gap); }

    CParkGenerator* mp_generator;        // +0x00
    bool m_parkBuilt;                    // +0x04
    bool m_05;
    uint8_t pad06[2];
    CMapListNode* mp_riserList;          // +0x08
    int m_riserMetas;                    // +0x0C
    int m_riserPieces;                   // +0x10
    uint32_t m_shellBlocks[8][3];        // +0x14: "floor_wall_block%d", "wall_block%d", "floor_block%d" (1..8)
    int m_74;
    void* mp_78;                         // +0x78: a Malloc block
    CMapListNode* mp_concrete_metas;     // +0x7C
    CMapListNode* mp_abstract_metas;     // +0x80
    int m_numConcreteMetas;              // +0x84
    int m_88;
    CMapListNode* m_cellMetas[kFloorSize][kFloorSize]; // +0x8C: [x][z] the metapieces with pieces in each cell
    int m_351C;
    GridDims m_parkBounds;               // +0x3520: the park's area
    GridDims m_shellBounds;              // +0x3526
    FloorCell m_floor[kFloorSize][kFloorSize]; // +0x352C: [x][z]
    CPieceSet m_sets[40];                // +0x1076C
    int m_numSets;                       // +0x13C8C
    CompressedMap m_map;                 // +0x13C90
    uint32_t m_flags;                    // +0x17728
    char m_parkName[0x40];               // +0x1772C
    GridDims m_1776C[8];
    uint8_t unknown1779C[8];
    uint32_t m_restartPieces[8];         // +0x177A4: the restart pieces of players 1-8
    CGapManager* mp_gapManager;          // +0x177C4
    int m_177C8;
};
static_assert(offsetof(CParkManager, m_shellBlocks) == 0x14);
static_assert(offsetof(CParkManager, mp_concrete_metas) == 0x7C);
static_assert(offsetof(CParkManager, m_351C) == 0x351C);
static_assert(offsetof(CParkManager, m_floor) == 0x352C);
static_assert(offsetof(CParkManager, m_sets) == 0x1076C);
static_assert(offsetof(CParkManager, m_map) == 0x13C90);
static_assert(offsetof(CParkManager, m_flags) == 0x17728);
static_assert(offsetof(CParkManager, m_restartPieces) == 0x177A4);
static_assert(sizeof(CParkManager) == 0x177CC);

// The cursor (200 bytes): the box the player moves over the grid, with the piece to place (or the copy to paste).
class CCursor {
public:
    enum Mode {
        kModeNone = 0,
        kModeRegular = 1,
        kModeGap = 2,          // "gap_regular": the gap tool
        kModeGapAdjust = 3,    // "gap_adjust": over a gap's piece
        kModeAreaSelect = 4,   // selecting an area to copy
        kModePaste = 5,        // pasting the clipboard
        kModeRailPlacement = 6, // "rail_placement"
    };

    // The one instance.
    static CCursor*& Instance() { return GameVariable<CCursor*>(0x007CE608); }

    // 0x0059C7A0: the constructor (on the first piece of set 0). Returns this.
    CCursor* Construct();
    // 0x0059C860: make the instance (on the park editor's heap) if there isn't one
    static void CreateInstance();
    // 0x00598A60: highlight (or not) the metapieces under the cursor (or under the paste)
    void HighlightIntersectingMetas(int on);
    // 0x00598B40: whether the cursor is over a gap's piece (the lookup shrinks m_cell_area to 1x1x1)
    bool IsOverGap();
    // 0x00598B60: move by (dx, dz) cells (kept in the park), then follow the floor and the gap state
    void Move(int dx, int dz);
    // 0x0059A1F0: one frame: turn by `rotInc` quarter turns, step on the stick (shiftX, shiftZ), slide towards
    // the target and show the piece there
    void Update(float shiftX, float shiftZ, int rotInc);
    // 0x0059A5A0: drop the area selection (and its highlight)
    void ClearAreaSelection();
    // 0x0059A610: whether the selected area can't be copied (bigger than 10 x 10, or the editor says no)
    bool IsAreaSelectionInvalid();
    // 0x0059A6D0: copy the selected area into a new clipboard (the oldest goes when there are 6). False if
    // it can't be copied.
    bool CopyAreaToClipboard();
    // 0x0059A8D0: keep the selected area highlighted as the cursor moves
    void UpdateAreaSelectionHighlight();
    // 0x0059BAC0: delete the cursor's piece
    void DestroyCursorMeta();
    // 0x0059BAF0: paste mode, with the newest clipboard (if there is one)
    void SwitchToClipboard();
    // 0x0059BC00: take the piece `checksum` (an abstract metapiece) and the mode its kind needs
    void ChangePiece(uint32_t checksum);

    // 0x005971E0: keep the cursor (or the paste) within the park
    void ClampToPark() { GameFunction<void(__thiscall*)(CCursor*)>(0x005971E0)(this); }
    // 0x005954D0: tell the scripts the mode changed (parked_set_helper_text_mode)
    void OnModeChanged() { GameFunction<void(__thiscall*)(CCursor*)>(0x005954D0)(this); }
    // 0x00598940: forget the gap being placed if `meta` is (one of) its pieces
    void ForgetGapAt(CMetaPiece* meta) { GameFunction<void(__thiscall*)(CCursor*, CMetaPiece*)>(0x00598940)(this, meta); }
    // 0x00597110: the selected area (all zero without a selection)
    void GetAreaSelectDims(GridDims* out) { GameFunction<void(__thiscall*)(CCursor*, GridDims*)>(0x00597110)(this, out); }
    // 0x00597190: delete the oldest clipboard
    void RemoveOldestClipboard() { GameFunction<void(__thiscall*)(CCursor*)>(0x00597190)(this); }

    CParkManager* mp_manager;    // +0x00
    GridDims m_cell_area;        // +0x04
    uint8_t pad0A[2];
    float m_moveTime;            // +0x0C: of the slide to the target (1 when a step starts)
    int unknown10;
    Mth::Vector m_target;        // +0x14
    Mth::Vector m_pos;           // +0x24
    uint8_t unknown34[4];
    int m_rot;                   // +0x38: quarter turns
    float m_targetAngle;         // +0x3C: degrees
    float m_angle;               // +0x40
    CMetaPiece* mp_meta;         // +0x44: the piece shown at the cursor
    uint8_t m_areaSelect;        // +0x48: 1 while an area is being selected
    GridDims m_areaStart;        // +0x49
    GridDims m_areaLast;         // +0x4F: the cursor's cell when the highlight was made
    bool m_areaHighlighted;      // +0x55
    uint8_t pad56[2];
    CClipboard* mp_clipboards;   // +0x58: the newest first
    CClipboard* mp_clipboard;    // +0x5C: the one being pasted
    int m_clipboardHeight;       // +0x60
    int m_clipboardSet;          // +0x64: the clipboard set's index
    int m_menuIndex;             // +0x68
    int m_mode;                  // +0x6C
    GapInfo m_gap;               // +0x70
    int m_C4;
};
static_assert(offsetof(CCursor, m_target) == 0x14);
static_assert(offsetof(CCursor, mp_meta) == 0x44);
static_assert(offsetof(CCursor, m_areaStart) == 0x49);
static_assert(offsetof(CCursor, m_areaHighlighted) == 0x55);
static_assert(offsetof(CCursor, mp_clipboards) == 0x58);
static_assert(offsetof(CCursor, m_gap) == 0x70);
static_assert(sizeof(CCursor) == 200);

} // namespace Ed
