#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"

namespace GameObjects {
struct CSignal;
}

// The menus (CreateScreenElement type vmenu / hmenu) and the container element: screen elements that arrange and
// focus their children.
namespace MenuSystem {

// A menu (vtable 0x0064A048, 0x1A8 bytes): its children are the items, laid out in a column (vmenu) or a row
// (hmenu); the focused menu moves its selection on the pad (pad_up / pad_down, or pad_left / pad_right), sending
// unfocus / focus to the items. An item tagged as not focusable (`not_focusable`) or with block_events is
// skipped.
class CMenuBase : public CUiWidget {
public:
    // 0x004A74E0: the constructor (returns this): nothing selected (-1), not focused, regular spacing off,
    // padding_scale 1, pad handling and wrapping on, no grid index.
    CMenuBase* MakeNew();
    // 0x004A75C0 / 0x004A7630: a vmenu / an hmenu (type VMenu / HMenu).
    CMenuBase* ConstructVMenu();
    CMenuBase* ConstructHMenu();
    // 0x004A7620: the destructor body (CUiWidget's).
    void DestroyBody();
    // 0x004A76C0 (vtable slot 0)
    CMenuBase* Teardown(uint8_t flags);
    // 0x004A76F0 (slot 1): internal_just, regular_space_amount (an integer), padding_scale, spacing_between (an
    // integer), enable / disable_pad_handling, allow_wrap / dont_allow_wrap; then CUiWidget's.
    void ChangeTraits(Scripts::CStruct* params);
    // 0x004A8100 (slot 5): the event for its script first (false if it went - or, with the manager's pad-event
    // priorities, if this menu may not have it); then: the focus-sent event 0x3056434F re-finds the selection and
    // lays the items out; focus (grid_index / child_id pick the item) / unfocus pass to the selected item; the pad
    // moves the selection. True.
    bool PassEventToScript(GameObjects::CSignal* event);

    // 0x004A7550: the regular spacing into `out`; whether it is used (> 0.0001).
    int GetRegularSpace(float* out) const;
    // 0x004A7580: the selection as tags (tag_selected_id, tag_selected_index, the grid index 0x8321DD71).
    void SetSelectionTags();
    // 0x004A7850: the selection moves by `direction` (unless `stay`, the first time) until a focusable item:
    // past an end it wraps (allow_wrap) or turns back; `gridX`: the item's tag_grid_x becomes the grid index.
    // None focusable: nothing selected.
    void MoveSelection(int direction, bool stay, bool gridX);
    // 0x004A7A40: a pad step: unfocus to the old item, the move, focus (with controller and the old item's grid
    // index) to the new one.
    void Step(int direction);
    // 0x004A7B80: the items laid out along the menu (regularly spaced, or by their sizes with padding and
    // spacing_between) and the menu's size set; with nothing selected and the focus, the first item focused.
    void Arrange();

    int m_selectedIndex;     // +0x178 (-1 none)
    uint32_t m_chosenItemId;   // +0x17C (0 none)
    bool m_focused;          // +0x180
    uint8_t m_pad181[3];
    int m_controller;        // +0x184 the controller of the focus event
    float m_innerAlignH;   // +0x188 internal_just
    float m_innerAlignV;   // +0x18C
    bool m_vertical;         // +0x190 a vmenu
    uint8_t m_pad191[3];
    float m_regularSpace;    // +0x194 regular_space_amount (0: off)
    float m_marginFactor;    // +0x198 padding_scale (1)
    float m_itemGap;  // +0x19C spacing_between
    bool m_padHandling;      // +0x1A0 enable_pad_handling (on)
    bool m_cyclesAround;        // +0x1A1 allow_wrap (on)
    uint8_t m_pad1A2[2];
    int m_gridIndex;         // +0x1A4 (-1)
};
static_assert(offsetof(CMenuBase, m_selectedIndex) == 0x178 && offsetof(CMenuBase, m_controller) == 0x184 &&
                  offsetof(CMenuBase, m_vertical) == 0x190 && offsetof(CMenuBase, m_padHandling) == 0x1A0 &&
                  offsetof(CMenuBase, m_gridIndex) == 0x1A4 && sizeof(CMenuBase) == 0x1A8,
              "layout must match the original");

// The container element (ContainerElement, vtable 0x0064A0BC, 0x17C bytes): passes focus / unfocus on to its
// focusable_child.
class CGroupWidget : public CUiWidget {
public:
    // 0x004AAC20: the constructor (returns this): no focusable child; type ContainerElement.
    CGroupWidget* MakeNew();
    // 0x004AAC80: the destructor body (CUiWidget's).
    void DestroyBody();
    // 0x004AACC0 (vtable slot 0)
    CGroupWidget* Teardown(uint8_t flags);
    // 0x004AA530 (slot 1): focusable_child, then CUiWidget's.
    void ChangeTraits(Scripts::CStruct* params);
    // 0x004AA560 (slot 5): the event for its script first (with pad-event priorities as the menu's); focus /
    // unfocus then go on to the focusable child (with the event's controller).
    bool PassEventToScript(GameObjects::CSignal* event);

    uint32_t m_focusableChild; // +0x178
};
static_assert(sizeof(CGroupWidget) == 0x17C, "layout must match the original");

} // namespace Front
