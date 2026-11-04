// The park editor's cursor (park_editing::BuildPointer): moving over the grid, turning, the piece or paste it shows, the area
// selection and the clipboards.

#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"

#include <cstdint>
#include <new>
#include <gsl/assert>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park/park_memory.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/checked_index.h"
#include "engine/maths/vec4.h"

namespace park_editing {

namespace {

constexpr double kHalf = 0.5;

// The cursor's tuning (the exe's constants).
constexpr float kQuarterTurn = 90.0F;        // 0x00645EB0
constexpr float kFullTurn = 360.0F;          // 0x00645EA8
constexpr float kDeadZone = 9.99999905e-09F; // 0x0064F7B0: the least squared stick deflection that moves
constexpr float kAxisDominance = 5.8284001F; // 0x0064F7AC: tan(67.5 degrees) squared
constexpr float kSlideStep = 0.15F;          // 0x0064F63C: of move_time_ per frame
constexpr float kTurnStep = 15.0F;           // 0x0064F640: degrees per frame
constexpr float kShowInset = 1.5F;           // 0x00646744
constexpr int kClipboardLimit = 6;

// The pooled clipboards (clipboard_pools, park_memory.h - 0x007CE5B4): new from the current pool or the heap;
// 0x005955C0 frees one.
void free_clipboard(Clipboard* clipboard) { Clipboard::dealloc(clipboard); }

// The park editor (the singleton 0x00590160 makes, 0x0058D290 releases): whether an area of w x l cells fits
// the clipboard (0x0058D6D0).
bool editor_allows_copy(int w, int length)
{
    ParkBuilderMode::fabricate(false);
    const bool allowed = park_editing::ParkBuilderMode::main_heap_has_room(w, length);
    ParkBuilderMode::get_release();
    return allowed;
}

// The rail editor (0x004F4970 looks it up): how many rail points are in a box of the world, and how many more
// there's room for.
RailAuthoring* get_rail_editor() { return RailAuthoring::sole(); }
int count_rail_points(float x0, float z0, float x1, float z1)
{
    return get_rail_editor()->count_points_inside(x0, z0, x1, z1);
}
int free_rail_points()
{
    get_rail_editor(); // (the original looks the editor up to pass it; free_points doesn't read it)
    return RailAuthoring::free_points();
}

// The heap the park editor allocates from (0x0059E180).
memory::MemSource* editor_arena() { return get_main_heap(); }

// Swap the box's w and l (a quarter turn).
void swap_width_and_length(CellBox& area)
{
    const uint8_t w = area.w;
    area.w = area.l;
    area.l = w;
}

} // namespace

// 0x00598A60
void BuildPointer::glow_overlapping_pieces(int on)
{
    if (mode_sel_ == BuildPointer::Mode::kPaste) {
        if (clipboard_) {
            clipboard_->glow_overlapping_pieces(this, on);
        }
        return;
    }
    const MapListHolder metas(park_owner_->get_intersecting_metas(cell_footprint_, 0, nullptr, 0));
    if (metas.list()) {
        for (MapListNode* node = metas.list(); node; node = node->following()) {
            node->get_placed_piece()->spotlight(on, 0);
        }
    }
}

// 0x00598B40
bool BuildPointer::is_over_gap()
{
    int half = 0;
    return park_owner_->get_gap_info(&cell_footprint_, &half) != nullptr;
}

// 0x00598B60
void BuildPointer::shift_by(int dx, int dz)
{
    clamp_to_park();
    const CellBox old = cell_footprint_;
    cell_footprint_.x = static_cast<uint8_t>(cell_footprint_.x + dx);
    cell_footprint_.z = static_cast<uint8_t>(cell_footprint_.z + dz);
    clamp_to_park();

    // Stand on the floor: the paste at its own height, otherwise the higher of the new and the old cells.
    if (mode_sel_ == BuildPointer::Mode::kPaste) {
        if (clipboard_) {
            clipboard_height_ = clipboard_->fetch_tallness(this);
            cell_footprint_.y = static_cast<int8_t>(clipboard_height_);
        } else {
            cell_footprint_.y = 0;
        }
    } else {
        const int here = park_owner_->get_ground_level(cell_footprint_, nullptr);
        const int before = park_owner_->get_ground_level(old, nullptr);
        cell_footprint_.y = static_cast<int8_t>(here > before ? here : before);
    }

    if (mode_sel_ != BuildPointer::Mode::kGap && mode_sel_ != BuildPointer::Mode::kGapAdjust) {
        return;
    }
    // The gap tool: adjusting when over a piece of a gap (taking that gap over unless one end is placed).
    BuildPointer::Mode mode = BuildPointer::Mode::kGap;
    const MapListHolder metas(park_owner_->get_intersecting_metas(cell_footprint_, 0, nullptr, 0));
    if (metas.list()) {
        CellBox pos = metas.list()->get_placed_piece()->cell_footprint();
        int half = 0;
        if (const GapDetails* gap = park_owner_->get_gap_info(&pos, &half)) {
            if (gap_.halves_placed() != 1) {
                gap_ = *gap;
            }
            mode = BuildPointer::Mode::kGapAdjust;
        }
    }
    if (mode_sel_ != mode) {
        mode_sel_ = mode;
        on_mode_changed();
    }
    if (gap_.halves_placed() > 0) {
        park_owner_->highlight_gap_pieces(true, &gap_);
    } else {
        const ParkLayout* manager = park_owner_;
        for (MapListNode* node = manager->concrete_metas(); node; node = node->following()) {
            if ((node->get_placed_piece()->flag_bits() & PieceGroup::kHighlightable) != 0U) {
                node->get_placed_piece()->spotlight(1, 1);
            }
        }
        manager->builder()->set_pieces_highlighted(0);
    }
}

// 0x0059A1F0
void BuildPointer::tick_frame(CursorStep cursor_step)
{
    const auto offset_cell_x = cursor_step.offset_cell_x_;
    const auto offset_cell_z = cursor_step.offset_cell_z_;
    const auto turn_step = cursor_step.turn_step_;
    // Turning: the angle aimed at moves a quarter turn (kept within 0..360, the shown angle following it round),
    // and the box turns with it.
    // (the sums are kept at double precision past the stores, as the x87 code does)
    if (turn_step > 0) {
        const double angle = static_cast<double>(target_angle_) + kQuarterTurn;
        quarter_turns_ = (quarter_turns_ + 1) & 3;
        target_angle_ = static_cast<float>(angle);
        if (angle > kFullTurn) {
            target_angle_ = static_cast<float>(angle - kFullTurn);
            angle_ = static_cast<float>(static_cast<double>(angle_) - kFullTurn);
        }
        swap_width_and_length(cell_footprint_);
    } else if (turn_step < 0) {
        const double angle = static_cast<double>(target_angle_) - kQuarterTurn;
        quarter_turns_ = (quarter_turns_ - 1) & 3;
        target_angle_ = static_cast<float>(angle);
        if (angle < 0.0) {
            target_angle_ = static_cast<float>(angle + kFullTurn);
            angle_ = static_cast<float>(static_cast<double>(angle_) + kFullTurn);
        }
        swap_width_and_length(cell_footprint_);
    }

    // Stepping: one cell along the stick's main axis (both when it's diagonal), once the last step has slid in.
    const float xx = offset_cell_x * offset_cell_x;
    const float zz = offset_cell_z * offset_cell_z;
    if (!(xx > kDeadZone || zz > kDeadZone) || !(move_time_ <= 0.0F)) {
        shift_by(0, 0);
    } else if (static_cast<double>(xx) * kAxisDominance < zz) {
        shift_by(0, offset_cell_z < 0.0F ? -1 : 1);
        move_time_ = 1.0F;
    } else if (static_cast<double>(zz) * kAxisDominance < xx) {
        shift_by(offset_cell_x < 0.0F ? -1 : 1, 0);
        move_time_ = 1.0F;
    } else {
        shift_by(0, offset_cell_z < 0.0F ? -1 : 1);
        shift_by(offset_cell_x < 0.0F ? -1 : 1, 0);
        move_time_ = 1.0F;
    }

    // The target: the middle of the box (on the floor), and the slide towards it.
    maths::Vec4 corner;
    maths::Vec4 size;
    const maths::Vec4* world = park_editing::ParkLayout::grid_dims_to_world(&corner, &cell_footprint_, &size);
    aim_point_ = *world;
    aim_point_.x = static_cast<float>((static_cast<double>(size.x) * kHalf) + aim_point_.x);
    aim_point_.z = static_cast<float>((static_cast<double>(size.z) * kHalf) + aim_point_.z);
    if (!(move_time_ > 0.0F)) {
        at_point_ = aim_point_;
        move_time_ = 0.0F;
    } else {
        at_point_.x = static_cast<float>(((static_cast<double>(aim_point_.x) - at_point_.x) / move_time_ * kSlideStep) +
                                         at_point_.x);
        at_point_.z = static_cast<float>(((static_cast<double>(aim_point_.z) - at_point_.z) / move_time_ * kSlideStep) +
                                         at_point_.z);
        at_point_.y = aim_point_.y;
        const double time = static_cast<double>(move_time_) - kSlideStep;
        move_time_ = static_cast<float>(time);
        if (time < 0.0) {
            move_time_ = 0.0F;
        }
    }

    // The shown angle turns towards the target angle.
    if (angle_ < target_angle_) {
        const double angle = static_cast<double>(angle_) + kTurnStep;
        angle_ = static_cast<float>(angle);
        if (angle > target_angle_) {
            angle_ = target_angle_;
        }
    } else if (angle_ > target_angle_) {
        const double angle = static_cast<double>(angle_) - kTurnStep;
        angle_ = static_cast<float>(angle);
        if (angle < target_angle_) {
            angle_ = target_angle_;
        }
    }

    // Show the paste, or the piece (the rail tool shows none), a little in from the corner.
    const maths::Vec4 shown = {.x = static_cast<float>(static_cast<double>(at_point_.x) - kShowInset),
                               .y = static_cast<float>(static_cast<double>(at_point_.y) + kShowInset),
                               .z = static_cast<float>(static_cast<double>(at_point_.z) - kShowInset),
                               .w = static_cast<float>(static_cast<double>(at_point_.w) + 1.0)};
    if (mode_sel_ == BuildPointer::Mode::kPaste) {
        if (clipboard_) {
            clipboard_->preview_pieces(shown, angle_, clipboard_height_);
        }
        return;
    }
    for (Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following()) {
        clipboard->free_all_piece_groups();
    }
    if (mode_sel_ != BuildPointer::Mode::kRailPlacement) {
        piece_group_->change_place(shown, angle_);
    }
}

// 0x0059A5A0
void BuildPointer::drop_area_marquee()
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    get_marquee_bounds(&area);
    park_owner_->highlight_metas_within_area(area, 0);
    box_select_stage_ = 0;
    area_highlighted_ = false;
}

