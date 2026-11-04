// The Win32 platform layer's DirectInput side: see platform_input.h.

#include "platform/platform_input.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "engine/pad_input/device.h"
#include "engine/gfx/view_cam.h"
#include "platform/compiler.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "platform/platform_state.h"

THUG2_MSVC_WARNING(disable : 4996) // (wcscpy: the original's own, into its fixed buffers)

namespace os_layer {

// The key table, and the scan of the devices.
constexpr std::size_t kKeyCount = 256;     // the keyboard's scan codes
constexpr int kKeyStateBytes = 256;        // what GetDeviceState reads
constexpr int kKeyCodeCount = 256;
constexpr int kByte1Shift = 8;             // the GUID text's words are read as 32-bit values and split into bytes
constexpr int kByte2Shift = 16;
constexpr int kByte3Shift = 24;
constexpr int kGuidNode3 = 5;              // Data4's bytes 5-7 (those up to 4 are not named)
constexpr int kGuidNode4 = 6;
constexpr int kGuidNode5 = 7;
constexpr int kHexBase = 16;
constexpr int kHexLetterBase = 10;         // 'A' is the digit ten
constexpr int kMaxDistanceSetting = 100;   // the "distance" option: 1-100, the clipping distance from it
constexpr std::size_t kJoystickNameSize = 0x400;

// The registry's "options" bits, and the two bytes of the options word they set.
constexpr int kHighByteMask = 0xFF00;
constexpr int kLowByteMask = 0x00FF;
constexpr int kHighByteOne = 0x100;
constexpr int kOptionOptions39 = 8;
constexpr int kOptionOption37 = 0x10;
constexpr int kOptionAntialiasing = 0x20;
constexpr int kOptionDistanceClipping = 0x100;

// A joystick's controls: 1-10 the axes, 11-39 the buttons (the DirectInput state's); the hat reports hundredths of a
// degree clockwise from up, all ones when centred; the axes range over +-1000.
constexpr uint32_t kHatCentred = 0xFFFFFFFFU;
constexpr int kHatRightFrom = 2250;
constexpr int kHatRightTo = 15750;
constexpr int kHatLeftFrom = 20250;
constexpr int kHatLeftTo = 33750;
constexpr int kHatUpFrom = 29250;
constexpr int kHatUpTo = 6750;
constexpr int kHatDownFrom = 11250;
constexpr int kHatDownTo = 24750;
constexpr int kJoystickAxisRange = 1000;
constexpr int kControlHatY = 5;
constexpr int kControlRx = 6;
constexpr int kControlRy = 7;
constexpr int kControlRz = 8;
constexpr int kControlSlider0 = 9;
constexpr int kControlSlider1 = 10;
constexpr int kFirstJoystickButton = 11;
constexpr int kLastJoystickButton = 39;
constexpr int kLastJoystickAxis = 10;
constexpr int kLastMouseButton = 11;       // the mouse's controls: 1-3 the axes, 4-11 the buttons
constexpr int kAxisPressThreshold = 200;   // an axis counts as a press beyond this
constexpr int kButtonDownShift = 7;
constexpr int kButtonDownMask = 0x80;

// The pad read: a port's binding table has 24 slots - 0-7 the stick directions (two a stick; the slots 0-4 are not named), 8-11 and 14-17 the pressure buttons, 12 / 13 / 18 / 19 the pad's bits 4-7
// and 20-23 its bits 0-3; the pad's buttons are one bit each.
constexpr int kSlotStick2Negative = 6;
constexpr int kSlotStick2Positive = 7;
constexpr int kSlotStick3Positive = 5;
constexpr int kSlotPressureLow = 8;
constexpr int kSlotPressureHigh = 14;
constexpr int kSlotBit4 = 12;
constexpr int kSlotBit5 = 13;
constexpr int kSlotBit6 = 18;
constexpr int kSlotBit7 = 19;
constexpr int kSlotBit0 = 20;
constexpr int kSlotBit1 = 21;
constexpr int kSlotBit2 = 22;
constexpr int kSlotBit3 = 23;
constexpr int kPadBit3 = 8;
constexpr int kPadBit4 = 0x10;
constexpr int kPadBit5 = 0x20;
constexpr int kPadBit6 = 0x40;
constexpr int kPadBit7 = 0x80;
constexpr int kFullPressure = 0xFF;

namespace {

// A stick axis read from two keys or buttons: the axis and its negative and positive inputs.
struct StickKeys {
    int index{};
    int negative{};
    int positive{};
};
// A stick axis pushed by one key: the axis and the key.
struct AxisKey {
    int index{};
    int key{};
};

IDirectInputDevice8A*& stored_keyboard_device()
{
    static IDirectInputDevice8A* keyboard_device{};
    return keyboard_device;
}

} // namespace

IDirectInputDevice8A* keyboard_device() { return stored_keyboard_device(); }
void set_keyboard_device(IDirectInputDevice8A* value) { stored_keyboard_device() = value; }

// The key bindings (0x98 actions: -1 for none; the menus' hints and the pads' keyboard actions read them).
std::array<PortControls, kPorts>& get_controls()
{
    static std::array<PortControls, kPorts> input_map{};
    return input_map;
}
constexpr int kKeyNameLength = 15;
namespace {
// NOT ORIGINAL grouping - platform_input.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_input_state()).
struct PlatformInputState {
    std::array<std::array<wchar_t, kKeyNameLength>, kKeyCount> key_names_{};
    std::array<uint8_t, kKeyCount> keys_{};
    std::array<uint8_t, kKeyCount> last_keys_{};
    int default_escape_control_{1};
    DIMOUSESTATE2 last_mouse_state_{};
    DIMOUSESTATE2 mouse_state_{};
    IDirectInputDevice8A* mouse_{};
    int pad_mode_setting_{};
    int default_enter_control_{};
};

PlatformInputState& platform_input_state()
{
    static core::NoDestroy<PlatformInputState> state;
    return state.get();
}

} // namespace

namespace {

// DirectInput and its devices (the keyboard and mouse are the first of each found).

constexpr uint8_t kMouseInvertX{0};
constexpr uint8_t kMouseInvertY{0};

IDirectInputDevice8A*& get_mouse() { return platform_input_state().mouse_; }

// Each joystick's constant-force effect (rumble: set_actuators).

// The devices' states: this frame's and the last.

std::array<uint8_t, kKeyCount>& get_keys() { return platform_input_state().keys_; }
std::array<uint8_t, kKeyCount>& last_keys() { return platform_input_state().last_keys_; }
DIMOUSESTATE2& mouse_state() { return platform_input_state().mouse_state_; }
DIMOUSESTATE2& last_mouse_state() { return platform_input_state().last_mouse_state_; }

// Per joystick: how many force-feedback axes it has (at most two) and their force resolutions (two each).

std::array<std::array<wchar_t, kKeyNameLength>, kKeyCount>& key_names() { return platform_input_state().key_names_; }

// dinput8.lib's data formats and dxguid.lib's constant-force effect GUID (the original's own copies of them).
const DIDATAFORMAT* joystick_format() { return &c_dfDIJoystick2; } // c_dfDIJoystick2
const DIDATAFORMAT* keyboard_format() { return &c_dfDIKeyboard; } // c_dfDIKeyboard
const DIDATAFORMAT* mouse_format() { return &c_dfDIMouse2; }    // c_dfDIMouse2
const GUID& constant_force_guid() { return GUID_ConstantForce; } // GUID_ConstantForce

// Any of `count` controls set (not -1).
bool any_binding(std::span<const int32_t, kBindings> controls)
{
    return std::ranges::any_of(controls, [](int32_t control) { return control != -1; });
}

// "pad<n>": a GUID as text - XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX - parsed digit group by digit group: the
// fourth group's last four digits are copied out first (the group runs on into the last one otherwise). A
// single character (or none) gives the zero GUID.
GUID parse_joystick_guid(const char* text, DWORD size)
{
    GUID guid{};
    if (size <= 1) {
        return guid;
    }
    std::string_view cursor(text, size);
    auto skip = [&cursor](std::size_t count) { cursor.remove_prefix(std::min(count, cursor.size())); };
    guid.Data1 = parse_hex(cursor);
    skip(1);
    guid.Data2 = static_cast<uint16_t>(parse_hex(cursor));
    skip(1);
    guid.Data3 = static_cast<uint16_t>(parse_hex(cursor));
    skip(1);
    uint32_t value = parse_hex(cursor);
    guid.Data4[0] = static_cast<uint8_t>(value >> kByte1Shift);
    guid.Data4[1] = static_cast<uint8_t>(value);
    skip(1);
    std::string_view four = cursor.substr(0, 4);
    value = parse_hex(four);
    guid.Data4[2] = static_cast<uint8_t>(value >> kByte1Shift);
    guid.Data4[3] = static_cast<uint8_t>(value);
    skip(4);
    value = parse_hex(cursor);
    guid.Data4[4] = static_cast<uint8_t>(value >> kByte3Shift);
    guid.Data4[kGuidNode3] = static_cast<uint8_t>(value >> kByte2Shift);
    guid.Data4[kGuidNode4] = static_cast<uint8_t>(value >> kByte1Shift);
    guid.Data4[kGuidNode5] = static_cast<uint8_t>(value);
    return guid;
}

// "distance": 1-100, the clipping distance from it; anything above 100 is none.
void place_at_range(uint32_t distance)
{
    platform_state().set_distance_setting(distance);
    if (distance > kMaxDistanceSetting) {
        platform_state().set_distance_setting(0);
    } else if (distance != 0) {
        gfx::set_draw_distance_setting(
            static_cast<int32_t>((static_cast<double>(distance) * kDrawDistanceStep) + kDrawDistanceBase));
    }
}

struct KeyNameEntry {
    uint8_t code;
    const wchar_t* name;
};

// The keyboard's names, in the order the original sets them (DIK codes).
constexpr auto kKeyNames = std::to_array<KeyNameEntry>({
    {.code = 0x1E, .name = L"A"},      {.code = 0x30, .name = L"B"},      {.code = 0x2E, .name = L"C"},        {.code = 0x20, .name = L"D"},        {.code = 0x12, .name = L"E"},
    {.code = 0x21, .name = L"F"},      {.code = 0x22, .name = L"G"},      {.code = 0x23, .name = L"H"},        {.code = 0x17, .name = L"I"},        {.code = 0x24, .name = L"J"},
    {.code = 0x25, .name = L"K"},      {.code = 0x26, .name = L"L"},      {.code = 0x32, .name = L"M"},        {.code = 0x31, .name = L"N"},        {.code = 0x18, .name = L"O"},
    {.code = 0x19, .name = L"P"},      {.code = 0x10, .name = L"Q"},      {.code = 0x13, .name = L"R"},        {.code = 0x1F, .name = L"S"},        {.code = 0x14, .name = L"T"},
    {.code = 0x16, .name = L"U"},      {.code = 0x2F, .name = L"V"},      {.code = 0x11, .name = L"W"},        {.code = 0x2D, .name = L"X"},        {.code = 0x15, .name = L"Y"},
    {.code = 0x2C, .name = L"Z"},      {.code = 0x0B, .name = L"0"},      {.code = 0x02, .name = L"1"},        {.code = 0x03, .name = L"2"},        {.code = 0x04, .name = L"3"},
    {.code = 0x05, .name = L"4"},      {.code = 0x06, .name = L"5"},      {.code = 0x07, .name = L"6"},        {.code = 0x08, .name = L"7"},        {.code = 0x09, .name = L"8"},
    {.code = 0x0A, .name = L"9"},      {.code = 0x01, .name = L"Esc"},    {.code = 0x0C, .name = L"Minus"},    {.code = 0x0D, .name = L"Equals"},   {.code = 0x0E, .name = L"Back"},
    {.code = 0x0F, .name = L"Tab"},    {.code = 0x1A, .name = L"["},      {.code = 0x1B, .name = L"]"},        {.code = 0x1C, .name = L"Return"},   {.code = 0x1D, .name = L"LCtrl"},
    {.code = 0x27, .name = L";"},      {.code = 0x28, .name = L"'"},      {.code = 0x29, .name = L"`"},        {.code = 0x2A, .name = L"LShift"},   {.code = 0x2B, .name = L"\\"},
    {.code = 0x33, .name = L","},      {.code = 0x34, .name = L"."},      {.code = 0x35, .name = L"/"},        {.code = 0x36, .name = L"RShift"},   {.code = 0x37, .name = L"*"},
    {.code = 0x38, .name = L"LAlt"},   {.code = 0xB8, .name = L"RAlt"},   {.code = 0x39, .name = L"Spacebar"}, {.code = 0x3A, .name = L"Capslock"}, {.code = 0x3B, .name = L"F1"},
    {.code = 0x3C, .name = L"F2"},     {.code = 0x3D, .name = L"F3"},     {.code = 0x3E, .name = L"F4"},       {.code = 0x3F, .name = L"F5"},       {.code = 0x40, .name = L"F6"},
    {.code = 0x41, .name = L"F7"},     {.code = 0x42, .name = L"F8"},     {.code = 0x43, .name = L"F9"},       {.code = 0x44, .name = L"F10"},      {.code = 0x57, .name = L"F11"},
    {.code = 0x58, .name = L"F12"},    {.code = 0x64, .name = L"F13"},    {.code = 0x65, .name = L"F14"},      {.code = 0x66, .name = L"F15"},      {.code = 0x45, .name = L"Numlock"},
    {.code = 0x46, .name = L"Scroll"}, {.code = 0x47, .name = L"KP 7"},   {.code = 0x48, .name = L"KP 8"},     {.code = 0x49, .name = L"KP 9"},     {.code = 0x4A, .name = L"KP -"},
    {.code = 0x4B, .name = L"KP 4"},   {.code = 0x4C, .name = L"KP 5"},   {.code = 0x4D, .name = L"KP 6"},     {.code = 0x4E, .name = L"KP +"},     {.code = 0x4F, .name = L"KP 1"},
    {.code = 0x50, .name = L"KP 2"},   {.code = 0x51, .name = L"KP 3"},   {.code = 0x52, .name = L"KP 0"},     {.code = 0x53, .name = L"KP ."},     {.code = 0xB3, .name = L"KP ,"},
    {.code = 0xB5, .name = L"KP /"},   {.code = 0x9C, .name = L"Enter"},  {.code = 0x9D, .name = L"RCtrl"},    {.code = 0xC5, .name = L"Pause"},    {.code = 0xC7, .name = L"Home"},
    {.code = 0xC8, .name = L"Up"},     {.code = 0xC9, .name = L"PgUp"},   {.code = 0xCB, .name = L"Left"},     {.code = 0xCD, .name = L"Right"},    {.code = 0xCF, .name = L"End"},
    {.code = 0xD0, .name = L"Down"},   {.code = 0xD1, .name = L"PgDown"}, {.code = 0xD2, .name = L"Insert"},   {.code = 0xD3, .name = L"Delete"},
});

} // namespace

// 0x005F2E00
DIJOYSTATE2* read_joystick(int port)
{
    if (!core::at(platform_state().joysticks(), port)) {
        return nullptr;
    }
    if (FAILED(core::at(platform_state().joysticks(), port)->Poll())) {
        if (FAILED(core::at(platform_state().joysticks(), port)->Acquire()) ||
            FAILED(core::at(platform_state().joysticks(), port)->Poll())) {
            return nullptr;
        }
    }
    IDirectInputDevice8A* joystick = core::at(platform_state().joysticks(), port);
    DIJOYSTATE2* state = &core::at(platform_state().joystick_states(), port);
    core::at(platform_state().last_joystick_states(), port) = *state;
    if (FAILED(joystick->GetDeviceState(sizeof(DIJOYSTATE2), state))) {
        return nullptr;
    }
    return state;
}

// 0x005F2E90
DIMOUSESTATE2* read_mouse()
{
    if (!get_mouse()) {
        return nullptr;
    }
    if (FAILED(get_mouse()->Poll())) {
        if (FAILED(get_mouse()->Acquire()) || FAILED(get_mouse()->Poll())) {
            return nullptr;
        }
    }
    DIMOUSESTATE2& state = mouse_state();
    DIMOUSESTATE2& last = last_mouse_state();
    last.lY = state.lY;
    last.lX = state.lX;
    std::memcpy(&last.lZ, &state.lZ, 3 * sizeof(LONG)); // (z and the first eight buttons)
    const HRESULT result = get_mouse()->GetDeviceState(sizeof(DIMOUSESTATE2), &state);
    if (state.lX == 0 && state.lY == 0) {
        // No movement this frame: x, then y, keep the last frame's less 2 (none within 2 of 0).
        const LONG x = last.lX;
        const LONG y = last.lY;
        if (x != 0 || y != 0) {
            if (x != 0) {
                if (x > 2) {
                    state.lX = x - 2;
                } else if (x < -2) {
                    state.lX = x + 2;
                }
            }
            if (y > 2) {
                state.lY = y - 2;
            } else if (y < -2) {
                state.lY = y + 2;
            }
        }
    }
    return FAILED(result) ? nullptr : &state;
}

// 0x005F2F80
bool has_key_bindings(int port) { return any_binding(core::at(get_controls(), port).keys); }

// 0x005F2FC0
bool has_mouse_bindings(int port) { return any_binding(core::at(get_controls(), port).mouse); }

// 0x005F3000
int joystick_axis(const DIJOYSTATE2* state, int control)
{
    switch (control) {
    case 1:
        return state->lX;
    case 2:
        return state->lY;
    case 3:
        return state->lZ;
    case 4: { // the hat (hundredths of a degree clockwise from up): right 22.5-157.5, left 202.5-337.5
        const DWORD hat = state->rgdwPOV[0];
        if (hat == kHatCentred) {
            return 0;
        }
        if (hat >= kHatRightFrom && hat <= kHatRightTo) {
            return kJoystickAxisRange;
        }
        if (hat >= kHatLeftFrom && hat <= kHatLeftTo) {
            return -kJoystickAxisRange;
        }
        return 0;
    }
    case kControlHatY: { // up 292.5-67.5, down 112.5-247.5
        const DWORD hat = state->rgdwPOV[0];
        if (hat == kHatCentred) {
            return 0;
        }
        if (hat >= kHatUpFrom || hat <= kHatUpTo) {
            return -kJoystickAxisRange;
        }
        if (hat >= kHatDownFrom && hat <= kHatDownTo) {
            return kJoystickAxisRange;
        }
        return 0;
    }
    case kControlRx:
        return state->lRx;
    case kControlRy:
        return state->lRy;
    case kControlRz:
        return state->lRz;
    case kControlSlider0:
        return state->rglSlider[0];
    case kControlSlider1:
        return state->rglSlider[1];
    default:
        return 0;
    }
}

// 0x005F30E0
bool get_key_down(int code)
{
    if (code == -1) {
        return false;
    }
    if (code == DIK_ESCAPE) {
        return (core::at(get_keys(), DIK_ESCAPE) != 0U) && (core::at(last_keys(), DIK_ESCAPE) == 0U);
    }
    return core::at(get_keys(), code) != 0;
}

// 0x005F3120
// (the original indexed the table with any code, -1 for an unbound key too, and read the bytes before it; a code
// outside the table now names no key)
wchar_t* get_key_name(int code)
{
    static std::array<wchar_t, 1> no_key_name{};
    if (code < 0 || std::cmp_greater_equal(code, key_names().size())) {
        return no_key_name.data();
    }
    return core::at(key_names(), code).data();
}

// 0x005F3130
BOOL CALLBACK add_joystick_axis(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID device)
{
    auto* joystick = static_cast<IDirectInputDevice8A*>(device);
    DIPROPRANGE range;
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = object->dwType;
    range.diph.dwHow = DIPH_BYID;
    range.lMin = -kJoystickAxisRange;
    range.lMax = kJoystickAxisRange;
    joystick->SetProperty(DIPROP_RANGE, &range.diph);
    const int port = core::at(platform_state().joysticks(), 1) == joystick ? 1 : 0;
    if (((object->dwFlags & DIDOI_FFACTUATOR) != 0U) && core::at(platform_state().force_axis_counts(), port) < 2) {
        const uint32_t count = core::at(platform_state().force_axis_counts(), port);
        core::at(platform_state().force_resolutions(), (2 * port) + count) = object->dwFFForceResolution;
        core::at(platform_state().force_axis_counts(), port) = count + 1;
    }
    return DIENUM_CONTINUE;
}

// 0x005F31D0
BOOL CALLBACK add_input_device(LPCDIDEVICEINSTANCEA device, LPVOID /*context*/)
{
    const DWORD type = device->dwDevType & 0xFF;
    IDirectInputDevice8A* created = nullptr;
    if (type == DI8DEVTYPE_GAMEPAD || type == DI8DEVTYPE_JOYSTICK) {
        int port = -1;
        for (int i = 0; i < kPorts; ++i) {
            if (std::memcmp(&core::at(get_controls(), i).joystick, &device->guidInstance, sizeof(GUID)) == 0) {
                port = i;
            }
        }
        if (port < 0) {
            return DIENUM_CONTINUE;
        }
        if (SUCCEEDED(platform_state().direct_input()->CreateDevice(device->guidInstance, &created, nullptr))) {
            core::at(platform_state().joysticks(), port) = created;
            created->SetDataFormat(joystick_format());
            created->SetCooperativeLevel(render::renderer().game_window(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
            created->EnumObjects(add_joystick_axis, created, DIDFT_AXIS);
            if (core::at(platform_state().force_axis_counts(), port) > 0) {
                // Rumble: an endless constant force along X and Y, started by set_actuators.
                std::array<DWORD, 2> axes = {{DIJOFS_X, DIJOFS_Y}};
                std::array<LONG, 2> direction = {{0, 0}};
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
                effect.rgdwAxes = axes.data();
                effect.rglDirection = direction.data();
                effect.lpEnvelope = nullptr;
                effect.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
                effect.lpvTypeSpecificParams = &force;
                effect.dwStartDelay = 0;
                created->CreateEffect(constant_force_guid(), &effect, &core::at(platform_state().effects(), port), nullptr);
            }
            created->Acquire();
        }
    }
    if (type == DI8DEVTYPE_KEYBOARD) {
        if (!stored_keyboard_device() && SUCCEEDED(platform_state().direct_input()->CreateDevice(device->guidInstance, &created, nullptr))) {
            stored_keyboard_device() = created;
            created->SetDataFormat(keyboard_format());
            created->SetCooperativeLevel(render::renderer().game_window(), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
            created->Acquire();
        }
    } else if (type == DI8DEVTYPE_MOUSE) {
        if (!get_mouse() && SUCCEEDED(platform_state().direct_input()->CreateDevice(device->guidInstance, &created, nullptr))) {
            get_mouse() = created;
            created->SetDataFormat(mouse_format());
            created->SetCooperativeLevel(render::renderer().game_window(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
            created->Acquire();
        }
    }
    return DIENUM_CONTINUE;
}

// 0x005F3400
void read_keyboard()
{
    IDirectInputDevice8A* keyboard = stored_keyboard_device();
    last_keys() = get_keys();
    const HRESULT result = keyboard->GetDeviceState(256, get_keys().data());
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED) {
        return;
    }
    if (SUCCEEDED(stored_keyboard_device()->Acquire())) {
        stored_keyboard_device()->GetDeviceState(kKeyStateBytes, get_keys().data());
    } else {
        get_keys().fill(0);
    }
}

// 0x005F3470
uint32_t parse_hex(std::string_view& cursor)
{
    auto digit = [](char character) { return character >= '0' && character <= '9'; };
    uint32_t value = 0;
    while (!cursor.empty() && (digit(cursor.front()) || (cursor.front() >= 'A' && cursor.front() <= 'F'))) {
        const char character = cursor.front();
        cursor.remove_prefix(1);
        value = (value * kHexBase) + static_cast<uint32_t>(character - (digit(character) ? '0' : 'A' - kHexLetterBase));
    }
    return value;
}

// 0x005F34D0
void load_registry_options(const char* key)
{
    HKEY options = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &options) != ERROR_SUCCESS) {
        return;
    }
    DWORD type = 0;
    DWORD value = 0;
    // (The size starts at 1, so the first read - "distance" - always fails, setting it to 4: "distance" is
    // read again after "options".)
    DWORD size = 1;
    if (RegQueryValueExA(options, "distance", nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
        place_at_range(value);
    }
    if (RegQueryValueExA(options, "options", nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
        if ((value & 1) != 0U) {
            render::view_setup().set_fog_setting(true);
        }
        if ((value & 2) != 0U) {
            render::view_setup().set_hq_shadows(1);
        }
        if ((value & 4) != 0U) {
            platform_state().set_options38_and39(static_cast<uint16_t>((platform_state().options38_and39() & kHighByteMask) | 1)); // (its low byte)
        }
        if ((value & kOptionOptions39) != 0U) {
            platform_state().set_options38_and39(static_cast<uint16_t>((platform_state().options38_and39() & kLowByteMask) | kHighByteOne)); // (its high byte)
        }
        if ((value & kOptionOption37) != 0U) {
            platform_state().set_option37(1);
        }
        if ((value & kOptionAntialiasing) != 0U) {
            render::renderer().set_antialiasing(true);
        }
        if ((value & kOptionDistanceClipping) != 0U) {
            render::view_setup().set_distance_clipping(true);
        }
    }
    if (RegQueryValueExA(options, "distance", nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
        place_at_range(value);
    }
    if (RegQueryValueExA(options, "resolution", nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
        render::renderer().set_resolution_setting(value > 3 ? 0 : static_cast<int>(value));
    }

    // The joystick each port plays with (the text buffer isn't cleared before the first: a short value leaves
    // whatever was there after it).
    std::array<char, kJoystickNameSize> text{};
    size = sizeof(text);
    if (RegQueryValueExA(options, "pad0", nullptr, &type, raw::bytes(text.data()), &size) == ERROR_SUCCESS) {
        core::at(get_controls(), 0).joystick = parse_joystick_guid(text.data(), size);
    }
    std::memset(text.data(), 0, sizeof(text));
    size = sizeof(text);
    if (RegQueryValueExA(options, "pad1", nullptr, &type, raw::bytes(text.data()), &size) == ERROR_SUCCESS) {
        core::at(get_controls(), 1).joystick = parse_joystick_guid(text.data(), size);
    }

    // Each port's keys and joystick controls (the ones not there stay as they are).
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            text::format_to(text, "k%i_%i", port, i);
            size = 4;
            if (RegQueryValueExA(options, text.data(), nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
                core::at(core::at(get_controls(), port).keys, i) = static_cast<int32_t>(value);
            }
        }
    }
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            text::format_to(text, "gp%i_%i", port, i);
            size = 4;
            if (RegQueryValueExA(options, text.data(), nullptr, &type, raw::bytes(&value), &size) == ERROR_SUCCESS) {
                core::at(core::at(get_controls(), port).gamepad, i) = static_cast<int32_t>(value);
            }
        }
    }
    RegCloseKey(options);
}

// 0x005F39E0
void release_input()
{
    for (int port = 0; port < kPorts; ++port) {
        if (core::at(platform_state().joysticks(), port)) {
            core::at(platform_state().joysticks(), port)->Release();
            core::at(platform_state().joysticks(), port) = nullptr;
        }
        if (core::at(platform_state().effects(), port)) {
            core::at(platform_state().effects(), port)->Release(); // (left set)
        }
    }
    if (stored_keyboard_device()) {
        stored_keyboard_device()->Release();
        stored_keyboard_device() = nullptr;
    }
    // (The mouse isn't released.)
    if (platform_state().direct_input()) {
        platform_state().direct_input()->Release();
        platform_state().direct_input() = nullptr;
    }
}

// 0x005F3BF0
wchar_t* set_key_name(int code, const wchar_t* name) { return std::wcscpy(core::at(key_names(), code).data(), name); }

// 0x005F3C10
void init_key_names()
{
    for (int code = 0; code < kKeyCodeCount; ++code) {
        std::wcscpy(core::at(key_names(), code).data(), L"(NONE)");
    }
    for (const KeyNameEntry& key : kKeyNames) {
        std::wcscpy(core::at(key_names(), key.code).data(), key.name);
    }
}

// --- the pad read ---

namespace {

// The pad mode and the default keys' Enter and Escape.
int& pad_mode_setting() { return platform_input_state().pad_mode_setting_; }
int& default_enter_control() { return platform_input_state().default_enter_control_; }
int& default_escape_control() { return platform_input_state().default_escape_control_; }
// The mouse's x / y sticks reversed (nothing sets them).
bool mouse_invert_x() { return kMouseInvertX != 0; }
bool mouse_invert_y() { return kMouseInvertY != 0; }

constexpr float kStickMax = 32767.0F;                 // 0x0065BF80
constexpr float kStickMin = -32768.0F;                // 0x00666B00
constexpr float kMouseStickScale = 6553.39990234375F; // 0x00666B04
constexpr float kJoystickStickScale = 0.001F;         // 0x006461D8
constexpr float kStickMaxReversed = -32767.0F;        // 0x00666B30 (the joystick's y sticks: up is negative)
constexpr int16_t kStickFull = 32767;

// The original's _ftol: truncated toward zero.
int32_t truncate(double value) { return static_cast<int32_t>(value); }

// A mouse axis as a stick: its movement scaled, within the stick's range.
double mouse_axis_stick(const DIMOUSESTATE2* state, int control)
{
    LONG value = 0; // (axis 1 x, 2 y, 3 the wheel)
    if (control == 1) {
        value = state->lX;
    } else if (control == 2) {
        value = state->lY;
    } else if (control == 3) {
        value = state->lZ;
    }
    const double stick = static_cast<double>(value) * kMouseStickScale;
    if (stick > kStickMax) {
        return kStickMax;
    }
    if (stick < kStickMin) {
        return kStickMin;
    }
    return stick;
}

// One of the pad's eight pressure buttons held.
void get_press(sio::PadSnapshot* pad, int button) { core::at(pad->pressure, button) = kFullPressure; }

} // namespace

// 0x005F4280
bool joystick_control_down(const DIJOYSTATE2* state, int control)
{
    if (control >= kFirstJoystickButton && control <= kLastJoystickButton) {
        return core::at(state->rgbButtons, control - kFirstJoystickButton) != 0;
    }
    if (control >= 1 && control <= kLastJoystickAxis) {
        const int value = joystick_axis(state, control);
        return value > kAxisPressThreshold || value < -kAxisPressThreshold;
    }
    return false;
}

// 0x005F42D0
bool mouse_control_down(const DIMOUSESTATE2* state, int control)
{
    if (control >= 4 && control <= kLastMouseButton) {
        return (core::at(state->rgbButtons, control - 4) >> kButtonDownShift) != 0;
    }
    if (control >= 1 && control <= 3) {
        LONG value = state->lZ;
        if (control == 1) {
            value = state->lX;
        } else if (control == 2) {
            value = state->lY;
        }
        return value > 3 || value < -3;
    }
    return false;
}

// 0x005F4330
double mouse_stick_value(const DIMOUSESTATE2* state, int negative, int positive)
{
    double value = 0.0;
    if (negative >= 4 && negative <= kLastMouseButton) {
        if ((core::at(state->rgbButtons, negative - 4) & kButtonDownMask) != 0) {
            value = -1.0;
        }
    } else if (negative >= 1 && negative <= 3) {
        value = mouse_axis_stick(state, negative);
    }
    if (positive >= 4 && positive <= kLastMouseButton) {
        return ((core::at(state->rgbButtons, positive - 4) & kButtonDownMask) != 0) ? value + 1.0 : value;
    }
    if (positive >= 1 && positive <= 3) {
        return mouse_axis_stick(state, positive); // (in place of the first)
    }
    return value;
}

// 0x005F4450
double joystick_stick_value(const DIJOYSTATE2* state, int negative, int positive)
{
    double value = 0.0;
    if (negative >= kFirstJoystickButton && negative <= kLastJoystickButton) {
        if (core::at(state->rgbButtons, negative - kFirstJoystickButton) != 0U) {
            value = -1.0;
        }
    } else if (negative >= 1 && negative <= kLastJoystickAxis) {
        value = joystick_axis(state, negative) * static_cast<double>(kJoystickStickScale);
    }
    if (positive >= kFirstJoystickButton && positive <= kLastJoystickButton) {
        return (core::at(state->rgbButtons, positive - kFirstJoystickButton) != 0U) ? value + 1.0 : value;
    }
    if (positive >= 1 && positive <= kLastJoystickAxis) {
        return joystick_axis(state, positive) * static_cast<double>(kJoystickStickScale); // (in place of the first)
    }
    return value;
}

namespace {

// The mouse's controls (with the key bindings mode): its sticks, pressure buttons and buttons.
void read_mouse_controls(const DIMOUSESTATE2* mouse, const PortControls& controls, sio::PadSnapshot* pad)
{
    // The sticks: each mouse axis's movement scaled twice - x 6553.4 within the stick's range, then x 32767 -
    // and only the low 16 bits kept.
    const auto& mouse_map = controls.mouse;
    auto stick = [&](StickKeys keys, bool reversed) {
        const int index = keys.index;
        const int negative = keys.negative;
        const int positive = keys.positive;
        auto value = static_cast<int16_t>(truncate(mouse_stick_value(mouse, negative, positive) * kStickMax));
        if (value != 0) {
            core::at(pad->sticks, index) = static_cast<int16_t>(reversed ? -value : value);
        }
    };
    stick({.index = 0, .negative = core::at(mouse_map, 2), .positive = core::at(mouse_map, 3)}, mouse_invert_x());
    stick({.index = 1, .negative = core::at(mouse_map, 0), .positive = core::at(mouse_map, 1)}, mouse_invert_y());
    stick({.index = 2,
           .negative = core::at(mouse_map, kSlotStick2Negative),
           .positive = core::at(mouse_map, kSlotStick2Positive)},
          mouse_invert_x());
    stick({.index = 3, .negative = core::at(mouse_map, 4), .positive = core::at(mouse_map, kSlotStick3Positive)},
          mouse_invert_y());
    for (int i = 0; i < 4; ++i) {
        if (mouse_control_down(mouse, core::at(mouse_map, kSlotPressureLow + i))) {
            get_press(pad, i);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (mouse_control_down(mouse, core::at(mouse_map, kSlotPressureHigh + i))) {
            get_press(pad, 4 + i);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (mouse_control_down(mouse, core::at(mouse_map, kSlotBit0 + i))) {
            pad->buttons |= static_cast<uint16_t>(1 << i);
        }
    }
    if (mouse_control_down(mouse, core::at(mouse_map, kSlotBit4))) {
        pad->buttons |= kPadBit4;
    }
    if (mouse_control_down(mouse, core::at(mouse_map, kSlotBit5))) {
        pad->buttons |= kPadBit5;
    }
    if (mouse_control_down(mouse, core::at(mouse_map, kSlotBit6))) {
        pad->buttons |= kPadBit6;
    }
    if (mouse_control_down(mouse, core::at(mouse_map, kSlotBit7))) {
        pad->buttons |= kPadBit7;
    }
}

// The joystick's controls: its sticks, pressure buttons and buttons.
void read_joystick_controls(const DIJOYSTATE2* joystick, const PortControls& controls, sio::PadSnapshot* pad)
{
    // The sticks (a larger push than the mouse's wins), the pressure buttons, the d-pad (axes only: a button
    // there never counts) and the other buttons.
    const auto& gamepad = controls.gamepad;
    auto stick = [&](StickKeys keys, float scale) {
        const int index = keys.index;
        const int negative = keys.negative;
        const int positive = keys.positive;
        auto value = static_cast<int16_t>(truncate(joystick_stick_value(joystick, negative, positive) * scale));
        if (value != 0 &&
            std::abs(static_cast<int>(core::at(pad->sticks, index))) < std::abs(static_cast<int>(value))) {
            core::at(pad->sticks, index) = value;
        }
    };
    stick({.index = 0, .negative = core::at(gamepad, 2), .positive = core::at(gamepad, 3)}, kStickMax);
    stick({.index = 1, .negative = core::at(gamepad, 0), .positive = core::at(gamepad, 1)}, kStickMaxReversed);
    stick({.index = 2,
           .negative = core::at(gamepad, kSlotStick2Negative),
           .positive = core::at(gamepad, kSlotStick2Positive)},
          kStickMax);
    stick({.index = 3, .negative = core::at(gamepad, 4), .positive = core::at(gamepad, kSlotStick3Positive)},
          kStickMaxReversed);
    for (int i = 0; i < 4; ++i) {
        if (joystick_control_down(joystick, core::at(gamepad, kSlotPressureLow + i))) {
            get_press(pad, i);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (joystick_control_down(joystick, core::at(gamepad, kSlotPressureHigh + i))) {
            get_press(pad, 4 + i);
        }
    }
    if (joystick_axis(joystick, core::at(gamepad, kSlotBit0)) < -kAxisPressThreshold) {
        pad->buttons |= 1;
    }
    if (joystick_axis(joystick, core::at(gamepad, kSlotBit1)) > kAxisPressThreshold) {
        pad->buttons |= 2;
    }
    if (joystick_axis(joystick, core::at(gamepad, kSlotBit2)) < -kAxisPressThreshold) {
        pad->buttons |= 4;
    }
    if (joystick_axis(joystick, core::at(gamepad, kSlotBit3)) > kAxisPressThreshold) {
        pad->buttons |= kPadBit3;
    }
    if (joystick_control_down(joystick, core::at(gamepad, kSlotBit4))) {
        pad->buttons |= kPadBit4;
    }
    if (joystick_control_down(joystick, core::at(gamepad, kSlotBit5))) {
        pad->buttons |= kPadBit5;
    }
    if (joystick_control_down(joystick, core::at(gamepad, kSlotBit6))) {
        pad->buttons |= kPadBit6;
    }
    if (joystick_control_down(joystick, core::at(gamepad, kSlotBit7))) {
        pad->buttons |= kPadBit7;
    }
}

// The port's own keys.
void read_port_keys(const PortControls& controls, int port, sio::PadSnapshot* pad)
{
    auto stick = [&](AxisKey axis_key, int16_t value) {
        const int index = axis_key.index;
        const int key = axis_key.key;
        if (get_key_down(key)) {
            core::at(pad->sticks, index) = value;
        }
    };
    // The port's keys (action 12 has none: Escape is the button 0x10, on port 0 only).
    const auto& keys = controls.keys;
    stick({.index = 0, .key = core::at(keys, 2)}, -kStickFull);
    stick({.index = 0, .key = core::at(keys, 3)}, kStickFull);
    stick({.index = 1, .key = core::at(keys, 0)}, kStickFull);
    stick({.index = 1, .key = core::at(keys, 1)}, -kStickFull);
    stick({.index = 2, .key = core::at(keys, kSlotStick2Negative)}, -kStickFull);
    stick({.index = 2, .key = core::at(keys, kSlotStick2Positive)}, kStickFull);
    stick({.index = 3, .key = core::at(keys, 4)}, kStickFull);
    stick({.index = 3, .key = core::at(keys, kSlotStick3Positive)}, -kStickFull);
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(keys, kSlotBit0 + i))) {
            pad->buttons |= static_cast<uint16_t>(1 << i);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(keys, kSlotPressureHigh + i))) {
            get_press(pad, 4 + i);
        }
    }
    if (get_key_down(core::at(keys, kSlotBit6))) {
        pad->buttons |= kPadBit6;
    }
    if (get_key_down(core::at(keys, kSlotBit7))) {
        pad->buttons |= kPadBit7;
    }
    if (get_key_down(DIK_ESCAPE) && port == 0) {
        pad->buttons |= kPadBit4;
    }
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(keys, kSlotPressureLow + i))) {
            get_press(pad, i);
        }
    }
    if (get_key_down(core::at(keys, kSlotBit5))) {
        pad->buttons |= kPadBit5;
    }
}

void read_default_keys(sio::PadSnapshot* pad)
{
    auto stick = [&](AxisKey axis_key, int16_t value) {
        const int index = axis_key.index;
        const int key = axis_key.key;
        if (get_key_down(key)) {
            core::at(pad->sticks, index) = value;
        }
    };
    // The default keys: the arrows, Space, Enter and Escape (as set), S X / Z C, Home End Delete PgDn, Q W E R,
    // Backspace, - = 1 2, 3 4. Enter and Escape are a pressure button or the button 0x10 / 0x20 (any other value:
    // the byte that far into the pressures).
    auto control = [&](int key, int setting) {
        if (!get_key_down(key)) {
            return;
        }
        if (setting == kPadBit5 || setting == kPadBit4) {
            pad->buttons |= static_cast<uint16_t>(setting);
        } else {
            core::at(pad->pressure, setting) = kFullPressure;
        }
    };
    stick({.index = 0, .key = DIK_LEFT}, -kStickFull);
    stick({.index = 0, .key = DIK_RIGHT}, kStickFull);
    stick({.index = 1, .key = DIK_UP}, kStickFull);
    stick({.index = 1, .key = DIK_DOWN}, -kStickFull);
    if (get_key_down(DIK_SPACE)) {
        pad->buttons |= kPadBit4;
    }
    control(DIK_RETURN, default_enter_control());
    control(DIK_ESCAPE, default_escape_control());
    stick({.index = 3, .key = DIK_S}, kStickFull);
    stick({.index = 3, .key = DIK_X}, -kStickFull);
    stick({.index = 2, .key = DIK_Z}, -kStickFull);
    stick({.index = 2, .key = DIK_C}, kStickFull);
    const auto dpad = std::to_array<int>({DIK_HOME, DIK_END, DIK_DELETE, DIK_NEXT});
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(dpad, i))) {
            pad->buttons |= static_cast<uint16_t>(1 << i);
        }
    }
    const auto pressure = std::to_array<int>({DIK_Q, DIK_W, DIK_E, DIK_R});
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(pressure, i))) {
            get_press(pad, i);
        }
    }
    if (get_key_down(DIK_BACK)) {
        pad->buttons |= kPadBit5;
    }
    const auto more_pressure = std::to_array<int>({DIK_MINUS, DIK_EQUALS, DIK_1, DIK_2});
    for (int i = 0; i < 4; ++i) {
        if (get_key_down(core::at(more_pressure, i))) {
            get_press(pad, 4 + i);
        }
    }
    if (get_key_down(DIK_3)) {
        pad->buttons |= kPadBit6;
    }
    if (get_key_down(DIK_4)) {
        pad->buttons |= kPadBit7;
    }
}

} // namespace

