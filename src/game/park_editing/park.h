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
#include <array>
#include <optional>
#include <span>

#include "formats/park_map_format.h"
#include "game/park_editing/park_rails.h"
#include "engine/maths/vec4.h"
#include "engine/core/mem.h"
#include "engine/core/checked_index.h"
#include "engine/core/slab_bank.h"
#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace render {
struct WorldScene;
}
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
constexpr float kGridPitch = 120.0F;     // 0x0064F41C
constexpr float kGridStepUp = 48.0F;     // 0x0064F420
constexpr float kGridStepAlong = 120.0F; // 0x0064F424
constexpr float kGridCells = 58.0F;      // 0x0064F7A8 (kFloorSize, park_map_format.h, as the float the original reads)
constexpr float kRiserBelowDrop = 1.5F;  // 0x00646744

constexpr std::size_t kGapNameSize = 0x20;             // a gap's name text ("gap", "gap2"...)
constexpr std::size_t kGapLabelSize = 0x80;            // the text of a gap end's EndGap label
constexpr int kFirstGapId = 2000;                      // the GapID the first end gets
constexpr int kNoCellCoord = 0xFF;                     // the low corner before any entry is added: no cell
constexpr std::size_t kFamilyEntryCapacity = 40;       // the entries a piece family's table holds
constexpr std::size_t kNodeCountEntries = 6;             // the sizes get_node_counts reports
constexpr int kShellBlockSets = 8;                      // the shell block numbers 1..8
constexpr std::size_t kParkNameSize = 0x40;            // the park's name text
constexpr std::size_t kBlockedAreaCapacity = 8;        // the areas a park layout can block
constexpr int kShellCells = 56;                         // the park shell's width and length in cells
constexpr int kPlayerEnd = 9;                           // one past the last player number
constexpr int kMaxMarqueeSpan = 10;                     // the widest area (in cells) a marquee may cover
constexpr int kShellHeightCells = 8;                    // the shell's and the build area's height in cells

// Progress markers through the park's rebuilding, compiled out of this build (the empty function 0x005480A0).
inline void mark_progress(const char* /*label*/) {}

constexpr std::size_t kCellBoxByteSize = 6;
// A box of cells: the corner (x, y, z) and the size (w, h, l). y is signed (floor steps below 0 are dug in).
// Passed by value in two stack slots.
struct CellBox {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t h;
    uint8_t l;

    static CellBox get_make(uint8_t x, int8_t y, uint8_t z, uint8_t w, uint8_t height, uint8_t length)
    {
        return {.x = x, .y = y, .z = z, .w = w, .h = height, .l = length};
    }
    // A cell: its x, y (height) and z.
    struct CellAt {
        uint8_t cell_x_{};
        int8_t cell_y_{};
        uint8_t cell_z_{};
    };

    // A box's extent in cells: width, height and length.
    struct CellExtent {
        uint8_t width{};
        uint8_t height{};
        uint8_t length{};
    };

    // 0x005901B0 (the constructor): returns this. (park_pieces.cpp, like the rest.)
    CellBox* assign_value(CellAt cell_at, CellExtent cell_extent);
    // (0x005901E0, byte `index` of x y z w h l, is the fields by name)
    // 0x005901F0: make the box reach the top of the world (h = 127 - y)
    void stretch_vertically();
    // 0x00590200: every height (-100 up 200)
    void raise_ceiling_to_limit();
    // 0x00590210: debug output ("pos=(x,y,z) dims=(w,h,l)")
    void dump_to_log() const;
};
static_assert(sizeof(CellBox) == kCellBoxByteSize, "passed by value in two stack slots");

// Where a piece of a metapiece lies: its cell offset and rotation within the metapiece (a descriptor's first 8 bytes).
struct PiecePlace {
    uint8_t x;
    int8_t y;
    uint8_t z;
    uint8_t rot;
    bool sets_floor_; // +0x04: from the palette entry's flag 0x0CD75A3F: the piece counts as floor - only
                      //        these set the cells' floor heights (set_meta_in_cells), PieceGroup::intersects' precise
                      //        test leaves them out, and their metapiece is kFloorLocked
    std::array<uint8_t, 3> spare_slot5_;
};

// One piece of a metapiece (a descriptor): where it lies and the piece (+0x08) - in a concrete metapiece the placed
// piece (PlacedSlot: the park builder owns it), in an abstract one the name of a source piece or of an inner palette
// metapiece (NamedSlot). The original's one word held either; each kind of metapiece keeps its own kind of slot, so a
// descriptor is 12 bytes as the original's: a metapiece's descriptors are on the park heap, and the park editor's
// memory meter counts that heap's free bytes.
template <typename Piece>
struct PieceSlot : PiecePlace {
    Piece piece;
};
using PlacedSlot = PieceSlot<ParkTile*>;
using NamedSlot = PieceSlot<uint32_t>;
constexpr std::size_t kNamedSlotByteSize = 12;
static_assert(sizeof(NamedSlot) == kNamedSlotByteSize && sizeof(PlacedSlot) == sizeof(PiecePlace) + sizeof(ParkTile*),
              "a descriptor is the original's size: the park heap's use is the player's memory meter");

// A metapiece's descriptors: the first two inside it, the others in an array from the current heap (the park heap
// while the park editor runs), its own - replaced when the metapiece grows or turns, freed with it.
// A slot's own size in cells as it lies (x, z).
struct SlotSize {
    uint8_t across{};
    uint8_t along{};
};

template <typename Piece>
class PieceSlots {
public:
    using Slot = PieceSlot<Piece>;

    // Descriptor `index` (signed: below 2 the first two; piece 2 is the array's first).
    Slot& at(int index) { return index < 2 ? core::at(first_, index) : core::at(extra(), index - 2); }
    // Room for `count` descriptors: a new array (none for 2 or fewer), the extra ones kept when it grows.
    void commission(int count);
    // Which slots are filled and the quarter turns.
    struct SlotTurn {
        uint32_t filled{};
        int turn{};
    };

    // Every descriptor turned `turn` quarter turns within `area` (its new size, already turned), into a new array:
    // `size_of(slot)` gives a slot's own SlotSize.
    template <typename SizeOf>
    void turn(SlotTurn slot_turn, const CellBox& area, SizeOf size_of);

private:
    // An array of `count` descriptors from the current heap (the memory manager's, uninitialised).
    static memory::HeapPtr<Slot> allocate(uint32_t count);
    // The extra descriptors (none without the array).
    std::span<Slot> extra() { return {extra_.get(), extra_ && limit_ > 2 ? limit_ - 2 : 0}; }

    std::array<Slot, 2> first_{};
    memory::HeapPtr<Slot> extra_; // the others (limit_ - 2 of them; piece 2 is the first)
    uint32_t limit_ = 0;          // room for this many
};

// A piece of the scene (what a metapiece is made of; park_pieces.cpp). Two kinds: a source piece (TileTemplate,
// kSource: a piece type - one sector of the "sk5ed" scene, its name and size) and a placed piece (ParkTile, made a
// CopiedPiece: a clone of its source's sector in the "cloned" scene; park_scene.h calls it DuplicatedTile). Both
// start with the sector and the flags. (The original's vtables: this base's 0x0064F3B4, the source's 0x0064F3C8,
// the clone's 0x0064F3D0.)
class ParkPiece : public memory::HeapObject {
public:
    // flag_bits_ (both kinds)
    static constexpr uint32_t kSource = 0x1; // a TileTemplate
    static constexpr uint32_t kCloned = 0x2; // a placed (cloned) piece

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

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t sector() const { return sector_; }
    void set_sector(uint32_t value) { sector_ = value; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }

private:
    uint32_t sector_ = 0;    // +0x04: a placed piece's sector in the "cloned" scene; a source's sector checksum
    uint32_t flag_bits_ = 0; // +0x08

protected:
    ParkPiece() = default;
};

constexpr std::size_t kParkTileIdentOffset = 0x10;
constexpr std::size_t kParkTileBlendSourceOffset = 0x14;
constexpr std::size_t kParkTileAtPointOffset = 0x18;
constexpr std::size_t kParkTileQuarterTurnsOffset = 0x28;
constexpr std::size_t kParkTileByteSize = 0x2C;
// A placed piece (0x2C bytes). Its own size is CopiedPiece's.
class ParkTile : public ParkPiece {
public:
    // flag_bits_ (a placed piece's; TileTemplate has its own)
    static constexpr uint32_t kCursor = 0x4; // the cursor's (never in the generator's list: not unlinked when deleted)
    static constexpr uint32_t kInList = 0x8; // in the generator's list
    static constexpr uint32_t kNoNode20 = 0x20; // (with 0x80) gets no node in the node array
    static constexpr uint32_t kGap = 0x40;      // a gap's trigger piece
    static constexpr uint32_t kNoNode80 = 0x80;
    static constexpr uint32_t kNoRails = 0x100;    // its rails aren't counted
    static constexpr uint32_t kDestroyNow = 0x200; // destroy_piece takes it out at once
    static constexpr uint32_t kDestroy = 0x1000;   // marked: clean_up_out_refs takes it out

