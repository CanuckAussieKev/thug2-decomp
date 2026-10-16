// The Win32 platform layer: input set-up, rumble and the window's message pump, with the PC port's SDL input and
// window (contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md).

#include "game/sys/platform.h"

#include <cstdint>
#include <cstring>

#include "game/gfx/settings.h"
#include "game/sio/sdl_input.h"
#include "game/sys/sdl_events.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Platform {

namespace {

// 0x005F3C10: fills the keyboard's key-name table (not rewritten yet).
void InitKeyNames() { GameFunction<void(__cdecl*)()>(0x005F3C10)(); }

} // namespace

// 0x005F3A50. PC port: rumble on the SDL game controller playing as `port` (the PC port jumped from here). The
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

// 0x005F56E0. PC port: the SDL event pump (which also sees the quit request; the PC port jumped from here). The
// original pumped the game window's (0x007D6A28) Win32 messages:
//     MSG msg;
//     while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
void ProcessWindowEvents()
{
    Platform::PumpSdlEvents();
}

// 0x005F44F0
bool InitInput(HINSTANCE instance, HWND window, int config)
{
    (void)instance; // (the original passes it to DirectInput8Create - the PC port's SDL input doesn't use it)
    (void)config;   // (the original's settings loader took it - the PC port's doesn't)
    if (!window)
        return false;
    auto& directInput = GameVariable<void*>(0x007D69D0);
    if (directInput)
        return true;
    // Per port: the joysticks (0x007D6318), their device state, the force-feedback effects (0x007D6668) and
    // the motors' ranges and kinds; the key bindings (-1: none); the graphics options (0x007D6434...).
    GameVariable<uint32_t>(0x007D6318) = 0;
    std::memset(reinterpret_cast<void*>(0x007D60F8), 0, 0x88 * 4);
    GameVariable<uint32_t>(0x007D631C) = 0;
    std::memset(reinterpret_cast<void*>(0x007D6448), 0, 0x88 * 4);
    GameVariable<uint32_t>(0x007D6668) = 0;
    GameVariable<uint32_t>(0x007D42D0) = 0;
    GameVariable<uint32_t>(0x007D666C) = 0;
    GameVariable<uint32_t>(0x007D42D4) = 0;
    GameVariable<uint32_t>(0x007D42D8) = 0;
    GameVariable<uint32_t>(0x007D42DC) = 0;
    std::memset(reinterpret_cast<void*>(0x007D6770), 0xFF, 0x98 * 4);
    GameVariable<uint32_t>(0x007D6434) = 0;
    GameVariable<uint32_t>(0x007D42C8) = 0;
    GameVariable<uint32_t>(0x007D6438) = 0;
    GameVariable<uint32_t>(0x007D42CC) = 0;
    GameVariable<uint32_t>(0x007D643C) = 0;
    GameVariable<uint32_t>(0x007D6440) = 0;
    GameVariable<uint32_t>(0x007D6444) = 0;
    // PC port: the graphics settings from thug2-decomp.ini in place of the original's loader 0x005F34D0(config).
    Nx::WriteGraphicsSettings();
    // PC port: SDL input (game controllers, the key bindings) in place of the original's
    //     DirectInput8Create(instance, 0x800, IID_IDirectInput8A, &directInput, nullptr);
    //     directInput->EnumDevices(0, 0x005F31D0 /* adds each joystick */, nullptr, DIEDFL_ATTACHEDONLY);
    Sio::InitSdlInput();
    InitKeyNames();
    return true;
}

} // namespace Platform

REPLACE(0x005F3A50, Platform::SetActuators);
REPLACE(0x005F56E0, Platform::ProcessWindowEvents);
REPLACE(0x005F44F0, Platform::InitInput);
