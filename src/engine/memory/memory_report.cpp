// NOT ORIGINAL - the memory a level takes, logged (engine/memory/memory_report.h).

#include "engine/memory/memory_report.h"

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>
#include <memory>
#include <span>

#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "engine/memory/memory_sizes.h"
#include "platform/run_log.h"
#include "engine/core/format.h"

namespace memory {

namespace {
constexpr int kBytesPerKb = 1024;
constexpr int kMegabyteShift = 20;
constexpr int kSmallHeapKb = 64;      // heaps that never used more are left out of the report
constexpr size_t kPartSize = 160;

int kb(int64_t bytes) { return static_cast<int>(bytes / kBytesPerKb); }

// The process's free address space: the total and the largest free block.
struct FreeSpace {
    uint64_t total{};
    uint64_t largest{};
};

FreeSpace address_space()
{
    FreeSpace space;
    uint64_t& total = space.total;
    uint64_t& largest = space.largest;
    MEMORY_BASIC_INFORMATION info;
    for (auto* at = static_cast<uint8_t*>(nullptr); VirtualQuery(at, &info, sizeof(info)) == sizeof(info);) {
        if (info.State == MEM_FREE) {
            total += info.RegionSize;
            largest = std::max<uint64_t>(largest, info.RegionSize);
        }
        // (the next region starts where this one ends)
        const std::span<uint8_t> region(static_cast<uint8_t*>(info.BaseAddress), info.RegionSize);
        uint8_t* next = std::to_address(region.end());
        if (next <= at) {
            break;
        }
        at = next;
    }
    return space;
}

} // namespace

void log_memory_use(const char* when)
{
    Hub* manager = Hub::get_pull();
    if (!manager || !manager->primary_zone()) {
        return;
    }
    Zone* main = manager->primary_zone();
    std::string line;
    std::array<char, kPartSize> part{};
    text::format_to(part, "main block %d KB, free %d KB (least %d KB); heaps:", kb(sizes::main_block_size()),
                    kb(main->free_byte_count()), kb(main->min_available()));
    line += part.data();
    for (MemSource* heap = manager->first_zone(); heap; heap = manager->following_zone(heap)) {
        if (heap->max_used_size() < kSmallHeapKb * kBytesPerKb) {
            continue; // (the small ones: themes, skater info...)
        }
        text::format_to(part, " %s %d KB (peak %d KB)", heap->label() ? heap->label() : "?", kb(heap->used_size()),
                        kb(heap->max_used_size()));
        line += part.data();
    }
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    const auto [total, largest] = address_space();
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE) {
        text::format_to(part, "; process: private %d MB (peak %d MB)", static_cast<int>(counters.PagefileUsage >> kMegabyteShift),
                        static_cast<int>(counters.PeakPagefileUsage >> kMegabyteShift));
        line += part.data();
    }
    text::format_to(part, ", address space free %d MB (largest %d MB)", static_cast<int>(total >> kMegabyteShift),
                    static_cast<int>(largest >> kMegabyteShift));
    line += part.data();
    get_log("memory %s: %s", when, line.c_str());
}

} // namespace memory
