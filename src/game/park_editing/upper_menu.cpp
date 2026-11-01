// The park editor's piece menu.

#include <cstdint>

#include "game/park_editing/piece_menu.h"

#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace park_editing {

namespace {

void run_menu_script(uint32_t script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}

} // namespace

TopBarMenus* TopBarMenus::singleton() { return sole(); }

void TopBarMenus::fabricate()
{
    if (TopBarMenus::sole())
        return;
    memory::enter_heap_scope(memory::bottom_up_heap());
    auto* menu = static_cast<TopBarMenus*>(memory::alloc(sizeof(TopBarMenus)));
    if (menu) {
        menu->point_group_ = nullptr;
        menu->selected_ = 0;
        menu->set_number_ = 0;
        menu->park_owner_ = ParkLayout::singleton();
    }
    TopBarMenus::sole() = menu;
    memory::leave_heap_scope();
}

void TopBarMenus::turn_on()
{
    run_menu_script(0x4693636A); // parked_make_piece_menu
    run_menu_script(0xED885B50); // parked_make_set_menu
    for (int i = 0; i < park_owner_->num_sets_; ++i) {
        PieceFamily& set = park_owner_->sets_[i];
        if (!set.is_displayed_)
            continue;
        script::Struct params;
        params.append_word(0x341B6370, set.label_); // set_name
        run_menu_script(0xA4F522E9, &params);       // parked_make_set_menu_item
        params.get_clear();
    }
    point_group_ = nullptr;
}

void TopBarMenus::turn_off_now()
{
    run_menu_script(0x5C7A3276); // parked_destroy_piece_menu
    run_menu_script(0x6BF4AA3D); // parked_destroy_set_menu
    point_group_ = nullptr;
}

} // namespace park_editing

using park_editing::TopBarMenus;
