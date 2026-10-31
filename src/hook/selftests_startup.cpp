// Self-test for the standalone build's start-up (src/game/sys/startup_init.cpp): the original's static initialisers
// (the C++ initialiser table at 0x0067C000) against Sys::RunStartupInitialisers, each from the same memory - the
// exe's whole data section as the image has it (the Early stage: nothing of the game has run, its initialisers
// neither) and a stand-in memory manager over an arena - and every word of the data section and the arena compared
// after (pointers into the arena made arena-relative). In the hooked build our GAME_GLOBALs are the exe's variables
// and each GAME_CODE is the original's address, so what ours writes must be the original's, word for word.
//
// Left out on both sides: the security cookie (entry 1, the C runtime's) and 0x00644A30 (the C++ library's hash_map
// of ended occlusion queries - ours is a std::unordered_map of its own). The original's exit-function registrations
// (_atexit, 0x006239CD) return at once: the exe's C runtime isn't set up, and ours registers none. The two mutexes
// compare as "made" (their handles differ) and are closed after.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/sys/startup_init.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

constexpr uint32_t kInitialisers = 0x0067C000, kInitialisersEnd = 0x0067C13C;
constexpr uint32_t kSecurityCookieInit = 0x00628777, kQueryMapInit = 0x00644A30;
constexpr uint32_t kAtexit = 0x006239CD;
// The exe's .data (its file part and the zero-filled rest).
constexpr uint32_t kDataStart = 0x0067C000, kDataEnd = 0x007DCA80;
constexpr uint32_t kMutexes[] = {0x006EE01C /* the streams' lock */, 0x007052AC /* the loading screen's */};
constexpr uint32_t kMemoryManager = 0x007D1800, kManagerReady = 0x007CC979;

constexpr uint32_t kArenaSize = 0x40000;
ArenaPair<kArenaSize> g_startup_storage;
uint8_t (*const g_startup_arenas)[kArenaSize] = g_startup_storage.Get();

std::vector<uint8_t> g_startupData; // the data section before the test (empty: nothing to put back)
Patch* g_startupAtexit = nullptr;

void RestoreStartupData()
{
    delete g_startupAtexit;
    g_startupAtexit = nullptr;
    if (!g_startupData.empty()) {
        std::memcpy(reinterpret_cast<void*>(kDataStart), g_startupData.data(), g_startupData.size());
        g_startupData.clear();
    }
}

void TestStartupInitialisers()
{
    if (GameVariable<void*>(kMemoryManager)) {
        Log("self-test: TestStartupInitialisers skipped (the game's start-up has run)");
        return;
    }
    // The original's game initialisers, in its table's order.
    std::vector<uint32_t> originals;
    for (uint32_t slot = kInitialisers; slot < kInitialisersEnd; slot += 4) {
        uint32_t init = *reinterpret_cast<const uint32_t*>(slot);
        if (init && init != kSecurityCookieInit && init != kQueryMapInit)
            originals.push_back(init);
    }
    Check(originals.size() == 76, "startup: the original's table has the 76 game initialisers compared");

    g_startupData.resize(kDataEnd - kDataStart);
    std::memcpy(g_startupData.data(), reinterpret_cast<const void*>(kDataStart), g_startupData.size());
    g_startupAtexit = new Patch(kAtexit, {0x33, 0xC0, 0xC3}); // xor eax, eax / ret

    auto run = [&](bool original) {
        std::memcpy(reinterpret_cast<void*>(kDataStart), g_startupData.data(), g_startupData.size());
        // The stand-in memory manager: one heap over the arena (dirty memory, so what isn't written shows).
        uint8_t* arena = g_startup_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        std::memset(arena + 0x1000, 0xCD, kArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Mem::Manager*>(arena);
        auto* region = new (arena + 0x600) Mem::Region(arena + 0x1000, arena + kArenaSize);
        auto* heap = new (arena + 0x620) Mem::Heap(region, 1, "startup");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->m_scriptHeap = heap;
        manager->PushContext(heap);
        GameVariable<void*>(kMemoryManager) = manager;
        GameVariable<bool>(kManagerReady) = true;

        if (original) {
            for (uint32_t init : originals)
                reinterpret_cast<void(__cdecl*)()>(init)();
        } else {
            Sys::RunStartupInitialisers();
        }

        std::vector<uint32_t> words;
        words.reserve((kDataEnd - kDataStart) / 4 + kArenaSize / 4);
        for (uint32_t address : kMutexes) {
            auto& handle = *reinterpret_cast<HANDLE*>(address);
            if (handle)
                CloseHandle(handle);
            handle = handle ? reinterpret_cast<HANDLE>(1) : nullptr;
        }
        for (uint32_t address = kDataStart; address < kDataEnd; address += 4)
            words.push_back(ArenaWord(g_startup_arenas, kArenaSize, *reinterpret_cast<const uint32_t*>(address)));
        for (uint32_t offset = 0; offset < kArenaSize; offset += 4)
            words.push_back(ArenaWordOrHigh(g_startup_arenas, kArenaSize, *reinterpret_cast<const uint32_t*>(arena + offset)));
        return words;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreStartupData();

    Check(expected == actual, "startup: the original's static initialisers and RunStartupInitialisers leave the same data");
    constexpr uint32_t kDataWords = (kDataEnd - kDataStart) / 4;
    int shown = 0, differ = 0;
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] == actual[i])
            continue;
        ++differ;
        if (shown++ >= 16)
            continue;
        if (i < kDataWords)
            Log("  startup: 0x%08X original %08X ours %08X", kDataStart + 4 * static_cast<uint32_t>(i), expected[i],
                actual[i]);
        else
            Log("  startup: arena +0x%05X original %08X ours %08X", 4 * static_cast<uint32_t>(i - kDataWords),
                expected[i], actual[i]);
    }
    if (differ)
        Log("  startup: %d words differ", differ);
}

} // namespace

SELFTEST(Early, TestStartupInitialisers, RestoreStartupData);
