// The PC settings (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/platform_config.h"

#include <windows.h>
#include "base/core/checked_index.h"
#include <string_view>

#include <cstdio>
#include <cstring>
#include <array>
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace build_info {

namespace {

constexpr size_t kPathLength = 1024;
// NOT ORIGINAL grouping - platform_config.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_config_state()).
struct PlatformConfigState {
    std::array<char, kPathLength> executable_directory_{};
    std::array<char, kPathLength> file_{};
};

PlatformConfigState& platform_config_state()
{
    static core::NoDestroy<PlatformConfigState> state;
    return state.get();
}

} // namespace

void prepare_all()
{
    GetModuleFileNameA(nullptr, platform_config_state().executable_directory_.data(), kPathLength);
    // up to the last backslash
    if (const size_t slash = std::string_view(platform_config_state().executable_directory_.data()).rfind('\\');
        slash != std::string_view::npos) {
        core::at(platform_config_state().executable_directory_, slash + 1) = '\0';
    }
    text::format_to(platform_config_state().file_, "%s%s", platform_config_state().executable_directory_.data(), kFileName);
}

const char* executable_directory() { return platform_config_state().executable_directory_.data(); }

bool fetch_flag(const char* section, const char* key, bool fallback)
{
    return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, platform_config_state().file_.data()) != 0;
}

int get_int(const char* section, const char* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, platform_config_state().file_.data()));
}

int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size)
{
    return static_cast<int>(GetPrivateProfileStringA(section, key, fallback, out, static_cast<DWORD>(size), platform_config_state().file_.data()));
}

} // namespace build_info
