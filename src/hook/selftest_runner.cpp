#include "hook/selftest.h"

#include <windows.h>

#include <vector>

#include "hook/hook.h"
#include "hook/log.h"
#include "hook/shadow.h"

namespace {

struct RegisteredTest {
    TestStage stage;
    const char* name;
    void (*test)();
    void (*cleanup)();
};

// Function-local so it exists before any SELFTEST() registrar in another file runs.
std::vector<RegisteredTest>& Registry()
{
    static std::vector<RegisteredTest> registry;
    return registry;
}

int g_failures = 0;

const char* StageName(TestStage stage)
{
    switch (stage) {
    case TestStage::Early:
        return "early";
    case TestStage::Late:
        return "late";
    case TestStage::EngineStarted:
        return "engine-started";
    }
    return "?";
}

// A crash inside DllMain would otherwise only show as error 0xC0000142 at launch, with nothing in the log.
int CrashFilter(EXCEPTION_POINTERS* info, const char* name)
{
    Log("SELFTEST CRASH in %s: exception 0x%08X at 0x%p", name,
        static_cast<unsigned>(info->ExceptionRecord->ExceptionCode), info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects in here: __try can't be used in a function that needs destructors unwound. (A crashed
// test's own destructors don't run; its cleanup function does.)
void RunGuarded(const RegisteredTest& t)
{
    __try {
        t.test();
    } __except (CrashFilter(GetExceptionInformation(), t.name)) {
        ++g_failures;
    }
    if (t.cleanup)
        t.cleanup();
}

} // namespace

void RegisterSelfTest(TestStage stage, const char* name, void (*test)(), void (*cleanup)())
{
    Registry().push_back({stage, name, test, cleanup});
}

int SelfTestFailures()
{
    return g_failures;
}

void Check(bool ok, const char* what)
{
    if (!ok) {
        ++g_failures;
        Log("SELFTEST FAIL: %s", what);
    }
}

void RunSelfTests(TestStage stage)
{
    if (!ConfigInt("selftests", "enabled", 1)) {
        Log("%s self-tests: skipped (thug2-decomp.ini)", StageName(stage));
        return;
    }
    int failures_before = g_failures, count = 0;
    for (const RegisteredTest& t : Registry())
        count += t.stage == stage;
    Log("%s self-tests: running %d", StageName(stage), count);
    // [selftests] trace=1 logs each test as it starts - for finding one that hangs.
    static const bool trace = ConfigInt("selftests", "trace", 0) != 0;
    for (const RegisteredTest& t : Registry()) {
        if (t.stage == stage) {
            if (trace)
                Log("  %s", t.name);
            RunGuarded(t);
        }
    }
    int failures = g_failures - failures_before;
    if (failures)
        Log("%s self-tests: %d FAILED (%d tests)", StageName(stage), failures, count);
    else
        Log("%s self-tests: all passed (%d tests)", StageName(stage), count);
}

// --- stage hooks ------------------------------------------------------------------------------------------

void RunStageOnce(TestStage stage)
{
    StopIfReplacementsFailed();
    static bool done[3] = {};
    bool& stageDone = done[static_cast<int>(stage)];
    if (stageDone)
        return;
    stageDone = true;
    RunSelfTests(stage);
    if (stage == TestStage::EngineStarted)
        Shadow::LogSummary();
}

namespace {

// Late: the game parses its command line (ParseNetworkParams, 0x004E1220) once, early in WinMain.
void __cdecl ParseNetworkParamsHook(void* params)
{
    RunStageOnce(TestStage::Late);
    Original<void(__cdecl*)(void*)>(0x004E1220)(params);
}

// EngineStarted: MainLoop (0x0044EEF0) is entered once, after every engine module has started. It's a
// thiscall with a register we must not disturb, so the hook is a stub that saves ECX/EDX, runs the tests
// once, and jumps into the original through its trampoline.
void* __cdecl EngineStartedBody()
{
    RunStageOnce(TestStage::EngineStarted);
    return OriginalFunction(0x0044EEF0);
}

__declspec(naked) void MainLoopHook()
{
    __asm {
        push ecx
        push edx
        call EngineStartedBody
        pop edx
        pop ecx
        jmp eax
    }
}

} // namespace

// Fallbacks: once ParseNetworkParams / MainLoop have source in src/game, that source calls RunStageOnce and these
// stay uninstalled (unless it's disabled in thug2-decomp.ini).
REPLACE_FALLBACK(0x004E1220, ParseNetworkParamsHook);
REPLACE_FALLBACK_UNCHECKED(0x0044EEF0, MainLoopHook); // passes everything through
