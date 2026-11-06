#pragma once

// NOT ORIGINAL - the checks (src/tests) never stop on a dialog: called first in each one's main(), a failed contract,
// an abort(), a Debug CRT assertion or report, or a crash prints to stderr (or ends the process) and the check exits
// with a non-zero code instead of waiting for someone to press a button - they run from scripts, unattended. (The game's
// own exes do the same through platform/crash.h, into their log.) No-op where none of it applies. Inline: every source
// file belongs to one target, and each check's main() is in its own.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib> // IWYU pragma: keep (_set_abort_behavior and its flags)
#include <initializer_list>
#endif

namespace tests {

inline void no_error_dialogs()
{
#ifdef _WIN32
    // no "stopped working", "insert a disk" or "can't open" boxes
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
#ifdef _MSC_VER
    // abort(): no message box and no fault report (the process ends with exit code 3)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // the Debug CRT's warnings, errors and assertions written to stderr, not shown in a window (no-ops in Release)
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
}

} // namespace tests
