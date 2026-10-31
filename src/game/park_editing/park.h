#pragma once

// The park editor (Create-A-Park): the park manager (park_editing::ParkLayout, the one instance at 0x007CE604) holds the
// park being edited - the metapieces placed on a 58 x 58 grid of cells, the floor heights, the piece sets the
// menus show and the compressed map that is saved; the cursor (park_editing::BuildPointer, 0x007CE608) is what the player moves
// over the grid; the upper menu (park_editing::TopBarMenus, 0x007CE60C) shows the pieces. Positions on the grid are
// CellBox (a cell and a size, in cells; y counts floor steps).
//
// The pieces: an abstract metapiece is a kind of piece (the palette, from Ed_standard_metapieces, named by a
// checksum); a concrete metapiece is one placed in the park (or under the cursor), made from an abstract one. Both
// are kept in singly linked lists of CMapListNode, and every cell has a list of the metapieces over it.
//
// One set of types for the three parts written separately (src/game/park_editing: the park manager's and the cursor's
// 0x00595030-0x0059C900; src/game/park: 0x0059C900-0x0059E0F7, which still reaches some fields through the
// offset accessors Bytes() / At<T>()).

#include <cstddef>
#include <cstdint>

#include "game/park_editing/park_rails.h"
#include "engine/maths/vec4.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace park_editing {

class ParkAssembler; // the scene side of the park (0x4A4 bytes, 0x00593460 constructs it)
class BuildPointer;
class ParkPiece;
class ParkTile;
class PieceGroup;
class TileTemplate;

class ParkLayout;
struct RailNodeInfo;

// The grid's cells in the world (the exe's constants): 120 x 48 x 120, the 58 x 58 grid centred on the origin.
constexpr float kGridPitch = 120.0f;     // 0x0064F41C
constexpr float kGridStepUp = 48.0f;     // 0x0064F420
constexpr float kGridStepAlong = 120.0f; // 0x0064F424
constexpr float kGridCells = 58.0f;      // 0x0064F7A8
constexpr float kRiserBelowDrop = 1.5f;  // 0x00646744
constexpr int kFloorSize = 58;           // the grid is 58 x 58 cells
constexpr int kGridSize = kFloorSize;

// Progress markers through the park's rebuilding, compiled out of this build (the empty function 0x005480A0).
inline void mark_progress(const char* /*label*/) {}

// A box of cells: the corner (x, y, z) and the size (w, h, l). y is signed (floor steps below 0 are dug in).
// Passed by value in two stack slots.
struct CellBox {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t h;
    uint8_t l;

    static CellBox get_make(uint8_t x, int8_t y, uint8_t z, uint8_t w, uint8_t h, uint8_t l)
    {
        return {x, y, z, w, h, l};
    }
    // 0x005901B0 (the constructor): returns this. (park_pieces.cpp, like the rest.)
    CellBox* assign_value(uint8_t cell_x, int8_t cell_y, uint8_t cell_z, uint8_t width, uint8_t height, uint8_t length);
    // 0x005901E0: byte `index` (x y z w h l)
    uint8_t& operator[](int index) { return reinterpret_cast<uint8_t*>(this)[index]; }
    uint8_t* get_byte(int index);
    // 0x005901F0: make the box reach the top of the world (h = 127 - y)
    void stretch_vertically();
    // 0x00590200: every height (-100 up 200)
    void raise_ceiling_to_limit();
    // 0x00590210: debug output ("pos=(x,y,z) dims=(w,h,l)")
    void dump_to_log() const;
};
static_assert(sizeof(CellBox) == 6, "passed by value in two stack slots");

// One piece of a metapiece (12 bytes): its cell offset and rotation within the metapiece, and the piece.
struct PieceGroupDesc {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t rot;
    bool sets_floor_; // +0x04: from the palette entry's flag 0x0CD75A3F: the piece counts as floor - only
                      //        these set the cells' floor heights (set_meta_in_cells), PieceGroup::intersects' precise
                      //        test leaves them out, and their metapiece is kFloorLocked
    uint8_t spare_slot5_[3];
    void* piece; // +0x08: the scene piece (its +0x08 flags); in an abstract metapiece, the piece's name
};
static_assert(sizeof(PieceGroupDesc) == 12, "layout must match the original");

// A piece of the scene (what a metapiece is made of; park_pieces.cpp). Two kinds: a source piece (TileTemplate,
// kSource: a piece type - one sector of the "sk5ed" scene, its name and size) and a placed piece (ParkTile, made a
// CopiedPiece: a clone of its source's sector in the "cloned" scene; park_scene.h calls it DuplicatedTile). Both
// start with the sector and the flags. (The original's vtables: this base's 0x0064F3B4, the source's 0x0064F3C8,
// the clone's 0x0064F3D0.)
class ParkPiece : public memory::HeapObject {
public:
    // flag_bits_ (both kinds)
    enum : uint32_t {
        kSource = 0x1, // a TileTemplate
        kCloned = 0x2, // a placed (cloned) piece
    };

    ParkPiece(const ParkPiece&) = delete;
    ParkPiece& operator=(const ParkPiece&) = delete;
    // 0x00590240 (0x00590490 / 0x00590640 the deleting ones): nothing to do.
    virtual ~ParkPiece() = default;
    // Its size (a placed piece's turned by its rotation); returns `out`.
    virtual maths::Vec4* measure_span(maths::Vec4* out) = 0;

    // 0x005904C0: its size in cells (w h l; the rest of `dims` as it was)
    void get_footprint_size(CellBox* dims);
    // 0x00590530: its source's size with x and z swapped for an odd `rot`; returns `out`
    maths::Vec4* get_dims_turned(maths::Vec4* out, int rot);

    uint32_t sector_ = 0;    // +0x04: a placed piece's sector in the "cloned" scene; a source's sector checksum
    uint32_t flag_bits_ = 0; // +0x08

protected:
    ParkPiece() = default;
};

// A placed piece (0x2C bytes). Its own size is CopiedPiece's.
class ParkTile : public ParkPiece {
public:
    // flag_bits_ (a placed piece's; TileTemplate has its own)
    enum : uint32_t {
        kCursor = 0x4,    // the cursor's (never in the generator's list: not unlinked when deleted)
        kInList = 0x8,    // in the generator's list
        kNoNode20 = 0x20, // (with 0x80) gets no node in the node array
        kGap = 0x40,      // a gap's trigger piece
        kNoNode80 = 0x80,
        kNoRails = 0x100,    // its rails aren't counted
        kDestroyNow = 0x200, // destroy_piece takes it out at once
        kDestroy = 0x1000,   // marked: clean_up_out_refs takes it out
    };

    ParkTile* following_ = nullptr;        // +0x0C: the generator's list
    uint32_t ident_ = 0;                   // +0x10: the generator's piece counter when made (the cluster of its rails)
    TileTemplate* blend_source_ = nullptr; // +0x14
    maths::Vec4 at_point_;                 // +0x18
    int quarter_turns_;                    // +0x28: quarter turns

    // 0x00590250 (folded with other +0x10 getters)
    uint32_t fetch_serial();
    // 0x00590670: put it at `position` (and move its sector if `update` is 1)
    void change_place(maths::Vec4* position, int update);
    // 0x00590260: turn it to `rot` quarter turns (and its sector if `update` is 1)
    void set_quarter_turns(int rot, int update);
    // 0x00590290: its sector drawn and collided with (or neither)
    void change_live(bool active);
    // 0x005902B0: its sector shown in every viewport (or none)
    void set_visible(bool visible);
    // 0x005906B0: its sector stretched to `length` along its own x (z when turned a quarter)
    void set_length(float length);
    // 0x00591510: its sector's geometry turned `degrees` about y
    void set_angle(float degrees);
    // 0x00591600: its sector's geometry tinted (purple for a gap's piece) or back to normal
    void set_highlight(bool on, bool gap);

protected:
    ParkTile() = default;
};
static_assert(offsetof(ParkTile, ident_) == 0x10 && offsetof(ParkTile, blend_source_) == 0x14 &&
                  offsetof(ParkTile, at_point_) == 0x18 && offsetof(ParkTile, quarter_turns_) == 0x28 &&
                  sizeof(ParkTile) == 0x2C,
              "layout must match the original (a placed piece: 0x2C bytes)");

// A source piece (0x30 bytes; park_scene.h calls it TemplateTile): a piece type, one sector of the "sk5ed" scene
// (sector_ is its checksum).
class TileTemplate final : public ParkPiece {
public:
    // flag_bits_ (besides kSource)
    enum : uint32_t {
        kGameObject = 0x400,       // the node is a GameObject (read_piece_flags, from the pieces' node array)
        kAbsentInNetGames = 0x800, // (with kGameObject)
        kRebuilt = 0x4000,         // rebuilt by rebuild_marked (a trigger the scripts change)
    };

    // 0x005905C0: an empty source piece.
    TileTemplate() { flag_bits_ = kSource; }
    // 0x00590600: its size; returns `out`
    maths::Vec4* measure_span(maths::Vec4* out) override;

