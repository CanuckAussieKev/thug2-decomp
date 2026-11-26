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

#include <array>
#include <cstdio>
#endif
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib> // IWYU pragma: keep (_set_abort_behavior and its flags)
#include <initializer_list>
#endif

namespace tests {

#ifdef _WIN32
// A crash (an access violation...) printed to stderr as the exception, the faulting address and the calls as
// module+offset - resolve them with tools/standalone/addr2sym.py against the module's PDB. For the intermittent ones
// that a debugger's timing hides.
inline LONG WINAPI report_crash(EXCEPTION_POINTERS* info)
{
    const auto where = [](const void* address) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = {};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<const char*>(address), &module) != FALSE) {
            GetModuleFileNameA(module, name, MAX_PATH);
            std::fprintf(stderr, "  %p %s+0x%llx\n", address, name,
                         static_cast<unsigned long long>(static_cast<const char*>(address) -
                                                         reinterpret_cast<const char*>(module)));
        } else {
            std::fprintf(stderr, "  %p\n", address);
        }
    };
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "CRASH: exception 0x%08lx at %p", record->ExceptionCode, record->ExceptionAddress);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::fprintf(stderr, " (%s address 0x%llx)", record->ExceptionInformation[0] == 0 ? "reading" : "writing",
                     static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "\n");
    where(record->ExceptionAddress);
    std::array<void*, 48> frames{};
    const USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        where(frames[i]);
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

inline void no_error_dialogs()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(report_crash);
    // no "stopped working", "insert a disk" or "can't open" boxes
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // and no window taking the focus from the user: a check's window (the Vulkan checks') is shown and raised without
    // being activated - SDL reads its hints from the environment, set here before any check starts SDL
    SetEnvironmentVariableW(L"SDL_WINDOW_ACTIVATE_WHEN_SHOWN", L"0");
    SetEnvironmentVariableW(L"SDL_WINDOW_ACTIVATE_WHEN_RAISED", L"0");
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
