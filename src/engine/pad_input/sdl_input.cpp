// Input on PC: SDL game controllers and the keyboard.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/pad_input/sdl_input.h"
#include "platform/keyboard.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

#include <SDL2/SDL.h>
#include <array>

#include "base/core/format.h"
#include "devtools/automation.h"
#include "engine/ext/extension.h"
#include "frontend/mouse_menus.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/pad_input/device.h"
#include "game/rider/thaw_bmx.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "engine/sys/debug_mode.h"
#include "engine/pad_input/pad_cfuncs.h"
#include "devtools/screenshot.h"
#include "platform/sdl_events.h"
#include "engine/pad_input/direct_input.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/net/net_game_state.h"

namespace sio {

namespace {

constexpr int kRumbleFull = 0x7FFF;            // SDL's rumble strength range
constexpr int kRumbleMilliseconds = 250;
constexpr int kStickUpperHalf = 127;           // a stick (0-255) above this is past the centre
constexpr int kStickMagnitudeMask = 0x7F;
constexpr int kTriggerShift = 7;               // a trigger axis (0-32767) down to 0-255
constexpr int kTriggerHeldThreshold = 0x80;
constexpr int kAxisShift = 8;                  // a stick axis (-32768..32767) down to -128..127
constexpr int kAxisCentre = 128;
constexpr int kStickRest = 0x80;
constexpr int kStickFull = 255;
constexpr int kStickCentred = 127;
constexpr int kFullRumble = 255;

constexpr int kTypedEnter = 0x19;
constexpr int kSpecialF1 = 0x1F;                // the special keys F1-F4 give outside the on-screen keyboard
constexpr int kSpecialF2 = 0x1C;
constexpr int kSpecialF3 = 0x1D;
constexpr int kSpecialF4 = 0x1E;
constexpr int kEscapeChar = 0x1B;
constexpr int kDirectInputKeyCount = 256;
constexpr size_t kMappingsBytes = 1024;
constexpr int kAllButtonBits = 0xFFFF;
constexpr int kByteBits = 8;
constexpr int kInGameMenuElement = 0x5999551E; // (text unknown)

} // namespace

namespace {

// NOT ORIGINAL grouping - whether the on-screen keyboard is up and whether a menu is on screen. Made on first use,
// never destroyed (screen_flags()).
struct ScreenFlags {
    bool keyboard_on_screen_{};
    bool in_menu_flag_{};
};

ScreenFlags& screen_flags()
{
    static core::NoDestroy<ScreenFlags> state;
    return state.get();
}

} // namespace

bool& keyboard_on_screen_flag() { return screen_flags().keyboard_on_screen_; }
bool& menu_flag() { return screen_flags().in_menu_flag_; }

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
// (an int: holds the ini's number as read, any value kept as given)
using PadButton = int;
constexpr int kUnbound = -1;
constexpr int kButtonA = SDL_CONTROLLER_BUTTON_A;
constexpr int kButtonB = SDL_CONTROLLER_BUTTON_B;
constexpr int kButtonX = SDL_CONTROLLER_BUTTON_X;
constexpr int kButtonY = SDL_CONTROLLER_BUTTON_Y;
constexpr int kButtonBack = SDL_CONTROLLER_BUTTON_BACK;
constexpr int kButtonStart = SDL_CONTROLLER_BUTTON_START;
constexpr int kButtonLeftStick = SDL_CONTROLLER_BUTTON_LEFTSTICK;
constexpr int kButtonRightStick = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
constexpr int kButtonLeftShoulder = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
constexpr int kButtonRightShoulder = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
constexpr int kDpadUp = SDL_CONTROLLER_BUTTON_DPAD_UP;
constexpr int kDpadDown = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
constexpr int kDpadLeft = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
constexpr int kDpadRight = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
constexpr int kRightTrigger = 21;
constexpr int kLeftTrigger = 22;

// (an int: holds the ini's number as read, any value kept as given)
using PadStick = int;
constexpr int kStickUnbound = -1;
constexpr int kStickLeft = 0;
constexpr int kStickRight = 1;

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

struct PlayerSlot {
    SDL_GameController* controller;
    uint8_t locked_out_; // after joining: locked out until all of its buttons are up (counts buttons down)
};
constexpr int kMaxPlayers = 2;
// The menu keys (see update_menu_keys).
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

// NOT ORIGINAL grouping - the SDL input's settings and bindings, the open controllers and the players' seats, the menu
// keys. Made on first use, never destroyed (sdl_input_state()).
struct SdlInputState {
    InputSettings settings_{};
    KeyBinds keys_{};
    PadBinds pad_{};
    bool using_keyboard_ = true;
    // The open controllers (removed ones shift the rest down).
    std::vector<SDL_GameController*> controllers_;
    uint8_t seats_in_use_ = 0;
    std::array<PlayerSlot, kMaxPlayers> players_ = {{{.controller = nullptr, .locked_out_ = 0}, {.controller = nullptr, .locked_out_ = 0}}};
    uint8_t in_menu_ = 0;
    uint8_t editing_park_ = 0;
    MenuKeys menu_keys_{};
    uint8_t was_in_menu_ = 0;
    uint8_t was_keyboard_on_screen_ = 0;
};

SdlInputState& sdl_input_state()
{
    static core::NoDestroy<SdlInputState> state;
    return state.get();
}

// --- the game's state ---------------------------------------------------------------------------------------

// The on-screen keyboard (text entry) is up.
bool get_keyboard_on_screen() { return screen_flags().keyboard_on_screen_; }
uint8_t in_menu_flag() { return static_cast<uint8_t>(screen_flags().in_menu_flag_); }

// --- controllers and players ----------------------------------------------------------------------------------

void attach_controller(int index)
{
    debug_log::printf(StageMap::kInfo, "Detected controller \"%s\"\n", SDL_GameControllerNameForIndex(index));
    SDL_GameController* controller = SDL_GameControllerOpen(index);
    SDL_GameControllerSetPlayerIndex(controller, -1);
    if (!controller) {
        return;
    }
    sdl_input_state().controllers_.push_back(controller);
}

void enroll_player(SDL_GameController* controller)
{
    if (sdl_input_state().seats_in_use_ >= 2) {
        debug_log::printf(StageMap::kInfo, "Already two players, not adding\n");
        return;
    }
    int slot = 0;
    while (slot < kMaxPlayers && core::at(sdl_input_state().players_, slot).controller) {
        ++slot;
    }
    if (slot == kMaxPlayers) {
        return;
    }
    SDL_GameControllerSetPlayerIndex(controller, slot);
    core::at(sdl_input_state().players_, slot).controller = controller;
    if (sdl_input_state().seats_in_use_ > 0) {
        core::at(sdl_input_state().players_, slot).locked_out_ = 1;
        SDL_JoystickRumble(SDL_GameControllerGetJoystick(controller), kRumbleFull, kRumbleFull, kRumbleMilliseconds);
    }
    ++sdl_input_state().seats_in_use_;
    debug_log::printf(StageMap::kInfo, "Added player %d: %s\n", slot + 1, SDL_GameControllerName(controller));
}

void prune_players()
{
    for (int i = 0; i < kMaxPlayers; ++i) {
        if (core::at(sdl_input_state().players_, i).controller &&
            (SDL_GameControllerGetAttached(core::at(sdl_input_state().players_, i).controller) == SDL_FALSE)) {
            debug_log::printf(StageMap::kInfo, "Pruned player %d\n", i + 1);
            core::at(sdl_input_state().players_, i).controller = nullptr;
            --sdl_input_state().seats_in_use_;
            debug_log::printf(StageMap::kInfo, "Remaining players: %d\n", sdl_input_state().seats_in_use_);
        }
    }
}

void detach_anim_driver(SDL_GameController* controller)
{
    debug_log::printf(StageMap::kInfo, "Controller \"%s\" disconnected\n", SDL_GameControllerName(controller));
    auto found = std::ranges::find(sdl_input_state().controllers_, controller);
    // (PartyMod looked one past the last entry when it wasn't found - past the end of its array when that was full)
    if (found == sdl_input_state().controllers_.end()) {
        debug_log::printf(StageMap::kWarn, "Did not find disconnected controller in list\n");
        return;
    }
    // (the player index read before the close - PartyMod read it from the closed, freed controller)
    const int player = SDL_GameControllerGetPlayerIndex(controller);
    SDL_GameControllerClose(controller);
    if (player != -1) {
        debug_log::printf(StageMap::kInfo, "Removed player %d\n", player + 1);
        core::at(sdl_input_state().players_, player).controller = nullptr;
        --sdl_input_state().seats_in_use_;
    }
    prune_players();
    sdl_input_state().controllers_.erase(found);
}

void init_controllers()
{
    debug_log::printf(StageMap::kInfo, "Initializing Controller Input\n");
    sdl_input_state().controllers_.clear();
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) != SDL_FALSE) {
            attach_controller(i);
        }
    }
}

