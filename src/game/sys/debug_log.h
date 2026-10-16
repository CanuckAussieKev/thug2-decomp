#pragma once

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

// The PC layer's own log ([Logging] in partymod.ini): to the console when [Miscellaneous] Debug is on, and to
// a file (LogToFile=1, LogFile= - partymod.log by default) next to the exe. Messages up to Level (2: info) are
// kept. Separate from the hook DLL's thug2-decomp.log.
namespace DebugLog {

enum class Level { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

// Read the [Logging] settings; `toConsole`: print to stdout too.
void Configure(bool toConsole);
void Printf(Level level, const char* format, ...);

} // namespace DebugLog
