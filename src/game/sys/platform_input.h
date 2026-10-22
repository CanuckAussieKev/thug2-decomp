#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstdint>

// The Win32 platform layer's DirectInput side (0x005F2E00-0x005F4280): the two ports' joysticks, the keyboard and
// the mouse - set up by InitInput (platform.h), read once a frame by the pad read - the controls each port has
// (from the registry), and the keyboard's key names. PartyMod's SDL input (game/sio/sdl_input.h) replaces most of
// it in the game: InitInput no longer makes the DirectInput devices, so the reads find none.
namespace Platform {

constexpr int kPorts = 2;
constexpr int kBindings = 24;

// A port's controls (0x130 bytes, 0x007D6770 + 0x130 * port; -1 = none). InitInput sets them all to -1.
struct PortControls {
    int32_t keys[kBindings];    // +0x00 the keyboard's key (DIK code) for each action: registry "k<port>_<n>"
    GUID joystick;              // +0x60 the joystick playing as this port: registry "pad<port>"
    int32_t gamepad[kBindings]; // +0x70 the joystick's control for each action: registry "gp<port>_<n>"
    int32_t padD0[kBindings];   // +0xD0 read by the pad read (0x005F5BC0); nothing here sets them
};
static_assert(offsetof(PortControls, joystick) == 0x60 && offsetof(PortControls, gamepad) == 0x70 &&
                  offsetof(PortControls, padD0) == 0xD0 && sizeof(PortControls) == 0x130,
              "layout must match the original");
inline PortControls* Controls() { return reinterpret_cast<PortControls*>(0x007D6770); }

// The graphics options (0x007D6434; the registry's "options", "distance" and "resolution" - PartyMod's
// [Graphics] in thug2-decomp.ini, game/gfx/settings.h).
struct Options {
    uint8_t antialiasing;     // 0x007D6434 options & 0x20
    uint8_t hqShadows;        // 0x007D6435 options & 2
    uint8_t fog;              // 0x007D6436 options & 1
    uint8_t option37;         // 0x007D6437 options & 0x10
    uint8_t option38;         // 0x007D6438 options & 4
    uint8_t option39;         // 0x007D6439 options & 8
    uint8_t distanceClipping; // 0x007D643A options & 0x100
    uint8_t pad3B;
    uint32_t resolution;      // 0x007D643C 0-3
    uint32_t distance;        // 0x007D6440 1-100 (0: none)
    int32_t clippingDistance; // 0x007D6444 distance * 5 + 95
};
static_assert(sizeof(Options) == 0x14, "layout must match the original");
inline Options& GraphicsOptions() { return *reinterpret_cast<Options*>(0x007D6434); }

// 0x005F2E00 (cdecl): the port's joystick read for this frame (polled - re-acquired once if that fails - its
// last state kept): its state, or null without a joystick or when a step fails.
DIJOYSTATE2* ReadJoystick(int port);
// 0x005F2E90 (cdecl): the mouse read for this frame, or null. A frame without movement keeps some of the last
// one's: x and y move 2 back toward 0 (left at 0 within 2).
DIMOUSESTATE2* ReadMouse();
// 0x005F2F80 (cdecl): whether the port has any keyboard controls.
bool HasKeyBindings(int port);
// 0x005F2FC0 (cdecl): whether the port has any of its +0xD0 controls.
bool HasPadD0Bindings(int port);
// 0x005F3000 (cdecl): a joystick control's position: 1-3 the axes X, Y, Z; 4 / 5 the hat across / down (1000
// right / down, -1000 left / up, 0 centred or along the other); 6-8 the axes Rx, Ry, Rz; 9 / 10 the sliders; else 0.
int JoystickAxis(const DIJOYSTATE2* state, int control);
// 0x005F30E0 (cdecl): whether the key (a DIK code) is down - Escape (1) only in the frame it went down; -1: no.
bool KeyDown(int code);
// 0x005F3120 (cdecl): the key's name (15 wide characters per key, 0x007D42F8).
wchar_t* KeyName(int code);
// 0x005F3130 (stdcall, DirectInput's EnumObjects callback): each axis of a joystick (`device`) gets the range
// -1000..1000; a force-feedback one is counted (at most two) and its force resolution kept. Continues.
BOOL CALLBACK AddJoystickAxis(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID device);
// 0x005F31D0 (stdcall, DirectInput's EnumDevices callback): a joystick or gamepad whose GUID a port names
// becomes that port's (its axes set up, a constant-force effect when it has force feedback); the first
// keyboard and mouse found are the keyboard and mouse. Continues.
BOOL CALLBACK AddInputDevice(LPCDIDEVICEINSTANCEA device, LPVOID context);
// 0x005F3400 (cdecl): the keyboard's state for this frame (the last frame's kept); lost: re-acquired and read
// again, or all up when that fails. There must be a keyboard.
void ReadKeyboard();
// 0x005F3470 (cdecl): the hex number at `*cursor` (digits and upper-case A-F), the cursor moved past it.
uint32_t ParseHex(const char** cursor);
// 0x005F34D0 (cdecl): the options and controls from HKEY_CURRENT_USER\<key> (PartyMod: thug2-decomp.ini).
void LoadRegistryOptions(const char* key);
// 0x005F39E0 (cdecl): the joysticks, their effects, the keyboard and DirectInput released (not the mouse).
void ReleaseInput();
// 0x005F3BF0 (cdecl): the key's name set (at most 14 characters). Returns the table's entry.
wchar_t* SetKeyName(int code, const wchar_t* name);
// 0x005F3C10 (cdecl): every key's name: "(NONE)", then the keyboard's.
void InitKeyNames();

} // namespace Platform
