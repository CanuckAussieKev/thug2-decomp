// NOT ORIGINAL - the memory a level takes, logged (game/core/memory_report.h).

#include "game/core/memory_report.h"

#include <windows.h>

#include <psapi.h>

#include <cstdio>
#include <string>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/memory_sizes.h"
#include "hook/log.h"

namespace Mem {

namespace {

int Kb(int64_t bytes) { return static_cast<int>(bytes / 1024); }

// The process's free address space: the total and the largest free block.
void AddressSpace(uint64_t& total, uint64_t& largest)
{
    total = 0;
    largest = 0;
    MEMORY_BASIC_INFORMATION info;
    for (auto* at = static_cast<uint8_t*>(nullptr); VirtualQuery(at, &info, sizeof(info)) == sizeof(info);) {
        if (info.State == MEM_FREE) {
            total += info.RegionSize;
            if (info.RegionSize > largest)
                largest = info.RegionSize;
        }
        uint8_t* next = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
        if (next <= at)
            break;
        at = next;
    }
}

} // namespace

void LogMemoryUse(const char* when)
{
    Manager* manager = Manager::Get();
    if (!manager || !manager->m_mainRegion)
        return;
    Region* main = manager->m_mainRegion;
    std::string line;
    char part[160];
    std::snprintf(part, sizeof(part), "main block %d KB, free %d KB (least %d KB); heaps:", Kb(Sizes::MainBlockSize()),
                  Kb(main->MemAvailable()), Kb(main->m_minAvailable));
    line += part;
    for (Allocator* heap = manager->FirstHeap(); heap; heap = manager->NextHeap(heap)) {
        if (heap->m_maxUsedSize < 64 * 1024)
            continue; // (the small ones: themes, skater info...)
        std::snprintf(part, sizeof(part), " %s %d KB (peak %d KB)", heap->m_name ? heap->m_name : "?",
                      Kb(heap->m_usedSize), Kb(heap->m_maxUsedSize));
        line += part;
    }
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    uint64_t total = 0, largest = 0;
    AddressSpace(total, largest);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        std::snprintf(part, sizeof(part), "; process: private %d MB (peak %d MB)",
                      static_cast<int>(counters.PagefileUsage >> 20), static_cast<int>(counters.PeakPagefileUsage >> 20));
        line += part;
    }
    std::snprintf(part, sizeof(part), ", address space free %d MB (largest %d MB)", static_cast<int>(total >> 20),
                  static_cast<int>(largest >> 20));
    line += part;
    Log("memory %s: %s", when, line.c_str());
}

} // namespace Mem
