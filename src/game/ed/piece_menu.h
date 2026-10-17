#pragma once

// The park editor's piece menu (the upper menu: the pieces of the selected set, the set menu, the slider).

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"

namespace Ed {

class CUpperMenuManager {
public:
    // The one instance.
    static CUpperMenuManager*& Instance() { return GameVariable<CUpperMenuManager*>(0x007CE60C); }

    // 0x00598DA0: close the menus and delete the instance
    static void DeleteInstance();
    // 0x0059A9E0: show the pieces of `set` (set number `setNumber`): a new piece menu when the set changes, the
    // selected piece focused, the slider and the name. (`unused`: the callers' second argument)
    void SetPieceSet(CPieceSet* set, int unused, int setNumber);

    CParkManager* mp_manager; // +0x00
    CPieceSet* mp_set;        // +0x04: the set shown
    int m_setNumber;          // +0x08
    int m_selected;           // +0x0C: the entry focused
};
static_assert(offsetof(CUpperMenuManager, m_selected) == 0xC);

} // namespace Ed
