#pragma once

// The park editor's piece menu (the upper menu: the pieces of the selected set, the set menu, the slider).

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"
#include "hook/game_global.h"

namespace Ed {

class CUpperMenuManager;
GAME_GLOBAL_EXTERN(CUpperMenuManager*, g_upperMenuManager);

class CUpperMenuManager {
public:
    // The one instance.
    static CUpperMenuManager*& Instance() { return g_upperMenuManager; }

    // 0x00598DA0: close the menus and delete the instance
    static void DeleteInstance();
    // 0x0059A9E0: show the pieces of `set` (set number `setNumber`): a new piece menu when the set changes, the
    // selected piece focused, the slider and the name. (`unused`: the callers' second argument)
    void SetPieceSet(CPieceSet* set, int unused, int setNumber);

    // upper_menu.cpp:
    // 0x00595570 (the getter the game calls)
    static CUpperMenuManager* sInstance();
    // 0x00597400: create the instance (on the bottom-up heap) if there isn't one
    static void Create();
    // 0x00597490: build the piece menu and the set menu (an item for each shown piece set)
    void Enable();
    // 0x00595580: destroy both menus
    void Disable();

    CParkManager* mp_manager; // +0x00
    CPieceSet* mp_set;        // +0x04: the set shown
    int m_setNumber;          // +0x08
    int m_selected;           // +0x0C: the entry focused
};
static_assert(offsetof(CUpperMenuManager, m_selected) == 0xC);


} // namespace Ed
