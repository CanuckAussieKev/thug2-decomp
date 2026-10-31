// NOT ORIGINAL - crash handling: every way the game or this DLL can break is logged and ends the process, never a
// dialog (hook/crash.h).

#include "game/sys/crash.h"

#include <windows.h>

#include <werapi.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <intrin.h>

#ifdef _DEBUG
#include <crtdbg.h>
#include <rtcapi.h>
#endif

#include "game/sys/log.h"

namespace {

bool g_console = false;
void (*g_crashHook)() = nullptr;

// The exception the vectored logger reported last (the unhandled filter doesn't report it twice).
const EXCEPTION_RECORD* g_reportedRecord = nullptr;
uintptr_t g_reportedAddress = 0;

void RunCrashHook()
{
    if (g_crashHook)
        g_crashHook();
}

// The end of every fatal report: the crash hook, the exit code logged, the process ended. A second thread that
// breaks meanwhile waits for the end.
} // namespace

[[noreturn]] void CrashEndProcess(unsigned exitCode)
{
    static std::atomic<long> ending{0};
    if (ending.exchange(1))
        for (;;)
            Sleep(INFINITE);
    RunCrashHook();
    Log("  the process ends (exit code %u = 0x%08X)", exitCode, exitCode);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), exitCode);
    for (;;)
        Sleep(INFINITE);
}

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

// The return addresses found on the stack from `esp` up (a scan, not a walk: optimized code - the game's - has no
// frame chain; some may be stale).
} // namespace

void CrashLogStackScan(uintptr_t esp)
{
    Log("  return addresses on the stack (newest first):");
    char name[MAX_PATH] = "?";
    uintptr_t offset = 0;
    int found = 0;
    for (uintptr_t slot = esp; found < 24 && slot < esp + 0x2000; slot += 4) {
        if (!Readable(slot, 4, false))
            break;
        uintptr_t value = *reinterpret_cast<const uint32_t*>(slot);
        if (!AfterCall(value) || !ModuleOf(value, name, &offset))
            continue;
        Log("    [esp+0x%X] %s+0x%X (%08X)", static_cast<unsigned>(slot - esp), name, static_cast<unsigned>(offset),
            static_cast<unsigned>(value));
        ++found;
    }
}

namespace {

// The calls that led here (a frame walk: fine for our Debug build's code): addresses in our DLL relative to it, the
// game's as they are.
void LogBacktrace()
{
    void* frames[24];
    USHORT count = CaptureStackBackTrace(2, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        char name[MAX_PATH] = "?";
        uintptr_t offset = reinterpret_cast<uintptr_t>(frames[i]);
        ModuleOf(reinterpret_cast<uintptr_t>(frames[i]), name, &offset);
        Log("    at %s+0x%X (%p)", name, static_cast<unsigned>(offset), frames[i]);
    }
}

// An exception's report: where, the registers, the top of the stack, the return addresses on it.
void ReportException(const EXCEPTION_POINTERS* info, const char* note)
{
    const CONTEXT* c = info->ContextRecord;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    char name[MAX_PATH] = "?";
    uintptr_t offset = c->Eip;
    ModuleOf(c->Eip, name, &offset);
    Log("EXCEPTION 0x%08X at %s+0x%X (%08X) %s", static_cast<unsigned>(code), name, static_cast<unsigned>(offset),
        static_cast<unsigned>(c->Eip), note);
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        Log("  %s address %08X",
            info->ExceptionRecord->ExceptionInformation[0] == 8   ? "executing"
            : info->ExceptionRecord->ExceptionInformation[0] != 0 ? "writing"
                                                                  : "reading",
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
    CrashLogStackScan(c->Esp);
}

// Crashes (access violations and other fatal exceptions) are logged before Windows ends the game - first chance: an
// exception the code then handles itself (self-tests' guarded runs) is logged too. At most a few.
LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* info)
{
    static int logged = 0;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    bool anyModule = false;
    if (!IsFatalException(code, &anyModule))
        return EXCEPTION_CONTINUE_SEARCH;
    // Only in the game or our DLL (system DLLs probe memory under their own handlers: IsBadReadPtr...).
    if (!anyModule) {
        HMODULE ours = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&CrashLogger), &ours);
        HMODULE at = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(info->ContextRecord->Eip), &at);
        if (at != ours && at != GetModuleHandleA(nullptr))
            return EXCEPTION_CONTINUE_SEARCH;
    }
    if (logged >= 4) {
        // (logged enough - but the hook still runs: earlier, handled exceptions mustn't keep a real crash's autosave
        // from being written)
        RunCrashHook();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ++logged;
    ReportException(info, "(first chance: if the code handles it, nothing more happens)");
    g_reportedRecord = info->ExceptionRecord;
    g_reportedAddress = info->ContextRecord->Eip;
    // (then whoever asked: the level editor's last action and autosave - SetCrashHook)
    RunCrashHook();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Nothing handled it: the process ends here, before Windows Error Reporting would show its box.
LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (info->ExceptionRecord != g_reportedRecord || info->ContextRecord->Eip != g_reportedAddress)
        ReportException(info, "(unhandled)");
    Log("  unhandled: nothing caught exception 0x%08X", static_cast<unsigned>(code));
    CrashEndProcess(code);
}

