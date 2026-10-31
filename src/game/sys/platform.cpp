// The Win32 platform layer: input set-up, rumble and the window's message pump, with PartyMod's SDL input and
// window (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "game/sys/platform.h"

#include <cstdint>
#include <cstring>

#include "game/gfx/settings.h"
#include "game/sio/sdl_input.h"
#include "game/sys/platform_input.h"
#include "game/sys/sdl_events.h"
#include "game/gfx/device.h"
#include "game/gfx/camera.h"

namespace Platform {

// 0x005F3A50. Originally from PartyMod Fixes: rumble on the SDL game controller playing as `port` (PartyMod jumped from here). The
// original drove the port's DirectInput force-feedback effect (0x007D6668[port]):
//     if (!effect) return;
//     if (left == 0 && right == 0) { effect->Stop(); return; }
//     a = (int)(range0[port] * (unsigned)left / 65536);  b = (int)(range1[port] * (unsigned)right / 65536);
//       (range0/range1: 0x007D42D0 + 8 * port and +4, the effect's axis ranges; unsigned via +2^32)
//     one motor (0x007D42C8[port] == 1): direction (0, 0), magnitude (int)(a / range0 * 10000)
//     two motors: direction (a, b), magnitude (int)(sqrt(a*a + b*b) / range0 * 10000)
//     effect->SetParameters(DIEFFECT{0x38 bytes, flags 0x12, 2 axes, the direction, the magnitude},
//                           DIEP_DIRECTION | DIEP_TYPESPECIFICPARAMS | DIEP_START)
// (x87 throughout, truncated by _ftol.)
void SetActuators(int port, int left, int right)
{
    Sio::SetSdlActuators(port, static_cast<uint16_t>(left), static_cast<uint16_t>(right));
}

// 0x005F56E0. Originally from PartyMod Fixes: the SDL event pump (which also sees the quit request; PartyMod jumped from here). The
// original pumped the game window's (0x007D6A28) Win32 messages:
//     MSG msg;
//     while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
void ProcessWindowEvents()
{
    Platform::PumpSdlEvents();
}

uint8_t g_option37; // (the graphics options: platform_input.h)
uint16_t g_options38And39; // (the distance clipping flag follows: gfx/settings.cpp)
uint8_t g_unknown643B;
uint32_t g_distanceSetting;

// 0x005F44F0
bool InitInput(HINSTANCE instance, HWND window, int config)
{
    (void)instance; // (the original passes it to DirectInput8Create - PartyMod's SDL input doesn't use it)
    (void)config;   // (the original's settings loader took it - PartyMod's doesn't)
    if (!window)
        return false;
    auto& directInput = g_directInput;
    if (directInput)
        return true;
    // Per port: the joysticks (0x007D6318), their device state, the force-feedback effects (0x007D6668) and
    // the motors' ranges and kinds; the key bindings (-1: none); the graphics options (0x007D6434...).
    g_joysticks[0] = nullptr;
    std::memset(Platform::g_joystickStates, 0, sizeof(Platform::g_joystickStates));
    g_joysticks[1] = nullptr;
    std::memset(Platform::g_lastJoystickStates, 0, sizeof(Platform::g_lastJoystickStates));
    g_effects[0] = nullptr;
    g_forceResolutions[0] = 0;
    g_effects[1] = nullptr;
    g_forceResolutions[1] = 0;
    g_forceResolutions[2] = 0;
    g_forceResolutions[3] = 0;
    std::memset(Platform::g_keyBindings, 0xFF, sizeof(Platform::g_keyBindings));
    Nx::g_antialiasing = false;
    Nx::g_hqShadows = 0;
    Nx::g_fogSetting = false;
    g_option37 = 0;
    g_forceAxisCounts[0] = 0;
    // (the original clears dwords here across byte-sized settings; each is written by name)
    g_options38And39 = 0;
    Nx::g_distanceClipping = false;
    g_unknown643B = 0;
    g_forceAxisCounts[1] = 0;
    reinterpret_cast<uint32_t&>(Nx::g_resolutionSetting) = 0;
    g_distanceSetting = 0;
    reinterpret_cast<uint32_t&>(Gfx::g_drawDistanceSetting) = 0;
    // Originally from PartyMod Fixes: the graphics settings from thug2-decomp.ini in place of the original's loader 0x005F34D0(config).
    Nx::WriteGraphicsSettings();
    // Originally from PartyMod Fixes: SDL input (game controllers, the key bindings) in place of the original's
    //     DirectInput8Create(instance, 0x800, IID_IDirectInput8A, &directInput, nullptr);
    //     directInput->EnumDevices(0, 0x005F31D0 /* adds each joystick */, nullptr, DIEDFL_ATTACHEDONLY);
    Sio::InitSdlInput();
    InitKeyNames();
    return true;
}

} // namespace Platform
