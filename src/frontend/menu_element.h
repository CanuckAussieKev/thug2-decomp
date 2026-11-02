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
class MenuBase final : public UiWidget {
public:
    // 0x004A74E0: nothing selected (-1), not focused, regular spacing off, padding_scale 1, pad handling and wrapping
    // on, no grid index. 0x004A75C0 / 0x004A7630: a vmenu / an hmenu (type VMenu / HMenu).
    explicit MenuBase(bool vertical);
    // 0x004A7620 (0x004A76C0 the deleting one): UiWidget's.
    ~MenuBase() override = default;
    // 0x004A76F0 (slot 1): internal_just, regular_space_amount (an integer), padding_scale, spacing_between (an
    // integer), enable / disable_pad_handling, allow_wrap / dont_allow_wrap; then UiWidget's.
    void change_traits(script::Struct* params) override;
    // 0x004A8100 (slot 5): the event for its script first (false if it went - or, with the manager's pad-event
    // priorities, if this menu may not have it); then: the focus-sent event 0x3056434F re-finds the selection and
    // lays the items out; focus (grid_index / child_id pick the item) / unfocus pass to the selected item; the pad
    // moves the selection. True.
    bool pass_event_to_script(objects::Signal* event) override;

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

    // The fields other code reads and sets.
    [[nodiscard]] int selected_index() const { return selected_index_; }
    [[nodiscard]] bool focused() const { return focused_; }
    [[nodiscard]] bool vertical() const { return vertical_; }
    [[nodiscard]] bool pad_handling() const { return pad_handling_; }
    [[nodiscard]] bool cycles_around() const { return cycles_around_; }

private:
    int selected_index_{-1};     // +0x178 (-1 none)
    uint32_t chosen_item_id_{0}; // +0x17C (0 none)
    bool focused_{false};        // +0x180
    int controller_{0};          // +0x184 the controller of the focus event
    float inner_align_h_{0.0f};  // +0x188 internal_just
    float inner_align_v_{0.0f};  // +0x18C
    bool vertical_;           // +0x190 a vmenu
    float regular_space_{0.0f}; // +0x194 regular_space_amount (0: off)
    float margin_factor_{1.0f}; // +0x198 padding_scale (1)
    float item_gap_{0.0f};      // +0x19C spacing_between
    bool pad_handling_{true};   // +0x1A0 enable_pad_handling (on)
    bool cycles_around_{true};  // +0x1A1 allow_wrap (on)
    int grid_index_{-1};        // +0x1A4 (-1)
};

// The container element (ContainerElement, vtable 0x0064A0BC, 0x17C bytes): passes focus / unfocus on to its
// focusable_child.
class GroupWidget final : public UiWidget {
public:
    // 0x004AAC20: no focusable child; type ContainerElement.
    GroupWidget();
    // 0x004AAC80 (0x004AACC0 the deleting one): UiWidget's.
    ~GroupWidget() override;
    // 0x004AA530 (slot 1): focusable_child, then UiWidget's.
    void change_traits(script::Struct* params) override;
    // 0x004AA560 (slot 5): the event for its script first (with pad-event priorities as the menu's); focus /
    // unfocus then go on to the focusable child (with the event's controller).
    bool pass_event_to_script(objects::Signal* event) override;

private:
    uint32_t focusable_child_{0}; // +0x178
};

} // namespace frontend
