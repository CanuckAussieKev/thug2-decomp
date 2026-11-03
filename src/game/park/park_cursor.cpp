// The park editor's cursor: placing pieces and restart points, gaps, area selection, raising and lowering the
// floor; and the park manager's set-up (0x0059D120-0x0059E0F7, see park_editor.h).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/stubs.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park/park_memory.h"
#include "engine/script/param_struct.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "formats/park_map_format.h"

namespace park_editing {

namespace {

constexpr uint32_t kTopDownLevel = 0xCD3682C1; // the level whose park lives in the top-down heap
constexpr uint32_t kParkEditorLevel = 0x7ECA21E5;
constexpr uint32_t kHoldsClipboards = 0x45C6C158; // a piece set's flag: its entries are the clipboards
constexpr int kMaxBuildHeight = 17;         // the most a piece's top may stand above the ground, in cells
constexpr int kNewGapPoints = 100;          // the points a gap starts with
constexpr int kMaxReach = 6;                // how far a gap's halves can reach
constexpr int kClipboardCount = 6;
constexpr uint32_t kClipboardTitle = 0x4E677220u; // ClipboardTitle
constexpr size_t kClipboardNameSize = 100;
constexpr uint32_t kMetaHasGap = 0x1000;          // a meta piece's flag

uint32_t& get_meta_flags(PieceGroup* meta) { return meta->flag_bits(); }
uint32_t meta_type(const PieceGroup* meta) { return meta->name_key(); }
CellBox& meta_footprint(PieceGroup* meta) { return meta->cell_footprint(); }

// The memory profiler's markers (compiled out of this build: the empty function 0x005480A0).
inline void memory_marker(const char* /*what*/) {}

PieceGroup* node_meta(MapListNode* node)
{
    PieceGroup* meta = node->get_piece_group();
    return meta->get_cast();
}
PieceGroup* as_meta(void* meta) { return static_cast<PieceGroup*>(meta); }
Clipboard* as_clipboard(Clipboard* clipboard) { return clipboard; }
ParkAssembler* as_generator(void* generator) { return static_cast<ParkAssembler*>(generator); }
Clipboard* next_clipboard(Clipboard* clipboard) { return clipboard->following(); }
// A clipboard's area where the cursor would paste it.
void clipboard_area(Clipboard* clipboard, BuildPointer* cursor, CellBox* area)
{
    as_clipboard(clipboard)->fetch_extent(cursor, area);
}
// The gap with a half at `area` (and which half), or null.
GapRecord* gap_at(ParkLayout* manager, CellBox* area, int* half) { return manager->get_gap_info(area, half); }
void drop_gap_entry(ParkLayout* manager, GapRecord* gap) { manager->drop_gap_entry(gap); }
bool register_gap_entry(ParkLayout* manager, GapRecord* gap) { return manager->register_gap_entry(gap); }
// The metas within an area (the flags: 0, 0, 1) - the caller owns the list.
MapListNode* metas_within(ParkLayout* manager, CellBox area)
{
    return manager->get_intersecting_metas(area, 0, nullptr, 1);
}
// The area selected.
void selected_area(BuildPointer* cursor, CellBox* area) { cursor->get_marquee_bounds(area); }
int ground_level_cell(ParkLayout* manager, CellBox area, bool* level)
{
    return manager->get_ground_level(area, level);
}
ParkBuilderMode* get_park_editor() { return ParkBuilderMode::fabricate(false); }
bool park_full(ParkBuilderMode* editor) { return editor->is_park_full() != 0; }
void release_park_editor() { ParkBuilderMode::get_release(); }
// (0x005BA1D0: `xor eax, eax; ret` - compiled out of this build, folded with stubs::return_zero)
void set_default_heap(memory::Arena* /*heap*/) { stubs::return_zero(); }

} // namespace

// 0x0059D120
void BuildPointer::remove_clipboards_too_big()
{
    Clipboard* previous = nullptr;
    Clipboard* clipboard = copy_buffers();
    while (clipboard) {
        Clipboard* next = next_clipboard(clipboard);
        CellBox area = {};
        clipboard_area(clipboard, this, &area);
        const uint8_t park_width = park_owner()->build_area().w;
        const uint8_t park_length = park_owner()->build_area().l;
        if (area.w > park_width || area.w > park_length || area.l > park_width || area.l > park_length) {
            // Too big to paste in this park: unlinked and deleted.
            if (previous)
                previous->set_following(next);
            if (clipboard == copy_buffers())
                set_copy_buffers(next);
            as_clipboard(clipboard)->teardown();
            clipboard_pools().dealloc(clipboard);
        } else {
            previous = clipboard;
        }
        clipboard = next;
    }
    set_active_copy_buffer(copy_buffers());
    step_piece_choice(0);
}

// 0x0059D240
bool BuildPointer::commit_tile_placement()
{
    ParkLayout* manager = park_owner();
    if (!manager->can_change_floor_here(cell_footprint(), false))
        return false;
    if (cell_footprint().h + ground_level_cell(manager, cell_footprint(), nullptr) > kMaxBuildHeight)
        return false;
    // A restart point needs a free slot of its type.
    const uint32_t type = meta_type(piece_group());
    for (int slot = 1; slot < kPlayerEnd; ++slot) {
        if (type == core::at(manager->restart_pieces(), slot - 1)) {
            if (!as_generator(manager->builder())->has_free_restart_slot(slot)) {
                debug::printf(
                    "No free slots exist for this type of restart point\n"); // "No free slots exist for this type of restart point\n"
                return false;
            }
            break;
        }
    }
    set_default_heap(get_main_heap());
    PieceGroup* source = manager->fetch_meta_template(meta_type(piece_group()), nullptr);
    PieceGroup* piece = manager->make_placed_piece(source, 0);
    piece->set_quarter_turns(quarter_turns());
    manager->place_meta_piece_in_park(cell_footprint(), piece);
    set_default_heap(get_main_heap());
    manager->rebuild_area(&cell_footprint(), false);
    memory_marker("After placing a piece"); // "After placing a piece"
    return true;
}

// 0x0059D380
bool BuildPointer::destroy_under_cursor()
{
    CellBox area = {};
    if (mode() == BuildPointer::Mode::kPaste) {
        if (!active_copy_buffer())
            return false;
        clipboard_area(active_copy_buffer(), this, &area);
    } else {
        area = cell_footprint();
    }
    return park_owner()->destroy_metas_in_area_and_rebuild(area, false, false, true);
}

// 0x0059D410
bool BuildPointer::add_gap_half()
{
    ParkBuilderMode* editor = get_park_editor();
    if (park_full(editor)) {
        release_park_editor();
        return false;
    }
    CellChainScratch metas{metas_within(park_owner(), cell_footprint())};
    bool added = false;
    if (metas.list()) {
        PieceGroup* meta = node_meta(metas.list());
        cell_footprint().dump_to_log();
        int half = 0;
        if (!gap_at(park_owner(), &cell_footprint(), &half) && meta) {
            GapRecord& gap = current_gap();
            if (gap.halves_placed() == 1)
                drop_gap_entry(park_owner(), &gap);
            else
                gap.set_halves_placed(0);
            const int n = gap.halves_placed();
            core::at(gap.at_point(), n) = meta_footprint(meta);
            core::at(gap.quarter_turns(), n) = 0;
            core::at(gap.reach_left(), n) = 0;
            core::at(gap.reach_right(), n) = 0;
            gap.set_points_held(kNewGapPoints);
            gap.set_halves_placed(n + 1);
            gap.set_slot_index(-1);
            if (n == 0) {
                // The gap's name: "gap", then "gap2", "gap3"...
                if (gap_counter() > 1)
                    THUG2_MSVC_WARNING(suppress : 4996)
                text::format_to(gap.text(), "gap%d", gap_counter()); // "gap%d"
                else std::memcpy(gap.text().data(), "gap", 4);
                ++gap_counter();
            }
            if (register_gap_entry(park_owner(), &gap)) {
                park_owner()->regenerate_ground(&cell_footprint(), 0);
                if (gap.halves_placed() > 0)
                    park_owner()->highlight_gap_pieces(true, &gap);
                added = true;
            } else {
                gap.set_halves_placed(0);
            }
        }
    }
    metas.teardown();
    release_park_editor();
    return added;
}

// 0x0059D600
bool BuildPointer::remove_gap_half()
{
    ParkLayout* manager = park_owner();
    if (current_gap().halves_placed() == 1) {
        drop_gap_entry(manager, &current_gap());
        current_gap().set_halves_placed(0);
        park_owner()->rebuild_area(&cell_footprint(), false);
        return true;
    }
    int half = 0;
    GapRecord* gap = gap_at(manager, &cell_footprint(), &half);
    if (!gap)
        return false;
    drop_gap_entry(park_owner(), gap);
    park_owner()->rebuild_area(&cell_footprint(), false);
    // The pieces that had gaps are told to look again.
    manager = park_owner();
    for (auto* node = manager->concrete_metas(); node; node = node->following()) {
        if (get_meta_flags(node_meta(node)) & kMetaHasGap)
            as_meta(node_meta(node))->spotlight(1, 1);
    }
    as_generator(manager->builder())->set_pieces_highlighted(0);
    return true;
}

// 0x0059D6B0
bool BuildPointer::change_gap(GapChange gap_change)
{
    const auto turn = gap_change.turn;
    const auto left = gap_change.left;
    const auto right = gap_change.right;
    int half = 0;
    GapRecord* gap = gap_at(park_owner(), &cell_footprint(), &half);
    if (!gap)
        return false;
    // A gap being made: only its own first half.
    if (current_gap().halves_placed() == 1 &&
        (core::at(gap->at_point(), 0).x != core::at(current_gap().at_point(), 0).x || core::at(gap->at_point(), 0).y != core::at(current_gap().at_point(), 0).y ||
         core::at(gap->at_point(), 0).z != core::at(current_gap().at_point(), 0).z))
        return false;
    GapRecord copy = *gap;
    bool changed = false;
    if (turn) {
        core::at(copy.quarter_turns(), half) -= turn;
        core::at(copy.reach_left(), half) = 0;
        core::at(copy.reach_right(), half) = 0;
        changed = true;
    }
    if (left) {
        core::at(copy.reach_left(), half) += left;
        if (core::at(copy.reach_left(), half) < 0)
            core::at(copy.reach_left(), half) = 0;
        else if (core::at(copy.reach_left(), half) > kMaxReach)
            core::at(copy.reach_left(), half) = kMaxReach;
        changed = true;
    }
    if (right) {
        core::at(copy.reach_right(), half) += right;
        core::at(copy.reach_right(), half) = std::max(core::at(copy.reach_right(), half), 0);
        core::at(copy.reach_right(), half) = std::min(core::at(copy.reach_right(), half), kMaxReach);
    } else if (!changed) {
        return true;
    }
    drop_gap_entry(park_owner(), gap);
    core::at(copy.quarter_turns(), half) &= 3;
    const CellBox park = park_owner()->build_area();
    const CellBox& at = core::at(copy.at_point(), half);
    int* m_reach_left = &core::at(copy.reach_left(), half);
    int* m_reach_right = &core::at(copy.reach_right(), half);
    // Each extent is kept inside the park: the one behind the gap's start, the other past its end.
    switch (core::at(copy.quarter_turns(), half)) {
    case 0:
    case 2: {
        int* behind = core::at(copy.quarter_turns(), half) == 0 ? m_reach_right : m_reach_left;
        int* ahead = core::at(copy.quarter_turns(), half) == 0 ? m_reach_left : m_reach_right;
        if (at.z - *behind < park.z)
            --*behind;
        if (at.z + at.l + *ahead > park.z + park.l)
            --*ahead;
        break;
    }
    case 1:
    case 3: {
        int* behind = core::at(copy.quarter_turns(), half) == 1 ? m_reach_right : m_reach_left;
        int* ahead = core::at(copy.quarter_turns(), half) == 1 ? m_reach_left : m_reach_right;
        if (at.x - *behind < park.x)
            --*behind;
        if (at.x + at.w + *ahead > park.x + park.w)
            --*ahead;
        break;
    }
    default:
        break;
    }
    register_gap_entry(park_owner(), &copy);
    park_owner()->rebuild_area(&park, false);
    if (current_gap().halves_placed() == 1)
        current_gap() = copy;
    return true;
}

// 0x0059D8E0
bool BuildPointer::destroy_selection()
{
    if (!selecting())
        return false;
    CellBox area = {};
    selected_area(this, &area);
    const bool destroyed = park_owner()->destroy_metas_in_area_and_rebuild(area, true, true, false);
    memory_marker("After Area deletion"); // "After Area deletion"
    start_selecting();
    area_highlighted_ = false;
    return destroyed;
}

// 0x0059D980
void BuildPointer::flatten_selection()
{
    if (!selecting())
        return;
    CellBox area = {};
    selected_area(this, &area);
    park_owner()->clear_area(area);
    start_selecting();
    area_highlighted_ = false;
}

// 0x0059D9F0
bool BuildPointer::raise_area(CellBox area, int height, int dir, bool level)
{
    if (area.w > kMaxMarqueeSpan || area.l > kMaxMarqueeSpan)
        return false;
    if (!park_owner()->raise_or_lower_floor(area, area.y, height, dir > 0, !level))
        return false;
    shift_by(0, 0);
    if (park_owner()->rebuild_area(&area, true)) {
        park_owner()->highlight_gap_pieces(false, nullptr);
        return true;
    }
    park_owner()->undo_floor_change(); // undone
    park_owner()->rebuild_area(&area, false);
    return false;
}

// 0x0059DAA0
bool BuildPointer::raise_lower(int dir)
{
    CellBox area = cell_footprint();
    if (mode() == BuildPointer::Mode::kAreaSelect) {
        if (selecting())
            selected_area(this, &area);
    } else if (mode() == BuildPointer::Mode::kPaste) {
        if (!active_copy_buffer())
            return false;
        clipboard_area(active_copy_buffer(), this, &area);
    }
    const CellBox park = ParkLayout::sole()->build_area();
    if (area.w > park.w || area.l > park.l)
        return false;
    // (the original has the level flag written over the copy's first byte)
    bool level = false;
    const int height = ground_level_cell(park_owner(), area, &level);
    if (height + dir <= -kFloorLevelDepth)
        return false;
    int top = area.h + height + dir;
    // The pieces standing on the area (any height) widen it, and the highest sets the top.
    const CellBox above = {.x = area.x, .y = area.y, .z = area.z, .w = area.w, .h = 16, .l = area.l};
    MapListNode* list = metas_within(ParkLayout::sole(), above);
    for (const MapListNode* node = list; node; node = node->following()) {
        PieceGroup* meta = node->get_piece_group();
        const CellBox at = meta_footprint(meta);
        if (at.h > area.h)
            top = at.h + height + dir;
        area.x = std::min(area.x, at.x);
        area.w = std::max(area.w, at.w);
        area.z = std::min(area.z, at.z);
        area.l = std::max(area.l, at.l);
    }
    MapListNode::free_node_chain(list);
    if (top > kMaxBuildHeight)
        return false;
    if (mode() == BuildPointer::Mode::kPaste && active_copy_buffer())
        as_clipboard(active_copy_buffer())->free_all_piece_groups();
    const bool raised = raise_area(area, height, dir, level);
    if (mode() == BuildPointer::Mode::kAreaSelect)
        area_highlighted_ = false;
    return raised;
}

// 0x0059DCD0
void ParkLayout::commission(uint32_t level)
{
    if (park_built_)
        return;
    park_built_ = true;
    prepare_all(level); // the park heap and pools
    for (auto& column : floor_) {
        for (auto& cell : column) {
            cell.touched_ = false;
            cell.delta_ = 0;
        }
    }
    // The park's gap manager (0xA8 bytes), in the top-down heap for that level.
    memory::Hub* memory = memory::Hub::get_pull();
    if (level == kTopDownLevel)
        memory->enter_heap_scope(memory->top_down_heap());
    void* block = memory::alloc(kGapRegistryByteSize);
    gap_registry_ = block ? memory::construct_in<GapRegistry>(block, this) : nullptr;
    if (level == kTopDownLevel)
        memory::Hub::get_pull()->leave_heap_scope();
    const int theme = (flag_bits() & 2) ? map_.shell_size() : 0;
    builder_->load_pieces({.level = level, .unused = theme});
    set_up_abstract_metas();
    set_up_road_mask();
    set_default_bounds();
    rebuild_park(0);
    if (level == kParkEditorLevel)
        builder_->build_goal_node_array();

    // The palette's sets (Ed_piece_sets): each a run of the standard meta pieces (Ed_standard_metapieces) from its
    // `first` up to the next set's, or the clipboards.
    script::Array* sets = script::fetch_list("Ed_piece_sets"); // "Ed_piece_sets"
    set_num_sets(static_cast<int>(sets->extent()));
    script::Array* standard = script::fetch_list("Ed_standard_metapieces"); // "Ed_standard_metapieces"
    int end = static_cast<int>(standard->extent());
    for (int i = num_sets() - 1; i >= 0; --i) {
        TileFamilyRef& set = core::at(get_sets(this), i);
        auto* description = sets->struct_at(static_cast<uint32_t>(i));
        set.label_ = nullptr;
        description->fetch_text("name", &set.label_, 1); // "name"
        set.name_checksum_ = checksums::hash_from_text(set.label_);
        set.selected_ = 0;
        set.entry_total_ = 0;
        uint32_t first = 0;
        if (description->read_hash_value("first", &first, 0)) { // "first"
            // `first`'s place in the standard pieces (the manager's list runs from the last to the first).
            int index = 0;
            int position = num_abstract_metas_ - 1;
            for (auto* node = abstract_metas_; node; node = node->following(), --position) {
                if (meta_type(node_meta(node)) == first) {
                    index = position;
                    break;
                }
            }
            set.entry_total_ = end - index;
            end = index;
            for (int entry = 0; entry < set.entry_total_; ++entry) {
                const int piece = index + entry;
                const PieceGroup* meta = nullptr;
                int at = num_abstract_metas_ - 1;
                for (auto* node = abstract_metas_; node; node = node->following(), --at) {
                    const PieceGroup* candidate = node_meta(node);
                    if (at == piece) {
                        meta = candidate;
                        break;
                    }
                }
                core::at(set.entries_, entry).checksum = meta_type(meta); // (no check for a missing piece, as in the original)
                auto* piece_description =
                    standard->struct_at(static_cast<uint32_t>(piece));
                if (!piece_description->fetch_text("text_name", // "text_name"
                                                   &core::at(set.entries_, entry).name, 0))
                    core::at(set.entries_, entry).name = nullptr;
            }
        }
        set.is_clipboard_ = false;
        if (description->has_bare_flag(kHoldsClipboards)) {
            set.is_clipboard_ = true;
            set.entry_total_ = kClipboardCount;
            const char* text = script::fetch_text(kClipboardTitle);
            for (int k = 0; k < kClipboardCount; ++k) {
                std::array<char, kClipboardNameSize> name{};
                text::format_to(name, "Clipboard%d", k); // "Clipboard%d"
                core::at(set.entries_, k).name = text;
                core::at(set.entries_, k).checksum = checksums::hash_from_text(name.data());
            }
        }
        set.is_displayed_ = !description->has_bare_flag("hidden"); // "hidden"
    }
}

} // namespace park_editing
