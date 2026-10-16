#pragma once

// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md

// The PC layer's log ([Logging] in thug2-decomp.ini): messages up to Level (2: info) go into thug2-decomp.log
// (through the hook DLL's Log, as "pc: <message>"), and to the console too when [Miscellaneous] Debug is on.
// Nothing is logged before Configure.
namespace DebugLog {

enum class Level { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

// Read the [Logging] settings; `toConsole`: print to stdout too.
void Configure(bool toConsole);
void Printf(Level level, const char* format, ...);

} // namespace DebugLog
