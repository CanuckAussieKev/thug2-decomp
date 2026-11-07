// NOT ORIGINAL - crash handling: every way the game can break is logged and ends the process, never a
// dialog (platform/crash.h).

#include "platform/crash.h"

#include <excpt.h>
#include <windows.h>

#include <werapi.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
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
#ifndef _WIN64
#include "base/core/compiler.h" // (THUG2_NOINLINE: the x86 run-time check fault)
#endif
#include "base/core/address.h"
#include "platform/code_address.h"
#include "platform/crt.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace {

// Decoding a CALL before a return address (see after_call): the bytes looked at, the opcodes, the ModRM byte's fields.
constexpr int kCallLookback = 7;
constexpr int kCallRel32Opcode = 0xE8;
constexpr int kCallIndirectOpcode = 0xFF;
constexpr int kModRmBase = 8;
constexpr int kModRmFieldMask = 7;
// The lengths of an FF /2 call: ModRM alone, + SIB or a byte's displacement, + both, + four bytes', + SIB and four.
constexpr std::array kIndirectCallLengths = {2, 3, 4, 6, 7};

// The stack scans and the dump of the top of the stack (a line of 0x20 bytes: eight words on x86, four on x64 - the
// words past 4 are not named). Both look at as many stack slots on x64 as on x86 (a slot: a pointer's size).
constexpr int kStackScanAddresses = 24;
constexpr uintptr_t kStackScanBytes = 0x800 * sizeof(uintptr_t);
constexpr std::size_t kBacktraceFrames = 24;
constexpr uintptr_t kStackDumpBytes = 0x20 * sizeof(uintptr_t);
constexpr uintptr_t kStackDumpLineBytes = 0x20;
#ifndef _WIN64
constexpr int kDumpWord5 = 5;
constexpr int kDumpWord6 = 6;
constexpr int kDumpWord7 = 7;
#endif

constexpr int kExecuteFault = 8; // ExceptionInformation[0]: the fault was an instruction fetch (DEP)
constexpr std::size_t kReportTextSize = 1024;
constexpr std::size_t kReportWideSize = 512;
constexpr uint32_t kStatusStackBufferOverrun = 0xC0000409U;  // STATUS_STACK_BUFFER_OVERRUN
constexpr uint32_t kStatusHeapCorruption = 0xC0000374U;      // STATUS_HEAP_CORRUPTION
constexpr uint32_t kStatusInvalidCrtParameter = 0xC0000417U; // STATUS_INVALID_CRUNTIME_PARAMETER
constexpr int kHangSleepMs = 1000;

// A context's instruction and stack pointers: EIP / ESP on x86, RIP / RSP on x64.
uintptr_t instruction_pointer(const CONTEXT& context)
{
#ifdef _WIN64
    return context.Rip;
#else
    return context.Eip;
#endif
}
uintptr_t stack_pointer(const CONTEXT& context)
{
#ifdef _WIN64
    return context.Rsp;
#else
    return context.Esp;
#endif
}

// NOT ORIGINAL grouping - crash.cpp's state (were globals). Made on first use, never destroyed (crash_state()).
struct CrashState {
    bool console_ = false;
    // The exception the vectored logger reported last (the unhandled filter doesn't report it twice).
    const EXCEPTION_RECORD* reported_record_ = nullptr;
    uintptr_t reported_address_ = 0;
    void (*crash_hook_)() = nullptr; // (set_crash_hook)
};

CrashState& crash_state()
{
    static core::NoDestroy<CrashState> state;
    return state.get();
}

void run_crash_hook()
{
    if (crash_state().crash_hook_) {
        crash_state().crash_hook_();
    }
}

// The end of every fatal report: the crash hook, the exit code logged, the process ended. A second thread that
// breaks meanwhile waits for the end.
} // namespace

[[noreturn]] void crash_end_process(unsigned exit_code)
{
    static std::atomic<long> ending{0};
    if (ending.exchange(1) != 0) {
        for (;;) {
            Sleep(INFINITE);
        }
    }
    run_crash_hook();
    get_log("  the process ends (exit code %u = 0x%08X)", exit_code, exit_code);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), exit_code);
    for (;;) {
        Sleep(INFINITE);
    }
}

