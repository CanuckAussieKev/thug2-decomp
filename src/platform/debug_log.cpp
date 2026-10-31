// The PC layer's log.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "platform/platform_config.h"
#include "platform/run_log.h"

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

void printf(StageMap level, const char* format, ...)
{
    if (!s_configured || static_cast<int>(level) > s_level)
        return;
    va_list args;
    if (s_to_console) {
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
    }
    // Into thug2-decomp.log, one line per message (Log adds the line end the messages carry).
    char text[1024];
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    size_t length = std::strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
        text[--length] = '\0';
    get_log("pc: %s", text);
}

} // namespace debug_log
