#pragma once

#include <cstddef>
#include <cstdint>

// The park editor's manager and cursor (Ed::CParkManager at 0x007CE604, Ed::CCursor at 0x007CE608): the parts at
// 0x0059C900-0x0059E0F7. Most of both classes isn't decompiled yet: their fields are reached by offset.
namespace Ed {

// A box of grid cells (6 bytes, passed by value in 8): its corner and its size. The park is 58 x 58 cells.
struct GridDims {
    uint8_t x;
    int8_t y; // the height (signed)
    uint8_t z;
    uint8_t width;
    uint8_t height;
    uint8_t length;
};
static_assert(sizeof(GridDims) == 6, "layout must match the original");

constexpr int kGridSize = 58;

class CParkManager;

// A list node of the park's meta pieces (CMapListNode, 12 bytes, pooled): +4 the meta piece, +8 the next node.
struct CMapListNode {
    void* unknown0;
    void* meta;
    CMapListNode* next;
};

// CMapListTemp: owns a list of nodes (0x00591C60 destroys them).
struct CMapListTemp {
    CMapListNode* list;
};

class CParkManager {
public:
    static CParkManager* Instance(); // 0x007CE604

    // 0x0059CB10: rebuild the whole park: tear down what's built, then (for a new park - `how` 0x1A4E0EF9 - or one
    // not loaded from a save) empty the grid and name it "unnamed park", else rebuild from the saved map; the
    // floor and every piece rebuilt, the scene updated.
    void RebuildPark(uint32_t how);
    // 0x0059CC70: rebuild the pieces in `area` (0x0059C600); with `checkMemory`, false if the park is now over its
    // memory budget.
    bool RebuildArea(const GridDims* area, bool checkMemory);
    // 0x0059CD20: destroy the meta pieces in `area` (not the floor's: flag 8) - with `skipRestarts` not restart
    // points, with `onlyWithin` only those wholly inside, with `skipLocked` not those with flag 0x100; the
    // area's scene updated. True (and the area rebuilt) if any went.
    bool DestroyMetasInArea(GridDims area, bool skipRestarts, bool onlyWithin, bool skipLocked);
    // 0x0059CEC0: flatten `area`: each piece in it (not the floor's) moved down by its height and the floor under
    // it set to 0, then the area's floor; rebuilt.
    void ClearArea(GridDims area);
    // 0x0059D080: raise (dir > 0) or lower the floor in `area`; if it won't fit in memory it's undone. True if done.
    bool ChangeFloorHeight(GridDims area, int dir);
    // 0x0059DCD0 (park_cursor.cpp): set up once for `level`: the park memory (Init), an empty grid, the park's map
    // (0x0058CCC0), the generator, the whole park built, and the palette's piece sets from Ed_piece_sets /
    // Ed_standard_metapieces.
    void Initialize(uint32_t level);

    uint8_t* Bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& At(uint32_t offset) { return *reinterpret_cast<T*>(Bytes() + offset); }

    void*& Generator() { return At<void*>(0x0); }       // the park's piece generator
    bool& FloorBuilt() { return At<bool>(0x5); }
    GridDims& ParkDims() { return At<GridDims>(0x3520); }
    uint32_t& Flags() { return At<uint32_t>(0x17728); } // bit 0: a new park, 1: loaded, 2: changed, 3: named
    int& NumSets() { return At<int>(0x13C8C); }
    // A grid cell (16 bytes): +0 the floor height there.
    uint32_t& CellHeight(int x, int z) { return At<uint32_t>(0x352C + (x * kGridSize + z) * 0x10); }
};

// A gap being made or changed (the gap manager's descriptor, 0x54 bytes): its two halves' cells, turns and extents.
struct GapDescriptor {
    GridDims loc[2];       // +0x00
    int rot[2];            // +0x0C (0-3)
    int leftExtent[2];     // +0x14 (0-6 cells)
    int rightExtent[2];    // +0x1C
    char name[32];         // +0x24 "gap", "gap2"...
    int score;             // +0x44
    int numCompleteHalves; // +0x48
    int id;                // +0x4C
    int unknown50;
};
static_assert(sizeof(GapDescriptor) == 0x54 && offsetof(GapDescriptor, name) == 0x24 &&
                  offsetof(GapDescriptor, numCompleteHalves) == 0x48,
              "layout must match the original");

class CCursor {
public:
    static CCursor*& Instance(); // 0x007CE608

    // (park_cursor.cpp)
    // 0x0059D120: the clipboards too big for this park are deleted; the first is current; the set's piece again.
    void RemoveClipboardsTooBig();
    // 0x0059D240: place the cursor's piece (if it fits, is under the height limit of 17 and - for a restart
    // point - has a free slot): a copy of its type, turned, added; rebuilt.
    bool PlacePiece();
    // 0x0059D380: destroy the pieces under the cursor (or the clipboard's area) - not locked ones.
    bool DestroyUnderCursor();
    // 0x0059D410: make the piece under the cursor a half of the gap being made (unless the park is full or it
    // has a gap there already): the first half names the gap ("gap", "gap2"...).
    bool AddGapHalf();
    // 0x0059D600: drop the gap being made, or the gap under the cursor (its pieces looked at again).
    bool RemoveGapHalf();
    // 0x0059D6B0: turn the gap under the cursor (its extents reset) and/or widen its left / right extents
    // (0-6, kept in the park).
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

    uint8_t* Bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& At(uint32_t offset) { return *reinterpret_cast<T*>(Bytes() + offset); }

    CParkManager*& Manager() { return At<CParkManager*>(0x0); }
    GridDims& Area() { return At<GridDims>(0x4); }        // the cells under the cursor
    int& Rotation() { return At<int>(0x38); }
    bool& Selecting() { return At<bool>(0x48); }          // an area is selected
    GapDescriptor& Gap() { return At<GapDescriptor>(0x70); } // the gap being made
    uint32_t& GapCounter() { return At<uint32_t>(0xC4); }
    uint8_t*& Meta() { return At<uint8_t*>(0x44); }       // the piece the cursor holds (flag 0x200 while it does)
    uint8_t*& Clipboards() { return At<uint8_t*>(0x58); } // the clipboards (next at +0x14)
    uint8_t*& CurrentClipboard() { return At<uint8_t*>(0x5C); }
    int& SetIndex() { return At<int>(0x64); }
    int& MenuSetNumber() { return At<int>(0x68); } // the set's place among the visible ones
    int& Mode() { return At<int>(0x6C); }          // 4: area selection, 5: clipboard
};

// A palette set of the park manager (0x154 bytes, from +0x1076C; Ed_piece_sets): its name, pieces, selection.
struct PieceSet {
    uint32_t nameChecksum;  // +0x000
    const char* name;       // +0x004
    struct {
        uint32_t type;      // the meta piece's checksum
        const char* textName;
    } entries[40];          // +0x008
    int numEntries;         // +0x148
    int selected;           // +0x14C
    bool visible;           // +0x150 (not "hidden")
    bool isClipboardSet;    // +0x151
    uint8_t pad152[2];
};
static_assert(sizeof(PieceSet) == 0x154 && offsetof(PieceSet, numEntries) == 0x148 &&
                  offsetof(PieceSet, visible) == 0x150,
              "layout must match the original");
inline PieceSet* Sets(CParkManager* manager) { return reinterpret_cast<PieceSet*>(manager->Bytes() + 0x1076C); }

} // namespace Ed
