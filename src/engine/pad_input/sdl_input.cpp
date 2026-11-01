// Input on PC: SDL game controllers and the keyboard.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/pad_input/sdl_input.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <SDL2/SDL.h>

#include "frontend/automation.h"
#include "editor/level_editor.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "engine/pad_input/device.h"
#include "game/rider/thaw_bmx.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "engine/sys/debug_mode.h"
#include "platform/platform_cfuncs.h"
#include "platform/screenshot.h"
#include "platform/sdl_events.h"
#include "platform/platform_input.h"

namespace sio {

bool g_keyboard_on_screen;
bool g_in_menu_flag;

namespace {

using debug_log::StageMap;

// --- settings -----------------------------------------------------------------------------------------------

struct KeyBinds {
    SDL_Scancode menu;
    SDL_Scancode camera_toggle_;
    SDL_Scancode camera_swivel_lock_;
    SDL_Scancode focus;
    SDL_Scancode caveman;

    SDL_Scancode grind;
    SDL_Scancode grab;
    SDL_Scancode ollie;
    SDL_Scancode kick;

    SDL_Scancode left_spin_;
    SDL_Scancode right_spin_;
    SDL_Scancode nollie;
    SDL_Scancode switch_revert_;

    SDL_Scancode right;
    SDL_Scancode left;
    SDL_Scancode up;
    SDL_Scancode down;
    SDL_Scancode feeble;

    SDL_Scancode camera_right_;
    SDL_Scancode camera_left_;
    SDL_Scancode camera_up_;
    SDL_Scancode camera_down_;

    SDL_Scancode item_right_;
    SDL_Scancode item_left_;
    SDL_Scancode item_up_;
    SDL_Scancode item_down_;
};

// SDL_GameControllerButton, plus the two triggers.
enum PadButton : int {
    kUnbound = -1,
    kButtonA = SDL_CONTROLLER_BUTTON_A,
    kButtonB = SDL_CONTROLLER_BUTTON_B,
    kButtonX = SDL_CONTROLLER_BUTTON_X,
    kButtonY = SDL_CONTROLLER_BUTTON_Y,
    kButtonBack = SDL_CONTROLLER_BUTTON_BACK,
    kButtonStart = SDL_CONTROLLER_BUTTON_START,
    kButtonLeftStick = SDL_CONTROLLER_BUTTON_LEFTSTICK,
    kButtonRightStick = SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    kButtonLeftShoulder = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    kButtonRightShoulder = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    kDpadUp = SDL_CONTROLLER_BUTTON_DPAD_UP,
    kDpadDown = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    kDpadLeft = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    kDpadRight = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    kRightTrigger = 21,
    kLeftTrigger = 22,
};

enum PadStick : int { kStickUnbound = -1, kStickLeft = 0, kStickRight = 1 };

struct PadBinds {
    PadButton menu;
    PadButton camera_toggle_;
    PadButton camera_swivel_lock_;
    PadButton focus;
    PadButton caveman;

    PadButton grind;
    PadButton grab;
    PadButton ollie;
    PadButton kick;

    PadButton left_spin_;
    PadButton right_spin_;
    PadButton nollie;
    PadButton switch_revert_;

    PadButton right;
    PadButton left;
    PadButton up;
    PadButton down;

    PadStick movement;
    PadStick camera;
};

InputSettings s_settings;
KeyBinds s_keys;
PadBinds s_pad;
bool s_using_keyboard = true;

// --- the game's state ---------------------------------------------------------------------------------------

// The on-screen keyboard (text entry) is up.
bool get_keyboard_on_screen() { return g_keyboard_on_screen != 0; }
uint8_t in_menu_flag() { return g_in_menu_flag; }

// --- controllers and players ----------------------------------------------------------------------------------

// The open controllers (removed ones shift the rest down).
std::vector<SDL_GameController*> s_controllers;

struct PlayerSlot {
    SDL_GameController* controller;
    uint8_t locked_out_; // after joining: locked out until all of its buttons are up (counts buttons down)
};
constexpr int kMaxPlayers = 2;
uint8_t s_seats_in_use = 0;
PlayerSlot s_players[kMaxPlayers] = {{nullptr, 0}, {nullptr, 0}};

void attach_controller(int index)
{
    debug_log::printf(StageMap::kInfo, "Detected controller \"%s\"\n", SDL_GameControllerNameForIndex(index));
    SDL_GameController* controller = SDL_GameControllerOpen(index);
    SDL_GameControllerSetPlayerIndex(controller, -1);
    if (!controller)
        return;
    s_controllers.push_back(controller);
}

void enroll_player(SDL_GameController* controller)
{
    if (s_seats_in_use >= 2) {
        debug_log::printf(StageMap::kInfo, "Already two players, not adding\n");
        return;
    }
    int slot = 0;
    while (slot < kMaxPlayers && s_players[slot].controller)
        ++slot;
    if (slot == kMaxPlayers)
        return;
    SDL_GameControllerSetPlayerIndex(controller, slot);
    s_players[slot].controller = controller;
    if (s_seats_in_use > 0) {
        s_players[slot].locked_out_ = 1;
        SDL_JoystickRumble(SDL_GameControllerGetJoystick(controller), 0x7FFF, 0x7FFF, 250);
    }
    ++s_seats_in_use;
    debug_log::printf(StageMap::kInfo, "Added player %d: %s\n", slot + 1, SDL_GameControllerName(controller));
}

void prune_players()
{
    for (int i = 0; i < kMaxPlayers; ++i) {
        if (s_players[i].controller && !SDL_GameControllerGetAttached(s_players[i].controller)) {
            debug_log::printf(StageMap::kInfo, "Pruned player %d\n", i + 1);
            s_players[i].controller = nullptr;
            --s_seats_in_use;
            debug_log::printf(StageMap::kInfo, "Remaining players: %d\n", s_seats_in_use);
        }
    }
}

void detach_anim_driver(SDL_GameController* controller)
{
    debug_log::printf(StageMap::kInfo, "Controller \"%s\" disconnected\n", SDL_GameControllerName(controller));
    auto found = std::find(s_controllers.begin(), s_controllers.end(), controller);
    // (PartyMod looked one past the last entry when it wasn't found - past the end of its array when that was full)
    if (found == s_controllers.end()) {
        debug_log::printf(StageMap::kWarn, "Did not find disconnected controller in list\n");
        return;
    }
    // (the player index read before the close - PartyMod read it from the closed, freed controller)
    int player = SDL_GameControllerGetPlayerIndex(controller);
    SDL_GameControllerClose(controller);
    if (player != -1) {
        debug_log::printf(StageMap::kInfo, "Removed player %d\n", player + 1);
        s_players[player].controller = nullptr;
        --s_seats_in_use;
    }
    prune_players();
    s_controllers.erase(found);
}

void init_controllers()
{
    debug_log::printf(StageMap::kInfo, "Initializing Controller Input\n");
    s_controllers.clear();
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i))
            attach_controller(i);
    }
}

