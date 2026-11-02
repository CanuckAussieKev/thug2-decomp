// NOT ORIGINAL - crash handling: every way the game can break is logged and ends the process, never a
// dialog (platform/crash.h).

#include "platform/crash.h"

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
#include <array>
#include <mutex>
#include <source_location>
#include <span>
#include <string_view>
#endif

#include "platform/run_log.h"
#include "platform/compiler.h"
#include "engine/core/pointer_cell.h"
#include "platform/code_address.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"

namespace {

bool g_console = false;
void (*g_crash_hook)() = nullptr;

// The exception the vectored logger reported last (the unhandled filter doesn't report it twice).
const EXCEPTION_RECORD* g_reported_record = nullptr;
uintptr_t g_reported_address = 0;

void run_crash_hook()
{
    if (g_crash_hook)
        g_crash_hook();
}

// The end of every fatal report: the crash hook, the exit code logged, the process ended. A second thread that
// breaks meanwhile waits for the end.
} // namespace

[[noreturn]] void crash_end_process(unsigned exit_code)
{
    static std::atomic<long> ending{0};
    if (ending.exchange(1))
        for (;;)
            Sleep(INFINITE);
    run_crash_hook();
    get_log("  the process ends (exit code %u = 0x%08X)", exit_code, exit_code);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), exit_code);
    for (;;)
        Sleep(INFINITE);
}

namespace {

// The module an address is in (its file name only) and the address relative to it; false if in none.
bool module_of(uintptr_t address, std::array<char, MAX_PATH>& name, uintptr_t* offset)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            cell::pointer<const char>(static_cast<uint32_t>(address)), &module) ||
        !module)
        return false;
    GetModuleFileNameA(module, name.data(), MAX_PATH);
    const std::string_view full(name.data());
    if (const size_t slash = full.rfind('\\'); slash != std::string_view::npos) {
        const std::string_view file = full.substr(slash + 1);
        memmove(name.data(), file.data(), file.size() + 1);
    }
    *offset = address - cell::address(module);
    return true;
}