    TileTemplate* following_ = nullptr;    // +0x0C
    maths::Vec4 extent_ = {0, 0, 0, 1.0f}; // +0x10: in world units (x and z whole cells); w = 1
    int field_20_ = 1;                     // +0x20
    uint32_t trigger_script_ = 0;          // +0x24
    int rail_point_total_ = 0;             // +0x28: the points of the rails its node array gives it
    int num_rail_segments_ = 0;            // +0x2C
};
static_assert(offsetof(TileTemplate, extent_) == 0x10 && offsetof(TileTemplate, trigger_script_) == 0x24 &&
                  sizeof(TileTemplate) == 0x30,
              "layout must match the original");

// A placed piece as made (0x00590620: an empty cloned piece).
class CopiedPiece final : public ParkTile {
public:
    CopiedPiece() { flag_bits_ = kCloned; }
    // 0x00590710: the source's size, x and z swapped when turned a quarter; returns `out`
    maths::Vec4* measure_span(maths::Vec4* out) override;
};

// A metapiece (abstract or concrete; 0x3C bytes; park_metapieces.cpp): its pieces, by descriptor. (The original's
// vtables: the base's 0x0064F3BC, PlacedPieceGroup's 0x0064F3D8, PieceBlueprint's 0x0064F3E4.)
class PieceGroup : public memory::HeapObject {
public:
    // The flags (flag_bits_).
    enum : uint32_t {
        kConcrete = 0x1, // a PlacedPieceGroup (only these are highlighted by an area selection)
        kFlag1 = kConcrete,
        kAbstract = 0x2,   // a PieceBlueprint (a palette entry)
        kInPark = 0x4,     // placed in the park (not the cursor's own piece)
        kBuiltShell = 0x8, // part of the floor / shell the park builds itself (risers, walls); is_riser
        kFlag20 = 0x20,    // being moved by a floor change (kept when an area is cleared with flag 4)
        kSector = 0x80,    // a single-piece palette entry (named after its piece, until "name" renames it)
        kFence = 0x100,    // a fence piece of the inner shell
        kCursor = 0x200,   // the cursor's piece
        kNoRails = 0x400,  // no_rails
        kFlag400 = kNoRails,
        kGap = 0x800,             // the gap tool (special_type gap_placement)
        kHighlightable = 0x1000,  // has a gap attached: highlighted while a gap is placed
        kRestartFlag = 0x2000,    // special_type red/green/blue/yellow_flag
        kFloorLocked = 0x4000,    // has a piece flagged 0x0CD75A3F: stops the floor under it being changed
        kMarqueeTool = 0x8000,    // the clipboard's area select tool (special_type area_selection)
        kRailPlacement = 0x10000, // the rail tool (special_type rail_placement)
        kRiserBelow = 0x20000,
    };

    PieceGroup(const PieceGroup&) = delete;
    PieceGroup& operator=(const PieceGroup&) = delete;
    // 0x005917E0 (0x00591670 / 0x00591BB0 the deleting ones): the extra descriptors freed.
    virtual ~PieceGroup();
    // Piece `index` (null past the end): a placed metapiece's cloned piece, a palette entry's source piece.
    virtual ParkPiece* get_piece(uint32_t index) = 0;
    // A piece added at the cells `pos` (x y z), turned `rot`.
    virtual void insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor) = 0;

    CellBox cell_footprint_ = {0, 0, 0, 0, 0, 0}; // +0x04 (the offsets are the original's: the vtable at +0x00)
    uint8_t pad0a_[2];
    uint32_t flag_bits_ = 0;               // +0x0C
    int quarter_turns_;                    // +0x10: quarter turns
    PieceGroupDesc desc_[2];               // +0x14: the first two pieces
    PieceGroupDesc* extra_desc_ = nullptr; // +0x2C: the others (piece 2 is extra_desc_[0])
    uint32_t slot_limit_ = 0;              // +0x30
    uint32_t filled_slots_ = 0;            // +0x34
    uint32_t name_key_;                    // +0x38: the abstract metapiece's name

    // 0x005902E0 (returns this; folded with many empty constructors): a metapiece as a concrete one
    PieceGroup* as_placed_piece() { return this; }
    PieceGroup* get_cast() { return this; }
    // 0x005902F0: one of the eight restart pieces (by its name)
    bool is_spawn_marker();
    // 0x00590340: descriptor `index` (signed: below 2 the first two)
    PieceGroupDesc* get_descriptor(int index);
    // 0x00591A50: each of its cloned pieces tinted (purple with `gap`) or back to normal
    void spotlight(int on, int gap);
    // 0x00591800: its pieces put at `position`, turned by `angle` degrees about it
    void change_place(maths::Vec4 position, float angle);
    // 0x005907B0: turned to `rot` quarter turns: its area and every descriptor turned about its corner
    void set_quarter_turns(int rot);
    // 0x005909C0: piece `index`'s centre relative to the metapiece's (returns `out`)
    maths::Vec4* get_relative_position(maths::Vec4* out, uint32_t index);
    // 0x00590750: whether the metapiece lies within `area` (x and z)
    bool fits_inside_region(CellBox* area);
    // 0x00590340 (as get_descriptor)
    PieceGroupDesc* get_entry(int index) { return get_descriptor(index); }
    // 0x00590400: how many pieces, a palette entry of several pieces counted by its pieces (an abstract metapiece)
    int count_pieces();
    // 0x00590AD0: room for `count` pieces (the extra ones kept when it grows)
    void commission(int count);
    // 0x00591AB0: its cell area from its pieces' bounds, and the pieces centred on it
    void finish_pieces();
    // 0x005903A0: its pieces' sectors drawn and collided with, or neither
    void toggle_shown(bool visible);
    // 0x00590CE0: does it take up any of `area`? With `precise` (its low byte), by the cells of its pieces that
    // aren't flagged setsFloor (when any is)
    bool intersects(CellBox area, uint32_t precise);
    // 0x00590B90: the box round its pieces (from each one's position and size)
    void get_bounds(maths::Vec4* max, maths::Vec4* min);
    // 0x005916B0: its pieces as an array of structures (each piece's checksum and position)
    void get_piece_list(script::Array* out);

protected:
    PieceGroup() = default;
};
static_assert(offsetof(PieceGroup, flag_bits_) == 0x0C && offsetof(PieceGroup, desc_) == 0x14 &&
                  offsetof(PieceGroup, extra_desc_) == 0x2C && offsetof(PieceGroup, filled_slots_) == 0x34 &&
                  offsetof(PieceGroup, name_key_) == 0x38 && sizeof(PieceGroup) == 0x3C,
              "layout must match the original");

// A metapiece placed in the park or held by the cursor: its pieces are cloned pieces.
class PlacedPieceGroup final : public PieceGroup {
public:
    // 0x00590B60: an empty one (kConcrete, unturned).
    PlacedPieceGroup();
    // 0x00590360: piece `index` (null past the end)
    ParkTile* get_piece(uint32_t index) override;
    // 0x00590ED0: `piece` (a placed piece) added at the cells `pos` (x y z), turned `rot`; the piece's position set to
    // its centre there
    void insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor) override;
};

// A palette entry: its descriptors name source pieces or other palette entries.
class PieceBlueprint final : public PieceGroup {
public:
    // 0x00590FA0: an empty one (kAbstract).
    PieceBlueprint();
    // 0x00590FD0: piece `index`'s source piece (from the generator; null past the end)
    TileTemplate* get_piece(uint32_t index) override;
    // 0x00591020: a source piece added by name (anything else only counted), the area grown to take it in (at least
    // 1 high)
    void insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor) override;
    // 0x00591100: a palette metapiece added by name (null only counted), the area grown to take it in
    void add_meta_piece(PieceGroup* meta, CellBox* pos, int rot, uint8_t sets_floor);
    // 0x005911D0: entry `index` counting the pieces of the palette metapieces it's made of one by one (each placed
    // and turned in this one); returns `out`
    PieceGroupDesc* get_entry(PieceGroupDesc* out, int index);
};

// A node of a metapiece list (12 bytes, pooled: the pool pair 0x007CE5F0).
struct MapListNode {
    int dy_;                  // in the floor change's list: how far the metapiece moves up (down if negative)
    PieceGroup* piece_group_; // +0x04
    MapListNode* following_;  // +0x08

    // 0x005480C0 (one of many getters folded together there)
    PieceGroup* get_piece_group() const { return piece_group_; }
    PieceGroup* get_placed_piece() { return piece_group_->as_placed_piece(); }

    // A new node from the pools (or the heap), zeroed; null if out of memory.
    static MapListNode* fabricate();
    // 0x005914B0: back to the pools (operator delete).
    static void dealloc(void* node);
    // 0x00591BE0: free a list (the nodes, not the metapieces; null is fine)
    static void __fastcall free_node_chain(MapListNode* list); // (__fastcall: the list in ECX, as the original)
    // 0x00590470: walks the list (a debug check with its asserts compiled out; never ends on a cycle).
    static void __fastcall audit_soundness(const MapListNode* list); // (__fastcall: the list in ECX)
};
static_assert(sizeof(MapListNode) == 12, "pool item");

