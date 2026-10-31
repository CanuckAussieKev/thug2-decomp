// The scrolling menus: see scrolling_menu.h.

#include <cstddef>
#include <cstdint>

#include "frontend/scrolling_menu.h"

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "frontend/menu_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/tracker.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element.h"

namespace frontend {

namespace {
extern void** const kListenerAdapterVtable;
extern void** const kCScrollingMenuVtable;
extern void** const kListenerAdapterVtableA180;
extern void** const kCScrollingMenuVtableA188;
extern void** const kListenerAdapterVtableA1b4;
extern void** const kCScrollingMenuVtableA1bC;
} // namespace

namespace {

using script::Struct;

constexpr uint32_t kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kInnerAlign = 0x67E093E4, kScrollSpeed = 0x9020A1D1, kNumItemsToShow = 0xC855511C;
constexpr uint32_t kResetWindowTop = 0xED889E1F, kResetWindowBottom = 0xC3A894F6, kRearrange = 0xA1F3B8BB;
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;

// The menus' type names, hashed at run time (the strings 0x0064A1F0 "vmenu" / 0x0064A1E8 "hmenu").
uint32_t v_menu_type() { return checksums::hash_from_text("vmenu"); }
uint32_t h_menu_type() { return checksums::hash_from_text("hmenu"); }

void v_update(UiWidget* e) { reinterpret_cast<void(__thiscall*)(UiWidget*)>(e->vtable_[10])(e); }

void set_event_consumed(objects::Signal* event, uint32_t how, uint32_t script)
{
    event->set_event_consumed(how, script);
}

UiTree* manager_of(const UiWidget* e) { return static_cast<UiTree*>(e->park_owner_); }

// With the manager's pad-event priorities, only the top priority element may have a pad event.
bool may_have(UiWidget* e, objects::Signal* event)
{
    UiTree* manager = manager_of(e);
    if (e->manager_uses_priorities() && manager->is_pad_event(event->kind_tag_))
        return e->has_priority() && manager->has_top_precedence(e->ident_);
    return true;
}

} // namespace

// 0x004AE050
WindowedList* WindowedList::make_new()
{
    UiWidget::make_new();
    listener_.make_new();
    vtable_ = kCScrollingMenuVtable;
    listener_.vtable_ = kListenerAdapterVtable;
    listener_.enter_wait_board(this);
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
    return this;
}

// 0x004AE170
WindowedList* WindowedList::construct_vertical()
{
    make_new();
    vtable_ = kCScrollingMenuVtableA188;
    listener_.vtable_ = kListenerAdapterVtableA180;
    vertical_ = true;
    return this;
}

// 0x004AE1C0
WindowedList* WindowedList::construct_horizontal()
{
    make_new();
    vtable_ = kCScrollingMenuVtableA1bC;
    listener_.vtable_ = kListenerAdapterVtableA1b4;
    vertical_ = false;
    return this;
}

// 0x004AE110
void WindowedList::destroy_body()
{
    vtable_ = kCScrollingMenuVtable;
    listener_.vtable_ = kListenerAdapterVtable;
    listener_.teardown();
    UiWidget::destroy_body();
}

// 0x004AE1A0
void WindowedList::destroy_body_vertical()
{
    vtable_ = kCScrollingMenuVtableA188;
    listener_.vtable_ = kListenerAdapterVtableA180;
    destroy_body();
}

// 0x004AE1F0
void WindowedList::destroy_body_horizontal()
{
    vtable_ = kCScrollingMenuVtableA1bC;
    listener_.vtable_ = kListenerAdapterVtableA1b4;
    destroy_body();
}

// 0x004AE240
WindowedList* WindowedList::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004AE270
WindowedList* WindowedList::destroy_vertical(uint8_t flags)
{
    destroy_body_vertical();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004AE2A0
WindowedList* WindowedList::destroy_horizontal(uint8_t flags)
{
    destroy_body_horizontal();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004AE2D0
void WindowedList::find_menu()
{
    {
        ElementPtr child;
        child.assign_value(eldest_.owner_object_);
        while (child) {
            if (child->kind_tag_ == v_menu_type() && vertical_)
                menu_ = child.owner_object_;
            else if (child->kind_tag_ == h_menu_type() && !vertical_)
                menu_ = child.owner_object_;
            child.assign_value(child->following_.owner_object_);
        }
    }
    float space = -1.0f;
    if (static_cast<MenuBase*>(menu_)->get_regular_space(&space) && num_items_to_show_ >= 0) {
        regular_space_ = space;
        window_length_ = static_cast<float>(static_cast<double>(num_items_to_show_) * space);
    } else {
        num_items_to_show_ = -1;
        window_length_ = vertical_ ? level_y_ : span_;
    }
}

// 0x004AE450 (the item's justification is read into the original's argument slots - no effect)
void WindowedList::item_place(ElementPtr* item, float* x, float* y, float* w, float* h)
{
    item->owner_object_->fetch_location(x, y);
    *x = static_cast<float>(static_cast<double>(menu_->shown_.scale_x_) * *x);
    *y = static_cast<float>(static_cast<double>(menu_->shown_.scale_y_) * *y);
    *w = static_cast<float>(static_cast<double>(menu_->shown_.scale_x_) * item->owner_object_->span_ *
                            item->owner_object_->shown_.scale_x_);
    *h = static_cast<float>(static_cast<double>(menu_->shown_.scale_y_) * item->owner_object_->level_y_ *
                            item->owner_object_->shown_.scale_y_);
    float align_h, align_v;
    item->owner_object_->fetch_alignment(&align_h, &align_v);
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
    int speed;
    if (params->fetch_int_value(kScrollSpeed, &speed, 0))
        scroll_speed_ = static_cast<float>(speed);
    params->fetch_int_value(kNumItemsToShow, &num_items_to_show_, 0);
    if (params->has_bare_flag(kResetWindowTop)) {
        window_start_ = 0.0f;
        rearrange_ = true;
    } else if (params->has_bare_flag(kResetWindowBottom)) {
        ElementPtr last;
        last.assign_value(menu_->fetch_youngest());
        float x, y, w, h;
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
        v_update(this);
        event->set_event_handled(ident_, 0);
    }
    if (event->kind_tag_ == kFocus) {
        if (!menu_)
            find_menu();
        if (!focused_) {
            focused_ = true;
            tracker->launch_event(kFocus, menu_->ident_, kSystem, event->payload_, false, -1.0f);
            event->set_event_handled(ident_, 0);
        }
    }
    if (event->kind_tag_ == kUnfocus && focused_) {
        find_menu();
        focused_ = false;
        tracker->launch_event(kUnfocus, menu_->ident_, kSystem, nullptr, false, -1.0f);
        event->set_event_handled(ident_, 0);
    }
    return true;
}

// 0x004AE7E0
void WindowedList::listener_receive(objects::Signal* event)
{
    // (`this` is the listener: the element is 0x178 before it)
    auto* self = reinterpret_cast<WindowedList*>(reinterpret_cast<uint8_t*>(this) - offsetof(WindowedList, listener_));
    if (!may_have(self, event))
        return;
    if (event->kind_tag_ == kFocusSentEvent && self->menu_) {
        if (event->aim_point_ == self->menu_->ident_) {
            self->rearrange_ = true;
            v_update(self);
        }
        set_event_consumed(event, self->ident_, 0);
    }
    if (event->kind_tag_ == kFocus) {
        if (!self->menu_)
            self->find_menu();
        ElementPtr item;
        item.assign_value(self->menu_->eldest_.owner_object_);
        while (item) {
            if (item->ident_ == event->aim_point_) {
                self->chosen_item_id_ = item->ident_;
                self->rearrange_ = true;
                set_event_consumed(event, self->ident_, 0);
                break;
            }
            item.assign_value(item->following_.owner_object_);
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
    int count = menu_->child_total();
    if (!(regular_space_ < 0.0f)) {
        // keep the selection in the middle of the shown items when it can be
        int half = num_items_to_show_ / 2;
        if (index >= half)
            before = static_cast<float>(static_cast<double>((num_items_to_show_ - 1) / 2) * regular_space_);
        if (index < count - half)
            after = static_cast<float>(static_cast<double>(half) * regular_space_);
    }
    menu_->change_alignment(-1.0f, -1.0f);
    float menu_x, menu_y;
    menu_->fetch_location(&menu_x, &menu_y);
    float place, length, other1, other2; // along the menu, and across it
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
        double start = static_cast<double>(window_start_) - scroll_speed_;
        window_start_ = static_cast<float>(start);
        if (start < place)
            window_start_ = static_cast<float>(static_cast<double>(place) - before);
        rearrange_ = true;
    } else {
        float end = static_cast<float>(static_cast<double>(place) + length);
        if (static_cast<double>(window_length_) + window_start_ - after < end) {
            double start = static_cast<double>(scroll_speed_) + window_start_;
            window_start_ = static_cast<float>(start);
            if (start + window_length_ > end)
                window_start_ = static_cast<float>(static_cast<double>(end) - window_length_ + after);
            rearrange_ = true;
        }
    }
    if (!rearrange_)
        return;
    float shift = -window_start_;
    if (vertical_)
        menu_->change_location(menu_x, shift, 1);
    else
        menu_->change_location(shift, menu_y, 1);
    // the items in the window shown, the others hidden
    index = 0;
    ElementPtr item;
    item.assign_value(menu_->eldest_.owner_object_);
    while (item) {
        if (vertical_)
            item_place(&item, &other1, &place, &other2, &length);
        else
            item_place(&item, &place, &other1, &length, &other2);
        if (!(regular_space_ < 0.0f)) {
            length = regular_space_;
            place = static_cast<float>(static_cast<double>(index) * regular_space_);
        }
        bool inside = !(place < window_start_) &&
                      !(static_cast<double>(window_length_) + window_start_ < static_cast<double>(place) + length);
        item->change_opacity(inside ? 1.0f : 0.0f, 1);
        item.assign_value(item->following_.owner_object_);
        ++index;
    }
    rearrange_ = false;
}

} // namespace frontend

namespace {

// The listener's slot 0 (`this` = the listener at +0x178): the element's destructor.
struct ListenerAdapter {
    frontend::WindowedList* get_self()
    {
        return reinterpret_cast<frontend::WindowedList*>(reinterpret_cast<uint8_t*>(this) - 0x178);
    }
    frontend::WindowedList* teardown(uint8_t flags) { return get_self()->teardown(flags); }
    frontend::WindowedList* destroy_vertical(uint8_t flags) { return get_self()->destroy_vertical(flags); }
    frontend::WindowedList* destroy_horizontal(uint8_t flags) { return get_self()->destroy_horizontal(flags); }
};

} // namespace

using frontend::WindowedList;

namespace frontend {
namespace {
static VtableSlot g_listener_adapter_vtable_slots[] = {&ListenerAdapter::teardown, &WindowedList::listener_receive};
void** const kListenerAdapterVtable = reinterpret_cast<void**>(g_listener_adapter_vtable_slots);
static VtableSlot g_c_scrolling_menu_vtable_slots[] = {
    &WindowedList::teardown,
    &WindowedList::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &WindowedList::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &UiWidget::get_properties,
    &UiWidget::blend_towards,
    &WindowedList::tick_frame};
void** const kCScrollingMenuVtable = reinterpret_cast<void**>(g_c_scrolling_menu_vtable_slots);
static VtableSlot g_listener_adapter_vtable_a180_slots[] = {&ListenerAdapter::destroy_vertical,
                                                            &WindowedList::listener_receive};
void** const kListenerAdapterVtableA180 = reinterpret_cast<void**>(g_listener_adapter_vtable_a180_slots);
static VtableSlot g_c_scrolling_menu_vtable_a188_slots[] = {
    &WindowedList::destroy_vertical,
    &WindowedList::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &WindowedList::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &UiWidget::get_properties,
    &UiWidget::blend_towards,
    &WindowedList::tick_frame};
void** const kCScrollingMenuVtableA188 = reinterpret_cast<void**>(g_c_scrolling_menu_vtable_a188_slots);
static VtableSlot g_listener_adapter_vtable_a_1b_4_slots[] = {&ListenerAdapter::destroy_horizontal,
                                                              &WindowedList::listener_receive};
void** const kListenerAdapterVtableA1b4 = reinterpret_cast<void**>(g_listener_adapter_vtable_a_1b_4_slots);
static VtableSlot g_c_scrolling_menu_vtable_a_1b_c_slots[] = {
    &WindowedList::destroy_horizontal,
    &WindowedList::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &WindowedList::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &UiWidget::get_properties,
    &UiWidget::blend_towards,
    &WindowedList::tick_frame};
void** const kCScrollingMenuVtableA1bC = reinterpret_cast<void**>(g_c_scrolling_menu_vtable_a_1b_c_slots);
} // namespace
} // namespace frontend