    // The fields other code reads and sets.
    [[nodiscard]] ParkTile* following() const { return following_; }
    void set_following(ParkTile* value) { following_ = value; }
    [[nodiscard]] uint32_t ident() const { return ident_; }
    void set_ident(uint32_t value) { ident_ = value; }
    [[nodiscard]] TileTemplate* blend_source() const { return blend_source_; }
    void set_blend_source(TileTemplate* value) { blend_source_ = value; }
    maths::Vec4& at_point() { return at_point_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    [[nodiscard]] int quarter_turns() const { return quarter_turns_; }

private:
    ParkTile* following_ = nullptr;        // +0x0C: the generator's list
    uint32_t ident_ = 0;                   // +0x10: the generator's piece counter when made (the cluster of its rails)
    TileTemplate* blend_source_ = nullptr; // +0x14
    maths::Vec4 at_point_{};               // +0x18
    int quarter_turns_{};                  // +0x28: quarter turns

public:

    // 0x00590250 (folded with other +0x10 getters)
    [[nodiscard]] uint32_t fetch_serial() const;
    // 0x00590670: put it at `position` (and move its sector if `update` is 1)
    void change_place(maths::Vec4* position, int update);
    // A tile's quarter turns and whether the piece is updated.
    struct QuarterTurns {
        int rot{};
        int update{};
    };

    // 0x00590260: turn it to `rot` quarter turns (and its sector if `update` is 1)
    void set_quarter_turns(QuarterTurns quarter_turns);
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
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ParkTile, ident_) == kParkTileIdentOffset && offsetof(ParkTile, blend_source_) == kParkTileBlendSourceOffset &&
                          offsetof(ParkTile, at_point_) == kParkTileAtPointOffset && offsetof(ParkTile, quarter_turns_) == kParkTileQuarterTurnsOffset &&
                          sizeof(ParkTile) == kParkTileByteSize,
                      "layout must match the original (a placed piece: 0x2C bytes)");
    }
};

constexpr std::size_t kTileTemplateExtentOffset = 0x10;
constexpr std::size_t kTileTemplateTriggerScriptOffset = 0x24;
constexpr std::size_t kTileTemplateByteSize = 0x30;
// A source piece (0x30 bytes; park_scene.h calls it TemplateTile): a piece type, one sector of the "sk5ed" scene
// (sector_ is its checksum).
class TileTemplate final : public ParkPiece {
public:
    // flag_bits_ (besides kSource)
    static constexpr uint32_t kGameObject =
        0x400; // the node is a GameObject (read_piece_flags, from the pieces' node array)
    static constexpr uint32_t kAbsentInNetGames = 0x800; // (with kGameObject)
    static constexpr uint32_t kRebuilt = 0x4000;         // rebuilt by rebuild_marked (a trigger the scripts change)

    // 0x005905C0: an empty source piece.
    TileTemplate() { flag_bits() = kSource; }
    // 0x00590600: its size; returns `out`
    maths::Vec4* measure_span(maths::Vec4* out) override;

    // The fields other code reads and sets.
    [[nodiscard]] TileTemplate* following() const { return following_; }
    void set_following(TileTemplate* value) { following_ = value; }
    maths::Vec4& extent() { return extent_; }
    [[nodiscard]] const maths::Vec4& extent() const { return extent_; }
    [[nodiscard]] uint32_t trigger_script() const { return trigger_script_; }
    void set_trigger_script(uint32_t value) { trigger_script_ = value; }
    int& rail_point_total() { return rail_point_total_; }
    [[nodiscard]] int rail_point_total() const { return rail_point_total_; }
    int& num_rail_segments() { return num_rail_segments_; }
    [[nodiscard]] int num_rail_segments() const { return num_rail_segments_; }

private:
    TileTemplate* following_ = nullptr;    // +0x0C
    maths::Vec4 extent_ = {.x = 0, .y = 0, .z = 0, .w = 1.0F}; // +0x10: in world units (x and z whole cells); w = 1
    int field_20_ = 1;                     // +0x20
    uint32_t trigger_script_ = 0;          // +0x24
    int rail_point_total_ = 0;             // +0x28: the points of the rails its node array gives it
    int num_rail_segments_ = 0;            // +0x2C
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(TileTemplate, extent_) == kTileTemplateExtentOffset && offsetof(TileTemplate, trigger_script_) == kTileTemplateTriggerScriptOffset &&
                          sizeof(TileTemplate) == kTileTemplateByteSize,
                      "layout must match the original");
    }
};

// A placed piece as made (0x00590620: an empty cloned piece).
class CopiedPiece final : public ParkTile {
public:
    CopiedPiece() { flag_bits() = kCloned; }
    // 0x00590710: the source's size, x and z swapped when turned a quarter; returns `out`
    maths::Vec4* measure_span(maths::Vec4* out) override;
};

// A metapiece (abstract or concrete; 0x3C bytes - the descriptors in each kind's own slots; park_metapieces.cpp): its
// pieces, by descriptor. (The original's vtables: the base's 0x0064F3BC, PlacedPieceGroup's 0x0064F3D8,
// PieceBlueprint's 0x0064F3E4.)
class PieceGroup : public memory::HeapObject {
public:
    // The flags (flag_bits_).
    static constexpr uint32_t kConcrete = 0x1; // a PlacedPieceGroup (only these are highlighted by an area selection)
    static constexpr uint32_t kFlag1 = kConcrete;
    static constexpr uint32_t kAbstract = 0x2; // a PieceBlueprint (a palette entry)
    static constexpr uint32_t kInPark = 0x4;   // placed in the park (not the cursor's own piece)
    static constexpr uint32_t kBuiltShell =
        0x8; // part of the floor / shell the park builds itself (risers, walls); is_riser
    static constexpr uint32_t kFlag20 =
        0x20; // being moved by a floor change (kept when an area is cleared with flag 4)
    static constexpr uint32_t kSector =
        0x80; // a single-piece palette entry (named after its piece, until "name" renames it)
    static constexpr uint32_t kFence = 0x100;   // a fence piece of the inner shell
    static constexpr uint32_t kCursor = 0x200;  // the cursor's piece
    static constexpr uint32_t kNoRails = 0x400; // no_rails
    static constexpr uint32_t kFlag400 = kNoRails;
    static constexpr uint32_t kGap = 0x800;            // the gap tool (special_type gap_placement)
    static constexpr uint32_t kHighlightable = 0x1000; // has a gap attached: highlighted while a gap is placed
    static constexpr uint32_t kRestartFlag = 0x2000;   // special_type red/green/blue/yellow_flag
    static constexpr uint32_t kFloorLocked =
        0x4000; // has a piece flagged 0x0CD75A3F: stops the floor under it being changed
    static constexpr uint32_t kMarqueeTool = 0x8000; // the clipboard's area select tool (special_type area_selection)
    static constexpr uint32_t kRailPlacement = 0x10000; // the rail tool (special_type rail_placement)
    static constexpr uint32_t kRiserBelow = 0x20000;

    PieceGroup(const PieceGroup&) = delete;
    PieceGroup& operator=(const PieceGroup&) = delete;
    PieceGroup(PieceGroup&&) = delete;
    PieceGroup& operator=(PieceGroup&&) = delete;
    // 0x005917E0 (0x00591670 / 0x00591BB0 the deleting ones): the extra descriptors freed (each kind's slots).
    virtual ~PieceGroup() = default;
    // Piece `index` (null past the end): a placed metapiece's cloned piece, a palette entry's source piece.
    virtual ParkPiece* get_piece(uint32_t index) = 0;
    // A piece added at the cells `pos` (x y z), turned `rot`.
    virtual void insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor) = 0;
    // 0x00590340: where descriptor `index` lies (signed: below 2 the first two)
    virtual PiecePlace* get_descriptor(int index) = 0;
    // 0x00590AD0: room for `count` pieces (the extra ones kept when it grows)
    virtual void commission(int count) = 0;

    // The fields other code reads and sets.
    CellBox& cell_footprint() { return cell_footprint_; }
    [[nodiscard]] const CellBox& cell_footprint() const { return cell_footprint_; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] int quarter_turns() const { return quarter_turns_; }
    void assign_quarter_turns(int value) { quarter_turns_ = value; }
    uint32_t& filled_slots() { return filled_slots_; }
    [[nodiscard]] uint32_t filled_slots() const { return filled_slots_; }
    uint32_t& name_key() { return name_key_; }
    [[nodiscard]] uint32_t name_key() const { return name_key_; }

private:
    CellBox cell_footprint_ = {.x = 0, .y = 0, .z = 0, .w = 0, .h = 0, .l = 0}; // +0x04 (the offsets are the original's: the vtable at +0x00)
    std::array<uint8_t, 2> pad0a_{};
    uint32_t flag_bits_ = 0;               // +0x0C
    int quarter_turns_{};                  // +0x10: quarter turns
    uint32_t filled_slots_ = 0;            // (+0x34 in the original)
    uint32_t name_key_{};                  // (+0x38): the abstract metapiece's name