void __cdecl InvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                              unsigned int line, uintptr_t)
{
    // (a Release CRT passes none of them)
    Log("INVALID PARAMETER: %ls in %ls (%ls:%u)", expression ? expression : L"?", function ? function : L"?",
        file ? file : L"?", line);
    LogBacktrace();
    CrashEndProcess(3);
}

void __cdecl PureCall()
{
    Log("PURE VIRTUAL CALL (a call through an object's vtable while it's constructed or after it's destroyed)");
    LogBacktrace();
    CrashEndProcess(3);
}

void __cdecl AbortSignal(int)
{
    Log("ABORT (abort() or std::terminate)");
    LogBacktrace();
    CrashEndProcess(3);
}

#ifdef _DEBUG
// Debug C runtime reports: a warning is logged; an assertion is logged and the code carries on (its dialog's Ignore -
// in thug2_check it ends the run); an error (heap corruption...) ends the process.
int __cdecl CrtReportHook(int type, char* message, int* returnValue)
{
    char text[1024];
    strncpy_s(text, message ? message : "", _TRUNCATE);
    for (size_t n = strlen(text); n && (text[n - 1] == '\n' || text[n - 1] == '\r'); --n)
        text[n - 1] = '\0';
    Log("DEBUG CRT REPORT (type %d): %s", type, text);
    if (type == _CRT_WARN)
        return TRUE;
    LogBacktrace();
    if (g_console || type == _CRT_ERROR)
        CrashEndProcess(3);
    if (returnValue)
        *returnValue = 0; // (no debugger break)
    return TRUE;
}

// Run-time check failures (e.g. "ESP was not properly saved across a function call"): logged, the process ends.
int __cdecl RtcErrorHandler(int type, const wchar_t*, int, const wchar_t*, const wchar_t* format, ...)
{
    wchar_t text[512] = L"";
    va_list args;
    va_start(args, format);
    if (format)
        vswprintf(text, 512, format, args);
    va_end(args);
    Log("RUN-TIME CHECK FAILURE #%d: %ls", type, text);
    LogBacktrace();
    CrashEndProcess(3);
}
#endif

#ifdef _DEBUG
void __stdcall PopsItsArgument(int) {}
// A __stdcall function called as __cdecl: it pops its argument and so does the caller - ESP ends up 4 off, which
// the /RTCs check after the call reports.
__declspec(noinline) void CallWithWrongConvention()
{
    auto call = reinterpret_cast<void(__cdecl*)(int)>(&PopsItsArgument);
    call(1);
}
#endif

struct FaultName {
    TestFault fault;
    const char* name;
};
constexpr FaultName kFaultNames[] = {
    {TestFault::Access, "access"},     {TestFault::RunTimeCheck, "rtc"},     {TestFault::CrtError, "crterror"},
    {TestFault::InvalidParameter, "invalidparam"}, {TestFault::PureCall, "purecall"}, {TestFault::Abort, "abort"},
    {TestFault::GameOverrun, "overrun"}, {TestFault::GameCrtMessage, "crtbox"}, {TestFault::FastFail, "fastfail"},
    {TestFault::Hang, "hang"},
};

} // namespace

