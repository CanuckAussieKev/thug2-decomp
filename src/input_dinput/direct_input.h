#pragma once

#include <windows.h>
#include <dinput.h>

#include "engine/pad_input/port_controls.h"

// The original's Win32 platform layer's DirectInput side (0x005F2E00-0x005F4280): the two ports' joysticks, the
// keyboard and the mouse, read with the controls each port has (engine/pad_input/port_controls.h) - once a frame by
// the pad read. PartyMod's SDL input (engine/pad_input/sdl_input.h) replaces most of it in the game: init_input no
// longer makes the DirectInput devices, so the reads find none. Only input_dinput/ sees <dinput.h>; the engine reaches
// it through its input source (input_dinput/dinput_source.h, engine/pad_input/input_source.h).
namespace sio {
struct PadSnapshot;
} // namespace sio

namespace os_layer {

// 0x005F2E00 (cdecl): the port's joystick read for this frame (polled - re-acquired once if that fails - its
// last state kept): its state, or null without a joystick or when a step fails.
DIJOYSTATE2* read_joystick(int port);
// 0x005F2E90 (cdecl): the mouse read for this frame, or null. A frame without movement keeps some of the last
// one's: x and y move 2 back toward 0 (left at 0 within 2).
DIMOUSESTATE2* read_mouse();
// 0x005F3000 (cdecl): a joystick control's position: 1-3 the axes X, Y, Z; 4 / 5 the hat across / down (1000
// right / down, -1000 left / up, 0 centred or along the other); 6-8 the axes Rx, Ry, Rz; 9 / 10 the sliders; else 0.
int joystick_axis(const DIJOYSTATE2* state, int control);
// 0x005F30E0 (cdecl): whether the key (a DIK code) is down - Escape (1) only in the frame it went down; -1: no.
bool get_key_down(int code);
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
// 0x005F39E0 (cdecl): the joysticks, their effects, the keyboard and DirectInput released (not the mouse).
void release_input();

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
constexpr int kPadKeyBindings = 0;
constexpr int kPadDefaultKeysPort0 = 1;
constexpr int kPadNoKeys = 2;
constexpr int kPadDefaultKeys = 3;

// 0x005F5BC0 (cdecl): the pad state for `port` (1 or 2) from the keyboard, the joystick and the mouse (each read
// for this frame first): nothing held for a port without a joystick, keys or mouse controls. Returns 0.
int read_pad(int port, sio::PadSnapshot* pad);
// 0x005F6610 / 0x005F6620 / 0x005F6630 (cdecl): the pad mode; what Enter and Escape are in the default keys (a
// pressure button 0-7, or the button 0x10 / 0x20).
void set_pad_mode(int mode);
void set_default_enter_control(int control);
void set_default_escape_control(int control);

IDirectInputDevice8A* keyboard_device();
void set_keyboard_device(IDirectInputDevice8A* value);

} // namespace os_layer