public:

    // 0x005902E0 (returns this; folded with many empty constructors): a metapiece as a concrete one
    PieceGroup* as_placed_piece() { return this; }
    PieceGroup* get_cast() { return this; }
    // 0x005902F0: one of the eight restart pieces (by its name)
    [[nodiscard]] bool is_spawn_marker() const;
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
    PiecePlace* get_entry(int index) { return get_descriptor(index); }
    // 0x00590400: how many pieces, a palette entry of several pieces counted by its pieces (an abstract metapiece)
    int count_pieces();
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
    // Every descriptor turned `turn` quarter turns (the metapiece's area already turned).
    virtual void turn_descriptors(int turn) = 0;

private:
    // Descriptor `index`'s placed piece (a concrete metapiece's) / its name (an abstract one's).
    ParkTile* placed_piece(uint32_t index);
    uint32_t piece_name(uint32_t index);
};

constexpr std::size_t kPlacedPieceGroupByteSize = 0x3C;
// A metapiece placed in the park or held by the cursor: its pieces are cloned pieces.
class PlacedPieceGroup final : public PieceGroup {
public:
    // 0x00590B60: an empty one (kConcrete, unturned).
    PlacedPieceGroup();
    // 0x00590360: piece `index` (null past the end)
    ParkTile* get_piece(uint32_t index) override;
    // 0x00590ED0: `piece` (a placed piece) added at the cells `pos` (x y z), turned `rot`; the piece's position set to
    // its centre there
    void insert_tile(ParkPiece* placed, CellBox* pos, int rot, bool sets_floor) override;
    PiecePlace* get_descriptor(int index) override { return &slots_.at(index); }
    void commission(int count) override;
    // Descriptor `index`, with its placed piece.
    PlacedSlot& slot(int index) { return slots_.at(index); }

protected:
    void turn_descriptors(int turn) override;

private:
    PieceSlots<ParkTile*> slots_; // +0x14
};

constexpr std::size_t kPieceBlueprintByteSize = 0x3C;
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
    PiecePlace* get_descriptor(int index) override { return &slots_.at(index); }
    void commission(int count) override;
    // Descriptor `index`, with its piece's name.
    NamedSlot& slot(int index) { return slots_.at(index); }
    // A meta piece's quarter turns and whether it sets the floor.
    struct MetaPlacement {
        int rot{};
        uint8_t sets_floor_{};
    };

    // 0x00591100: a palette metapiece added by name (null only counted), the area grown to take it in
    void add_meta_piece(PieceGroup* meta, CellBox* pos, MetaPlacement meta_placement);
    // 0x005911D0: entry `index` counting the pieces of the palette metapieces it's made of one by one (each placed
    // and turned in this one); returns `out`
    NamedSlot* get_entry(NamedSlot* out, int index);

protected:
    void turn_descriptors(int turn) override;

private:
    PieceSlots<uint32_t> slots_; // +0x14
};
static_assert(sizeof(void*) != 4 || (sizeof(PlacedPieceGroup) == kPlacedPieceGroupByteSize && sizeof(PieceBlueprint) == kPieceBlueprintByteSize),
              "a metapiece is the original's size: the park heap's use is the player's memory meter");

constexpr std::size_t kMapListNodeByteSize = 12;
// A node of a metapiece list (12 bytes, pooled: the pool pair 0x007CE5F0).
struct MapListNode {
    // The fields other code reads and sets.
    [[nodiscard]] int dy() const { return dy_; }
    void set_dy(int value) { dy_ = value; }
    [[nodiscard]] PieceGroup* piece_group() const { return piece_group_; }
    void set_piece_group(PieceGroup* value) { piece_group_ = value; }
    [[nodiscard]] MapListNode* following() const { return following_; }
    void set_following(MapListNode* value) { following_ = value; }

private:
    int dy_;                  // in the floor change's list: how far the metapiece moves up (down if negative)
    PieceGroup* piece_group_; // +0x04
    MapListNode* following_;  // +0x08

public:

    // 0x005480C0 (one of many getters folded together there)
    [[nodiscard]] PieceGroup* get_piece_group() const { return piece_group_; }
    PieceGroup* get_placed_piece() { return piece_group_->as_placed_piece(); }

    // A new node from the pools (or the heap), zeroed; null if out of memory.
    static MapListNode* fabricate();
    // 0x005914B0: back to the pools (operator delete).
    static void dealloc(void* node);
    // 0x00591BE0: free a list (the nodes, not the metapieces; null is fine)
    static void free_node_chain(MapListNode* list);
    // 0x00590470: walks the list (a debug check with its asserts compiled out; never ends on a cycle).
    static void audit_soundness(const MapListNode* list);
};
static_assert(sizeof(MapListNode) == kMapListNodeByteSize, "pool item");

// The holder of a list returned by the manager's lookups (0x004E1970 makes it, 0x00469490 reads it, 0x00591C60
// frees the list).
struct MapListHolder {
    // The fields other code reads and sets.
    [[nodiscard]] MapListNode* list() const { return list_; }

private:
    MapListNode* list_;

public:
    explicit MapListHolder(MapListNode* list) : list_(list) {}
    ~MapListHolder()
    {
        if (list_) {
            MapListNode::free_node_chain(list_);
        }
    }
    MapListHolder(const MapListHolder&) = delete;
    MapListHolder& operator=(const MapListHolder&) = delete;
};
// The same holder as plain data, for code that frees the list itself (0x00591C60).
struct CellChainScratch {
    // A list the park manager handed out (freed by teardown).
    explicit CellChainScratch(MapListNode* list) : list_(list) {}

    [[nodiscard]] MapListNode* list() const { return list_; }

private:
    MapListNode* list_;

public:

    // 0x00591450: debug output of the list (each metapiece's address and cells)
    void dump_to_log();
    // 0x00591C60: the destructor (the list freed)
    void teardown();
};

constexpr std::size_t kGapDetailsTextOffset = 0x24;
constexpr std::size_t kGapDetailsHalvesPlacedOffset = 0x48;
constexpr std::size_t kGapDetailsByteSize = 0x54;
// A gap (0x54 bytes; 0x0058CBC0 constructs the two ends): the gap tool's state, and how the gap manager keeps one.
struct GapDetails {
    // The fields other code reads and sets.
    std::array<CellBox, 2>& at_point() { return at_point_; }
    [[nodiscard]] const std::array<CellBox, 2>& at_point() const { return at_point_; }
    std::array<int, 2>& quarter_turns() { return quarter_turns_; }
    [[nodiscard]] const std::array<int, 2>& quarter_turns() const { return quarter_turns_; }
    std::array<int, 2>& reach_left() { return reach_left_; }
    [[nodiscard]] const std::array<int, 2>& reach_left() const { return reach_left_; }
    std::array<int, 2>& reach_right() { return reach_right_; }
    [[nodiscard]] const std::array<int, 2>& reach_right() const { return reach_right_; }
    std::array<char, kGapNameSize>& text() { return text_; }
    [[nodiscard]] const std::array<char, kGapNameSize>& text() const { return text_; }
    [[nodiscard]] int points_held() const { return points_held_; }
    void set_points_held(int value) { points_held_ = value; }
    [[nodiscard]] int halves_placed() const { return halves_placed_; }
    void set_halves_placed(int value) { halves_placed_ = value; }
    [[nodiscard]] int slot_index() const { return slot_index_; }
    void set_slot_index(int value) { slot_index_ = value; }
    [[nodiscard]] uint32_t cancel_flags() const { return cancel_flags_; }
    void set_cancel_flags(uint32_t value) { cancel_flags_ = value; }

private:
    std::array<CellBox, 2> at_point_{};   // +0x00: the cells of its two halves
    std::array<int, 2> quarter_turns_{};  // +0x0C (0-3; 4 bits each in the map)
    std::array<int, 2> reach_left_{};     // +0x14 (0-6 cells)
    std::array<int, 2> reach_right_{};    // +0x1C
    std::array<char, kGapNameSize> text_{};       // +0x24: its name ("gap", "gap2"...)
    int points_held_{};       // +0x44
    int halves_placed_{};     // +0x48: how many of its halves are placed (2 for a whole gap)
    int slot_index_{};        // +0x4C: the gap manager's index (-1 none)
    uint32_t cancel_flags_{}; // +0x50

public:

    // 0x0058CBC0: the halves' cells cleared (the rest isn't touched).
    GapDetails();
    // 0x0058CBF0: the length of half `half` across the gap, in the world's units: its extents and its piece's length
    // (turned 0 / 2) or width (1 / 3); 0 for another turn.
    float fetch_magnitude(int half);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(GapDetails, text_) == kGapDetailsTextOffset && offsetof(GapDetails, halves_placed_) == kGapDetailsHalvesPlacedOffset &&
                          sizeof(GapDetails) == kGapDetailsByteSize,
                      "layout must match the original");
    }
};
using GapRecord = GapDetails;