// 0x0059A610
bool BuildPointer::is_area_selection_invalid() const
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    get_marquee_bounds(&area);
    if (area.w > kMaxMarqueeSpan || area.l > kMaxMarqueeSpan) {
        return true;
    }
    return !editor_allows_copy(area.w, area.l);
}

// 0x0059A6D0
bool BuildPointer::copy_area_to_clipboard()
{
    if (is_area_selection_invalid()) {
        return false;
    }
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    get_marquee_bounds(&area);

    // Not if its rails wouldn't fit in what's left of the rail budget (the original asks the global manager).
    maths::Vec4 corner;
    maths::Vec4 size;
    const maths::Vec4* world = park_editing::ParkLayout::grid_dims_to_world(&corner, &area, &size);
    const float x0 = world->x;
    const float z0 = world->z;
    auto x1 = static_cast<float>(static_cast<double>(size.x) + x0);
    auto z1 = static_cast<float>(static_cast<double>(size.z) + z0);
    const int rails = count_rail_points(x0, z0, x1, z1);
    if (rails > free_rail_points()) {
        return false;
    }

    int count = 0;
    for (const Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following()) {
        ++count;
    }
    if (count == kClipboardLimit) {
        remove_oldest_clipboard();
    }

    auto* memory = clipboard_pools().allocate<Clipboard>();
    Clipboard* copy = memory ? new (memory) Clipboard : nullptr;
    Expects(copy != nullptr);
    // Without room for the copy, the oldest clipboards go one by one.
    while (!copy->copy_picked_to_clipboard(area) && copy_buffers_) {
        Clipboard* previous = nullptr;
        Clipboard* last = copy_buffers_;
        while (last->following()) {
            previous = last;
            last = last->following();
        }
        last->teardown();
        free_clipboard(last);
        if (previous) {
            previous->set_following(nullptr);
        } else {
            copy_buffers_ = nullptr;
        }
    }
    if (copy->is_vacant()) {
        if (copy) {
            copy->teardown();
            free_clipboard(copy);
        }
        return false;
    }
    copy->set_following(copy_buffers_);
    copy_buffers_ = copy;
    return true;
}

