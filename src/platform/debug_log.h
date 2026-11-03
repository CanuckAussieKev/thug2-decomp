#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The PC layer's log ([Logging] in thug2-decomp.ini): messages up to Level (2: info) go into thug2-decomp.log
// (through Log, as "pc: <message>"), and to the console too when [Miscellaneous] Debug is on.
// Nothing is logged before Configure.
#include <cstdint>
#include <cstdio>
#include <array>
#include <string>

#include "engine/core/format.h"

namespace debug_log {

constexpr size_t kLineSize = 1024; // the buffer a log line is formatted into

enum class StageMap : uint8_t { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

// Read the [Logging] settings; `to_console`: print to stdout too.
void configure(bool to_console);
// Whether a message of `level` is logged (configured, and within Level).
bool wants(StageMap level);
// Whether messages go to stdout too.
bool console_on();
// A formatted message into thug2-decomp.log ("pc: <message>", its line ends taken off).
void write_message(const char* message);

// (the format checked against the arguments when it's compiled: text::format)
template <typename... Args>
void printf(StageMap level, text::FormatFor<Args...> format, Args... args)
{
    if (!wants(level)) {
        return;
    }
    if (console_on()) {
        std::fputs(text::format(format, args...).c_str(), stdout);
    }
    // Into thug2-decomp.log, one line per message (Log adds the line end the messages carry).
    std::array<char, kLineSize> line{};
    text::format_to(line, format, args...);
    write_message(line.data());
}

} // namespace debug_log
