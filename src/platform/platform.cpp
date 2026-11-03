// The Win32 platform layer: input set-up, rumble and the window's message pump, with PartyMod's SDL input and
// window (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "platform/platform.h"

#include <cstdint>
#include <cstring>

#include "engine/gfx/settings.h"
#include "engine/pad_input/sdl_input.h"
#include "platform/platform_input.h"
#include "platform/sdl_events.h"
#include "engine/gfx/view_cam.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "platform/platform_state.h"

namespace os_layer {

// 0x005F3A50. Originally from PartyMod Fixes: rumble on the SDL game controller playing as `port` (PartyMod jumped from here). The
// original drove the port's DirectInput force-feedback effect (0x007D6668[port]):
//     if (!effect) return;
//     if (left == 0 && right == 0) { effect->Halt(); return; }
//     a = (int)(range0[port] * (unsigned)left / 65536);  b = (int)(range1[port] * (unsigned)right / 65536);
//       (range0/range1: 0x007D42D0 + 8 * port and +4, the effect's axis ranges; unsigned via +2^32)
//     one motor (0x007D42C8[port] == 1): direction (0, 0), magnitude (int)(a / range0 * 10000)
//     two motors: direction (a, b), magnitude (int)(sqrt(a*a + b*b) / range0 * 10000)
//     effect->SetParameters(DIEFFECT{0x38 bytes, flags 0x12, 2 axes, the direction, the magnitude},
//                           DIEP_DIRECTION | DIEP_TYPESPECIFICPARAMS | DIEP_START)
// (x87 throughout, truncated by _ftol.)
void set_actuators(int port, int left, int right)
{
    sio::set_sdl_actuators(port, static_cast<uint16_t>(left), static_cast<uint16_t>(right));
}

// 0x005F56E0. Originally from PartyMod Fixes: the SDL event pump (which also sees the quit request; PartyMod jumped from here). The
// original pumped the game window's (0x007D6A28) Win32 messages:
//     MSG msg;
//     while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
void process_window_events() { os_layer::pump_sdl_events(); }

// 0x005F44F0
bool init_input(HINSTANCE instance, HWND window, int config)
{
    (void)instance; // (the original passes it to DirectInput8Create - PartyMod's SDL input doesn't use it)
    (void)config;   // (the original's settings loader took it - PartyMod's doesn't)
    if (!window)
        return false;
    auto& direct_input = platform_state().direct_input();
    if (direct_input)
        return true;
    // Per port: the joysticks (0x007D6318), their device state, the force-feedback effects (0x007D6668) and
    // the motors' ranges and kinds; the key bindings (-1: none); the graphics options (0x007D6434...).
    core::at(platform_state().joysticks(), 0) = nullptr;
    std::memset(os_layer::platform_state().joystick_states().data(), 0, sizeof(os_layer::platform_state().joystick_states()));
    core::at(platform_state().joysticks(), 1) = nullptr;
    std::memset(os_layer::platform_state().last_joystick_states().data(), 0, sizeof(os_layer::platform_state().last_joystick_states()));
    core::at(platform_state().effects(), 0) = nullptr;
    core::at(platform_state().force_resolutions(), 0) = 0;
    core::at(platform_state().effects(), 1) = nullptr;
    core::at(platform_state().force_resolutions(), 1) = 0;
    core::at(platform_state().force_resolutions(), 2) = 0;
    core::at(platform_state().force_resolutions(), 3) = 0;
    std::memset(os_layer::get_controls().data(), 0xFF, sizeof(os_layer::get_controls()));
    render::renderer().set_antialiasing(false);
    render::view_setup().set_hq_shadows(0);
    render::view_setup().set_fog_setting(false);
    platform_state().set_option37(0);
    core::at(platform_state().force_axis_counts(), 0) = 0;
    // (the original clears dwords here across byte-sized settings; each is written by name)
    platform_state().set_options38_and39(0);
    render::view_setup().set_distance_clipping(false);
    platform_state().set_unknown643b(0);
    core::at(platform_state().force_axis_counts(), 1) = 0;
    render::renderer().set_resolution_setting(0);
    platform_state().set_distance_setting(0);
    gfx::set_draw_distance_setting(0);
    // Originally from PartyMod Fixes: the graphics settings from thug2-decomp.ini in place of the original's loader 0x005F34D0(config).
    render::write_graphics_settings();
    // Originally from PartyMod Fixes: SDL input (game controllers, the key bindings) in place of the original's
    //     DirectInput8Create(instance, 0x800, IID_IDirectInput8A, &direct_input, nullptr);
    //     direct_input->EnumDevices(0, 0x005F31D0 /* adds each joystick */, nullptr, DIEDFL_ATTACHEDONLY);
    sio::init_sdl_input();
    init_key_names();
    return true;
}

} // namespace os_layer