void SetCrashHook(void (*hook)()) { g_crashHook = hook; }

bool IsFatalException(uint32_t code, bool* anyModule)
{
    *anyModule = false;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_IN_PAGE_ERROR:
        return true;
    case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN (raised the old way; a fast fail never gets here)
    case 0xC0000374: // STATUS_HEAP_CORRUPTION (ntdll's heap)
    case 0xC0000417: // STATUS_INVALID_CRUNTIME_PARAMETER
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        *anyModule = true;
        return true;
    default:
        return false;
    }
}

void InstallCrashHandling(bool console)
{
    g_console = console;
    // No error boxes: not Windows' "stopped working" (nor "insert a disk", "can't open")...
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // ...nor Windows Error Reporting's UI (its reports queued, not shown).
    using WerSetFlagsFn = HRESULT(WINAPI*)(DWORD);
    if (auto werSetFlags =
            reinterpret_cast<WerSetFlagsFn>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "WerSetFlags")))
        werSetFlags(WER_FAULT_REPORTING_FLAG_QUEUE);
    // abort(): no message box, no report - our handler logs it.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, AbortSignal);
    _set_invalid_parameter_handler(InvalidParameter);
    _set_purecall_handler(PureCall);
#ifdef _DEBUG
    // Debug CRT reports: our hook decides; whatever gets past it goes to the log / the console, never a window.
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, console ? _CRTDBG_FILE_STDOUT : static_cast<_HFILE>(LogFileHandle()));
    }
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReportHook);
    _RTC_SetErrorFuncW(RtcErrorHandler);
#endif
    static bool vectored = false;
    if (!vectored) {
        AddVectoredExceptionHandler(1, CrashLogger);
        vectored = true;
    }
    SetUnhandledExceptionFilter(UnhandledFilter);
}

bool ParseTestFault(const std::string& name, TestFault* out)
{
    for (const FaultName& entry : kFaultNames) {
        if (_stricmp(name.c_str(), entry.name) == 0) {
            *out = entry.fault;
            return true;
        }
    }
    return false;
}

const char* TestFaultName(TestFault fault)
{
    for (const FaultName& entry : kFaultNames)
        if (entry.fault == fault)
            return entry.name;
    return "?";
}

const char* TestFaultNames() { return "access rtc crterror invalidparam purecall abort overrun crtbox fastfail hang"; }

bool CauseTestFault(TestFault fault)
{
    Log("crash test: %s", TestFaultName(fault));
    switch (fault) {
    case TestFault::Access:
        *static_cast<volatile int*>(nullptr) = 1;
        break;
    case TestFault::RunTimeCheck:
#ifdef _DEBUG
        CallWithWrongConvention();
        break;
#else
        return false;
#endif
    case TestFault::CrtError:
#ifdef _DEBUG
        _RPT0(_CRT_ERROR, "crash test: a Debug CRT error report");
        break;
#else
        return false;
#endif
    case TestFault::InvalidParameter:
        _invalid_parameter_noinfo();
        break;
    case TestFault::PureCall:
        _purecall();
        break;
    case TestFault::Abort:
        abort();
    case TestFault::GameOverrun:
    case TestFault::GameCrtMessage:
        if (!CauseGameCrtFault(fault))
            return false;
        break;
    case TestFault::FastFail:
        __fastfail(2); // FAST_FAIL_STACK_COOKIE_CHECK_FAILURE
    case TestFault::Hang:
        for (;;)
            Sleep(1000);
    }
    // (a handler returned: nothing ended the process)
    Log("crash test: %s - still running after it", TestFaultName(fault));
    return true;
}

// The standalone build has no original C runtime to point at ours (the hooked build: hook/crash_game.cpp).
void InstallGameCrtHandlers() {}
bool CauseGameCrtFault(TestFault) { return false; }
