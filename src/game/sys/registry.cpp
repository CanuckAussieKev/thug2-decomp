// The game's settings in the registry.

#include "game/sys/registry.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace Sys {

namespace {
const char kSettingsKey[] = "Software\\Activision\\Tony Hawk's Underground 2\\Settings";
} // namespace

// 0x004E1810
void WriteDefaultSetting(const char* name, uint32_t value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_WRITE | KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type = REG_DWORD;
        DWORD existing = 0;
        DWORD size = sizeof(existing);
        if (RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&existing), &size) == ERROR_SUCCESS)
            return; // already set (the original leaves the key open here)
        RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
        return;
    }
    char path[256] = {};
    std::snprintf(path, sizeof(path), "%s\\%s", kSettingsKey, name); // (the original's sprintf is unbounded)
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

} // namespace Sys
