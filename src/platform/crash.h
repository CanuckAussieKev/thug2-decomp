#pragma once

#include <cstdint>
#include <string>

// NOT ORIGINAL - what happens when the game breaks: it's logged and the process ends, never a dialog
// that waits for someone (the workspaces' games run unattended - an agent's run that hung on "Buffer overrun
// detected!" or Windows' "stopped working" box had to be found and killed by hand).
//
// Every report goes to thug2-decomp.log in a form tools/symbolize.ts reads - a header line, then indented lines with
// the <module>+0x... addresses:
//   EXCEPTION 0x... at <module>+0x...      a fatal exception, first chance (code that handles it carries on) - the
//                                          registers, the top of the stack and the return addresses found on it;
//                                          "  unhandled: ..." follows when nothing handled it and the process ends
//   RUN-TIME CHECK FAILURE #n: ...         a Debug build's /RTC check (ESP not saved, a variable used uninitialised...)
//   DEBUG CRT REPORT (type n): ...         a Debug CRT assertion (logged, then carries on, as its dialog's Ignore) or
//                                          error (the process ends)
//   INVALID PARAMETER: ...                 a CRT function given an invalid argument
//   PURE VIRTUAL CALL                      a call through a destroyed object's vtable
//   ABORT                                  abort() (also std::terminate)
//   BUFFER OVERRUN DETECTED ...            the game's own /GS check (its VC7.1 CRT's __security_error_handler 0x006287DD,
//                                          which would show "Buffer overrun detected!" and wait): one of the game's
//                                          functions found its stack cookie overwritten when returning
//   GAME CRT MESSAGE: ...                  anything else the game's CRT would show in a message box (R6025 pure virtual
//                                          function call, R6002...: its __crtMessageBoxA 0x00634205)
// then "  the process ends (exit code ...)". Exit codes: 3 for the reports above, 0xC0000409 for a
// buffer overrun, the exception's code for an unhandled exception.
//
// Not catchable: our own build's /GS failures and other fast-fail exits (int 29h ends the process at once - no
// handler runs). Windows Error Reporting is told not to show anything (SetErrorMode, WerSetFlags); its dump may still
// be written to %LOCALAPPDATA%\CrashDumps when LocalDumps is set up: tools/pipeline/game/read_dump.ts reads it.

// Installs everything: the vectored exception logger, the unhandled-exception filter, the CRT's invalid-parameter /
// purecall / abort handlers, the Debug CRT's report and run-time check hooks, no error boxes. `console`: reports to
// stdout.
void install_crash_handling(bool console);

// Whether the vectored logger reports this exception code (the fatal ones). `any_module`: also when it's raised outside
// the game (heap corruption, a raised buffer overrun, an invalid CRT parameter - they come from ntdll /
// the CRT); else only from the game (system DLLs probe memory under their own handlers).
bool is_fatal_exception(uint32_t code, bool* any_module);

// Deliberate faults, to check that all of the above works: the automation channel's `crash <kind>` (workspaces only -
// src/frontend/automation.h).
enum class TestFault {
    kAccess,           // a write to address 0: EXCEPTION, then unhandled
    kRunTimeCheck,     // a call that leaves ESP wrong (Debug /RTC builds): RUN-TIME CHECK FAILURE
    kCrtError,         // _RPT0(_CRT_ERROR, ...) (Debug CRT): DEBUG CRT REPORT (type 1)
    kInvalidParameter, // _invalid_parameter_noinfo: INVALID PARAMETER
    kPureCall,         // _purecall: PURE VIRTUAL CALL
    kCancelNow,            // abort(): ABORT
    kGameOverrun,      // the game's __security_error_handler(1, 0): BUFFER OVERRUN DETECTED
    kGameCrtMessage,   // the game's __crtMessageBoxA: GAME CRT MESSAGE
    kFastFail,         // __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE): ends at once, nothing logged (our /GS)
    kHang,             // the calling thread sleeps for ever (tools' not-responding detection)
};
// "access", "rtc", "crterror", "invalidparam", "purecall", "abort", "overrun", "crtbox", "fastfail", "hang".
bool parse_test_fault(const std::string& name, TestFault* out);
const char* test_fault_name(TestFault fault);
// The kinds, space-separated (for messages).
const char* test_fault_names();
// Logs "crash test: <kind>" and causes it. Returns false for a kind this build can't cause (kRunTimeCheck and kCrtError
// in a Release build), true if it was caused and the process still runs (a handler that didn't end it: a bug).
bool cause_test_fault(TestFault fault);

// The end of every fatal report: the crash hook, the exit code logged, the
// process ended.
[[noreturn]] void crash_end_process(unsigned exit_code);
// The return addresses found on the stack from `esp` up, logged.
void crash_log_stack_scan(uintptr_t esp);

// The two faults that went through the original's C runtime (kGameOverrun, kGameCrtMessage): there is no such runtime
// here, so they return false (not caused).
bool cause_game_crt_fault(TestFault fault);
