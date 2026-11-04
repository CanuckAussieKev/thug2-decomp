// The original's key bindings, key names and registry options (0x005F2F80, 0x005F3120, 0x005F3470-0x005F34D0,
// 0x005F3BF0, 0x005F3C10): see port_controls.h.

#include "engine/pad_input/port_controls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <span>
#include <string_view>
#include <utility>

#include "base/core/checked_index.h"
#include "base/core/compiler.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/view_setup.h"
#include "engine/pad_input/options_state.h"
#include "platform/registry.h"

THUG2_MSVC_WARNING(disable : 4996) // (wcscpy: the original's own, into its fixed buffers)

namespace os_layer {

constexpr std::size_t kKeyCount = 256; // the keyboard's scan codes
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

// The key bindings (0x98 actions: -1 for none; the menus' hints and the pads' keyboard actions read them).
std::array<PortControls, kPorts>& get_controls()
{
    static std::array<PortControls, kPorts> input_map{};
    return input_map;
}
constexpr int kKeyNameLength = 15;

namespace {

// NOT ORIGINAL grouping - the keys' names (the original's table at 0x007D42F8). Made on first use, never
// destroyed (key_name_state()).
struct KeyNameState {
    std::array<std::array<wchar_t, kKeyNameLength>, kKeyCount> key_names_{};
};

KeyNameState& key_name_state()
{
    static core::NoDestroy<KeyNameState> state;
    return state.get();
}

std::array<std::array<wchar_t, kKeyNameLength>, kKeyCount>& key_names() { return key_name_state().key_names_; }

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
    options_state().set_distance_setting(distance);
    if (distance > kMaxDistanceSetting) {
        options_state().set_distance_setting(0);
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

// 0x005F2F80
bool has_key_bindings(int port) { return any_binding(core::at(get_controls(), port).keys); }

// 0x005F2FC0
bool has_mouse_bindings(int port) { return any_binding(core::at(get_controls(), port).mouse); }

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
    if (sys::query_registry_value(options, "distance", &type, &value, &size) == ERROR_SUCCESS) {
        place_at_range(value);
    }
    if (sys::query_registry_value(options, "options", &type, &value, &size) == ERROR_SUCCESS) {
        if ((value & 1) != 0U) {
            render::view_setup().set_fog_setting(true);
        }
        if ((value & 2) != 0U) {
            render::view_setup().set_hq_shadows(1);
        }
        if ((value & 4) != 0U) {
            options_state().set_options38_and39(static_cast<uint16_t>((options_state().options38_and39() & kHighByteMask) | 1)); // (its low byte)
        }
        if ((value & kOptionOptions39) != 0U) {
            options_state().set_options38_and39(static_cast<uint16_t>((options_state().options38_and39() & kLowByteMask) | kHighByteOne)); // (its high byte)
        }
        if ((value & kOptionOption37) != 0U) {
            options_state().set_option37(1);
        }
        if ((value & kOptionAntialiasing) != 0U) {
            render::renderer().set_antialiasing(true);
        }
        if ((value & kOptionDistanceClipping) != 0U) {
            render::view_setup().set_distance_clipping(true);
        }
    }
    if (sys::query_registry_value(options, "distance", &type, &value, &size) == ERROR_SUCCESS) {
        place_at_range(value);
    }
    if (sys::query_registry_value(options, "resolution", &type, &value, &size) == ERROR_SUCCESS) {
        render::renderer().set_resolution_setting(value > 3 ? 0 : static_cast<int>(value));
    }

    // The joystick each port plays with (the text buffer isn't cleared before the first: a short value leaves
    // whatever was there after it).
    std::array<char, kJoystickNameSize> text{};
    size = sizeof(text);
    if (sys::query_registry_value(options, "pad0", &type, text.data(), &size) == ERROR_SUCCESS) {
        core::at(get_controls(), 0).joystick = parse_joystick_guid(text.data(), size);
    }
    std::memset(text.data(), 0, sizeof(text));
    size = sizeof(text);
    if (sys::query_registry_value(options, "pad1", &type, text.data(), &size) == ERROR_SUCCESS) {
        core::at(get_controls(), 1).joystick = parse_joystick_guid(text.data(), size);
    }

    // Each port's keys and joystick controls (the ones not there stay as they are).
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            text::format_to(text, "k%i_%i", port, i);
            size = 4;
            if (sys::query_registry_value(options, text.data(), &type, &value, &size) == ERROR_SUCCESS) {
                core::at(core::at(get_controls(), port).keys, i) = static_cast<int32_t>(value);
            }
        }
    }
    for (int port = 0; port < kPorts; ++port) {
        for (int i = 0; i < kBindings; ++i) {
            text::format_to(text, "gp%i_%i", port, i);
            size = 4;
            if (sys::query_registry_value(options, text.data(), &type, &value, &size) == ERROR_SUCCESS) {
                core::at(core::at(get_controls(), port).gamepad, i) = static_cast<int32_t>(value);
            }
        }
    }
    RegCloseKey(options);
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

} // namespace os_layer