// --- reading a controller -------------------------------------------------------------------------------------

// How far a stick axis (0-255, 128 the centre) is from the centre.
uint8_t stick_deflection(uint8_t value)
{
    if (value > kStickUpperHalf) {
        return value & kStickMagnitudeMask;
    }
    return ~value & kStickMagnitudeMask;
}

uint8_t is_controller_button_held(SDL_GameController* controller, PadButton button)
{
    if (button == kLeftTrigger) {
        auto pressure =
            static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> kTriggerShift);
        return static_cast<uint8_t>(pressure > kTriggerHeldThreshold);
    }
    if (button == kRightTrigger) {
        auto pressure =
            static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> kTriggerShift);
        return static_cast<uint8_t>(pressure > kTriggerHeldThreshold);
    }
    return SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button));
}

// Where a stick's axes go.
struct StickOut {
    uint8_t* x{};
    uint8_t* y{};
};

// A stick into (x, y), where it's further from the centre than what's there (the keyboard's).
void read_stick(SDL_GameController* controller, PadStick stick, StickOut stick_out)
{
    auto* const x = stick_out.x;
    auto* const y = stick_out.y;
    uint8_t pad_axis_x = 0;
    uint8_t stick_y = 0;
    if (stick == kStickLeft) {
        pad_axis_x =
            static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) >> kAxisShift) + kAxisCentre);
        stick_y = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) >> kAxisShift) + kAxisCentre);
    } else if (stick == kStickRight) {
        pad_axis_x =
            static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) >> kAxisShift) + kAxisCentre);
        stick_y = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) >> kAxisShift) + kAxisCentre);
    } else {
        pad_axis_x = kStickRest;
        stick_y = kStickRest;
    }
    if (stick_deflection(pad_axis_x) > stick_deflection(*x)) {
        *x = pad_axis_x;
    }
    if (stick_deflection(stick_y) > stick_deflection(*y)) {
        *y = stick_y;
    }
}

// Whether the park editor is up: the screen element 0x5999551E exists (the screen element manager 0x00701440's
// fetch_part 0x004AAE20 - it returns a smart pointer, released by its destructor).
uint8_t in_park_editor()
{
    const frontend::UiTree* manager = frontend::UiTree::sole();
    if (!manager) {
        return 0;
    }
    frontend::ElementPtr element;
    return frontend::UiTree::fetch_part(&element, kInGameMenuElement, 0)->get_pull() ? 1 : 0;
}

// Not original (PartyMod reads a Caveman binding and never uses it) - the Caveman key / button steps off the board
// and back on, pressing what the game's SwitchControl_Trigger (game\skater\switch_control.qb) waits for in the
// trigger set it reads (Skater::Component::platform_structure): with UsePS2Controls the PS2 one, L1 and R1 together
// (PressTwoAnyOrder, 400 ms); without, the Xbox one, black (the original PC game's Key Pad 1 / spin left key). Not in a
// menu or while editing a park, where those mean something else.
// Original behaviour (PartyMod v1.0.4): the binding is read, nothing presses it.
void press_caveman(std::array<uint8_t, kPortBufferBytes>& data)
{
    if ((sdl_input_state().in_menu_ != 0U) || (sdl_input_state().editing_park_ != 0U)) {
        return;
    }
    if (sdl_input_state().settings_.ps2_controls_ == 0U) {
        core::at(data, kControlExtraButtons) |= 1 << 0; // black
        return;
    }
    core::at(data, kControlButtonsLow) |= (1 << 2) | (1 << 3); // L1, R1
    core::at(data, kPressureL1) = kFullPressure;
    core::at(data, kPressureR1) = kFullPressure;
}

