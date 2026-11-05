#pragma once

#include <cstdint>

namespace sys {

// 0x004E1810: WinMain's default settings, kept in HKEY_CURRENT_USER\Software\Activision\Tony Hawk's
// Underground 2\Settings as DWORDs. If the key exists, `name` is written only when it isn't there yet. If the
// key doesn't, the original creates "Settings\<name>" (a key named after the value - a bug kept as is) and
// writes the value into it.
void write_default_setting(const char* name, uint32_t value);

// RegQueryValueExA / RegSetValueExA over the value's own bytes: `data` holds `*size` bytes (the value's size comes
// back there); `type` may be null; `key` an open HKEY. The Win32 result (ERROR_SUCCESS: 0).
long query_registry_value(void* key, const char* name, unsigned long* type, void* data, unsigned long* size);
long set_registry_value(void* key, const char* name, unsigned long type, const void* data, unsigned long size);

} // namespace sys
