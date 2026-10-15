#pragma once

// Self-tests compare decompiled functions against the originals in the exe, logging any mismatch.
//
// A test runs at one stage of startup, chosen by what it needs:
//   Early         - in DllMain, before our replacements are installed. Nothing of the game is set up: no
//                   memory manager, and not even the game's C runtime (originals that call _stricmp, sprintf
//                   etc. crash here).
//   Late          - when the game parses its command line (0x004E1220): C runtime and memory manager ready.
//   EngineStarted - when MainLoop is entered (0x0044EEF0): every engine module exists (file/archive
//                   managers, script system...), just before the first frame.
//
// Register a test next to the code it tests:
//
//   void TestThing() { Check(Original<Fn>(0x00401234)(1) == Thing(1), "Thing"); }
//   SELFTEST(Late, TestThing);
//   SELFTEST(Early, TestOther, RestoreSomething); // optional cleanup, run even if the test crashes
//
// Every test runs inside a crash guard: a crash is logged as "SELFTEST CRASH in <test>" and counted as a
// failure, and the game carries on. [selftests] enabled=0 in thug2-decomp.ini turns them all off.

enum class TestStage { Early, Late, EngineStarted };

void RegisterSelfTest(TestStage stage, const char* name, void (*test)(), void (*cleanup)());

// Record a failure (logged as "SELFTEST FAIL: <what>") unless `ok`.
void Check(bool ok, const char* what);

// Run every test registered for `stage` and log a summary.
void RunSelfTests(TestStage stage);

struct SelfTestRegistrar {
    SelfTestRegistrar(TestStage stage, const char* name, void (*test)(), void (*cleanup)() = nullptr)
    {
        RegisterSelfTest(stage, name, test, cleanup);
    }
};

#define SELFTEST_CONCAT2(a, b) a##b
#define SELFTEST_CONCAT(a, b) SELFTEST_CONCAT2(a, b)
#define SELFTEST(stage, test, ...)                                                                   \
    static SelfTestRegistrar SELFTEST_CONCAT(g_selftest_, __LINE__)(TestStage::stage, #test, test, ##__VA_ARGS__)
