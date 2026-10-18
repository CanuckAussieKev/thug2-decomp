#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"
#include "game/obj/event.h"

// The scrolling menus (VScrollingMenu / HScrollingMenu): an element holding a vmenu / hmenu that shows a window of
// its items, scrolling to keep the focused item in view (items outside the window get alpha 0).
namespace Front {

class CScrollingMenu : public CScreenElement {
public:
    // 0x004AE050: the constructor (returns this): vtables 0x0064A154 / 0x0064A14C (the listener, registered for
    // the element), no menu yet, scroll_speed 10000, every item shown, no regular spacing (-1); type
    // VScrollingMenu (also for the horizontal one: its constructor doesn't change it).
    CScrollingMenu* Construct();
    // 0x004AE170 / 0x004AE1C0: vertical (vtables 0x0064A188 / 0x0064A180) / horizontal (0x0064A1BC / 0x0064A1B4).
    CScrollingMenu* ConstructVertical();
    CScrollingMenu* ConstructHorizontal();
    // 0x004AE110: the destructor body (the listener goes, then CScreenElement's). 0x004AE1A0 / 0x004AE1F0: the
    // vertical / horizontal one's (their vtables, then this).
    void DestroyBody();
    void DestroyBodyVertical();
    void DestroyBodyHorizontal();
    // 0x004AE240 / 0x004AE270 / 0x004AE2A0 (vtable slot 0 of each); 0x004AE210 / 0x004AE220 / 0x004AE230: the
    // same from the listener (+0x178).
    CScrollingMenu* Destroy(uint8_t flags);
    CScrollingMenu* DestroyVertical(uint8_t flags);
    CScrollingMenu* DestroyHorizontal(uint8_t flags);

    // 0x004AE520 (slot 1): internal_just, scroll_speed (an integer), num_items_to_show, reset_window_top /
    // reset_window_bottom / 0xA1F3B8BB (a new arrangement); then CScreenElement's.
    void SetProperties(Script::CStruct* params);
    // 0x004AE6A0 (slot 5): the event for its script first (pad-event priorities as the menus'); the focus flag's
    // event finds the menu and arranges; focus / unfocus pass to the menu. True unless refused.
    bool PassEventToScript(Obj::CEvent* event);
    // 0x004AE960 (slot 10): the window follows the selected item (by scroll_speed per frame), the menu moves
    // against it and the items outside the window are hidden.
    void Update();
    // 0x004AE7E0 (the listener's slot 1, `this` = the listener): the menu's focus flag event re-arranges; focus
    // on one of the menu's items selects it.
    void ListenerReceive(Obj::CEvent* event);

    // 0x004AE2D0: the vmenu (vertical) / hmenu (horizontal) child - the last one - and the window's length (the
    // shown items times the regular spacing, else the element's size).
    void FindMenu();
    // 0x004AE450: an item's top left and size in the menu (scaled by the menu's scale).
    void ItemPlace(ElementPtr* item, float* x, float* y, float* w, float* h);

    Obj::CEventListener m_listener; // +0x178
    CScreenElement* mp_menu;        // +0x194
    uint32_t m_selectedId;          // +0x198 the item focused
    float m_internalJustX;          // +0x19C internal_just
    float m_internalJustY;          // +0x1A0
    bool m_focused;                 // +0x1A4
    bool m_rearrange;               // +0x1A5
    uint8_t pad1A6[2];
    float m_windowStart;            // +0x1A8 along the menu
    float m_scrollSpeed;            // +0x1AC per frame
    int m_numItemsToShow;           // +0x1B0 (-1: all)
    float m_windowLength;           // +0x1B4
    float m_regularSpace;           // +0x1B8 the menu's (-1: none)
    bool m_vertical;                // +0x1BC
    uint8_t pad1BD[3];
};
static_assert(offsetof(CScrollingMenu, m_listener) == 0x178 && offsetof(CScrollingMenu, mp_menu) == 0x194 &&
                  offsetof(CScrollingMenu, m_focused) == 0x1A4 && offsetof(CScrollingMenu, m_windowStart) == 0x1A8 &&
                  offsetof(CScrollingMenu, m_regularSpace) == 0x1B8 && offsetof(CScrollingMenu, m_vertical) == 0x1BC &&
                  sizeof(CScrollingMenu) == 0x1C0,
              "layout must match the original");

} // namespace Front