constexpr std::size_t kClipboardEntryQuarterTurnsOffset = 0x08;
constexpr std::size_t kClipboardEntryPreviewOffset = 0x10;
constexpr std::size_t kClipboardEntryByteSize = 0x18;
// A metapiece copied into a clipboard (0x18 bytes, pooled: the pool pair 0x007CE5C4; copy_buffer.cpp): the palette
// piece it was made from, its cell and size where it was copied (raised so the lowest piece of the copy is at 0), its
// turn, and the preview shown at the cursor. `centre_x` / `centre_z` below are the copy's centre cell.
struct ClipboardEntry {
    // The fields other code reads and sets.
    [[nodiscard]] PieceGroup* piece_group() const { return piece_group_; }
    void set_piece_group(PieceGroup* value) { piece_group_ = value; }
    [[nodiscard]] uint8_t pos_x() const { return pos_x_; }
    void set_pos_x(uint8_t value) { pos_x_ = value; }
    [[nodiscard]] int8_t y() const { return y_; }
    void set_y(int8_t value) { y_ = value; }
    [[nodiscard]] uint8_t depth_cell() const { return depth_cell_; }
    void set_depth_cell(uint8_t value) { depth_cell_ = value; }
    [[nodiscard]] int quarter_turns() const { return quarter_turns_; }
    void set_quarter_turns(int value) { quarter_turns_ = value; }
    [[nodiscard]] uint8_t w() const { return w_; }
    void set_w(uint8_t value) { w_ = value; }
    [[nodiscard]] uint8_t l() const { return l_; }
    void set_l(uint8_t value) { l_ = value; }
    [[nodiscard]] PieceGroup* preview() const { return preview_; }
    void set_preview(PieceGroup* value) { preview_ = value; }
    [[nodiscard]] ClipboardEntry* following() const { return following_; }
    void set_following(ClipboardEntry* value) { following_ = value; }

private:
    PieceGroup* piece_group_; // +0x00: the abstract metapiece
    uint8_t pos_x_;           // +0x04
    int8_t y_;                // +0x05
    uint8_t depth_cell_;      // +0x06
    uint8_t pad07;
    int quarter_turns_; // +0x08: quarter turns
    uint8_t w_;         // +0x0C
    uint8_t l_;         // +0x0D
    std::array<uint8_t, 2> pad0e_;
    PieceGroup* preview_;       // +0x10: shown at the cursor (kCursor)
    ClipboardEntry* following_; // +0x14

public:

    // 0x0058B800: the cells it would take with the cursor's copy (turned with the cursor; one high, at its height).
    void fetch_extent(CellBox* area, uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058B940: whether the floor under it can be changed there
    bool can_place(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // Where a copy is grafted: the centre cell (x, z) and the height.
    struct GraftPlace {
        uint8_t centre_x_{};
        int height{};
        uint8_t centre_z_{};
    };

    // 0x0058B9C0: pasted: the floor raised to its height plus `height` (at most 15; an inner-shell piece's own height
    // on top), then (unless it's of the inner shell) a new metapiece turned with the cursor put there
    void graft(GraftPlace graft_place, BuildPointer* cursor);
    // 0x0058BB20: the floor height under it
    int fetch_tallness(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor);
    // 0x0058BBA0: highlight (or not) the metapieces (not the inner shell's) in its cells
    void glow_overlapping_pieces(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor, int on);
    // 0x0058BCA0: its preview (made the first time) placed at `position` turned by `angle` degrees
    void show_preview(uint8_t centre_x, int height, uint8_t centre_z, maths::Vec4 position, float angle);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ClipboardEntry, quarter_turns_) == kClipboardEntryQuarterTurnsOffset && offsetof(ClipboardEntry, preview_) == kClipboardEntryPreviewOffset &&
                          sizeof(ClipboardEntry) == kClipboardEntryByteSize,
                      "pool item");
    }
};

constexpr std::size_t kClipboardByteSize = 0x18;
// A copied area (0x18 bytes, pooled: the pool pair 0x007CE5B4; copy_buffer.cpp): the metapieces (and rails) copied from
// it.
struct Clipboard {
    // The fields other code reads and sets.
    [[nodiscard]] Clipboard* following() const { return following_; }
    void set_following(Clipboard* value) { following_ = value; }

private:
    CellBox edit_span_{};           // +0x00
    uint8_t lo_cell_x_{kNoCellCoord}, lo_cell_z_{kNoCellCoord}; // +0x06: the cells its entries cover
    uint8_t hi_cell_x_{0}, hi_cell_z_{0};       // +0x08
    std::array<uint8_t, 2> pad0a_{};
    ClipboardEntry* metas_{nullptr}; // +0x0C: its entries (the newest first)
    void* rail_groups_{nullptr};    // +0x10: the rail editor's copies of the rails over it (linked by their first word)
    Clipboard* following_{nullptr}; // +0x14: the cursor's list

public:

    // 0x0058BDF0
    Clipboard();
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
    maths::Vec4* get_centre(maths::Vec4* centre) const;
    // 0x0058C130: show the copy at `position`, turned by `angle` (its rails too)
    void preview_pieces(maths::Vec4 position, float angle, int height);
    // 0x0058C200: remove what preview_pieces made
    void free_all_piece_groups();
    // 0x0058C780: paste the copy at the cursor: false (nothing changed) if the park memory has no room for it, the
    // floor can't be changed under an entry or there aren't enough free rail points
    bool show_at(BuildPointer* cursor);
    // 0x0058BF10: the area it covers placed at the cursor (turned with it)
    void fetch_extent(BuildPointer* cursor, CellBox* area) const;
};
static_assert(sizeof(Clipboard) == kClipboardByteSize, "pool item");

constexpr std::size_t kPieceFamilyEntryTotalOffset = 0x148;
constexpr std::size_t kPieceFamilyIsDisplayedOffset = 0x150;
constexpr std::size_t kPieceFamilyByteSize = 0x154;
// A piece set: the entries of one of the piece menu's sets (0x154 bytes, Ed_piece_sets).
struct PieceFamily {
    struct TableRow {
        uint32_t checksum; // the abstract metapiece (or sector)
        const char* name;  // shown in the menu (null: "---")
    };
    uint32_t name_checksum_; // +0x000
    const char* label_;      // +0x004: shown in the set menu (set_name)
    std::array<TableRow, kFamilyEntryCapacity> entries_;   // +0x008
    int entry_total_;        // +0x148
    int selected_;           // +0x14C
    bool is_displayed_;      // +0x150: has a menu entry
    bool is_clipboard_;      // +0x151: the clipboard set
    std::array<uint8_t, 2> pad152;
};
static_assert(offsetof(PieceFamily, entry_total_) == kPieceFamilyEntryTotalOffset && offsetof(PieceFamily, is_displayed_) == kPieceFamilyIsDisplayedOffset &&
                  sizeof(PieceFamily) == kPieceFamilyByteSize,
              "layout must match the original");
using TileFamilyRef = PieceFamily;

constexpr std::size_t kFloorCellByteSize = 16;
// A cell of the floor (16 bytes).
struct FloorCell {
    int level_y_;           // in floor steps (-16 to 15)
    uint32_t heights_used_; // +0x04: bit (y + 17) for each level a piece fills
    bool touched_;          // +0x08: changed by the floor change under way
    std::array<uint8_t, 3> pad9;
    int delta_; // +0x0C: by how much
};
static_assert(sizeof(FloorCell) == kFloorCellByteSize, "grid cell");

// The park as saved (0x3A98 bytes): a header, the floor heights, then the metapieces (8 bytes each: name index,
// cell, height, turn) and the gaps (0x30 bytes each). The park layout keeps the map as these bytes (saved as an
// integer array, sent to the other players as they are): ParkMap (park_map_format.h) reads and writes them.

// The gap manager (the singleton 0x007CE5D4; the park manager keeps it at +0x177C4): the gaps placed in the park.
class GapRegistry;

constexpr std::size_t kGapRegistryGapByteSize = 0xEC;
constexpr std::size_t kGapRegistryGapDetailsOffset = 0x98;
constexpr std::size_t kGapRegistryStartedFirstOffset = 0x84;
constexpr std::size_t kGapRegistryParkOwnerOffset = 0xA4;
constexpr std::size_t kGapRegistryByteSize = 0xA8;
class GapRegistry {
public:
    static GapRegistry*& sole();

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
    explicit GapRegistry(ParkLayout* manager);

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
    ~GapRegistry();
    GapRegistry(const GapRegistry&) = delete;
    GapRegistry& operator=(const GapRegistry&) = delete;

    // --- gap_manager.cpp (0x0058CD40-0x0058D290) ---
    // One gap (0xEC bytes, made by StartGap / EndGap): the ids of its two ends, their trigger pieces and
    // metapieces, its name and its GapDetails.
    struct Gap {
        std::array<uint32_t, 2> ids_{};      // +0x00: each end's GapID (the manager's counter when it was made)
        std::array<ParkTile*, 2> pieces_{};  // +0x08: the trigger pieces
        std::array<PieceGroup*, 2> metas_{}; // +0x10
        std::array<char, kGapLabelSize> label_{};     // +0x18: the text of the gap's EndGap
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

