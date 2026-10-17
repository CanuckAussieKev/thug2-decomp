// Input on PC: SDL game controllers and the keyboard.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/sio/sdl_input.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL2/SDL.h>

#include "game/front/mouse_menus.h"
#include "game/sio/device.h"
#include "game/sys/config.h"
#include "game/sys/debug_log.h"
#include "game/sys/platform_cfuncs.h"
#include "game/sys/sdl_events.h"
#include "hook/game.h"

namespace Sio {

namespace {

using DebugLog::Level;

// --- settings -----------------------------------------------------------------------------------------------

struct KeyBinds {
    SDL_Scancode menu;
    SDL_Scancode cameraToggle;
    SDL_Scancode cameraSwivelLock;
    SDL_Scancode focus;
    SDL_Scancode caveman;

    SDL_Scancode grind;
    SDL_Scancode grab;
    SDL_Scancode ollie;
    SDL_Scancode kick;

    SDL_Scancode leftSpin;
    SDL_Scancode rightSpin;
    SDL_Scancode nollie;
    SDL_Scancode switchRevert;

    SDL_Scancode right;
    SDL_Scancode left;
    SDL_Scancode up;
    SDL_Scancode down;
    SDL_Scancode feeble;

    SDL_Scancode cameraRight;
    SDL_Scancode cameraLeft;
    SDL_Scancode cameraUp;
    SDL_Scancode cameraDown;

    SDL_Scancode itemRight;
    SDL_Scancode itemLeft;
    SDL_Scancode itemUp;
    SDL_Scancode itemDown;
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
    PadButton cameraToggle;
    PadButton cameraSwivelLock;
    PadButton focus;
    PadButton caveman;

    PadButton grind;
    PadButton grab;
    PadButton ollie;
    PadButton kick;

    PadButton leftSpin;
    PadButton rightSpin;
    PadButton nollie;
    PadButton switchRevert;

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
bool s_usingKeyboard = true;

// --- the game's state ---------------------------------------------------------------------------------------

// The on-screen keyboard (text entry) is up.
bool KeyboardOnScreen() { return GameVariable<uint8_t>(0x007CE46E) != 0; }
uint8_t InMenuFlag() { return GameVariable<uint8_t>(0x007CE46F); }

// --- controllers and players ----------------------------------------------------------------------------------

// The open controllers (a list that only grows; removed ones shift the rest down).
int s_controllerCount;
int s_controllerCapacity;
SDL_GameController** s_controllers;

struct PlayerSlot {
    SDL_GameController* controller;
    uint8_t lockedOut; // after joining: locked out until all of its buttons are up (counts buttons down)
};
constexpr int kMaxPlayers = 2;
uint8_t s_playerCount = 0;
PlayerSlot s_players[kMaxPlayers] = {{nullptr, 0}, {nullptr, 0}};

void AddController(int index)
{
    DebugLog::Printf(Level::kInfo, "Detected controller \"%s\"\n", SDL_GameControllerNameForIndex(index));
    SDL_GameController* controller = SDL_GameControllerOpen(index);
    SDL_GameControllerSetPlayerIndex(controller, -1);
    if (!controller)
        return;
    if (s_controllerCount == s_controllerCapacity) {
        int capacity = s_controllerCapacity + 1;
        auto* grown = static_cast<SDL_GameController**>(
            std::realloc(s_controllers, sizeof(SDL_GameController*) * capacity));
        if (!grown)
            return;
        s_controllerCapacity = capacity;
        s_controllers = grown;
    }
    s_controllers[s_controllerCount++] = controller;
}

void AddPlayer(SDL_GameController* controller)
{
    if (s_playerCount >= 2) {
        DebugLog::Printf(Level::kInfo, "Already two players, not adding\n");
        return;
    }
    int slot = 0;
    while (slot < kMaxPlayers && s_players[slot].controller)
        ++slot;
    if (slot == kMaxPlayers)
        return;
    SDL_GameControllerSetPlayerIndex(controller, slot);
    s_players[slot].controller = controller;
    if (s_playerCount > 0) {
        s_players[slot].lockedOut = 1;
        SDL_JoystickRumble(SDL_GameControllerGetJoystick(controller), 0x7FFF, 0x7FFF, 250);
    }
    ++s_playerCount;
    DebugLog::Printf(Level::kInfo, "Added player %d: %s\n", slot + 1, SDL_GameControllerName(controller));
}

void PrunePlayers()
{
    for (int i = 0; i < kMaxPlayers; ++i) {
        if (s_players[i].controller && !SDL_GameControllerGetAttached(s_players[i].controller)) {
            DebugLog::Printf(Level::kInfo, "Pruned player %d\n", i + 1);
            s_players[i].controller = nullptr;
            --s_playerCount;
            DebugLog::Printf(Level::kInfo, "Remaining players: %d\n", s_playerCount);
        }
    }
}

void RemoveController(SDL_GameController* controller)
{
    DebugLog::Printf(Level::kInfo, "Controller \"%s\" disconnected\n", SDL_GameControllerName(controller));
    int i = 0;
    while (i < s_controllerCount && s_controllers[i] != controller)
        ++i;
    // (as in PartyMod: when it isn't found this looks one past the last entry, which can be a stale copy)
    if (s_controllers[i] != controller) {
        DebugLog::Printf(Level::kWarn, "Did not find disconnected controller in list\n");
        return;
    }
    SDL_GameControllerClose(controller);
    int player = SDL_GameControllerGetPlayerIndex(controller);
    if (player != -1) {
        DebugLog::Printf(Level::kInfo, "Removed player %d\n", player + 1);
        s_players[player].controller = nullptr;
        --s_playerCount;
    }
    PrunePlayers();
    for (; i < s_controllerCount - 1; ++i)
        s_controllers[i] = s_controllers[i + 1];
    --s_controllerCount;
}

void InitControllers()
{
    DebugLog::Printf(Level::kInfo, "Initializing Controller Input\n");
    s_controllerCount = 0;
    s_controllerCapacity = 1;
    s_controllers = static_cast<SDL_GameController**>(std::malloc(sizeof(SDL_GameController*) * s_controllerCapacity));
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i))
            AddController(i);
    }
}

