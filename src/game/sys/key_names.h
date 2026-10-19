#pragma once

// The short names of the keyboard's special keys, by scan code (shown in the controller setup).
namespace Sys {

// 0x004E0DA0: the names in English (also French's).
void __cdecl SetKeyNames();
// 0x004E0FE0: the names in German (the keypad's "ZB" for Ziffernblock).
void __cdecl SetGermanKeyNames();

} // namespace Sys
