#pragma once

// The short names of the keyboard's special keys, by scan code (shown in the controller setup).
namespace sys {

// 0x004E0DA0: the names in English (also French's).
void __cdecl set_key_names();
// 0x004E0FE0: the names in German (the keypad's "ZB" for Ziffernblock).
void __cdecl set_german_key_names();

} // namespace sys