namespace {

// The module an address is in (its file name only) and the address relative to it; false if in none.
bool module_of(uintptr_t address, std::array<char, MAX_PATH>& name, uintptr_t* offset)
{
    HMODULE module = nullptr;
    if ((GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            core::at_address<const char>(address), &module) == FALSE) ||
        !module) {
        return false;
    }
    GetModuleFileNameA(module, name.data(), MAX_PATH);
    const std::string_view full(name.data());
    if (const size_t slash = full.rfind('\\'); slash != std::string_view::npos) {
        const std::string_view file = full.substr(slash + 1);
        memmove(name.data(), file.data(), file.size() + 1);
    }
    *offset = address - core::address(module);
    return true;
}

bool readable(uintptr_t address, size_t size, bool code)
{
    MEMORY_BASIC_INFORMATION info;
    if ((VirtualQuery(core::at_address<void>(address), &info, sizeof(info)) == 0U) || info.State != MEM_COMMIT) {
        return false;
    }
    const DWORD wanted = code ? (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
                              : (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return ((info.Protect & wanted) != 0U) && ((info.Protect & PAGE_GUARD) == 0U) &&
           address + size <= core::address(info.BaseAddress) + info.RegionSize;
}

// Whether `address` follows a CALL instruction (E8 rel32, FF /2 with a 0-, 1- or 4-byte displacement or SIB).
bool after_call(uintptr_t address)
{
    if (!readable(address - kCallLookback, kCallLookback, true)) {
        return false;
    }
    const std::span<const uint8_t, 7> before(core::at_address<const uint8_t>(address - 7), 7); // (the 7 bytes before)
    if (core::at(before, 2) == kCallRel32Opcode) {
        return true;
    }
    return std::ranges::any_of(kIndirectCallLengths, [&](const int length) {
        return core::at(before, kCallLookback - length) == kCallIndirectOpcode &&
               ((core::at(before, kModRmBase - length) >> 3) & kModRmFieldMask) == 2;
    });
}

// The return addresses found on the stack from `esp` up (a scan, not a walk: optimized code - the game's - has no
// frame chain; some may be stale). A slot is a pointer's size: 4 bytes on x86, 8 on x64.
} // namespace

void crash_log_stack_scan(uintptr_t esp)
{
    get_log("  return addresses on the stack (newest first):");
    std::array<char, MAX_PATH> name = {"?"};
    uintptr_t offset = 0;
    int found = 0;
    for (uintptr_t slot = esp; found < kStackScanAddresses && slot < esp + kStackScanBytes; slot += sizeof(uintptr_t)) {
        if (!readable(slot, sizeof(uintptr_t), false)) {
            break;
        }
        const uintptr_t value = *core::at_address<const uintptr_t>(slot);
        if (!after_call(value) || !module_of(value, name, &offset)) {
            continue;
        }
#ifdef _WIN64
        get_log("    [rsp+0x%X] %s+0x%X (%016llX)", static_cast<unsigned>(slot - esp), name.data(),
                static_cast<unsigned>(offset), static_cast<unsigned long long>(value));
#else
        get_log("    [esp+0x%X] %s+0x%X (%08X)", static_cast<unsigned>(slot - esp), name.data(), static_cast<unsigned>(offset),
                static_cast<unsigned>(value));
#endif
        ++found;
    }
}

namespace {

// The calls that led here (a frame walk: fine for our Debug build's code): addresses in our DLL relative to it, the
// game's as they are.
void log_backtrace()
{
    std::array<void*, kBacktraceFrames> frames{};
    const USHORT count = CaptureStackBackTrace(2, 24, frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        std::array<char, MAX_PATH> name = {"?"};
        auto offset = core::address(core::at(frames, i));
        module_of(core::address(core::at(frames, i)), name, &offset);
        get_log("    at %s+0x%X (%p)", name.data(), static_cast<unsigned>(offset), core::at(frames, i));
    }
}

#ifdef _WIN64
// One step of the x64 unwinder (the function's unwind data, as the system's own exception dispatch reads it):
// `context` becomes its caller's - false at the end of the stack. A function without unwind data is a leaf: its return
// address is at RSP.
bool unwind_to_caller(CONTEXT* context)
{
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context->Rip, &image_base, nullptr);
    if (!function) {
        if (!readable(context->Rsp, sizeof(DWORD64), false)) {
            return false;
        }
        context->Rip = *core::at_address<const DWORD64>(context->Rsp);
        context->Rsp += sizeof(DWORD64);
    } else {
        void* handler_data = nullptr;
        DWORD64 establisher_frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context->Rip, function, context, &handler_data,
                         &establisher_frame, nullptr);
    }
    return context->Rip != 0;
}