bool readable(uintptr_t address, size_t size, bool code)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(cell::pointer<void>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    const DWORD wanted = code ? (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
                              : (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return (info.Protect & wanted) && !(info.Protect & PAGE_GUARD) &&
           address + size <= cell::address(info.BaseAddress) + info.RegionSize;
}

// Whether `address` follows a CALL instruction (E8 rel32, FF /2 with a 0-, 1- or 4-byte displacement or SIB).
bool after_call(uintptr_t address)
{
    if (!readable(address - 7, 7, true))
        return false;
    const std::span<const uint8_t, 7> before(cell::pointer<const uint8_t>(address - 7), 7); // (the 7 bytes before)
    if (core::at(before, 2) == 0xE8)
        return true;
    for (const int length : {2, 3, 4, 6, 7}) {
        if (core::at(before, 7 - length) == 0xFF && ((core::at(before, 8 - length) >> 3) & 7) == 2)
            return true;
    }
    return false;
}

// The return addresses found on the stack from `esp` up (a scan, not a walk: optimized code - the game's - has no
// frame chain; some may be stale).
} // namespace

void crash_log_stack_scan(uintptr_t esp)
{
    get_log("  return addresses on the stack (newest first):");
    std::array<char, MAX_PATH> name = {"?"};
    uintptr_t offset = 0;
    int found = 0;
    for (uintptr_t slot = esp; found < 24 && slot < esp + 0x2000; slot += 4) {
        if (!readable(slot, 4, false))
            break;
        const uintptr_t value = *cell::pointer<const uint32_t>(slot);
        if (!after_call(value) || !module_of(value, name, &offset))
            continue;
        get_log("    [esp+0x%X] %s+0x%X (%08X)", static_cast<unsigned>(slot - esp), name.data(), static_cast<unsigned>(offset),
                static_cast<unsigned>(value));
        ++found;
    }
}

namespace {

// The calls that led here (a frame walk: fine for our Debug build's code): addresses in our DLL relative to it, the
// game's as they are.
void log_backtrace()
{
    std::array<void*, 24> frames{};
    const USHORT count = CaptureStackBackTrace(2, 24, frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        std::array<char, MAX_PATH> name = {"?"};
        auto offset = cell::address(core::at(frames, i));
        module_of(cell::address(core::at(frames, i)), name, &offset);
        get_log("    at %s+0x%X (%p)", name.data(), static_cast<unsigned>(offset), core::at(frames, i));
    }
}

// An exception's report: where, the registers, the top of the stack, the return addresses on it.
void report_exception(const EXCEPTION_POINTERS* info, const char* note)
{
    const CONTEXT* c = info->ContextRecord;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    std::array<char, MAX_PATH> name = {"?"};
    uintptr_t offset = c->Eip;
    module_of(c->Eip, name, &offset);
    get_log("EXCEPTION 0x%08X at %s+0x%X (%08X) %s", static_cast<unsigned>(code), name.data(), static_cast<unsigned>(offset),
            static_cast<unsigned>(c->Eip), note);
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        get_log("  %s address %08X",
                info->ExceptionRecord->ExceptionInformation[0] == 8   ? "executing"
                : info->ExceptionRecord->ExceptionInformation[0] != 0 ? "writing"
                                                                      : "reading",
                static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]));
    get_log("  EAX %08X EBX %08X ECX %08X EDX %08X ESI %08X EDI %08X EBP %08X ESP %08X", static_cast<unsigned>(c->Eax),
            static_cast<unsigned>(c->Ebx), static_cast<unsigned>(c->Ecx), static_cast<unsigned>(c->Edx),
            static_cast<unsigned>(c->Esi), static_cast<unsigned>(c->Edi), static_cast<unsigned>(c->Ebp),
            static_cast<unsigned>(c->Esp));
    // The top of the stack as it is (arguments, saved registers, locals).
    for (uintptr_t slot = c->Esp; slot < c->Esp + 0x80; slot += 0x20) {
        if (!readable(slot, 0x20, false))
            break;
        const std::span<const uint32_t, 8> w(cell::pointer<const uint32_t>(slot), 8);
        get_log("  [esp+0x%02X] %08X %08X %08X %08X %08X %08X %08X %08X", static_cast<unsigned>(slot - c->Esp),
                core::at(w, 0), core::at(w, 1), core::at(w, 2), core::at(w, 3), core::at(w, 4), core::at(w, 5),
                core::at(w, 6), core::at(w, 7));
    }
    crash_log_stack_scan(c->Esp);
}

// Crashes (access violations and other fatal exceptions) are logged before Windows ends the game - first chance: an
// exception the code then handles itself (self-tests' guarded runs) is logged too. At most a few.
LONG CALLBACK crash_logger(EXCEPTION_POINTERS* info)
{
    static int logged = 0;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    bool any_module = false;
    if (!is_fatal_exception(code, &any_module))
        return EXCEPTION_CONTINUE_SEARCH;
    // Only in the game or our DLL (system DLLs probe memory under their own handlers: IsBadReadPtr...).
    if (!any_module) {
        HMODULE ours = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(code_address(&crash_logger)), &ours);
        HMODULE at = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           cell::pointer<const char>(info->ContextRecord->Eip), &at);
        if (at != ours && at != GetModuleHandleA(nullptr))
            return EXCEPTION_CONTINUE_SEARCH;
    }
    if (logged >= 4) {
        // (logged enough - but the hook still runs: earlier, handled exceptions mustn't keep a real crash's autosave
        // from being written)
        run_crash_hook();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ++logged;
    report_exception(info, "(first chance: if the code handles it, nothing more happens)");
    g_reported_record = info->ExceptionRecord;
    g_reported_address = info->ContextRecord->Eip;
    // (then whoever asked: the level editor's last action and autosave - set_crash_hook)
    run_crash_hook();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Nothing handled it: the process ends here, before Windows Error Reporting would show its box.
LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (info->ExceptionRecord != g_reported_record || info->ContextRecord->Eip != g_reported_address)
        report_exception(info, "(unhandled)");
    get_log("  unhandled: nothing caught exception 0x%08X", static_cast<unsigned>(code));
    crash_end_process(code);
}

