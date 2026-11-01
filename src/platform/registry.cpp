// The game's settings in the registry.

#include "platform/registry.h"
#include "engine/core/raw_view.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <array>

namespace sys {

namespace {
constexpr auto kSettingsKey = std::to_array(R"(Software\Activision\Tony Hawk's Underground 2\Settings)");
} // namespace

// 0x004E1810
void write_default_setting(const char* name, uint32_t value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kSettingsKey.data(), 0, KEY_WRITE | KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type = REG_DWORD;
        DWORD existing = 0;
        DWORD size = sizeof(existing);
        if (RegQueryValueExA(key, name, nullptr, &type, raw::bytes(&existing), &size) == ERROR_SUCCESS)
            return; // already set (the original leaves the key open here)
        RegSetValueExA(key, name, 0, REG_DWORD, raw::bytes(&value), sizeof(value));
        RegCloseKey(key);
        return;
    }
    std::array<char, 256> path = {};
    std::snprintf(path.data(), sizeof(path), "%s\\%s", kSettingsKey.data(), name); // (the original's sprintf is unbounded)
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path.data(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExA(key, name, 0, REG_DWORD, raw::bytes(&value), sizeof(value));
    RegCloseKey(key);
}

} // namespace sys
