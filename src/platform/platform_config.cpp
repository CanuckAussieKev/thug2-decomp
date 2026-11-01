// The PC settings (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/platform_config.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <array>

namespace build_info {

namespace {

constexpr size_t kPathLength = 1024;
std::array<char, kPathLength> s_executable_directory;
std::array<char, kPathLength> s_file;

} // namespace

void prepare_all()
{
    GetModuleFileNameA(nullptr, s_executable_directory.data(), kPathLength);
    // up to the last backslash
    if (char* exe = std::strrchr(s_executable_directory.data(), '\\'))
        exe[1] = '\0';
    std::snprintf(s_file.data(), sizeof(s_file), "%s%s", s_executable_directory.data(), kFileName);
}

const char* executable_directory() { return s_executable_directory.data(); }

bool fetch_flag(const char* section, const char* key, bool fallback)
{
    return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, s_file.data()) != 0;
}

int get_int(const char* section, const char* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, s_file.data()));
}

int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size)
{
    return static_cast<int>(GetPrivateProfileStringA(section, key, fallback, out, static_cast<DWORD>(size), s_file.data()));
}

} // namespace build_info
