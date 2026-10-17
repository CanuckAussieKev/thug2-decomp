#pragma once

// The park editor's park (Create-A-Park): a 58 x 58 grid of cells, each with a floor height and a list of the
// metapieces standing on it, the compressed form of the park that's saved and sent online, the piece sets, and
// the road mask. One instance (CParkManager::sInstance, 0x007CE604), 0x177CC bytes, built by 0x00598E00.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Ed {

// An area of cells: position and size (x and z across the floor, y up in steps of a floor level).
struct GridDims {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t h;
    uint8_t l;

    // 0x005901B0 (returns this)
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
    // 0x005901E0: the bytes by index (0 x, 1 y, 2 z, 3 w, 4 h, 5 l).
    uint8_t* Byte(int index) { return reinterpret_cast<uint8_t*>(this) + index; }
    // 0x005901F0: everything from y up to the top of the world.
    void MakeInfiniteOnY() { h = static_cast<uint8_t>(127 - y); }
    // 0x00590200: every height (-100 up 200).
    void MakeInfinitelyHigh()
    {
        y = -100;
        h = 200;
    }
};
static_assert(sizeof(GridDims) == 6, "passed by value in two stack slots");

class CMetaPiece;

// A link in the lists of metapieces (12 bytes, from the pools at 0x007CE5F0).
struct CMapListNode {
    int m_slideAmount; // used by the floor slide (CParkManager::SlideArea)
    CMetaPiece* mp_meta;
    CMapListNode* mp_next;

    // 0x005480C0 (the linker shares it with other one-pointer getters)
    CMetaPiece* GetMeta() const { return mp_meta; }
};
static_assert(sizeof(CMapListNode) == 12, "pool item");

// A metapiece (0x3C bytes): one or more pieces placed as a unit. Abstract ones are the palette's (built from
// Ed_standard_metapieces); concrete ones stand in the park. Only the fields seen so far.
class CMetaPiece {
public:
    void* m_vtable;
    GridDims m_cellArea;
    uint8_t pad0A[2];
    uint32_t m_flags; // kFlag*
    int m_rot;
    uint8_t unknown14[0x38 - 0x14];
    uint32_t m_nameChecksum;

    enum : uint32_t {
        kFlagInPark = 0x4,
        kFlagRiser = 0x8,           // is_riser
        kFlagSlid = 0x20,           // already moved by the current floor slide
        kFlagSingle = 0x80,         // a single piece
        kFlagNoRails = 0x400,       // no_rails
        kFlagGapPlacement = 0x800,  // special_type gap_placement
        kFlagInGap = 0x1000,        // a gap is attached
        kFlagRestart = 0x2000,      // special_type restart flags (red/green/blue/yellow)
        kFlagSpecial4000 = 0x4000,
        kFlagAreaSelection = 0x8000, // special_type area_selection
        kFlagRailPlacement = 0x10000 // special_type rail_placement
    };

    // 0x005902E0: the concrete or abstract metapiece itself (a cast in the original).
    CMetaPiece* Cast() { return this; }
};
static_assert(offsetof(CMetaPiece, m_flags) == 0xC && offsetof(CMetaPiece, m_nameChecksum) == 0x38 &&
                  sizeof(CMetaPiece) == 0x3C,
              "layout must match the original");

// One cell of the floor (16 bytes).
struct FloorHeightEntry {
    int m_height;           // -16 to 15
    uint32_t m_heightsUsed; // bit (y + 17) for each level a piece fills
    bool m_markAsSlid;      // changed by the current floor slide
    uint8_t pad9[3];
    int m_slideAmount;
};
static_assert(sizeof(FloorHeightEntry) == 16, "grid cell");

// A set of pieces in the piece menu (0x154 bytes).
struct CPieceSet {
    uint32_t unknown0;
    char m_name[0x150 - 4];
    bool m_isUsed;
    uint8_t pad151[3];
};
static_assert(sizeof(CPieceSet) == 0x154, "layout must match the original");

// The park as saved (0x3A98 bytes): a header, the floor heights, the metapieces (8 bytes each), the gaps (0x30
// bytes each).
struct CompressedMap {
    uint32_t m_checksum; // CRC of everything after it
    uint16_t m_size;     // bytes used
    uint16_t m_version;  // kVersion
    uint16_t m_theme;
    uint16_t unknownA;
    uint8_t m_x, m_z, m_w, m_l; // the park's inner bounds
    uint16_t m_numMetas;
    uint16_t m_numGaps;
    uint8_t m_maxPlayers;
    uint8_t pad15[3];
    uint32_t m_unknown18; // from the park editor (+0x78); 0 reads as "Default"
    char m_parkName[64];
    uint8_t m_data[0x3A98 - 0x5C];

    static constexpr uint16_t kVersion = 20004;
};
static_assert(offsetof(CompressedMap, m_numMetas) == 0x10 && offsetof(CompressedMap, m_parkName) == 0x1C &&
                  sizeof(CompressedMap) == 0xEA6 * 4,
              "layout must match the original");