// --- reading a controller -------------------------------------------------------------------------------------

// How far a stick axis (0-255, 128 the centre) is from the centre.
uint8_t stick_deflection(uint8_t value)
{
    if (value > 127)
        return value & 0x7F;
    return ~value & 0x7F;
}

uint8_t is_controller_button_held(SDL_GameController* controller, PadButton button)
{
    if (button == kLeftTrigger) {
        auto pressure =
            static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        return pressure > 0x80;
    }
    if (button == kRightTrigger) {
        auto pressure =
            static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
        return pressure > 0x80;
    }
    return SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button));
}

// A stick into (x, y), where it's further from the centre than what's there (the keyboard's).
void read_stick(SDL_GameController* controller, PadStick stick, uint8_t* x, uint8_t* y)
{
    uint8_t pad_axis_x;
    uint8_t stick_y;
    if (stick == kStickLeft) {
        pad_axis_x =
            static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) >> 8) + 128);
        stick_y = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) >> 8) + 128);
    } else if (stick == kStickRight) {
        pad_axis_x =
            static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) >> 8) + 128);
        stick_y = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) >> 8) + 128);
    } else {
        pad_axis_x = 0x80;
        stick_y = 0x80;
    }
    if (stick_deflection(pad_axis_x) > stick_deflection(*x))
        *x = pad_axis_x;
    if (stick_deflection(stick_y) > stick_deflection(*y))
        *y = stick_y;
}

// Whether the park editor is up: the screen element 0x5999551E exists (the screen element manager 0x00701440's
// fetch_part 0x004AAE20 - it returns a smart pointer, released by its destructor).
uint8_t in_park_editor()
{
    frontend::UiTree* manager = frontend::UiTree::sole();
    if (!manager)
        return 0;
    frontend::ElementPtr element;
    return manager->fetch_part(&element, 0x5999551E, 0)->get_pull() ? 1 : 0;
}

uint8_t s_in_menu = 0;
uint8_t s_editing_park = 0;

// Not original (PartyMod reads a Caveman binding and never uses it) - the Caveman key / button steps off the board
// and back on, pressing what the game's SwitchControl_Trigger (game\rider\switch_control.qb) waits for in the
// trigger set it reads (Skater::Component::platform_structure): with UsePS2Controls the PS2 one, L1 and R1 together
// (PressTwoAnyOrder, 400 ms); without, the Xbox one, black (the original PC game's Key Pad 1 / spin left key). Not in a
// menu or while editing a park, where those mean something else.
// Original behaviour (PartyMod v1.0.4): the binding is read, nothing presses it.
void press_caveman(uint8_t* data)
{
    if (s_in_menu || s_editing_park)
        return;
    if (!s_settings.ps2_controls_) {
        data[20] |= 1 << 0; // black
        return;
    }
    data[3] |= (1 << 2) | (1 << 3); // L1, R1
    data[16] = 0xFF;
    data[17] = 0xFF;
}