// A controller's shoulder buttons: PS2 style, the park editor's (the right trigger is L1), or the PC game's.
void read_pad_shoulders(std::array<uint8_t, kPortBufferBytes>& data, SDL_GameController* controller)
{
    if (sdl_input_state().settings_.ps2_controls_ != 0U) {
        if (is_controller_button_held(controller, sdl_input_state().pad_.left_spin_) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.right_spin_) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 3;
            core::at(data, kPressureR1) = kFullPressure;
            if (sdl_input_state().editing_park_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 0; // black, editing a park
            }
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.nollie) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
            if (sdl_input_state().in_menu_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 1; // white, in a menu
            }
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.switch_revert_) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 1;
            core::at(data, kPressureR2) = kFullPressure;
            if (sdl_input_state().in_menu_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 0; // black, in a menu
            } else if (sdl_input_state().editing_park_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 1; // white, editing a park
            }
        }
    } else if ((sdl_input_state().editing_park_ != 0U) && !get_keyboard_on_screen()) {
        // editing a park: the right trigger is L1
        if (is_controller_button_held(controller, sdl_input_state().pad_.switch_revert_) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.nollie) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.left_spin_) != 0U) {
            core::at(data, kControlExtraButtons) |= 1 << 1;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.right_spin_) != 0U) {
            core::at(data, kControlExtraButtons) |= 1 << 0;
        }
    } else {
        if (is_controller_button_held(controller, sdl_input_state().pad_.nollie) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.switch_revert_) != 0U) {
            core::at(data, kControlButtonsLow) |= 1 << 3;
            core::at(data, kPressureR1) = kFullPressure;
            core::at(data, kControlButtonsLow) |= 1 << 1;
            core::at(data, kPressureR2) = kFullPressure;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.left_spin_) != 0U) {
            core::at(data, kControlExtraButtons) |= 1 << 1;
        }
        if (is_controller_button_held(controller, sdl_input_state().pad_.right_spin_) != 0U) {
            core::at(data, kControlExtraButtons) |= 1 << 0;
        }
    }
}

// The control data's bytes: 2 and 3 the button bits (set while down here; inverted at the end - the game's
// format has a bit set while a button is up), 4-7 the sticks, 8-19 the pressures, 20 the Xbox's black (bit 0)
// and white (bit 1).
void poll_controller(InputPort* device, SDL_GameController* controller)
{
    if (SDL_GameControllerGetAttached(controller) == SDL_FALSE) {
        return;
    }
    std::array<uint8_t, kPortBufferBytes>& data = device->control_data();
    device->set_is_usable(1);
    device->set_is_connected(true);

    if (is_controller_button_held(controller, sdl_input_state().pad_.menu) != 0U) {
        core::at(data, kControlButtonsHigh) |= 1 << 3;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.camera_toggle_) != 0U) {
        core::at(data, kControlButtonsHigh) |= 1 << 0;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.camera_swivel_lock_) != 0U) {
        core::at(data, kControlButtonsHigh) |= 1 << 2;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.focus) != 0U) {
        core::at(data, kControlButtonsHigh) |= 1 << 1;
    }

    if (is_controller_button_held(controller, sdl_input_state().pad_.grind) != 0U) {
        core::at(data, kControlButtonsLow) |= 1 << 4;
        core::at(data, kPressureTriangle) = kFullPressure;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.grab) != 0U) {
        core::at(data, kControlButtonsLow) |= 1 << kCircleBit;
        core::at(data, kPressureCircle) = kFullPressure;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.ollie) != 0U) {
        core::at(data, kControlButtonsLow) |= 1 << kCrossBit;
        core::at(data, kPressureCross) = kFullPressure;
    }
    if (is_controller_button_held(controller, sdl_input_state().pad_.kick) != 0U) {
        core::at(data, kControlButtonsLow) |= 1 << kSquareBit;
        core::at(data, kPressureSquare) = kFullPressure;
    }

    // the shoulders
    read_pad_shoulders(data, controller);
    if (is_controller_button_held(controller, sdl_input_state().pad_.caveman) != 0U) {
        press_caveman(data);
    }

    // the d-pad (plain buttons: not the triggers)
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(sdl_input_state().pad_.up)) !=
        0U) {
        core::at(data, kControlButtonsHigh) |= 1 << 4;
        core::at(data, kPressureUp) = kFullPressure;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(sdl_input_state().pad_.right)) !=
        0U) {
        core::at(data, kControlButtonsHigh) |= 1 << kRightBit;
        core::at(data, kPressureRight) = kFullPressure;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(sdl_input_state().pad_.down)) !=
        0U) {
        core::at(data, kControlButtonsHigh) |= 1 << kDownBit;
        core::at(data, kPressureDown) = kFullPressure;
    }
    if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(sdl_input_state().pad_.left)) !=
        0U) {
        core::at(data, kControlButtonsHigh) |= 1 << kLeftBit;
        core::at(data, kPressureLeft) = kFullPressure;
    }

    read_stick(controller, sdl_input_state().pad_.camera, {.x = &core::at(data, kControlRightStickX), .y = &core::at(data, kControlRightStickY)});
    read_stick(controller, sdl_input_state().pad_.movement, {.x = &core::at(data, kControlLeftStickX), .y = &core::at(data, kControlLeftStickY)});
}

// --- the keyboard -----------------------------------------------------------------------------------------------

// The menu keys are fixed (Enter, Escape, E, R, the arrows, 1, 2, -); in a menu they stand for the buttons, and
// outside one their bindings work as keys. A menu key still down when the menu (or the on-screen keyboard) comes
// or goes is locked out until it's released, unless it's also bound to the same thing.
constexpr uint8_t kLockedOut = 2;

// The menu or the on-screen keyboard came or went: a menu key still down is locked out until it's released, unless
// it's also bound to the same thing.
void lock_out_menu_keys(uint8_t keyboard_on_screen)
{
    if (sdl_input_state().keys_.ollie != SDL_SCANCODE_RETURN && sdl_input_state().menu_keys_.affirmative == 1) {
        sdl_input_state().menu_keys_.affirmative = kLockedOut;
    }
    if (sdl_input_state().keys_.grab != SDL_SCANCODE_ESCAPE && sdl_input_state().menu_keys_.negative == 1) {
        sdl_input_state().menu_keys_.negative = kLockedOut;
    }
    if (sdl_input_state().keys_.kick != SDL_SCANCODE_E && sdl_input_state().menu_keys_.menu == 1) {
        sdl_input_state().menu_keys_.menu = kLockedOut;
    }
    if (sdl_input_state().keys_.grind != SDL_SCANCODE_R && sdl_input_state().menu_keys_.extra == 1) {
        sdl_input_state().menu_keys_.extra = kLockedOut;
    }
    if (sdl_input_state().keys_.up != SDL_SCANCODE_UP && sdl_input_state().menu_keys_.up == 1) {
        sdl_input_state().menu_keys_.up = kLockedOut;
    }
    if (sdl_input_state().keys_.down != SDL_SCANCODE_DOWN && sdl_input_state().menu_keys_.down == 1) {
        sdl_input_state().menu_keys_.down = kLockedOut;
    }
    if (sdl_input_state().keys_.left != SDL_SCANCODE_LEFT && sdl_input_state().menu_keys_.left == 1) {
        sdl_input_state().menu_keys_.left = kLockedOut;
    }
    if (sdl_input_state().keys_.right != SDL_SCANCODE_RIGHT && sdl_input_state().menu_keys_.right == 1) {
        sdl_input_state().menu_keys_.right = kLockedOut;
    }
    if (sdl_input_state().settings_.ps2_controls_ != 0U) {
        if (sdl_input_state().keys_.left_spin_ != SDL_SCANCODE_1 && sdl_input_state().menu_keys_.rotate_left_ == 1) {
            sdl_input_state().menu_keys_.rotate_left_ = kLockedOut;
        }
        if (sdl_input_state().keys_.right_spin_ != SDL_SCANCODE_2 && sdl_input_state().menu_keys_.rotate_right_ == 1) {
            sdl_input_state().menu_keys_.rotate_right_ = kLockedOut;
        }
        if (sdl_input_state().keys_.switch_revert_ != SDL_SCANCODE_MINUS && sdl_input_state().menu_keys_.zoom == 1) {
            sdl_input_state().menu_keys_.zoom = kLockedOut;
        }
    } else {
        if (sdl_input_state().keys_.nollie != SDL_SCANCODE_1 && sdl_input_state().menu_keys_.rotate_left_ == 1) {
            sdl_input_state().menu_keys_.rotate_left_ = kLockedOut;
        }
        if (sdl_input_state().keys_.switch_revert_ != SDL_SCANCODE_2 &&
            sdl_input_state().menu_keys_.rotate_right_ == 1) {
            sdl_input_state().menu_keys_.rotate_right_ = kLockedOut;
        }
        if (sdl_input_state().keys_.right_spin_ != SDL_SCANCODE_MINUS && sdl_input_state().menu_keys_.zoom == 1) {
            sdl_input_state().menu_keys_.zoom = kLockedOut;
        }
    }
    sdl_input_state().was_in_menu_ = sdl_input_state().in_menu_;
    sdl_input_state().was_keyboard_on_screen_ = keyboard_on_screen;
}

