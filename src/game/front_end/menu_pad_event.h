#pragma once

#include <cstdint>

namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - a menu pad event (pad_up, pad_choose, pad_start...) sent as modules::FrontEnd::input_logic sends it: for
// the first controller's device, unless its mapping is inactive or menu input is blocked. Ours, from the mouse_menus
// feature (frontend/mouse_menus.h); the test-automation channel, -edit and the level editor's play press menu buttons
// with it too.
namespace modules {

// Why the event wouldn't be sent now (nullptr: it would be).
const char* menu_pad_event_refusal(FrontEnd* front_end);
// The event launched (the menu event's script parameters: device_num, controller 0). Only when there is no refusal.
void launch_menu_pad_event(FrontEnd* front_end, uint32_t event);
// Both: sent unless refused.
void send_menu_pad_event(FrontEnd* front_end, uint32_t event);

} // namespace modules