void invalid_parameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line,
                       uintptr_t)
{
    // (a Release CRT passes none of them)
    get_log("INVALID PARAMETER: %ls in %ls (%ls:%u)", expression ? expression : L"?", function ? function : L"?",
            file ? file : L"?", line);
    log_backtrace();
    crash_end_process(3);
}

void pure_call()
{
    get_log("PURE VIRTUAL CALL (a call through an object's vtable while it's constructed or after it's destroyed)");
    log_backtrace();
    crash_end_process(3);
}

void abort_signal(int)
{
    get_log("ABORT (abort() or std::terminate)");
    log_backtrace();
    crash_end_process(3);
}

#ifdef _DEBUG
// Debug C runtime reports: a warning is logged; an assertion is logged and the code carries on (its dialog's Ignore -
// on the console it ends the run); an error (heap corruption...) ends the process.
int crt_report_hook(int type, char* message, int* return_value)
{
    std::array<char, 1024> text{};
    strncpy_s(text.data(), text.size(), message ? message : "", _TRUNCATE);
    for (size_t n = strlen(text.data()); n && (core::at(text, n - 1) == '\n' || core::at(text, n - 1) == '\r'); --n)
        core::at(text, n - 1) = '\0';
    get_log("DEBUG CRT REPORT (type %d): %s", type, text.data());
    if (type == _CRT_WARN)
        return TRUE;
    log_backtrace();
    if (g_console || type == _CRT_ERROR)
        crash_end_process(3);
    if (return_value)
        *return_value = 0; // (no debugger break)
    return TRUE;
}

// Run-time check failures (e.g. "ESP was not properly saved across a function call"): logged, the process ends.
// NOLINTNEXTLINE(modernize-avoid-variadic-functions): the C runtime's callback type (_RTC_error_fnW) is variadic
int rtc_error_handler(int type, const wchar_t*, int, const wchar_t*, const wchar_t* format, ...)
{
    std::array<wchar_t, 512> text{};
    va_list args = nullptr;
    va_start(args, format);
    if (format)
        vswprintf(text.data(), text.size(), format, args);
    va_end(args);
    get_log("RUN-TIME CHECK FAILURE #%d: %ls", type, text.data());
    log_backtrace();
    crash_end_process(3);
}
#endif

#ifdef _DEBUG
void __stdcall pops_its_argument(int) {}
// A __stdcall function called as __cdecl: it pops its argument and so does the caller - ESP ends up 4 off, which
// the /RTCs check after the call reports.
THUG2_NOINLINE void call_with_wrong_convention()
{
    auto call = function_at<void (*)(int)>(code_address(&pops_its_argument));
    call(1);
}
#endif

struct FaultName {
    TestFault fault;
    const char* name;
};
constexpr auto kFaultNames = std::to_array<FaultName>({
    {TestFault::kAccess, "access"},       {TestFault::kRunTimeCheck, "rtc"},
    {TestFault::kCrtError, "crterror"},   {TestFault::kInvalidParameter, "invalidparam"},
    {TestFault::kPureCall, "purecall"},   {TestFault::kCancelNow, "abort"},
    {TestFault::kGameOverrun, "overrun"}, {TestFault::kGameCrtMessage, "crtbox"},
    {TestFault::kFastFail, "fastfail"},   {TestFault::kHang, "hang"},
});

} // namespace

void set_crash_hook(void (*hook)()) { g_crash_hook = hook; }

bool is_fatal_exception(uint32_t code, bool* any_module)
{
    *any_module = false;
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
        *any_module = true;
        return true;
    default:
        return false;
    }
}

namespace {
// [debug] soft_bounds=1 (engine/core/checked_index.h): an index outside an array is logged once per place, with the
// calls that led there, and the game carries on.
struct SeenPlaces {
    static constexpr size_t kPlaces = 256;
    std::mutex lock;
    std::array<std::source_location, kPlaces> places{};
    size_t count = 0;
};

SeenPlaces& seen_places()
{
    static SeenPlaces seen;
    return seen;
}

// True the first time a place is reported.
bool note_place(const std::source_location& where)
{
    SeenPlaces& seen = seen_places();
    const std::scoped_lock guard(seen.lock);
    for (size_t i = 0; i < seen.count; ++i) {
        const std::source_location& place = core::at(seen.places, i);
        if (place.line() == where.line() && std::string_view(place.file_name()) == where.file_name())
            return false;
    }
    if (seen.count < SeenPlaces::kPlaces)
        core::at(seen.places, seen.count++) = where;
    return true;
}

void report_out_of_range(const std::source_location& where, std::ptrdiff_t index, std::ptrdiff_t extent)
{
    if (!note_place(where))
        return;
    get_log("OUT OF RANGE (soft_bounds: carried on): index %td of %td at %s:%u (%s)", index, extent, where.file_name(),
            static_cast<unsigned>(where.line()), where.function_name());
    log_backtrace();
}
} // namespace