void update_menu_keys(platform::KeyState keys)
{
    const auto keyboard_on_screen = static_cast<uint8_t>(screen_flags().keyboard_on_screen_);
    if (sdl_input_state().in_menu_ != sdl_input_state().was_in_menu_ ||
        keyboard_on_screen != sdl_input_state().was_keyboard_on_screen_) {
        lock_out_menu_keys(keyboard_on_screen);
    }

    // a key's state, except while it's locked out and still down
    auto update = [keys](uint8_t& state, SDL_Scancode key) {
        if (!(state == kLockedOut && core::at(keys, key))) {
            state = core::at(keys, key);
        }
    };
    update(sdl_input_state().menu_keys_.affirmative, SDL_SCANCODE_RETURN);
    update(sdl_input_state().menu_keys_.negative, SDL_SCANCODE_ESCAPE);
    update(sdl_input_state().menu_keys_.menu, SDL_SCANCODE_E);
    update(sdl_input_state().menu_keys_.extra, SDL_SCANCODE_R);
    update(sdl_input_state().menu_keys_.up, SDL_SCANCODE_UP);
    update(sdl_input_state().menu_keys_.down, SDL_SCANCODE_DOWN);
    update(sdl_input_state().menu_keys_.left, SDL_SCANCODE_LEFT);
    update(sdl_input_state().menu_keys_.right, SDL_SCANCODE_RIGHT);
    update(sdl_input_state().menu_keys_.rotate_left_, SDL_SCANCODE_1);
    update(sdl_input_state().menu_keys_.rotate_right_, SDL_SCANCODE_2);
    update(sdl_input_state().menu_keys_.zoom, SDL_SCANCODE_MINUS);
}

// A bound key (its scan code's low byte, as the bindings are passed): a menu key doesn't count as a binding in a
// menu or while locked out.
uint8_t get_key_down(platform::KeyState keys, uint8_t key)
{
    auto menu_key = [keys, key](uint8_t state) -> uint8_t { return state != kLockedOut && (!sdl_input_state().in_menu_ && core::at(keys, key)); };
    switch (key) {
    case SDL_SCANCODE_RETURN:
        return menu_key(sdl_input_state().menu_keys_.affirmative);
    case SDL_SCANCODE_ESCAPE:
        return menu_key(sdl_input_state().menu_keys_.negative);
    case SDL_SCANCODE_E:
        return menu_key(sdl_input_state().menu_keys_.menu);
    case SDL_SCANCODE_R:
        return menu_key(sdl_input_state().menu_keys_.extra);
    case SDL_SCANCODE_UP:
        return menu_key(sdl_input_state().menu_keys_.up);
    case SDL_SCANCODE_DOWN:
        return menu_key(sdl_input_state().menu_keys_.down);
    case SDL_SCANCODE_LEFT:
        return menu_key(sdl_input_state().menu_keys_.left);
    case SDL_SCANCODE_RIGHT:
        return menu_key(sdl_input_state().menu_keys_.right);
    case SDL_SCANCODE_1:
        return menu_key(sdl_input_state().menu_keys_.rotate_left_);
    case SDL_SCANCODE_2:
        return menu_key(sdl_input_state().menu_keys_.rotate_right_);
    case SDL_SCANCODE_MINUS:
        return menu_key(sdl_input_state().menu_keys_.zoom);
    default:
        return core::at(keys, key);
    }
}