    // The fields other code reads and sets.
    [[nodiscard]] const std::array<bool, kMaxGaps>& started_first() const { return started_first_; }

private:
    uint32_t next_id_{kFirstGapId};       // +0x00: the next end's GapID
    std::array<Gap*, kMaxGaps> gaps_ptr_{};    // +0x04
    std::array<bool, kMaxGaps> started_first_{}; // +0x84: the gap's first half (StartGap) was placed first
    ParkLayout* park_owner_;       // +0xA4: the park it's in (null in a copy)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(GapRegistry::Gap) == kGapRegistryGapByteSize && offsetof(GapRegistry::Gap, details_) == kGapRegistryGapDetailsOffset &&
                          offsetof(GapRegistry, gaps_ptr_) == 4 && offsetof(GapRegistry, started_first_) == kGapRegistryStartedFirstOffset &&
                          offsetof(GapRegistry, park_owner_) == kGapRegistryParkOwnerOffset && sizeof(GapRegistry) == kGapRegistryByteSize,
                      "layout must match the original");
    }
};

constexpr std::size_t kRestartPointEditSpanOffset = 0x14;
constexpr std::size_t kRestartPointKindTagOffset = 0x1C;
constexpr std::size_t kRestartPointByteSize = 0x24;
// A restart point of the park generator (0x24 bytes).
struct RestartPoint {
    using KindTag = uint8_t;
    static constexpr int kFree = 0;
    static constexpr int kPlayer1 = 1;
    static constexpr int kPlayer2 = 2;
    static constexpr int kHorse = 3;
    static constexpr int kCrownRestart = 4;
    static constexpr int kRedFlag = 5; // 5-8 the CTF flags: red, green, blue, yellow
    static constexpr int kYellowFlag = 8;
    maths::Vec4 at_point_; // +0x00
    int quarter_turns_;    // +0x10
    CellBox edit_span_;    // +0x14
    std::array<uint8_t, 2> pad1a_;
    int kind_tag_;      // +0x1C
    uint8_t automatic_; // +0x20: put down by the park itself (generate_node_array), not placed (the byte given)
    std::array<uint8_t, 3> pad21;
};
static_assert(offsetof(RestartPoint, edit_span_) == kRestartPointEditSpanOffset && offsetof(RestartPoint, kind_tag_) == kRestartPointKindTagOffset &&
                  sizeof(RestartPoint) == kRestartPointByteSize,
              "layout must match the original");

// What build_source_rails keeps of each node of the pieces' node array (park_scene.h).
struct PieceNodeInfo;

constexpr std::size_t kParkAssemblerSourceRailsOffset = 0x24;
constexpr std::size_t kParkAssemblerRailsOffset = 0x58;
constexpr std::size_t kParkAssemblerNodeInfoOffset = 0x8C;
constexpr std::size_t kParkAssemblerRestartsOffset = 0x98;
constexpr std::size_t kParkAssemblerLightTotalOffset = 0x320;
constexpr std::size_t kParkAssemblerLightsOffset = 0x33C;
constexpr std::size_t kParkAssemblerField43cOffset = 0x43C;
// The park generator (0x4A4 bytes): the scene side of the park editor (park_scene.h).
class ParkAssembler {
public:
    // flag_bits_
    static constexpr uint32_t kSourcesLoaded = 0x2; // load_source_pieces done
    static constexpr uint32_t kPiecesCreated = 0x4; // (create_piece sets it)
    static constexpr uint32_t kHasPieces = 0x8;
    static constexpr uint32_t kSuperSectorsUpdated = 0x10;
    static constexpr uint32_t kNodeArrayBuilt = 0x20;
    static constexpr uint32_t kSceneChanged =
        0x40; // sectors added or deleted since the last update of the super sectors
    static constexpr int kRestartTableSize = 18;
    static constexpr int kLightCapacity = 16;

    // --- park_scene.cpp, park_nodes.cpp (0x00592740-0x00595030; their types: park_scene.h) ---
    // 0x005933F0: take away the restart point of type `player` at `area` (its cell x, y, z)
    void unplant_restart_point(CellBox* area, int player);
    // 0x005935F0: mark a piece for clean_up_out_refs (taken out at once if it has DuplicatedTile::kDestroyNow)
    void destroy_piece(void* memory);
    // 0x00593610: take out and delete the pieces marked by destroy_piece
    void clean_up_out_refs();
    // 0x005947D0: every piece's sector deleted and cloned again (the node array dropped); 0x00594910: those of the
    // pieces whose type has TemplateTile::kRebuilt
    void rebuild_all();
    void rebuild_marked();
    // 0x00593460: the constructor (no pieces, restarts or rails; 2 players).
    ParkAssembler();
    // 0x00592FE0: light `index` at `position`
    void move_light_source(const maths::Vec4* position, int index);
    // 0x00593010: `rgba`'s red, green and blue (0-255) lit at `position`: the ambient part plus the falloff part of
    // the lights' sum of 1 / (160000 + squared distance in x and z); over 255 is 255.
    uint32_t sample_light_colour(const maths::Vec4* position, uint32_t rgba); // (r g b a from the low byte; alpha kept)
    // 0x00594410: a piece's vertex colours (from its source's) and its collision's vertex intensities lit
    void light_piece(ParkTile* source);
    // 0x00593370: how many restart points of `type` there are
    int count_restarts(int type);
    // 0x00593500: a gap's trigger piece (the type 0xC0341DF7) at `position` turned `rot` quarter turns, added
    void* make_gap_marker_piece(maths::Vec4* position, float length, int rot);
    // 0x00594730: a piece's sector cloned from its source into the scene, placed, lit (a gap piece told the gap
    // manager)
    void add_piece_to_scene(ParkTile* tile);
    // 0x00594610: every piece taken out and deleted (with the rail editor's rails taken out of the scene first),
    // then every source piece deleted
    void unload_pieces();
    // 0x00594C90: the source pieces from the "sk5ed" scene's sectors (their sizes from their boxes), the "cloned"
    // scene, the bounds; the restart points freed (the argument unused)
    void load_source_pieces(int unused);
    // The level whose pieces are loaded (and the original's unused word).
    struct PieceLoad {
        uint32_t level{};
        int unused{};
    };

    // 0x00594FD0: load_source_pieces, then the source pieces' flags and rails from the pieces' node array (the
    // rails on the top-down heap for the park editor's own level 0xCD3682C1)
    void load_pieces(PieceLoad piece_load);
    // 0x00592A10: the point of the placed pieces' rails nearest to `position` (and the squared distance); false if
    // none is nearer than 1e8 squared units
    bool find_nearest_rail_point(const maths::Vec4* position, maths::Vec4* nearest, float* distance_squared);
    // 0x00592AB0: the node array's sizes: [0] nodes, [1] rail points, [2] rail segments, [3] / [4] memory
    // estimates, [5] rail points again
    void get_node_counts(std::span<int, kNodeCountEntries> counts);
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
    static uint32_t next_geometry_node(script::Array* nodes, int* index);
    // 0x005927D0: the next unused rail node of the piece `geometry` (after the node named `previous`, or a first
    // one: one with no links, else a linked one - `*is_loop` set); its name (0 if none)
    uint32_t find_following_rail_node(script::Array* nodes, checksums::Name geometry_name, uint32_t previous, RailNodeInfo* info,
                                      bool* is_loop);
    // 0x005936E0: the source rails from the pieces' node array (each source piece's rail point and segment counts)
    void build_source_rails(script::Array* nodes);
    // --- park_generator.cpp (0x00591C70-0x00592740) ---
    // 0x00591C70: the destructor's body (the scenes forgotten, the node array's asset destroyed)
    ~ParkAssembler();
    ParkAssembler(const ParkAssembler&) = delete;
    ParkAssembler& operator=(const ParkAssembler&) = delete;
    void teardown() { this->~ParkAssembler(); }
    // 0x00591CF0: every piece tinted red, or back to normal
    void glow_every_piece(bool on);
    // 0x00591D20
    void set_player_cap(int count);
    // The most players the park is set up for (a whole int; the compressed map keeps its low byte)
    [[nodiscard]] int get_player_limit() const { return max_players_; }
    // 0x00591D30: the most players the park has room for: one more for each 585500 bytes the main heap has free
    // (counting those already set up)
    [[nodiscard]] int get_player_cap_limit() const;
    // 0x00591D90: the piece type named `name` (null if none)
    TileTemplate* get_piece_source(uint32_t name);
    // 0x00591DD0: delete the park's sector `name` (false if there's none)
    bool remove_scene_sector(uint32_t name, int unused);
    // 0x00591DE0: the placed pieces' rails emptied and their pools freed
    void destroy_rails();
    // The park's lighting: the ambient colour, the lights' falloff per channel and the cursor's ambience.
    struct ParkLighting {
        float ambient_r_{};
        float ambient_g_{};
        float ambient_b_{};
        float falloff_r_{};
        float falloff_g_{};
        float falloff_b_{};
        float cursor_ambience_{};
    };