// The same, guarded: a broken stack the unwinder reads past ends the walk instead of faulting again (MSVC's
// structured exception handling; other compilers unwind unguarded).
bool unwind_to_caller_guarded(CONTEXT* context)
{
#ifdef _MSC_VER
    __try {
        return unwind_to_caller(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return unwind_to_caller(context);
#endif
}

// The calls that led to the fault, unwound from its context: each frame's stack pointer and code address, newest
// first (x64 code has unwind data, so this is a walk - x86's report can only scan).
void log_unwound_calls(const CONTEXT& fault)
{
    get_log("  calls (unwound from the context, newest first):");
    CONTEXT context = fault;
    for (std::size_t frame = 0; frame < kBacktraceFrames; ++frame) {
        std::array<char, MAX_PATH> name = {"?"};
        uintptr_t offset = context.Rip;
        module_of(context.Rip, name, &offset);
        get_log("    [rsp=%016llX] %s+0x%X (%016llX)", context.Rsp, name.data(), static_cast<unsigned>(offset),
                context.Rip);
        if (!unwind_to_caller_guarded(&context)) {
            break;
        }
    }
}
#endif

// An exception's report: where, the registers, the top of the stack, the return addresses on it (on x64 also the calls
// unwound from the context).
void report_exception(const EXCEPTION_POINTERS* info, const char* note)
{
    const CONTEXT* context = info->ContextRecord;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    std::array<char, MAX_PATH> name = {"?"};
    uintptr_t offset = instruction_pointer(*context);
    module_of(instruction_pointer(*context), name, &offset);
#ifdef _WIN64
    get_log("EXCEPTION 0x%08X at %s+0x%X (%016llX) %s", static_cast<unsigned>(code), name.data(),
            static_cast<unsigned>(offset), context->Rip, note);
#else
    get_log("EXCEPTION 0x%08X at %s+0x%X (%08X) %s", static_cast<unsigned>(code), name.data(),
            static_cast<unsigned>(offset), static_cast<unsigned>(context->Eip), note);
#endif
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        const char* access = "reading";
        if (info->ExceptionRecord->ExceptionInformation[0] == kExecuteFault) {
            access = "executing";
        } else if (info->ExceptionRecord->ExceptionInformation[0] != 0) {
            access = "writing";
        }
#ifdef _WIN64
        get_log("  %s address %016llX", access, info->ExceptionRecord->ExceptionInformation[1]);
#else
        get_log("  %s address %08X", access, static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]));
#endif
    }
#ifdef _WIN64
    get_log("  RAX %016llX RBX %016llX RCX %016llX RDX %016llX", context->Rax, context->Rbx, context->Rcx,
            context->Rdx);
    get_log("  RSI %016llX RDI %016llX RBP %016llX RSP %016llX", context->Rsi, context->Rdi, context->Rbp,
            context->Rsp);
    get_log("  R8  %016llX R9  %016llX R10 %016llX R11 %016llX", context->R8, context->R9, context->R10, context->R11);
    get_log("  R12 %016llX R13 %016llX R14 %016llX R15 %016llX", context->R12, context->R13, context->R14,
            context->R15);
    // The top of the stack as it is (the callee's home space, arguments, saved registers, locals).
    for (uintptr_t slot = context->Rsp; slot < context->Rsp + kStackDumpBytes; slot += kStackDumpLineBytes) {
        if (!readable(slot, kStackDumpLineBytes, false)) {
            break;
        }
        const std::span<const uint64_t, 4> w(core::at_address<const uint64_t>(slot), 4);
        get_log("  [rsp+0x%02X] %016llX %016llX %016llX %016llX", static_cast<unsigned>(slot - context->Rsp),
                core::at(w, 0), core::at(w, 1), core::at(w, 2), core::at(w, 3));
    }
    log_unwound_calls(*context);
