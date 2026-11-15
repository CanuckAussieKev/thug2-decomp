// NOT ORIGINAL - the macOS (arm64) port's stand-ins for the three platform files whose Win32 side has no counterpart
// here: the crash reporter (structured exception handling, vectored handlers, Windows Error Reporting's dumps), the
// frame profiler's stack sampler (the x64 unwind data) and the audio-session mute (the Windows audio session API).
//
// What each keeps is its interface: the code that calls them compiles and runs, with the Win32-only part left out. The
// crash reporter does what a person needs on this platform - a signal's backtrace in thug2-decomp.log - and the other
// two are off. docs/design/macos-port.md has the list of what is left to write.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include "platform/app_window.h"
#include "platform/child_process.h"
#include "platform/crash.h"
#include "platform/frame_profile.h"
#include "platform/mute_audio.h"

// --- the crash reporter (platform/crash.cpp) ----------------------------------------------------------------------
namespace {

// The log, as platform/crash.cpp keeps it: thug2-decomp.log beside the exe (platform/run_log.cpp owns it; this
// writes to stderr, which the run log also captures).
void write_report(const char* kind, const char* detail, uintptr_t address, int signal_number)
{
    void* frames[64];
    const int count = backtrace(frames, 64);
    std::fprintf(stderr, "%s: %s at 0x%llx (signal %d)\n", kind, detail != nullptr ? detail : "",
                 static_cast<unsigned long long>(address), signal_number);
    char** names = backtrace_symbols(frames, count);
    if (names != nullptr) {
        for (int i = 0; i < count; ++i) {
            std::fprintf(stderr, "  %s\n", names[i]);
        }
        std::free(names);
    }
    std::fflush(stderr);
}

void handle_signal(int number, siginfo_t* info, void* /*context*/)
{
    const uintptr_t address = info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
    write_report("EXCEPTION", "unhandled", address, number);
    // (the process ends as the fault would have: the handler's own return would loop on the fault)
    signal(number, SIG_DFL);
    raise(number);
}

} // namespace

void install_crash_handling(bool /*console*/)
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = handle_signal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
}

bool is_fatal_exception(uint32_t /*code*/, bool* /*any_module*/) { return false; } // (no exception codes: signals)

bool parse_test_fault(const std::string& name, TestFault* out)
{
    struct FaultName {
        const char* name;
        TestFault fault;
    };
    static constexpr FaultName kNames[] = {
        {"access", TestFault::kAccess},       {"rtc", TestFault::kRunTimeCheck},
        {"crterror", TestFault::kCrtError},   {"invalidparam", TestFault::kInvalidParameter},
        {"purecall", TestFault::kPureCall},   {"abort", TestFault::kCancelNow},
        {"overrun", TestFault::kGameOverrun}, {"crtbox", TestFault::kGameCrtMessage},
        {"fastfail", TestFault::kFastFail},   {"hang", TestFault::kHang},
    };
    for (const FaultName& fault : kNames) {
        if (name == fault.name) {
            if (out != nullptr) {
                *out = fault.fault;
            }
            return true;
        }
    }
    return false;
}

const char* test_fault_name(TestFault fault)
{
    switch (fault) {
    case TestFault::kAccess:
        return "access";
    case TestFault::kRunTimeCheck:
        return "rtc";
    case TestFault::kCrtError:
        return "crterror";
    case TestFault::kInvalidParameter:
        return "invalidparam";
    case TestFault::kPureCall:
        return "purecall";
    case TestFault::kCancelNow:
        return "abort";
    case TestFault::kGameOverrun:
        return "overrun";
    case TestFault::kGameCrtMessage:
        return "crtbox";
    case TestFault::kFastFail:
        return "fastfail";
    case TestFault::kHang:
        return "hang";
    }
    return "?";
}

const char* test_fault_names()
{
    return "access rtc crterror invalidparam purecall abort overrun crtbox fastfail hang";
}

