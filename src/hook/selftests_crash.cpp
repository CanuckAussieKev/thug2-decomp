// Self-tests for the crash handling (hook/crash.cpp - ours, no original to compare with): which exceptions are
// reported, the test faults' names, and that the game's CRT message box really goes through the cached MessageBoxA
// pointer InstallGameCrtHandlers sets (run against the exe's own __crtMessageBoxA). The handlers themselves - that each
// fault is logged and ends the process, without a dialog - are checked by thug2_check in child processes
// (tools/check/main.cpp, "crash handling").

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "hook/crash.h"
#include "hook/game.h"
#include "hook/selftest.h"

namespace {

void TestCrashClassification()
{
    bool any = true;
    Check(IsFatalException(EXCEPTION_ACCESS_VIOLATION, &any) && !any, "crash: an access violation, game / DLL only");
    Check(IsFatalException(EXCEPTION_STACK_OVERFLOW, &any) && !any, "crash: a stack overflow");
    Check(IsFatalException(0xC0000409, &any) && any, "crash: a raised buffer overrun, from any module");
    Check(IsFatalException(0xC0000374, &any) && any, "crash: heap corruption, from any module");
    Check(!IsFatalException(0xE06D7363, &any), "crash: a C++ exception isn't (it may be caught)");
    Check(!IsFatalException(EXCEPTION_BREAKPOINT, &any), "crash: a breakpoint isn't");
    Check(!IsFatalException(0x406D1388, &any), "crash: the thread-naming exception isn't");
}
SELFTEST(Early, TestCrashClassification);

void TestCrashFaultNames()
{
    const TestFault all[] = {TestFault::Access,      TestFault::RunTimeCheck,   TestFault::CrtError,
                             TestFault::InvalidParameter, TestFault::PureCall, TestFault::Abort,
                             TestFault::GameOverrun, TestFault::GameCrtMessage, TestFault::FastFail,
                             TestFault::Hang};
    std::string names;
    for (TestFault fault : all) {
        TestFault parsed = TestFault::Hang;
        if (!ParseTestFault(TestFaultName(fault), &parsed) || parsed != fault)
            Check(false, "crash: a fault's name doesn't parse back");
        names += std::string(names.empty() ? "" : " ") + TestFaultName(fault);
    }
    Check(names == TestFaultNames(), "crash: TestFaultNames lists every fault in order");
    TestFault parsed;
    Check(ParseTestFault("OVERRUN", &parsed) && parsed == TestFault::GameOverrun, "crash: names in any case");
    Check(!ParseTestFault("over", &parsed) && !ParseTestFault("", &parsed), "crash: no partial names");
}
SELFTEST(Early, TestCrashFaultNames);

// The game's __crtMessageBoxA (0x00634205) with a MessageBoxA already cached (0x007DB55C) calls it with the window
// GetActiveWindow gave (none looked up: 0) and its own arguments - what GameCrtMessageBox relies on.
struct BoxCall {
    int calls;
    HWND window;
    const char* text;
    const char* caption;
    UINT type;
};
BoxCall g_box;
int WINAPI RecordingBox(HWND window, const char* text, const char* caption, UINT type)
{
    g_box = {g_box.calls + 1, window, text, caption, type};
    return 7;
}

uintptr_t g_savedBox = 0;
void RestoreGameBox() { GameVariable<uintptr_t>(0x007DB55C) = g_savedBox; }

void TestGameCrtMessageBoxRoute()
{
    // (the other cached user32 functions must be unset - they are until the game's CRT shows a box)
    if (GameVariable<uintptr_t>(0x007DB560) || GameVariable<uintptr_t>(0x007DB568)) {
        Check(false, "crash: the game's CRT has looked up user32 already (a box was shown?)");
        return;
    }
    g_savedBox = GameVariable<uintptr_t>(0x007DB55C);
    GameVariable<decltype(&RecordingBox)>(0x007DB55C) = RecordingBox;
    g_box = {};
    static const char kText[] = "text";
    static const char kCaption[] = "caption";
    int result = GameFunction<int(__cdecl*)(const char*, const char*, unsigned)>(0x00634205)(kText, kCaption, 0x12010);
    RestoreGameBox();
    Check(result == 7 && g_box.calls == 1 && g_box.window == nullptr && g_box.text == kText &&
              g_box.caption == kCaption && g_box.type == 0x12010,
          "crash: the game's __crtMessageBoxA calls the cached MessageBoxA");
}
SELFTEST(Early, TestGameCrtMessageBoxRoute, RestoreGameBox);

} // namespace
