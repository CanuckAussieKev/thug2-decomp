#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The PC layer's log ([Logging] in thug2-decomp.ini): messages up to Level (2: info) go into thug2-decomp.log
// (through Log, as "pc: <message>"), and to the console too when [Miscellaneous] Debug is on.
// Nothing is logged before Configure.
namespace debug_log {

enum class StageMap { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

// Read the [Logging] settings; `to_console`: print to stdout too.
void configure(bool to_console);
void printf(StageMap level, const char* format, ...);

} // namespace debug_log
