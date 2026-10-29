// Self-tests for the animation cache (src/game/gfx/anim_cache.cpp): original and ours on a cache made in an arena -
// random entries added, users added and removed, slot times, the oldest given back, flushes, removals, defragmenting
// (with the copies failing), release callbacks, the getters, destruction - the memory manager, the AnimHeap, the slot
// pools, the clock and Dbg::Printf stood in; the calls, results and memory compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/anim_cache.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Gfx::CAnimCache;
using Gfx::CAnimCacheEntry;

uint32_t g_cacheTime;
uint32_t g_heapAvailable;
uint32_t g_poolA, g_poolB; // the stand-in pools
void* g_releaseCallback;

void StandInCacheCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { return g_cacheTime; });
    s.At({0x005BB9E0, AddressOf(&Mem::Manager::InitNamedHeap)}, 16, [](Regs& r) {
        Record({0xB9E0, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2])), r.args[3]});
        return 0u;
    });
    s.At({0x005BAF70, AddressOf(&Mem::Manager::DeleteNamedHeap)}, 8, [](Regs& r) {
        Record({0xAF70, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x005BAF50, AddressOf(&Mem::Manager::GetNamedHeap)}, 8, [](Regs& r) {
        Record({0xAF50, r.args[0], r.args[1]});
        return 0x4EA90000u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x005BB050, AddressOf(&Mem::Manager::Available)}, 0, [](Regs&) {
        Record({0xB050});
        return g_heapAvailable;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return Bump(0x1E4);
    });
    // (a pool holds the slots at offsets whose 16-byte index is even (A) or odd (B))
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Record({0xA3B0, r.ecx, ArenaRelocate(r.args[0])});
        uint32_t odd = ((r.args[0] - reinterpret_cast<uint32_t>(g_arena)) >> 4) & 1;
        return r.ecx == (odd ? g_poolB : g_poolA) ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, r.ecx, ArenaRelocate(r.args[0])});
        return 0u;
    });
    g_releaseCallback = s.Thunk(0, [](Regs& r) {
        Record({0xCA11, ArenaRelocate(r.args[0])});
        return 0u;
    });
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

// Mostly an entry in use (one with a slot when `withSlot`), else any.
CAnimCacheEntry* PickEntry(Lcg& random, CAnimCache* cache, bool withSlot)
{
    std::vector<CAnimCacheEntry*> used;
    for (CAnimCacheEntry* entry : cache->mp_entries) {
        if (entry && entry->m_type && (!withSlot || entry->mp_slots[0] || entry->mp_slots[1] || entry->mp_slots[2]))
            used.push_back(entry);
    }
    if (!used.empty() && random.Below(4))
        return used[random.Below(static_cast<uint32_t>(used.size()))];
    return cache->mp_entries[random.Below(50)];
}

// What an entry of these keys takes (the test's own sum).
uint32_t EntrySize(int type, int rotationKeys, int translationKeys, uint32_t granularity)
{
    int rotation = type == 2 ? 8 : type == 3 ? 16 : type == 4 ? 20 : 0;
    int translation = type == 2 ? 8 : type == 3 || type == 4 ? 16 : 0;
    uint32_t size = static_cast<uint32_t>(rotation * rotationKeys + translation * translationKeys);
    return (size / granularity + 1) * granularity;
}

