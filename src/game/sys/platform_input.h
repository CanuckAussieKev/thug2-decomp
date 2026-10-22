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
namespace Sio {
struct PadState;
} // namespace Sio

namespace Platform {

constexpr int kPorts = 2;
constexpr int kBindings = 24;

// A port's controls (0x130 bytes, 0x007D6770 + 0x130 * port; -1 = none). InitInput sets them all to -1.
struct PortControls {
    int32_t keys[kBindings];    // +0x00 the keyboard's key (DIK code) for each action: registry "k<port>_<n>"
    GUID joystick;              // +0x60 the joystick playing as this port: registry "pad<port>"
    int32_t gamepad[kBindings]; // +0x70 the joystick's control for each action: registry "gp<port>_<n>"
    int32_t mouse[kBindings];   // +0xD0 the mouse's control for each action (nothing here sets them)
};
static_assert(offsetof(PortControls, joystick) == 0x60 && offsetof(PortControls, gamepad) == 0x70 &&
                  offsetof(PortControls, mouse) == 0xD0 && sizeof(PortControls) == 0x130,
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
// 0x005F2FC0 (cdecl): whether the port has any mouse controls.
bool HasMouseBindings(int port);
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

// --- the pad read (0x005F4280-0x005F4450, 0x005F5BC0-0x005F6630) ---
// A port's controls map onto the PS2-style pad state (Sio::PadState): actions 0-7 the two sticks (left stick
// up, down, left, right; right stick up, down, left, right), 8-11 and 14-17 the pressure buttons, 12 / 13 / 18 / 19
// the buttons 0x10 / 0x20 / 0x40 / 0x80, 20-23 the d-pad's 1 / 2 / 4 / 8. A control is a key (DIK code), a mouse
// control (1-3 the axes X, Y, Z; 4-11 the buttons) or a joystick control (JoystickAxis's 1-10; 11-39 the buttons).

// 0x005F4280 (cdecl): a joystick control held: its button down, or its axis past +-200.
bool JoystickControlDown(const DIJOYSTATE2* state, int control);
// 0x005F42D0 (cdecl): a mouse control held: its button down, or its axis past +-3.
bool MouseControlDown(const DIMOUSESTATE2* state, int control);
// 0x005F4330 (cdecl): a mouse stick: -1 for the first control's button (or its axis), +1 added for the second's
// (or its axis instead); an axis counts as its movement x 6553.4, at most +-32767 / -32768. (The x87 value.)
double MouseStickValue(const DIMOUSESTATE2* state, int negative, int positive);
// 0x005F4450 (cdecl): a joystick stick the same way: an axis counts as its position / 1000.
double JoystickStickValue(const DIJOYSTATE2* state, int negative, int positive);

// The keyboard's part of the pad read (SetPadMode): 0 the mouse, the joystick and the port's keys; 1 the default
// keys for port 0, the port's keys for the others; 2 no keys; 3 the default keys. The mouse only counts in mode 0.
enum PadMode { kPadKeyBindings = 0, kPadDefaultKeysPort0 = 1, kPadNoKeys = 2, kPadDefaultKeys = 3 };

// 0x005F5BC0 (cdecl): the pad state for `port` (1 or 2) from the keyboard, the joystick and the mouse (each read
// for this frame first): nothing held for a port without a joystick, keys or mouse controls. Returns 0.
int ReadPad(int port, Sio::PadState* state);
// 0x005F6610 / 0x005F6620 / 0x005F6630 (cdecl): the pad mode; what Enter and Escape are in the default keys (a
// pressure button 0-7, or the button 0x10 / 0x20).
void SetPadMode(int mode);
void SetDefaultEnterControl(int control);
void SetDefaultEscapeControl(int control);

} // namespace Platform
