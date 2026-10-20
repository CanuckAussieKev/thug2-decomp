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
void (*g_crashHook)() = nullptr;
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

bool ConfigSetInt(const char* section, const char* key, int value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%d", value);
    return WritePrivateProfileStringA(section, key, text, GameFolderPath("thug2-decomp.ini").c_str()) != 0;
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

namespace {

// The module an address is in (its file name only) and the address relative to it; false if in none.
bool ModuleOf(uintptr_t address, char (&name)[MAX_PATH], uintptr_t* offset)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module) ||
        !module)
        return false;
    GetModuleFileNameA(module, name, MAX_PATH);
    if (char* slash = strrchr(name, '\\'))
        memmove(name, slash + 1, strlen(slash));
    *offset = address - reinterpret_cast<uintptr_t>(module);
    return true;
}

bool Readable(uintptr_t address, size_t size, bool code)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    DWORD wanted = code ? (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
                        : (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return (info.Protect & wanted) && !(info.Protect & PAGE_GUARD) &&
           address + size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}

// Whether `address` follows a CALL instruction (E8 rel32, FF /2 with a 0-, 1- or 4-byte displacement or SIB).
bool AfterCall(uintptr_t address)
{
    if (!Readable(address - 7, 7, true))
        return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(address);
    if (p[-5] == 0xE8)
        return true;
    for (int length : {2, 3, 4, 6, 7}) {
        if (p[-length] == 0xFF && ((p[-length + 1] >> 3) & 7) == 2)
            return true;
    }
    return false;
}

// Crashes (access violations and other fatal exceptions) are logged before Windows ends the game: where, the
// registers, and the return addresses found on the stack (a scan, not a walk: optimized code has no frame chain -
// some may be stale). First-chance: an exception the code then handles itself (self-tests' guarded runs) is logged
// too. At most a few.
LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* info)
{
    static int logged = 0;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                 code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW ||
                 code == EXCEPTION_PRIV_INSTRUCTION;
    if (!fatal)
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = info->ContextRecord;
    char name[MAX_PATH] = "?";
    uintptr_t offset = c->Eip;
    // Only in the game or our DLL (system DLLs probe memory under their own handlers: IsBadReadPtr...).
    HMODULE ours = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&CrashLogger), &ours);
    HMODULE at = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(c->Eip), &at);
    if (at != ours && at != GetModuleHandleA(nullptr))
        return EXCEPTION_CONTINUE_SEARCH;
    if (logged >= 4) {
        // (logged enough - but the hook still runs: earlier, handled exceptions mustn't keep a real crash's autosave
        // from being written)
        if (g_crashHook)
            g_crashHook();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ++logged;
    ModuleOf(c->Eip, name, &offset);
    Log("EXCEPTION 0x%08X at %s+0x%X (%08X) (first chance: if the code handles it, nothing more happens)",
        static_cast<unsigned>(code), name, static_cast<unsigned>(offset), static_cast<unsigned>(c->Eip));
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        Log("  %s address %08X", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]));
    Log("  EAX %08X EBX %08X ECX %08X EDX %08X ESI %08X EDI %08X EBP %08X ESP %08X",
        static_cast<unsigned>(c->Eax), static_cast<unsigned>(c->Ebx), static_cast<unsigned>(c->Ecx),
        static_cast<unsigned>(c->Edx), static_cast<unsigned>(c->Esi), static_cast<unsigned>(c->Edi),
        static_cast<unsigned>(c->Ebp), static_cast<unsigned>(c->Esp));
    // The top of the stack as it is (arguments, saved registers, locals).
    for (uintptr_t slot = c->Esp; slot < c->Esp + 0x80; slot += 0x20) {
        if (!Readable(slot, 0x20, false))
            break;
        const uint32_t* w = reinterpret_cast<const uint32_t*>(slot);
        Log("  [esp+0x%02X] %08X %08X %08X %08X %08X %08X %08X %08X", static_cast<unsigned>(slot - c->Esp), w[0], w[1],
            w[2], w[3], w[4], w[5], w[6], w[7]);
    }
    Log("  return addresses on the stack (newest first):");
    int found = 0;
    for (uintptr_t slot = c->Esp; found < 24 && slot < c->Esp + 0x2000; slot += 4) {
        if (!Readable(slot, 4, false))
            break;
        uintptr_t value = *reinterpret_cast<const uint32_t*>(slot);
        if (!AfterCall(value) || !ModuleOf(value, name, &offset))
            continue;
        Log("    [esp+0x%X] %s+0x%X (%08X)", static_cast<unsigned>(slot - c->Esp), name, static_cast<unsigned>(offset),
            static_cast<unsigned>(value));
        ++found;
    }
    // (then whoever asked: the level editor's last action and autosave - SetCrashHook)
    if (g_crashHook)
        g_crashHook();
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void LogOpen()
{
    // (fopen_s doesn't share a file open for writing: when another copy of the game has the log - [features]
    // allow_multiple_instances - this one writes thug2-decomp-2.log, -3, ...)
    if (fopen_s(&g_log, GameFolderPath("thug2-decomp.log").c_str(), "w") != 0) {
        for (int copy = 2; copy <= 9 && !g_log; ++copy) {
            std::string name = "thug2-decomp-" + std::to_string(copy) + ".log";
            if (fopen_s(&g_log, GameFolderPath(name.c_str()).c_str(), "w") != 0)
                g_log = nullptr;
        }
    }
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReportHook);
    g_previous_rtc_handler = _RTC_SetErrorFuncW(RtcErrorHandler);
#endif
    AddVectoredExceptionHandler(1, CrashLogger);
}

void SetCrashHook(void (*hook)()) { g_crashHook = hook; }

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