// The holder of a list returned by the manager's lookups (0x004E1970 makes it, 0x00469490 reads it, 0x00591C60
// frees the list).
struct MapListHolder {
    MapListNode* list;
    explicit MapListHolder(MapListNode* l) : list(l) {}
    ~MapListHolder()
    {
        if (list)
            MapListNode::free_node_chain(list);
    }
    MapListHolder(const MapListHolder&) = delete;
    MapListHolder& operator=(const MapListHolder&) = delete;
};
// The same holder as plain data, for code that frees the list itself (0x00591C60).
struct CellChainScratch {
    MapListNode* list;

    // 0x00591450: debug output of the list (each metapiece's address and cells)
    void dump_to_log();
    // 0x00591C60: the destructor (the list freed)
    void teardown();
};

// A gap (0x54 bytes; 0x0058CBC0 constructs the two ends): the gap tool's state, and how the gap manager keeps one.
struct GapDetails {
    CellBox at_point_[2];   // +0x00: the cells of its two halves
    int quarter_turns_[2];  // +0x0C (0-3; 4 bits each in the map)
    int reach_left_[2];     // +0x14 (0-6 cells)
    int reach_right_[2];    // +0x1C
    char text_[0x20];       // +0x24: its name ("gap", "gap2"...)
    int points_held_;       // +0x44
    int halves_placed_;     // +0x48: how many of its halves are placed (2 for a whole gap)
    int slot_index_;        // +0x4C: the gap manager's index (-1 none)
    uint32_t cancel_flags_; // +0x50

    // 0x0058CBC0: the halves' cells cleared (the rest isn't touched). Returns this.
    GapDetails* make_new();
    // 0x0058CBF0: the length of half `half` across the gap, in the world's units: its extents and its piece's length
    // (turned 0 / 2) or width (1 / 3); 0 for another turn.
    float fetch_magnitude(int half);
};
static_assert(offsetof(GapDetails, text_) == 0x24 && offsetof(GapDetails, halves_placed_) == 0x48 &&
                  sizeof(GapDetails) == 0x54,
              "layout must match the original");
using GapRecord = GapDetails;

// A metapiece copied into a clipboard (0x18 bytes, pooled: the pool pair 0x007CE5C4; copy_buffer.cpp): the palette
// piece it was made from, its cell and size where it was copied (raised so the lowest piece of the copy is at 0), its
// turn, and the preview shown at the cursor. `centre_x` / `centre_z` below are the copy's centre cell.
struct ClipboardEntry {
    PieceGroup* piece_group_; // +0x00: the abstract metapiece
    uint8_t pos_x_;           // +0x04
    int8_t y_;                // +0x05
    uint8_t depth_cell_;      // +0x06
    uint8_t pad07;
    int quarter_turns_; // +0x08: quarter turns
    uint8_t w_;         // +0x0C
    uint8_t l_;         // +0x0D
    uint8_t pad0e_[2];
    PieceGroup* preview_;       // +0x10: shown at the cursor (kCursor)
    ClipboardEntry* following_; // +0x14

