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
// Where the game reaches a stage (Late: ParseNetworkParams 0x004E1220 is entered, early in WinMain;
// EngineStarted: MainLoop 0x0044EEF0 is entered): stop if replacements failed to install, then run that stage's
// tests the first time only (EngineStarted also logs the shadow-check summary). Called by the test runner's
// fallback hooks and, once those functions have source, by the game source itself.
void RunStageOnce(TestStage stage);
// Failures (and crashes) so far, all stages together.
int SelfTestFailures();

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

// Arena-relative words, for tests that run the original and ours each in its own arena and compare what they
// leave: `arenas` is the pair (the original's, then ours, `size` bytes each, one after the other - an
// `uint8_t arenas[2][size]`). A word pointing into either arena (its end included) becomes 0xA0000000 + its
// offset in that arena. The mapping is the same for both runs: a value that isn't a pointer - a checksum, flags,
// the address of a string in the DLL - and only happens to fall inside one of the arenas maps the same way in
// both. (Mapping only the run's own arena made such a value compare differently whenever it fell inside the
// original's arena: the DLL, and the arenas with it, load at a different address each time the game runs, so a
// test passed offline and failed now and then in the game. Seen with CRC("Ped_") = 0x01DD6B72, two words in
// the DLL and the flags word 0x02000001.)
inline unsigned int ArenaWord(const void* arenas, unsigned int size, unsigned int word)
{
    unsigned int start = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(arenas));
    if (word - start > 2 * size)
        return word;
    return 0xA0000000u + (word - start) % size;
}
// The same, and a word whose top half is the top half of an arena address (an uninitialised 16-bit field can
// hold the top half of a stale pointer into the arena) gets 0xA000 + that 64K block's number in its arena there,
// the bottom half kept - for the image of a run's arena. Also the same for both runs, given arenas aligned to 64K
// (ArenaPair): the blocks then never straddle the two arenas.
inline unsigned int ArenaWordOrHigh(const void* arenas, unsigned int size, unsigned int word)
{
    unsigned int start = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(arenas));
    if (word - start <= 2 * size)
        return 0xA0000000u + (word - start) % size;
    unsigned int high = word >> 16;
    unsigned int first = start >> 16;
    if (high - first >= 2 * (size >> 16))
        return word;
    return (word & 0xFFFFu) | ((0xA000u + (high - first) % (size >> 16)) << 16);
}
// Storage for such a pair aligned to 64K (`Size` a multiple of 0x10000), used as `uint8_t arenas[2][Size]`:
//     ArenaPair<0x40000> g_storage;
//     uint8_t (*const g_arenas)[0x40000] = g_storage.Get();
template <unsigned int Size>
struct ArenaPair {
    static_assert(Size % 0x10000 == 0, "whole 64K blocks");
    unsigned char storage[2 * Size + 0x10000];
    unsigned char (*Get())[Size]
    {
        auto address = (reinterpret_cast<unsigned long long>(storage) + 0xFFFF) & ~0xFFFFull;
        return reinterpret_cast<unsigned char (*)[Size]>(static_cast<unsigned int>(address));
    }
};