// The keyboard's shoulder keys: PS2 style, the park editor's (the right trigger is L1), or the PC game's.
void read_key_shoulders(std::array<uint8_t, kPortBufferBytes>& data, platform::KeyState keys)
{
    // (a binding's scan code is passed as its low byte)
    auto down = [keys](SDL_Scancode key) { return get_key_down(keys, static_cast<uint8_t>(key)) != 0; };
    auto menu_down = [](uint8_t state) { return sdl_input_state().in_menu_ && state == 1; };
    if (sdl_input_state().settings_.ps2_controls_ != 0U) {
        if (down(sdl_input_state().keys_.left_spin_) || menu_down(sdl_input_state().menu_keys_.rotate_left_)) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
        }
        if (down(sdl_input_state().keys_.right_spin_) || menu_down(sdl_input_state().menu_keys_.rotate_right_)) {
            core::at(data, kControlButtonsLow) |= 1 << 3;
            core::at(data, kPressureR1) = kFullPressure;
            if (sdl_input_state().editing_park_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 0; // black, editing a park
            }
        }
        if (down(sdl_input_state().keys_.nollie)) {
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
            if (sdl_input_state().in_menu_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 1; // white, in a menu
            }
        }
        if (down(sdl_input_state().keys_.switch_revert_) || menu_down(sdl_input_state().menu_keys_.zoom)) {
            core::at(data, kControlButtonsLow) |= 1 << 1;
            core::at(data, kPressureR2) = kFullPressure;
            if (sdl_input_state().in_menu_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 0; // black, in a menu
            } else if (sdl_input_state().editing_park_ != 0U) {
                core::at(data, kControlExtraButtons) |= 1 << 1; // white, editing a park
            }
        }
    } else if ((sdl_input_state().editing_park_ != 0U) && !get_keyboard_on_screen()) {
        // editing a park: the right trigger is L1
        if (down(sdl_input_state().keys_.switch_revert_)) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
        }
        if (down(sdl_input_state().keys_.nollie)) {
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
        }
        if (down(sdl_input_state().keys_.left_spin_)) {
            core::at(data, kControlExtraButtons) |= 1 << 1;
        }
        if (down(sdl_input_state().keys_.right_spin_)) {
            core::at(data, kControlExtraButtons) |= 1 << 0;
        }
    } else {
        if (down(sdl_input_state().keys_.nollie) || menu_down(sdl_input_state().menu_keys_.rotate_left_)) {
            core::at(data, kControlButtonsLow) |= 1 << 2;
            core::at(data, kPressureL1) = kFullPressure;
            core::at(data, kControlButtonsLow) |= 1 << 0;
            core::at(data, kPressureL2) = kFullPressure;
        }
        if (down(sdl_input_state().keys_.switch_revert_) || menu_down(sdl_input_state().menu_keys_.rotate_right_)) {
            core::at(data, kControlButtonsLow) |= 1 << 3;
            core::at(data, kPressureR1) = kFullPressure;
            core::at(data, kControlButtonsLow) |= 1 << 1;
            core::at(data, kPressureR2) = kFullPressure;
        }
        // Not PartyMod's - without UsePS2Controls, the spin keys the original PC game's way round (its keyboard,
        // 0x005F5BC0: the key table's spin left -> the Xbox pad's black, spin right -> white). The game reads the Xbox
        // triggers then (Component::platform_structure), where black steps off the board (SwitchControl_Trigger's
        // Xbox_Trigger): spin left alone does it, as in the original game.
        // Original behaviour (PartyMod v1.0.4): the other way round - spin left white, spin right black:
        //     if (down(sdl_input_state().keys_.left_spin_)) core::at(data, kControlExtraButtons) |= 1 << 1;
        //     if (down(sdl_input_state().keys_.right_spin_) || menu_down(sdl_input_state().menu_keys_.zoom)) core::at(data, kControlExtraButtons) |= 1 << 0;
        if (down(sdl_input_state().keys_.left_spin_)) {
            core::at(data, kControlExtraButtons) |= 1 << 0; // black
        }
        if (down(sdl_input_state().keys_.right_spin_)) {
            core::at(data, kControlExtraButtons) |= 1 << 1; // white
        }
        if (menu_down(sdl_input_state().menu_keys_.zoom)) {
            core::at(data, kControlExtraButtons) |= 1 << 0;
        }
    }
}

// The keyboard's sticks: opposite keys together cancel out.
void read_key_sticks(std::array<uint8_t, kPortBufferBytes>& data, platform::KeyState keys)
{
    // (a binding's scan code is passed as its low byte)
    auto down = [keys](SDL_Scancode key) { return get_key_down(keys, static_cast<uint8_t>(key)) != 0; };
    // the sticks: opposite keys together cancel out
    if (down(sdl_input_state().keys_.camera_left_) && !down(sdl_input_state().keys_.camera_right_)) {
        core::at(data, kControlRightStickX) = 0;
    }
    if (down(sdl_input_state().keys_.camera_right_) && !down(sdl_input_state().keys_.camera_left_)) {
        core::at(data, kControlRightStickX) = kStickFull;
    }
    if (down(sdl_input_state().keys_.camera_up_) && !down(sdl_input_state().keys_.camera_down_)) {
        core::at(data, kControlRightStickY) = 0;
    }
    if (down(sdl_input_state().keys_.camera_down_) && !down(sdl_input_state().keys_.camera_up_)) {
        core::at(data, kControlRightStickY) = kStickFull;
    }
    // Not original - THAW's half-stick key (PartyMod's THAW build: Feeble, 64 / 192 instead of 0 / 255) while THAW's
    // BMX is ridden (game/rider/thaw_bmx.h half_stick_key): its grind pick reads how far the stick is pushed.
    // Original behaviour: the Feeble binding is read but unused - the movement keys always give 0 / 255.
    const bool half = down(sdl_input_state().keys_.feeble) && skater::thaw_bmx::half_stick_key();
    const uint8_t low = half ? 64 : 0;
    const uint8_t high = half ? 192 : 255;
    if (down(sdl_input_state().keys_.left) && !down(sdl_input_state().keys_.right)) {
        core::at(data, kControlLeftStickX) = low;
    }
    if (down(sdl_input_state().keys_.right) && !down(sdl_input_state().keys_.left)) {
        core::at(data, kControlLeftStickX) = high;
    }
    if (down(sdl_input_state().keys_.up) && !down(sdl_input_state().keys_.down)) {
        core::at(data, kControlLeftStickY) = low;
    }
    if (down(sdl_input_state().keys_.down) && !down(sdl_input_state().keys_.up)) {
        core::at(data, kControlLeftStickY) = high;
    }
}

void poll_keyboard(InputPort* device)
{
    std::array<uint8_t, kPortBufferBytes>& data = device->control_data();
    device->set_is_usable(1);
    device->set_is_connected(true);

    const platform::KeyState keys = platform::keyboard_keys();
    update_menu_keys(keys);
    if (get_keyboard_on_screen()) {
        return;
    }
    // (a binding's scan code is passed as its low byte)
    auto down = [keys](SDL_Scancode key) { return get_key_down(keys, static_cast<uint8_t>(key)) != 0; };
    auto menu_down = [](uint8_t state) { return sdl_input_state().in_menu_ && state == 1; };

    // Not original - an extension that owns the game (engine/ext/extension.h; the level editor flies on
    // W/A/S/D/Q/E/Shift/Ctrl/Space): while it does, the keyboard gives the game only the Pause key (Start: leaves the
    // editor), once it has been up since the editor started; nothing else.
    // Original behaviour: every binding is read.
    if (ext::Extension* owner = ext::game_owner()) {
        if (owner->pass_pause_key(core::at(keys, sdl_input_state().keys_.menu) != 0)) {
            core::at(data, kControlButtonsHigh) |= 1 << 3;
        }
        return;
    }

    if (down(sdl_input_state().keys_.menu)) {
        core::at(data, kControlButtonsHigh) |= 1 << 3;
    }
    if (down(sdl_input_state().keys_.camera_toggle_)) {
        core::at(data, kControlButtonsHigh) |= 1 << 0;
    }
    if (down(sdl_input_state().keys_.focus)) {
        core::at(data, kControlButtonsHigh) |= 1 << 1;
    }
    if (down(sdl_input_state().keys_.camera_swivel_lock_)) {
        core::at(data, kControlButtonsHigh) |= 1 << 2;
    }

    if (down(sdl_input_state().keys_.grind) || menu_down(sdl_input_state().menu_keys_.extra)) {
        core::at(data, kControlButtonsLow) |= 1 << 4;
        core::at(data, kPressureTriangle) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.grab) || menu_down(sdl_input_state().menu_keys_.negative)) {
        core::at(data, kControlButtonsLow) |= 1 << kCircleBit;
        core::at(data, kPressureCircle) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.ollie) || menu_down(sdl_input_state().menu_keys_.affirmative)) {
        core::at(data, kControlButtonsLow) |= 1 << kCrossBit;
        core::at(data, kPressureCross) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.kick) || menu_down(sdl_input_state().menu_keys_.menu)) {
        core::at(data, kControlButtonsLow) |= 1 << kSquareBit;
        core::at(data, kPressureSquare) = kFullPressure;
    }

    // the shoulders
    read_key_shoulders(data, keys);
    if (down(sdl_input_state().keys_.caveman)) {
        press_caveman(data);
    }

    // the d-pad: the item keys
    if (down(sdl_input_state().keys_.item_up_) || menu_down(sdl_input_state().menu_keys_.up)) {
        core::at(data, kControlButtonsHigh) |= 1 << 4;
        core::at(data, kPressureUp) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.item_right_) || menu_down(sdl_input_state().menu_keys_.right)) {
        core::at(data, kControlButtonsHigh) |= 1 << kRightBit;
        core::at(data, kPressureRight) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.item_down_) || menu_down(sdl_input_state().menu_keys_.down)) {
        core::at(data, kControlButtonsHigh) |= 1 << kDownBit;
        core::at(data, kPressureDown) = kFullPressure;
    }
    if (down(sdl_input_state().keys_.item_left_) || menu_down(sdl_input_state().menu_keys_.left)) {
        core::at(data, kControlButtonsHigh) |= 1 << kLeftBit;
        core::at(data, kPressureLeft) = kFullPressure;
    }

    read_key_sticks(data, keys);
}

