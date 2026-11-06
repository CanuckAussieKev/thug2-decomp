#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstdint>

// Input on PC: SDL game controllers and the keyboard in place of the original's DirectInput. The bindings are
// in thug2-decomp.ini ([Keybinds]: SDL scan codes; [Gamepad]: SDL game controller buttons, 21 / 22 the right / left
// trigger, 23-27 SDL 3's MISC2-MISC6, -1 none; sticks 0 left, 1 right); [Miscellaneous] UsePS2Controls (on by default) gives the shoulder
// buttons the PS2's layout (L1/R1 spin, L2/R2 nollie/switch) and the skater code its PS2 behaviour.
// gamecontrollerdb.txt next to the exe adds controller mappings.
//
// Players: a controller becomes player 1 or 2 when one of its buttons is pressed (the second rumbles once and is
// locked out until all its buttons are up). Not original ([features] single_player_any_pad, on by default): outside a
// split-screen game player 1's port reads the controller a button was pressed on last instead of its seat's, and the
// other port none; [features] cursor_by_device: a controller button hides the cursor and stops the keyboard's rumble
// mute, a key or the mouse shows it again and mutes player 1's rumble. Port 0 also reads the keyboard. Each frame sio::InputPort::work_on
// fills the device's PS2-format control data from them.
namespace input {
class Source;
} // namespace input

namespace sio {

struct InputSettings {
    uint8_t ps2_controls_; // UsePS2Controls
    uint8_t ui_controls_;  // UIControls (read, unused)
};
// The settings (read when the source starts).
InputSettings& the_input_settings();

// Whether the keyboard was used last (a key went down; a controller button or stick moves it to the
// controller). Button glyphs depend on it.
bool& using_keyboard();

// The SDL input source (input::Source, engine/pad_input/input_source.h; app/ adds it): started from
// os_layer::init_input - SDL's game controllers, the settings and bindings, the game's key binding table (0x007D6770,
// DirectInput codes, for the menus' hints), the event handler; with PS2 controls, the IsPS2 script command
// (sys::install_is_ps2) -; each port's poll (sio::InputPort::work_on) first handles the pending events, then fills the
// port's control data from the keyboard (port 0) and the port's controller; rumble on the controller playing as a port
// (os_layer::set_actuators).
input::Source& sdl_source();

// NOT ORIGINAL - whether the on-screen keyboard is up and whether a menu, dialog box or the quit dialog is on screen
// (the front end sets them; the input reads them).
bool& keyboard_on_screen_flag();
bool& menu_flag();
// Whether text entry is on (0x007CCDF8): typed keys are taken outside a menu, and the main loop keeps running while the
// window is inactive. The online game sets it when a network game is entered and clears it when the session's players
// are let go; PartyMod's input sets it every poll (typed text only works with it).
bool& text_entry_flag();

} // namespace sio
