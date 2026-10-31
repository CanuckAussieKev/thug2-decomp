#pragma once

#include <cstddef>
#include <cstdint>

#include "frontend/screen_element.h"

namespace objects {
struct Signal;
}

// The menus (CreateScreenElement type vmenu / hmenu) and the container element: screen elements that arrange and
// focus their children.
namespace frontend {

// A menu (vtable 0x0064A048, 0x1A8 bytes): its children are the items, laid out in a column (vmenu) or a row
// (hmenu); the focused menu moves its selection on the pad (pad_up / pad_down, or pad_left / pad_right), sending
// unfocus / focus to the items. An item tagged as not focusable (`not_focusable`) or with block_events is
// skipped.
class MenuBase : public UiWidget {
public:
    // 0x004A74E0: the constructor (returns this): nothing selected (-1), not focused, regular spacing off,
    // padding_scale 1, pad handling and wrapping on, no grid index.
    MenuBase* make_new();
    // 0x004A75C0 / 0x004A7630: a vmenu / an hmenu (type VMenu / HMenu).
    MenuBase* construct_v_menu();
    MenuBase* construct_h_menu();
    // 0x004A7620: the destructor body (UiWidget's).
    void destroy_body();
    // 0x004A76C0 (vtable slot 0)
    MenuBase* teardown(uint8_t flags);
    // 0x004A76F0 (slot 1): internal_just, regular_space_amount (an integer), padding_scale, spacing_between (an
    // integer), enable / disable_pad_handling, allow_wrap / dont_allow_wrap; then UiWidget's.
    void change_traits(script::Struct* params);
    // 0x004A8100 (slot 5): the event for its script first (false if it went - or, with the manager's pad-event
    // priorities, if this menu may not have it); then: the focus-sent event 0x3056434F re-finds the selection and
    // lays the items out; focus (grid_index / child_id pick the item) / unfocus pass to the selected item; the pad
    // moves the selection. True.
    bool pass_event_to_script(objects::Signal* event);

    // 0x004A7550: the regular spacing into `out`; whether it is used (> 0.0001).
    int get_regular_space(float* out) const;
    // 0x004A7580: the selection as tags (tag_selected_id, tag_selected_index, the grid index 0x8321DD71).
    void set_selection_tags();
    // 0x004A7850: the selection moves by `direction` (unless `stay`, the first time) until a focusable item:
    // past an end it wraps (allow_wrap) or turns back; `grid_x`: the item's tag_grid_x becomes the grid index.
    // None focusable: nothing selected.
    void move_selection(int direction, bool stay, bool grid_x);
    // 0x004A7A40: a pad step: unfocus to the old item, the move, focus (with controller and the old item's grid
    // index) to the new one.
    void get_step(int direction);
    // 0x004A7B80: the items laid out along the menu (regularly spaced, or by their sizes with padding and
    // spacing_between) and the menu's size set; with nothing selected and the focus, the first item focused.
    void arrange();

    int selected_index_;      // +0x178 (-1 none)
    uint32_t chosen_item_id_; // +0x17C (0 none)
    bool focused_;            // +0x180
    uint8_t pad181_[3];
    int controller_;      // +0x184 the controller of the focus event
    float inner_align_h_; // +0x188 internal_just
    float inner_align_v_; // +0x18C
    bool vertical_;       // +0x190 a vmenu
    uint8_t pad191_[3];
    float regular_space_; // +0x194 regular_space_amount (0: off)
    float margin_factor_; // +0x198 padding_scale (1)
    float item_gap_;      // +0x19C spacing_between
    bool pad_handling_;   // +0x1A0 enable_pad_handling (on)
    bool cycles_around_;  // +0x1A1 allow_wrap (on)
    uint8_t pad1a2_[2];
    int grid_index_; // +0x1A4 (-1)
};
static_assert(offsetof(MenuBase, selected_index_) == 0x178 && offsetof(MenuBase, controller_) == 0x184 &&
                  offsetof(MenuBase, vertical_) == 0x190 && offsetof(MenuBase, pad_handling_) == 0x1A0 &&
                  offsetof(MenuBase, grid_index_) == 0x1A4 && sizeof(MenuBase) == 0x1A8,
              "layout must match the original");

// The container element (ContainerElement, vtable 0x0064A0BC, 0x17C bytes): passes focus / unfocus on to its
// focusable_child.
class GroupWidget : public UiWidget {
public:
    // 0x004AAC20: the constructor (returns this): no focusable child; type ContainerElement.
    GroupWidget* make_new();
    // 0x004AAC80: the destructor body (UiWidget's).
    void destroy_body();
    // 0x004AACC0 (vtable slot 0)
    GroupWidget* teardown(uint8_t flags);
    // 0x004AA530 (slot 1): focusable_child, then UiWidget's.
    void change_traits(script::Struct* params);
    // 0x004AA560 (slot 5): the event for its script first (with pad-event priorities as the menu's); focus /
    // unfocus then go on to the focusable child (with the event's controller).
    bool pass_event_to_script(objects::Signal* event);

    uint32_t focusable_child_; // +0x178
};
static_assert(sizeof(GroupWidget) == 0x17C, "layout must match the original");

} // namespace frontend