    // 0x00591E00: the lights' count, ambient and falloff colours and the cursor's ambience
    void configure_lighting(int count, const ParkLighting& lighting);
    // 0x00591E60: the restart slots (first to last) a restart type uses (unknown types: left as they were)
    static void fetch_spawn_slots(int type, int* first, int* last);
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
    static void update_bounds(uint32_t unused);
    // 0x00592290: the gaps' trigger pieces (ParkTile::kGap) shown or hidden
    void set_pieces_highlighted(int on);
    // 0x005922D0: the restart points' pieces drawn and collided with, or neither
    void set_restart_pieces_active(bool active);
    // 0x00592320: a copy of the "sk5ed" sector `name` in the "cloned" scene (its name)
    uint32_t duplicate_scene_sector(uint32_t name, int a, int b, int unused);
    // 0x00592370: delete the park's sector `name`, then rebuild the collision (false if there was none)
    bool destroy_sector(uint32_t name, int unused);
    // 0x005923C0: a new placed piece of `source` (or of its source), with `flags` (0x324 of them)
    gsl::owner<ParkTile*> create_piece(ParkPiece* source, uint32_t flags);
    // 0x00592550: a placed piece onto the front of the generator's list (light_piece, 0x00594410, then lights it)
    void insert_tile(ParkTile* piece);
    // 0x00592570: a piece taken off the list (not the cursor's), its sector deleted (unless `keep_sector`), deleted
    void delete_piece(ParkTile* piece, int keep_sector);
    // 0x005925E0: the piece types' flags and trigger scripts from the pieces' node array
    void read_piece_flags(script::Array* nodes);
    // A restart point's type, whether it is automatic and the placed restarts kept.
    struct RestartKind {
        int type{};
        int automatic{};
        uint32_t keep_placed_{};
    };

    // 0x00593200: a restart point of `type` at `position` (the cells `area`) in the first free slot of its type (or,
    // when `automatic`, one put down automatically before); with `automatic` and `keep_placed`, where a placed one
    // of the type is. False when there's no room.
    bool plant_restart_point(maths::Vec4* position, int rot, CellBox* area, RestartKind restart_kind);
    // 0x005933D0: whether there are fewer than `count` restart points of `type`
    bool needs_restarts(int type, int count);
    // 0x00594EA0: the level's node array from the park (the pieces, their rails, the rail editor's, the restart
    // points, the goal editor's), given to the node manager and parsed
    void build_node_array(MapListNode* metas);
    // 0x00593580: delete the scene's pieces (`keep_rails` 0: the rail editor's rails go out of the scene too)
    void destroy_pieces(int keep_rails);
    // 0x00594C70: unload_pieces, and the source rails freed (the argument unused)
    void sweep_up(int type);

    // The fields other code reads and sets.
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] ParkTile* pieces() const { return pieces_; }
    void set_pieces(ParkTile* value) { pieces_ = value; }
    [[nodiscard]] render::WorldScene* scene_handle() const { return scene_handle_; }
    std::optional<assets::NodeTableResource>& node_table() { return node_table_; }
    [[nodiscard]] const std::optional<assets::NodeTableResource>& node_table() const { return node_table_; }

private:
    uint32_t flag_bits_{0}; // +0x000
    uint32_t field_04_{0};
    ParkTile* pieces_{nullptr};                 // +0x008
    TileTemplate* sources_{nullptr};            // +0x00C
    render::WorldScene* source_world_{nullptr}; // +0x010: "sk5ed"
    render::WorldScene* scene_handle_{nullptr}; // +0x014: "cloned"
    uint32_t field_18_{};
    static constexpr int kFirstPieceId = 0x20; // the first id a placed piece gets
    uint32_t next_piece_id_{kFirstPieceId};    // +0x01C (from 0x20)
    int max_players_{2};                       // +0x020
    ParkRailSet source_rails_;                 // +0x024: the pieces' rails, from their node array
    ParkRailSet rails_;                        // +0x058: the placed pieces' rails, while the node array is built
    PieceNodeInfo* node_info_{};               // +0x08C: (while build_source_rails runs)
    int* used_nodes_{};                        // +0x090: (likewise) the rail nodes taken
    int num_used_nodes_{};                     // +0x094
    std::array<RestartPoint, kRestartTableSize> restarts_{}; // +0x098: by type - 0 player 2, 1 player 1, 2-7 horse, 8-13 king of
                                                 // the hill, 14-17 the flags (spawn_slot_array)
    int light_total_{};                          // +0x320
    std::array<float, 3> ambient_{};                         // +0x324 (rgb)
    std::array<float, 3> falloff_{};                         // +0x330 (rgb)
    std::array<maths::Vec4, kLightCapacity> lights_{};       // +0x33C
    uint32_t field_43c_{};
    // +0x440: the node array's owner (made by the constructor, destroyed first by the destructor - the original made
    // it in place in its storage)
    std::optional<assets::NodeTableResource> node_table_;
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ParkAssembler, source_rails_) == kParkAssemblerSourceRailsOffset && offsetof(ParkAssembler, rails_) == kParkAssemblerRailsOffset &&
                          offsetof(ParkAssembler, node_info_) == kParkAssemblerNodeInfoOffset && offsetof(ParkAssembler, restarts_) == kParkAssemblerRestartsOffset &&
                          offsetof(ParkAssembler, light_total_) == kParkAssemblerLightTotalOffset && offsetof(ParkAssembler, lights_) == kParkAssemblerLightsOffset &&
                          offsetof(ParkAssembler, field_43c_) == kParkAssemblerField43cOffset,
                      "layout must match the original");
    }
};

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

constexpr std::size_t kParkLayoutShellBlocksOffset = 0x14;
constexpr std::size_t kParkLayoutConcreteMetasOffset = 0x7C;
constexpr std::size_t kParkLayoutCellMetasOffset = 0x8C;
constexpr std::size_t kParkLayoutShellSizeOffset = 0x351C;
constexpr std::size_t kParkLayoutFloorOffset = 0x352C;
constexpr std::size_t kParkLayoutSetsOffset = 0x1076C;
constexpr std::size_t kParkLayoutNumSetsOffset = 0x13C8C;
constexpr std::size_t kParkLayoutMapOffset = 0x13C90;
constexpr std::size_t kParkLayoutFlagBitsOffset = 0x17728;
constexpr std::size_t kParkLayoutBlockedAreasOffset = 0x1776C;
constexpr std::size_t kParkLayoutNumBlockedAreasOffset = 0x1779C;
constexpr std::size_t kParkLayoutRestartPiecesOffset = 0x177A4;
constexpr std::size_t kParkLayoutGapRegistryOffset = 0x177C4;
constexpr std::size_t kParkLayoutByteSize = 0x177CC;
class ParkLayout {
public:
    // flag_bits_: the compressed map's state (meanings partly guessed from where they're set)
    static constexpr uint32_t kMapValid = 0x1;         // built from the park (kMapUpToDate)
    static constexpr uint32_t kMapInSync = 0x2;        // a park is in it, its header readable (kMapLoaded)
    static constexpr uint32_t kMapNewerThanPark = 0x4; // came from outside: memory card, network (kMapSaved)
    static constexpr uint32_t kMapNotSavedLocal = 0x8;
    static constexpr uint32_t kMapUpToDate = kMapValid;
    static constexpr uint32_t kMapLoaded = kMapInSync;
    static constexpr uint32_t kMapSaved = kMapNewerThanPark;
    static constexpr uint32_t kFlag8 = kMapNotSavedLocal;
    static constexpr int kMaxSets = 40;
    static constexpr int kNumRestartPieces = 8;

    // The one instance.
    static ParkLayout*& sole();
    // 0x00595200 (the getter the game calls)
    static ParkLayout* singleton();

    // --- park_accessors.cpp (0x00595030-0x00595D40) ---
    // 0x00595030: the compressed map; `flag_clean` clears kMapNotSavedLocal.
    ParkMap* get_packed_park_data(bool flag_clean);
    // 0x00595050: take a whole compressed map (loaded or received).
    void set_packed_park_data(const ParkMap* map, bool flag_dirty);
    // NOT ORIGINAL: the same from a received map's bytes (a whole map's worth).
    void set_packed_park_bytes(const uint8_t* bytes, bool flag_dirty);
    // 0x00595090: the compressed map is valid and saved.
    [[nodiscard]] bool has_local_park_copy() const;
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
    PieceFamily* get_piece_family(int index, int* used_before);
    // Where a park map's figures go: its gaps, meta pieces, theme, a word, width and length.
    struct MapInfoOut {
        int* num_gaps_{};
        int* num_metas_{};
        uint16_t* theme{};
        uint32_t* unknown18{};
        int* width{};
        int* length{};
    };

