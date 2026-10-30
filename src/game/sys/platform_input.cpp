// The Win32 platform layer's DirectInput side: see platform_input.h.

#include "game/sys/platform_input.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "game/sio/device.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/settings.h"
#include "game/gfx/camera.h"
#include "game/gfx/device.h"

#pragma warning(disable : 4996) // (sprintf / wcscpy: the original's own, into its fixed buffers)

namespace Platform {

GAME_GLOBAL(IDirectInput8A*, g_directInput, 0x007D69D0);
GAME_GLOBAL(IDirectInputDevice8A*, g_keyboardDevice, 0x007D69D4);

// The key bindings (0x98 actions: -1 for none; the menus' hints and the pads' keyboard actions read them).
GAME_GLOBAL(int32_t[0x98], g_keyBindings, 0x007D6770);
GAME_GLOBAL(IDirectInputDevice8A*[kPorts], g_joysticks, 0x007D6318);
GAME_GLOBAL(IDirectInputEffect*[kPorts], g_effects, 0x007D6668); // each joystick's constant-force effect (rumble)
GAME_GLOBAL(uint32_t[kPorts], g_forceAxisCounts, 0x007D42C8);
GAME_GLOBAL(uint32_t[2 * kPorts], g_forceResolutions, 0x007D42D0);
constexpr int kKeyNameLength = 15;
GAME_GLOBAL(wchar_t[256][kKeyNameLength], g_keyNames, 0x007D42F8);
GAME_GLOBAL(uint8_t[256], g_keys, 0x007D6670);
GAME_GLOBAL(uint8_t[256], g_lastKeys, 0x007D6320);
// The joysticks' states: this frame's and the last.
GAME_GLOBAL(DIJOYSTATE2[2], g_joystickStates, 0x007D60F8);
GAME_GLOBAL(DIJOYSTATE2[2], g_lastJoystickStates, 0x007D6448);

namespace {

// DirectInput and its devices (the keyboard and mouse are the first of each found).
IDirectInput8A*& DirectInput() { return g_directInput; }
IDirectInputDevice8A*& Keyboard() { return g_keyboardDevice; }

GAME_GLOBAL(int, g_defaultEscapeControl, 0x0068E244, 1);
GAME_GLOBAL(DIMOUSESTATE2, g_lastMouseState, 0x007D42E0);
GAME_GLOBAL(DIMOUSESTATE2, g_mouseState, 0x007D6420);
GAME_GLOBAL(IDirectInputDevice8A*, g_mouse, 0x007D69D8);
GAME_GLOBAL(int, g_padModeSetting, 0x007D6A3C);
GAME_GLOBAL(int, g_defaultEnterControl, 0x007D6A40);
GAME_GLOBAL(uint8_t, g_mouseInvertX, 0x007D6A44);
GAME_GLOBAL(uint8_t, g_mouseInvertY, 0x007D6A45);

IDirectInputDevice8A*& Mouse() { return g_mouse; }
IDirectInputDevice8A** Joysticks() { return g_joysticks; }
// Each joystick's constant-force effect (rumble: SetActuators).
IDirectInputEffect** Effects() { return g_effects; }

// The devices' states: this frame's and the last.
DIJOYSTATE2* JoystickStates() { return g_joystickStates; }
DIJOYSTATE2* LastJoystickStates() { return g_lastJoystickStates; }
uint8_t* Keys() { return g_keys; }
uint8_t* LastKeys() { return g_lastKeys; }
DIMOUSESTATE2& MouseState() { return g_mouseState; }
DIMOUSESTATE2& LastMouseState() { return g_lastMouseState; }

// Per joystick: how many force-feedback axes it has (at most two) and their force resolutions (two each).
uint32_t* ForceAxisCounts() { return g_forceAxisCounts; }
uint32_t* ForceResolutions() { return g_forceResolutions; }

wchar_t (*KeyNames())[kKeyNameLength] { return g_keyNames; }

HWND GameWindow() { return Nx::g_gameWindow; }

// dinput8.lib's data formats and the constant-force effect's GUID, as the exe has them.
const DIDATAFORMAT* JoystickFormat() { return reinterpret_cast<const DIDATAFORMAT*>(0x00667204); } // c_dfDIJoystick2
const DIDATAFORMAT* KeyboardFormat() { return reinterpret_cast<const DIDATAFORMAT*>(0x00666FFC); } // c_dfDIKeyboard
const DIDATAFORMAT* MouseFormat() { return reinterpret_cast<const DIDATAFORMAT*>(0x00666DF4); }    // c_dfDIMouse2
const GUID& ConstantForceGuid() { return *reinterpret_cast<const GUID*>(0x00666B54); }             // GUID_ConstantForce

// Any of `count` controls set (not -1).
bool AnyBinding(const int32_t* controls, int count)
{
    for (int i = 0; i < count; ++i) {
        if (controls[i] != -1)
            return true;
    }
    return false;
}

// "pad<n>": a GUID as text - XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX - parsed digit group by digit group: the
// fourth group's last four digits are copied out first (the group runs on into the last one otherwise). A
// single character (or none) gives the zero GUID.
GUID ParseJoystickGuid(const char* text, DWORD size)
{
    GUID guid{};
    if (size <= 1)
        return guid;
    const char* cursor = text;
    guid.Data1 = ParseHex(&cursor);
    ++cursor;
    guid.Data2 = static_cast<uint16_t>(ParseHex(&cursor));
    ++cursor;
    guid.Data3 = static_cast<uint16_t>(ParseHex(&cursor));
    ++cursor;
    uint32_t value = ParseHex(&cursor);
    guid.Data4[0] = static_cast<uint8_t>(value >> 8);
    guid.Data4[1] = static_cast<uint8_t>(value);
    const char* group = cursor + 1;
    char four[5];
    std::memcpy(four, group, 4);
    four[4] = 0;
    const char* fourCursor = four;
    value = ParseHex(&fourCursor);
    guid.Data4[2] = static_cast<uint8_t>(value >> 8);
    guid.Data4[3] = static_cast<uint8_t>(value);
    cursor = group + 4;
    value = ParseHex(&cursor);
    guid.Data4[4] = static_cast<uint8_t>(value >> 24);
    guid.Data4[5] = static_cast<uint8_t>(value >> 16);
    guid.Data4[6] = static_cast<uint8_t>(value >> 8);
    guid.Data4[7] = static_cast<uint8_t>(value);
    return guid;
}

// "distance": 1-100, the clipping distance from it; anything above 100 is none.
void SetDistance(uint32_t distance)
{
    g_distanceSetting = distance;
    if (distance > 100)
        g_distanceSetting = 0;
    else if (distance != 0)
        Gfx::g_drawDistanceSetting = static_cast<int32_t>(static_cast<double>(distance) * 5.0f + 95.0f);
}

struct KeyNameEntry {
    uint8_t code;
    const wchar_t* name;
};

// The keyboard's names, in the order the original sets them (DIK codes).
const KeyNameEntry kKeyNames[] = {
    {0x1E, L"A"},        {0x30, L"B"},        {0x2E, L"C"},      {0x20, L"D"},      {0x12, L"E"},
    {0x21, L"F"},        {0x22, L"G"},        {0x23, L"H"},      {0x17, L"I"},      {0x24, L"J"},
    {0x25, L"K"},        {0x26, L"L"},        {0x32, L"M"},      {0x31, L"N"},      {0x18, L"O"},
    {0x19, L"P"},        {0x10, L"Q"},        {0x13, L"R"},      {0x1F, L"S"},      {0x14, L"T"},
    {0x16, L"U"},        {0x2F, L"V"},        {0x11, L"W"},      {0x2D, L"X"},      {0x15, L"Y"},
    {0x2C, L"Z"},        {0x0B, L"0"},        {0x02, L"1"},      {0x03, L"2"},      {0x04, L"3"},
    {0x05, L"4"},        {0x06, L"5"},        {0x07, L"6"},      {0x08, L"7"},      {0x09, L"8"},
    {0x0A, L"9"},        {0x01, L"Esc"},      {0x0C, L"Minus"},  {0x0D, L"Equals"}, {0x0E, L"Back"},
    {0x0F, L"Tab"},      {0x1A, L"["},        {0x1B, L"]"},      {0x1C, L"Return"}, {0x1D, L"LCtrl"},
    {0x27, L";"},        {0x28, L"'"},        {0x29, L"`"},      {0x2A, L"LShift"}, {0x2B, L"\\"},
    {0x33, L","},        {0x34, L"."},        {0x35, L"/"},      {0x36, L"RShift"}, {0x37, L"*"},
    {0x38, L"LAlt"},     {0xB8, L"RAlt"},     {0x39, L"Spacebar"}, {0x3A, L"Capslock"}, {0x3B, L"F1"},
    {0x3C, L"F2"},       {0x3D, L"F3"},       {0x3E, L"F4"},     {0x3F, L"F5"},     {0x40, L"F6"},
    {0x41, L"F7"},       {0x42, L"F8"},       {0x43, L"F9"},     {0x44, L"F10"},    {0x57, L"F11"},
    {0x58, L"F12"},      {0x64, L"F13"},      {0x65, L"F14"},    {0x66, L"F15"},    {0x45, L"Numlock"},
    {0x46, L"Scroll"},   {0x47, L"KP 7"},     {0x48, L"KP 8"},   {0x49, L"KP 9"},   {0x4A, L"KP -"},
    {0x4B, L"KP 4"},     {0x4C, L"KP 5"},     {0x4D, L"KP 6"},   {0x4E, L"KP +"},   {0x4F, L"KP 1"},
    {0x50, L"KP 2"},     {0x51, L"KP 3"},     {0x52, L"KP 0"},   {0x53, L"KP ."},   {0xB3, L"KP ,"},
    {0xB5, L"KP /"},     {0x9C, L"Enter"},    {0x9D, L"RCtrl"},  {0xC5, L"Pause"},  {0xC7, L"Home"},
    {0xC8, L"Up"},       {0xC9, L"PgUp"},     {0xCB, L"Left"},   {0xCD, L"Right"},  {0xCF, L"End"},
    {0xD0, L"Down"},     {0xD1, L"PgDown"},   {0xD2, L"Insert"}, {0xD3, L"Delete"},
};

} // namespace

// 0x005F2E00
DIJOYSTATE2* ReadJoystick(int port)
{
    if (!Joysticks()[port])
        return nullptr;
    if (FAILED(Joysticks()[port]->Poll())) {
        if (FAILED(Joysticks()[port]->Acquire()) || FAILED(Joysticks()[port]->Poll()))
            return nullptr;
    }
    IDirectInputDevice8A* joystick = Joysticks()[port];
    DIJOYSTATE2* state = &JoystickStates()[port];
    LastJoystickStates()[port] = *state;
    if (FAILED(joystick->GetDeviceState(sizeof(DIJOYSTATE2), state)))
        return nullptr;
    return state;
}

// 0x005F2E90
DIMOUSESTATE2* ReadMouse()
{
    if (!Mouse())
        return nullptr;
    if (FAILED(Mouse()->Poll())) {
        if (FAILED(Mouse()->Acquire()) || FAILED(Mouse()->Poll()))
            return nullptr;
    }
    DIMOUSESTATE2& state = MouseState();
    DIMOUSESTATE2& last = LastMouseState();
    last.lY = state.lY;
    last.lX = state.lX;
    std::memcpy(&last.lZ, &state.lZ, 3 * sizeof(LONG)); // (z and the first eight buttons)
    HRESULT result = Mouse()->GetDeviceState(sizeof(DIMOUSESTATE2), &state);
    if (state.lX == 0 && state.lY == 0) {
        // No movement this frame: x, then y, keep the last frame's less 2 (none within 2 of 0).
        LONG x = last.lX, y = last.lY;
        if (x != 0 || y != 0) {
            if (x != 0) {
                if (x > 2)
                    state.lX = x - 2;
                else if (x < -2)
                    state.lX = x + 2;
            }
            if (y > 2)
                state.lY = y - 2;
            else if (y < -2)
                state.lY = y + 2;
        }
    }
    return FAILED(result) ? nullptr : &state;
}

// 0x005F2F80
bool HasKeyBindings(int port)
{
    return AnyBinding(Controls()[port].keys, kBindings);
}

// 0x005F2FC0
bool HasMouseBindings(int port)
{
    return AnyBinding(Controls()[port].mouse, kBindings);
}

// 0x005F3000
int JoystickAxis(const DIJOYSTATE2* state, int control)
{
    switch (control) {
    case 1:
        return state->lX;
    case 2:
        return state->lY;
    case 3:
        return state->lZ;
    case 4: { // the hat (hundredths of a degree clockwise from up): right 22.5-157.5, left 202.5-337.5
        DWORD hat = state->rgdwPOV[0];
        if (hat == 0xFFFFFFFF)
            return 0;
        if (hat >= 2250 && hat <= 15750)
            return 1000;
        if (hat >= 20250 && hat <= 33750)
            return -1000;
        return 0;
    }
    case 5: { // up 292.5-67.5, down 112.5-247.5
        DWORD hat = state->rgdwPOV[0];
        if (hat == 0xFFFFFFFF)
            return 0;
        if (hat >= 29250 || hat <= 6750)
            return -1000;
        if (hat >= 11250 && hat <= 24750)
            return 1000;
        return 0;
    }
    case 6:
        return state->lRx;
    case 7:
        return state->lRy;
    case 8:
        return state->lRz;
    case 9:
        return state->rglSlider[0];
    case 10:
        return state->rglSlider[1];
    default:
        return 0;
    }
}

// 0x005F30E0
bool KeyDown(int code)
{
    if (code == -1)
        return false;
    if (code == DIK_ESCAPE)
        return Keys()[DIK_ESCAPE] && !LastKeys()[DIK_ESCAPE];
    return Keys()[code] != 0;
}

// 0x005F3120
wchar_t* KeyName(int code)
{
    return KeyNames()[code];
}

// 0x005F3130
BOOL CALLBACK AddJoystickAxis(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID device)
{
    auto* joystick = static_cast<IDirectInputDevice8A*>(device);
    DIPROPRANGE range;
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = object->dwType;
    range.diph.dwHow = DIPH_BYID;
    range.lMin = -1000;
    range.lMax = 1000;
    joystick->SetProperty(DIPROP_RANGE, &range.diph);
    int port = Joysticks()[1] == joystick ? 1 : 0;
    if ((object->dwFlags & DIDOI_FFACTUATOR) && ForceAxisCounts()[port] < 2) {
        uint32_t count = ForceAxisCounts()[port];
        ForceResolutions()[2 * port + count] = object->dwFFForceResolution;
        ForceAxisCounts()[port] = count + 1;
    }
    return DIENUM_CONTINUE;
}

// 0x005F31D0
BOOL CALLBACK AddInputDevice(LPCDIDEVICEINSTANCEA device, LPVOID /*context*/)
{
    DWORD type = device->dwDevType & 0xFF;
    IDirectInputDevice8A* created;
    if (type == DI8DEVTYPE_GAMEPAD || type == DI8DEVTYPE_JOYSTICK) {
        int port = -1;
        for (int i = 0; i < kPorts; ++i) {
            if (std::memcmp(&Controls()[i].joystick, &device->guidInstance, sizeof(GUID)) == 0)
                port = i;
        }
        if (port < 0)
            return DIENUM_CONTINUE;
        if (SUCCEEDED(DirectInput()->CreateDevice(device->guidInstance, &created, nullptr))) {
            Joysticks()[port] = created;
            created->SetDataFormat(JoystickFormat());
            created->SetCooperativeLevel(GameWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
            created->EnumObjects(AddJoystickAxis, created, DIDFT_AXIS);
            if (ForceAxisCounts()[port] > 0) {
                // Rumble: an endless constant force along X and Y, started by SetActuators.
                DWORD axes[2] = {DIJOFS_X, DIJOFS_Y};
                LONG direction[2] = {0, 0};
                DICONSTANTFORCE force = {0};
                DIEFFECT effect = {};
                effect.dwSize = sizeof(DIEFFECT);
                effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
                effect.dwDuration = INFINITE;
                effect.dwSamplePeriod = 0;
                effect.dwGain = DI_FFNOMINALMAX;
                effect.dwTriggerButton = DIEB_NOTRIGGER;
                effect.dwTriggerRepeatInterval = 0;
                effect.cAxes = 2;
                effect.rgdwAxes = axes;
                effect.rglDirection = direction;
                effect.lpEnvelope = nullptr;
                effect.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
                effect.lpvTypeSpecificParams = &force;
                effect.dwStartDelay = 0;
                created->CreateEffect(ConstantForceGuid(), &effect, &Effects()[port], nullptr);
            }
            created->Acquire();
        }
    }
    if (type == DI8DEVTYPE_KEYBOARD) {
        if (!Keyboard() && SUCCEEDED(DirectInput()->CreateDevice(device->guidInstance, &created, nullptr))) {
            Keyboard() = created;
            created->SetDataFormat(KeyboardFormat());
            created->SetCooperativeLevel(GameWindow(), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
            created->Acquire();
        }
    } else if (type == DI8DEVTYPE_MOUSE) {
        if (!Mouse() && SUCCEEDED(DirectInput()->CreateDevice(device->guidInstance, &created, nullptr))) {
            Mouse() = created;
            created->SetDataFormat(MouseFormat());
            created->SetCooperativeLevel(GameWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
            created->Acquire();
        }
    }
    return DIENUM_CONTINUE;
}

// 0x005F3400
void ReadKeyboard()
{
    IDirectInputDevice8A* keyboard = Keyboard();
    std::memcpy(LastKeys(), Keys(), 256);
    HRESULT result = keyboard->GetDeviceState(256, Keys());
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED)
        return;
    if (SUCCEEDED(Keyboard()->Acquire()))
        Keyboard()->GetDeviceState(256, Keys());
    else
        std::memset(Keys(), 0, 256);
}

// 0x005F3470
uint32_t ParseHex(const char** cursor)
{
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    uint32_t value = 0;
    while (digit(**cursor) || (**cursor >= 'A' && **cursor <= 'F')) {
        char c = *(*cursor)++;
        value = value * 16 + static_cast<uint32_t>(c - (digit(c) ? '0' : 'A' - 10));
    }
    return value;
}

// 0x005F34D0
void LoadRegistryOptions(const char* key)
{
    HKEY options;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &options) != ERROR_SUCCESS)
        return;
    DWORD type, value;
    // (The size starts at 1, so the first read - "distance" - always fails, setting it to 4: "distance" is
    // read again after "options".)
    DWORD size = 1;
    if (RegQueryValueExA(options, "distance", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
        ERROR_SUCCESS)
        SetDistance(value);
    if (RegQueryValueExA(options, "options", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
        ERROR_SUCCESS) {
        if (value & 1)
            Nx::g_fogSetting = true;
        if (value & 2)
            Nx::g_hqShadows = 1;
        if (value & 4)
            reinterpret_cast<uint8_t*>(&g_options38And39)[0] = 1;
        if (value & 8)
            reinterpret_cast<uint8_t*>(&g_options38And39)[1] = 1;
        if (value & 0x10)
            g_option37 = 1;
        if (value & 0x20)
            reinterpret_cast<uint8_t&>(Nx::g_antialiasing) = 1;
        if (value & 0x100)
            Nx::g_distanceClipping = true;
    }
    if (RegQueryValueExA(options, "distance", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
        ERROR_SUCCESS)
        SetDistance(value);
    if (RegQueryValueExA(options, "resolution", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
        ERROR_SUCCESS)
        Nx::g_resolutionSetting = value > 3 ? 0 : value;

    // The joystick each port plays with (the text buffer isn't cleared before the first: a short value leaves
    // whatever was there after it).
    char text[0x400];
    size = sizeof(text);
    if (RegQueryValueExA(options, "pad0", nullptr, &type, reinterpret_cast<BYTE*>(text), &size) == ERROR_SUCCESS)
        Controls()[0].joystick = ParseJoystickGuid(text, size);
    std::memset(text, 0, sizeof(text));
    size = sizeof(text);
    if (RegQueryValueExA(options, "pad1", nullptr, &type, reinterpret_cast<BYTE*>(text), &size) == ERROR_SUCCESS)
        Controls()[1].joystick = ParseJoystickGuid(text, size);

    // Each port's keys and joystick controls (the ones not there stay as they are).
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            std::sprintf(text, "k%i_%i", port, i);
            size = 4;
            if (RegQueryValueExA(options, text, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
                ERROR_SUCCESS)
                Controls()[port].keys[i] = static_cast<int32_t>(value);
        }
    }
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            std::sprintf(text, "gp%i_%i", port, i);
            size = 4;
            if (RegQueryValueExA(options, text, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
                ERROR_SUCCESS)
                Controls()[port].gamepad[i] = static_cast<int32_t>(value);
        }
    }
    RegCloseKey(options);
}

// 0x005F39E0
void ReleaseInput()
{
    for (int port = 0; port < kPorts; ++port) {
        if (Joysticks()[port]) {
            Joysticks()[port]->Release();
            Joysticks()[port] = nullptr;
        }
        if (Effects()[port])
            Effects()[port]->Release(); // (left set)
    }
    if (Keyboard()) {
        Keyboard()->Release();
        Keyboard() = nullptr;
    }
    // (The mouse isn't released.)
    if (DirectInput()) {
        DirectInput()->Release();
        DirectInput() = nullptr;
    }
}

// 0x005F3BF0
wchar_t* SetKeyName(int code, const wchar_t* name)
{
    return std::wcscpy(KeyNames()[code], name);
}

// 0x005F3C10
void InitKeyNames()
{
    for (int code = 0; code < 256; ++code)
        std::wcscpy(KeyNames()[code], L"(NONE)");
    for (const KeyNameEntry& key : kKeyNames)
        std::wcscpy(KeyNames()[key.code], key.name);
}

// --- the pad read ---

namespace {

// The pad mode and the default keys' Enter and Escape.
int& PadModeSetting() { return g_padModeSetting; }
int& DefaultEnterControl() { return g_defaultEnterControl; }
int& DefaultEscapeControl() { return g_defaultEscapeControl; }
// The mouse's x / y sticks reversed (nothing sets them).
bool MouseInvertX() { return g_mouseInvertX != 0; }
bool MouseInvertY() { return g_mouseInvertY != 0; }

constexpr float kStickMax = 32767.0f;                 // 0x0065BF80
constexpr float kStickMin = -32768.0f;                // 0x00666B00
constexpr float kMouseStickScale = 6553.39990234375f; // 0x00666B04
constexpr float kJoystickStickScale = 0.001f;         // 0x006461D8
constexpr float kStickMaxReversed = -32767.0f;        // 0x00666B30 (the joystick's y sticks: up is negative)
constexpr int16_t kStickFull = 32767;

// The original's _ftol: truncated toward zero.
int32_t Truncate(double value) { return static_cast<int32_t>(value); }

// A mouse axis as a stick: its movement scaled, within the stick's range.
double MouseAxisStick(const DIMOUSESTATE2* state, int control)
{
    LONG value = control == 1 ? state->lX : control == 2 ? state->lY : control == 3 ? state->lZ : 0;
    double stick = static_cast<double>(value) * kMouseStickScale;
    if (stick > kStickMax)
        return kStickMax;
    if (stick < kStickMin)
        return kStickMin;
    return stick;
}

// One of the pad's eight pressure buttons held.
void Press(Sio::PadState* pad, int button) { pad->pressure[button] = 0xFF; }

} // namespace

// 0x005F4280
bool JoystickControlDown(const DIJOYSTATE2* state, int control)
{
    if (control >= 11 && control <= 39)
        return state->rgbButtons[control - 11] != 0;
    if (control >= 1 && control <= 10) {
        int value = JoystickAxis(state, control);
        return value > 200 || value < -200;
    }
    return false;
}

// 0x005F42D0
bool MouseControlDown(const DIMOUSESTATE2* state, int control)
{
    if (control >= 4 && control <= 11)
        return (state->rgbButtons[control - 4] >> 7) != 0;
    if (control >= 1 && control <= 3) {
        LONG value = control == 1 ? state->lX : control == 2 ? state->lY : state->lZ;
        return value > 3 || value < -3;
    }
    return false;
}

// 0x005F4330
double MouseStickValue(const DIMOUSESTATE2* state, int negative, int positive)
{
    double value = 0.0;
    if (negative >= 4 && negative <= 11) {
        if (state->rgbButtons[negative - 4] & 0x80)
            value = -1.0;
    } else if (negative >= 1 && negative <= 3) {
        value = MouseAxisStick(state, negative);
    }
    if (positive >= 4 && positive <= 11)
        return (state->rgbButtons[positive - 4] & 0x80) ? value + 1.0 : value;
    if (positive >= 1 && positive <= 3)
        return MouseAxisStick(state, positive); // (in place of the first)
    return value;
}

// 0x005F4450
double JoystickStickValue(const DIJOYSTATE2* state, int negative, int positive)
{
    double value = 0.0;
    if (negative >= 11 && negative <= 39) {
        if (state->rgbButtons[negative - 11])
            value = -1.0;
    } else if (negative >= 1 && negative <= 10) {
        value = JoystickAxis(state, negative) * static_cast<double>(kJoystickStickScale);
    }
    if (positive >= 11 && positive <= 39)
        return state->rgbButtons[positive - 11] ? value + 1.0 : value;
    if (positive >= 1 && positive <= 10)
        return JoystickAxis(state, positive) * static_cast<double>(kJoystickStickScale); // (in place of the first)
    return value;
}

// 0x005F5BC0
int ReadPad(int port, Sio::PadState* pad)
{
    --port;
    ReadKeyboard();
    DIJOYSTATE2* joystick = ReadJoystick(port);
    DIMOUSESTATE2* mouse = ReadMouse();
    pad->buttons = 0;
    for (int16_t& stick : pad->sticks)
        stick = 0;
    std::memset(pad->pressure, 0, sizeof(pad->pressure));
    if (!joystick && !HasKeyBindings(port) && !HasMouseBindings(port))
        return 0;
    const PortControls& controls = Controls()[port];

    if (mouse && PadModeSetting() == kPadKeyBindings) {
        // The sticks: each mouse axis's movement scaled twice - x 6553.4 within the stick's range, then x 32767 -
        // and only the low 16 bits kept.
        const int32_t* m = controls.mouse;
        auto stick = [&](int index, int negative, int positive, bool reversed) {
            auto value = static_cast<int16_t>(Truncate(MouseStickValue(mouse, negative, positive) * kStickMax));
            if (value != 0)
                pad->sticks[index] = static_cast<int16_t>(reversed ? -value : value);
        };
        stick(0, m[2], m[3], MouseInvertX());
        stick(1, m[0], m[1], MouseInvertY());
        stick(2, m[6], m[7], MouseInvertX());
        stick(3, m[4], m[5], MouseInvertY());
        for (int i = 0; i < 4; ++i) {
            if (MouseControlDown(mouse, m[8 + i]))
                Press(pad, i);
        }
        for (int i = 0; i < 4; ++i) {
            if (MouseControlDown(mouse, m[14 + i]))
                Press(pad, 4 + i);
        }
        for (int i = 0; i < 4; ++i) {
            if (MouseControlDown(mouse, m[20 + i]))
                pad->buttons |= static_cast<uint16_t>(1 << i);
        }
        if (MouseControlDown(mouse, m[12]))
            pad->buttons |= 0x10;
        if (MouseControlDown(mouse, m[13]))
            pad->buttons |= 0x20;
        if (MouseControlDown(mouse, m[18]))
            pad->buttons |= 0x40;
        if (MouseControlDown(mouse, m[19]))
            pad->buttons |= 0x80;
    }

    if (joystick) {
        // The sticks (a larger push than the mouse's wins), the pressure buttons, the d-pad (axes only: a button
        // there never counts) and the other buttons.
        const int32_t* g = controls.gamepad;
        auto stick = [&](int index, int negative, int positive, float scale) {
            auto value = static_cast<int16_t>(Truncate(JoystickStickValue(joystick, negative, positive) * scale));
            if (value != 0 && std::abs(static_cast<int>(pad->sticks[index])) < std::abs(static_cast<int>(value)))
                pad->sticks[index] = value;
        };
        stick(0, g[2], g[3], kStickMax);
        stick(1, g[0], g[1], kStickMaxReversed);
        stick(2, g[6], g[7], kStickMax);
        stick(3, g[4], g[5], kStickMaxReversed);
        for (int i = 0; i < 4; ++i) {
            if (JoystickControlDown(joystick, g[8 + i]))
                Press(pad, i);
        }
        for (int i = 0; i < 4; ++i) {
            if (JoystickControlDown(joystick, g[14 + i]))
                Press(pad, 4 + i);
        }
        if (JoystickAxis(joystick, g[20]) < -200)
            pad->buttons |= 1;
        if (JoystickAxis(joystick, g[21]) > 200)
            pad->buttons |= 2;
        if (JoystickAxis(joystick, g[22]) < -200)
            pad->buttons |= 4;
        if (JoystickAxis(joystick, g[23]) > 200)
            pad->buttons |= 8;
        if (JoystickControlDown(joystick, g[12]))
            pad->buttons |= 0x10;
        if (JoystickControlDown(joystick, g[13]))
            pad->buttons |= 0x20;
        if (JoystickControlDown(joystick, g[18]))
            pad->buttons |= 0x40;
        if (JoystickControlDown(joystick, g[19]))
            pad->buttons |= 0x80;
    }

    int mode = PadModeSetting();
    if (mode == kPadNoKeys)
        return 0;
    auto stick = [&](int index, int key, int16_t value) {
        if (KeyDown(key))
            pad->sticks[index] = value;
    };
    if (mode != kPadDefaultKeys && !(mode == kPadDefaultKeysPort0 && port == 0)) {
        // The port's keys (action 12 has none: Escape is the button 0x10, on port 0 only).
        const int32_t* k = controls.keys;
        stick(0, k[2], -kStickFull);
        stick(0, k[3], kStickFull);
        stick(1, k[0], kStickFull);
        stick(1, k[1], -kStickFull);
        stick(2, k[6], -kStickFull);
        stick(2, k[7], kStickFull);
        stick(3, k[4], kStickFull);
        stick(3, k[5], -kStickFull);
        for (int i = 0; i < 4; ++i) {
            if (KeyDown(k[20 + i]))
                pad->buttons |= static_cast<uint16_t>(1 << i);
        }
        for (int i = 0; i < 4; ++i) {
            if (KeyDown(k[14 + i]))
                Press(pad, 4 + i);
        }
        if (KeyDown(k[18]))
            pad->buttons |= 0x40;
        if (KeyDown(k[19]))
            pad->buttons |= 0x80;
        if (KeyDown(DIK_ESCAPE) && port == 0)
            pad->buttons |= 0x10;
        for (int i = 0; i < 4; ++i) {
            if (KeyDown(k[8 + i]))
                Press(pad, i);
        }
        if (KeyDown(k[13]))
            pad->buttons |= 0x20;
        return 0;
    }

    // The default keys: the arrows, Space, Enter and Escape (as set), S X / Z C, Home End Delete PgDn, Q W E R,
    // Backspace, - = 1 2, 3 4. Enter and Escape are a pressure button or the button 0x10 / 0x20 (any other value:
    // the byte that far into the pressures).
    auto control = [&](int key, int setting) {
        if (!KeyDown(key))
            return;
        if (setting == 0x20 || setting == 0x10)
            pad->buttons |= static_cast<uint16_t>(setting);
        else
            pad->pressure[setting] = 0xFF;
    };
    stick(0, DIK_LEFT, -kStickFull);
    stick(0, DIK_RIGHT, kStickFull);
    stick(1, DIK_UP, kStickFull);
    stick(1, DIK_DOWN, -kStickFull);
    if (KeyDown(DIK_SPACE))
        pad->buttons |= 0x10;
    control(DIK_RETURN, DefaultEnterControl());
    control(DIK_ESCAPE, DefaultEscapeControl());
    stick(3, DIK_S, kStickFull);
    stick(3, DIK_X, -kStickFull);
    stick(2, DIK_Z, -kStickFull);
    stick(2, DIK_C, kStickFull);
    const int dpad[] = {DIK_HOME, DIK_END, DIK_DELETE, DIK_NEXT};
    for (int i = 0; i < 4; ++i) {
        if (KeyDown(dpad[i]))
            pad->buttons |= static_cast<uint16_t>(1 << i);
    }
    const int pressure[] = {DIK_Q, DIK_W, DIK_E, DIK_R};
    for (int i = 0; i < 4; ++i) {
        if (KeyDown(pressure[i]))
            Press(pad, i);
    }
    if (KeyDown(DIK_BACK))
        pad->buttons |= 0x20;
    const int morePressure[] = {DIK_MINUS, DIK_EQUALS, DIK_1, DIK_2};
    for (int i = 0; i < 4; ++i) {
        if (KeyDown(morePressure[i]))
            Press(pad, 4 + i);
    }
    if (KeyDown(DIK_3))
        pad->buttons |= 0x40;
    if (KeyDown(DIK_4))
        pad->buttons |= 0x80;
    return 0;
}

// 0x005F6610
void SetPadMode(int mode)
{
    PadModeSetting() = mode;
}

// 0x005F6620
void SetDefaultEnterControl(int control)
{
    DefaultEnterControl() = control;
}

// 0x005F6630
void SetDefaultEscapeControl(int control)
{
    DefaultEscapeControl() = control;
}

} // namespace Platform

REPLACE(0x005F2E00, Platform::ReadJoystick);
REPLACE(0x005F2E90, Platform::ReadMouse);
REPLACE(0x005F2F80, Platform::HasKeyBindings);
REPLACE(0x005F2FC0, Platform::HasMouseBindings);
REPLACE(0x005F3000, Platform::JoystickAxis);
REPLACE(0x005F30E0, Platform::KeyDown);
REPLACE(0x005F3120, Platform::KeyName);
REPLACE(0x005F3130, Platform::AddJoystickAxis);
REPLACE(0x005F31D0, Platform::AddInputDevice);
REPLACE(0x005F3400, Platform::ReadKeyboard);
REPLACE(0x005F3470, Platform::ParseHex);
REPLACE(0x005F34D0, Platform::LoadRegistryOptions);
REPLACE(0x005F39E0, Platform::ReleaseInput);
REPLACE(0x005F3BF0, Platform::SetKeyName);
REPLACE(0x005F3C10, Platform::InitKeyNames);
REPLACE(0x005F4280, Platform::JoystickControlDown);
REPLACE(0x005F42D0, Platform::MouseControlDown);
REPLACE(0x005F4330, Platform::MouseStickValue);
REPLACE(0x005F4450, Platform::JoystickStickValue);
REPLACE(0x005F5BC0, Platform::ReadPad);
REPLACE(0x005F6610, Platform::SetPadMode);
REPLACE(0x005F6620, Platform::SetDefaultEnterControl);
REPLACE(0x005F6630, Platform::SetDefaultEscapeControl);
