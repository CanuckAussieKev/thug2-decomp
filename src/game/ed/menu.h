#pragma once

// The park editor's piece menu (the "upper menu": the piece menu and the menu of piece sets). One instance
// (CUpperMenuManager::sInstance, 0x007CE60C), 0x10 bytes, destroyed by 0x00598DA0.

#include <cstddef>
#include <cstdint>

#include "game/ed/park_manager.h"

namespace Ed {

class CUpperMenuManager {
public:
    // 0x00595570
    static CUpperMenuManager* sInstance();
    // 0x00597400: create the instance (on the bottom-up heap) if there isn't one.
    static void Create();

    // 0x00597490: build the piece menu and the set menu (an item for each used piece set).
    void Enable();
    // 0x00595580: destroy both menus.
    void Disable();

    CParkManager* mp_manager;
    int m_unknown4; // set to 0 by Enable and Disable
    uint32_t unknown8;
    uint32_t unknownC;
};
static_assert(sizeof(CUpperMenuManager) == 0x10, "layout must match the original");

} // namespace Ed
