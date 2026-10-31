#pragma once

// The park editor's piece menu (the upper menu: the pieces of the selected set, the set menu, the slider).

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"

namespace ParkEditing {

class CTopBarMenus;
extern CTopBarMenus* g_upperMenuManager;

class CTopBarMenus {
public:
    // The one instance.
    static CTopBarMenus*& Instance() { return g_upperMenuManager; }

    // 0x00598DA0: close the menus and delete the instance
    static void DeleteInstance();
    // 0x0059A9E0: show the pieces of `set` (set number `familyNumber`): a new piece menu when the set changes, the
    // selected piece focused, the slider and the name. (`unused`: the callers' second argument)
    void SelectPieceFamily(CPieceFamily* set, int unused, int familyNumber);

    // upper_menu.cpp:
    // 0x00595570 (the getter the game calls)
    static CTopBarMenus* sSingleton();
    // 0x00597400: create the instance (on the bottom-up heap) if there isn't one
    static void Create();
    // 0x00597490: build the piece menu and the set menu (an item for each shown piece set)
    void Enable();
    // 0x00595580: destroy both menus
    void Disable();

    CParkLayout* m_parkOwner; // +0x00
    CPieceFamily* mp_set;        // +0x04: the set shown
    int m_setNumber;          // +0x08
    int m_selected;           // +0x0C: the entry focused
};
static_assert(offsetof(CTopBarMenus, m_selected) == 0xC);


} // namespace Ed
