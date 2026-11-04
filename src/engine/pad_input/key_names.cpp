// The keyboard's key names: see key_names.h.

#include "engine/pad_input/key_names.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/pad_input/direct_input.h"

namespace sys {

namespace {

// A key's name (at most 14 characters).
void set_key_name(unsigned scan_code, const wchar_t* name)
{
    os_layer::set_key_name(static_cast<int>(scan_code), name);
}

struct KeyName {
    uint8_t scan_code_;
    const wchar_t* name;
};

// In the order the original sets them; the keypad's names differ by language.
constexpr auto kBefore = std::to_array<KeyName>({
    {.scan_code_ = 0x01, .name = L"ESC"}, {.scan_code_ = 0x0F, .name = L"TAB"}, {.scan_code_ = 0x0E, .name = L"BK"}, {.scan_code_ = 0x1C, .name = L"ENT"}, {.scan_code_ = 0x38, .name = L"LA"}, {.scan_code_ = 0xB8, .name = L"RA"}, {.scan_code_ = 0x1D, .name = L"LC"},
    {.scan_code_ = 0x2A, .name = L"LS"},  {.scan_code_ = 0x36, .name = L"RS"},  {.scan_code_ = 0x39, .name = L"SB"}, {.scan_code_ = 0x3A, .name = L"CL"},  {.scan_code_ = 0x45, .name = L"NL"}, {.scan_code_ = 0x46, .name = L"SC"},
});
constexpr auto kKeypad = std::to_array<KeyName>({
    {.scan_code_ = 0x47, .name = L"KP7"}, {.scan_code_ = 0x48, .name = L"KP8"}, {.scan_code_ = 0x49, .name = L"KP9"}, {.scan_code_ = 0x4A, .name = L"KP-"}, {.scan_code_ = 0x4B, .name = L"KP4"}, {.scan_code_ = 0x4C, .name = L"KP5"},
    {.scan_code_ = 0x4D, .name = L"KP6"}, {.scan_code_ = 0x37, .name = L"KP*"}, {.scan_code_ = 0x4E, .name = L"KP+"}, {.scan_code_ = 0x4F, .name = L"KP1"}, {.scan_code_ = 0x50, .name = L"KP2"}, {.scan_code_ = 0x51, .name = L"KP3"},
    {.scan_code_ = 0x52, .name = L"KP0"}, {.scan_code_ = 0x53, .name = L"KP."}, {.scan_code_ = 0xB3, .name = L"KP."}, {.scan_code_ = 0xB5, .name = L"KP/"}, {.scan_code_ = 0x9C, .name = L"KPE"},
});
constexpr auto kGermanKeypad = std::to_array<KeyName>({
    {.scan_code_ = 0x47, .name = L"ZB7"}, {.scan_code_ = 0x48, .name = L"ZB8"}, {.scan_code_ = 0x49, .name = L"ZB9"}, {.scan_code_ = 0x4A, .name = L"ZB-"}, {.scan_code_ = 0x4B, .name = L"ZB4"}, {.scan_code_ = 0x4C, .name = L"ZB5"},
    {.scan_code_ = 0x4D, .name = L"ZB6"}, {.scan_code_ = 0x37, .name = L"ZB*"}, {.scan_code_ = 0x4E, .name = L"ZB+"}, {.scan_code_ = 0x4F, .name = L"ZB1"}, {.scan_code_ = 0x50, .name = L"ZB2"}, {.scan_code_ = 0x51, .name = L"ZB3"},
    {.scan_code_ = 0x52, .name = L"ZB0"}, {.scan_code_ = 0x53, .name = L"ZB."}, {.scan_code_ = 0xB3, .name = L"ZB."}, {.scan_code_ = 0xB5, .name = L"ZB/"}, {.scan_code_ = 0x9C, .name = L"ZBE"},
});
constexpr auto kAfter = std::to_array<KeyName>({
    {.scan_code_ = 0x9D, .name = L"RC"},  {.scan_code_ = 0xC5, .name = L"PAU"}, {.scan_code_ = 0xC7, .name = L"HO"},  {.scan_code_ = 0xC8, .name = L"UP"},  {.scan_code_ = 0xC9, .name = L"PUP"}, {.scan_code_ = 0xCB, .name = L"LFT"},
    {.scan_code_ = 0xCD, .name = L"RGT"}, {.scan_code_ = 0xCF, .name = L"END"}, {.scan_code_ = 0xD0, .name = L"DWN"}, {.scan_code_ = 0xD1, .name = L"PDN"}, {.scan_code_ = 0xD2, .name = L"INS"}, {.scan_code_ = 0xD3, .name = L"DEL"},
});

template <size_t N>
void assign_value(const std::array<KeyName, N>& names)
{
    for (const KeyName& key : names) {
        set_key_name(key.scan_code_, key.name);
    }
}

} // namespace

// 0x004E0DA0
void set_key_names()
{
    assign_value(kBefore);
    assign_value(kKeypad);
    assign_value(kAfter);
}

// 0x004E0FE0
void set_german_key_names()
{
    assign_value(kBefore);
    assign_value(kGermanKeypad);
    assign_value(kAfter);
}

} // namespace sys