void install_crash_handling(bool console)
{
    g_console = console;
    if (config_int("debug", "soft_bounds", 0) != 0) {
        core::set_out_of_range_handler(report_out_of_range);
        get_log("soft_bounds: an index outside an array is logged and the game carries on ([debug] soft_bounds=1)");
    }
    // No error boxes: not Windows' "stopped working" (nor "insert a disk", "can't open")...
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // ...nor Windows Error Reporting's UI (its reports queued, not shown).
    using WerSetFlagsFn = HRESULT(WINAPI*)(DWORD);
    if (auto wer_set_flags =
            crt::library_function<WerSetFlagsFn>("kernel32.dll", "WerSetFlags"))
        wer_set_flags(WER_FAULT_REPORTING_FLAG_QUEUE);
    // abort(): no message box, no report - our handler logs it.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, abort_signal);
    _set_invalid_parameter_handler(invalid_parameter);
    _set_purecall_handler(pure_call);
#ifdef _DEBUG
    // Debug CRT reports: our hook decides; whatever gets past it goes to the log / the console, never a window.
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, console ? _CRTDBG_FILE_STDOUT : static_cast<_HFILE>(log_file_handle()));
    }
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, crt_report_hook);
    _RTC_SetErrorFuncW(rtc_error_handler);
#endif
    static bool vectored = false;
    if (!vectored) {
        AddVectoredExceptionHandler(1, crash_logger);
        vectored = true;
    }
    SetUnhandledExceptionFilter(unhandled_filter);
}

bool parse_test_fault(const std::string& name, TestFault* out)
{
    for (const FaultName& entry : kFaultNames) {
        if (_stricmp(name.c_str(), entry.name) == 0) {
            *out = entry.fault;
            return true;
        }
    }
    return false;
}

const char* test_fault_name(TestFault fault)
{
    for (const FaultName& entry : kFaultNames)
        if (entry.fault == fault)
            return entry.name;
    return "?";
}

const char* test_fault_names()
{
    return "access rtc crterror invalidparam purecall abort overrun crtbox fastfail hang";
}

bool cause_test_fault(TestFault fault)
{
    get_log("crash test: %s", test_fault_name(fault));
    switch (fault) {
    case TestFault::kAccess:
        *static_cast<volatile int*>(nullptr) = 1;
        break;
    case TestFault::kRunTimeCheck:
#ifdef _DEBUG
        call_with_wrong_convention();
        break;
#else
        return false;
#endif
    case TestFault::kCrtError:
#ifdef _DEBUG
        _RPT0(_CRT_ERROR, "crash test: a Debug CRT error report");
        break;
#else
        return false;
#endif
    case TestFault::kInvalidParameter:
        _invalid_parameter_noinfo();
        break;
    case TestFault::kPureCall:
        pure_call();
        break;
    case TestFault::kCancelNow:
        abort();
    case TestFault::kGameOverrun:
    case TestFault::kGameCrtMessage:
        if (!cause_game_crt_fault(fault))
            return false;
        break;
    case TestFault::kFastFail:
        __fastfail(2); // FAST_FAIL_STACK_COOKIE_CHECK_FAILURE
    case TestFault::kHang:
        for (;;)
            Sleep(1000);
    }
    // (a handler returned: nothing ended the process)
    get_log("crash test: %s - still running after it", test_fault_name(fault));
    return true;
}

// (no original C runtime to point at ours)
bool cause_game_crt_fault(TestFault) { return false; }

bool run_guarded(void (*function)())
{
#ifdef _MSC_VER
    __try {
        function();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
#else
    function();
    return true;
#endif
}
