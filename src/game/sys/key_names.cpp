// The keyboard's key names: see key_names.h.

#include "game/sys/key_names.h"

#include <cstddef>
#include <cstdint>

#include "game/sys/platform_input.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Sys {

namespace {

// A key's name (at most 14 characters).
void SetKeyName(unsigned scanCode, const wchar_t* name)
{
    Platform::SetKeyName(static_cast<int>(scanCode), name);
}

struct KeyName {
    uint8_t scanCode;
    const wchar_t* name;
};

// In the order the original sets them; the keypad's names differ by language.
const KeyName kBefore[] = {
    {0x01, L"ESC"}, {0x0F, L"TAB"}, {0x0E, L"BK"}, {0x1C, L"ENT"}, {0x38, L"LA"}, {0xB8, L"RA"}, {0x1D, L"LC"},
    {0x2A, L"LS"},  {0x36, L"RS"},  {0x39, L"SB"}, {0x3A, L"CL"},  {0x45, L"NL"}, {0x46, L"SC"},
};
const KeyName kKeypad[] = {
    {0x47, L"KP7"}, {0x48, L"KP8"}, {0x49, L"KP9"}, {0x4A, L"KP-"}, {0x4B, L"KP4"}, {0x4C, L"KP5"},
    {0x4D, L"KP6"}, {0x37, L"KP*"}, {0x4E, L"KP+"}, {0x4F, L"KP1"}, {0x50, L"KP2"}, {0x51, L"KP3"},
    {0x52, L"KP0"}, {0x53, L"KP."}, {0xB3, L"KP."}, {0xB5, L"KP/"}, {0x9C, L"KPE"},
};
const KeyName kGermanKeypad[] = {
    {0x47, L"ZB7"}, {0x48, L"ZB8"}, {0x49, L"ZB9"}, {0x4A, L"ZB-"}, {0x4B, L"ZB4"}, {0x4C, L"ZB5"},
    {0x4D, L"ZB6"}, {0x37, L"ZB*"}, {0x4E, L"ZB+"}, {0x4F, L"ZB1"}, {0x50, L"ZB2"}, {0x51, L"ZB3"},
    {0x52, L"ZB0"}, {0x53, L"ZB."}, {0xB3, L"ZB."}, {0xB5, L"ZB/"}, {0x9C, L"ZBE"},
};
const KeyName kAfter[] = {
    {0x9D, L"RC"},  {0xC5, L"PAU"}, {0xC7, L"HO"},  {0xC8, L"UP"},  {0xC9, L"PUP"}, {0xCB, L"LFT"},
    {0xCD, L"RGT"}, {0xCF, L"END"}, {0xD0, L"DWN"}, {0xD1, L"PDN"}, {0xD2, L"INS"}, {0xD3, L"DEL"},
};

template <size_t N>
void Set(const KeyName (&names)[N])
{
    for (const KeyName& key : names)
        SetKeyName(key.scanCode, key.name);
}

} // namespace

// 0x004E0DA0
void __cdecl SetKeyNames()
{
    Set(kBefore);
    Set(kKeypad);
    Set(kAfter);
}

// 0x004E0FE0
void __cdecl SetGermanKeyNames()
{
    Set(kBefore);
    Set(kGermanKeypad);
    Set(kAfter);
}

} // namespace Sys

REPLACE(0x004E0DA0, Sys::SetKeyNames);
REPLACE(0x004E0FE0, Sys::SetGermanKeyNames);
