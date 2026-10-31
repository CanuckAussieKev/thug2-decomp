// The PC settings (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/platform_config.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace build_info {

namespace {

constexpr size_t kPathLength = 1024;
char s_executable_directory[kPathLength];
char s_file[kPathLength];

} // namespace

void prepare_all()
{
    GetModuleFileNameA(nullptr, s_executable_directory, kPathLength);
    // up to the last backslash
    if (char* exe = std::strrchr(s_executable_directory, '\\'))
        exe[1] = '\0';
    std::snprintf(s_file, sizeof(s_file), "%s%s", s_executable_directory, kFileName);
}

const char* executable_directory() { return s_executable_directory; }

bool fetch_flag(const char* section, const char* key, bool fallback)
{
    return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, s_file) != 0;
}

int get_int(const char* section, const char* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, s_file));
}

int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size)
{
    return static_cast<int>(GetPrivateProfileStringA(section, key, fallback, out, static_cast<DWORD>(size), s_file));
}

} // namespace Config