class CParkManager {
public:
    static constexpr int kFloorMaxWidth = 58;
    static constexpr int kFloorMaxLength = 58;
    static constexpr int kMaxSets = 40;
    static constexpr int kNumSpecialPieces = 8;
    enum : uint32_t {
        kMapValid = 0x1,         // built from the park
        kMapInSync = 0x2,        // header readable (theme)
        kMapNewerThanPark = 0x4, // came from outside (memory card, network)
        kMapNotSavedLocal = 0x8,
    };

    // 0x00595200
    static CParkManager* sInstance();

    // 0x00595030: the compressed map; `markSaved` clears kMapNotSavedLocal.
    CompressedMap* GetCompressedMapBuffer(bool markSaved);
    // 0x00595050: take a whole compressed map (loaded or received).
    void SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved);
    // 0x00595090: the compressed map is valid and saved.
    bool IsMapSavedLocally();
    // 0x005950B0: the compressed map's theme (0 when it isn't readable).
    int GetTheme();
    // 0x005950D0: the compressed map's park name (null if it isn't a map of this version).
    const char* GetParkName();
    // 0x005950F0
    void SetParkName(const char* name);
    // 0x00595140: update the park's geometry: 0 takes out removed pieces, 1 rebuilds it all, 2 rebuilds the
    // pieces marked for it.
    void RebuildParkGeometry(int how);
    // 0x00595170: the checksum of the park's shell model ("sk5ed_shell" or "sk5ed<theme+1>_shell").
    uint32_t GetShellChecksum();
    // 0x005951E0
    CMapListNode* GetMapListNode(int x, int z);
    // 0x00595210: piece set `index`; `usedBefore` gets how many sets before it are used.
    CPieceSet* GetPieceSet(int index, int* usedBefore);
    // 0x00595250: header fields of a compressed map.
    void GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* theme, uint32_t* unknown18,
                    int* width, int* length);
    // 0x005952B0: undo the floor slide on the cells it marked; clamp every height to -16..15.
    void UndoFloorSlide();
    // 0x00595300: which special piece (1 to 8) has this name; 0 if none.
    int GetSpecialPieceIndex(uint32_t name);
    // 0x00595620: the palette metapiece with this name; `index` gets its position counted from the end.
    CMetaPiece* GetAbstractMetaPiece(uint32_t name, int* index);
    // 0x00595990: is there memory for the park grown or shrunk to `area` (w x l)?
    bool CanResizeTo(GridDims area);
    // 0x005959D0: the highest floor under `area`; `uniform` (optional) says whether all of it is level with
    // its first cell.
    int GetFloorHeight(GridDims area, bool* uniform);
    // 0x00595A90: the world position of a cell's corner (cells are 120 x 48 x 120, the grid centred on 29, 29);
    // `dimensions` (optional) gets the size of `area` in world units.
    Mth::Vector GridCoordinatesToWorld(const GridDims& area, Mth::Vector* dimensions);
    // 0x00595B30: does `area` overlap a rectangle of the road mask?
    bool IntersectsRoadMask(GridDims area);
    // 0x00595D40: clear the floor slide's marks inside the park, and every placed piece's kFlagSlid.
    void ResetSlideMarks();

    void* mp_generator; // CParkGenerator
    uint8_t unknown4[0x7C - 4];
    CMapListNode* mp_concreteMetaList;
    CMapListNode* mp_abstractMetaList;
    int m_numConcreteMetas;
    int m_numAbstractMetas;
    CMapListNode* mp_bucketList[kFloorMaxWidth][kFloorMaxLength];
    int m_theme;
    GridDims m_parkNearBounds; // inside the shell
    GridDims m_parkFarBounds;  // everything that may be built on
    FloorHeightEntry m_floor[kFloorMaxWidth][kFloorMaxLength];
    CPieceSet m_sets[kMaxSets];
    int m_numSets;
    CompressedMap m_compressedMap;
    uint32_t m_compressedMapFlags; // kMap*
    char m_parkName[64];
    GridDims m_roadMask[8];
    uint32_t m_roadMaskLength;
    uint32_t unknown177A0;
    uint32_t m_specialPieceNames[kNumSpecialPieces]; // restart_1, restart_multi, restart_horse, king_of_hill,
                                                     // red, blue, green, yellow flags
    uint32_t unknown177C4;
    void* mp_gapManager; // CGapManager
};
static_assert(offsetof(CParkManager, mp_bucketList) == 0x8C && offsetof(CParkManager, m_theme) == 0x351C &&
                  offsetof(CParkManager, m_floor) == 0x352C && offsetof(CParkManager, m_sets) == 0x1076C &&
                  offsetof(CParkManager, m_numSets) == 0x13C8C && offsetof(CParkManager, m_compressedMap) == 0x13C90 &&
                  offsetof(CParkManager, m_compressedMapFlags) == 0x17728 &&
                  offsetof(CParkManager, m_roadMask) == 0x1776C && offsetof(CParkManager, m_roadMaskLength) == 0x1779C &&
                  offsetof(CParkManager, m_specialPieceNames) == 0x177A4 &&
                  offsetof(CParkManager, mp_gapManager) == 0x177C8 && sizeof(CParkManager) == 0x177CC,
              "layout must match the original");

} // namespace Ed
