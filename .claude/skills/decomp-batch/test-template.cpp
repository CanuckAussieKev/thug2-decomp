// Template for an Early self-test comparing original game functions with our replacements, on a stand-in
// memory manager (the pattern of the script tests in src/hook/selftest.cpp). Copy, rename, fill in the
// marked parts; register with SELFTEST(Early, TestThing) at the bottom of selftest.cpp.
//
// - Both runs start from identical state (same globals, same zeroed/dirtied arena), so allocations land at
//   the same offsets and the arenas can be compared word for word after relocating pointers.
// - Every global the code touches is saved first and put back after, even on failure.
// - The original run uses Original<Fn>(address) (and OriginalCode guards so the original's own internal
//   calls also go to originals when hooks are installed - in the game; offline they always do).

alignas(16) uint8_t g_thing_arenas[2][0x20000]; // bigger if a crash at 0x448386 says the heap ran out

void TestThing()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestThing skipped (the memory manager is already set up)");
        return;
    }
    // 1. Every global the code reads or writes: saved, then zeroed for each run.
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {
        {0x007CC979, 1, {}}, // the manager-ready flag (set true below)
        {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}, {0x006F3644, 16, {}},
        {0x006F3654, 16, {}}, {0x006F362C, 16, {}}, // script pools: zeroed = allocate from the manager
        {0x006F3570, 24, {}}, // permanent string cache: off
        // FILL IN: the globals of the code under test (lists, counters, pointers...)
    };
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    // A stand-in symbol table if the code looks symbols up (RestoreSymbolTable puts the real one back).
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        for (Saved& s : saved)
            std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        GameVariable<bool>(0x007CC979) = true;
        // Don't null a global that guards a call into game code (e.g. the script cache 0x6F3558): give it
        // a stand-in object, or the test never reaches that call (see StandInScriptCache()).
        std::memset(table, 0, sizeof(table));

        // 2. The stand-in manager: one script heap over the arena (dirty memory, so zero-fills are tested).
        uint8_t* arena = g_thing_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_thing_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_thing_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + sizeof(g_thing_arenas[0]));
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->m_scriptHeap = heap;
        manager->PushContext(heap);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(g_thing_arenas[0]) ? 0xA0000000u + (word - base) : word;
        };

        // 3. FILL IN: build inputs, then call the original or ours for every case, pushing results (return
        //    values too - not just output buffers) onto `trace`.
        std::vector<OriginalCode*> guards;
        if (original) {
            for (uint32_t address : {0x00400000u /* FILL IN: the functions under test */})
                guards.push_back(new OriginalCode(address));
        }
        // e.g.
        // uint32_t r = original ? Original<uint32_t(__cdecl*)(int)>(0x00401234)(5) : Thing(5);
        // trace.push_back(r);
        for (OriginalCode* guard : guards)
            delete guard;

        // 4. The whole arena, pointers made arena-relative (and stale half-pointers in 16-bit fields).
        // The half-pointer pass works in whole 64 KB segments, so the segment holding the arena's end also
        // matches whatever is placed right after the arena (another static -> false differences). If that
        // bites, allocate the arenas with VirtualAlloc (64 KB aligned, nothing else shares their segments)
        // instead of as statics.
        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(sizeof(g_thing_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_thing_arenas[0]));
        for (uint32_t& word : image) {
            word = relocate(word);
            uint32_t high = word >> 16;
            if (high >= (base >> 16) && high <= ((base + sizeof(g_thing_arenas[0])) >> 16))
                word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    RestoreSymbolTable();
    Check(expected == actual, "thing: FILL IN what's covered");
    if (expected.size() != actual.size())
        Log("  thing trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  thing entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Calling a register-argument entry point (naked stub vs original) - names used inside __asm must not be
// MASM words (out, type, size, ...):
//
// const uint8_t* CallWithEcxEdx(void* code, const uint8_t* token, uint32_t symbolName, void* stackArgument)
// {
//     const uint8_t* result;
//     __asm {
//         mov ecx, token
//         mov edx, symbolName
//         push stackArgument
//         call code
//         add esp, 4          // only if the CALLER pops (check the original's RET)
//         mov result, eax
//     }
//     return result;
// }
// ... CallWithEcxEdx(original ? OriginalFunction(0x00470130) : reinterpret_cast<void*>(&Script::TheStub), ...)
// (call the stub itself: offline the hooks aren't installed, so the game address runs the original)
