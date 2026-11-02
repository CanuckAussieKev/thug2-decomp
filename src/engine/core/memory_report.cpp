// NOT ORIGINAL - the memory a level takes, logged (engine/core/memory_report.h).

#include "engine/core/memory_report.h"

#include <windows.h>

#include <psapi.h>

#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/memory_sizes.h"
#include "platform/run_log.h"

namespace memory {

namespace {

int kb(int64_t bytes) { return static_cast<int>(bytes / 1024); }

// The process's free address space: the total and the largest free block.
void address_space(uint64_t& total, uint64_t& largest)
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
        // (the next region starts where this one ends)
        const std::span<uint8_t> region(static_cast<uint8_t*>(info.BaseAddress), info.RegionSize);
        uint8_t* next = std::to_address(region.end());
        if (next <= at)
            break;
        at = next;
    }
}

} // namespace

void log_memory_use(const char* when)
{
    Hub* manager = Hub::get_pull();
    if (!manager || !manager->primary_zone_)
        return;
    Zone* main = manager->primary_zone_;
    std::string line;
    std::array<char, 160> part{};
    std::snprintf(part.data(), sizeof(part),
                  "main block %d KB, free %d KB (least %d KB); heaps:", kb(sizes::main_block_size()),
                  kb(main->free_byte_count()), kb(main->min_available_));
    line += part.data();
    for (MemSource* heap = manager->first_zone(); heap; heap = manager->following_zone(heap)) {
        if (heap->max_used_size_ < 64 * 1024)
            continue; // (the small ones: themes, skater info...)
        std::snprintf(part.data(), sizeof(part), " %s %d KB (peak %d KB)", heap->label_ ? heap->label_ : "?",
                      kb(heap->used_size_), kb(heap->max_used_size_));
        line += part.data();
    }
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    uint64_t total = 0, largest = 0;
    address_space(total, largest);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        std::snprintf(part.data(), sizeof(part), "; process: private %d MB (peak %d MB)",
                      static_cast<int>(counters.PagefileUsage >> 20),
                      static_cast<int>(counters.PeakPagefileUsage >> 20));
        line += part.data();
    }
    std::snprintf(part.data(), sizeof(part), ", address space free %d MB (largest %d MB)", static_cast<int>(total >> 20),
                  static_cast<int>(largest >> 20));
    line += part.data();
    get_log("memory %s: %s", when, line.c_str());
}

} // namespace memory
