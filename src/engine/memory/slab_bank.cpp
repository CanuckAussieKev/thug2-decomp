#include "engine/memory/slab_bank.h"

#include "base/core/str.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <array>
#include <span>
#include <utility>

#include <gsl/assert>

#include "engine/memory/mem.h"
#include "engine/lists/key_map.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"

namespace memory {
namespace {
constexpr int kReportedPoolSlots = 16;    // pools named in the log once when full
constexpr int kNearlyFullPoolSlots = 32;
constexpr int kFifths = 5;                   // a pool at four fifths is "80 % full"
constexpr int kFourFifths = 4;
constexpr int kMinNearlyFullItems = 50;      // smaller pools aren't reported as nearly full
constexpr int kHashItemBytes = static_cast<int>(sizeof(lists::KeyMap::Item)); // (12 bytes on x86: memory_sizes.cpp)
} // namespace

// 0x005BA420
SlabBank::SlabBank(int item_size, int count, const char* name) : num_(count), slot_bytes_(item_size)
{
    const auto bytes = static_cast<std::size_t>(num_) * static_cast<std::size_t>(slot_bytes_);
    memory_ = std::span<uint8_t>(static_cast<uint8_t*>(alloc(static_cast<unsigned int>(item_size * count))), bytes);
    // Link every item to the next (from the last, which ends the list); the first heads it.
    gsl::owner<FreeSlot*> next = nullptr;
    for (int i = num_ - 1; i >= 0; --i) {
        next = memory::construct_in<FreeSlot>(&core::at(memory_, i * slot_bytes_), next);
    }
    free_list_ = next;

    // The original copies with no length limit, like strcpy.
    const char* source = name ? name : "unnamed";
    text::copy_unbounded(label_.data(), source);
}

// 0x005BA3D0
void* SlabBank::carve()
{
    if (!free_list_) {
        // Not original - the log names a pool that ran dry, once per pool (most callers don't check for null).
        static std::array<const SlabBank*, kReportedPoolSlots> reported;
        static int g_reported_count;
        bool seen = false;
        for (int i = 0; i < g_reported_count; ++i) {
            seen = seen || core::at(reported, i) == this;
        }
        // (a pool without a name is a self-test's: the game names every pool, "unnamed" for none)
        if (!seen && g_reported_count < kReportedPoolSlots && core::at(label_, 0) != '\0') {
            core::at(reported, g_reported_count++) = this;
            get_log("pool %s full: all %d items (%d bytes each) in use", label_.data(), num_, slot_bytes_);
        }
        return nullptr;
    }
    ++taken_count_;
    if (taken_count_ > peak_used_) {
        peak_used_ = taken_count_;
        // Not original either - a pool reaching 80 % is logged once per pool (which pools a big level comes near:
        // the script pools' sizes are ours, engine/memory/memory_sizes.h).
        if (static_cast<int64_t>(taken_count_) * kFifths == static_cast<int64_t>(num_) * kFourFifths && num_ >= kMinNearlyFullItems &&
            core::at(label_, 0) != '\0') {
            static std::array<const SlabBank*, kNearlyFullPoolSlots> g_nearly_full;
            static int g_nearly_full_count;
            bool seen = false;
            for (int i = 0; i < g_nearly_full_count; ++i) {
                seen = seen || core::at(g_nearly_full, i) == this;
            }
            if (!seen && g_nearly_full_count < kNearlyFullPoolSlots) {
                core::at(g_nearly_full, g_nearly_full_count++) = this;
                get_log("pool %s 80%% full: %d of %d items in use", label_.data(), taken_count_, num_);
            }
        }
    }
    void* const item = free_list_;
    free_list_ = free_list_->next;
    return item;
}

// 0x005BA400
void SlabBank::dealloc(void* item)
{
    --taken_count_;
    gsl::owner<memory::SlabBank::FreeSlot*> slot{
        memory::construct_in<FreeSlot>(item, free_list_)}; // (in the bank's own block)
    free_list_ = slot;
}

// 0x005BA3B0
bool SlabBank::get_holds(const void* pointer) const
{
    // (where it is from the block's start: before it reads negative)
    const std::ptrdiff_t offset = static_cast<const uint8_t*>(pointer) - memory_.data();
    return offset >= 0 && static_cast<std::size_t>(offset) < memory_.size();
}

namespace slot_pool_setup {

namespace {
std::array<SlabBank*, 2>& pools()
{
    static std::array<SlabBank*, 2> pools{}; // (the memory card's path buffer follows)
    return pools;
}

SlabBank*& pool_at(int index) { return core::at(pools(), index); }
} // namespace

// 0x005BC3C0
void set_up(SlabRun slab_run)
{
    const auto index = slab_run.index;
    const auto count = slab_run.count;
    if (index != 0) {
        return;
    }
    void* memory = alloc(sizeof(SlabBank));
    pool_at(0) = memory ? memory::construct_in<SlabBank>(memory, kHashItemBytes, count, static_cast<const char*>("HashItem")) : nullptr;
}

// 0x005BC450
void* create_item(int index)
{
    if (SlabBank* pool = pool_at(index)) {
        return pool->carve();
    }
    // Only pool 0 has a known item size; the original asks for 0 bytes for the others.
    return alloc(index == 0 ? kHashItemBytes : 0);
}

// 0x005BC3B0
SlabBank* get_pool(int index) { return pool_at(index); }

// 0x005BC4A0
void return_entry(int index, void* item)
{
    if (SlabBank* pool = pool_at(index)) {
        pool->dealloc(item);
    } else {
        dealloc(item);
    }
}

} // namespace slot_pool_setup

void* PoolPair::carve(unsigned int size)
{
    if (SlabBank* pool = core::at(pools_, current_)) {
        // Not original - an object bigger than the pool's items (its class grew, the pool's size didn't) ends the
        // game with a report instead of running into the next item.
        Expects(std::cmp_less_equal(size, pool->slot_bytes()));
        return pool->carve();
    }
    return alloc(size);
}

void PoolPair::dealloc(void* item)
{
    for (const int which : {current_, current_ ^ 1}) {
        SlabBank* pool = core::at(pools_, which);
        if (pool && pool->get_holds(item)) {
            pool->dealloc(item);
            return;
        }
    }
    memory::dealloc(item);
}

} // namespace memory
