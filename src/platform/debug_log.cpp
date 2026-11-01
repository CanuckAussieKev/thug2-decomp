// The PC layer's log.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <array>

#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"

namespace debug_log {

namespace {

bool s_configured = false;
int s_level = static_cast<int>(StageMap::kInfo);
bool s_to_console = false;

} // namespace

void configure(bool to_console)
{
    s_to_console = to_console;
    s_level = build_info::get_int(build_info::kLogging, "Level", 2);
    s_configured = true;
}

bool wants(StageMap level) { return s_configured && static_cast<int>(level) <= s_level; }

bool console_on() { return s_to_console; }

void write_message(const char* message)
{
    std::array<char, 1024> text{};
    std::snprintf(text.data(), sizeof(text), "%s", message);
    size_t length = std::strlen(text.data());
    while (length > 0 && (core::at(text, length - 1) == '\n' || core::at(text, length - 1) == '\r'))
        core::at(text, --length) = '\0';
    get_log("pc: %s", text.data());
}

} // namespace debug_log
