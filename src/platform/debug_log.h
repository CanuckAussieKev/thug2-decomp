#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The PC layer's log ([Logging] in thug2-decomp.ini): messages up to Level (2: info) go into thug2-decomp.log
// (through Log, as "pc: <message>"), and to the console too when [Miscellaneous] Debug is on.
// Nothing is logged before Configure.
#include <cstdint>
#include <cstdio>
#include <array>

namespace debug_log {

enum class StageMap : uint8_t { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

// Read the [Logging] settings; `to_console`: print to stdout too.
void configure(bool to_console);
// Whether a message of `level` is logged (configured, and within Level).
bool wants(StageMap level);
// Whether messages go to stdout too.
bool console_on();
// A formatted message into thug2-decomp.log ("pc: <message>", its line ends taken off).
void write_message(const char* message);

template <typename... Args>
void printf(StageMap level, const char* format, Args... args)
{
    if (!wants(level))
        return;
    if (console_on())
        std::printf(format, args...);
    // Into thug2-decomp.log, one line per message (Log adds the line end the messages carry).
    std::array<char, 1024> text{};
    std::snprintf(text.data(), sizeof(text), format, args...);
    write_message(text.data());
}

} // namespace debug_log