    // 0x00595250: header fields of a compressed map.
    static void get_map_info(const ParkMap* map, MapInfoOut info);
    // 0x005952B0: undo the marked floor changes (clamping every height to -16..15)
    void restore_floor_heights();
    // 0x00595300: which restart piece (1 to 8) has this name; 0 if none.
    int get_restart_index(uint32_t name);
    // 0x00595620: the palette metapiece with this name; `index` gets its position counted from the end.
    PieceGroup* fetch_meta_template(uint32_t name, int* index);
    // 0x00595990: is there memory for the park grown or shrunk to `area` (w x l)?
    [[nodiscard]] bool can_resize_to(CellBox area) const;
    // 0x005959D0: the highest floor in `area`; `is_flat` (if given) says whether it's all one height
    int get_ground_level(CellBox area, bool* uniform);
    // 0x00595A90: the world position of `area`'s corner (w = 1), and its size (if given)
    static maths::Vec4 cell_to_world_pos(const CellBox& area, maths::Vec4* dimensions);
    static maths::Vec4* grid_dims_to_world(maths::Vec4* out, CellBox* area, maths::Vec4* size)
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
        for (MapListNode* node = abstract_metas_; node; node = node->following()) {
            PieceGroup* meta = node->get_placed_piece();
            if (meta->name_key() == checksum) {
                return meta;
            }
        }
        return nullptr;
    }

    // --- park_lists.cpp: the map of metapieces ---
    // 0x00595670: a new concrete metapiece (on the park heap) made from the palette's `source`, its pieces made by
    // the generator with `flags`; added to the list of concrete metapieces
    PieceGroup* make_placed_piece(PieceGroup* abstract_meta, uint32_t flags);
    // 0x005958B0: is a placed piece (not the inner shell) not wholly inside `area` (x and z)?
    bool has_pieces_outside(CellBox area);
    // 0x00595DF0: add `meta` to the front of `list` unless it's in it already
    static void add_meta_to_list(PieceGroup* meta, MapListNode** list);
    // 0x00595E80: unlink `meta` from `list`
    static void remove_meta_from_list(PieceGroup* meta, MapListNode** list);
    // 0x00595EE0: list `meta` in the cell lists under it
    void add_meta_to_cells(PieceGroup* meta);
    // 0x00595FC0: take `meta` out of the cell lists under it
    void remove_meta_from_cells(PieceGroup* meta);
    // 0x00597740: the metapieces intersecting `area` (or, given `shape` turned to `rot` for the moment, those its
    // pieces would touch with its corner at `area`); `skip_inner_shell` (its low byte; the whole value is passed on to
    // PieceGroup::intersects) leaves out risers and counts only real
    // pieces' cells. A new list the caller frees.
    MapListNode* get_intersecting_metas(CellBox area, int rot, PieceGroup* meta, uint32_t skip_inner_shell);
    // 0x00597A00: every metapiece with a piece in a cell of `area` (a new list the caller frees)
    MapListNode* collect_pieces_in_region(CellBox area);
    // 0x00597AC0: the gap at the metapiece in cell `pos` (made 1x1x1), and which of its ends (`half`)
    GapDetails* get_gap_info(CellBox* cell, int* half);
    // 0x005983D0: whether a metapiece over `area` (at any height) has kFloorLocked
    bool has_floor_locked_meta(CellBox area);
    // The risers' bottom row and their height.
    struct RiserSpan {
        int y{};
        int height{};
    };

    // 0x005984A0: change the floor cells of `area` (from height `y`: up or down a step, or to `target`; `mode` 1:
    // to the target, 2: up / exactly, 4: every height counts), marking them; the metapieces on it go on `list` and
    // their own areas change with them. False if a cell would leave -16..15.
    bool build_riser_list(CellBox area, RiserSpan riser_span, uint32_t flags, MapListNode** list);
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
    void export_to_struct(script::Struct* out);
    // 0x00597720
    void set_shell_size(int theme);
    // 0x005987A0: an empty park in the compressed map
    void clear_compressed_map();
    // 0x00597BD0: a gap made from `gap` (the gap manager makes its triggers)
    bool register_gap_entry(GapDetails* gap);
    // 0x00597FC0: delete a gap
    void drop_gap_entry(GapDetails* gap);
    // 0x00591450: debug output of a list (CellChainScratch::dump_to_log)
    static void print_map_list(const MapListNode* list);

    // --- park_manager.cpp, park_map.cpp, park_floor.cpp (0x00598E00-0x0059C900) ---
    // 0x00598E00: the constructor (an empty park; the generator made).
    ParkLayout();
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
    bool regenerate_ground(const CellBox* area, int unused); // (the callers pass 0 as `unused`)
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

    // Short names (src/game/park).
    [[nodiscard]] bool floor_built() const { return field_05_; }
    void set_floor_built(bool value) { field_05_ = value; }
    void set_num_sets(int value) { num_sets_ = value; }

    // The fields other code reads and sets.
    [[nodiscard]] ParkAssembler* builder() const { return builder_; }
    [[nodiscard]] MapListNode* concrete_metas() const { return concrete_metas_; }
    CellBox& build_area() { return build_area_; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] const CellBox& build_area() const { return build_area_; }
    [[nodiscard]] const CellBox& shell_bounds() const { return shell_bounds_; }
    std::array<PieceFamily, kMaxSets>& sets() { return sets_; }
    [[nodiscard]] const std::array<PieceFamily, kMaxSets>& sets() const { return sets_; }
    [[nodiscard]] int num_sets() const { return num_sets_; }
    [[nodiscard]] const std::array<uint32_t, kNumRestartPieces>& restart_pieces() const { return restart_pieces_; }

private:
    ParkAssembler* builder_; // +0x00
    bool park_built_{false}; // +0x04: the park is built (its changes go into the map when asked)
    bool field_05_{false};   // +0x05: the floor is built
    std::array<uint8_t, 2> pad06{};
    MapListNode* riser_list_;     // +0x08
    int riser_metas_{};           // +0x0C
    int riser_pieces_{};          // +0x10
    std::array<std::array<uint32_t, 3>, kShellBlockSets> shell_blocks_{}; // +0x14: "floor_wall_block%d", "wall_block%d", "floor_block%d" (1..8)
    int field_74_;
    void* field_78_;                                  // +0x78: a Malloc block
    MapListNode* concrete_metas_{nullptr};            // +0x7C
    MapListNode* abstract_metas_{nullptr};            // +0x80
    int num_concrete_metas_{0};                       // +0x84
    int num_abstract_metas_{0};                       // +0x88
    std::array<std::array<MapListNode*, kFloorSize>, kFloorSize> cell_metas_{}; // +0x8C: [x][z] the metapieces with pieces in each cell
    int shell_size_;                          // +0x351C: picks the shell model (sk5ed%d_shell) and the bounds' update
    CellBox build_area_{};                    // +0x3520: the park's area (inside the shell)
    CellBox shell_bounds_{};                  // +0x3526: everything that may be built on
    std::array<std::array<FloorCell, kFloorSize>, kFloorSize> floor_{}; // +0x352C: [x][z]
    std::array<PieceFamily, kMaxSets> sets_{};              // +0x1076C
    int num_sets_{};                            // +0x13C8C
    ParkMap map_{};                             // +0x13C90
    uint32_t flag_bits_{0};                     // +0x17728
    std::array<char, kParkNameSize> park_name_{};                  // +0x1772C
    std::array<CellBox, kBlockedAreaCapacity> blocked_areas_{};              // +0x1776C: (the first four: the strips round the park; then Ed_Roadmask)
    uint32_t num_blocked_areas_{};            // +0x1779C
    uint32_t unknown177a0_{};
    std::array<uint32_t, kNumRestartPieces> restart_pieces_{}; // +0x177A4: restart_1, restart_multi, restart_horse, king_of_hill,
                                                   // and the red, green, blue, yellow flags
    GapRegistry* gap_registry_{nullptr};     // +0x177C4
    GapRegistry* gap_manager_copy_{nullptr}; // +0x177C8: see create_gap_manager_copy

    void read_compressed_map(script::Array* names, bool current);
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ParkLayout, shell_blocks_) == kParkLayoutShellBlocksOffset && offsetof(ParkLayout, concrete_metas_) == kParkLayoutConcreteMetasOffset &&
                          offsetof(ParkLayout, cell_metas_) == kParkLayoutCellMetasOffset && offsetof(ParkLayout, shell_size_) == kParkLayoutShellSizeOffset &&
                          offsetof(ParkLayout, floor_) == kParkLayoutFloorOffset && offsetof(ParkLayout, sets_) == kParkLayoutSetsOffset &&
                          offsetof(ParkLayout, num_sets_) == kParkLayoutNumSetsOffset && offsetof(ParkLayout, map_) == kParkLayoutMapOffset &&
                          offsetof(ParkLayout, flag_bits_) == kParkLayoutFlagBitsOffset && offsetof(ParkLayout, blocked_areas_) == kParkLayoutBlockedAreasOffset &&
                          offsetof(ParkLayout, num_blocked_areas_) == kParkLayoutNumBlockedAreasOffset &&
                          offsetof(ParkLayout, restart_pieces_) == kParkLayoutRestartPiecesOffset && offsetof(ParkLayout, gap_registry_) == kParkLayoutGapRegistryOffset &&
                          sizeof(ParkLayout) == kParkLayoutByteSize,
                      "layout must match the original");
    }
};
inline std::span<PieceFamily> get_sets(ParkLayout* manager) { return manager->sets(); }

