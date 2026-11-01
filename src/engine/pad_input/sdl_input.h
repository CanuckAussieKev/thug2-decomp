#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstdint>

// Input on PC: SDL game controllers and the keyboard in place of the original's DirectInput. The bindings are
// in thug2-decomp.ini ([Keybinds]: SDL scan codes; [Gamepad]: SDL game controller buttons, 21 / 22 the right / left
// trigger, -1 none; sticks 0 left, 1 right); [Miscellaneous] UsePS2Controls (on by default) gives the shoulder
// buttons the PS2's layout (L1/R1 spin, L2/R2 nollie/switch) and the skater code its PS2 behaviour.
// gamecontrollerdb.txt next to the exe adds controller mappings.
//
// Players: a controller becomes player 1 or 2 when one of its buttons is pressed (the second rumbles once and is
// locked out until all its buttons are up). Port 0 also reads the keyboard. Each frame sio::InputPort::work_on
// fills the device's PS2-format control data from them.
namespace sio {

class InputPort;

struct InputSettings {
    uint8_t ps2_controls_; // UsePS2Controls
    uint8_t ui_controls_;  // UIControls (read, unused)
};
// The settings (read by init_sdl_input).
InputSettings& the_input_settings();

// Whether the keyboard was used last (a key went down; a controller button or stick moves it to the
// controller). Button glyphs depend on it.
bool& using_keyboard();

// Set input up (from os_layer::init_input): SDL's game controllers, the settings and bindings, the game's key
// binding table (0x007D6770, DirectInput codes, for the menus' hints), the event handler; with PS2 controls,
// the IsPS2 script command (sys::install_is_ps2).
void init_sdl_input();

// A device's frame (sio::InputPort::work_on): pending events handled, then the control data from the keyboard
// (port 0) and the port's controller.
void process_sdl_device(InputPort* device);

// Rumble on the controller playing as `port` (os_layer::set_actuators): `high` and `low` frequency motors,
// 0-65535.
void set_sdl_actuators(int port, uint16_t high, uint16_t low);

extern bool g_keyboard_on_screen;
extern bool g_in_menu_flag;

} // namespace sio