#else
    get_log("  EAX %08X EBX %08X ECX %08X EDX %08X ESI %08X EDI %08X EBP %08X ESP %08X",
            static_cast<unsigned>(context->Eax), static_cast<unsigned>(context->Ebx),
            static_cast<unsigned>(context->Ecx), static_cast<unsigned>(context->Edx),
            static_cast<unsigned>(context->Esi), static_cast<unsigned>(context->Edi),
            static_cast<unsigned>(context->Ebp), static_cast<unsigned>(context->Esp));
    // The top of the stack as it is (arguments, saved registers, locals).
    for (uintptr_t slot = context->Esp; slot < context->Esp + kStackDumpBytes; slot += kStackDumpLineBytes) {
        if (!readable(slot, kStackDumpLineBytes, false)) {
            break;
        }
        const std::span<const uint32_t, 8> w(core::at_address<const uint32_t>(slot), 8);
        get_log("  [esp+0x%02X] %08X %08X %08X %08X %08X %08X %08X %08X", static_cast<unsigned>(slot - context->Esp),
                core::at(w, 0), core::at(w, 1), core::at(w, 2), core::at(w, 3), core::at(w, 4), core::at(w, kDumpWord5),
                core::at(w, kDumpWord6), core::at(w, kDumpWord7));
    }
#endif
    crash_log_stack_scan(stack_pointer(*context));
}

// Crashes (access violations and other fatal exceptions) are logged before Windows ends the game - first chance: an
// exception the code then handles itself (self-tests' guarded runs) is logged too. At most a few.
LONG CALLBACK crash_logger(EXCEPTION_POINTERS* info)
{
    static int logged = 0;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    bool any_module = false;
    if (!is_fatal_exception(code, &any_module)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Only in the game or our DLL (system DLLs probe memory under their own handlers: IsBadReadPtr...).
    if (!any_module) {
        HMODULE ours = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(code_address(&crash_logger)), &ours);
        HMODULE at = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           core::at_address<const char>(instruction_pointer(*info->ContextRecord)), &at);
        if (at != ours && at != GetModuleHandleA(nullptr)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    if (logged >= 4) {
        // (logged enough - but the hook still runs: earlier, handled exceptions mustn't keep a real crash's autosave
        // from being written)
        run_crash_hook();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ++logged;
    report_exception(info, "(first chance: if the code handles it, nothing more happens)");
    crash_state().reported_record_ = info->ExceptionRecord;
    crash_state().reported_address_ = instruction_pointer(*info->ContextRecord);
    // (then whoever asked: the level editor's last action and autosave - set_crash_hook)
    run_crash_hook();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Nothing handled it: the process ends here, before Windows Error Reporting would show its box.
LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (info->ExceptionRecord != crash_state().reported_record_ ||
        instruction_pointer(*info->ContextRecord) != crash_state().reported_address_) {
        report_exception(info, "(unhandled)");
    }
    get_log("  unhandled: nothing caught exception 0x%08X", static_cast<unsigned>(code));
    crash_end_process(code);
}

void invalid_parameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line,
                       uintptr_t /*unused*/)
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

void abort_signal(int /*unused*/)
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
    std::array<char, kReportTextSize> text{};
    if (message) {
        // Its line ends cut off in the C runtime's own buffer (the hook takes the report: nothing prints it after).
        const std::span<char> report(message, std::strlen(message));
        for (size_t length = report.size();
             (length != 0U) && (core::at(report, length - 1) == '\n' || core::at(report, length - 1) == '\r');
             --length) {
            core::at(report, length - 1) = '\0';
        }
        strncpy_s(text.data(), text.size(), message, _TRUNCATE);
    }
    get_log("DEBUG CRT REPORT (type %d): %s", type, text.data());
    if (type == _CRT_WARN) {
        return TRUE;
    }
    log_backtrace();
    if (crash_state().console_ || type == _CRT_ERROR) {
        crash_end_process(3);
    }
    if (return_value) {
        *return_value = 0; // (no debugger break)
    }
    return TRUE;
}

// Run-time check failures (e.g. "ESP was not properly saved across a function call"): logged, the process ends. A C
// function: the C runtime's callback type (_RTC_error_fnW) is variadic.
extern "C" int rtc_error_handler(int type, const wchar_t* /*unused*/, int /*unused*/, const wchar_t* /*unused*/,
                                 const wchar_t* format, ...)
{
    std::array<wchar_t, kReportWideSize> text{};
    va_list args = nullptr;
    va_start(args, format);
    if (format) { // (cut to the buffer, always terminated)
        _vsnwprintf_s(text.data(), text.size(), _TRUNCATE, format, args);
    }
    va_end(args);
    get_log("RUN-TIME CHECK FAILURE #%d: %ls", type, text.data());
    log_backtrace();
    crash_end_process(3);
}
#endif

