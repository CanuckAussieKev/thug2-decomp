// The PC settings file (partymod.ini).
// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

#include "game/sys/config.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace Config {

namespace {

constexpr size_t kPathLength = 1024;
char s_executableDirectory[kPathLength];
char s_file[kPathLength];

} // namespace

void Init()
{
    GetModuleFileNameA(nullptr, s_executableDirectory, kPathLength);
    // up to the last backslash
    if (char* exe = std::strrchr(s_executableDirectory, '\\'))
        exe[1] = '\0';
    std::snprintf(s_file, sizeof(s_file), "%s%s", s_executableDirectory, kFileName);
}

const char* ExecutableDirectory() { return s_executableDirectory; }

bool GetBool(const char* section, const char* key, bool fallback)
{
    return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, s_file) != 0;
}

int GetInt(const char* section, const char* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, s_file));
}

int GetString(const char* section, const char* key, const char* fallback, char* out, size_t size)
{
    return static_cast<int>(GetPrivateProfileStringA(section, key, fallback, out, static_cast<DWORD>(size), s_file));
}

} // namespace Config
