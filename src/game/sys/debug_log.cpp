// The PC layer's log.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/sys/debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "game/sys/config.h"
#include "game/sys/log.h"

namespace DebugLog {

namespace {

bool s_configured = false;
int s_level = static_cast<int>(Level::kInfo);
bool s_toConsole = false;

} // namespace

void Configure(bool toConsole)
{
    s_toConsole = toConsole;
    s_level = Config::GetInt(Config::kLogging, "Level", 2);
    s_configured = true;
}

void Printf(Level level, const char* format, ...)
{
    if (!s_configured || static_cast<int>(level) > s_level)
        return;
    va_list args;
    if (s_toConsole) {
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
    Log("pc: %s", text);
}

} // namespace DebugLog