// --- typed keys -----------------------------------------------------------------------------------------------

// A key press: with the on-screen keyboard up, Enter / Backspace / Escape / keypad Enter as typed characters;
// otherwise Enter and F1-F4 as the game's special keys (0x19, 0x1F, 0x1C-0x1E: the chat's F1...).
void key_pressed(SDL_Keycode key)
{
    if (!get_keyboard_on_screen()) {
        if (key == SDLK_RETURN) {
            key_input({.key = kTypedEnter, .flags = 0});
        } else if (key == SDLK_F1) {
            key_input({.key = kSpecialF1, .flags = 0});
        } else if (key == SDLK_F2) {
            key_input({.key = kSpecialF2, .flags = 0});
        } else if (key == SDLK_F3) {
            key_input({.key = kSpecialF3, .flags = 0});
        } else if (key == SDLK_F4) {
            key_input({.key = kSpecialF4, .flags = 0});
        }
        return;
    }
    int typed = 0;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        typed = '\r';
    } else if (key == SDLK_BACKSPACE) {
        typed = '\b';
    } else if (key == SDLK_ESCAPE) {
        typed = kEscapeChar;
    } else {
        typed = -1;
    }
    if (typed != -1) {
        key_input({.key = typed, .flags = 0});
    }
}

// Typed text, one character at a time, while the on-screen keyboard is up.
void text_typed(const char* text)
{
    if (!get_keyboard_on_screen()) {
        return;
    }
    if (std::strlen(text) == 1) {
        key_input({.key = *text, .flags = 0});
    } else {
        debug_log::printf(StageMap::kDebug, "Input text '%s' > 1 byte!!\n", text);
    }
}

void handle_input_event(SDL_Event* event)
{
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (SDL_IsGameController(event->cdevice.which) != SDL_FALSE) {
            attach_controller(event->cdevice.which);
        } else {
            debug_log::printf(StageMap::kInfo, "Not a game controller: %s\n",
                              SDL_JoystickNameForIndex(event->cdevice.which));
        }
        return;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which)) {
            detach_anim_driver(controller);
        }
        return;
    case SDL_JOYDEVICEADDED:
        debug_log::printf(StageMap::kInfo, "Joystick added: %s\n", SDL_JoystickNameForIndex(event->jdevice.which));
        return;
    case SDL_KEYDOWN:
        sdl_input_state().using_keyboard_ = true;
        // (not original: Enter / F1-F4 aren't the game's special keys while an extension has the keyboard -
        // engine/ext/extension.h; the new front end)
        if (!ext::game_input_blocked()) {
            key_pressed(event->key.keysym.sym);
        }
        return;
    case SDL_CONTROLLERBUTTONDOWN: {
        // (the event's controller, read through cdevice: the same place as cbutton's)
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        const int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player == -1) {
            enroll_player(controller);
        } else if (core::at(sdl_input_state().players_, player).locked_out_ != 0U) {
            ++core::at(sdl_input_state().players_, player).locked_out_;
        }
        sdl_input_state().using_keyboard_ = false;
        return;
    }
    case SDL_CONTROLLERBUTTONUP: {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        const int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player != -1 && (core::at(sdl_input_state().players_, player).locked_out_ != 0U)) {
            --core::at(sdl_input_state().players_, player).locked_out_;
        }
        return;
    }
    case SDL_CONTROLLERAXISMOTION:
        sdl_input_state().using_keyboard_ = false;
        return;
    case SDL_TEXTINPUT:
        text_typed(std::data(event->text.text));
        return;
    default:
        return;
    }
}

// --- the game's key bindings ------------------------------------------------------------------------------------

// SDL scan codes -> DirectInput key codes (-1: none).
const std::array<uint32_t, 256> kDirectInputKeys = {{
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
}};

int32_t direct_input_key(SDL_Scancode scan_code)
{
    if (static_cast<int>(scan_code) < kDirectInputKeyCount) {
        return static_cast<int32_t>(core::at(kDirectInputKeys, scan_code));
    }
    return -1;
}