// The control data's bytes: 2 and 3 the button bits (set while down here; inverted at the end - the game's
// format has a bit set while a button is up), 4-7 the sticks, 8-19 the pressures, 20 the Xbox's black (bit 0)
// and white (bit 1).
void poll_controller(InputPort* device, SDL_GameController* controller)
{
    if (!SDL_GameControllerGetAttached(controller))
        return;
    uint8_t* data = device->control_data_;
    device->is_usable_ = 1;
    device->is_connected_ = true;

    if (is_controller_button_held(controller, s_pad.menu))
        data[2] |= 1 << 3;
    if (is_controller_button_held(controller, s_pad.camera_toggle_))
        data[2] |= 1 << 0;
    if (is_controller_button_held(controller, s_pad.camera_swivel_lock_))
        data[2] |= 1 << 2;
    if (is_controller_button_held(controller, s_pad.focus))
        data[2] |= 1 << 1;

    if (is_controller_button_held(controller, s_pad.grind)) {
        data[3] |= 1 << 4;
        data[12] = 0xFF;
    }
    if (is_controller_button_held(controller, s_pad.grab)) {
        data[3] |= 1 << 5;
        data[13] = 0xFF;
    }
    if (is_controller_button_held(controller, s_pad.ollie)) {
        data[3] |= 1 << 6;
        data[14] = 0xFF;
    }
    if (is_controller_button_held(controller, s_pad.kick)) {
        data[3] |= 1 << 7;
        data[15] = 0xFF;
    }

    // the shoulders
    if (s_settings.ps2_controls_) {
        if (is_controller_button_held(controller, s_pad.left_spin_)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (is_controller_button_held(controller, s_pad.right_spin_)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            if (s_editing_park)
                data[20] |= 1 << 0; // black, editing a park
        }
        if (is_controller_button_held(controller, s_pad.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
            if (s_in_menu)
                data[20] |= 1 << 1; // white, in a menu
        }
        if (is_controller_button_held(controller, s_pad.switch_revert_)) {
            data[3] |= 1 << 1;
            data[19] = 0xFF;
            if (s_in_menu)
                data[20] |= 1 << 0; // black, in a menu
            else if (s_editing_park)
                data[20] |= 1 << 1; // white, editing a park
        }
    } else if (s_editing_park && !get_keyboard_on_screen()) {
        // editing a park: the right trigger is L1
        if (is_controller_button_held(controller, s_pad.switch_revert_)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (is_controller_button_held(controller, s_pad.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (is_controller_button_held(controller, s_pad.left_spin_))
            data[20] |= 1 << 1;
        if (is_controller_button_held(controller, s_pad.right_spin_))
            data[20] |= 1 << 0;
    } else {
        if (is_controller_button_held(controller, s_pad.nollie)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (is_controller_button_held(controller, s_pad.switch_revert_)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            data[3] |= 1 << 1;
            data[19] = 0xFF;
        }
        if (is_controller_button_held(controller, s_pad.left_spin_))
            data[20] |= 1 << 1;
        if (is_controller_button_held(controller, s_pad.right_spin_))
            data[20] |= 1 << 0;
    }
    if (is_controller_button_held(controller, s_pad.caveman))
        press_caveman(data);

    // the d-pad (plain buttons: not the triggers)
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(s_pad.up))) {
        data[2] |= 1 << 4;
        data[10] = 0xFF;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(s_pad.right))) {
        data[2] |= 1 << 5;
        data[8] = 0xFF;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(s_pad.down))) {
        data[2] |= 1 << 6;
        data[11] = 0xFF;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(s_pad.left))) {
        data[2] |= 1 << 7;
        data[9] = 0xFF;
    }

    read_stick(controller, s_pad.camera, &data[4], &data[5]);
    read_stick(controller, s_pad.movement, &data[6], &data[7]);
}

// --- the keyboard -----------------------------------------------------------------------------------------------

// The menu keys are fixed (Enter, Escape, E, R, the arrows, 1, 2, -); in a menu they stand for the buttons, and
// outside one their bindings work as keys. A menu key still down when the menu (or the on-screen keyboard) comes
// or goes is locked out until it's released, unless it's also bound to the same thing.
constexpr uint8_t kLockedOut = 2;
struct MenuKeys {
    uint8_t affirmative; // A / X
    uint8_t negative;    // B / circle
    uint8_t menu;        // X / square
    uint8_t extra;       // Y / triangle
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
    uint8_t rotate_left_;  // LB / L1
    uint8_t rotate_right_; // RB / R1
    uint8_t zoom;          // black (R2 too, rerouted to black)
};
MenuKeys s_menu_keys;

uint8_t s_was_in_menu = 0;
uint8_t s_was_keyboard_on_screen = 0;

void update_menu_keys(const uint8_t* keys)
{
    uint8_t keyboard_on_screen = g_keyboard_on_screen;
    if (s_in_menu != s_was_in_menu || keyboard_on_screen != s_was_keyboard_on_screen) {
        if (s_keys.ollie != SDL_SCANCODE_RETURN && s_menu_keys.affirmative == 1)
            s_menu_keys.affirmative = kLockedOut;
        if (s_keys.grab != SDL_SCANCODE_ESCAPE && s_menu_keys.negative == 1)
            s_menu_keys.negative = kLockedOut;
        if (s_keys.kick != SDL_SCANCODE_E && s_menu_keys.menu == 1)
            s_menu_keys.menu = kLockedOut;
        if (s_keys.grind != SDL_SCANCODE_R && s_menu_keys.extra == 1)
            s_menu_keys.extra = kLockedOut;
        if (s_keys.up != SDL_SCANCODE_UP && s_menu_keys.up == 1)
            s_menu_keys.up = kLockedOut;
        if (s_keys.down != SDL_SCANCODE_DOWN && s_menu_keys.down == 1)
            s_menu_keys.down = kLockedOut;
        if (s_keys.left != SDL_SCANCODE_LEFT && s_menu_keys.left == 1)
            s_menu_keys.left = kLockedOut;
        if (s_keys.right != SDL_SCANCODE_RIGHT && s_menu_keys.right == 1)
            s_menu_keys.right = kLockedOut;
        if (s_settings.ps2_controls_) {
            if (s_keys.left_spin_ != SDL_SCANCODE_1 && s_menu_keys.rotate_left_ == 1)
                s_menu_keys.rotate_left_ = kLockedOut;
            if (s_keys.right_spin_ != SDL_SCANCODE_2 && s_menu_keys.rotate_right_ == 1)
                s_menu_keys.rotate_right_ = kLockedOut;
            if (s_keys.switch_revert_ != SDL_SCANCODE_MINUS && s_menu_keys.zoom == 1)
                s_menu_keys.zoom = kLockedOut;
        } else {
            if (s_keys.nollie != SDL_SCANCODE_1 && s_menu_keys.rotate_left_ == 1)
                s_menu_keys.rotate_left_ = kLockedOut;
            if (s_keys.switch_revert_ != SDL_SCANCODE_2 && s_menu_keys.rotate_right_ == 1)
                s_menu_keys.rotate_right_ = kLockedOut;
            if (s_keys.right_spin_ != SDL_SCANCODE_MINUS && s_menu_keys.zoom == 1)
                s_menu_keys.zoom = kLockedOut;
        }
        s_was_in_menu = s_in_menu;
        s_was_keyboard_on_screen = keyboard_on_screen;
    }

    // a key's state, except while it's locked out and still down
    auto update = [keys](uint8_t& state, SDL_Scancode key) {
        if (!(state == kLockedOut && keys[key]))
            state = keys[key];
    };
    update(s_menu_keys.affirmative, SDL_SCANCODE_RETURN);
    update(s_menu_keys.negative, SDL_SCANCODE_ESCAPE);
    update(s_menu_keys.menu, SDL_SCANCODE_E);
    update(s_menu_keys.extra, SDL_SCANCODE_R);
    update(s_menu_keys.up, SDL_SCANCODE_UP);
    update(s_menu_keys.down, SDL_SCANCODE_DOWN);
    update(s_menu_keys.left, SDL_SCANCODE_LEFT);
    update(s_menu_keys.right, SDL_SCANCODE_RIGHT);
    update(s_menu_keys.rotate_left_, SDL_SCANCODE_1);
    update(s_menu_keys.rotate_right_, SDL_SCANCODE_2);
    update(s_menu_keys.zoom, SDL_SCANCODE_MINUS);
}

// A bound key (its scan code's low byte, as the bindings are passed): a menu key doesn't count as a binding in a
// menu or while locked out.
uint8_t get_key_down(const uint8_t* keys, uint8_t key)
{
    auto menu_key = [keys, key](uint8_t state) -> uint8_t { return state != kLockedOut && (!s_in_menu && keys[key]); };
    switch (key) {
    case SDL_SCANCODE_RETURN:
        return menu_key(s_menu_keys.affirmative);
    case SDL_SCANCODE_ESCAPE:
        return menu_key(s_menu_keys.negative);
    case SDL_SCANCODE_E:
        return menu_key(s_menu_keys.menu);
    case SDL_SCANCODE_R:
        return menu_key(s_menu_keys.extra);
    case SDL_SCANCODE_UP:
        return menu_key(s_menu_keys.up);
    case SDL_SCANCODE_DOWN:
        return menu_key(s_menu_keys.down);
    case SDL_SCANCODE_LEFT:
        return menu_key(s_menu_keys.left);
    case SDL_SCANCODE_RIGHT:
        return menu_key(s_menu_keys.right);
    case SDL_SCANCODE_1:
        return menu_key(s_menu_keys.rotate_left_);
    case SDL_SCANCODE_2:
        return menu_key(s_menu_keys.rotate_right_);
    case SDL_SCANCODE_MINUS:
        return menu_key(s_menu_keys.zoom);
    default:
        return keys[key];
    }
}

void poll_keyboard(InputPort* device)
{
    uint8_t* data = device->control_data_;
    device->is_usable_ = 1;
    device->is_connected_ = true;

    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    update_menu_keys(keys);
    if (get_keyboard_on_screen())
        return;
    // (a binding's scan code is passed as its low byte)
    auto down = [keys](SDL_Scancode key) { return get_key_down(keys, static_cast<uint8_t>(key)) != 0; };
    auto menu_down = [](uint8_t state) { return s_in_menu && state == 1; };

    // Not original - the level editor (editor/level_editor.h) flies on W/A/S/D/Q/E/Shift/Ctrl/Space: while it
    // runs, the keyboard gives the game only the Pause key (Start: leaves the editor), once it has been up since the
    // editor started; nothing else.
    // Original behaviour: every binding is read.
    if (frontend::level_editor::live_now()) {
        if (frontend::level_editor::pass_pause_key(keys[s_keys.menu] != 0))
            data[2] |= 1 << 3;
        return;
    }

    if (down(s_keys.menu))
        data[2] |= 1 << 3;
    if (down(s_keys.camera_toggle_))
        data[2] |= 1 << 0;
    if (down(s_keys.focus))
        data[2] |= 1 << 1;
    if (down(s_keys.camera_swivel_lock_))
        data[2] |= 1 << 2;

    if (down(s_keys.grind) || menu_down(s_menu_keys.extra)) {
        data[3] |= 1 << 4;
        data[12] = 0xFF;
    }
    if (down(s_keys.grab) || menu_down(s_menu_keys.negative)) {
        data[3] |= 1 << 5;
        data[13] = 0xFF;
    }
    if (down(s_keys.ollie) || menu_down(s_menu_keys.affirmative)) {
        data[3] |= 1 << 6;
        data[14] = 0xFF;
    }
    if (down(s_keys.kick) || menu_down(s_menu_keys.menu)) {
        data[3] |= 1 << 7;
        data[15] = 0xFF;
    }

    // the shoulders
    if (s_settings.ps2_controls_) {
        if (down(s_keys.left_spin_) || menu_down(s_menu_keys.rotate_left_)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (down(s_keys.right_spin_) || menu_down(s_menu_keys.rotate_right_)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            if (s_editing_park)
                data[20] |= 1 << 0; // black, editing a park
        }
        if (down(s_keys.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
            if (s_in_menu)
                data[20] |= 1 << 1; // white, in a menu
        }
        if (down(s_keys.switch_revert_) || menu_down(s_menu_keys.zoom)) {
            data[3] |= 1 << 1;
            data[19] = 0xFF;
            if (s_in_menu)
                data[20] |= 1 << 0; // black, in a menu
            else if (s_editing_park)
                data[20] |= 1 << 1; // white, editing a park
        }
    } else if (s_editing_park && !get_keyboard_on_screen()) {
        // editing a park: the right trigger is L1
        if (down(s_keys.switch_revert_)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (down(s_keys.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (down(s_keys.left_spin_))
            data[20] |= 1 << 1;
        if (down(s_keys.right_spin_))
            data[20] |= 1 << 0;
    } else {
        if (down(s_keys.nollie) || menu_down(s_menu_keys.rotate_left_)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (down(s_keys.switch_revert_) || menu_down(s_menu_keys.rotate_right_)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            data[3] |= 1 << 1;
            data[19] = 0xFF;
        }
        // Not PartyMod's - without UsePS2Controls, the spin keys the original PC game's way round (its keyboard,
        // 0x005F5BC0: the key table's spin left -> the Xbox pad's black, spin right -> white). The game reads the Xbox
        // triggers then (Component::platform_structure), where black steps off the board (SwitchControl_Trigger's
        // Xbox_Trigger): spin left alone does it, as in the original game.
        // Original behaviour (PartyMod v1.0.4): the other way round - spin left white, spin right black:
        //     if (down(s_keys.left_spin_)) data[20] |= 1 << 1;
        //     if (down(s_keys.right_spin_) || menu_down(s_menu_keys.zoom)) data[20] |= 1 << 0;
        if (down(s_keys.left_spin_))
            data[20] |= 1 << 0; // black
        if (down(s_keys.right_spin_))
            data[20] |= 1 << 1; // white
        if (menu_down(s_menu_keys.zoom))
            data[20] |= 1 << 0;
    }
    if (down(s_keys.caveman))
        press_caveman(data);

    // the d-pad: the item keys
    if (down(s_keys.item_up_) || menu_down(s_menu_keys.up)) {
        data[2] |= 1 << 4;
        data[10] = 0xFF;
    }
    if (down(s_keys.item_right_) || menu_down(s_menu_keys.right)) {
        data[2] |= 1 << 5;
        data[8] = 0xFF;
    }
    if (down(s_keys.item_down_) || menu_down(s_menu_keys.down)) {
        data[2] |= 1 << 6;
        data[11] = 0xFF;
    }
    if (down(s_keys.item_left_) || menu_down(s_menu_keys.left)) {
        data[2] |= 1 << 7;
        data[9] = 0xFF;
    }

    // the sticks: opposite keys together cancel out
    if (down(s_keys.camera_left_) && !down(s_keys.camera_right_))
        data[4] = 0;
    if (down(s_keys.camera_right_) && !down(s_keys.camera_left_))
        data[4] = 255;
    if (down(s_keys.camera_up_) && !down(s_keys.camera_down_))
        data[5] = 0;
    if (down(s_keys.camera_down_) && !down(s_keys.camera_up_))
        data[5] = 255;
    // Not original - THAW's half-stick key (PartyMod's THAW build: Feeble, 64 / 192 instead of 0 / 255) while THAW's
    // BMX is ridden (game/rider/thaw_bmx.h half_stick_key): its grind pick reads how far the stick is pushed.
    // Original behaviour: the Feeble binding is read but unused - the movement keys always give 0 / 255.
    const bool half = down(s_keys.feeble) && skater::thaw_bmx::half_stick_key();
    const uint8_t low = half ? 64 : 0, high = half ? 192 : 255;
    if (down(s_keys.left) && !down(s_keys.right))
        data[6] = low;
    if (down(s_keys.right) && !down(s_keys.left))
        data[6] = high;
    if (down(s_keys.up) && !down(s_keys.down))
        data[7] = low;
    if (down(s_keys.down) && !down(s_keys.up))
        data[7] = high;
}

// --- typed keys -----------------------------------------------------------------------------------------------

// A key press: with the on-screen keyboard up, Enter / Backspace / Escape / keypad Enter as typed characters;
// otherwise Enter and F1-F4 as the game's special keys (0x19, 0x1F, 0x1C-0x1E: the chat's F1...).
void key_pressed(SDL_Keycode key)
{
    if (!get_keyboard_on_screen()) {
        if (key == SDLK_RETURN)
            key_input(0x19, 0);
        else if (key == SDLK_F1)
            key_input(0x1F, 0);
        else if (key == SDLK_F2)
            key_input(0x1C, 0);
        else if (key == SDLK_F3)
            key_input(0x1D, 0);
        else if (key == SDLK_F4)
            key_input(0x1E, 0);
        return;
    }
    int typed;
    if (key == SDLK_RETURN)
        typed = '\r';
    else if (key == SDLK_BACKSPACE)
        typed = '\b';
    else if (key == SDLK_ESCAPE)
        typed = 0x1B;
    else if (key == SDLK_KP_ENTER)
        typed = '\r';
    else
        typed = -1;
    if (typed != -1)
        key_input(typed, 0);
}

// Typed text, one character at a time, while the on-screen keyboard is up.
void text_typed(const char* text)
{
    if (!get_keyboard_on_screen())
        return;
    if (std::strlen(text) == 1)
        key_input(text[0], 0);
    else
        debug_log::printf(StageMap::kDebug, "Input text '%s' > 1 byte!!\n", text);
}

void handle_input_event(SDL_Event* event)
{
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (SDL_IsGameController(event->cdevice.which))
            attach_controller(event->cdevice.which);
        else
            debug_log::printf(StageMap::kInfo, "Not a game controller: %s\n",
                              SDL_JoystickNameForIndex(event->cdevice.which));
        return;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which))
            detach_anim_driver(controller);
        return;
    case SDL_JOYDEVICEADDED:
        debug_log::printf(StageMap::kInfo, "Joystick added: %s\n", SDL_JoystickNameForIndex(event->jdevice.which));
        return;
    case SDL_KEYDOWN:
        s_using_keyboard = true;
        // (not original: Enter / F1-F4 aren't the game's special keys while the new front end has the keyboard -
        // frontend/new_frontend.h)
        if (!frontend::new_frontend::blocks_game_input())
            key_pressed(event->key.keysym.sym);
        return;
    case SDL_CONTROLLERBUTTONDOWN: {
        // (the event's controller, read through cdevice: the same place as cbutton's)
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player == -1)
            enroll_player(controller);
        else if (s_players[player].locked_out_)
            ++s_players[player].locked_out_;
        s_using_keyboard = false;
        return;
    }
    case SDL_CONTROLLERBUTTONUP: {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player != -1 && s_players[player].locked_out_)
            --s_players[player].locked_out_;
        return;
    }
    case SDL_CONTROLLERAXISMOTION:
        s_using_keyboard = false;
        return;
    case SDL_TEXTINPUT:
        text_typed(event->text.text);
        return;
    default:
        return;
    }
}

// --- the game's key bindings ------------------------------------------------------------------------------------

// SDL scan codes -> DirectInput key codes (-1: none).
const uint32_t kDirectInputKeys[256] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x1E,       0x30,       0x2E,       0x20,       // 0..7
    0x12,       0x21,       0x22,       0x23,       0x17,       0x24,       0x25,       0x26,       // 8..15
    0x32,       0x31,       0x18,       0x19,       0x10,       0x13,       0x1F,       0x14,       // 16..23
    0x16,       0x2F,       0x11,       0x2D,       0x15,       0x2C,       0x02,       0x03,       // 24..31
    0x04,       0x05,       0x06,       0x07,       0x08,       0x09,       0x0A,       0x0B,       // 32..39
    0x1C,       0x01,       0x0E,       0x0F,       0x39,       0x0C,       0x0D,       0x1A,       // 40..47
    0x1B,       0x2B,       0x2B,       0x27,       0x28,       0x29,       0x33,       0x34,       // 48..55
    0x35,       0x3A,       0x3B,       0x3C,       0x3D,       0x3E,       0x3F,       0x40,       // 56..63
    0x41,       0x42,       0x43,       0x44,       0x57,       0x58,       0xFFFFFFFF, 0x46,       // 64..71
    0xC5,       0xD2,       0xC7,       0xC9,       0xD3,       0xCF,       0xD1,       0xCD,       // 72..79
    0xCB,       0xD0,       0xC8,       0x45,       0xB5,       0x37,       0x4A,       0x4E,       // 80..87
    0x9C,       0x4F,       0x50,       0x51,       0x4B,       0x4C,       0x4D,       0x47,       // 88..95
    0x48,       0x49,       0x52,       0x53,       0x2B,       0xDD,       0xDE,       0x8D,       // 96..103
    0x64,       0x65,       0x66,       0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 104..111
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 112..119
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 120..127
    0xB0,       0xAE,       0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xB3,       0xFFFFFFFF, 0xFFFFFFFF, // 128..135
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 136..143
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 144..151
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 152..159
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 160..167
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 168..175
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 176..183
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 184..191
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 192..199
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 200..207
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 208..215
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 216..223
    0x1D,       0x2A,       0x38,       0xDB,       0x9D,       0x36,       0xB8,       0xDC,       // 224..231
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 232..239
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 240..247
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 248..255
};

int32_t direct_input_key(SDL_Scancode scan_code)
{
    if (static_cast<int>(scan_code) < 256)
        return static_cast<int32_t>(kDirectInputKeys[scan_code]);
    return -1;
}

// The bindings in the game's table (0x007D6770, port 0's actions), for the menus' key hints.
void write_game_key_bindings()
{
    int32_t* binds = os_layer::g_input_map;
    binds[0] = direct_input_key(s_keys.up);
    binds[1] = direct_input_key(s_keys.down);
    binds[2] = direct_input_key(s_keys.left);
    binds[3] = direct_input_key(s_keys.right);

    binds[4] = direct_input_key(s_keys.camera_up_);
    binds[5] = direct_input_key(s_keys.camera_down_);
    binds[6] = direct_input_key(s_keys.camera_left_);
    binds[7] = direct_input_key(s_keys.camera_right_);

    binds[8] = direct_input_key(s_keys.ollie);
    binds[9] = direct_input_key(s_keys.grab);
    binds[10] = direct_input_key(s_keys.kick);
    binds[11] = direct_input_key(s_keys.grind);

    binds[13] = direct_input_key(s_keys.camera_toggle_);
    binds[14] = direct_input_key(s_keys.left_spin_);
    binds[15] = direct_input_key(s_keys.right_spin_);
    binds[16] = direct_input_key(s_keys.nollie);
    binds[17] = direct_input_key(s_keys.switch_revert_);
    binds[18] = direct_input_key(s_keys.focus);

    binds[20] = direct_input_key(s_keys.item_up_);
    binds[21] = direct_input_key(s_keys.item_down_);
    binds[22] = direct_input_key(s_keys.item_left_);
    binds[23] = direct_input_key(s_keys.item_right_);
}

// --- reading the settings -----------------------------------------------------------------------------------------

void load_input_settings()
{
    s_settings.ps2_controls_ = build_info::fetch_flag(build_info::kMiscellaneous, "UsePS2Controls", true);
    s_settings.ui_controls_ = static_cast<uint8_t>(build_info::get_int(build_info::kMiscellaneous, "UIControls", 1));
}

SDL_Scancode get_key(const char* name, SDL_Scancode fallback)
{
    return static_cast<SDL_Scancode>(build_info::get_int(build_info::kKeybinds, name, fallback));
}

void load_key_binds()
{
    s_keys.menu = get_key("Pause", SDL_SCANCODE_RETURN);
    s_keys.camera_toggle_ = get_key("ViewToggle", SDL_SCANCODE_F);
    s_keys.camera_swivel_lock_ = get_key("SwivelLock", SDL_SCANCODE_GRAVE);
    s_keys.focus = get_key("Focus", SDL_SCANCODE_KP_ENTER);
    s_keys.caveman = get_key("Caveman", SDL_SCANCODE_E);

    s_keys.grind = get_key("Grind", SDL_SCANCODE_KP_8);
    s_keys.grab = get_key("Grab", SDL_SCANCODE_KP_6);
    s_keys.ollie = get_key("Ollie", SDL_SCANCODE_KP_2);
    s_keys.kick = get_key("Flip", SDL_SCANCODE_KP_4);

    s_keys.left_spin_ = get_key("SpinLeft", SDL_SCANCODE_KP_1);
    s_keys.right_spin_ = get_key("SpinRight", SDL_SCANCODE_KP_3);
    s_keys.nollie = get_key("Nollie", SDL_SCANCODE_KP_7);
    s_keys.switch_revert_ = get_key("Switch", SDL_SCANCODE_KP_9);

    s_keys.right = get_key("Right", SDL_SCANCODE_D);
    s_keys.left = get_key("Left", SDL_SCANCODE_A);
    s_keys.up = get_key("Forward", SDL_SCANCODE_W);
    s_keys.down = get_key("Backward", SDL_SCANCODE_S);
    s_keys.feeble = get_key("Feeble", SDL_SCANCODE_LSHIFT);

    s_keys.camera_right_ = get_key("CameraRight", SDL_SCANCODE_KP_MULTIPLY);
    s_keys.camera_left_ = get_key("CameraLeft", SDL_SCANCODE_KP_DIVIDE);
    s_keys.camera_up_ = get_key("CameraUp", SDL_SCANCODE_KP_PLUS);
    s_keys.camera_down_ = get_key("CameraDown", SDL_SCANCODE_KP_MINUS);

    s_keys.item_right_ = get_key("ItemRight", SDL_SCANCODE_L);
    s_keys.item_left_ = get_key("ItemLeft", SDL_SCANCODE_J);
    s_keys.item_up_ = get_key("ItemUp", SDL_SCANCODE_I);
    s_keys.item_down_ = get_key("ItemDown", SDL_SCANCODE_K);
}

PadButton get_button(const char* name, int fallback)
{
    return static_cast<PadButton>(build_info::get_int(build_info::kGamepad, name, fallback));
}

void load_pad_binds()
{
    s_pad.menu = get_button("Pause", kButtonStart);
    s_pad.camera_toggle_ = get_button("ViewToggle", kButtonBack);
    s_pad.camera_swivel_lock_ = get_button("SwivelLock", kButtonRightStick);
    s_pad.focus = get_button("Focus", kButtonLeftStick);
    // (unbound unless set: PartyMod's fallback 0 is the A button - Ollie - harmless only while nothing used it)
    s_pad.caveman = get_button("Caveman", kUnbound);

    s_pad.grind = get_button("Grind", kButtonY);
    s_pad.grab = get_button("Grab", kButtonB);
    s_pad.ollie = get_button("Ollie", kButtonA);
    s_pad.kick = get_button("Flip", kButtonX);

    s_pad.left_spin_ = get_button("SpinLeft", kButtonLeftShoulder);
    s_pad.right_spin_ = get_button("SpinRight", kButtonRightShoulder);
    s_pad.nollie = get_button("Nollie", kLeftTrigger);
    s_pad.switch_revert_ = get_button("Switch", kRightTrigger);

    s_pad.right = get_button("Right", kDpadRight);
    s_pad.left = get_button("Left", kDpadLeft);
    s_pad.up = get_button("Forward", kDpadUp);
    s_pad.down = get_button("Backward", kDpadDown);

    s_pad.movement = static_cast<PadStick>(build_info::get_int(build_info::kGamepad, "MovementStick", kStickLeft));
    s_pad.camera = static_cast<PadStick>(build_info::get_int(build_info::kGamepad, "CameraStick", kStickRight));
}

// PartyMod's view of the device has these flags as 32-bit words (the game's are bytes followed by padding): the
// byte set and the padding after it cleared, as its word stores did. The held-down word's second byte is
// keyboard_control_, cleared this way too.
void set_usable(InputPort* device, bool on)
{
    device->is_usable_ = on ? 1 : 0;
    std::memset(device->pad15_, 0, sizeof(device->pad15_));
}
void set_connected(InputPort* device, bool on)
{
    device->is_connected_ = on;
    std::memset(device->pade1_, 0, sizeof(device->pade1_));
}
void set_held_down(InputPort* device, bool on)
{
    device->held_down_ = on ? 1 : 0;
    device->keyboard_control_ = 0;
    std::memset(device->padee_, 0, sizeof(device->padee_));
}

} // namespace