// --- reading a controller -------------------------------------------------------------------------------------

// How far a stick axis (0-255, 128 the centre) is from the centre.
uint8_t AxisDistance(uint8_t value)
{
    if (value > 127)
        return value & 0x7F;
    return ~value & 0x7F;
}

uint8_t ButtonDown(SDL_GameController* controller, PadButton button)
{
    if (button == kLeftTrigger) {
        auto pressure = static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        return pressure > 0x80;
    }
    if (button == kRightTrigger) {
        auto pressure = static_cast<uint8_t>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
        return pressure > 0x80;
    }
    return SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button));
}

// A stick into (x, y), where it's further from the centre than what's there (the keyboard's).
void ReadStick(SDL_GameController* controller, PadStick stick, uint8_t* x, uint8_t* y)
{
    uint8_t stickX;
    uint8_t stickY;
    if (stick == kStickLeft) {
        stickX = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) >> 8) + 128);
        stickY = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) >> 8) + 128);
    } else if (stick == kStickRight) {
        stickX = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) >> 8) + 128);
        stickY = static_cast<uint8_t>((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) >> 8) + 128);
    } else {
        stickX = 0x80;
        stickY = 0x80;
    }
    if (AxisDistance(stickX) > AxisDistance(*x))
        *x = stickX;
    if (AxisDistance(stickY) > AxisDistance(*y))
        *y = stickY;
}

// Whether the park editor is up: the screen element 0x5999551E exists (the screen element manager 0x00701440's
// GetElement 0x004AAE20 - it returns a smart pointer, released through 0x004637E0).
uint8_t InParkEditor()
{
    void* manager = GameVariable<void*>(0x00701440);
    if (!manager)
        return 0;
    int element[4] = {0, 0, 0, 0};
    // (thiscall: PartyMod calls them as fastcall with a null EDX, which they don't read)
    auto* found = GameFunction<uint32_t*(__thiscall*)(void*, void*, uint32_t, uint32_t)>(0x004AAE20)(
        manager, element, 0x5999551E, 0);
    if (found && *found) {
        if (element[0])
            GameFunction<void(__thiscall*)(void*, void*)>(0x004637E0)(reinterpret_cast<void*>(element[0]), element);
        return 1;
    }
    return 0;
}

uint8_t s_inMenu = 0;
uint8_t s_editingPark = 0;

