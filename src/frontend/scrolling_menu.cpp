// The scrolling menus: see scrolling_menu.h.

#include <cstdint>
#include <new>

#include "frontend/scrolling_menu.h"

#include "engine/core/checksums.h"
#include "frontend/menu_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/tracker.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "engine/core/down_cast.h"

namespace frontend {

namespace {

using script::Struct;

constexpr uint32_t kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kInnerAlign = 0x67E093E4, kScrollSpeed = 0x9020A1D1, kNumItemsToShow = 0xC855511C;
constexpr uint32_t kResetWindowTop = 0xED889E1F, kResetWindowBottom = 0xC3A894F6, kRearrange = 0xA1F3B8BB;
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;

// The menus' type names, hashed at run time (the strings 0x0064A1F0 "vmenu" / 0x0064A1E8 "hmenu").
uint32_t v_menu_type() { return checksums::hash_from_text("vmenu"); }
uint32_t h_menu_type() { return checksums::hash_from_text("hmenu"); }

void set_event_consumed(objects::Signal* event, uint32_t how, uint32_t script)
{
    event->set_event_consumed(how, script);
}

UiTree* manager_of(const UiWidget* e) { return core::down_cast<UiTree>(e->park_owner()); }

// With the manager's pad-event priorities, only the top priority element may have a pad event.
bool may_have(UiWidget* e, objects::Signal* event)
{
    UiTree* manager = manager_of(e);
    if (e->manager_uses_priorities() && manager->is_pad_event(event->kind_tag_))
        return e->has_priority() && manager->has_top_precedence(e->ident());
    return true;
}

} // namespace

// 0x004AE050 (0x004AE170 / 0x004AE1C0 the vertical / horizontal one's: then vertical_)
WindowedList::WindowedList(bool vertical)
{
    listener_.enter_wait_board(this);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    menu_ = nullptr;
    chosen_item_id_ = 0;
    focused_ = false;
    rearrange_ = false;
    window_start_ = 0.0f;
    scroll_speed_ = 10000.0f;
    num_items_to_show_ = -1;
    window_length_ = 0.0f;
    regular_space_ = -1.0f;
    change_species(kVScrollingMenu);
    vertical_ = vertical;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004AE2D0
void WindowedList::find_menu()
{
    {
        ElementPtr child;
        child.assign_value(eldest().get_pull());
        while (child) {
            if (child->kind_tag() == v_menu_type() && vertical_)
                // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
                menu_ = child.get_pull();
            else if (child->kind_tag() == h_menu_type() && !vertical_)
                menu_ = child.get_pull();
            child.assign_value(child->following().get_pull());
        }
    }
    float space = -1.0f;
    if (core::down_cast<MenuBase>(menu_)->get_regular_space(&space) && num_items_to_show_ >= 0) {
        regular_space_ = space;
        window_length_ = static_cast<float>(static_cast<double>(num_items_to_show_) * space);
    } else {
        num_items_to_show_ = -1;
        window_length_ = vertical_ ? level_y() : span();
    }
}

// 0x004AE450 (the item's justification is read into the original's argument slots - no effect)
void WindowedList::item_place(ElementPtr* item, float* x, float* y, float* w, float* h)
{
    item->get_pull()->fetch_location(x, y);
    *x = static_cast<float>(static_cast<double>(menu_->shown().scale_x_) * *x);
    *y = static_cast<float>(static_cast<double>(menu_->shown().scale_y_) * *y);
    *w = static_cast<float>(static_cast<double>(menu_->shown().scale_x_) * item->get_pull()->span() *
                            item->get_pull()->shown().scale_x_);
    *h = static_cast<float>(static_cast<double>(menu_->shown().scale_y_) * item->get_pull()->level_y() *
                            item->get_pull()->shown().scale_y_);
    float align_h = 0.0f, align_v = 0.0f;
    item->get_pull()->fetch_alignment(&align_h, &align_v);
    *x = static_cast<float>(*x - (static_cast<double>(align_h) + 1.0) * *w * 0.5);
    *y = static_cast<float>(*y - (static_cast<double>(align_v) + 1.0) * *h * 0.5);
}

// 0x004AE520 (reset_window_bottom: the last item's bottom - the vertical place, also for the horizontal one)
void WindowedList::change_traits(script::Struct* params)
{
    float align_h = 0.0f, align_v = 0.0f;
    if (read_alignment_param(params, kInnerAlign, &align_h, &align_v)) {
        inner_align_h_ = align_h;
        inner_align_v_ = align_v;
    }
    int speed = 0;
    if (params->fetch_int_value(kScrollSpeed, &speed, 0))
        scroll_speed_ = static_cast<float>(speed);
    params->fetch_int_value(kNumItemsToShow, &num_items_to_show_, 0);
    if (params->has_bare_flag(kResetWindowTop)) {
        window_start_ = 0.0f;
        rearrange_ = true;
    } else if (params->has_bare_flag(kResetWindowBottom)) {
        ElementPtr last;
        last.assign_value(menu_->fetch_youngest());
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        item_place(&last, &x, &y, &w, &h);
        rearrange_ = true;
        window_start_ = static_cast<float>(static_cast<double>(y) + h - window_length_);
    } else if (params->has_bare_flag(kRearrange)) {
        rearrange_ = true;
    }
    UiWidget::change_traits(params);
}

// 0x004AE6A0
bool WindowedList::pass_event_to_script(objects::Signal* event)
{
    if (!may_have(this, event) || !objects::Entity::pass_event_to_script(event))
        return false;
    objects::EventWaitBoard* tracker = objects::get_tracker();
    if (event->kind_tag_ == kFocusSentEvent) {
        find_menu();
        rearrange_ = true;
        tick_frame();
        event->set_event_handled(ident(), 0);
    }
    if (event->kind_tag_ == kFocus) {
        if (!menu_)
            find_menu();
        if (!focused_) {
            focused_ = true;
            tracker->launch_event(kFocus, menu_->ident(), kSystem, event->payload_, false, -1.0f);
            event->set_event_handled(ident(), 0);
        }
    }
    if (event->kind_tag_ == kUnfocus && focused_) {
        find_menu();
        focused_ = false;
        tracker->launch_event(kUnfocus, menu_->ident(), kSystem, nullptr, false, -1.0f);
        event->set_event_handled(ident(), 0);
    }
    return true;
}

// (The original's listener filter is 0x004AE7E0 itself, `this` the listener: the element is 0x178 before it.)
void WindowedList::MenuListener::receive(objects::Signal* event)
{
    list_->listener_receive(event);
}

// 0x004AE7E0
void WindowedList::listener_receive(objects::Signal* event)
{
    WindowedList* self = this;
    if (!may_have(self, event))
        return;
    if (event->kind_tag_ == kFocusSentEvent && self->menu_) {
        if (event->aim_point_ == self->menu_->ident()) {
            self->rearrange_ = true;
            self->tick_frame();
        }
        set_event_consumed(event, self->ident(), 0);
    }
    if (event->kind_tag_ == kFocus) {
        if (!self->menu_)
            self->find_menu();
        ElementPtr item;
        item.assign_value(self->menu_->eldest().get_pull());
        while (item) {
            if (item->ident() == event->aim_point_) {
                self->chosen_item_id_ = item->ident();
                self->rearrange_ = true;
                set_event_consumed(event, self->ident(), 0);
                break;
            }
            item.assign_value(item->following().get_pull());
        }
    }
}

// 0x004AE960
void WindowedList::tick_frame()
{
    if (!menu_)
        return;
    int index = 0;
    float before = 0.0f, after = 0.0f; // the window's margins before / after the selected item
    ElementPtr selected;
    if (chosen_item_id_)
        selected.assign_value(menu_->find_child_with_id(chosen_item_id_, &index));
    else
        selected.assign_value(menu_->find_child_at_slot(0));
    if (!selected)
        return;
    const int count = menu_->child_total();
    if (!(regular_space_ < 0.0f)) {
        // keep the selection in the middle of the shown items when it can be
        const int half = num_items_to_show_ / 2;
        if (index >= half)
            // NOLINTNEXTLINE(bugprone-integer-division): as the original
            before = static_cast<float>(static_cast<double>((num_items_to_show_ - 1) / 2) * regular_space_);
        if (index < count - half)
            after = static_cast<float>(static_cast<double>(half) * regular_space_);
    }
    menu_->change_alignment(-1.0f, -1.0f);
    float menu_x = 0.0f, menu_y = 0.0f;
    menu_->fetch_location(&menu_x, &menu_y);
    float place = 0.0f, length = 0.0f, other1 = 0.0f, other2 = 0.0f; // along the menu, and across it
    if (vertical_)
        item_place(&selected, &other1, &place, &other2, &length);
    else
        item_place(&selected, &place, &other1, &length, &other2);
    if (!(regular_space_ < 0.0f)) {
        length = regular_space_;
        place = static_cast<float>(static_cast<double>(index) * regular_space_);
    }
    if (static_cast<double>(before) + window_start_ > place) {
        // scroll back (by scroll_speed), not past the item's margin
        const double start = static_cast<double>(window_start_) - scroll_speed_;
        window_start_ = static_cast<float>(start);
        if (start < place)
            window_start_ = static_cast<float>(static_cast<double>(place) - before);
        rearrange_ = true;
    } else {
        auto end = static_cast<float>(static_cast<double>(place) + length);
        if (static_cast<double>(window_length_) + window_start_ - after < end) {
            const double start = static_cast<double>(scroll_speed_) + window_start_;
            window_start_ = static_cast<float>(start);
            if (start + window_length_ > end)
                window_start_ = static_cast<float>(static_cast<double>(end) - window_length_ + after);
            rearrange_ = true;
        }
    }
    if (!rearrange_)
        return;
    const float shift = -window_start_;
    if (vertical_)
        menu_->change_location(menu_x, shift, 1);
    else
        menu_->change_location(shift, menu_y, 1);
    // the items in the window shown, the others hidden
    index = 0;
    ElementPtr item;
    item.assign_value(menu_->eldest().get_pull());
    while (item) {
        if (vertical_)
            item_place(&item, &other1, &place, &other2, &length);
        else
            item_place(&item, &place, &other1, &length, &other2);
        if (!(regular_space_ < 0.0f)) {
            length = regular_space_;
            place = static_cast<float>(static_cast<double>(index) * regular_space_);
        }
        const bool inside = !(place < window_start_) && !(static_cast<double>(window_length_) + window_start_ <
                                                          static_cast<double>(place) + length);
        item->change_opacity(inside ? 1.0f : 0.0f, 1);
        item.assign_value(item->following().get_pull());
        ++index;
    }
    rearrange_ = false;
}

} // namespace frontend


namespace frontend {} // namespace frontend
