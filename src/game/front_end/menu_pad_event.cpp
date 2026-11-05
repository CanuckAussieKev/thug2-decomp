// NOT ORIGINAL - a menu pad event sent as the front end's input logic sends it (menu_pad_event.h): ours, from the
// mouse_menus feature.

#include "game/front_end/menu_pad_event.h"

#include <cstdint>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "engine/objects/tracker.h"
#include "engine/pad_input/device.h"
#include "engine/script/param_struct.h"
#include "game/front_end/front_end.h"

namespace modules {

namespace {

constexpr uint32_t kDeviceNum = 0xC9428A08;  // "device_num"
constexpr uint32_t kController = 0xB30D9965; // "controller"
constexpr uint32_t kMenuEvent = 0x36B2EE74;  // the pad events' target and source

} // namespace

const char* menu_pad_event_refusal(FrontEnd* front_end)
{
    const char* not_sent = nullptr;
    const FrontEndInputHandler* handler = core::at(front_end->pad_listeners(), 0).get();
    if (core::at(front_end->pad_mappings(), 0).mapping_ == 0) {
        not_sent = "the first controller's pad mapping is inactive";
    } else if (front_end->block_menu_input()) {
        not_sent = "menu input is blocked (SetButtonEventMappings block_menu_input)";
    } else if (!handler || !handler->dev()) {
        not_sent = "no input device";
    }
    return not_sent;
}

void launch_menu_pad_event(FrontEnd* front_end, uint32_t event)
{
    const FrontEndInputHandler* handler = core::at(front_end->pad_listeners(), 0).get();
    script::Struct params;
    params.put_int(checksums::Name{kDeviceNum}, handler->dev()->port_no());
    params.put_int(checksums::Name{kController}, 0);
    objects::get_tracker()->launch_event(event, kMenuEvent, kMenuEvent, &params, false, -1.0F);
    params.get_clear();
}

void send_menu_pad_event(FrontEnd* front_end, uint32_t event)
{
    if (menu_pad_event_refusal(front_end) != nullptr) {
        return;
    }
    launch_menu_pad_event(front_end, event);
}

} // namespace modules
