#include "hook/selftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/menu_backdrop.h"

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

// The early tests run the original code as the reference, and expect THUG2PM.exe's. A stock, already unwrapped
// THUG2.exe (a no-CD build: the same layout and addresses - tools/game/install_manifest.ts) differs from it in these
// few spots: PartyMod's exe patch (its DLL's loading in WinMain, the code cave, the DLL's name), and the no-CD build's
// own edits (SetScreenBlur returning at once, the network service loop, loading from the memory card, the GameSpy
// key client), and two string constants its maker damaged. While the early tests run - before the game has started - THUG2PM.exe's bytes are written there, and
// the exe's own put back after; the game itself runs the exe as it is (each of these functions but the key client's
// is rewritten anyway, and the patch's code is only reached from the original WinMain).
struct ReferenceBytes {
    uint32_t address;
    std::vector<uint8_t> stock; // the no-CD THUG2.exe's
    std::vector<uint8_t> reference; // THUG2PM.exe's
};
const ReferenceBytes kReferenceBytes[] = {
    {0x0048C170, {0xB0, 0x01, 0xC3, 0x90, 0x90}, {0x51, 0x8B, 0x4C, 0x24, 0x08}},
    {0x004E26A1, {0xEB, 0x5F, 0x11}, {0xFC, 0x59, 0x06}},
    {0x00545B4F, {0xF4, 0x01}, {0x98, 0x3A}},
    {0x00545CDE, {0x7C, 0xD8, 0x8E, 0x58}, {0x8D, 0x4C, 0xE8, 0xD4}},
    {0x00545CE8, {0x01}, {0x00}},
    {0x005480A1, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC},
     {0x67, 0x68, 0x98, 0xB1, 0x64, 0x00, 0x8B, 0x35, 0x30, 0x51, 0x64, 0x00, 0xEB, 0x05}},
    {0x005480B4, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, {0xFF, 0xD6, 0xE9, 0xD5, 0x05, 0x0B, 0x00}},
    {0x005A8A01, {0x90, 0x90}, {0x75, 0x9C}},
    {0x005F95BB, {0xEB}, {0x75}},
    {0x0064B191, // the window title's end and the patch's DLL name ("partymod.dll" at 0x0064B198)
     {0x6F, 0x6E, 0x79, 0x20, 0x48, 0x61, 0x77, 0x6B, 0x27, 0x73, 0x20, 0x55, 0x6E, 0x64, 0x65, 0x72, 0x67, 0x72, 0x6F,
      0x75},
     {0x48, 0x55, 0x47, 0x32, 0x00, 0x00, 0x00, 0x70, 0x61, 0x72, 0x74, 0x79, 0x6D, 0x6F, 0x64, 0x2E, 0x64, 0x6C, 0x6C,
      0x00}},
    // The park editor's "MaxPlayers" and "MaxPlayersPossible" (read by GetParkEditorMaxPlayers 0x0058E120 and
    // GetParkEditorMaxPlayersPossible 0x0058E140): 0x90 filler in the stock exe - damage from the no-CD build's
    // maker, not an intended edit. Ours write the names as literals (src/game/ed/park_cfuncs.cpp), so the game is
    // right on either exe; the names are put back here so that the originals they're compared with are the
    // undamaged ones (TestParkMaxPlayersDamagedNames writes the filler back over them itself).
    {0x0064F29C,
     {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
      0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
     {0x4D, 0x61, 0x78, 0x50, 0x6C, 0x61, 0x79, 0x65, 0x72, 0x73, 0x00, 0x00, 0x4D, 0x61, 0x78,
      0x50, 0x6C, 0x61, 0x79, 0x65, 0x72, 0x73, 0x50, 0x6F, 0x73, 0x73, 0x69, 0x62, 0x6C, 0x65}},
};

// Writes THUG2PM.exe's bytes over the stock exe's (only where the stock exe's are there); Restore puts them back.
// Returns how many spots it wrote.
int WriteReferenceBytes(bool restore)
{
    int written = 0;
    for (const ReferenceBytes& r : kReferenceBytes) {
        auto* at = reinterpret_cast<uint8_t*>(r.address);
        const std::vector<uint8_t>& from = restore ? r.reference : r.stock;
        const std::vector<uint8_t>& to = restore ? r.stock : r.reference;
        if (std::memcmp(at, from.data(), from.size()) != 0)
            continue;
        DWORD old;
        VirtualProtect(at, to.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(at, to.data(), to.size());
        VirtualProtect(at, to.size(), old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, to.size());
        ++written;
    }
    return written;
}

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

// The watchdog: a test still running after this long is taken to hang (original or rewritten code looping on bad
// data) - it's named in the log and the process ends, rather than thug2_check (or the game) hanging unnoticed.
constexpr ULONGLONG kHangMilliseconds = 45000;
const char* volatile g_runningTest = nullptr;
volatile ULONGLONG g_runningSince = 0;

// The watchdog runs beside the tests, so it calls nothing a test may stand in for: StandInApi (standins.h)
// patches the Windows function itself, process-wide (kernel32's Sleep for the stream thread's and main loop's
// tests). Its Sleep(1000) used to land in such a stand-in now and then - it then recorded into g_trace from this
// thread while the test's own thread did (a corrupted trace, "vector insert iterator out-of-range"), counted down
// the test's scripted sleeps and spun until the test ended. It waits with ntdll's NtDelayExecution instead, and
// reads the clock with GetTickCount64 (no test stands in for either; keep it that way).
using DelayFn = LONG(NTAPI*)(BOOLEAN alertable, LARGE_INTEGER* interval);
DelayFn g_delay = nullptr;

void WatchdogWait()
{
    LARGE_INTEGER interval;
    interval.QuadPart = -10000000LL; // 1 s, relative (100 ns units)
    if (g_delay)
        g_delay(FALSE, &interval);
    else
        SleepEx(1000, FALSE);
}

DWORD WINAPI Watchdog(void*)
{
    for (;;) {
        WatchdogWait();
        const char* name = g_runningTest;
        if (name && GetTickCount64() - g_runningSince > kHangMilliseconds) {
            Log("SELFTEST HANG in %s: still running after %u s - the process ended", name,
                static_cast<unsigned>(kHangMilliseconds / 1000));
            TerminateProcess(GetCurrentProcess(), 3);
        }
    }
}

void StartWatchdog()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll"))
        g_delay = reinterpret_cast<DelayFn>(GetProcAddress(ntdll, "NtDelayExecution"));
    if (HANDLE thread = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr))
        CloseHandle(thread);
}