InputSettings& the_input_settings() { return s_settings; }
bool& using_keyboard() { return s_using_keyboard; }

void init_sdl_input()
{
    debug_log::printf(StageMap::kInfo, "Initializing Input Manager!\n");
    SDL_Init(SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "false");

    char mappings[1024];
    int length =
        sprintf_s(mappings, sizeof(mappings), "%s%s", build_info::executable_directory(), "gamecontrollerdb.txt");
    if (length) {
        // (-1, a failure, counts as loaded too)
        if (SDL_GameControllerAddMappingsFromFile(mappings))
            debug_log::printf(StageMap::kInfo, "Loaded mappings\n");
        else
            debug_log::printf(StageMap::kWarn, "Failed to load %s\n", mappings);
    }

    load_input_settings();
    load_key_binds();
    load_pad_binds();
    init_controllers();
    if (s_settings.ps2_controls_)
        sys::install_is_ps2();
    // (not PartyMod's - [features] debug_mode: CD false, the developers' menus - engine/sys/debug_mode.h)
    sys::debug_mode::install();
    write_game_key_bindings();
    os_layer::register_event_handler(handle_input_event);
    // (not PartyMod's - the mouse_menus feature: the mouse's events, when it's on)
    mouse_menus::prepare_all();
    // (not PartyMod's either - the level editor: its mouse and Escape key)
    frontend::level_editor::prepare_all();
    // (nor the new front end's input - frontend/new_frontend.h; nothing when it's off)
    frontend::new_frontend::prepare_all();
    // (nor the screenshot key - platform/screenshot.h)
    sys::screenshot::prepare_all();
}

