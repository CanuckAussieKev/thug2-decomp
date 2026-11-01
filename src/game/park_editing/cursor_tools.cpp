// The park editor's cursor: clipboards, area selection, the gap being built, keeping it in the park.

#include <cstdint>

#include "game/park_editing/park.h"

#include "engine/core/slab_bank.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace park_editing {

memory::PoolPair g_clipboard_pools;
BuildPointer* g_cursor;

namespace {

// The gap manager in use's store_gap_details.
void store_gap_details(int tab, GapDetails* info) { GapRegistry::sole()->store_gap_details(tab, info); }

} // namespace

void Clipboard::dealloc(void* item) { g_clipboard_pools.dealloc(item); }

BuildPointer* BuildPointer::singleton() { return g_cursor; }

void BuildPointer::hide_clipboards()
{
    for (Clipboard* clipboard = copy_buffers_; clipboard; clipboard = clipboard->following_)
        clipboard->free_all_piece_groups();
}

void BuildPointer::set_gap_label_and_points(const char* name, int score)
{
    if (name) {
        if (*name == '\0') {
            gap_.text_[0] = 'x';
            gap_.text_[1] = '\0';
        } else {
            char* to = gap_.text_;
            while ((*to++ = *name++) != '\0') { // (no length limit, as in the original)
            }
        }
    }
    if (score >= 0)
        gap_.points_held_ = score;
    store_gap_details(gap_.slot_index_, &gap_);
}

void BuildPointer::set_gap_void_mask(script::Struct* flags)
{
    if (!flags)
        return;
    uint32_t cancel = 0;
    for (script::Component* component = flags->step_to_entry(nullptr); component;
         component = flags->step_to_entry(component))
        cancel |= static_cast<uint32_t>(script::fetch_int_value(component->checksum));
    gap_.cancel_flags_ = cancel;
    store_gap_details(gap_.slot_index_, &gap_);
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
        clipboard = clipboard->following_;
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
    params.put_name_key(0x6835B854, mode);                               // mode
    script::launch_script(0x9DB065AD, &params, nullptr, false, nullptr); // parked_set_helper_text_mode
    params.get_clear();
}

void BuildPointer::get_marquee_bounds(CellBox* area)
{
    if (box_select_stage_ == 0) {
        area->assign_value(0, 0, 0, 0, 0, 0);
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
    while (last->following_) {
        previous = last;
        last = last->following_;
    }
    last->teardown();
    Clipboard::dealloc(last);
    if (previous)
        previous->following_ = nullptr;
    else
        copy_buffers_ = nullptr;
}

void BuildPointer::clamp_to_park()
{
    const CellBox& bounds = park_owner_->build_area_;
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
    CellBox area;
    area.assign_value(0, 0, 0, 0, 0, 0);
    clipboard_->fetch_extent(this, &area);
    int last = bounds.w + bounds.x - 1;
    int shift = 0;
    if (static_cast<int8_t>(area.x) < bounds.x)
        shift = bounds.x - static_cast<int8_t>(area.x);
    int end = static_cast<int8_t>(area.w + area.x - 1);
    if (end > last)
        shift = last - end;
    pos.x = static_cast<uint8_t>(pos.x + shift);
    last = bounds.l + bounds.z - 1;
    shift = 0;
    if (static_cast<int8_t>(area.z) < bounds.z)
        shift = bounds.z - static_cast<int8_t>(area.z);
    end = static_cast<int8_t>(area.l + area.z - 1);
    if (end > last)
        shift = last - end;
    pos.z = static_cast<uint8_t>(pos.z + shift);
}

void BuildPointer::free_copy_buffers()
{
    Clipboard* clipboard = copy_buffers_;
    while (clipboard) {
        Clipboard* next = clipboard->following_;
        clipboard->teardown();
        Clipboard::dealloc(clipboard);
        clipboard = next;
    }
    clipboard_ = nullptr;
}

void BuildPointer::forget_gap_at(PieceGroup* meta)
{
    CellBox area = meta->cell_footprint_;
    int half = 0;
    GapDetails* gap = park_owner_->get_gap_info(&area, &half);
    if (!gap)
        return;
    if (gap_.halves_placed_ != 1 ||
        (gap->at_point_[0].x == gap_.at_point_[0].x && gap->at_point_[0].y == gap_.at_point_[0].y &&
         gap->at_point_[0].z == gap_.at_point_[0].z))
        gap_.halves_placed_ = 0;
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
        CellBox area;
        area.assign_value(0, 0, 0, 0, 0, 0);
        get_marquee_bounds(&area);
        if (area.w <= 10 && area.l <= 10)
            box_select_stage_ = 2;
    }
    return box_select_stage_;
}

} // namespace park_editing

