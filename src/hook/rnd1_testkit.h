#pragma once

// Shared pieces of the engine effects' self-tests (selftests_rnd1_effects.cpp, selftests_rnd1_polys.cpp): stand-in
// hash tables (IterateNext / FlushItem / PutItem over a list of their own - removal during a walk keeps the walk's
// place), the frame time as x87 code, rand() scripted, arena blocks.

#include <windows.h>

#include <cstdint>
#include <map>
#include <vector>

#include "game/lst/hashtable.h"
#include "game/sys/timer.h"
#include "hook/rfx_testkit.h"

namespace Rnd1Tests {

using namespace RfxTests;
using Lst::HashTable;

inline uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }
inline float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

inline std::map<HashTable*, std::vector<HashTable::Item>> g_items;
inline HashTable* NewTable()
{
    auto* table = reinterpret_cast<HashTable*>(Block(sizeof(HashTable)));
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    g_items[table].clear();
    return table;
}
inline void Put(HashTable* table, void* value)
{
    g_items[table].push_back({reinterpret_cast<uint32_t>(value), value, nullptr});
}

inline double g_frameSeconds;
inline uint32_t g_rand;

// The memory manager, the tables, the frame time and rand() stood in.
inline void StandInTables(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00588190, AddressOf(&HashTable::IterateNext)}, 0, [](Regs& r) {
        auto* table = reinterpret_cast<HashTable*>(r.ecx);
        std::vector<HashTable::Item>& items = g_items[table];
        int next = table->m_iterBucket + 1;
        table->m_iterBucket = next;
        if (next < static_cast<int>(items.size())) {
            table->m_iterItem = &items[next];
            return 1u;
        }
        table->m_iterItem = nullptr;
        return 0u;
    });
    s.At({0x005B80D0, AddressOf(&HashTable::FlushItem)}, 4, [](Regs& r) {
        auto* table = reinterpret_cast<HashTable*>(r.ecx);
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Record({0x80D0, Relocate(r.ecx), Relocate(key)});
        std::vector<HashTable::Item>& items = g_items[table];
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].key == key) {
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                if (static_cast<int>(i) <= table->m_iterBucket)
                    --table->m_iterBucket;
                break;
            }
        }
        return 0u;
    });
    s.At({0x00462740, AddressOf(&HashTable::PutItem)}, 8, [](Regs& r) {
        Record({0x2740, Relocate(r.ecx), Relocate(*reinterpret_cast<uint32_t*>(r.args[0])), Relocate(r.args[1])});
        Put(reinterpret_cast<HashTable*>(r.ecx), reinterpret_cast<void*>(r.args[1]));
        return 1u;
    });
    // The frame's time: FLD qword [g_frameSeconds]; RET (on the x87 stack, as both return it).
    for (uint32_t address : {0x005BEAA0u, AddressOf(&Tmr::GetSlomoFrameSeconds)}) {
        std::vector<uint8_t> code = {0xDD, 0x05};
        std::vector<uint8_t> where = Dword(reinterpret_cast<uint32_t>(&g_frameSeconds));
        code.insert(code.end(), where.begin(), where.end());
        code.push_back(0xC3);
        s.Bytes(address, code);
    }
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_rand = g_rand * 214013u + 2531011u;
        return (g_rand >> 16) & 0x7FFF;
    });
}

} // namespace Rnd1Tests
