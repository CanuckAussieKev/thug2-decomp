// The park editor's cursor: clipboards, area selection, the gap being built, keeping it in the park.

#include "engine/core/str.h"

#include <cstdint>
#include <utility>

#include "game/park_editing/park.h"

#include "engine/core/slab_bank.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "game/park/park_memory.h"
#include "engine/core/checksums.h"

namespace park_editing {

BuildPointer*& BuildPointer::sole()
{
    static BuildPointer* cursor{};
    return cursor;
}

namespace {

// The gap manager in use's store_gap_details.
void store_gap_details(int tab, GapDetails* info) { GapRegistry::sole()->store_gap_details(tab, info); }

} // namespace

void Clipboard::dealloc(void* item) { park_editing::clipboard_pools().dealloc(item); }

BuildPointer* BuildPointer::singleton() { return park_editing::BuildPointer::sole(); }

void BuildPointer::hide_clipboards()
{
    for (Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following())
        clipboard->free_all_piece_groups();
}

void BuildPointer::set_gap_label_and_points(const char* name, int score)
{
    if (name) {
        if (*name == '\0') {
            core::at(gap_.text(), 0) = 'x';
            core::at(gap_.text(), 1) = '\0';
        } else {
            text::copy_unbounded(gap_.text().data(), name); // (no length limit, as in the original)
        }
    }
    if (score >= 0)
        gap_.set_points_held(score);
    store_gap_details(gap_.slot_index(), &gap_);
}

void BuildPointer::set_gap_void_mask(script::Struct* flags)
{
    if (!flags)
        return;
    uint32_t cancel = 0;
    for (script::Component* component = flags->step_to_entry(nullptr); component;
         component = flags->step_to_entry(component))
        cancel |= static_cast<uint32_t>(script::fetch_int_value(component->value.word()));
    gap_.set_cancel_flags(cancel);
    store_gap_details(gap_.slot_index(), &gap_);
}

void BuildPointer::show_clipboard()
{
    if (clipboard_)
        clipboard_->show_at(this);
}

void BuildPointer::select_clipboard(int index)
{
    hide_clipboards();
    Clipboard* clipboard = copy_buffers_;
    for (int i = 0; i < index && clipboard; ++i)
        clipboard = clipboard->following();
    clipboard_ = clipboard;
}

void BuildPointer::toggle_shown(bool visible)
{
    if (piece_group_)
        piece_group_->toggle_shown(visible);
    if (!visible && clipboard_)
        clipboard_->free_all_piece_groups();
    area_highlighted_ = false;
}

void BuildPointer::on_mode_changed()
{
    script::Struct params;
    uint32_t mode = 0;
    if (mode_sel_ == kModeGapAdjust)
        mode = 0xE75A3A17; // gap_adjust
    else if (mode_sel_ == kModeGap)
        mode = 0x780A5CF3; // gap_regular
    else if (mode_sel_ == kModeRailPlacement)
        mode = 0xFFD81C08; // rail_placement
    else
        mode = 0xB58EFC2B;                                               // regular
    params.put_name_key(checksums::Name{0x6835B854}, mode);                               // mode
    script::launch_script(0x9DB065AD, &params, nullptr, false, nullptr); // parked_set_helper_text_mode
    params.get_clear();
}

void BuildPointer::get_marquee_bounds(CellBox* area)
{
    if (box_select_stage_ == 0) {
        area->assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
        return;
    }
    uint8_t low_x = area_start_.x, high_x = area_last_.x;
    if (high_x < low_x) {
        low_x = area_last_.x;
        high_x = area_start_.x;
    }
    uint8_t low_z = area_start_.z, high_z = area_last_.z;
    if (high_z < low_z) {
        low_z = area_last_.z;
        high_z = area_start_.z;
    }
    area->x = low_x;
    area->z = low_z;
    area->y = 0;
    area->w = static_cast<uint8_t>(high_x - low_x + 1);
    area->h = 1;
    area->l = static_cast<uint8_t>(high_z - low_z + 1);
}

void BuildPointer::remove_oldest_clipboard()
{
    Clipboard* previous = nullptr;
    Clipboard* last = copy_buffers_;
    if (!last)
        return;
    while (last->following()) {
        previous = last;
        last = last->following();
    }
    last->teardown();
    Clipboard::dealloc(last);
    if (previous)
        previous->set_following(nullptr);
    else
        copy_buffers_ = nullptr;
}

void BuildPointer::clamp_to_park()
{
    const CellBox& bounds = park_owner_->build_area();
    CellBox& pos = cell_footprint_;
    if (pos.x < bounds.x)
        pos.x = bounds.x;
    if (pos.w + pos.x > bounds.w + bounds.x)
        pos.x = static_cast<uint8_t>(bounds.w - pos.w + bounds.x);
    if (pos.z < bounds.z)
        pos.z = bounds.z;
    if (pos.l + pos.z > bounds.l + bounds.z)
        pos.z = static_cast<uint8_t>(bounds.l - pos.l + bounds.z);

    if (mode_sel_ != kModePaste || !clipboard_)
        return;
    // the pasted area (whose corner may be off the grid: signed) is pushed back inside too
    CellBox area{};
    area.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
    clipboard_->fetch_extent(this, &area);
    int last = bounds.w + bounds.x - 1;
    int shift = 0;
    if (std::cmp_less(static_cast<int8_t>(area.x), bounds.x))
        shift = bounds.x - static_cast<int8_t>(area.x);
    int end = int{static_cast<int8_t>(area.w + area.x - 1)};
    if (end > last)
        shift = last - end;
    pos.x = static_cast<uint8_t>(pos.x + shift);
    last = bounds.l + bounds.z - 1;
    shift = 0;
    if (std::cmp_less(static_cast<int8_t>(area.z), bounds.z))
        shift = bounds.z - static_cast<int8_t>(area.z);
    end = int{static_cast<int8_t>(area.l + area.z - 1)};
    if (end > last)
        shift = last - end;
    pos.z = static_cast<uint8_t>(pos.z + shift);
}

void BuildPointer::free_copy_buffers()
{
    Clipboard* clipboard = copy_buffers_;
    while (clipboard) {
        Clipboard* next = clipboard->following();
        clipboard->teardown();
        Clipboard::dealloc(clipboard);
        clipboard = next;
    }
    clipboard_ = nullptr;
}

void BuildPointer::forget_gap_at(PieceGroup* meta)
{
    CellBox area = meta->cell_footprint();
    int half = 0;
    const GapDetails* gap = park_owner_->get_gap_info(&area, &half);
    if (!gap)
        return;
    if (gap_.halves_placed() != 1 ||
        (core::at(gap->at_point(), 0).x == core::at(gap_.at_point(), 0).x && core::at(gap->at_point(), 0).y == core::at(gap_.at_point(), 0).y &&
         core::at(gap->at_point(), 0).z == core::at(gap_.at_point(), 0).z))
        gap_.set_halves_placed(0);
}

int BuildPointer::advance_area_selection()
{
    if (box_select_stage_ == 0) {
        area_start_ = cell_footprint_;
        area_last_ = cell_footprint_;
        box_select_stage_ = 1;
        area_highlighted_ = false;
        return box_select_stage_;
    }
    if (box_select_stage_ == 1) {
        CellBox area{};
        area.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
        get_marquee_bounds(&area);
        if (area.w <= 10 && area.l <= 10)
            box_select_stage_ = 2;
    }
    return box_select_stage_;
}

} // namespace park_editing

