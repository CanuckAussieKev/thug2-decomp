#pragma once

// The park editor's piece menu (the upper menu: the pieces of the selected set, the set menu, the slider).

#include <cstddef>
#include <cstdint>

#include "game/park_editing/park.h"

namespace park_editing {

class TopBarMenus;

class TopBarMenus {
public:
    // The one instance.
    static TopBarMenus*& sole();

    // 0x00598DA0: close the menus and delete the instance
    static void discard_occurrence();
    // 0x0059A9E0: show the pieces of `set` (set number `family_number`): a new piece menu when the set changes, the
    // selected piece focused, the slider and the name. (`unused`: the callers' second argument)
    void select_piece_family(PieceFamily* set, int unused, int family_number);

    // upper_menu.cpp:
    // 0x00595570 (the getter the game calls)
    static TopBarMenus* singleton();
    // 0x00597400: create the instance (on the bottom-up heap) if there isn't one
    static void fabricate();
    // 0x00597490: build the piece menu and the set menu (an item for each shown piece set)
    void turn_on();
    // 0x00595580: destroy both menus
    void turn_off_now();

private:
    ParkLayout* park_owner_;   // +0x00
    PieceFamily* point_group_; // +0x04: the set shown
    int set_number_;           // +0x08
    int selected_;             // +0x0C: the entry focused
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(TopBarMenus, selected_) == 0xC);
    }
};

} // namespace park_editing