// 0x0059A8D0
void BuildPointer::update_area_selection_highlight()
{
    if (box_select_stage_ != 1) {
        return;
    }
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    if (!area_highlighted_) {
        get_marquee_bounds(&area);
        park_owner_->highlight_metas_within_area(area, 1);
        area_highlighted_ = true;
        return;
    }
    if (cell_footprint_.x != area_last_.x || cell_footprint_.z != area_last_.z) {
        get_marquee_bounds(&area);
        park_owner_->highlight_metas_within_area(area, 0);
        area_last_ = cell_footprint_;
        get_marquee_bounds(&area);
        park_owner_->highlight_metas_within_area(area, 1);
    }
}

// 0x0059BAC0
void BuildPointer::destroy_cursor_meta()
{
    if (!piece_group_) {
        return;
    }
    piece_group_->flag_bits() &= ~PieceGroup::kCursor;
    park_owner_->free_placed_piece(piece_group_, 0);
    park_owner_->builder()->clean_up_out_refs();
    piece_group_ = nullptr;
}

// 0x0059BAF0
void BuildPointer::switch_to_clipboard()
{
    int count = 0;
    for (const Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following()) {
        ++count;
    }
    if (count == 0) {
        return;
    }

    // The clipboard set (its menu index counts the sets with menus before it). Without one, the original
    // writes through a null set.
    PieceFamily* set = nullptr;
    const int sets = park_owner_->num_sets();
    for (int i = 0; i < sets; ++i) {
        int menu_index = 0;
        for (int j = 0; j < i; ++j) {
            if (core::at(park_owner_->sets(), j).is_displayed_) {
                ++menu_index;
            }
        }
        menu_index_ = menu_index;
        if (core::at(park_owner_->sets(), i).is_clipboard_) {
            paste_set_index_ = i;
            set = &core::at(park_owner_->sets(), i);
            break;
        }
    }
    Expects(set != nullptr);
    set->selected_ = 0;

    for (Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following()) {
        clipboard->free_all_piece_groups();
    }
    clipboard_ = copy_buffers_;
    destroy_cursor_meta();
    if (mode_sel_ != BuildPointer::Mode::kPaste) {
        mode_sel_ = BuildPointer::Mode::kPaste;
        on_mode_changed();
    }
    clamp_to_park();
}