// The bindings in the game's table (0x007D6770, port 0's actions), for the menus' key hints.
void write_game_key_bindings()
{
    std::array<int32_t, os_layer::kBindings>& binds = os_layer::get_controls().front().keys;
    core::at(binds, os_layer::kBindUp) = direct_input_key(sdl_input_state().keys_.up);
    core::at(binds, os_layer::kBindDown) = direct_input_key(sdl_input_state().keys_.down);
    core::at(binds, os_layer::kBindLeft) = direct_input_key(sdl_input_state().keys_.left);
    core::at(binds, os_layer::kBindRight) = direct_input_key(sdl_input_state().keys_.right);

    core::at(binds, os_layer::kBindCameraUp) = direct_input_key(sdl_input_state().keys_.camera_up_);
    core::at(binds, os_layer::kBindCameraDown) = direct_input_key(sdl_input_state().keys_.camera_down_);
    core::at(binds, os_layer::kBindCameraLeft) = direct_input_key(sdl_input_state().keys_.camera_left_);
    core::at(binds, os_layer::kBindCameraRight) = direct_input_key(sdl_input_state().keys_.camera_right_);

    core::at(binds, os_layer::kBindOllie) = direct_input_key(sdl_input_state().keys_.ollie);
    core::at(binds, os_layer::kBindGrab) = direct_input_key(sdl_input_state().keys_.grab);
    core::at(binds, os_layer::kBindKick) = direct_input_key(sdl_input_state().keys_.kick);
    core::at(binds, os_layer::kBindGrind) = direct_input_key(sdl_input_state().keys_.grind);

    core::at(binds, os_layer::kBindCameraToggle) = direct_input_key(sdl_input_state().keys_.camera_toggle_);
    core::at(binds, os_layer::kBindLeftSpin) = direct_input_key(sdl_input_state().keys_.left_spin_);
    core::at(binds, os_layer::kBindRightSpin) = direct_input_key(sdl_input_state().keys_.right_spin_);
    core::at(binds, os_layer::kBindNollie) = direct_input_key(sdl_input_state().keys_.nollie);
    core::at(binds, os_layer::kBindSwitchRevert) = direct_input_key(sdl_input_state().keys_.switch_revert_);
    core::at(binds, os_layer::kBindFocus) = direct_input_key(sdl_input_state().keys_.focus);

    core::at(binds, os_layer::kBindItemUp) = direct_input_key(sdl_input_state().keys_.item_up_);
    core::at(binds, os_layer::kBindItemDown) = direct_input_key(sdl_input_state().keys_.item_down_);
    core::at(binds, os_layer::kBindItemLeft) = direct_input_key(sdl_input_state().keys_.item_left_);
    core::at(binds, os_layer::kBindItemRight) = direct_input_key(sdl_input_state().keys_.item_right_);
}

// --- reading the settings -----------------------------------------------------------------------------------------

void load_input_settings()
{
    sdl_input_state().settings_.ps2_controls_ =
        static_cast<uint8_t>(build_info::fetch_flag(build_info::kMiscellaneous, "UsePS2Controls", true));
    sdl_input_state().settings_.ui_controls_ = static_cast<uint8_t>(build_info::get_int(build_info::kMiscellaneous, "UIControls", 1));
}

SDL_Scancode get_key(const char* name, SDL_Scancode fallback)
{
    return static_cast<SDL_Scancode>(build_info::get_int(build_info::kKeybinds, name, fallback));
}

void load_key_binds()
{
    sdl_input_state().keys_.menu = get_key("Pause", SDL_SCANCODE_RETURN);
    sdl_input_state().keys_.camera_toggle_ = get_key("ViewToggle", SDL_SCANCODE_F);
    sdl_input_state().keys_.camera_swivel_lock_ = get_key("SwivelLock", SDL_SCANCODE_GRAVE);
    sdl_input_state().keys_.focus = get_key("Focus", SDL_SCANCODE_KP_ENTER);
    sdl_input_state().keys_.caveman = get_key("Caveman", SDL_SCANCODE_E);

    sdl_input_state().keys_.grind = get_key("Grind", SDL_SCANCODE_KP_8);
    sdl_input_state().keys_.grab = get_key("Grab", SDL_SCANCODE_KP_6);
    sdl_input_state().keys_.ollie = get_key("Ollie", SDL_SCANCODE_KP_2);
    sdl_input_state().keys_.kick = get_key("Flip", SDL_SCANCODE_KP_4);

    sdl_input_state().keys_.left_spin_ = get_key("SpinLeft", SDL_SCANCODE_KP_1);
    sdl_input_state().keys_.right_spin_ = get_key("SpinRight", SDL_SCANCODE_KP_3);
    sdl_input_state().keys_.nollie = get_key("Nollie", SDL_SCANCODE_KP_7);
    sdl_input_state().keys_.switch_revert_ = get_key("Switch", SDL_SCANCODE_KP_9);

    sdl_input_state().keys_.right = get_key("Right", SDL_SCANCODE_D);
    sdl_input_state().keys_.left = get_key("Left", SDL_SCANCODE_A);
    sdl_input_state().keys_.up = get_key("Forward", SDL_SCANCODE_W);
    sdl_input_state().keys_.down = get_key("Backward", SDL_SCANCODE_S);
    sdl_input_state().keys_.feeble = get_key("Feeble", SDL_SCANCODE_LSHIFT);

    sdl_input_state().keys_.camera_right_ = get_key("CameraRight", SDL_SCANCODE_KP_MULTIPLY);
    sdl_input_state().keys_.camera_left_ = get_key("CameraLeft", SDL_SCANCODE_KP_DIVIDE);
    sdl_input_state().keys_.camera_up_ = get_key("CameraUp", SDL_SCANCODE_KP_PLUS);
    sdl_input_state().keys_.camera_down_ = get_key("CameraDown", SDL_SCANCODE_KP_MINUS);

    sdl_input_state().keys_.item_right_ = get_key("ItemRight", SDL_SCANCODE_L);
    sdl_input_state().keys_.item_left_ = get_key("ItemLeft", SDL_SCANCODE_J);
    sdl_input_state().keys_.item_up_ = get_key("ItemUp", SDL_SCANCODE_I);
    sdl_input_state().keys_.item_down_ = get_key("ItemDown", SDL_SCANCODE_K);
}

PadButton get_button(const char* name, int fallback)
{
    return static_cast<PadButton>(build_info::get_int(build_info::kGamepad, name, fallback));
}

void load_pad_binds()
{
    sdl_input_state().pad_.menu = get_button("Pause", kButtonStart);
    sdl_input_state().pad_.camera_toggle_ = get_button("ViewToggle", kButtonBack);
    sdl_input_state().pad_.camera_swivel_lock_ = get_button("SwivelLock", kButtonRightStick);
    sdl_input_state().pad_.focus = get_button("Focus", kButtonLeftStick);
    // (unbound unless set: PartyMod's fallback 0 is the A button - Ollie - harmless only while nothing used it)
    sdl_input_state().pad_.caveman = get_button("Caveman", kUnbound);

    sdl_input_state().pad_.grind = get_button("Grind", kButtonY);
    sdl_input_state().pad_.grab = get_button("Grab", kButtonB);
    sdl_input_state().pad_.ollie = get_button("Ollie", kButtonA);
    sdl_input_state().pad_.kick = get_button("Flip", kButtonX);

    sdl_input_state().pad_.left_spin_ = get_button("SpinLeft", kButtonLeftShoulder);
    sdl_input_state().pad_.right_spin_ = get_button("SpinRight", kButtonRightShoulder);
    sdl_input_state().pad_.nollie = get_button("Nollie", kLeftTrigger);
    sdl_input_state().pad_.switch_revert_ = get_button("Switch", kRightTrigger);

    sdl_input_state().pad_.right = get_button("Right", kDpadRight);
    sdl_input_state().pad_.left = get_button("Left", kDpadLeft);
    sdl_input_state().pad_.up = get_button("Forward", kDpadUp);
    sdl_input_state().pad_.down = get_button("Backward", kDpadDown);

    sdl_input_state().pad_.movement = static_cast<PadStick>(build_info::get_int(build_info::kGamepad, "MovementStick", kStickLeft));
    sdl_input_state().pad_.camera = static_cast<PadStick>(build_info::get_int(build_info::kGamepad, "CameraStick", kStickRight));
}

