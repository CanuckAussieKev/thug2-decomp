#pragma once

#include <cstdint>

namespace Sys {

// 0x004E1810: WinMain's default settings, kept in HKEY_CURRENT_USER\Software\Activision\Tony Hawk's
// Underground 2\Settings as DWORDs. If the key exists, `name` is written only when it isn't there yet. If the
// key doesn't, the original creates "Settings\<name>" (a key named after the value - a bug kept as is) and
// writes the value into it.
void WriteDefaultSetting(const char* name, uint32_t value);

} // namespace Sys
