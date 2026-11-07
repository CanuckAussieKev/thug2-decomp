#pragma once

#include <string>

// NOT ORIGINAL - the keys thug2-decomp.ini names ([Keybinds]: SDL scan codes, or key names where a key's name is
// taken) as SDL 3 numbers them. The settings were written for SDL 2: SDL 3 keeps its scan codes and names except the
// media and application keys (SDL 2's 258-286), which it renumbered, renamed or dropped - an SDL 2 number or name
// still means the key it meant on Windows.
namespace platform {

// An SDL 2 scan code number -> SDL 3's for the same key (0: a key SDL 3 doesn't have - mail, calculator, brightness...).
int scancode_from_sdl2(int code);
// A key's name -> SDL 3's scan code: SDL 3's names, and SDL 2's for the keys SDL 3 renamed ("AudioPlay"...); 0 when
// unknown.
int scancode_from_name(const std::string& name);

} // namespace platform
