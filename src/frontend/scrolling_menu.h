#pragma once

#include <cstddef>
#include <cstdint>

#include "frontend/screen_element.h"
#include "engine/objects/signal.h"

// The scrolling menus (VScrollingMenu / HScrollingMenu): an element holding a vmenu / hmenu that shows a window of
// its items, scrolling to keep the focused item in view (items outside the window get alpha 0).
namespace frontend {

class WindowedList : public UiWidget {
public:
    // 0x004AE050: the constructor (returns this): vtables 0x0064A154 / 0x0064A14C (the listener, registered for
    // the element), no menu yet, scroll_speed 10000, every item shown, no regular spacing (-1); type
    // VScrollingMenu (also for the horizontal one: its constructor doesn't change it).
    WindowedList* make_new();
    // 0x004AE170 / 0x004AE1C0: vertical (vtables 0x0064A188 / 0x0064A180) / horizontal (0x0064A1BC / 0x0064A1B4).
    WindowedList* construct_vertical();
    WindowedList* construct_horizontal();
    // 0x004AE110: the destructor body (the listener goes, then UiWidget's). 0x004AE1A0 / 0x004AE1F0: the
    // vertical / horizontal one's (their vtables, then this).
    void destroy_body();
    void destroy_body_vertical();
    void destroy_body_horizontal();
    // 0x004AE240 / 0x004AE270 / 0x004AE2A0 (vtable slot 0 of each); 0x004AE210 / 0x004AE220 / 0x004AE230: the
    // same from the listener (+0x178).
    WindowedList* teardown(uint8_t flags);
    WindowedList* destroy_vertical(uint8_t flags);
    WindowedList* destroy_horizontal(uint8_t flags);

    // 0x004AE520 (slot 1): internal_just, scroll_speed (an integer), num_items_to_show, reset_window_top /
    // reset_window_bottom / 0xA1F3B8BB (a new arrangement); then UiWidget's.
    void change_traits(script::Struct* params);
    // 0x004AE6A0 (slot 5): the event for its script first (pad-event priorities as the menus'); the focus flag's
    // event finds the menu and arranges; focus / unfocus pass to the menu. True unless refused.
    bool pass_event_to_script(objects::Signal* event);
    // 0x004AE960 (slot 10): the window follows the selected item (by scroll_speed per frame), the menu moves
    // against it and the items outside the window are hidden.
    void tick_frame();
    // 0x004AE7E0 (the listener's filter): the menu's focus flag event re-arranges; focus on one of the menu's items
    // selects it.
    void listener_receive(objects::Signal* event);

    // The element's event listener: its events go to listener_receive. (The original's listener vtables - one per
    // constructor - differ only in the destructor, which nothing calls through the listener.)
    struct MenuListener final : objects::SignalReceiver {
        void receive(objects::Signal* event) override;
    };

    // 0x004AE2D0: the vmenu (vertical) / hmenu (horizontal) child - the last one - and the window's length (the
    // shown items times the regular spacing, else the element's size).
    void find_menu();
    // 0x004AE450: an item's top left and size in the menu (scaled by the menu's scale).
    void item_place(ElementPtr* item, float* x, float* y, float* w, float* h);

    MenuListener listener_;   // +0x178
    UiWidget* menu_;          // +0x194
    uint32_t chosen_item_id_; // +0x198 the item focused
    float inner_align_h_;     // +0x19C internal_just
    float inner_align_v_;     // +0x1A0
    bool focused_;            // +0x1A4
    bool rearrange_;          // +0x1A5
    uint8_t pad1a6_[2];
    float window_start_;    // +0x1A8 along the menu
    float scroll_speed_;    // +0x1AC per frame
    int num_items_to_show_; // +0x1B0 (-1: all)
    float window_length_;   // +0x1B4
    float regular_space_;   // +0x1B8 the menu's (-1: none)
    bool vertical_;         // +0x1BC
    uint8_t pad1bd_[3];
};
static_assert(offsetof(WindowedList, listener_) == 0x178 && offsetof(WindowedList, menu_) == 0x194 &&
                  offsetof(WindowedList, focused_) == 0x1A4 && offsetof(WindowedList, window_start_) == 0x1A8 &&
                  offsetof(WindowedList, regular_space_) == 0x1B8 && offsetof(WindowedList, vertical_) == 0x1BC &&
                  sizeof(WindowedList) == 0x1C0,
              "layout must match the original");

} // namespace frontend