// 0x005F5BC0
int read_pad(int port, sio::PadSnapshot* pad)
{
    --port;
    read_keyboard();
    const DIJOYSTATE2* joystick = read_joystick(port);
    const DIMOUSESTATE2* mouse = read_mouse();
    pad->buttons = 0;
    for (int16_t& stick : pad->sticks) {
        stick = 0;
    }
    std::memset(pad->pressure.data(), 0, sizeof(pad->pressure));
    if (!joystick && !has_key_bindings(port) && !has_mouse_bindings(port)) {
        return 0;
    }
    const PortControls& controls = core::at(get_controls(), port);

    if (mouse && pad_mode_setting() == kPadKeyBindings) {
        read_mouse_controls(mouse, controls, pad);
    }

    if (joystick) {
        read_joystick_controls(joystick, controls, pad);
    }

    const int mode = pad_mode_setting();
    if (mode == kPadNoKeys) {
        return 0;
    }
    if (mode != kPadDefaultKeys && !(mode == kPadDefaultKeysPort0 && port == 0)) {
        read_port_keys(controls, port, pad);
        return 0;
    }

    read_default_keys(pad);
    return 0;
}

// 0x005F6610
void set_pad_mode(int mode) { pad_mode_setting() = mode; }

// 0x005F6620
void set_default_enter_control(int control) { default_enter_control() = control; }

// 0x005F6630
void set_default_escape_control(int control) { default_escape_control() = control; }

} // namespace os_layer
