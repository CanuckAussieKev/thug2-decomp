#include "hook/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#ifdef _DEBUG
#include <crtdbg.h>
#include <rtcapi.h>
#endif

namespace {
FILE* g_log = nullptr;
bool g_console = false;
}

std::string GameFolderPath(const char* file_name)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\'))
        slash[1] = '\0';
    return std::string(path) + file_name;
}

int ConfigInt(const char* section, const char* key, int fallback)
{
    return static_cast<int>(
        GetPrivateProfileIntA(section, key, fallback, GameFolderPath("thug2-decomp.ini").c_str()));
}

#ifdef _DEBUG
namespace {

// The calls that led here: addresses in our DLL relative to it, the game's as they are.
void LogBacktrace()
{
    void* frames[24];
    USHORT count = CaptureStackBackTrace(2, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(frames[i]), &module);
        char name[MAX_PATH] = "?";
        if (module) {
            GetModuleFileNameA(module, name, MAX_PATH);
            if (char* slash = strrchr(name, '\\'))
                memmove(name, slash + 1, strlen(slash));
        }
        Log("    at %s+0x%X (%p)", name,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)),
            frames[i]);
    }
}

// Debug C runtime reports (assertions, errors): logged, then shown as usual.
int __cdecl CrtReportHook(int type, char* message, int* returnValue)
{
    (void)returnValue;
    Log("DEBUG CRT REPORT (type %d): %s", type, message ? message : "");
    LogBacktrace();
    // Tools run unattended: an assertion or error ends the run (exit code 3) instead of waiting on a dialog.
    if (g_console && type != _CRT_WARN) {
        std::fflush(stdout);
        _exit(3);
    }
    return FALSE;
}

// Run-time check failures (e.g. "ESP was not properly saved across a function call"): logged, then shown as usual.
_RTC_error_fnW g_previous_rtc_handler = nullptr;
int __cdecl RtcErrorHandler(int type, const wchar_t* file, int line, const wchar_t* module, const wchar_t* format, ...)
{
    wchar_t text[512] = L"";
    va_list args;
    va_start(args, format);
    if (format)
        vswprintf(text, 512, format, args);
    va_end(args);
    Log("RUN-TIME CHECK FAILURE #%d: %ls", type, text);
    LogBacktrace();
    if (g_console) {
        std::fflush(stdout);
        _exit(3);
    }
    if (g_previous_rtc_handler) {
        // (the usual dialog, with the message already formatted)
        return g_previous_rtc_handler(type, file, line, module, L"%ls", text);
    }
    return 0;
}

} // namespace
#endif

void LogOpen()
{
    fopen_s(&g_log, GameFolderPath("thug2-decomp.log").c_str(), "w");
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReportHook);
    g_previous_rtc_handler = _RTC_SetErrorFuncW(RtcErrorHandler);
#endif
}

void LogToConsole()
{
    g_console = true;
    // No dialogs in tools: CRT reports and run-time check failures are printed and end the run, crashes
    // and abort() exit without the Windows error box.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReportHook);
    _RTC_SetErrorFuncW(RtcErrorHandler);
#endif
}

void Log(const char* fmt, ...)
{
    if (g_console) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        std::putchar('\n');
        return;
    }
    if (!g_log)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}
