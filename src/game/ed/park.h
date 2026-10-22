#pragma once

// The park editor (Create-A-Park): the park manager (Ed::CParkManager, the one instance at 0x007CE604) holds the
// park being edited - the metapieces placed on a 58 x 58 grid of cells, the floor heights, the piece sets the
// menus show and the compressed map that is saved; the cursor (Ed::CCursor, 0x007CE608) is what the player moves
// over the grid; the upper menu (Ed::CUpperMenuManager, 0x007CE60C) shows the pieces. Positions on the grid are
// GridDims (a cell and a size, in cells; y counts floor steps).
//
// The pieces: an abstract metapiece is a kind of piece (the palette, from Ed_standard_metapieces, named by a
// checksum); a concrete metapiece is one placed in the park (or under the cursor), made from an abstract one. Both
// are kept in singly linked lists of CMapListNode, and every cell has a list of the metapieces over it.
//
// One set of types for the three parts written separately (src/game/ed: the park manager's and the cursor's
// 0x00595030-0x0059C900; src/game/park: 0x0059C900-0x0059E0F7, which still reaches some fields through the
// offset accessors Bytes() / At<T>()).

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game.h"

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Ed {

class CParkGenerator; // the scene side of the park (0x4A4 bytes, 0x00593460 constructs it)
class CCursor;
class CPiece;
class CMetaPiece;
class CParkManager;
struct RailNodeInfo;

// The grid's cells in the world (the exe's constants): 120 x 48 x 120, the 58 x 58 grid centred on the origin.
constexpr float kCellWidth = 120.0f;  // 0x0064F41C
constexpr float kCellHeight = 48.0f;  // 0x0064F420
constexpr float kCellLength = 120.0f; // 0x0064F424
constexpr float kGridCells = 58.0f;   // 0x0064F7A8
constexpr float kRiserBelowDrop = 1.5f; // 0x00646744
constexpr int kFloorSize = 58;        // the grid is 58 x 58 cells
constexpr int kGridSize = kFloorSize;

// Progress markers through the park's rebuilding, compiled out of this build (the empty function 0x005480A0).
inline void MarkProgress(const char* /*label*/) {}

// A box of cells: the corner (x, y, z) and the size (w, h, l). y is signed (floor steps below 0 are dug in).
// Passed by value in two stack slots.
struct GridDims {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t h;
    uint8_t l;

    // 0x005901B0 (the constructor; inline here)
    static GridDims Make(uint8_t x, int8_t y, uint8_t z, uint8_t w, uint8_t h, uint8_t l) { return {x, y, z, w, h, l}; }
    GridDims* Set(uint8_t x_, int8_t y_, uint8_t z_, uint8_t w_, uint8_t h_, uint8_t l_)
    {
        x = x_;
        y = y_;
        z = z_;
        w = w_;
        h = h_;
        l = l_;
        return this;
    }
    // 0x005901E0: byte `index` (x y z w h l)
    uint8_t& operator[](int index) { return reinterpret_cast<uint8_t*>(this)[index]; }
    uint8_t* Byte(int index) { return reinterpret_cast<uint8_t*>(this) + index; }
    // 0x005901F0: make the box reach the top of the world (h = 127 - y)
    void MakeInfiniteOnY() { h = static_cast<uint8_t>(0x7F - y); }
    // 0x00590200: every height (-100 up 200)
    void MakeInfinitelyHigh()
    {
        y = -100;
        h = 200;
    }
};
static_assert(sizeof(GridDims) == 6, "passed by value in two stack slots");

// One piece of a metapiece (12 bytes): its cell offset and rotation within the metapiece, and the piece.
struct SMetaDescriptor {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t rot;
    bool isReal;     // +0x04: fills cells (not just a marker)
    uint8_t pad5[3];
    void* piece;     // +0x08: the scene piece (its +0x08 flags); in an abstract metapiece, the piece's name
};
static_assert(sizeof(SMetaDescriptor) == 12, "layout must match the original");

// A metapiece (abstract or concrete; 0x3C bytes). Vtable slot 0: piece `index` (__thiscall); slot 1: the scalar
// deleting destructor; slot 2: add a piece.
class CMetaPiece {
public:
    // The flags (m_flags).
    enum : uint32_t {
        kFlag1 = 0x1,               // (only these are highlighted by an area selection)
        kInPark = 0x4,              // placed in the park (not the cursor's own piece)
        kInnerShell = 0x8,          // part of the floor / shell the park builds itself (risers, walls); is_riser
        kFlag20 = 0x20,             // being moved by a floor change (kept when an area is cleared with flag 4)
        kSector = 0x80,             // a single-piece palette entry (named after its piece, until "name" renames it)
        kFence = 0x100,             // a fence piece of the inner shell
        kCursor = 0x200,            // the cursor's piece
        kNoRails = 0x400,           // no_rails
        kFlag400 = kNoRails,
        kGap = 0x800,               // the gap tool (special_type gap_placement)
        kHighlightable = 0x1000,    // has a gap attached: highlighted while a gap is placed
        kRestartFlag = 0x2000,      // special_type red/green/blue/yellow_flag
        kFloorLocked = 0x4000,      // has a piece flagged 0x0CD75A3F: stops the floor under it being changed
        kAreaSelect = 0x8000,       // the clipboard's area select tool (special_type area_selection)
        kRailPlacement = 0x10000,   // the rail tool (special_type rail_placement)
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
    CMetaPiece* Cast() { return this; }
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
    // 0x00590340 (the game's own, for a test of the descriptors)
    SMetaDescriptor* GetEntry(int index)
    {
        return GameFunction<SMetaDescriptor*(__thiscall*)(CMetaPiece*, int)>(0x00590340)(this, index);
    }
    // 0x005902F0: whether it's one of the restart pieces (by its name)
    bool IsRestartPiece() { return GameFunction<bool(__thiscall*)(CMetaPiece*)>(0x005902F0)(this); }
    // 0x00590400: how many pieces
    int CountPieces() { return GameFunction<int(__thiscall*)(CMetaPiece*)>(0x00590400)(this); }
    // 0x00590AD0: room for `count` pieces
    void Initialize(int count) { GameFunction<void(__thiscall*)(CMetaPiece*, int)>(0x00590AD0)(this, count); }
    // 0x00591AB0: its cell area from its pieces
    void FinishPieces() { GameFunction<void(__thiscall*)(CMetaPiece*)>(0x00591AB0)(this); }
    // 0x005903A0: show or hide its pieces
    void SetVisibility(bool visible) { GameFunction<void(__thiscall*)(CMetaPiece*, bool)>(0x005903A0)(this, visible); }
    // 0x00590CE0: does it take up any of `area` (with `precise`, by its real pieces' cells)?
    bool Intersects(GridDims area, uint32_t precise)
    {
        return GameFunction<bool(__thiscall*)(CMetaPiece*, GridDims, uint32_t)>(0x00590CE0)(this, area, precise);
    }
    // Vtable slot 0: piece `index` (its scene piece)
    CPiece* GetPiece(uint32_t index)
    {
        return reinterpret_cast<CPiece*(__thiscall*)(CMetaPiece*, uint32_t)>(m_vtable[0])(this, index);
    }
    // Vtable slot 1: the scalar deleting destructor
    void Delete() { reinterpret_cast<void(__thiscall*)(CMetaPiece*, int)>(m_vtable[1])(this, 1); }
    // Vtable slot 2: add a piece at `pos`
    void AddPiece(void* piece, GridDims* pos, int rot, bool isReal)
    {
        reinterpret_cast<void(__thiscall*)(CMetaPiece*, void*, GridDims*, int, bool)>(m_vtable[2])(this, piece, pos, rot,
                                                                                                  isReal);
    }
};
static_assert(offsetof(CMetaPiece, m_flags) == 0x0C && offsetof(CMetaPiece, m_desc) == 0x14 &&
                  offsetof(CMetaPiece, mp_extra_desc) == 0x2C && offsetof(CMetaPiece, m_num_used_entries) == 0x34 &&
                  offsetof(CMetaPiece, m_name_checksum) == 0x38 && sizeof(CMetaPiece) == 0x3C,
              "layout must match the original");

// A piece of the scene (what a metapiece is made of).
class CPiece {
public:
    // 0x00590670: put it at `position` (and move its scene object if `update`)
    void SetPosition(Mth::Vector* position, int update)
    {
        GameFunction<void(__thiscall*)(CPiece*, Mth::Vector*, int)>(0x00590670)(this, position, update);
    }
    // 0x00590260: turn it to `rot` quarter turns (and its scene object if `update`)
    void SetRot(int rot, int update) { GameFunction<void(__thiscall*)(CPiece*, int, int)>(0x00590260)(this, rot, update); }
    // 0x005904C0: its size in cells
    void GetCellDims(GridDims* dims) { GameFunction<void(__thiscall*)(CPiece*, GridDims*)>(0x005904C0)(this, dims); }

    uint8_t unknown00[0x28];
    int m_rot; // +0x28
};

// A node of a metapiece list (12 bytes, pooled: the pool pair 0x007CE5F0).
struct CMapListNode {
    int m_dy;              // in the floor change's list: how far the metapiece moves up (down if negative)
    CMetaPiece* mp_meta;   // +0x04
    CMapListNode* mp_next; // +0x08

    // 0x005480C0 (one of many getters folded together there)
    CMetaPiece* GetMeta() const { return mp_meta; }
    CMetaPiece* GetConcreteMeta() { return mp_meta->CastToConcreteMeta(); }

    // A new node from the pools (or the heap), zeroed; null if out of memory.
    static CMapListNode* Create();
    // 0x005914B0: back to the pools (operator delete).
    static void Free(void* node);
    // 0x00591BE0: free a list (the nodes, not the metapieces; null is fine)
    static void __fastcall DestroyList(CMapListNode* list); // (__fastcall: the list in ECX, as the original)
    // 0x00590470: walks the list (a debug check with its asserts compiled out; never ends on a cycle).
    static void VerifyIntegrity(const CMapListNode* list);
};
static_assert(sizeof(CMapListNode) == 12, "pool item");

// The holder of a list returned by the manager's lookups (0x004E1970 makes it, 0x00469490 reads it, 0x00591C60
// frees the list).
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
// The same holder as plain data, for code that frees the list itself (0x00591C60).
struct CMapListTemp {
    CMapListNode* list;
};

// A gap (0x54 bytes; 0x0058CBC0 constructs the two ends): the gap tool's state, and how the gap manager keeps one.
struct GapInfo {
    GridDims m_pos[2];         // +0x00: the cells of its two halves
    int m_rot[2];              // +0x0C (0-3; 4 bits each in the map)
    int m_leftExtent[2];       // +0x14 (0-6 cells)
    int m_rightExtent[2];      // +0x1C
    char m_text[0x20];         // +0x24: its name ("gap", "gap2"...)
    int m_score;               // +0x44
    int m_numCompleteHalves;   // +0x48: how many of its halves are placed (2 for a whole gap)
    int m_tabIndex;            // +0x4C: the gap manager's index (-1 none)
    uint32_t m_cancelFlags;    // +0x50

    // 0x0058CBC0: the halves' cells cleared (the rest isn't touched). Returns this.
    GapInfo* Construct();
    // 0x0058CBF0: the length of half `half` across the gap, in the world's units: its extents and its piece's length
    // (turned 0 / 2) or width (1 / 3); 0 for another turn.
    float GetLength(int half);
};
static_assert(offsetof(GapInfo, m_text) == 0x24 && offsetof(GapInfo, m_numCompleteHalves) == 0x48 &&
                  sizeof(GapInfo) == 0x54,
              "layout must match the original");
using GapDescriptor = GapInfo;

// A metapiece copied into a clipboard (0x18 bytes, pooled: the pool pair 0x007CE5C4; clipboard.cpp): the palette
// piece it was made from, its cell and size where it was copied (raised so the lowest piece of the copy is at 0), its
// turn, and the preview shown at the cursor. `centreX` / `centreZ` below are the copy's centre cell.
struct CClipboardEntry {
    CMetaPiece* mp_meta;            // +0x00: the abstract metapiece
    uint8_t m_x;                    // +0x04
    int8_t m_y;                     // +0x05
    uint8_t m_z;                    // +0x06
    uint8_t pad07;
    int m_rot;                      // +0x08: quarter turns
    uint8_t m_w;                    // +0x0C
    uint8_t m_l;                    // +0x0D
    uint8_t pad0E[2];
    CMetaPiece* mp_preview;         // +0x10: shown at the cursor (kCursor)
    CClipboardEntry* mp_next;       // +0x14

    // 0x0058B800: the cells it would take with the cursor's copy (turned with the cursor; one high, at its height).
    void GetArea(GridDims* area, uint8_t centreX, uint8_t centreZ, CCursor* cursor);
    // 0x0058B940: whether the floor under it can be changed there
    bool CanPlace(uint8_t centreX, uint8_t centreZ, CCursor* cursor);
    // 0x0058B9C0: pasted: the floor raised to its height plus `height` (at most 15; an inner-shell piece's own height
    // on top), then (unless it's of the inner shell) a new metapiece turned with the cursor put there
    void Paste(uint8_t centreX, int height, uint8_t centreZ, CCursor* cursor);
    // 0x0058BB20: the floor height under it
    int GetHeight(uint8_t centreX, uint8_t centreZ, CCursor* cursor);
    // 0x0058BBA0: highlight (or not) the metapieces (not the inner shell's) in its cells
    void HighlightIntersectingMetas(uint8_t centreX, uint8_t centreZ, CCursor* cursor, int on);
    // 0x0058BCA0: its preview (made the first time) placed at `position` turned by `angle` degrees
    void ShowPreview(uint8_t centreX, int height, uint8_t centreZ, Mth::Vector position, float angle);
};
static_assert(offsetof(CClipboardEntry, m_rot) == 0x08 && offsetof(CClipboardEntry, mp_preview) == 0x10 &&
                  sizeof(CClipboardEntry) == 0x18,
              "pool item");

// A copied area (0x18 bytes, pooled: the pool pair 0x007CE5B4; clipboard.cpp): the metapieces (and rails) copied from
// it.
struct CClipboard {
    GridDims m_area;           // +0x00
    uint8_t m_min_x, m_min_z;  // +0x06: the cells its entries cover
    uint8_t m_max_x, m_max_z;  // +0x08
    uint8_t pad0A[2];
    CClipboardEntry* mp_metas; // +0x0C: its entries (the newest first)
    void* mp_rails;            // +0x10: the rail editor's copies of the rails over it (linked by their first word)
    CClipboard* mp_next;       // +0x14: the cursor's list

    // 0x0058BDF0
    CClipboard* Construct();
    // 0x0058C430 (also reached through 0x0058C980): the destructor - its entries (and previews) and rails freed
    void Destroy();
    // 0x005955C0: give an item back to the clipboard pools (operator delete)
    static void Free(void* item);
    // 0x0058B7E0
    bool IsEmpty();
    // 0x0058C590: copy the metapieces wholly in `area` (not restart points or risers below) and its rails; false
    // (and emptied) when the entries' pool is full
    bool CopySelectionToClipboard(GridDims area);
    // 0x0058C270: `meta` added (raised by `dy`), unless the same piece is in it already; false when the pool is full
    bool AddEntry(CMetaPiece* meta, int dy);
    // 0x0058BE30: the floor height under the copy at the cursor (the highest under any entry; 0 without entries)
    int GetHeight(CCursor* cursor);
    // 0x0058BEB0: highlight the metapieces the copy would replace
    void HighlightIntersectingMetas(CCursor* cursor, int on);
    // 0x0058C050: the world position of the middle of its centre cell (at its floor). Returns `centre`.
    Mth::Vector* GetCentre(Mth::Vector* centre);
    // 0x0058C130: show the copy at `position`, turned by `angle` (its rails too)
    void ShowMetas(Mth::Vector position, float angle, int height);
    // 0x0058C200: remove what ShowMetas made
    void DestroyMetas();
    // 0x0058C780: paste the copy at the cursor: false (nothing changed) if the park memory has no room for it, the
    // floor can't be changed under an entry or there aren't enough free rail points
    bool ShowAt(CCursor* cursor);
    // 0x0058BF10: the area it covers placed at the cursor (turned with it)
    void GetArea(CCursor* cursor, GridDims* area);
};
static_assert(sizeof(CClipboard) == 0x18, "pool item");

// A piece set: the entries of one of the piece menu's sets (0x154 bytes, Ed_piece_sets).
struct CPieceSet {
    struct Entry {
        uint32_t checksum;  // the abstract metapiece (or sector)
        const char* name;   // shown in the menu (null: "---")
    };
    uint32_t m_nameChecksum; // +0x000
    const char* m_name;      // +0x004: shown in the set menu (set_name)
    Entry m_entries[40];     // +0x008
    int m_numEntries;        // +0x148
    int m_selected;          // +0x14C
    bool m_visible;          // +0x150: has a menu entry
    bool m_isClipboard;      // +0x151: the clipboard set
    uint8_t pad152[2];
};
static_assert(offsetof(CPieceSet, m_numEntries) == 0x148 && offsetof(CPieceSet, m_visible) == 0x150 &&
                  sizeof(CPieceSet) == 0x154,
              "layout must match the original");
using PieceSet = CPieceSet;

// A cell of the floor (16 bytes).
struct FloorCell {
    int m_height;           // in floor steps (-16 to 15)
    uint32_t m_heightsUsed; // +0x04: bit (y + 17) for each level a piece fills
    bool m_touched;         // +0x08: changed by the floor change under way
    uint8_t pad9[3];
    int m_delta;            // +0x0C: by how much
};
static_assert(sizeof(FloorCell) == 16, "grid cell");

// The park as saved (0x3A98 bytes): a header, the floor heights, then the metapieces (8 bytes each: name index,
// cell, height, turn) and the gaps (0x30 bytes each).
struct CompressedMap {
    uint32_t m_checksum;     // +0x00: CRC of everything after it
    uint16_t m_size;         // +0x04: bytes used
    uint16_t m_version;      // +0x06: kVersion (gaps 0x30 bytes); older parks have 0x2C-byte gaps
    uint16_t m_shellSize;    // +0x08: the park manager's m_shellSize (the shell model, the bounds)
    uint16_t unknown0A;
    uint8_t m_x, m_z, m_w, m_l; // +0x0C: the park's bounds
    uint16_t m_numMetas;     // +0x10
    uint16_t m_numGaps;      // +0x12
    uint8_t m_maxPlayers;    // +0x14
    uint8_t pad15[3];
    uint32_t m_theme;        // +0x18: the park editor's (+0x78); 0 reads as "Default"
    char m_name[0x40];       // +0x1C
    int8_t m_floorHeights[kFloorSize * kFloorSize]; // +0x5C
    uint8_t m_rest[0x3A98 - 0x5C - kFloorSize * kFloorSize];

    static constexpr uint16_t kVersion = 20004;
};
static_assert(offsetof(CompressedMap, m_numMetas) == 0x10 && offsetof(CompressedMap, m_name) == 0x1C &&
                  offsetof(CompressedMap, m_floorHeights) == 0x5C && sizeof(CompressedMap) == 0x3A98,
              "layout must match the original");

// The gap manager (the singleton 0x007CE5D4; the park manager keeps it at +0x177C4): the gaps placed in the park.
class CGapManager {
public:
    static CGapManager*& Instance() { return GameVariable<CGapManager*>(0x007CE5D4); }

    // --- gap_slots.cpp (0x0058C990-0x0058CD3F) ---
    // 0x0058CBB0 (the getter the game calls)
    static CGapManager* sInstance();
    // 0x0058CA10: the gap `meta` is an end of (its slot into `index`, if given)
    bool GetGapIndex(CMetaPiece* meta, int* index);
    // 0x0058CA60: the trigger name ("EditorTrgGap<slot>") of the gap end whose trigger piece is `piece` (0 if none);
    // with the script integer PrintGaps, an odd slot's gap is printed
    uint32_t GetGapTriggerName(CPiece* piece);
    // 0x0058CB20: slot `index`'s gap info (null if empty; its name copied into the text first), and which end (`half`)
    GapInfo* GetGapInfo(int index, int* half);
    // 0x0058CB70: the gap shown: both ends' trigger pieces highlighted
    void SetCurrentGap(int index);
    // 0x0058C990: a free slot (-1 none)
    int GetFreeGapIndex();
    // 0x0058C9B0: gap `index`'s second end in a free slot (none free: nothing), given the next GapID
    void EndGap(void* piece, CMetaPiece* meta, int index);
    // 0x0058CCC0: the constructor (no gaps, the counter at 2000, in `manager`'s park; makes itself the instance).
    // Returns this.
    CGapManager* Construct(CParkManager* manager);

    // 0x0058CDA0 (gap_manager.cpp): gap `index`'s info (its name, the rest copied); the checklist made again
    void SetGapInfo(int index, GapInfo* info);
    // 0x0058D070 (gap_manager.cpp): the gap's first end (its trigger piece and metapiece)
    void StartGap(void* piece, CMetaPiece* meta, int index);
    // 0x0058CDF0 (gap_manager.cpp): remove gap `index` (its trigger pieces deleted; with `forget`, also from the
    // goal editor's gaps)
    void RemoveGap(int index, bool forget);
    // 0x0058CE90: remove every gap
    void RemoveAllGaps();
    // 0x0058CF30: a copy of `other`'s gaps (without their pieces; no park manager)
    void CopyFrom(CGapManager* other);
    // 0x0058CF20: the destructor (also clears the singleton)
    void Destroy();

    // --- gap_manager.cpp (0x0058CD40-0x0058D290) ---
    // One gap (0xEC bytes, made by StartGap / EndGap): the ids of its two ends, their trigger pieces and
    // metapieces, its name and its GapInfo.
    struct Gap {
        uint32_t m_ids[2];        // +0x00: each end's GapID (the manager's counter when it was made)
        CPiece* mp_pieces[2];     // +0x08: the trigger pieces
        CMetaPiece* mp_metas[2];  // +0x10
        char m_name[0x80];        // +0x18: the text of the gap's EndGap
        GapInfo m_info;           // +0x98
    };
    static constexpr int kMaxGaps = 32;

    // 0x0058CD40: the current level's gap checklist made again from the gaps (each never landed)
    void UpdateChecklist();
    // 0x0058CEC0: `piece`'s trigger length (cells) from the gap it's an end of (0 if none)
    void SetTriggerLength(CPiece* piece);
    // 0x0058D120: run StartGap / EndGap on `script`'s object for gap `index`'s two ends (GapID, score, text; the
    // start also with combined_flags), the end placed first first
    void FireGap(int index, Script::CScript* script);

    uint32_t m_nextId;               // +0x00: the next end's GapID
    Gap* mp_gaps[kMaxGaps];          // +0x04
    bool m_startedFirst[kMaxGaps];   // +0x84: the gap's first half (StartGap) was placed first
    CParkManager* mp_manager;        // +0xA4: the park it's in (null in a copy)
};
static_assert(sizeof(CGapManager::Gap) == 0xEC && offsetof(CGapManager::Gap, m_info) == 0x98 &&
                  offsetof(CGapManager, mp_gaps) == 4 && offsetof(CGapManager, m_startedFirst) == 0x84 &&
                  offsetof(CGapManager, mp_manager) == 0xA4 && sizeof(CGapManager) == 0xA8,
              "layout must match the original");

// The park generator (the scene side): what's used of it here.
class CParkGenerator {
public:
    // --- park_scene.cpp, park_nodes.cpp (0x00592740-0x00595030; the data: park_scene.h) ---
    // 0x005933F0: take away the restart point of type `player` at `area` (its cell x, y, z)
    void RemoveRestart(GridDims* area, int player);
    // 0x005935F0: mark a piece for CleanUpOutRefs (taken out at once if it has ClonedPiece::kDestroyNow)
    void DestroyPiece(void* piece);
    // 0x00593610: take out and delete the pieces marked by DestroyPiece
    void CleanUpOutRefs();
    // 0x005947D0: every piece's sector deleted and cloned again (the node array dropped); 0x00594910: those of the
    // pieces whose type has SourcePiece::kRebuilt
    void RebuildAll();
    void RebuildMarked();
    // 0x00593460: the constructor (no pieces, restarts or rails; 2 players). Returns this.
    CParkGenerator* Construct();
    // 0x00592FE0: light `index` at `position`
    void SetLightPosition(const Mth::Vector* position, int index);
    // 0x00593010: `rgba`'s red, green and blue (0-255) lit at `position`: the ambient part plus the falloff part of
    // the lights' sum of 1 / (160000 + squared distance in x and z); over 255 is 255.
    void CalculateLighting(const Mth::Vector* position, uint8_t* rgba);
    // 0x00594410: a piece's vertex colours (from its source's) and its collision's vertex intensities lit
    void LightPiece(CPiece* piece);
    // 0x00593370: how many restart points of `type` there are
    int CountRestarts(int type);
    // 0x00593500: a gap's trigger piece (the type 0xC0341DF7) at `position` turned `rot` quarter turns, added
    void* CreateGapPiece(Mth::Vector* position, float length, int rot);
    // 0x00594730: a piece's sector cloned from its source into the scene, placed, lit (a gap piece told the gap
    // manager)
    void AddPieceToScene(CPiece* piece);
    // 0x00594610: every piece taken out and deleted (with the rail editor's rails taken out of the scene first),
    // then every source piece deleted
    void UnloadPieces();
    // 0x00594C90: the source pieces from the "sk5ed" scene's sectors (their sizes from their boxes), the "cloned"
    // scene, the bounds; the restart points freed (the argument unused)
    void LoadSourcePieces(int unused);
    // 0x00594FD0: LoadSourcePieces, then the source pieces' flags and rails from the pieces' node array (the
    // rails on the top-down heap for the park editor's own level 0xCD3682C1)
    void LoadPieces(uint32_t level, int unused);
    // 0x00592A10: the point of the placed pieces' rails nearest to `position` (and the squared distance); false if
    // none is nearer than 1e8 squared units
    bool FindNearestRailPoint(const Mth::Vector* position, Mth::Vector* point, float* distanceSquared);
    // 0x00592AB0: the node array's sizes: [0] nodes, [1] rail points, [2] rail segments, [3] / [4] memory
    // estimates, [5] rail points again
    void GetNodeCounts(int* counts);
    // 0x00592C10: a node array of the goal editor's nodes only, the node manager's
    void BuildGoalNodeArray();
    // 0x00592C90: the node array dropped from the node manager and the symbol table
    static void RemoveNodeArray();
    // 0x00592CD0: the placed pieces' rails (m_rails) made from the source rails of the metapieces in `metas`
    void BuildRails(CMapListNode* metas);
    // 0x00592ED0 / 0x005949F0 / 0x00593AC0: the pieces', the rails', the restart points' nodes written into `nodes`
    // from node `*index` on (`*index` counts them)
    void WritePieceNodes(Script::CArray* nodes, int* index);
    void WriteRailNodes(Script::CArray* nodes, int* index);
    void WriteRestartNodes(Script::CArray* nodes, int* index);
    // 0x00592740: the name of the next LevelGeometry node from `*index` on (0 at the end); `*index` past it
    uint32_t NextGeometryNode(Script::CArray* nodes, int* index);
    // 0x005927D0: the next unused rail node of the piece `geometry` (after the node named `previous`, or a first
    // one: one with no links, else a linked one - `*isLoop` set); its name (0 if none)
    uint32_t NextRailNode(Script::CArray* nodes, uint32_t geometry, uint32_t previous, RailNodeInfo* info, bool* isLoop);
    // 0x005936E0: the source rails from the pieces' node array (each source piece's rail point and segment counts)
    void BuildSourceRails(Script::CArray* nodes);
    // 0x00592290: show or hide the highlight of every piece
    void SetPiecesHighlighted(int on) { GameFunction<void(__thiscall*)(CParkGenerator*, int)>(0x00592290)(this, on); }
    // 0x00591D20 (sets +0x20)
    void SetMaxPlayers(int count) { GameFunction<void(__thiscall*)(CParkGenerator*, int)>(0x00591D20)(this, count); }
    // +0x20: the most players the park is set up for (a whole int; the compressed map keeps its low byte)
    int GetMaxPlayers() { return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 0x20); }
    // 0x00591D30: the most players the park has room for (from the park memory's use and MaxPlayers)
    int GetMaxPlayersPossible() { return GameFunction<int(__thiscall*)(CParkGenerator*)>(0x00591D30)(this); }
    // 0x00591C70: the destructor
    void Destroy() { GameFunction<void(__thiscall*)(CParkGenerator*)>(0x00591C70)(this); }
    // 0x00592550: a placed piece into the generator's list (LightPiece, 0x00594410, then lights it)
    void AddPiece(CPiece* piece) { GameFunction<void(__thiscall*)(CParkGenerator*, CPiece*)>(0x00592550)(this, piece); }
    // 0x00591D90: a piece type's source (loaded the first time); 0x005923C0: a new piece from a source
    void* GetPieceSource(uint32_t name) { return GameFunction<void*(__thiscall*)(CParkGenerator*, uint32_t)>(0x00591D90)(this, name); }
    void* CreatePiece(void* source, uint32_t flags)
    {
        return GameFunction<void*(__thiscall*)(CParkGenerator*, void*, uint32_t)>(0x005923C0)(this, source, flags);
    }
    // 0x00593200: a restart point of `type` at `position` (the cells `area`) in the first free slot of its type (or,
    // when `automatic`, one put down automatically before); with `automatic` and `keepPlaced`, where a placed one
    // of the type is. False when there's no room.
    bool AddRestart(Mth::Vector* position, int rot, GridDims* area, int type, int automatic, uint32_t keepPlaced);
    // 0x005933D0: whether there are fewer than `count` restart points of `type`
    bool NeedsRestarts(int type, int count);
    // 0x00591FE0: forget the restart points put down automatically; 0x00591FC0: all of them
    void ClearAutomaticRestarts() { GameFunction<void(__thiscall*)(CParkGenerator*)>(0x00591FE0)(this); }
    void ClearRestarts() { GameFunction<void(__thiscall*)(CParkGenerator*)>(0x00591FC0)(this); }
    // 0x00594EA0: the level's node array from the park (the pieces, their rails, the rail editor's, the restart
    // points, the goal editor's), given to the node manager and parsed
    void BuildNodeArray(CMapListNode* metas);
    // 0x005920D0: the park's bounding boxes
    void UpdateBounds(uint32_t value) { GameFunction<void(__thiscall*)(CParkGenerator*, uint32_t)>(0x005920D0)(this, value); }
    // 0x00593580: delete the scene's pieces (`keepRails` 0: the rail editor's rails go out of the scene too)
    void DestroyPieces(int keepRails);
    // 0x00592090: rebuild the collision (when allowed)
    void RebuildCollision() { GameFunction<void(__thiscall*)(CParkGenerator*)>(0x00592090)(this); }
    // 0x00594C70: UnloadPieces, and the source rails freed (the argument unused)
    void CleanUp(int type);
};

// What RebuildFloorPiece is asked (0x1C bytes): the cell and its floor height; it says where the top piece went.
struct FloorPieceRequest {
    int x;
    int y; // (out) the top piece's height
    int z;
    int unknownC;
    int unknown10;
    int index; // height - 1: the riser_above%d piece
    int height;
};

class CParkManager {
public:
    // m_flags: the compressed map's state (meanings partly guessed from where they're set)
    enum : uint32_t {
        kMapValid = 0x1,         // built from the park (kMapUpToDate)
        kMapInSync = 0x2,        // a park is in it, its header readable (kMapLoaded)
        kMapNewerThanPark = 0x4, // came from outside: memory card, network (kMapSaved)
        kMapNotSavedLocal = 0x8,
        kMapUpToDate = kMapValid,
        kMapLoaded = kMapInSync,
        kMapSaved = kMapNewerThanPark,
        kFlag8 = kMapNotSavedLocal,
    };
    static constexpr int kMaxSets = 40;
    static constexpr int kNumRestartPieces = 8;

    // The one instance.
    static CParkManager*& Instance() { return GameVariable<CParkManager*>(0x007CE604); }
    // 0x00595200 (the getter the game calls)
    static CParkManager* sInstance();

    // --- park_accessors.cpp (0x00595030-0x00595D40) ---
    // 0x00595030: the compressed map; `markSaved` clears kMapNotSavedLocal.
    CompressedMap* GetCompressedMapBuffer(bool markSaved);
    // 0x00595050: take a whole compressed map (loaded or received).
    void SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved);
    // 0x00595090: the compressed map is valid and saved.
    bool IsMapSavedLocally();
    // 0x005950B0: the compressed map's shell size (0 when it isn't readable).
    int GetShellSize();
    // 0x005950D0: the compressed map's park name (null if it isn't a map of this version).
    const char* GetParkName();
    // 0x005950F0
    void SetParkName(const char* name);
    // 0x00595140: update the park's geometry: 0 takes out removed pieces, 1 rebuilds it all, 2 rebuilds the
    // pieces marked for it.
    void RebuildParkGeometry(int how);
    // 0x00595170: the checksum of the park's shell model ("sk5ed_shell" or "sk5ed<size+1>_shell").
    uint32_t GetShellChecksum();
    // 0x005951E0
    CMapListNode* GetMapListNode(int x, int z);
    // 0x00595210: piece set `index`; `menuIndex` gets how many sets before it are shown.
    CPieceSet* GetPieceSet(int index, int* menuIndex);
    // 0x00595250: header fields of a compressed map.
    void GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* shellSize, uint32_t* theme,
                    int* width, int* length);
    // 0x005952B0: undo the marked floor changes (clamping every height to -16..15)
    void RestoreFloorHeights();
    // 0x00595300: which restart piece (1 to 8) has this name; 0 if none.
    int GetRestartIndex(uint32_t name);
    // 0x00595620: the palette metapiece with this name; `index` gets its position counted from the end.
    CMetaPiece* GetAbstractMetaPiece(uint32_t name, int* index);
    // 0x00595990: is there memory for the park grown or shrunk to `area` (w x l)?
    bool CanResizeTo(GridDims area);
    // 0x005959D0: the highest floor in `area`; `isFlat` (if given) says whether it's all one height
    int GetFloorHeight(GridDims area, bool* isFlat);
    // 0x00595A90: the world position of `area`'s corner (w = 1), and its size (if given)
    Mth::Vector GridCoordinatesToWorld(const GridDims& area, Mth::Vector* size);
    Mth::Vector* GridDimsToWorld(Mth::Vector* out, GridDims* area, Mth::Vector* size)
    {
        *out = GridCoordinatesToWorld(*area, size);
        return out;
    }
    // 0x00595B30: whether `area` overlaps one of the blocked areas (the road mask)
    bool IsAreaBlocked(GridDims area);
    // 0x00595BC0: a copy of the gap manager in use (on the script heap); the one in use stays current
    void CreateGapManagerCopy();
    // 0x00595C70: make the copy the gap manager in use
    void UseGapManagerCopy();
    // 0x00595C80
    void DestroyGapManagerCopy();
    // 0x00595CB0: highlight the pieces a gap can attach to (and, when highlighting, make `gap` current)
    void HighlightGapPieces(bool on, GapInfo* gap);
    // 0x00595D40: clear the marks (and every metapiece's kFlag20)
    void ClearFloorChangeMarks();
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

    // --- park_lists.cpp: the map of metapieces ---
    // 0x00595670: a new concrete metapiece (on the park heap) made from the palette's `source`, its pieces made by
    // the generator with `flags`; added to the list of concrete metapieces
    CMetaPiece* CreateConcreteMeta(CMetaPiece* source, uint32_t flags);
    // 0x005958B0: is a placed piece (not the inner shell) not wholly inside `area` (x and z)?
    bool HasPiecesOutside(GridDims area);
    // 0x00595DF0: add `meta` to the front of `list` unless it's in it already
    void AddMetaToList(CMetaPiece* meta, CMapListNode** list);
    // 0x00595E80: unlink `meta` from `list`
    void RemoveMetaFromList(CMetaPiece* meta, CMapListNode** list);
    // 0x00595EE0: list `meta` in the cell lists under it
    void AddMetaToCells(CMetaPiece* meta);
    // 0x00595FC0: take `meta` out of the cell lists under it
    void RemoveMetaFromCells(CMetaPiece* meta);
    // 0x00597740: the metapieces intersecting `area` (or, given `shape` turned to `rot` for the moment, those its
    // pieces would touch with its corner at `area`); `skipInnerShell` (its low byte; the whole value is passed on to
    // CMetaPiece::Intersects) leaves out risers and counts only real
    // pieces' cells. A new list the caller frees.
    CMapListNode* GetIntersectingMetas(GridDims area, int rot, CMetaPiece* shape, uint32_t skipInnerShell);
    // 0x00597A00: every metapiece with a piece in a cell of `area` (a new list the caller frees)
    CMapListNode* GetAreaMetaPieces(GridDims area);
    // 0x00597AC0: the gap at the metapiece in cell `pos` (made 1x1x1), and which of its ends (`half`)
    GapInfo* GetGapInfo(GridDims* pos, int* half);
    // 0x005983D0: whether a metapiece over `area` (at any height) has kFloorLocked
    bool HasFloorLockedMeta(GridDims area);
    // 0x005984A0: change the floor cells of `area` (from height `y`: up or down a step, or to `target`; `mode` 1:
    // to the target, 2: up / exactly, 4: every height counts), marking them; the metapieces on it go on `list` and
    // their own areas change with them. False if a cell would leave -16..15.
    bool BuildRiserList(GridDims area, int y, int target, uint32_t mode, CMapListNode** list);
    // 0x00598150: `meta` placed (or taken away): raise the floor under its real pieces to their tops (or lower it
    // to their bottoms where nothing's left above) and mark (or clear) the levels they fill
    void SetMetaInCells(CMetaPiece* meta, int add);

    // --- park_io.cpp: set-up and saving ---
    // 0x005960D0: the palette from Ed_standard_metapieces (single pieces, multiple pieces, and ones made of other
    // palette metapieces), the restart pieces, and a single-piece metapiece for every piece used
    void SetUpAbstractMetas();
    // 0x00596980: the park into the compressed map (unless the map is newer than the park)
    void WriteCompressedMap();
    // 0x00596CD0: the park's bounds (inside: an ed_default_inner_dim square, centred)
    void SetDefaultBounds();
    // 0x00596D50: the blocked areas from Ed_Roadmask (after the first four)
    void SetUpRoadMask();
    // 0x00596E50: the lights from Ed_Light_Info
    void SetUpLights();
    // 0x00596FB0: the memory bar (parked_update_percent_bar park_percent, park_full)
    static void UpdatePercentBar();
    // 0x005975A0
    void WriteCompressedMapIfBuilt();
    // 0x005975B0: the park into `params` (Park_editor_map, Park_editor_goals, the rails, MaxPlayers)
    void WriteIntoStructure(Script::CStruct* params);
    // 0x00597720
    void SetShellSize(int size);
    // 0x005987A0: an empty park in the compressed map
    void ClearCompressedMap();
    // 0x00597BD0: a gap made from `gap` (the gap manager makes its triggers)
    bool AddGap(GapInfo* gap);
    // 0x00597FC0: delete a gap
    void RemoveGap(GapInfo* gap);
    // 0x00591450: debug output of a list (CMapListTemp::PrintContents)
    static void PrintMapList(const CMapListNode* list);

    // --- park_manager.cpp, park_map.cpp, park_floor.cpp (0x00598E00-0x0059C900) ---
    // 0x00598E00: the constructor (an empty park; the generator made). Returns this.
    CParkManager* Construct();
    // 0x00599BF0 / 0x00599C70: make / delete the instance
    static void CreateInstance();
    static void DeleteInstance();
    // 0x00598FC0: a park from a script structure (its goals, rails, player count and map)
    void ReadFromStructure(Script::CStruct* params, bool applyToEditor);
    // 0x00599140: save the park to, or load it from, CustomParks\custom<slot>.prk
    void AccessDisk(bool save, int slot);
    // 0x00599EB0 / 0x0059B750: the park built from the compressed map
    void ReadFromOldCompressedMap();
    void ReadFromCompressedMap();
    // 0x0059BE20: an empty park ("unnamed park")
    void ResetPark();
    // 0x005993E0: the restart points every park has, then the node array
    void GenerateNodeArray();
    // 0x00599A20: put `meta` in the park at `pos` (x, y, z)
    void AddMetaPieceToPark(GridDims pos, CMetaPiece* meta);
    // 0x00599CD0: whether the floor of `area` can be raised or lowered (not with pieces on it; without `uneven`,
    // only where it's flat at area.y)
    bool CanChangeFloorHere(GridDims area, bool uneven);
    // 0x00599E20: delete what's in the layer under `area` (counted in m_riserMetas / m_riserPieces)
    void DestroyMetasBelow(GridDims area);
    // 0x0059B020: delete every placed metapiece (`keepRails` is passed to the generator)
    void DestroyConcreteMetas(int keepRails);
    // 0x0059B090: `meta` deleted and made again (dx, dy, dz) cells away, with its gap. Returns the new one.
    CMetaPiece* RelocateMetaPiece(CMetaPiece* meta, int dx, int dy, int dz);
    // 0x0059B280: put the floor and the moved metapieces back
    void UndoFloorChange();
    // 0x0059B2C0: move each metapiece of `list` by its m_dy
    void MoveRiserList(CMapListNode* list);
    // 0x0059B320: the floor pieces of one cell (risers, the floor, riser_below walls)
    void RebuildFloorPiece(FloorPieceRequest* request);
    // 0x0059BD40: take the park down (the map brought up to date first)
    void Destroy(int keepRails);
    // 0x0059BED0: the park's bounds become `bounds`: what's outside goes, the fence goes round it
    void RebuildInnerShell(GridDims bounds);
    // 0x0059C520: raise or lower (or with `level`, set to `target`) the floor of `area` and what's on it
    bool ChangeFloorHeight(GridDims area, int y, int target, bool raise, bool level);
    // 0x0059C600: the floor pieces of `area` and round it (null: the whole park, after clearing what's outside)
    bool RebuildFloor(GridDims* area, int unused); // (the callers pass 0 as `unused`)
    // 0x00599880: delete the metapieces intersecting `area` (`flags`: 2 not the inner shell, 4 not kFlag20 ones;
    // passed on to DestroyConcreteMeta). Returns the count of metapieces << 16 | the count of their pieces.
    uint32_t DestroyMetasInArea(GridDims area, uint32_t flags);
    // 0x00599960: highlight (or not) the metapieces wholly within `area`
    void HighlightMetasWithinArea(GridDims area, int on);
    // 0x0059AE90: take `meta` out of the park and delete it (the cursor's own piece stays). `flags` 1: also
    // delete what stands on it (2: not the inner shell).
    void DestroyConcreteMeta(CMetaPiece* meta, uint32_t flags);

    // --- src/game/park/park_editor.cpp, park_cursor.cpp (0x0059C900-0x0059E0F7) ---
    // 0x0059CB10: rebuild the whole park: tear down what's built, then (for a new park - `how` 0x1A4E0EF9 - or one
    // not loaded from a save) empty the grid and name it "unnamed park", else rebuild from the saved map; the floor
    // and every piece rebuilt, the scene updated.
    void RebuildPark(uint32_t how);
    // 0x0059CC70: rebuild the pieces in `area` (0x0059C600); with `checkMemory`, false if the park is now over its
    // memory budget.
    bool RebuildArea(const GridDims* area, bool checkMemory);
    // 0x0059CD20: destroy the metapieces in `area` (not the floor's: flag 8) - with `skipRestarts` not restart
    // points, with `onlyWithin` only those wholly inside, with `skipLocked` not those with flag 0x100; the area's
    // scene updated. True (and the area rebuilt) if any went.
    bool DestroyMetasInAreaAndRebuild(GridDims area, bool skipRestarts, bool onlyWithin, bool skipLocked);
    // 0x0059CEC0: flatten `area`: each piece in it (not the floor's) moved down by its height and the floor under
    // it set to 0, then the area's floor; rebuilt.
    void ClearArea(GridDims area);
    // 0x0059D080: raise (dir > 0) or lower the floor in `area`; if it won't fit in memory it's undone. True if done.
    bool ChangeFloorHeight(GridDims area, int dir);
    // 0x0059DCD0: set up once for `level`: the park memory (Init), an empty grid, the park's map (0x0058CCC0), the
    // generator, the whole park built, and the palette's piece sets from Ed_piece_sets / Ed_standard_metapieces.
    void Initialize(uint32_t level);

    // Offset accessors (src/game/park).
    uint8_t* Bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& At(uint32_t offset) { return *reinterpret_cast<T*>(Bytes() + offset); }
    void*& Generator() { return At<void*>(0x0); }       // the park's piece generator
    bool& FloorBuilt() { return At<bool>(0x5); }
    GridDims& ParkDims() { return At<GridDims>(0x3520); }
    uint32_t& Flags() { return At<uint32_t>(0x17728); }
    int& NumSets() { return At<int>(0x13C8C); }
    // A grid cell (16 bytes): +0 the floor height there.
    uint32_t& CellHeight(int x, int z) { return At<uint32_t>(0x352C + (x * kGridSize + z) * 0x10); }

    CParkGenerator* mp_generator;        // +0x00
    bool m_parkBuilt;                    // +0x04: the park is built (its changes go into the map when asked)
    bool m_05;                           // +0x05: the floor is built
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
    int m_numAbstractMetas;              // +0x88
    CMapListNode* m_cellMetas[kFloorSize][kFloorSize]; // +0x8C: [x][z] the metapieces with pieces in each cell
    int m_shellSize;                     // +0x351C: picks the shell model (sk5ed%d_shell) and the bounds' update
    GridDims m_parkBounds;               // +0x3520: the park's area (inside the shell)
    GridDims m_shellBounds;              // +0x3526: everything that may be built on
    FloorCell m_floor[kFloorSize][kFloorSize]; // +0x352C: [x][z]
    CPieceSet m_sets[kMaxSets];          // +0x1076C
    int m_numSets;                       // +0x13C8C
    CompressedMap m_map;                 // +0x13C90
    uint32_t m_flags;                    // +0x17728
    char m_parkName[0x40];               // +0x1772C
    GridDims m_blockedAreas[8];          // +0x1776C: (the first four: the strips round the park; then Ed_Roadmask)
    uint32_t m_numBlockedAreas;          // +0x1779C
    uint32_t unknown177A0;
    uint32_t m_restartPieces[kNumRestartPieces]; // +0x177A4: restart_1, restart_multi, restart_horse, king_of_hill,
                                                 // and the red, green, blue, yellow flags
    CGapManager* mp_gapManager;          // +0x177C4
    CGapManager* mp_gapManagerCopy;      // +0x177C8: see CreateGapManagerCopy