void process_sdl_device(InputPort* device)
{
    s_editing_park = in_park_editor();
    s_in_menu = in_menu_flag() && !s_editing_park;

    device->capabilities_ = 3;
    device->rumble_motor_count_ = 2;
    device->rumble_cap_[0] = 255;
    device->rumble_cap_[1] = 255;
    device->cur_phase_ = InputPort::kReady;

    os_layer::pump_sdl_events();

    set_usable(device, false);
    set_connected(device, false);
    uint8_t* data = device->control_data_;
    data[0] = 0;
    data[1] = 0x70;
    // The button bits are set for buttons down while the keyboard and the controller are read (so both can add
    // to them) and inverted at the end.
    data[2] = 0;
    data[3] = 0;
    for (int i = 12; i < 20; ++i) // the button and shoulder pressures
        data[i] = 0;
    for (int i = 8; i < 12; ++i) // the d-pad's
        data[i] = 0;
    for (int i = 4; i < 8; ++i) // the sticks, centred
        data[i] = 127;
    data[20] = 0;

    // Not original - the new front end (frontend/new_frontend.h): while it's up (and until the keys that chose for it
    // are let go) it has the keyboard and the controllers - the game reads nothing pressed.
    // Original behaviour: the keyboard and the controller always read.
    bool new_frontend = frontend::new_frontend::blocks_game_input();
    if (device->port_no_ == 0) {
        set_usable(device, true);
        set_connected(device, true);
        if (!get_keyboard_on_screen() && !new_frontend)
            poll_keyboard(device);
    }
    auto port = static_cast<uint32_t>(device->port_no_);
    if (port < kMaxPlayers && s_players[port].controller && !s_players[port].locked_out_ && !new_frontend)
        poll_controller(device, s_players[port].controller);
    // (not original: the buttons the test-automation channel presses - frontend/automation.h)
    frontend::automation::add_pad_buttons(device->port_no_, data);

    data[2] = static_cast<uint8_t>(~data[2]);
    data[3] = static_cast<uint8_t>(~data[3]);
    set_held_down(device, (0xFFFF ^ (data[2] << 8 | data[3])) != 0);
    device->start_or_a_ = ((~data[2] & (1 << 3)) || (~data[3] & (1 << 6))) ? 1 : 0;

    // (typed text only works with these set)
    online::g_text_entry = 1;
    g_in_menu_flag = 0;
}

void __cdecl set_sdl_actuators(int port, uint16_t high, uint16_t low)
{
    for (SDL_GameController* controller : s_controllers) {
        if (SDL_GameControllerGetAttached(controller) && SDL_GameControllerGetPlayerIndex(controller) == port)
            SDL_JoystickRumble(SDL_GameControllerGetJoystick(controller), low, high, 0);
    }
}

} // namespace sio