// 0x0059BC00
void BuildPointer::change_piece(uint32_t checksum)
{
    destroy_cursor_meta();
    // (an unknown checksum is read through a null metapiece, as in the original)
    PieceGroup* source = park_owner_->find_piece_template(checksum);
    if ((source->flag_bits() & PieceGroup::kRailPlacement) != 0U) {
        cell_footprint_.w = 1;
        cell_footprint_.h = 1;
        cell_footprint_.l = 1;
    } else {
        piece_group_ = park_owner_->make_placed_piece(source, PieceGroup::kCursor | PieceGroup::kInPark);
        piece_group_->flag_bits() |= PieceGroup::kCursor;
        cell_footprint_.w = piece_group_->cell_footprint().w;
        cell_footprint_.h = piece_group_->cell_footprint().h;
        cell_footprint_.l = piece_group_->cell_footprint().l;
        if ((quarter_turns_ & 1) != 0) {
            swap_width_and_length(cell_footprint_);
        }
    }
    drop_area_marquee();

    const uint32_t flags = source->flag_bits();
    if ((flags & PieceGroup::kGap) != 0U) {
        if (mode_sel_ != BuildPointer::Mode::kGap) {
            mode_sel_ = BuildPointer::Mode::kGap;
            on_mode_changed();
        }
        return;
    }
    BuildPointer::Mode mode = BuildPointer::Mode::kRegular;
    if ((flags & PieceGroup::kMarqueeTool) != 0U) {
        mode = BuildPointer::Mode::kAreaSelect;
    } else if ((flags & PieceGroup::kRailPlacement) != 0U) {
        mode = BuildPointer::Mode::kRailPlacement;
    }
    if (mode_sel_ != mode) {
        mode_sel_ = mode;
        on_mode_changed();
    }
    park_owner_->highlight_gap_pieces(false, nullptr);
}

// 0x0059C7A0
BuildPointer::BuildPointer() : park_owner_(ParkLayout::sole())
{
    cell_footprint_ = CellBox::get_make(0, 0, 0, 0, 0, 0);
    aim_point_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    at_point_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    area_start_ = CellBox::get_make(0, 0, 0, 0, 0, 0);
    area_last_ = CellBox::get_make(0, 0, 0, 0, 0, 0);
    // (gap_ made first by its constructor)
    gap_.set_halves_placed(0);
    gap_.set_cancel_flags(0);
    const PieceFamily* set = park_owner_->get_piece_family(0, &menu_index_);
    change_piece(core::at(set->entries_, set->selected_).checksum);
}

// 0x0059C860
void BuildPointer::spawn_occurrence()
{
    if (sole()) {
        return;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(editor_arena());
    void* memory = memory::alloc(sizeof(BuildPointer));
    sole() = memory ? memory::construct_in<BuildPointer>(memory) : nullptr;
    memory::Hub::get_pull()->leave_heap_scope();
}

} // namespace park_editing
