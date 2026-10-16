// The PC layer's own log.
// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

#include "game/sys/debug_log.h"

#include <cstdarg>
#include <cstdio>

#include "game/sys/config.h"

namespace DebugLog {

namespace {

int s_level = static_cast<int>(Level::kTrace);
FILE* s_file = nullptr;
bool s_toConsole = false;

} // namespace

void Configure(bool toConsole)
{
    s_toConsole = toConsole;
    s_level = Config::GetInt(Config::kLogging, "Level", 2);
    if (!Config::GetBool(Config::kLogging, "LogToFile", false))
        return;
    char name[256] = {};
    if (Config::GetString(Config::kLogging, "LogFile", "partymod.log", name, sizeof(name))) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%s%s", Config::ExecutableDirectory(), name);
#pragma warning(suppress : 4996) // (fopen: the file stays open for the whole run)
        s_file = std::fopen(path, "w");
    }
}

void Printf(Level level, const char* format, ...)
{
    if (static_cast<int>(level) > s_level)
        return;
    va_list args;
    if (s_toConsole) {
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
    }
    if (s_file) {
        va_start(args, format);
        std::vfprintf(s_file, format, args);
        va_end(args);
    }
}

} // namespace DebugLog