void TestCas1AnimCache()
{
    GameMemory& memory = *new GameMemory({{0x006F92B0, 4}, {0x006F92B8, 16}, {0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInCacheCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("anim cache", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x7500 + round);
            g_poolA = random.Below(3) ? 0x9001A000u : 0;
            g_poolB = random.Below(3) ? 0x9001B000u : 0;
            auto* pools = reinterpret_cast<uint32_t*>(0x006F92B8);
            pools[0] = random.Below(2);
            pools[2] = g_poolA;
            pools[3] = g_poolB;
            g_cacheTime = 1000;
            g_heapAvailable = 0x100000;
            std::vector<uint32_t> trace;
            static const int kTypes[] = {2, 3, 4, 0, 1, 5};
            int type = random.Pick(kTypes);
            uint32_t granularity = random.Pick({16u, 64u, 100u, 256u});
            At<void(__cdecl*)(uint32_t, uint32_t, int)>(original, 0x00480D10)(0x123456, granularity, type);
            auto* cache = GameVariable<CAnimCache*>(0x006F92B0);
            trace.push_back(ArenaRelocate(At<uint32_t(__cdecl*)()>(original, 0x00480660)()));
            // A second Create does nothing.
            At<void(__cdecl*)(uint32_t, uint32_t, int)>(original, 0x00480D10)(0x1000, 8, 2);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            const uint32_t keys[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0};
            for (int step = 0; step < 50; ++step) {
                g_trace.clear();
                g_cacheTime += random.Below(3);
                uint32_t op = random.Below(16);
                trace.push_back(op);
                uint32_t key = random.Pick(keys);
                switch (op) {
                case 0:
                case 1:
                case 2: { // an entry added (sometimes no room)
                    g_heapAvailable = random.Below(5) ? 0x100000 : random.Below(300);
                    int entryType = random.Below(4) ? type : random.Pick(kTypes);
                    int rotations = static_cast<int>(random.Below(40)), translations = static_cast<int>(random.Below(40));
                    if (random.Below(4) == 0) // (just enough room, or a byte short)
                        g_heapAvailable = EntrySize(entryType, rotations, translations, granularity) - random.Below(2);
                    auto* entry = reinterpret_cast<CAnimCacheEntry*>(
                        At<uint32_t(__thiscall*)(void*, uint32_t, int16_t, int, int, int)>(original, 0x00480980)(
                            cache, key, static_cast<int16_t>(random.Below(70)), rotations, translations, entryType));
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(entry)));
                    if (entry) { // (keys of its own: defragmenting must keep each entry's)
                        for (uint32_t i = 0; i < entry->m_size; ++i)
                            entry->mp_data[i] = static_cast<uint8_t>(random.Next());
                    }
                    break;
                }
                case 3:
                case 4:
                    trace.push_back(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00480E30)(cache, key));
                    break;
                case 5:
                    At<void(__thiscall*)(void*, uint32_t, int)>(original, 0x00480E80)(cache, key,
                                                                                    static_cast<int>(random.Below(4)));
                    break;
                case 6: { // slot times, the getters, a release callback, flags, use stamps
                    CAnimCacheEntry* entry = PickEntry(random, cache, true);
                    if (!entry)
                        break;
                    int index = static_cast<int>(random.Below(3));
                    if (!entry->mp_slots[index] && random.Below(3)) // (a slot to time, mostly)
                        index = static_cast<int>(At<uint32_t(__thiscall*)(void*)>(original, 0x00480BF0)(entry));
                    if (index < 0)
                        index = 0;
                    for (int i = 0; i < 3; ++i) {
                        int16_t time = static_cast<int16_t>(random.Next());
                        trace.push_back(At<uint32_t(__thiscall*)(void*, int, int16_t)>(original, 0x00480590)(
                                            entry, index, time) &
                                        0xFFFF);
                    }
                    trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x004805E0)(entry) & 0xFF);
                    trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x004805F0)(entry) & 0xFF);
                    trace.push_back(ArenaRelocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x00480600)(entry, 7)));
                    trace.push_back(ArenaRelocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x00480610)(entry, 7)));
                    trace.push_back(ArenaRelocate(At<uint32_t(__thiscall*)(void*)>(original, 0x00480620)(entry)));
                    trace.push_back(ArenaRelocate(At<uint32_t(__thiscall*)(void*)>(original, 0x00480630)(entry)));
                    if (random.Below(2))
                        At<void(__thiscall*)(void*, void*, void*)>(original, 0x00480640)(
                            entry, random.Below(3) ? g_releaseCallback : nullptr,
                            reinterpret_cast<void*>(0x00ABC000 + step));
                    entry->m_lastUsed = random.Below(1200);
                    if (random.Below(4) == 0)
                        entry->m_flags ^= CAnimCacheEntry::kKeep;
                    if (random.Below(6) == 0)
                        entry->m_flags = static_cast<uint16_t>(entry->m_flags ^ 0x1FFF); // (users wrapping)
                    break;
                }
                case 7: {
                    uint32_t users = 0x5A5A;
                    CAnimCacheEntry* entry = PickEntry(random, cache, false);
                    uint32_t size = entry && random.Below(2) ? entry->m_allocated : random.Below(600);
                    trace.push_back(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t*)>(original, 0x004806C0)(
                        cache, size, &users));
                    trace.push_back(users);
                    break;
                }
                case 8:
                    for (int i = 0; i < 3; ++i) { // (the oldest kept now and then)
                        CAnimCacheEntry* entry = PickEntry(random, cache, false);
                        if (entry && random.Below(3) == 0)
                            entry->m_flags ^= CAnimCacheEntry::kKeep;
                    }
                    trace.push_back(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x00480ED0)(
                                        cache, random.Below(4), random.Below(600)) &
                                    0xFF);
                    break;
                case 9:
                    At<void(__thiscall*)(void*, bool)>(original, 0x00480DB0)(cache, random.Below(3) != 0);
                    break;
                case 10:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x00480DF0)(cache, key);
                    break;
                case 11: { // defragmented (the copy or the pointer list failing now and then)
                    if (random.Below(4) == 0)
                        g_failAllocations = {random.Below(2) == 0, true};
                    trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x00480F20)(cache));
                    g_failAllocations.clear();
                    break;
                }
                case 12:
                    trace.push_back(At<uint32_t(__thiscall*)(void*, uint32_t, int, int, int)>(original, 0x00480670)(
                        cache, 9, static_cast<int>(random.Below(50)), static_cast<int>(random.Below(50)),
                        random.Pick(kTypes)));
                    trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x00480800)(cache));
                    trace.push_back(At<uint32_t(__cdecl*)()>(original, 0x00480940)());
                    break;
                case 13: { // an entry given back directly
                    CAnimCacheEntry* entry = PickEntry(random, cache, false);
                    if (entry && !(entry->m_flags & CAnimCacheEntry::kBusy))
                        trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x00480AD0)(entry));
                    break;
                }
                case 14:
                    cache->m_evictUnused = random.Below(2) != 0;
                    break;
                default: { // a slot freed directly, a new slot
                    CAnimCacheEntry* entry = PickEntry(random, cache, random.Below(2) != 0);
                    if (!entry)
                        break;
                    if (random.Below(2))
                        At<void(__thiscall*)(void*, int)>(original, 0x00480C90)(entry, static_cast<int>(random.Below(4)));
                    else
                        trace.push_back(At<uint32_t(__thiscall*)(void*)>(original, 0x00480BF0)(entry));
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            g_trace.clear();
            DumpArena(trace); // (before the destruction clears it)
            if (random.Below(2)) {
                At<void(__cdecl*)()>(original, 0x00481340)();
            } else { // the destructor bodies by hand
                At<void(__thiscall*)(void*)>(original, 0x00481200)(cache->mp_entries[3]);
                At<void(__thiscall*)(void*)>(original, 0x004812F0)(cache);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x006F92B0)));
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "anim cache: made, entries added (no room, every type), users, slot times, getters, oldest given back, "
              "flushed, removed, defragmented (copies failing), released, destroyed");
}

} // namespace

SELFTEST(Early, TestCas1AnimCache, RemoveStandIns);