// PartyMod's view of the device has these flags as 32-bit words (the game's are bytes followed by padding): the
// byte set and the padding after it cleared, as its word stores did. The held-down word's second byte is
// keyboard_control_, cleared this way too.
void set_usable(InputPort* device, bool on)
{
    device->set_is_usable(on ? 1 : 0);
    std::memset(device->pad15().data(), 0, sizeof(device->pad15()));
}
void set_connected(InputPort* device, bool on)
{
    device->set_is_connected(on);
    std::memset(device->pade1().data(), 0, sizeof(device->pade1()));
}
void set_held_down(InputPort* device, bool on)
{
    device->set_held_down(on ? 1 : 0);
    device->set_keyboard_control(0);
    std::memset(device->padee().data(), 0, sizeof(device->padee()));
}

} // namespace

InputSettings& the_input_settings() { return sdl_input_state().settings_; }
bool& using_keyboard() { return sdl_input_state().using_keyboard_; }

void init_sdl_input()
{
    debug_log::printf(StageMap::kInfo, "Initializing Input Manager!\n");
    SDL_Init(SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "false");

    std::array<char, kMappingsBytes> mappings{};
    const size_t length = text::format_to(mappings, "%s%s", build_info::executable_directory(), "gamecontrollerdb.txt");
    if (length != 0) {
        // (sprintf_s's -1, a failure, counted as loaded too; a path too long for the buffer is cut now)
        if (SDL_GameControllerAddMappingsFromFile(mappings.data())) {
            debug_log::printf(StageMap::kInfo, "Loaded mappings\n");
        } else {
            debug_log::printf(StageMap::kWarn, "Failed to load %s\n", mappings.data());
        }
    }

    load_input_settings();
    load_key_binds();
    load_pad_binds();
    init_controllers();
    if (sdl_input_state().settings_.ps2_controls_ != 0U) {
        sys::install_is_ps2();
    }
    // (not PartyMod's - [features] debug_mode: CD false, the developers' menus - engine/sys/debug_mode.h)
    sys::debug_mode::install();
    write_game_key_bindings();
    os_layer::register_event_handler(handle_input_event);
    // (not PartyMod's - the mouse_menus feature: the mouse's events, when it's on)
    mouse_menus::prepare_all();
    // (not PartyMod's either - the extensions, engine/ext/extension.h: the level editor's mouse and Escape key, the
    // new front end's input - nothing when it's off)
    ext::prepare_input();
    // (nor the screenshot key - devtools/screenshot.h)
    sys::screenshot::prepare_all();
}

void process_sdl_device(InputPort* device)
{
    sdl_input_state().editing_park_ = in_park_editor();
    sdl_input_state().in_menu_ =
        static_cast<uint8_t>((in_menu_flag() != 0U) && (sdl_input_state().editing_park_ == 0U));

    device->set_capabilities(3);
    device->set_rumble_motor_count(2);
    core::at(device->rumble_cap(), 0) = kFullRumble;
    core::at(device->rumble_cap(), 1) = kFullRumble;
    device->set_cur_phase(InputPort::State::kReady);

    os_layer::pump_sdl_events();

    set_usable(device, false);
    set_connected(device, false);
    std::array<uint8_t, kPortBufferBytes>& data = device->control_data();
    core::at(data, 0) = 0;
    core::at(data, 1) = kAnalogModeByte;
    // The button bits are set for buttons down while the keyboard and the controller are read (so both can add
    // to them) and inverted at the end.
    core::at(data, kControlButtonsHigh) = 0;
    core::at(data, kControlButtonsLow) = 0;
    for (int i = kPressureTriangle; i < kControlExtraButtons; ++i) { // the button and shoulder pressures
        core::at(data, i) = 0;
    }
    for (int i = kPressureRight; i < kPressureTriangle; ++i) { // the d-pad's
        core::at(data, i) = 0;
    }
    for (int i = 4; i < kPressureRight; ++i) { // the sticks, centred
        core::at(data, i) = kStickCentred;
    }
    core::at(data, kControlExtraButtons) = 0;

    // Not original - an extension that blocks the game's input (engine/ext/extension.h; the new front end: while it's
    // up, and until the keys that chose for it are let go, it has the keyboard and the controllers) - the game reads
    // nothing pressed.
    // Original behaviour: the keyboard and the controller always read.
    const bool blocked = ext::game_input_blocked();
    if (device->port_no() == 0) {
        set_usable(device, true);
        set_connected(device, true);
        if (!get_keyboard_on_screen() && !blocked) {
            poll_keyboard(device);
        }
    }
    auto port = static_cast<uint32_t>(device->port_no());
    if (port < kMaxPlayers && core::at(sdl_input_state().players_, port).controller &&
        (core::at(sdl_input_state().players_, port).locked_out_ == 0U) && !blocked) {
        poll_controller(device, core::at(sdl_input_state().players_, port).controller);
    }
    // (not original: the buttons the test-automation channel presses - devtools/automation.h)
    frontend::automation::add_pad_buttons(device->port_no(), data);

    core::at(data, kControlButtonsHigh) = static_cast<uint8_t>(~core::at(data, kControlButtonsHigh));
    core::at(data, kControlButtonsLow) = static_cast<uint8_t>(~core::at(data, kControlButtonsLow));
    set_held_down(device, (kAllButtonBits ^ (core::at(data, kControlButtonsHigh) << kByteBits | core::at(data, kControlButtonsLow))) != 0);
    device->set_start_or_a((((~core::at(data, kControlButtonsHigh) & (1 << 3)) != 0) ||
                            ((~core::at(data, kControlButtonsLow) & (1 << kCrossBit)) != 0))
                               ? 1
                               : 0);

    // (typed text only works with these set)
    online::net_game_state().set_text_entry(true);
    screen_flags().in_menu_flag_ = false;
}

void set_sdl_actuators(int port, uint16_t high, uint16_t low)
{
    for (SDL_GameController* controller : sdl_input_state().controllers_) {
        if ((SDL_GameControllerGetAttached(controller) != SDL_FALSE) &&
            SDL_GameControllerGetPlayerIndex(controller) == port) {
            SDL_JoystickRumble(SDL_GameControllerGetJoystick(controller), low, high, 0);
        }
    }
}

} // namespace sio