bool cause_test_fault(TestFault fault)
{
    switch (fault) {
    case TestFault::kAccess:
        *reinterpret_cast<volatile int*>(0) = 1;
        return true;
    case TestFault::kCancelNow:
        std::abort();
    case TestFault::kHang:
        for (;;) {
            sleep(1);
        }
    case TestFault::kRunTimeCheck:
    case TestFault::kCrtError:
    case TestFault::kInvalidParameter:
    case TestFault::kPureCall:
    case TestFault::kGameOverrun:
    case TestFault::kGameCrtMessage:
    case TestFault::kFastFail:
    default:
        return false; // (MSVC's runtime's own faults: there is no such runtime here)
    }
}

[[noreturn]] void crash_end_process(unsigned exit_code)
{
    std::fprintf(stderr, "  the process ends (exit code %u)\n", exit_code);
    std::fflush(stderr);
    std::exit(static_cast<int>(exit_code));
}

void crash_log_stack_scan(uintptr_t /*esp*/)
{
    // (no stack walk without the Win32 unwind data: the signal's backtrace carries it)
}

bool run_guarded(void (*function)())
{
    if (function != nullptr) {
        function();
    }
    return true; // (no guard: the caller's report is what it was)
}

bool cause_game_crt_fault(TestFault /*fault*/) { return false; }

// --- the frame profiler (platform/frame_profile.cpp) ---------------------------------------------------------------
namespace frame_profile {

int level() { return 0; } // (the Win32 sampler and its log are not ported: always off)

int slot(const char* /*name*/) { return -1; }

void add_ms(int /*slot*/, double /*milliseconds*/) {}

Scope::Scope(const char* /*name*/) {}
Scope::~Scope() = default;

void loop_begin() {}
void loop_end(bool /*drawn*/, bool /*loading*/) {}

} // namespace frame_profile

// --- the audio session's mute (platform/mute_audio.cpp) -----------------------------------------------------------
namespace sound {

bool& audio_muted()
{
    static bool muted = false;
    return muted;
}

bool set_audio_session_muted(bool muted)
{
    audio_muted() = muted;
    return true; // (the game's own output is what a portable build mutes: no session API here)
}

bool audio_session_muted() { return audio_muted(); }

} // namespace sound

// --- the editor's own window's Win32 side (platform/app_window.cpp) ------------------------------------------------
// (an application window's taskbar group, relaunch command, icon and its kept placement: an .exe's Windows concepts.
// The window is SDL3's and keeps its size; the placement is not kept between runs.)
namespace sys::app_window {

namespace {

AppWindow g_window;

} // namespace

void request(const AppWindow& window) { g_window = window; }
bool requested() { return false; }
const AppWindow& settings() { return g_window; }
Placement read_placement() { return Placement{}; }
void prepare_process() {}
void prepare_window(HWND /*window*/) {}
void note_made(SDL_Window* /*window*/) {}
bool note_event(SDL_Window* /*window*/, const SDL_Event& /*event*/, PixelSize* /*size*/) { return false; }
void save_placement() {}
HWND find_running(const std::string& /*title*/) { return nullptr; }

} // namespace sys::app_window

// --- the editor's Import Level's child processes (platform/child_process.cpp) -------------------------------------
// (the pipeline is started as a child process with a job object around it; a portable build has no job objects yet -
// docs/design/macos-port.md. The level editor's Import Level says why it can't start one.)
namespace sys::child_process {

int start(const Command& /*command*/, std::string* why)
{
    if (why != nullptr) {
        *why = "child processes are not ported to macOS yet";
    }
    return 0;
}

std::string take_output(int /*child*/) { return {}; }
bool finished(int /*child*/, int* exit_code)
{
    if (exit_code != nullptr) {
        *exit_code = 0;
    }
    return true;
}
void stop(int /*child*/) {}
void release(int /*child*/) {}

std::wstring command_line(const Command& command)
{
    std::wstring line = quoted(std::wstring(command.program.begin(), command.program.end()));
    for (const std::string& argument : command.arguments) {
        line += L" ";
        line += quoted(std::wstring(argument.begin(), argument.end()));
    }
    return line;
}

std::wstring quoted(const std::wstring& argument)
{
    bool quotes = argument.empty();
    for (const wchar_t letter : argument) {
        if (letter == L' ' || letter == L'\t') {
            quotes = true;
        }
    }
    return quotes ? L"\"" + argument + L"\"" : argument;
}

} // namespace sys::child_process