#if defined(_DEBUG) && !defined(_WIN64)
void __stdcall pops_its_argument(int /*unused*/) {}
// A __stdcall function called as __cdecl: it pops its argument and so does the caller - ESP ends up 4 off, which
// the /RTCs check after the call reports. (x86 only: x64 has one calling convention.)
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
    {.fault = TestFault::kAccess, .name = "access"},       {.fault = TestFault::kRunTimeCheck, .name = "rtc"},
    {.fault = TestFault::kCrtError, .name = "crterror"},   {.fault = TestFault::kInvalidParameter, .name = "invalidparam"},
    {.fault = TestFault::kPureCall, .name = "purecall"},   {.fault = TestFault::kCancelNow, .name = "abort"},
    {.fault = TestFault::kGameOverrun, .name = "overrun"}, {.fault = TestFault::kGameCrtMessage, .name = "crtbox"},
    {.fault = TestFault::kFastFail, .name = "fastfail"},   {.fault = TestFault::kHang, .name = "hang"},
});

} // namespace

void set_crash_hook(void (*hook)()) { crash_state().crash_hook_ = hook; }

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
    case kStatusStackBufferOverrun: // STATUS_STACK_BUFFER_OVERRUN (raised the old way; a fast fail never gets here)
    case kStatusHeapCorruption: // STATUS_HEAP_CORRUPTION (ntdll's heap)
    case kStatusInvalidCrtParameter: // STATUS_INVALID_CRUNTIME_PARAMETER
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        *any_module = true;
        return true;
    default:
        return false;
    }
}

namespace {
// [debug] soft_bounds=1 (base/core/checked_index.h): an index outside an array is logged once per place, with the
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
        if (place.line() == where.line() && std::string_view(place.file_name()) == where.file_name()) {
            return false;
        }
    }
    if (seen.count < SeenPlaces::kPlaces) {
        core::at(seen.places, seen.count++) = where;
    }
    return true;
}

void report_out_of_range(const std::source_location& where, std::ptrdiff_t index, std::ptrdiff_t extent)
{
    if (!note_place(where)) {
        return;
    }
    get_log("OUT OF RANGE (soft_bounds: carried on): index %td of %td at %s:%u (%s)", index, extent, where.file_name(),
            static_cast<unsigned>(where.line()), where.function_name());
    log_backtrace();
}
} // namespace

void install_crash_handling(bool console)
{
    crash_state().console_ = console;
    if (config_int("debug", "soft_bounds", 0) != 0) {
        core::set_out_of_range_handler(report_out_of_range);
        get_log("soft_bounds: an index outside an array is logged and the game carries on ([debug] soft_bounds=1)");
    }
    // No error boxes: not Windows' "stopped working" (nor "insert a disk", "can't open")...
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // ...nor Windows Error Reporting's UI (its reports queued, not shown).
    using WerSetFlagsFn = HRESULT(WINAPI*)(DWORD);
    if (auto wer_set_flags = crt::library_function<WerSetFlagsFn>({.file = "kernel32.dll"}, "WerSetFlags")) {
        wer_set_flags(WER_FAULT_REPORTING_FLAG_QUEUE);
    }
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
    const auto found = std::ranges::find_if(
        kFaultNames, [&](const FaultName& entry) { return _stricmp(name.c_str(), entry.name) == 0; });
    if (found == kFaultNames.end()) {
        return false;
    }
    *out = found->fault;
    return true;
}

const char* test_fault_name(TestFault fault)
{
    for (const FaultName& entry : kFaultNames) {
        if (entry.fault == fault) {
            return entry.name;
        }
    }
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
    case TestFault::kAccess: {
        // A write to a page that allows no access: an access violation (a real one, as a bad pointer makes).
        void* page = VirtualAlloc(nullptr, sizeof(int), MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
        *static_cast<volatile int*>(page) = 1;
        break;
    }
    case TestFault::kRunTimeCheck:
#if defined(_DEBUG) && !defined(_WIN64)
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
        if (!cause_game_crt_fault(fault)) {
            return false;
        }
        break;
    case TestFault::kFastFail:
        __fastfail(2); // FAST_FAIL_STACK_COOKIE_CHECK_FAILURE
    case TestFault::kHang:
        for (;;) {
            Sleep(kHangSleepMs);
        }
    }
    // (a handler returned: nothing ended the process)
    get_log("crash test: %s - still running after it", test_fault_name(fault));
    return true;
}

// (no original C runtime to point at ours)
bool cause_game_crt_fault(TestFault /*unused*/) { return false; }

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
