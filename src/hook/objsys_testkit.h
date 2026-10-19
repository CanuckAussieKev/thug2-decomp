#pragma once

// Shared pieces of the objsys agent's self-tests (selftests_objsys*.cpp): game memory saved before a test and put
// back by its cleanup - which also runs when the test crashes (a GameMemory's destructor then doesn't), so a crash
// can't leave stand-in values in the globals other tests read.

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

#include "hook/standins.h"

namespace ObjsysTests {

struct SavedRange {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};
inline std::vector<SavedRange> g_saved;

// Save these ranges (replacing any saved before; RestoreGlobals puts them back).
inline void SaveGlobals(std::initializer_list<std::pair<uint32_t, uint32_t>> ranges)
{
    g_saved.clear();
    for (auto [address, size] : ranges) {
        SavedRange s{address, size, {}};
        s.bytes.assign(reinterpret_cast<uint8_t*>(address), reinterpret_cast<uint8_t*>(address) + size);
        g_saved.push_back(std::move(s));
    }
}
inline void RestoreGlobals()
{
    for (const SavedRange& s : g_saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    g_saved.clear();
}
inline void ZeroGlobals()
{
    for (const SavedRange& s : g_saved)
        std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
}

// The cleanup for SELFTEST: stand-ins removed, globals restored.
inline void CleanUp()
{
    TestStandIns::RemoveStandIns();
    RestoreGlobals();
}

} // namespace ObjsysTests