// The control data's bytes: 2 and 3 the button bits (set while down here; inverted at the end - the game's
// format has a bit set while a button is up), 4-7 the sticks, 8-19 the pressures, 20 the Xbox's black (bit 0)
// and white (bit 1).
void PollController(Device* device, SDL_GameController* controller)
{
    if (!SDL_GameControllerGetAttached(controller))
        return;
    uint8_t* data = device->m_controlData;
    device->m_valid = 1;
    device->m_pluggedIn = true;

    if (ButtonDown(controller, s_pad.menu))
        data[2] |= 1 << 3;
    if (ButtonDown(controller, s_pad.cameraToggle))
        data[2] |= 1 << 0;
    if (ButtonDown(controller, s_pad.cameraSwivelLock))
        data[2] |= 1 << 2;
    if (ButtonDown(controller, s_pad.focus))
        data[2] |= 1 << 1;

    if (ButtonDown(controller, s_pad.grind)) {
        data[3] |= 1 << 4;
        data[12] = 0xFF;
    }
    if (ButtonDown(controller, s_pad.grab)) {
        data[3] |= 1 << 5;
        data[13] = 0xFF;
    }
    if (ButtonDown(controller, s_pad.ollie)) {
        data[3] |= 1 << 6;
        data[14] = 0xFF;
    }
    if (ButtonDown(controller, s_pad.kick)) {
        data[3] |= 1 << 7;
        data[15] = 0xFF;
    }

    // the shoulders
    if (s_settings.ps2Controls) {
        if (ButtonDown(controller, s_pad.leftSpin)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (ButtonDown(controller, s_pad.rightSpin)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            if (s_editingPark)
                data[20] |= 1 << 0; // black, editing a park
        }
        if (ButtonDown(controller, s_pad.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
            if (s_inMenu)
                data[20] |= 1 << 1; // white, in a menu
        }
        if (ButtonDown(controller, s_pad.switchRevert)) {
            data[3] |= 1 << 1;
            data[19] = 0xFF;
            if (s_inMenu)
                data[20] |= 1 << 0; // black, in a menu
            else if (s_editingPark)
                data[20] |= 1 << 1; // white, editing a park
        }
    } else if (s_editingPark && !KeyboardOnScreen()) {
        // editing a park: the right trigger is L1
        if (ButtonDown(controller, s_pad.switchRevert)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (ButtonDown(controller, s_pad.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (ButtonDown(controller, s_pad.leftSpin))
            data[20] |= 1 << 1;
        if (ButtonDown(controller, s_pad.rightSpin))
            data[20] |= 1 << 0;
    } else {
        if (ButtonDown(controller, s_pad.nollie)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (ButtonDown(controller, s_pad.switchRevert)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            data[3] |= 1 << 1;
            data[19] = 0xFF;
        }
        if (ButtonDown(controller, s_pad.leftSpin))
            data[20] |= 1 << 1;
        if (ButtonDown(controller, s_pad.rightSpin))
            data[20] |= 1 << 0;
    }

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

    ReadStick(controller, s_pad.camera, &data[4], &data[5]);
    ReadStick(controller, s_pad.movement, &data[6], &data[7]);
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
    uint8_t rotateLeft;  // LB / L1
    uint8_t rotateRight; // RB / R1
    uint8_t zoom;        // black (R2 too, rerouted to black)
};
MenuKeys s_menuKeys;

uint8_t s_wasInMenu = 0;
uint8_t s_wasKeyboardOnScreen = 0;

void UpdateMenuKeys(const uint8_t* keys)
{
    uint8_t keyboardOnScreen = GameVariable<uint8_t>(0x007CE46E);
    if (s_inMenu != s_wasInMenu || keyboardOnScreen != s_wasKeyboardOnScreen) {
        if (s_keys.ollie != SDL_SCANCODE_RETURN && s_menuKeys.affirmative == 1)
            s_menuKeys.affirmative = kLockedOut;
        if (s_keys.grab != SDL_SCANCODE_ESCAPE && s_menuKeys.negative == 1)
            s_menuKeys.negative = kLockedOut;
        if (s_keys.kick != SDL_SCANCODE_E && s_menuKeys.menu == 1)
            s_menuKeys.menu = kLockedOut;
        if (s_keys.grind != SDL_SCANCODE_R && s_menuKeys.extra == 1)
            s_menuKeys.extra = kLockedOut;
        if (s_keys.up != SDL_SCANCODE_UP && s_menuKeys.up == 1)
            s_menuKeys.up = kLockedOut;
        if (s_keys.down != SDL_SCANCODE_DOWN && s_menuKeys.down == 1)
            s_menuKeys.down = kLockedOut;
        if (s_keys.left != SDL_SCANCODE_LEFT && s_menuKeys.left == 1)
            s_menuKeys.left = kLockedOut;
        if (s_keys.right != SDL_SCANCODE_RIGHT && s_menuKeys.right == 1)
            s_menuKeys.right = kLockedOut;
        if (s_settings.ps2Controls) {
            if (s_keys.leftSpin != SDL_SCANCODE_1 && s_menuKeys.rotateLeft == 1)
                s_menuKeys.rotateLeft = kLockedOut;
            if (s_keys.rightSpin != SDL_SCANCODE_2 && s_menuKeys.rotateRight == 1)
                s_menuKeys.rotateRight = kLockedOut;
            if (s_keys.switchRevert != SDL_SCANCODE_MINUS && s_menuKeys.zoom == 1)
                s_menuKeys.zoom = kLockedOut;
        } else {
            if (s_keys.nollie != SDL_SCANCODE_1 && s_menuKeys.rotateLeft == 1)
                s_menuKeys.rotateLeft = kLockedOut;
            if (s_keys.switchRevert != SDL_SCANCODE_2 && s_menuKeys.rotateRight == 1)
                s_menuKeys.rotateRight = kLockedOut;
            if (s_keys.rightSpin != SDL_SCANCODE_MINUS && s_menuKeys.zoom == 1)
                s_menuKeys.zoom = kLockedOut;
        }
        s_wasInMenu = s_inMenu;
        s_wasKeyboardOnScreen = keyboardOnScreen;
    }

    // a key's state, except while it's locked out and still down
    auto update = [keys](uint8_t& state, SDL_Scancode key) {
        if (!(state == kLockedOut && keys[key]))
            state = keys[key];
    };
    update(s_menuKeys.affirmative, SDL_SCANCODE_RETURN);
    update(s_menuKeys.negative, SDL_SCANCODE_ESCAPE);
    update(s_menuKeys.menu, SDL_SCANCODE_E);
    update(s_menuKeys.extra, SDL_SCANCODE_R);
    update(s_menuKeys.up, SDL_SCANCODE_UP);
    update(s_menuKeys.down, SDL_SCANCODE_DOWN);
    update(s_menuKeys.left, SDL_SCANCODE_LEFT);
    update(s_menuKeys.right, SDL_SCANCODE_RIGHT);
    update(s_menuKeys.rotateLeft, SDL_SCANCODE_1);
    update(s_menuKeys.rotateRight, SDL_SCANCODE_2);
    update(s_menuKeys.zoom, SDL_SCANCODE_MINUS);
}

// A bound key (its scan code's low byte, as the bindings are passed): a menu key doesn't count as a binding in a
// menu or while locked out.
uint8_t KeyDown(const uint8_t* keys, uint8_t key)
{
    auto menuKey = [keys, key](uint8_t state) -> uint8_t { return state != kLockedOut && (!s_inMenu && keys[key]); };
    switch (key) {
    case SDL_SCANCODE_RETURN: return menuKey(s_menuKeys.affirmative);
    case SDL_SCANCODE_ESCAPE: return menuKey(s_menuKeys.negative);
    case SDL_SCANCODE_E: return menuKey(s_menuKeys.menu);
    case SDL_SCANCODE_R: return menuKey(s_menuKeys.extra);
    case SDL_SCANCODE_UP: return menuKey(s_menuKeys.up);
    case SDL_SCANCODE_DOWN: return menuKey(s_menuKeys.down);
    case SDL_SCANCODE_LEFT: return menuKey(s_menuKeys.left);
    case SDL_SCANCODE_RIGHT: return menuKey(s_menuKeys.right);
    case SDL_SCANCODE_1: return menuKey(s_menuKeys.rotateLeft);
    case SDL_SCANCODE_2: return menuKey(s_menuKeys.rotateRight);
    case SDL_SCANCODE_MINUS: return menuKey(s_menuKeys.zoom);
    default: return keys[key];
    }
}

void PollKeyboard(Device* device)
{
    uint8_t* data = device->m_controlData;
    device->m_valid = 1;
    device->m_pluggedIn = true;

    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    UpdateMenuKeys(keys);
    if (KeyboardOnScreen())
        return;
    // (a binding's scan code is passed as its low byte)
    auto down = [keys](SDL_Scancode key) { return KeyDown(keys, static_cast<uint8_t>(key)) != 0; };
    auto menuDown = [](uint8_t state) { return s_inMenu && state == 1; };

    if (down(s_keys.menu))
        data[2] |= 1 << 3;
    if (down(s_keys.cameraToggle))
        data[2] |= 1 << 0;
    if (down(s_keys.focus))
        data[2] |= 1 << 1;
    if (down(s_keys.cameraSwivelLock))
        data[2] |= 1 << 2;

    if (down(s_keys.grind) || menuDown(s_menuKeys.extra)) {
        data[3] |= 1 << 4;
        data[12] = 0xFF;
    }
    if (down(s_keys.grab) || menuDown(s_menuKeys.negative)) {
        data[3] |= 1 << 5;
        data[13] = 0xFF;
    }
    if (down(s_keys.ollie) || menuDown(s_menuKeys.affirmative)) {
        data[3] |= 1 << 6;
        data[14] = 0xFF;
    }
    if (down(s_keys.kick) || menuDown(s_menuKeys.menu)) {
        data[3] |= 1 << 7;
        data[15] = 0xFF;
    }

    // the shoulders
    if (s_settings.ps2Controls) {
        if (down(s_keys.leftSpin) || menuDown(s_menuKeys.rotateLeft)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (down(s_keys.rightSpin) || menuDown(s_menuKeys.rotateRight)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            if (s_editingPark)
                data[20] |= 1 << 0; // black, editing a park
        }
        if (down(s_keys.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
            if (s_inMenu)
                data[20] |= 1 << 1; // white, in a menu
        }
        if (down(s_keys.switchRevert) || menuDown(s_menuKeys.zoom)) {
            data[3] |= 1 << 1;
            data[19] = 0xFF;
            if (s_inMenu)
                data[20] |= 1 << 0; // black, in a menu
            else if (s_editingPark)
                data[20] |= 1 << 1; // white, editing a park
        }
    } else if (s_editingPark && !KeyboardOnScreen()) {
        // editing a park: the right trigger is L1
        if (down(s_keys.switchRevert)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
        }
        if (down(s_keys.nollie)) {
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (down(s_keys.leftSpin))
            data[20] |= 1 << 1;
        if (down(s_keys.rightSpin))
            data[20] |= 1 << 0;
    } else {
        if (down(s_keys.nollie) || menuDown(s_menuKeys.rotateLeft)) {
            data[3] |= 1 << 2;
            data[16] = 0xFF;
            data[3] |= 1 << 0;
            data[18] = 0xFF;
        }
        if (down(s_keys.switchRevert) || menuDown(s_menuKeys.rotateRight)) {
            data[3] |= 1 << 3;
            data[17] = 0xFF;
            data[3] |= 1 << 1;
            data[19] = 0xFF;
        }
        if (down(s_keys.leftSpin))
            data[20] |= 1 << 1;
        if (down(s_keys.rightSpin) || menuDown(s_menuKeys.zoom))
            data[20] |= 1 << 0;
    }

    // the d-pad: the item keys
    if (down(s_keys.itemUp) || menuDown(s_menuKeys.up)) {
        data[2] |= 1 << 4;
        data[10] = 0xFF;
    }
    if (down(s_keys.itemRight) || menuDown(s_menuKeys.right)) {
        data[2] |= 1 << 5;
        data[8] = 0xFF;
    }
    if (down(s_keys.itemDown) || menuDown(s_menuKeys.down)) {
        data[2] |= 1 << 6;
        data[11] = 0xFF;
    }
    if (down(s_keys.itemLeft) || menuDown(s_menuKeys.left)) {
        data[2] |= 1 << 7;
        data[9] = 0xFF;
    }

    // the sticks: opposite keys together cancel out
    if (down(s_keys.cameraLeft) && !down(s_keys.cameraRight))
        data[4] = 0;
    if (down(s_keys.cameraRight) && !down(s_keys.cameraLeft))
        data[4] = 255;
    if (down(s_keys.cameraUp) && !down(s_keys.cameraDown))
        data[5] = 0;
    if (down(s_keys.cameraDown) && !down(s_keys.cameraUp))
        data[5] = 255;
    if (down(s_keys.left) && !down(s_keys.right))
        data[6] = 0;
    if (down(s_keys.right) && !down(s_keys.left))
        data[6] = 255;
    if (down(s_keys.up) && !down(s_keys.down))
        data[7] = 0;
    if (down(s_keys.down) && !down(s_keys.up))
        data[7] = 255;
}

// --- typed keys -----------------------------------------------------------------------------------------------

// A key press: with the on-screen keyboard up, Enter / Backspace / Escape / keypad Enter as typed characters;
// otherwise Enter and F1-F4 as the game's special keys (0x19, 0x1F, 0x1C-0x1E: the chat's F1...).
void KeyPressed(SDL_Keycode key)
{
    if (!KeyboardOnScreen()) {
        if (key == SDLK_RETURN)
            KeyInput(0x19, 0);
        else if (key == SDLK_F1)
            KeyInput(0x1F, 0);
        else if (key == SDLK_F2)
            KeyInput(0x1C, 0);
        else if (key == SDLK_F3)
            KeyInput(0x1D, 0);
        else if (key == SDLK_F4)
            KeyInput(0x1E, 0);
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
        KeyInput(typed, 0);
}

// Typed text, one character at a time, while the on-screen keyboard is up.
void TextTyped(const char* text)
{
    if (!KeyboardOnScreen())
        return;
    if (std::strlen(text) == 1)
        KeyInput(text[0], 0);
    else
        DebugLog::Printf(Level::kDebug, "Input text '%s' > 1 byte!!\n", text);
}

void HandleInputEvent(SDL_Event* event)
{
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (SDL_IsGameController(event->cdevice.which))
            AddController(event->cdevice.which);
        else
            DebugLog::Printf(Level::kInfo, "Not a game controller: %s\n", SDL_JoystickNameForIndex(event->cdevice.which));
        return;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which))
            RemoveController(controller);
        return;
    case SDL_JOYDEVICEADDED:
        DebugLog::Printf(Level::kInfo, "Joystick added: %s\n", SDL_JoystickNameForIndex(event->jdevice.which));
        return;
    case SDL_KEYDOWN:
        s_usingKeyboard = true;
        KeyPressed(event->key.keysym.sym);
        return;
    case SDL_CONTROLLERBUTTONDOWN: {
        // (the event's controller, read through cdevice: the same place as cbutton's)
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player == -1)
            AddPlayer(controller);
        else if (s_players[player].lockedOut)
            ++s_players[player].lockedOut;
        s_usingKeyboard = false;
        return;
    }
    case SDL_CONTROLLERBUTTONUP: {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        int player = SDL_GameControllerGetPlayerIndex(controller);
        if (player != -1 && s_players[player].lockedOut)
            --s_players[player].lockedOut;
        return;
    }
    case SDL_CONTROLLERAXISMOTION:
        s_usingKeyboard = false;
        return;
    case SDL_TEXTINPUT:
        TextTyped(event->text.text);
        return;
    default:
        return;
    }
}

// --- the game's key bindings ------------------------------------------------------------------------------------

// SDL scan codes -> DirectInput key codes (-1: none).
const uint32_t kDirectInputKeys[256] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x1E, 0x30, 0x2E, 0x20, // 0..7
    0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,                         // 8..15
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,                         // 16..23
    0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03,                         // 24..31
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                         // 32..39
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,                         // 40..47
    0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34,                         // 48..55
    0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,                         // 56..63
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xFFFFFFFF, 0x46,                   // 64..71
    0xC5, 0xD2, 0xC7, 0xC9, 0xD3, 0xCF, 0xD1, 0xCD,                         // 72..79
    0xCB, 0xD0, 0xC8, 0x45, 0xB5, 0x37, 0x4A, 0x4E,                         // 80..87
    0x9C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47,                         // 88..95
    0x48, 0x49, 0x52, 0x53, 0x2B, 0xDD, 0xDE, 0x8D,                         // 96..103
    0x64, 0x65, 0x66, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 104..111
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 112..119
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 120..127
    0xB0, 0xAE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xB3, 0xFFFFFFFF, 0xFFFFFFFF, // 128..135
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
    0x1D, 0x2A, 0x38, 0xDB, 0x9D, 0x36, 0xB8, 0xDC,                         // 224..231
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 232..239
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 240..247
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, // 248..255
};

int32_t DirectInputKey(SDL_Scancode scanCode)
{
    if (static_cast<int>(scanCode) < 256)
        return static_cast<int32_t>(kDirectInputKeys[scanCode]);
    return -1;
}

// The bindings in the game's table (0x007D6770, port 0's actions), for the menus' key hints.
void WriteGameKeyBindings()
{
    auto* binds = &GameVariable<int32_t>(0x007D6770);
    binds[0] = DirectInputKey(s_keys.up);
    binds[1] = DirectInputKey(s_keys.down);
    binds[2] = DirectInputKey(s_keys.left);
    binds[3] = DirectInputKey(s_keys.right);

    binds[4] = DirectInputKey(s_keys.cameraUp);
    binds[5] = DirectInputKey(s_keys.cameraDown);
    binds[6] = DirectInputKey(s_keys.cameraLeft);
    binds[7] = DirectInputKey(s_keys.cameraRight);

    binds[8] = DirectInputKey(s_keys.ollie);
    binds[9] = DirectInputKey(s_keys.grab);
    binds[10] = DirectInputKey(s_keys.kick);
    binds[11] = DirectInputKey(s_keys.grind);

    binds[13] = DirectInputKey(s_keys.cameraToggle);
    binds[14] = DirectInputKey(s_keys.leftSpin);
    binds[15] = DirectInputKey(s_keys.rightSpin);
    binds[16] = DirectInputKey(s_keys.nollie);
    binds[17] = DirectInputKey(s_keys.switchRevert);
    binds[18] = DirectInputKey(s_keys.focus);

    binds[20] = DirectInputKey(s_keys.itemUp);
    binds[21] = DirectInputKey(s_keys.itemDown);
    binds[22] = DirectInputKey(s_keys.itemLeft);
    binds[23] = DirectInputKey(s_keys.itemRight);
}

// --- reading the settings -----------------------------------------------------------------------------------------

void LoadInputSettings()
{
    s_settings.ps2Controls = Config::GetBool(Config::kMiscellaneous, "UsePS2Controls", true);
    s_settings.uiControls = static_cast<uint8_t>(Config::GetInt(Config::kMiscellaneous, "UIControls", 1));
}

SDL_Scancode Key(const char* name, SDL_Scancode fallback)
{
    return static_cast<SDL_Scancode>(Config::GetInt(Config::kKeybinds, name, fallback));
}

void LoadKeyBinds()
{
    s_keys.menu = Key("Pause", SDL_SCANCODE_RETURN);
    s_keys.cameraToggle = Key("ViewToggle", SDL_SCANCODE_F);
    s_keys.cameraSwivelLock = Key("SwivelLock", SDL_SCANCODE_GRAVE);
    s_keys.focus = Key("Focus", SDL_SCANCODE_KP_ENTER);
    s_keys.caveman = Key("Caveman", SDL_SCANCODE_E);

    s_keys.grind = Key("Grind", SDL_SCANCODE_KP_8);
    s_keys.grab = Key("Grab", SDL_SCANCODE_KP_6);
    s_keys.ollie = Key("Ollie", SDL_SCANCODE_KP_2);
    s_keys.kick = Key("Flip", SDL_SCANCODE_KP_4);

    s_keys.leftSpin = Key("SpinLeft", SDL_SCANCODE_KP_1);
    s_keys.rightSpin = Key("SpinRight", SDL_SCANCODE_KP_3);
    s_keys.nollie = Key("Nollie", SDL_SCANCODE_KP_7);
    s_keys.switchRevert = Key("Switch", SDL_SCANCODE_KP_9);

    s_keys.right = Key("Right", SDL_SCANCODE_D);
    s_keys.left = Key("Left", SDL_SCANCODE_A);
    s_keys.up = Key("Forward", SDL_SCANCODE_W);
    s_keys.down = Key("Backward", SDL_SCANCODE_S);
    s_keys.feeble = Key("Feeble", SDL_SCANCODE_LSHIFT);

    s_keys.cameraRight = Key("CameraRight", SDL_SCANCODE_KP_MULTIPLY);
    s_keys.cameraLeft = Key("CameraLeft", SDL_SCANCODE_KP_DIVIDE);
    s_keys.cameraUp = Key("CameraUp", SDL_SCANCODE_KP_PLUS);
    s_keys.cameraDown = Key("CameraDown", SDL_SCANCODE_KP_MINUS);

    s_keys.itemRight = Key("ItemRight", SDL_SCANCODE_L);
    s_keys.itemLeft = Key("ItemLeft", SDL_SCANCODE_J);
    s_keys.itemUp = Key("ItemUp", SDL_SCANCODE_I);
    s_keys.itemDown = Key("ItemDown", SDL_SCANCODE_K);
}

PadButton Button(const char* name, int fallback)
{
    return static_cast<PadButton>(Config::GetInt(Config::kGamepad, name, fallback));
}

void LoadPadBinds()
{
    s_pad.menu = Button("Pause", kButtonStart);
    s_pad.cameraToggle = Button("ViewToggle", kButtonBack);
    s_pad.cameraSwivelLock = Button("SwivelLock", kButtonRightStick);
    s_pad.focus = Button("Focus", kButtonLeftStick);
    s_pad.caveman = Button("Caveman", 0);

    s_pad.grind = Button("Grind", kButtonY);
    s_pad.grab = Button("Grab", kButtonB);
    s_pad.ollie = Button("Ollie", kButtonA);
    s_pad.kick = Button("Flip", kButtonX);

    s_pad.leftSpin = Button("SpinLeft", kButtonLeftShoulder);
    s_pad.rightSpin = Button("SpinRight", kButtonRightShoulder);
    s_pad.nollie = Button("Nollie", kLeftTrigger);
    s_pad.switchRevert = Button("Switch", kRightTrigger);

    s_pad.right = Button("Right", kDpadRight);
    s_pad.left = Button("Left", kDpadLeft);
    s_pad.up = Button("Forward", kDpadUp);
    s_pad.down = Button("Backward", kDpadDown);

    s_pad.movement = static_cast<PadStick>(Config::GetInt(Config::kGamepad, "MovementStick", kStickLeft));
    s_pad.camera = static_cast<PadStick>(Config::GetInt(Config::kGamepad, "CameraStick", kStickRight));
}

// Stores 32 bits at `offset` into the device. PartyMod's view of the device has these fields as 32-bit (the game's
// are bytes followed by padding - and +0xEC's second byte is m_keyboardControl, cleared this way); its last one,
// "start or A pressed" at +0xF0, lies just past the end of the 0xF0-byte device. Kept as it was.
void Store32(Device* device, uint32_t offset, uint32_t value)
{
    std::memcpy(reinterpret_cast<uint8_t*>(device) + offset, &value, sizeof(value));
}

} // namespace

InputSettings& TheInputSettings() { return s_settings; }
bool& UsingKeyboard() { return s_usingKeyboard; }

void InitSdlInput()
{
    DebugLog::Printf(Level::kInfo, "Initializing Input Manager!\n");
    SDL_Init(SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "false");

    char mappings[1024];
    int length = sprintf_s(mappings, sizeof(mappings), "%s%s", Config::ExecutableDirectory(), "gamecontrollerdb.txt");
    if (length) {
        // (-1, a failure, counts as loaded too)
        if (SDL_GameControllerAddMappingsFromFile(mappings))
            DebugLog::Printf(Level::kInfo, "Loaded mappings\n");
        else
            DebugLog::Printf(Level::kWarn, "Failed to load %s\n", mappings);
    }

    LoadInputSettings();
    LoadKeyBinds();
    LoadPadBinds();
    InitControllers();
    if (s_settings.ps2Controls)
        Sys::InstallIsPS2();
    WriteGameKeyBindings();
    Platform::RegisterEventHandler(HandleInputEvent);
    // (not PartyMod's - the mouse_menus feature: the mouse's events, when it's on)
    MouseMenus::Init();
}

void ProcessSdlDevice(Device* device)
{
    s_editingPark = InParkEditor();
    s_inMenu = InMenuFlag() && !s_editingPark;

    device->m_capabilities = 3;
    device->m_numActuators = 2;
    device->m_actuatorMax[0] = 255;
    device->m_actuatorMax[1] = 255;
    device->m_state = Device::kReady;

    Platform::PumpSdlEvents();

    Store32(device, 0x14, 0); // m_valid
    Store32(device, 0xE0, 0); // m_pluggedIn
    uint8_t* data = device->m_controlData;
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

    if (device->m_port == 0) {
        Store32(device, 0x14, 1);
        Store32(device, 0xE0, 1);
        if (!KeyboardOnScreen())
            PollKeyboard(device);
    }
    auto port = static_cast<uint32_t>(device->m_port);
    if (port < kMaxPlayers && s_players[port].controller && !s_players[port].lockedOut)
        PollController(device, s_players[port].controller);

    data[2] = static_cast<uint8_t>(~data[2]);
    data[3] = static_cast<uint8_t>(~data[3]);
    Store32(device, 0xEC, (0xFFFF ^ (data[2] << 8 | data[3])) ? 1 : 0); // m_pressed (and m_keyboardControl)
    Store32(device, 0xF0, ((~data[2] & (1 << 3)) || (~data[3] & (1 << 6))) ? 1 : 0); // start or A (past the end)

    // (typed text only works with these set)
    GameVariable<uint8_t>(0x007CCDF8) = 1;
    GameVariable<uint8_t>(0x007CE46F) = 0;
}

void __cdecl SetSdlActuators(int port, uint16_t high, uint16_t low)
{
    for (int i = 0; i < s_controllerCount; ++i) {
        if (SDL_GameControllerGetAttached(s_controllers[i]) && SDL_GameControllerGetPlayerIndex(s_controllers[i]) == port)
            SDL_JoystickRumble(SDL_GameControllerGetJoystick(s_controllers[i]), low, high, 0);
    }
}

} // namespace Sio