// The cursor (200 bytes): the box the player moves over the grid, with the piece to place (or the copy to paste).

constexpr std::size_t kBuildPointerAimPointOffset = 0x14;
constexpr std::size_t kBuildPointerPieceGroupOffset = 0x44;
constexpr std::size_t kBuildPointerAreaStartOffset = 0x49;
constexpr std::size_t kBuildPointerAreaHighlightedOffset = 0x55;
constexpr std::size_t kBuildPointerCopyBuffersOffset = 0x58;
constexpr std::size_t kBuildPointerModeSelOffset = 0x6C;
constexpr std::size_t kBuildPointerGapOffset = 0x70;
constexpr std::size_t kBuildPointerByteSize = 200;
class BuildPointer {
public:
    enum class Mode : uint8_t {
        kNone = 0,
        kRegular = 1,
        kGap = 2, // "gap_regular": the gap tool
        kGapAdjust = 3, // "gap_adjust": over a gap's piece
        kAreaSelect = 4, // selecting an area to copy
        kPaste = 5, // pasting the clipboard
        kRailPlacement = 6, // "rail_placement"
    };

    // The one instance.
    static BuildPointer*& sole();
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
    char* get_gap_title() { return gap_.text().data(); }
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
    void get_marquee_bounds(CellBox* area) const;
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
    // 0x0059C7A0: the constructor (on the first piece of set 0).
    BuildPointer();
    // 0x0059C860: make the instance (on the park editor's heap) if there isn't one
    static void spawn_occurrence();
    // 0x00598A60: highlight (or not) the metapieces under the cursor (or under the paste)
    void glow_overlapping_pieces(int on);
    // 0x00598B40: whether the cursor is over a gap's piece (the lookup shrinks cell_footprint_ to 1x1x1)
    bool is_over_gap();
    // 0x00598B60: move by (dx, dz) cells (kept in the park), then follow the floor and the gap state
    void shift_by(int dx, int dz);
    // A cursor step: the cell offset (x, z) and the quarter turns.
    struct CursorStep {
        float offset_cell_x_{};
        float offset_cell_z_{};
        int turn_step_{};
    };

    // 0x0059A1F0: one frame: turn by `turn_step` quarter turns, step on the stick (offset_cell_x, offset_cell_z), slide towards the
    // target and show the piece there
    void tick_frame(CursorStep cursor_step);
    // 0x0059A5A0: drop the area selection (and its highlight)
    void drop_area_marquee();
    // 0x0059A610: whether the selected area can't be copied (bigger than 10 x 10, or the editor says no)
    [[nodiscard]] bool is_area_selection_invalid() const;
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
    // A gap's change: the turn and the left and right steps.
    struct GapChange {
        int turn{};
        int left{};
        int right{};
    };

    // 0x0059D6B0: turn the gap under the cursor (its extents reset) and/or widen its left / right extents (0-6,
    // kept in the park).
    bool change_gap(GapChange gap_change);
    // 0x0059D8E0 / 0x0059D980: destroy the pieces in / flatten the selected area.
    bool destroy_selection();
    void flatten_selection();
    // 0x0059D9F0: raise / lower `area` (at most 10 x 10) to `height` + dir; undone if out of memory.
    bool raise_area(CellBox area, int height, int dir, bool level);
    // 0x0059DAA0: raise / lower the cursor's area (the selection, the clipboard's), widened by the pieces on it;
    // not above 17 or below -16.
    bool raise_lower(int dir);
    // 0x0059C900: delete the cursor (its piece first: no longer the cursor's, destroyed).
    static void teardown();
    // 0x0059C950: the next (dir 1) / previous (-1) piece of the current set, wrapping; a clipboard set selects a
    // clipboard instead.
    void step_piece_choice(int dir);
    // 0x0059C9B0: the next / previous visible set (if any); a clipboard set selects its clipboard and drops the
    // cursor's piece (mode 5).
    void step_piece_family(int dir);

    // Short names (src/game/park).
    ParkLayout*& park_owner() { return park_owner_; }
    CellBox& cell_footprint() { return cell_footprint_; } // the cells under the cursor
    // An area is being selected (the selection stage read as a flag: non-zero), and its first corner set.
    [[nodiscard]] bool selecting() const { return box_select_stage_ != 0; }
    void start_selecting() { box_select_stage_ = 1; }
    GapDetails& current_gap() { return gap_; } // the gap being made
    uint32_t& gap_counter() { return gap_counter_; }
    PieceGroup*& piece_group() { return piece_group_; } // the piece the cursor holds (flag 0x200 while it does)
    void set_copy_buffers(Clipboard* value) { copy_buffers_ = value; }
    [[nodiscard]] Clipboard* active_copy_buffer() const { return clipboard_; }
    void set_active_copy_buffer(Clipboard* value) { clipboard_ = value; }
    int& paste_set_index() { return paste_set_index_; }
    int& visible_set_slot() { return menu_index_; } // the set's place among the visible ones
    [[nodiscard]] Mode mode() const { return mode_sel_; }           // 4: area selection, 5: clipboard
    void set_mode(Mode value) { mode_sel_ = value; }

    // The fields other code reads and sets.
    [[nodiscard]] const CellBox& cell_footprint() const { return cell_footprint_; }
    maths::Vec4& at_point() { return at_point_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    [[nodiscard]] int quarter_turns() const { return quarter_turns_; }
    [[nodiscard]] Clipboard* copy_buffers() const { return copy_buffers_; }
    [[nodiscard]] int clipboard_height() const { return clipboard_height_; }
    [[nodiscard]] int paste_set_index() const { return paste_set_index_; }
    [[nodiscard]] int menu_index() const { return menu_index_; }
    [[nodiscard]] Mode mode_sel() const { return mode_sel_; }
    [[nodiscard]] const GapDetails& current_gap() const { return gap_; }

private:
    ParkLayout* park_owner_; // +0x00
    CellBox cell_footprint_{}; // +0x04
    std::array<uint8_t, 2> pad0a_{};
    float move_time_{1.0F}; // +0x0C: of the slide to the target (1 when a step starts)
    int unknown10{};
    maths::Vec4 aim_point_{}; // +0x14
    maths::Vec4 at_point_{};  // +0x24
    std::array<uint8_t, 4> unknown34{};
    int quarter_turns_{0};             // +0x38: quarter turns
    float target_angle_{0.0F};         // +0x3C: degrees
    float angle_{0.0F};                // +0x40
    PieceGroup* piece_group_{nullptr}; // +0x44: the piece shown at the cursor
    uint8_t box_select_stage_{0};      // +0x48: the area selection (0 none, 1 first corner set, 2 done)
    CellBox area_start_{};     // +0x49: its first corner
    CellBox area_last_{};      // +0x4F: the cursor's cell when the highlight was made (the other corner)
    bool area_highlighted_{false}; // +0x55
    std::array<uint8_t, 2> pad56{};
    Clipboard* copy_buffers_{nullptr}; // +0x58: the newest first
    Clipboard* clipboard_{nullptr};    // +0x5C: the one being pasted
    int clipboard_height_{};  // +0x60
    int paste_set_index_{0};  // +0x64: the clipboard set's index
    int menu_index_{0};       // +0x68
    Mode mode_sel_{Mode::kNone}; // +0x6C (the original's int: the rest is zero)
    std::array<uint8_t, 3> unknown6d_{};
    GapDetails gap_;          // +0x70
    uint32_t gap_counter_{1}; // +0xC4: names the next gap
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(BuildPointer, aim_point_) == kBuildPointerAimPointOffset && offsetof(BuildPointer, piece_group_) == kBuildPointerPieceGroupOffset &&
                          offsetof(BuildPointer, area_start_) == kBuildPointerAreaStartOffset && offsetof(BuildPointer, area_highlighted_) == kBuildPointerAreaHighlightedOffset &&
                          offsetof(BuildPointer, copy_buffers_) == kBuildPointerCopyBuffersOffset && offsetof(BuildPointer, mode_sel_) == kBuildPointerModeSelOffset &&
                          offsetof(BuildPointer, gap_) == kBuildPointerGapOffset && sizeof(BuildPointer) == kBuildPointerByteSize,
                      "layout must match the original");
    }
};

// Park editor script commands (park_cfuncs.cpp).
// 0x0058E120 GetParkEditorMaxPlayers: <MaxPlayers> = the park's player count.
bool cfunc_get_park_editor_max_players(script::Struct* params, script::Script* script);
// 0x0058E140 GetParkEditorMaxPlayersPossible: <MaxPlayersPossible> = how many players the park has room for.
bool cfunc_get_park_editor_max_players_possible(script::Struct* params, script::Script* script);

inline constexpr float kParkCellSizeX{120.0F};
inline constexpr float kParkCellSizeY{48.0F};
inline constexpr float kParkCellSizeZ{120.0F};
memory::PoolPair& clipboard_entry_pools();
memory::PoolPair& node_pools();

} // namespace park_editing