// No C++ objects in here: __try can't be used in a function that needs destructors unwound. (A crashed
// test's own destructors don't run; its cleanup function does.)
void RunGuarded(const RegisteredTest& t)
{
    g_runningSince = GetTickCount64();
    g_runningTest = t.name;
    __try {
        t.test();
    } __except (CrashFilter(GetExceptionInformation(), t.name)) {
        ++g_failures;
    }
    g_runningTest = nullptr;
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
    // A game run can skip the early tests: thug2_check already runs every one of them offline, against both exes, before
    // any game run, and they're most of a game's start-up time. [selftests] early=0 (the workspaces' default -
    // tools/game/make_workspace.ps1) skips them in the game; the environment variable THUG2_SELFTESTS_EARLY=1 runs them
    // anyway (tools/game/run_game.ps1 -FullTests: a range's final run). thug2_check has no ini: it always runs them.
    if (stage == TestStage::Early) {
        char early[8] = {};
        bool forced = GetEnvironmentVariableA("THUG2_SELFTESTS_EARLY", early, sizeof(early)) && early[0] == '1';
        if (!forced && !ConfigInt("selftests", "early", 1)) {
            Log("early self-tests: skipped ([selftests] early=0 - thug2_check runs them offline; "
                "THUG2_SELFTESTS_EARLY=1 runs them)");
            return;
        }
    }
    // Only the tests whose names contain one of a comma-separated list of words run: the environment variable
    // THUG2_SELFTESTS (tools/mutate.ts's spec TESTS; tools/game/run_game.ps1 -Tests) or [selftests] filter= in the ini -
    // a mutation run checks its batch's tests in a second, a game run only those of the work it's testing.
    char filter[512] = {};
    if (!GetEnvironmentVariableA("THUG2_SELFTESTS", filter, sizeof(filter)))
        GetPrivateProfileStringA("selftests", "filter", "", filter, sizeof(filter),
                                 GameFolderPath("thug2-decomp.ini").c_str());
    auto wanted = [&filter](const char* name) {
        if (!filter[0])
            return true;
        for (const char* word = filter; *word;) {
            const char* end = std::strchr(word, ',');
            size_t length = end ? static_cast<size_t>(end - word) : std::strlen(word);
            if (length && std::string(name).find(std::string(word, length)) != std::string::npos)
                return true;
            word = end ? end + 1 : word + length;
        }
        return false;
    };
    int failures_before = g_failures, count = 0;
    for (const RegisteredTest& t : Registry())
        count += t.stage == stage && wanted(t.name);
    if (filter[0])
        Log("%s self-tests: only those named after %s (THUG2_SELFTESTS)", StageName(stage), filter);
    Log("%s self-tests: running %d", StageName(stage), count);
    StartWatchdog();
    // [selftests] trace=1 logs each test as it starts - for finding one that hangs.
    static const bool trace = ConfigInt("selftests", "trace", 0) != 0;
    // The ini's menu_backdrop doesn't take part: off while the tests run (the tests that need it set it themselves).
    // With it on, its hooks in the renderer, file loading and cameras ran inside tests' stand-ins.
    int backdrop = Front::MenuBackdrop::Setting();
    Front::MenuBackdrop::Setting() = 0;
    // (a stock no-CD THUG2.exe: THUG2PM.exe's bytes where the two differ, for the early tests - see kReferenceBytes)
    int referenceSpots = stage == TestStage::Early ? WriteReferenceBytes(false) : 0;
    if (referenceSpots)
        Log("%s self-tests: a stock THUG2.exe - THUG2PM.exe's bytes in its %d differing spots while they run",
            StageName(stage), referenceSpots);
    for (const RegisteredTest& t : Registry()) {
        if (t.stage == stage && wanted(t.name)) {
            if (trace)
                Log("  %s", t.name);
            RunGuarded(t);
        }
    }
    if (referenceSpots)
        WriteReferenceBytes(true);
    Front::MenuBackdrop::Setting() = backdrop;
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

// EngineStarted: MainLoop (0x0044EEF0) is entered once, after every engine module has started. It's a plain
// thiscall (the manager in ECX, no arguments), so the hook is a stub that keeps ECX (and EDX), runs the tests
// once, and jumps into the original through its trampoline. (Mlp::Manager::MainLoop calls RunStageOnce itself.)
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
