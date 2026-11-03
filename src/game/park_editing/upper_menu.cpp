// The park editor's piece menu.

#include <cstdint>

#include "game/park_editing/piece_menu.h"

#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park.h"

namespace park_editing {

namespace {

constexpr std::uint32_t kParkedDestroyPieceMenu = 0x5C7A3276U; // parked_destroy_piece_menu
constexpr std::uint32_t kParkedDestroySetMenu = 0x6BF4AA3DU;   // parked_destroy_set_menu
constexpr std::uint32_t kParkedMakePieceMenu = 0x4693636AU;    // parked_make_piece_menu
constexpr std::uint32_t kParkedMakeSetMenu = 0xED885B50U;      // parked_make_set_menu
constexpr std::uint32_t kParkedMakeSetMenuItem = 0xA4F522E9U;  // parked_make_set_menu_item
constexpr std::uint32_t kSetName = 0x341B6370U;                // set_name

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
    run_menu_script(kParkedMakePieceMenu); // parked_make_piece_menu
    run_menu_script(kParkedMakeSetMenu); // parked_make_set_menu
    for (int i = 0; i < park_owner_->num_sets(); ++i) {
        const PieceFamily& set = core::at(park_owner_->sets(), i);
        if (!set.is_displayed_)
            continue;
        script::Struct params;
        params.append_word(kSetName, set.label_); // set_name
        run_menu_script(kParkedMakeSetMenuItem, &params);       // parked_make_set_menu_item
        params.get_clear();
    }
    point_group_ = nullptr;
}

void TopBarMenus::turn_off_now()
{
    run_menu_script(kParkedDestroyPieceMenu); // parked_destroy_piece_menu
    run_menu_script(kParkedDestroySetMenu); // parked_destroy_set_menu
    point_group_ = nullptr;
}

} // namespace park_editing