    // 0x0058B800: the cells it would take with the cursor's copy (turned with the cursor; one high, at its height).
    void fetch_extent(CellBox* area, uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058B940: whether the floor under it can be changed there
    bool can_place(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058B9C0: pasted: the floor raised to its height plus `height` (at most 15; an inner-shell piece's own height
    // on top), then (unless it's of the inner shell) a new metapiece turned with the cursor put there
    void graft(uint8_t centre_x, int height, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058BB20: the floor height under it
    int fetch_tallness(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058BBA0: highlight (or not) the metapieces (not the inner shell's) in its cells
    void glow_overlapping_pieces(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor, int on);
    // 0x0058BCA0: its preview (made the first time) placed at `position` turned by `angle` degrees
    void show_preview(uint8_t centre_x, int height, uint8_t centre_z, maths::Vec4 position, float angle);
};
static_assert(offsetof(ClipboardEntry, quarter_turns_) == 0x08 && offsetof(ClipboardEntry, preview_) == 0x10 &&
                  sizeof(ClipboardEntry) == 0x18,
              "pool item");

// A copied area (0x18 bytes, pooled: the pool pair 0x007CE5B4; copy_buffer.cpp): the metapieces (and rails) copied from
// it.
struct Clipboard {
    CellBox edit_span_;             // +0x00
    uint8_t lo_cell_x_, lo_cell_z_; // +0x06: the cells its entries cover
    uint8_t hi_cell_x_, hi_cell_z_; // +0x08
    uint8_t pad0a_[2];
    ClipboardEntry* metas_; // +0x0C: its entries (the newest first)
    void* rail_groups_;     // +0x10: the rail editor's copies of the rails over it (linked by their first word)
    Clipboard* following_;  // +0x14: the cursor's list

    // 0x0058BDF0
    Clipboard* make_new();
    // 0x0058C430 (also reached through 0x0058C980): the destructor - its entries (and previews) and rails freed
    void teardown();
    // 0x005955C0: give an item back to the clipboard pools (operator delete)
    static void dealloc(void* item);
    // 0x0058B7E0
    bool is_vacant();
    // 0x0058C590: copy the metapieces wholly in `area` (not restart points or risers below) and its rails; false
    // (and emptied) when the entries' pool is full
    bool copy_picked_to_clipboard(CellBox area);
    // 0x0058C270: `meta` added (raised by `dy`), unless the same piece is in it already; false when the pool is full
    bool add_row(PieceGroup* meta, int dy);
    // 0x0058BE30: the floor height under the copy at the cursor (the highest under any entry; 0 without entries)
    int fetch_tallness(BuildPointer* cursor);
    // 0x0058BEB0: highlight the metapieces the copy would replace
    void glow_overlapping_pieces(BuildPointer* cursor, int on);
    // 0x0058C050: the world position of the middle of its centre cell (at its floor). Returns `centre`.
    maths::Vec4* get_centre(maths::Vec4* centre);
    // 0x0058C130: show the copy at `position`, turned by `angle` (its rails too)
    void preview_pieces(maths::Vec4 position, float angle, int height);
    // 0x0058C200: remove what preview_pieces made
    void free_all_piece_groups();
    // 0x0058C780: paste the copy at the cursor: false (nothing changed) if the park memory has no room for it, the
    // floor can't be changed under an entry or there aren't enough free rail points
    bool show_at(BuildPointer* cursor);
    // 0x0058BF10: the area it covers placed at the cursor (turned with it)
    void fetch_extent(BuildPointer* cursor, CellBox* area);
};
static_assert(sizeof(Clipboard) == 0x18, "pool item");

// A piece set: the entries of one of the piece menu's sets (0x154 bytes, Ed_piece_sets).
struct PieceFamily {
    struct TableRow {
        uint32_t checksum; // the abstract metapiece (or sector)
        const char* name;  // shown in the menu (null: "---")
    };
    uint32_t name_checksum_; // +0x000
    const char* label_;      // +0x004: shown in the set menu (set_name)
    TableRow entries_[40];   // +0x008
    int entry_total_;        // +0x148
    int selected_;           // +0x14C
    bool is_displayed_;      // +0x150: has a menu entry
    bool is_clipboard_;      // +0x151: the clipboard set
    uint8_t pad152[2];
};
static_assert(offsetof(PieceFamily, entry_total_) == 0x148 && offsetof(PieceFamily, is_displayed_) == 0x150 &&
                  sizeof(PieceFamily) == 0x154,
              "layout must match the original");
using TileFamilyRef = PieceFamily;

// A cell of the floor (16 bytes).
struct FloorCell {
    int level_y_;           // in floor steps (-16 to 15)
    uint32_t heights_used_; // +0x04: bit (y + 17) for each level a piece fills
    bool touched_;          // +0x08: changed by the floor change under way
    uint8_t pad9[3];
    int delta_; // +0x0C: by how much
};
static_assert(sizeof(FloorCell) == 16, "grid cell");

// The park as saved (0x3A98 bytes): a header, the floor heights, then the metapieces (8 bytes each: name index,
// cell, height, turn) and the gaps (0x30 bytes each).
struct CompressedMap {
    uint32_t checksum_;   // +0x00: CRC of everything after it
    uint16_t extent_;     // +0x04: bytes used
    uint16_t rev_;        // +0x06: kVersion (gaps 0x30 bytes); older parks have 0x2C-byte gaps
    uint16_t shell_size_; // +0x08: the park manager's shell_size_ (the shell model, the bounds)
    uint16_t unknown0a_;
    uint8_t pos_x_, depth_cell_, w_, l_; // +0x0C: the park's bounds
    uint16_t composite_count_;           // +0x10
    uint16_t num_gaps_;                  // +0x12
    uint8_t max_players_;                // +0x14
    uint8_t pad15[3];
    uint32_t style_set_;                            // +0x18: the park editor's (+0x78); 0 reads as "Default"
    char label_[0x40];                              // +0x1C
    int8_t floor_heights_[kFloorSize * kFloorSize]; // +0x5C
    uint8_t rest_[0x3A98 - 0x5C - kFloorSize * kFloorSize];

    static constexpr uint16_t kVersion = 20004;
};
static_assert(offsetof(CompressedMap, composite_count_) == 0x10 && offsetof(CompressedMap, label_) == 0x1C &&
                  offsetof(CompressedMap, floor_heights_) == 0x5C && sizeof(CompressedMap) == 0x3A98,
              "layout must match the original");

// The gap manager (the singleton 0x007CE5D4; the park manager keeps it at +0x177C4): the gaps placed in the park.
class GapRegistry;
extern GapRegistry* g_gap_registry;

class GapRegistry {
public:
    static GapRegistry*& sole() { return g_gap_registry; }

    // --- gap_slots.cpp (0x0058C990-0x0058CD3F) ---
    // 0x0058CBB0 (the getter the game calls)
    static GapRegistry* singleton();
    // 0x0058CA10: the gap `meta` is an end of (its slot into `index`, if given)
    bool get_gap_index(PieceGroup* meta, int* index);
    // 0x0058CA60: the trigger name ("EditorTrgGap<slot>") of the gap end whose trigger piece is `piece` (0 if none);
    // with the script integer PrintGaps, an odd slot's gap is printed
    uint32_t get_gap_trigger_name(ParkTile* piece);
    // 0x0058CB20: slot `index`'s gap info (null if empty; its name copied into the text first), and which end (`half`)
    GapDetails* get_gap_info(int index, int* half);
    // 0x0058CB70: the gap shown: both ends' trigger pieces highlighted
    void change_gap_in_progress(int index);
    // 0x0058C990: a free slot (-1 none)
    int find_empty_gap_slot();
    // 0x0058C9B0: gap `index`'s second end in a free slot (none free: nothing), given the next GapID
    void end_gap(void* piece, PieceGroup* meta, int index);
    // 0x0058CCC0: the constructor (no gaps, the counter at 2000, in `manager`'s park; makes itself the instance).
    // Returns this.
    GapRegistry* make_new(ParkLayout* manager);

    // 0x0058CDA0 (gap_manager.cpp): gap `index`'s info (its name, the rest copied); the checklist made again
    void store_gap_details(int index, GapDetails* info);
    // 0x0058D070 (gap_manager.cpp): the gap's first end (its trigger piece and metapiece)
    void start_gap(void* piece, PieceGroup* meta, int index);
    // 0x0058CDF0 (gap_manager.cpp): remove gap `index` (its trigger pieces deleted; with `forget`, also from the
    // goal editor's gaps)
    void drop_gap_entry(int index, bool forget);
    // 0x0058CE90: remove every gap
    void clear_gap_entries();
    // 0x0058CF30: a copy of `other`'s gaps (without their pieces; no park manager)
    void copy_from(GapRegistry* other);
    // 0x0058CF20: the destructor (also clears the singleton)
    void teardown();

    // --- gap_manager.cpp (0x0058CD40-0x0058D290) ---
    // One gap (0xEC bytes, made by StartGap / EndGap): the ids of its two ends, their trigger pieces and
    // metapieces, its name and its GapDetails.
    struct Gap {
        uint32_t ids_[2];      // +0x00: each end's GapID (the manager's counter when it was made)
        ParkTile* pieces_[2];  // +0x08: the trigger pieces
        PieceGroup* metas_[2]; // +0x10
        char label_[0x80];     // +0x18: the text of the gap's EndGap
        GapDetails details_;   // +0x98
    };
    static constexpr int kMaxGaps = 32;

    // 0x0058CD40: the current level's gap checklist made again from the gaps (each never landed)
    void update_checklist();
    // 0x0058CEC0: `piece`'s trigger length (cells) from the gap it's an end of (0 if none)
    void set_trigger_length(ParkTile* piece);
    // 0x0058D120: run StartGap / EndGap on `script`'s object for gap `index`'s two ends (GapID, score, text; the
    // start also with combined_flags), the end placed first first
    void fire_gap(int index, script::Script* script);

    uint32_t next_id_;             // +0x00: the next end's GapID
    Gap* gaps_ptr_[kMaxGaps];      // +0x04
    bool started_first_[kMaxGaps]; // +0x84: the gap's first half (StartGap) was placed first
    ParkLayout* park_owner_;       // +0xA4: the park it's in (null in a copy)
};
static_assert(sizeof(GapRegistry::Gap) == 0xEC && offsetof(GapRegistry::Gap, details_) == 0x98 &&
                  offsetof(GapRegistry, gaps_ptr_) == 4 && offsetof(GapRegistry, started_first_) == 0x84 &&
                  offsetof(GapRegistry, park_owner_) == 0xA4 && sizeof(GapRegistry) == 0xA8,
              "layout must match the original");

// A restart point of the park generator (0x24 bytes).
struct RestartPoint {
    enum KindTag {
        kFree = 0,
        kPlayer1 = 1,
        kPlayer2 = 2,
        kHorse = 3,
        kCrownRestart = 4,
        kRedFlag = 5, // 5-8 the CTF flags: red, green, blue, yellow
        kYellowFlag = 8,
    };
    maths::Vec4 at_point_; // +0x00
    int quarter_turns_;    // +0x10
    CellBox edit_span_;    // +0x14
    uint8_t pad1a_[2];
    int kind_tag_;      // +0x1C
    uint8_t automatic_; // +0x20: put down by the park itself (generate_node_array), not placed (the byte given)
    uint8_t pad21[3];
};
static_assert(offsetof(RestartPoint, edit_span_) == 0x14 && offsetof(RestartPoint, kind_tag_) == 0x1C &&
                  sizeof(RestartPoint) == 0x24,
              "layout must match the original");

// What build_source_rails keeps of each node of the pieces' node array (park_scene.h).
struct PieceNodeInfo;

// The park generator (0x4A4 bytes): the scene side of the park editor (park_scene.h).
class ParkAssembler {
public:
    // flag_bits_
    enum : uint32_t {
        kSourcesLoaded = 0x2, // load_source_pieces done
        kPiecesCreated = 0x4, // (create_piece sets it)
        kHasPieces = 0x8,
        kSuperSectorsUpdated = 0x10,
        kNodeArrayBuilt = 0x20,
        kSceneChanged = 0x40, // sectors added or deleted since the last update of the super sectors
    };
    static constexpr int kRestartTableSize = 18;
    static constexpr int kLightCapacity = 16;

    // --- park_scene.cpp, park_nodes.cpp (0x00592740-0x00595030; their types: park_scene.h) ---
    // 0x005933F0: take away the restart point of type `player` at `area` (its cell x, y, z)
    void unplant_restart_point(CellBox* area, int player);
    // 0x005935F0: mark a piece for clean_up_out_refs (taken out at once if it has DuplicatedTile::kDestroyNow)
    void destroy_piece(void* piece);
    // 0x00593610: take out and delete the pieces marked by destroy_piece
    void clean_up_out_refs();
    // 0x005947D0: every piece's sector deleted and cloned again (the node array dropped); 0x00594910: those of the
    // pieces whose type has TemplateTile::kRebuilt
    void rebuild_all();
    void rebuild_marked();
    // 0x00593460: the constructor (no pieces, restarts or rails; 2 players). Returns this.
    ParkAssembler* make_new();
    // 0x00592FE0: light `index` at `position`
    void move_light_source(const maths::Vec4* position, int index);
    // 0x00593010: `rgba`'s red, green and blue (0-255) lit at `position`: the ambient part plus the falloff part of
    // the lights' sum of 1 / (160000 + squared distance in x and z); over 255 is 255.
    void sample_light_colour(const maths::Vec4* position, uint8_t* rgba);
    // 0x00594410: a piece's vertex colours (from its source's) and its collision's vertex intensities lit
    void light_piece(ParkTile* piece);
    // 0x00593370: how many restart points of `type` there are
    int count_restarts(int type);
    // 0x00593500: a gap's trigger piece (the type 0xC0341DF7) at `position` turned `rot` quarter turns, added
    void* make_gap_marker_piece(maths::Vec4* position, float length, int rot);
    // 0x00594730: a piece's sector cloned from its source into the scene, placed, lit (a gap piece told the gap
    // manager)
    void add_piece_to_scene(ParkTile* piece);
    // 0x00594610: every piece taken out and deleted (with the rail editor's rails taken out of the scene first),
    // then every source piece deleted
    void unload_pieces();
    // 0x00594C90: the source pieces from the "sk5ed" scene's sectors (their sizes from their boxes), the "cloned"
    // scene, the bounds; the restart points freed (the argument unused)
    void load_source_pieces(int unused);
    // 0x00594FD0: load_source_pieces, then the source pieces' flags and rails from the pieces' node array (the
    // rails on the top-down heap for the park editor's own level 0xCD3682C1)
    void load_pieces(uint32_t level, int unused);
    // 0x00592A10: the point of the placed pieces' rails nearest to `position` (and the squared distance); false if
    // none is nearer than 1e8 squared units
    bool find_nearest_rail_point(const maths::Vec4* position, maths::Vec4* point, float* distance_squared);
    // 0x00592AB0: the node array's sizes: [0] nodes, [1] rail points, [2] rail segments, [3] / [4] memory
    // estimates, [5] rail points again
    void get_node_counts(int* counts);
    // 0x00592C10: a node array of the goal editor's nodes only, the node manager's
    void build_goal_node_array();
    // 0x00592C90: the node array dropped from the node manager and the symbol table
    static void remove_node_array();
    // 0x00592CD0: the placed pieces' rails (rails_) made from the source rails of the metapieces in `metas`
    void build_rails(MapListNode* metas);
    // 0x00592ED0 / 0x005949F0 / 0x00593AC0: the pieces', the rails', the restart points' nodes written into `nodes`
    // from node `*index` on (`*index` counts them)
    void write_piece_nodes(script::Array* nodes, int* index);
    void write_rail_nodes(script::Array* nodes, int* index);
    void write_restart_nodes(script::Array* nodes, int* index);
    // 0x00592740: the name of the next LevelGeometry node from `*index` on (0 at the end); `*index` past it
    uint32_t next_geometry_node(script::Array* nodes, int* index);
    // 0x005927D0: the next unused rail node of the piece `geometry` (after the node named `previous`, or a first
    // one: one with no links, else a linked one - `*is_loop` set); its name (0 if none)
    uint32_t find_following_rail_node(script::Array* nodes, uint32_t geometry, uint32_t previous, RailNodeInfo* info,
                                      bool* is_loop);
    // 0x005936E0: the source rails from the pieces' node array (each source piece's rail point and segment counts)
    void build_source_rails(script::Array* nodes);
    // --- park_generator.cpp (0x00591C70-0x00592740) ---
    // 0x00591C70: the destructor's body (the scenes forgotten, the node array's asset destroyed)
    void destroy_body();
    void teardown() { destroy_body(); }
    // 0x00591CF0: every piece tinted red, or back to normal
    void glow_every_piece(bool on);
    // 0x00591D20
    void set_player_cap(int count);
    // The most players the park is set up for (a whole int; the compressed map keeps its low byte)
    int get_player_limit() { return max_players_; }
    // 0x00591D30: the most players the park has room for: one more for each 585500 bytes the main heap has free
    // (counting those already set up)
    int get_player_cap_limit();
    // 0x00591D90: the piece type named `name` (null if none)
    TileTemplate* get_piece_source(uint32_t name);
    // 0x00591DD0: delete the park's sector `name` (false if there's none)
    bool remove_scene_sector(uint32_t name, int unused);
    // 0x00591DE0: the placed pieces' rails emptied and their pools freed
    void destroy_rails();
    // 0x00591E00: the lights' count, ambient and falloff colours and the cursor's ambience
    void configure_lighting(int count, float ambient_r, float ambient_g, float ambient_b, float falloff_r,
                            float falloff_g, float falloff_b, float cursor_ambience);
    // 0x00591E60: the restart slots (first to last) a restart type uses (unknown types: left as they were)
    void fetch_spawn_slots(int type, int* first, int* last);
    // 0x00591F50: whether a slot of `type`'s is free (or holds one put down automatically)
    bool has_free_restart_slot(int type);
    // 0x00591FE0: forget the restart points put down automatically; 0x00591FC0: all of them
    void purge_auto_restart_points();
    void clear_restarts();
    // 0x00592010: the "cloned" scene's bounding box (as big as a park can be)
    void set_up_bounds();
    // 0x00592090: the super sectors brought up to date (when allowed: 0x0067F398)
    void rebuild_collision();
    // 0x005920D0: the shell's two boxes: everything, and the park's bounds (`unused`: the callers' argument)
    void update_bounds(uint32_t unused);
    // 0x00592290: the gaps' trigger pieces (ParkTile::kGap) shown or hidden
    void set_pieces_highlighted(int on);
    // 0x005922D0: the restart points' pieces drawn and collided with, or neither
    void set_restart_pieces_active(bool active);
    // 0x00592320: a copy of the "sk5ed" sector `name` in the "cloned" scene (its name)
    uint32_t duplicate_scene_sector(uint32_t name, int a, int b, int unused);
    // 0x00592370: delete the park's sector `name`, then rebuild the collision (false if there was none)
    bool destroy_sector(uint32_t name, int unused);
    // 0x005923C0: a new placed piece of `source` (or of its source), with `flags` (0x324 of them)
    ParkTile* create_piece(ParkPiece* source, uint32_t flags);
    // 0x00592550: a placed piece onto the front of the generator's list (light_piece, 0x00594410, then lights it)
    void insert_tile(ParkTile* piece);
    // 0x00592570: a piece taken off the list (not the cursor's), its sector deleted (unless `keep_sector`), deleted
    void delete_piece(ParkTile* piece, int keep_sector);
    // 0x005925E0: the piece types' flags and trigger scripts from the pieces' node array
    void read_piece_flags(script::Array* nodes);
    // 0x00593200: a restart point of `type` at `position` (the cells `area`) in the first free slot of its type (or,
    // when `automatic`, one put down automatically before); with `automatic` and `keep_placed`, where a placed one
    // of the type is. False when there's no room.
    bool plant_restart_point(maths::Vec4* position, int rot, CellBox* area, int type, int automatic,
                             uint32_t keep_placed);
    // 0x005933D0: whether there are fewer than `count` restart points of `type`
    bool needs_restarts(int type, int count);
    // 0x00594EA0: the level's node array from the park (the pieces, their rails, the rail editor's, the restart
    // points, the goal editor's), given to the node manager and parsed
    void build_node_array(MapListNode* metas);
    // 0x00593580: delete the scene's pieces (`keep_rails` 0: the rail editor's rails go out of the scene too)
    void destroy_pieces(int keep_rails);
    // 0x00594C70: unload_pieces, and the source rails freed (the argument unused)
    void sweep_up(int type);

    uint32_t flag_bits_; // +0x000
    uint32_t field_04_;
    ParkTile* pieces_;      // +0x008
    TileTemplate* sources_; // +0x00C
    void* source_world_;    // +0x010: "sk5ed"
    void* scene_handle_;    // +0x014: "cloned"
    uint32_t field_18_;
    uint32_t next_piece_id_;                   // +0x01C (from 0x20)
    int max_players_;                          // +0x020
    ParkRailSet source_rails_;                 // +0x024: the pieces' rails, from their node array
    ParkRailSet rails_;                        // +0x058: the placed pieces' rails, while the node array is built
    PieceNodeInfo* node_info_;                 // +0x08C: (while build_source_rails runs)
    int* used_nodes_;                          // +0x090: (likewise) the rail nodes taken
    int num_used_nodes_;                       // +0x094
    RestartPoint restarts_[kRestartTableSize]; // +0x098: by type - 0 player 2, 1 player 1, 2-7 horse, 8-13 king of
                                               // the hill, 14-17 the flags (spawn_slot_array)
    int light_total_;                          // +0x320
    float ambient_[3];                         // +0x324 (rgb)
    float falloff_[3];                         // +0x330 (rgb)
    maths::Vec4 lights_[kLightCapacity];       // +0x33C
    uint32_t field_43c_;
    alignas(4) uint8_t node_array_[0x64]; // +0x440: an assets::NodeTableResource made in place (the node array's owner)
};
static_assert(offsetof(ParkAssembler, source_rails_) == 0x24 && offsetof(ParkAssembler, rails_) == 0x58 &&
                  offsetof(ParkAssembler, node_info_) == 0x8C && offsetof(ParkAssembler, restarts_) == 0x98 &&
                  offsetof(ParkAssembler, light_total_) == 0x320 && offsetof(ParkAssembler, lights_) == 0x33C &&
                  offsetof(ParkAssembler, node_array_) == 0x440 && sizeof(ParkAssembler) == 0x4A4,
              "layout must match the original");

// What rebuild_floor_piece is asked (0x1C bytes): the cell and its floor height; it says where the top piece went.
struct FloorPieceRequest {
    int x;
    int y; // (out) the top piece's height
    int z;
    int unknownc_;
    int unknown10;
    int index; // height - 1: the riser_above%d piece
    int height;
};

extern ParkLayout* g_park_layout;

class ParkLayout {
public:
    // flag_bits_: the compressed map's state (meanings partly guessed from where they're set)
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
    static ParkLayout*& sole() { return g_park_layout; }
    // 0x00595200 (the getter the game calls)
    static ParkLayout* singleton();

    // --- park_accessors.cpp (0x00595030-0x00595D40) ---
    // 0x00595030: the compressed map; `flag_clean` clears kMapNotSavedLocal.
    CompressedMap* get_packed_park_data(bool flag_clean);
    // 0x00595050: take a whole compressed map (loaded or received).
    void set_packed_park_data(const CompressedMap* map, bool flag_dirty);
    // 0x00595090: the compressed map is valid and saved.
    bool has_local_park_copy();
    // 0x005950B0: the compressed map's shell size (0 when it isn't readable).
    int get_shell_size();
    // 0x005950D0: the compressed map's park name (null if it isn't a map of this version).
    const char* get_park_name();
    // 0x005950F0
    void set_park_name(const char* name);
    // 0x00595140: update the park's geometry: 0 takes out removed pieces, 1 rebuilds it all, 2 rebuilds the
    // pieces marked for it.
    void rebuild_park_geometry(int how);
    // 0x00595170: the checksum of the park's shell model ("sk5ed_shell" or "sk5ed<size+1>_shell").
    uint32_t get_shell_checksum();
    // 0x005951E0
    MapListNode* get_cell_chain(int x, int z);
    // 0x00595210: piece set `index`; `menu_index` gets how many sets before it are shown.
    PieceFamily* get_piece_family(int index, int* menu_index);
    // 0x00595250: header fields of a compressed map.
    void get_map_info(const CompressedMap* map, int* num_gaps, int* num_metas, uint16_t* shell_size, uint32_t* theme,
                      int* width, int* length);
    // 0x005952B0: undo the marked floor changes (clamping every height to -16..15)
    void restore_floor_heights();
    // 0x00595300: which restart piece (1 to 8) has this name; 0 if none.
    int get_restart_index(uint32_t name);
    // 0x00595620: the palette metapiece with this name; `index` gets its position counted from the end.
    PieceGroup* fetch_meta_template(uint32_t name, int* index);
    // 0x00595990: is there memory for the park grown or shrunk to `area` (w x l)?
    bool can_resize_to(CellBox area);
    // 0x005959D0: the highest floor in `area`; `is_flat` (if given) says whether it's all one height
    int get_ground_level(CellBox area, bool* is_flat);
    // 0x00595A90: the world position of `area`'s corner (w = 1), and its size (if given)
    maths::Vec4 cell_to_world_pos(const CellBox& area, maths::Vec4* size);
    maths::Vec4* grid_dims_to_world(maths::Vec4* out, CellBox* area, maths::Vec4* size)
    {
        *out = cell_to_world_pos(*area, size);
        return out;
    }
    // 0x00595B30: whether `area` overlaps one of the blocked areas (the road mask)
    bool is_area_blocked(CellBox area);
    // 0x00595BC0: a copy of the gap manager in use (on the script heap); the one in use stays current
    void create_gap_manager_copy();
    // 0x00595C70: make the copy the gap manager in use
    void use_gap_manager_copy();
    // 0x00595C80
    void destroy_gap_manager_copy();
    // 0x00595CB0: highlight the pieces a gap can attach to (and, when highlighting, make `gap` current)
    void highlight_gap_pieces(bool on, GapDetails* gap);
    // 0x00595D40: clear the marks (and every metapiece's kFlag20)
    void clear_floor_change_marks();
    // The abstract metapiece named `checksum` (null if none) - inline in the original.
    PieceGroup* find_piece_template(uint32_t checksum)
    {
        for (MapListNode* node = abstract_metas_; node; node = node->following_) {
            PieceGroup* meta = node->get_placed_piece();
            if (meta->name_key_ == checksum)
                return meta;
        }
        return nullptr;
    }

    // --- park_lists.cpp: the map of metapieces ---
    // 0x00595670: a new concrete metapiece (on the park heap) made from the palette's `source`, its pieces made by
    // the generator with `flags`; added to the list of concrete metapieces
    PieceGroup* make_placed_piece(PieceGroup* source, uint32_t flags);
    // 0x005958B0: is a placed piece (not the inner shell) not wholly inside `area` (x and z)?
    bool has_pieces_outside(CellBox area);
    // 0x00595DF0: add `meta` to the front of `list` unless it's in it already
    void add_meta_to_list(PieceGroup* meta, MapListNode** list);
    // 0x00595E80: unlink `meta` from `list`
    void remove_meta_from_list(PieceGroup* meta, MapListNode** list);
    // 0x00595EE0: list `meta` in the cell lists under it
    void add_meta_to_cells(PieceGroup* meta);
    // 0x00595FC0: take `meta` out of the cell lists under it
    void remove_meta_from_cells(PieceGroup* meta);
    // 0x00597740: the metapieces intersecting `area` (or, given `shape` turned to `rot` for the moment, those its
    // pieces would touch with its corner at `area`); `skip_inner_shell` (its low byte; the whole value is passed on to
    // PieceGroup::intersects) leaves out risers and counts only real
    // pieces' cells. A new list the caller frees.
    MapListNode* get_intersecting_metas(CellBox area, int rot, PieceGroup* shape, uint32_t skip_inner_shell);
    // 0x00597A00: every metapiece with a piece in a cell of `area` (a new list the caller frees)
    MapListNode* collect_pieces_in_region(CellBox area);
    // 0x00597AC0: the gap at the metapiece in cell `pos` (made 1x1x1), and which of its ends (`half`)
    GapDetails* get_gap_info(CellBox* pos, int* half);
    // 0x005983D0: whether a metapiece over `area` (at any height) has kFloorLocked
    bool has_floor_locked_meta(CellBox area);
    // 0x005984A0: change the floor cells of `area` (from height `y`: up or down a step, or to `target`; `mode` 1:
    // to the target, 2: up / exactly, 4: every height counts), marking them; the metapieces on it go on `list` and
    // their own areas change with them. False if a cell would leave -16..15.
    bool build_riser_list(CellBox area, int y, int target, uint32_t mode, MapListNode** list);
    // 0x00598150: `meta` placed (or taken away): raise the floor under its real pieces to their tops (or lower it
    // to their bottoms where nothing's left above) and mark (or clear) the levels they fill
    void set_meta_in_cells(PieceGroup* meta, int add);

    // --- park_io.cpp: set-up and saving ---
    // 0x005960D0: the palette from Ed_standard_metapieces (single pieces, multiple pieces, and ones made of other
    // palette metapieces), the restart pieces, and a single-piece metapiece for every piece used
    void set_up_abstract_metas();
    // 0x00596980: the park into the compressed map (unless the map is newer than the park)
    void write_compressed_map();
    // 0x00596CD0: the park's bounds (inside: an ed_default_inner_dim square, centred)
    void set_default_bounds();
    // 0x00596D50: the blocked areas from Ed_Roadmask (after the first four)
    void set_up_road_mask();
    // 0x00596E50: the lights from Ed_Light_Info
    void set_up_lights();
    // 0x00596FB0: the memory bar (parked_update_percent_bar park_percent, park_full)
    static void update_percent_bar();
    // 0x005975A0
    void write_compressed_map_if_built();
    // 0x005975B0: the park into `params` (Park_editor_map, Park_editor_goals, the rails, MaxPlayers)
    void export_to_struct(script::Struct* params);
    // 0x00597720
    void set_shell_size(int size);
    // 0x005987A0: an empty park in the compressed map
    void clear_compressed_map();
    // 0x00597BD0: a gap made from `gap` (the gap manager makes its triggers)
    bool register_gap_entry(GapDetails* gap);
    // 0x00597FC0: delete a gap
    void drop_gap_entry(GapDetails* gap);
    // 0x00591450: debug output of a list (CellChainScratch::dump_to_log)
    static void print_map_list(const MapListNode* list);

    // --- park_manager.cpp, park_map.cpp, park_floor.cpp (0x00598E00-0x0059C900) ---
    // 0x00598E00: the constructor (an empty park; the generator made). Returns this.
    ParkLayout* make_new();
    // 0x00599BF0 / 0x00599C70: make / delete the instance
    static void spawn_occurrence();
    static void discard_occurrence();
    // 0x00598FC0: a park from a script structure (its goals, rails, player count and map)
    void decode_from_struct(script::Struct* params, bool apply_to_editor);
    // 0x00599140: save the park to, or load it from, CustomParks\custom<slot>.prk
    void transfer_park_file(bool save, int slot);
    // 0x00599EB0 / 0x0059B750: the park built from the compressed map
    void read_from_old_compressed_map();
    void read_from_compressed_map();
    // 0x0059BE20: an empty park ("unnamed park")
    void reset_park();
    // 0x005993E0: the restart points every park has, then the node array
    void generate_node_array();
    // 0x00599A20: put `meta` in the park at `pos` (x, y, z)
    void place_meta_piece_in_park(CellBox pos, PieceGroup* meta);
    // 0x00599CD0: whether the floor of `area` can be raised or lowered (not with pieces on it; without `uneven`,
    // only where it's flat at area.y)
    bool can_change_floor_here(CellBox area, bool uneven);
    // 0x00599E20: delete what's in the layer under `area` (counted in riser_metas_ / riser_pieces_)
    void destroy_metas_below(CellBox area);
    // 0x0059B020: delete every placed metapiece (`keep_rails` is passed to the generator)
    void destroy_concrete_metas(int keep_rails);
    // 0x0059B090: `meta` deleted and made again (dx, dy, dz) cells away, with its gap. Returns the new one.
    PieceGroup* shift_placed_piece(PieceGroup* meta, int dx, int dy, int dz);
    // 0x0059B280: put the floor and the moved metapieces back
    void undo_floor_change();
    // 0x0059B2C0: move each metapiece of `list` by its dy_
    void move_riser_list(MapListNode* list);
    // 0x0059B320: the floor pieces of one cell (risers, the floor, riser_below walls)
    void rebuild_floor_piece(FloorPieceRequest* request);
    // 0x0059BD40: take the park down (the map brought up to date first)
    void teardown(int keep_rails);
    // 0x0059BED0: the park's bounds become `bounds`: what's outside goes, the fence goes round it
    void rebuild_inner_shell(CellBox bounds);
    // 0x0059C520: raise or lower (or with `level`, set to `target`) the floor of `area` and what's on it
    bool raise_or_lower_floor(CellBox area, int y, int target, bool raise, bool level);
    // 0x0059C600: the floor pieces of `area` and round it (null: the whole park, after clearing what's outside)
    bool regenerate_ground(CellBox* area, int unused); // (the callers pass 0 as `unused`)
    // 0x00599880: delete the metapieces intersecting `area` (`flags`: 2 not the inner shell, 4 not kFlag20 ones;
    // passed on to free_placed_piece). Returns the count of metapieces << 16 | the count of their pieces.
    uint32_t free_piece_groups_in_region(CellBox area, uint32_t flags);
    // 0x00599960: highlight (or not) the metapieces wholly within `area`
    void highlight_metas_within_area(CellBox area, int on);
    // 0x0059AE90: take `meta` out of the park and delete it (the cursor's own piece stays). `flags` 1: also
    // delete what stands on it (2: not the inner shell).
    void free_placed_piece(PieceGroup* meta, uint32_t flags);

    // --- src/game/park/park_editor.cpp, park_cursor.cpp (0x0059C900-0x0059E0F7) ---
    // 0x0059CB10: rebuild the whole park: tear down what's built, then (for a new park - `how` 0x1A4E0EF9 - or one
    // not loaded from a save) empty the grid and name it "unnamed park", else rebuild from the saved map; the floor
    // and every piece rebuilt, the scene updated.
    void rebuild_park(uint32_t how);
    // 0x0059CC70: rebuild the pieces in `area` (0x0059C600); with `check_memory`, false if the park is now over its
    // memory budget.
    bool rebuild_area(const CellBox* area, bool check_memory);
    // 0x0059CD20: destroy the metapieces in `area` (not the floor's: flag 8) - with `skip_restarts` not restart
    // points, with `only_within` only those wholly inside, with `skip_locked` not those with flag 0x100; the area's
    // scene updated. True (and the area rebuilt) if any went.
    bool destroy_metas_in_area_and_rebuild(CellBox area, bool skip_restarts, bool only_within, bool skip_locked);
    // 0x0059CEC0: flatten `area`: each piece in it (not the floor's) moved down by its height and the floor under
    // it set to 0, then the area's floor; rebuilt.
    void clear_area(CellBox area);
    // 0x0059D080: raise (dir > 0) or lower the floor in `area`; if it won't fit in memory it's undone. True if done.
    bool raise_or_lower_floor(CellBox area, int dir);
    // 0x0059DCD0: set up once for `level`: the park memory (Init), an empty grid, the park's map (0x0058CCC0), the
    // generator, the whole park built, and the palette's piece sets from Ed_piece_sets / Ed_standard_metapieces.
    void commission(uint32_t level);

    // Offset accessors (src/game/park).
    uint8_t* get_bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& get_at(uint32_t offset)
    {
        return *reinterpret_cast<T*>(get_bytes() + offset);
    }
    void*& get_generator() { return get_at<void*>(0x0); } // the park's piece generator
    bool& floor_built() { return get_at<bool>(0x5); }
    CellBox& park_dims() { return get_at<CellBox>(0x3520); }
    uint32_t& current_flags() { return get_at<uint32_t>(0x17728); }
    int& num_sets() { return get_at<int>(0x13C8C); }
    // A grid cell (16 bytes): +0 the floor height there.
    uint32_t& grid_level_count(int x, int z) { return get_at<uint32_t>(0x352C + (x * kGridSize + z) * 0x10); }

    ParkAssembler* builder_; // +0x00
    bool park_built_;        // +0x04: the park is built (its changes go into the map when asked)
    bool field_05_;          // +0x05: the floor is built
    uint8_t pad06[2];
    MapListNode* riser_list_;     // +0x08
    int riser_metas_;             // +0x0C
    int riser_pieces_;            // +0x10
    uint32_t shell_blocks_[8][3]; // +0x14: "floor_wall_block%d", "wall_block%d", "floor_block%d" (1..8)
    int field_74_;
    void* field_78_;                                  // +0x78: a Malloc block
    MapListNode* concrete_metas_;                     // +0x7C
    MapListNode* abstract_metas_;                     // +0x80
    int num_concrete_metas_;                          // +0x84
    int num_abstract_metas_;                          // +0x88
    MapListNode* cell_metas_[kFloorSize][kFloorSize]; // +0x8C: [x][z] the metapieces with pieces in each cell
    int shell_size_;                          // +0x351C: picks the shell model (sk5ed%d_shell) and the bounds' update
    CellBox build_area_;                      // +0x3520: the park's area (inside the shell)
    CellBox shell_bounds_;                    // +0x3526: everything that may be built on
    FloorCell floor_[kFloorSize][kFloorSize]; // +0x352C: [x][z]
    PieceFamily sets_[kMaxSets];              // +0x1076C
    int num_sets_;                            // +0x13C8C
    CompressedMap map_;                       // +0x13C90
    uint32_t flag_bits_;                      // +0x17728
    char park_name_[0x40];                    // +0x1772C
    CellBox blocked_areas_[8];                // +0x1776C: (the first four: the strips round the park; then Ed_Roadmask)
    uint32_t num_blocked_areas_;              // +0x1779C
    uint32_t unknown177a0_;
    uint32_t restart_pieces_[kNumRestartPieces]; // +0x177A4: restart_1, restart_multi, restart_horse, king_of_hill,
                                                 // and the red, green, blue, yellow flags
    GapRegistry* gap_registry_;                  // +0x177C4
    GapRegistry* gap_manager_copy_;              // +0x177C8: see create_gap_manager_copy

private:
    void read_compressed_map(script::Array* names, bool current);
};
static_assert(offsetof(ParkLayout, shell_blocks_) == 0x14 && offsetof(ParkLayout, concrete_metas_) == 0x7C &&
                  offsetof(ParkLayout, cell_metas_) == 0x8C && offsetof(ParkLayout, shell_size_) == 0x351C &&
                  offsetof(ParkLayout, floor_) == 0x352C && offsetof(ParkLayout, sets_) == 0x1076C &&
                  offsetof(ParkLayout, num_sets_) == 0x13C8C && offsetof(ParkLayout, map_) == 0x13C90 &&
                  offsetof(ParkLayout, flag_bits_) == 0x17728 && offsetof(ParkLayout, blocked_areas_) == 0x1776C &&
                  offsetof(ParkLayout, num_blocked_areas_) == 0x1779C &&
                  offsetof(ParkLayout, restart_pieces_) == 0x177A4 && offsetof(ParkLayout, gap_registry_) == 0x177C4 &&
                  sizeof(ParkLayout) == 0x177CC,
              "layout must match the original");
inline PieceFamily* get_sets(ParkLayout* manager) { return manager->sets_; }

// The cursor (200 bytes): the box the player moves over the grid, with the piece to place (or the copy to paste).
extern BuildPointer* g_cursor;

class BuildPointer {
public:
    enum Mode {
        kModeNone = 0,
        kModeRegular = 1,
        kModeGap = 2,           // "gap_regular": the gap tool
        kModeGapAdjust = 3,     // "gap_adjust": over a gap's piece
        kModeAreaSelect = 4,    // selecting an area to copy
        kModePaste = 5,         // pasting the clipboard
        kModeRailPlacement = 6, // "rail_placement"
    };

    // The one instance.
    static BuildPointer*& sole() { return g_cursor; }
    // 0x00595330 (the getter the game calls)
    static BuildPointer* singleton();

    // --- cursor_tools.cpp (0x00595330-0x005989C0) ---
    // 0x00595340: take every clipboard's preview out of the park
    void hide_clipboards();
    // 0x00595360: the gap's name ("" makes it "x") and score (unless negative), handed to the gap manager
    void set_gap_label_and_points(const char* name, int score);
    // 0x005953C0: the gap's cancel flags, from the names in `flags` (each a global integer), handed to the gap manager
    void set_gap_void_mask(script::Struct* flags);
    // 0x00595430
    char* get_gap_title() { return gap_.text_; }
    // 0x00595440: show the current clipboard's preview here
    void show_clipboard();
    // 0x00595450: hide every preview and make clipboard `index` the current one (none past the end)
    void select_clipboard(int index);
    // 0x005954A0
    void toggle_shown(bool visible);
    // 0x005954D0: tell the scripts the mode changed (parked_set_helper_text_mode)
    void on_mode_changed();
    // 0x00597100: back to the first corner of the area selection
    void restart_area_selection()
    {
        box_select_stage_ = 1;
        area_highlighted_ = false;
    }
    // 0x00597110: the selected area (one level high; all zero without a selection)
    void get_marquee_bounds(CellBox* out);
    // 0x00597190: delete the oldest clipboard (the last in the list)
    void remove_oldest_clipboard();
    // 0x005971E0: keep the cursor (or the paste) within the park
    void clamp_to_park();
    // 0x005988A0: destroy every clipboard (the list head is left as it was)
    void free_copy_buffers();
    // 0x00598940: forget the gap being placed if `meta` is (one of) its pieces
    void forget_gap_at(PieceGroup* meta);
    // 0x005989C0: the next step of the area selection: start it at the cursor, or finish it when it's at most
    // 11 x 11; the state after (0 none, 1 first corner set, 2 done)
    int advance_area_selection();

    // --- cursor.cpp (0x00598A60-0x0059C860) ---
    // 0x0059C7A0: the constructor (on the first piece of set 0). Returns this.
    BuildPointer* make_new();
    // 0x0059C860: make the instance (on the park editor's heap) if there isn't one
    static void spawn_occurrence();
    // 0x00598A60: highlight (or not) the metapieces under the cursor (or under the paste)
    void glow_overlapping_pieces(int on);
    // 0x00598B40: whether the cursor is over a gap's piece (the lookup shrinks cell_footprint_ to 1x1x1)
    bool is_over_gap();
    // 0x00598B60: move by (dx, dz) cells (kept in the park), then follow the floor and the gap state
    void shift_by(int dx, int dz);
    // 0x0059A1F0: one frame: turn by `turn_step` quarter turns, step on the stick (offset_cell_x, offset_cell_z), slide towards the
    // target and show the piece there
    void tick_frame(float offset_cell_x, float offset_cell_z, int turn_step);
    // 0x0059A5A0: drop the area selection (and its highlight)
    void drop_area_marquee();
    // 0x0059A610: whether the selected area can't be copied (bigger than 10 x 10, or the editor says no)
    bool is_area_selection_invalid();
    // 0x0059A6D0: copy the selected area into a new clipboard (the oldest goes when there are 6). False if it
    // can't be copied.
    bool copy_area_to_clipboard();
    // 0x0059A8D0: keep the selected area highlighted as the cursor moves
    void update_area_selection_highlight();
    // 0x0059BAC0: delete the cursor's piece
    void destroy_cursor_meta();
    // 0x0059BAF0: paste mode, with the newest clipboard (if there is one)
    void switch_to_clipboard();
    // 0x0059BC00: take the piece `checksum` (an abstract metapiece) and the mode its kind needs
    void change_piece(uint32_t checksum);

    // --- src/game/park/park_cursor.cpp, park_editor.cpp (0x0059C900-0x0059E0F7) ---
    // 0x0059D120: the clipboards too big for this park are deleted; the first is current; the set's piece again.
    void remove_clipboards_too_big();
    // 0x0059D240: place the cursor's piece (if it fits, is under the height limit of 17 and - for a restart point
    // - has a free slot): a copy of its type, turned, added; rebuilt.
    bool commit_tile_placement();
    // 0x0059D380: destroy the pieces under the cursor (or the clipboard's area) - not locked ones.
    bool destroy_under_cursor();
    // 0x0059D410: make the piece under the cursor a half of the gap being made (unless the park is full or it has
    // a gap there already): the first half names the gap ("gap", "gap2"...).
    bool add_gap_half();
    // 0x0059D600: drop the gap being made, or the gap under the cursor (its pieces looked at again).
    bool remove_gap_half();
    // 0x0059D6B0: turn the gap under the cursor (its extents reset) and/or widen its left / right extents (0-6,
    // kept in the park).
    bool change_gap(int turn, int left, int right);
    // 0x0059D8E0 / 0x0059D980: destroy the pieces in / flatten the selected area.
    bool destroy_selection();
    void flatten_selection();
    // 0x0059D9F0: raise / lower `area` (at most 10 x 10) to `height` + dir; undone if out of memory.
    bool raise_area(CellBox area, int height, int dir, char level);
    // 0x0059DAA0: raise / lower the cursor's area (the selection, the clipboard's), widened by the pieces on it;
    // not above 17 or below -16.
    bool raise_lower(int dir);
    // 0x0059C900: delete the cursor (its piece first: no longer the cursor's, destroyed).
    static void __cdecl teardown();
    // 0x0059C950: the next (dir 1) / previous (-1) piece of the current set, wrapping; a clipboard set selects a
    // clipboard instead.
    void step_piece_choice(int dir);
    // 0x0059C9B0: the next / previous visible set (if any); a clipboard set selects its clipboard and drops the
    // cursor's piece (mode 5).
    void step_piece_family(int dir);

    // Offset accessors (src/game/park).
    uint8_t* get_bytes() { return reinterpret_cast<uint8_t*>(this); }
    template <typename T>
    T& get_at(uint32_t offset)
    {
        return *reinterpret_cast<T*>(get_bytes() + offset);
    }
    ParkLayout*& hub() { return get_at<ParkLayout*>(0x0); }
    CellBox& get_area() { return get_at<CellBox>(0x4); } // the cells under the cursor
    int& get_rotation() { return get_at<int>(0x38); }
    bool& selecting() { return get_at<bool>(0x48); }               // an area is selected
    GapDetails& current_gap() { return get_at<GapDetails>(0x70); } // the gap being made
    uint32_t& gap_counter() { return get_at<uint32_t>(0xC4); }
    uint8_t*& get_meta() { return get_at<uint8_t*>(0x44); } // the piece the cursor holds (flag 0x200 while it does)
    uint8_t*& get_clipboards() { return get_at<uint8_t*>(0x58); } // the clipboards (next at +0x14)
    uint8_t*& active_copy_buffer() { return get_at<uint8_t*>(0x5C); }
    int& set_index() { return get_at<int>(0x64); }
    int& visible_set_slot() { return get_at<int>(0x68); } // the set's place among the visible ones
    int& get_mode() { return get_at<int>(0x6C); }         // 4: area selection, 5: clipboard

    ParkLayout* park_owner_; // +0x00
    CellBox cell_footprint_; // +0x04
    uint8_t pad0a_[2];
    float move_time_; // +0x0C: of the slide to the target (1 when a step starts)
    int unknown10;
    maths::Vec4 aim_point_; // +0x14
    maths::Vec4 at_point_;  // +0x24
    uint8_t unknown34[4];
    int quarter_turns_;        // +0x38: quarter turns
    float target_angle_;       // +0x3C: degrees
    float angle_;              // +0x40
    PieceGroup* piece_group_;  // +0x44: the piece shown at the cursor
    uint8_t box_select_stage_; // +0x48: the area selection (0 none, 1 first corner set, 2 done)
    CellBox area_start_;       // +0x49: its first corner
    CellBox area_last_;        // +0x4F: the cursor's cell when the highlight was made (the other corner)
    bool area_highlighted_;    // +0x55
    uint8_t pad56[2];
    Clipboard* copy_buffers_; // +0x58: the newest first
    Clipboard* clipboard_;    // +0x5C: the one being pasted
    int clipboard_height_;    // +0x60
    int paste_set_index_;     // +0x64: the clipboard set's index
    int menu_index_;          // +0x68
    int mode_sel_;            // +0x6C
    GapDetails gap_;          // +0x70
    uint32_t gap_counter_;    // +0xC4: names the next gap
};
static_assert(offsetof(BuildPointer, aim_point_) == 0x14 && offsetof(BuildPointer, piece_group_) == 0x44 &&
                  offsetof(BuildPointer, area_start_) == 0x49 && offsetof(BuildPointer, area_highlighted_) == 0x55 &&
                  offsetof(BuildPointer, copy_buffers_) == 0x58 && offsetof(BuildPointer, mode_sel_) == 0x6C &&
                  offsetof(BuildPointer, gap_) == 0x70 && sizeof(BuildPointer) == 200,
              "layout must match the original");

// Park editor script commands (park_cfuncs.cpp).
// 0x0058E120 GetParkEditorMaxPlayers: <MaxPlayers> = the park's player count.
bool __cdecl cfunc_get_park_editor_max_players(script::Struct* params, script::Script* script);
// 0x0058E140 GetParkEditorMaxPlayersPossible: <MaxPlayersPossible> = how many players the park has room for.
bool __cdecl cfunc_get_park_editor_max_players_possible(script::Struct* params, script::Script* script);

extern float g_park_cell_size_x;
extern float g_park_cell_size_y;
extern float g_park_cell_size_z;
extern maths::Vec4 g_rail_origin;
extern memory::PoolPair g_rail_pools;
extern memory::PoolPair g_clipboard_pools;
extern memory::PoolPair g_clipboard_entry_pools;
extern memory::PoolPair g_node_pools;

extern bool g_rail_editor_flag;
} // namespace park_editing