private:
    void ReadCompressedMap(Script::CArray* names, bool current);
};
static_assert(offsetof(CParkManager, m_shellBlocks) == 0x14 && offsetof(CParkManager, mp_concrete_metas) == 0x7C &&
                  offsetof(CParkManager, m_cellMetas) == 0x8C && offsetof(CParkManager, m_shellSize) == 0x351C &&
                  offsetof(CParkManager, m_floor) == 0x352C && offsetof(CParkManager, m_sets) == 0x1076C &&
                  offsetof(CParkManager, m_numSets) == 0x13C8C && offsetof(CParkManager, m_map) == 0x13C90 &&
                  offsetof(CParkManager, m_flags) == 0x17728 && offsetof(CParkManager, m_blockedAreas) == 0x1776C &&
                  offsetof(CParkManager, m_numBlockedAreas) == 0x1779C &&
                  offsetof(CParkManager, m_restartPieces) == 0x177A4 && offsetof(CParkManager, mp_gapManager) == 0x177C4 &&
                  sizeof(CParkManager) == 0x177CC,
              "layout must match the original");
inline CPieceSet* Sets(CParkManager* manager) { return manager->m_sets; }

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
    // 0x00595330 (the getter the game calls)
    static CCursor* sInstance();

    // --- cursor_tools.cpp (0x00595330-0x005989C0) ---
    // 0x00595340: take every clipboard's preview out of the park
    void HideClipboards();
    // 0x00595360: the gap's name ("" makes it "x") and score (unless negative), handed to the gap manager
    void SetGapNameAndScore(const char* name, int score);
    // 0x005953C0: the gap's cancel flags, from the names in `flags` (each a global integer), handed to the gap manager
    void SetGapCancelFlags(Script::CStruct* flags);
    // 0x00595430
    char* GetGapName() { return m_gap.m_text; }
    // 0x00595440: show the current clipboard's preview here
    void ShowClipboard();
    // 0x00595450: hide every preview and make clipboard `index` the current one (none past the end)
    void SelectClipboard(int index);
    // 0x005954A0
    void SetVisibility(bool visible);
    // 0x005954D0: tell the scripts the mode changed (parked_set_helper_text_mode)
    void OnModeChanged();
    // 0x00597100: back to the first corner of the area selection
    void RestartAreaSelection()
    {
        m_areaSelect = 1;
        m_areaHighlighted = false;
    }
    // 0x00597110: the selected area (one level high; all zero without a selection)
    void GetAreaSelectDims(GridDims* out);
    // 0x00597190: delete the oldest clipboard (the last in the list)
    void RemoveOldestClipboard();
    // 0x005971E0: keep the cursor (or the paste) within the park
    void ClampToPark();
    // 0x005988A0: destroy every clipboard (the list head is left as it was)
    void DestroyClipboards();
    // 0x00598940: forget the gap being placed if `meta` is (one of) its pieces
    void ForgetGapAt(CMetaPiece* meta);
    // 0x005989C0: the next step of the area selection: start it at the cursor, or finish it when it's at most
    // 11 x 11; the state after (0 none, 1 first corner set, 2 done)
    int AdvanceAreaSelection();

    // --- cursor.cpp (0x00598A60-0x0059C860) ---
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
    // 0x0059A1F0: one frame: turn by `rotInc` quarter turns, step on the stick (shiftX, shiftZ), slide towards the
    // target and show the piece there
    void Update(float shiftX, float shiftZ, int rotInc);
    // 0x0059A5A0: drop the area selection (and its highlight)
    void ClearAreaSelection();
    // 0x0059A610: whether the selected area can't be copied (bigger than 10 x 10, or the editor says no)
    bool IsAreaSelectionInvalid();
    // 0x0059A6D0: copy the selected area into a new clipboard (the oldest goes when there are 6). False if it
    // can't be copied.
    bool CopyAreaToClipboard();
    // 0x0059A8D0: keep the selected area highlighted as the cursor moves
    void UpdateAreaSelectionHighlight();
    // 0x0059BAC0: delete the cursor's piece
    void DestroyCursorMeta();
    // 0x0059BAF0: paste mode, with the newest clipboard (if there is one)
    void SwitchToClipboard();
    // 0x0059BC00: take the piece `checksum` (an abstract metapiece) and the mode its kind needs
    void ChangePiece(uint32_t checksum);

    // --- src/game/park/park_cursor.cpp, park_editor.cpp (0x0059C900-0x0059E0F7) ---
    // 0x0059D120: the clipboards too big for this park are deleted; the first is current; the set's piece again.
    void RemoveClipboardsTooBig();
    // 0x0059D240: place the cursor's piece (if it fits, is under the height limit of 17 and - for a restart point
    // - has a free slot): a copy of its type, turned, added; rebuilt.
    bool PlacePiece();
    // 0x0059D380: destroy the pieces under the cursor (or the clipboard's area) - not locked ones.
    bool DestroyUnderCursor();
    // 0x0059D410: make the piece under the cursor a half of the gap being made (unless the park is full or it has
    // a gap there already): the first half names the gap ("gap", "gap2"...).
    bool AddGapHalf();
    // 0x0059D600: drop the gap being made, or the gap under the cursor (its pieces looked at again).
    bool RemoveGapHalf();
    // 0x0059D6B0: turn the gap under the cursor (its extents reset) and/or widen its left / right extents (0-6,
    // kept in the park).
    bool ChangeGap(int turn, int left, int right);
    // 0x0059D8E0 / 0x0059D980: destroy the pieces in / flatten the selected area.
    bool DestroySelection();
    void FlattenSelection();
    // 0x0059D9F0: raise / lower `area` (at most 10 x 10) to `height` + dir; undone if out of memory.
    bool RaiseArea(GridDims area, int height, int dir, char level);
    // 0x0059DAA0: raise / lower the cursor's area (the selection, the clipboard's), widened by the pieces on it;
    // not above 17 or below -16.
    bool RaiseLower(int dir);
    // 0x0059C900: delete the cursor (its piece first: no longer the cursor's, destroyed).
    static void __cdecl Destroy();
    // 0x0059C950: the next (dir 1) / previous (-1) piece of the current set, wrapping; a clipboard set selects a
    // clipboard instead.
    void ChangePieceInSet(int dir);
    // 0x0059C9B0: the next / previous visible set (if any); a clipboard set selects its clipboard and drops the
    // cursor's piece (mode 5).
    void ChangeSet(int dir);

    // Offset accessors (src/game/park).
    uint8_t* Bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& At(uint32_t offset) { return *reinterpret_cast<T*>(Bytes() + offset); }
    CParkManager*& Manager() { return At<CParkManager*>(0x0); }
    GridDims& Area() { return At<GridDims>(0x4); }        // the cells under the cursor
    int& Rotation() { return At<int>(0x38); }
    bool& Selecting() { return At<bool>(0x48); }          // an area is selected
    GapInfo& Gap() { return At<GapInfo>(0x70); }          // the gap being made
    uint32_t& GapCounter() { return At<uint32_t>(0xC4); }
    uint8_t*& Meta() { return At<uint8_t*>(0x44); }       // the piece the cursor holds (flag 0x200 while it does)
    uint8_t*& Clipboards() { return At<uint8_t*>(0x58); } // the clipboards (next at +0x14)
    uint8_t*& CurrentClipboard() { return At<uint8_t*>(0x5C); }
    int& SetIndex() { return At<int>(0x64); }
    int& MenuSetNumber() { return At<int>(0x68); }        // the set's place among the visible ones
    int& Mode() { return At<int>(0x6C); }                 // 4: area selection, 5: clipboard

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
    uint8_t m_areaSelect;        // +0x48: the area selection (0 none, 1 first corner set, 2 done)
    GridDims m_areaStart;        // +0x49: its first corner
    GridDims m_areaLast;         // +0x4F: the cursor's cell when the highlight was made (the other corner)
    bool m_areaHighlighted;      // +0x55
    uint8_t pad56[2];
    CClipboard* mp_clipboards;   // +0x58: the newest first
    CClipboard* mp_clipboard;    // +0x5C: the one being pasted
    int m_clipboardHeight;       // +0x60
    int m_clipboardSet;          // +0x64: the clipboard set's index
    int m_menuIndex;             // +0x68
    int m_mode;                  // +0x6C
    GapInfo m_gap;               // +0x70
    uint32_t m_gapCounter;       // +0xC4: names the next gap
};
static_assert(offsetof(CCursor, m_target) == 0x14 && offsetof(CCursor, mp_meta) == 0x44 &&
                  offsetof(CCursor, m_areaStart) == 0x49 && offsetof(CCursor, m_areaHighlighted) == 0x55 &&
                  offsetof(CCursor, mp_clipboards) == 0x58 && offsetof(CCursor, m_mode) == 0x6C &&
                  offsetof(CCursor, m_gap) == 0x70 && sizeof(CCursor) == 200,
              "layout must match the original");

// Park editor script commands (park_cfuncs.cpp).
// 0x0058E120 GetParkEditorMaxPlayers: <MaxPlayers> = the park's player count.
bool __cdecl CFunc_GetParkEditorMaxPlayers(Script::CStruct* params, Script::CScript* script);
// 0x0058E140 GetParkEditorMaxPlayersPossible: <MaxPlayersPossible> = how many players the park has room for.
bool __cdecl CFunc_GetParkEditorMaxPlayersPossible(Script::CStruct* params, Script::CScript* script);

} // namespace Ed
