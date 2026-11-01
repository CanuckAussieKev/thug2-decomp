#pragma once

#include "engine/core/raw_view.h"

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a library's configuration / the compiler-specific macro
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstdint>

// The Win32 platform layer's DirectInput side (0x005F2E00-0x005F4280): the two ports' joysticks, the keyboard and
// the mouse - set up by init_input (platform.h), read once a frame by the pad read - the controls each port has
// (from the registry), and the keyboard's key names. PartyMod's SDL input (engine/pad_input/sdl_input.h) replaces most of
// it in the game: init_input no longer makes the DirectInput devices, so the reads find none.
namespace sio {
struct PadSnapshot;
} // namespace sio

namespace os_layer {

constexpr int kPorts = 2;
constexpr int kBindings = 24;

// A port's controls (0x130 bytes, 0x007D6770 + 0x130 * port; -1 = none). init_input sets them all to -1.
struct PortControls {
    int32_t keys[kBindings];    // +0x00 the keyboard's key (DIK code) for each action: registry "k<port>_<n>"
    GUID joystick;              // +0x60 the joystick playing as this port: registry "pad<port>"
    int32_t gamepad[kBindings]; // +0x70 the joystick's control for each action: registry "gp<port>_<n>"
    int32_t mouse[kBindings];   // +0xD0 the mouse's control for each action (nothing here sets them)
};
static_assert(offsetof(PortControls, joystick) == 0x60 && offsetof(PortControls, gamepad) == 0x70 &&
                  offsetof(PortControls, mouse) == 0xD0 && sizeof(PortControls) == 0x130,
              "layout must match the original");
extern int32_t g_input_map[0x98];
inline PortControls* get_controls() { return raw::as<PortControls>(g_input_map); }

// The graphics options (0x007D6434; the registry's "options", "distance" and "resolution" - PartyMod's
// [Graphics] in thug2-decomp.ini, engine/gfx/settings.h).
struct Options {
    uint8_t antialiasing;       // 0x007D6434 options & 0x20
    uint8_t hq_shadows_;        // 0x007D6435 options & 2
    uint8_t fog;                // 0x007D6436 options & 1
    uint8_t option37;           // 0x007D6437 options & 0x10
    uint8_t option38;           // 0x007D6438 options & 4
    uint8_t option39;           // 0x007D6439 options & 8
    uint8_t distance_clipping_; // 0x007D643A options & 0x100
    uint8_t pad3b_;
    uint32_t resolution;        // 0x007D643C 0-3
    uint32_t distance;          // 0x007D6440 1-100 (0: none)
    int32_t clipping_distance_; // 0x007D6444 distance * 5 + 95
};
static_assert(sizeof(Options) == 0x14, "layout must match the original");

// 0x005F2E00 (cdecl): the port's joystick read for this frame (polled - re-acquired once if that fails - its
// last state kept): its state, or null without a joystick or when a step fails.
DIJOYSTATE2* read_joystick(int port);
// 0x005F2E90 (cdecl): the mouse read for this frame, or null. A frame without movement keeps some of the last
// one's: x and y move 2 back toward 0 (left at 0 within 2).
DIMOUSESTATE2* read_mouse();
// 0x005F2F80 (cdecl): whether the port has any keyboard controls.
bool has_key_bindings(int port);
// 0x005F2FC0 (cdecl): whether the port has any mouse controls.
bool has_mouse_bindings(int port);
// 0x005F3000 (cdecl): a joystick control's position: 1-3 the axes X, Y, Z; 4 / 5 the hat across / down (1000
// right / down, -1000 left / up, 0 centred or along the other); 6-8 the axes Rx, Ry, Rz; 9 / 10 the sliders; else 0.
int joystick_axis(const DIJOYSTATE2* state, int control);
// 0x005F30E0 (cdecl): whether the key (a DIK code) is down - Escape (1) only in the frame it went down; -1: no.
bool get_key_down(int code);
// 0x005F3120 (cdecl): the key's name (15 wide characters per key, 0x007D42F8).
wchar_t* get_key_name(int code);
// 0x005F3130 (stdcall, DirectInput's EnumObjects callback): each axis of a joystick (`device`) gets the range
// -1000..1000; a force-feedback one is counted (at most two) and its force resolution kept. Continues.
BOOL CALLBACK add_joystick_axis(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID device);
// 0x005F31D0 (stdcall, DirectInput's EnumDevices callback): a joystick or gamepad whose GUID a port names
// becomes that port's (its axes set up, a constant-force effect when it has force feedback); the first
// keyboard and mouse found are the keyboard and mouse. Continues.
BOOL CALLBACK add_input_device(LPCDIDEVICEINSTANCEA device, LPVOID context);
// 0x005F3400 (cdecl): the keyboard's state for this frame (the last frame's kept); lost: re-acquired and read
// again, or all up when that fails. There must be a keyboard.
void read_keyboard();
// 0x005F3470 (cdecl): the hex number at `*cursor` (digits and upper-case A-F), the cursor moved past it.
uint32_t parse_hex(const char** cursor);
// 0x005F34D0 (cdecl): the options and controls from HKEY_CURRENT_USER\<key> (PartyMod: thug2-decomp.ini).
void load_registry_options(const char* key);
// 0x005F39E0 (cdecl): the joysticks, their effects, the keyboard and DirectInput released (not the mouse).
void release_input();
// 0x005F3BF0 (cdecl): the key's name set (at most 14 characters). Returns the table's entry.
wchar_t* set_key_name(int code, const wchar_t* name);
// 0x005F3C10 (cdecl): every key's name: "(NONE)", then the keyboard's.
void init_key_names();

// --- the pad read (0x005F4280-0x005F4450, 0x005F5BC0-0x005F6630) ---
// A port's controls map onto the PS2-style pad state (sio::PadSnapshot): actions 0-7 the two sticks (left stick
// up, down, left, right; right stick up, down, left, right), 8-11 and 14-17 the pressure buttons, 12 / 13 / 18 / 19
// the buttons 0x10 / 0x20 / 0x40 / 0x80, 20-23 the d-pad's 1 / 2 / 4 / 8. A control is a key (DIK code), a mouse
// control (1-3 the axes X, Y, Z; 4-11 the buttons) or a joystick control (joystick_axis's 1-10; 11-39 the buttons).

// 0x005F4280 (cdecl): a joystick control held: its button down, or its axis past +-200.
bool joystick_control_down(const DIJOYSTATE2* state, int control);
// 0x005F42D0 (cdecl): a mouse control held: its button down, or its axis past +-3.
bool mouse_control_down(const DIMOUSESTATE2* state, int control);
// 0x005F4330 (cdecl): a mouse stick: -1 for the first control's button (or its axis), +1 added for the second's
// (or its axis instead); an axis counts as its movement x 6553.4, at most +-32767 / -32768. (The x87 value.)
double mouse_stick_value(const DIMOUSESTATE2* state, int negative, int positive);
// 0x005F4450 (cdecl): a joystick stick the same way: an axis counts as its position / 1000.
double joystick_stick_value(const DIJOYSTATE2* state, int negative, int positive);

// The keyboard's part of the pad read (set_pad_mode): 0 the mouse, the joystick and the port's keys; 1 the default
// keys for port 0, the port's keys for the others; 2 no keys; 3 the default keys. The mouse only counts in mode 0.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum PadMode : uint8_t { kPadKeyBindings = 0, kPadDefaultKeysPort0 = 1, kPadNoKeys = 2, kPadDefaultKeys = 3 };

// 0x005F5BC0 (cdecl): the pad state for `port` (1 or 2) from the keyboard, the joystick and the mouse (each read
// for this frame first): nothing held for a port without a joystick, keys or mouse controls. Returns 0.
int read_pad(int port, sio::PadSnapshot* state);
// 0x005F6610 / 0x005F6620 / 0x005F6630 (cdecl): the pad mode; what Enter and Escape are in the default keys (a
// pressure button 0-7, or the button 0x10 / 0x20).
void set_pad_mode(int mode);
void set_default_enter_control(int control);
void set_default_escape_control(int control);

extern IDirectInput8A* g_direct_input;
extern IDirectInputDevice8A* g_keyboard_device;
extern int32_t g_input_map[0x98];
extern DIJOYSTATE2 g_joystick_states[2];
extern DIJOYSTATE2 g_last_joystick_states[2];
extern IDirectInputDevice8A* g_joysticks[kPorts];
extern IDirectInputEffect* g_effects[kPorts];
extern uint32_t g_force_axis_counts[kPorts];
extern uint32_t g_force_resolutions[2 * kPorts];
extern uint8_t g_option37; // the graphics options' other bytes (the named ones are in gfx/settings.h)
extern uint16_t g_options38_and39;
extern uint8_t g_unknown643b;
extern uint32_t g_distance_setting;

} // namespace os_layer
