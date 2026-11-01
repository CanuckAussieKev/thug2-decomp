#include "engine/core/slab_bank.h"

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <array>

#include "engine/core/mem.h"
#include "platform/run_log.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace memory {

// 0x005BA420
SlabBank::SlabBank(int item_size, int count, const char* name) : num_(count), slot_bytes_(item_size)
{
    auto* block = static_cast<uint8_t*>(alloc(static_cast<unsigned int>(item_size * count)));
    finish_ = block + num_ * slot_bytes_;
    outset_ = block;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    taken_count_ = 0;
    peak_used_ = 0;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    free_list_ = block;

    // Link every item to the next; the last one ends the list.
    uint8_t* item = block;
    for (int i = 0; i < num_; ++i, item += slot_bytes_)
        *raw::as<void*>(item) = i < num_ - 1 ? item + slot_bytes_ : nullptr;

    // The original copies with no length limit, like strcpy.
    const char* source = name ? name : "unnamed";
    text::copy_unbounded(label_.data(), source);
}

// 0x005BA3D0
void* SlabBank::carve()
{
    void* item = free_list_;
    if (!item) {
        // Not original - the log names a pool that ran dry, once per pool (most callers don't check for null).
        static std::array<const SlabBank*, 16> reported;
        static int g_reported_count;
        bool seen = false;
        for (int i = 0; i < g_reported_count; ++i)
            seen = seen || core::at(reported, i) == this;
        // (a pool without a name is a self-test's: the game names every pool, "unnamed" for none)
        if (!seen && g_reported_count < 16 && core::at(label_, 0) != '\0') {
            core::at(reported, g_reported_count++) = this;
            get_log("pool %s full: all %d items (%d bytes each) in use", label_.data(), num_, slot_bytes_);
        }
        return nullptr;
    }
    ++taken_count_;
    if (taken_count_ > peak_used_) {
        peak_used_ = taken_count_;
        // Not original either - a pool reaching 80 % is logged once per pool (which pools a big level comes near:
        // the script pools' sizes are ours, engine/core/memory_sizes.h).
        if (static_cast<int64_t>(taken_count_) * 5 == static_cast<int64_t>(num_) * 4 && num_ >= 50 &&
            core::at(label_, 0) != '\0') {
            static std::array<const SlabBank*, 32> g_nearly_full;
            static int g_nearly_full_count;
            bool seen = false;
            for (int i = 0; i < g_nearly_full_count; ++i)
                seen = seen || core::at(g_nearly_full, i) == this;
            if (!seen && g_nearly_full_count < 32) {
                core::at(g_nearly_full, g_nearly_full_count++) = this;
                get_log("pool %s 80%% full: %d of %d items in use", label_.data(), taken_count_, num_);
            }
        }
    }
    free_list_ = *static_cast<void**>(item);
    return item;
}

// 0x005BA400
void SlabBank::dealloc(void* item)
{
    --taken_count_;
    *static_cast<void**>(item) = free_list_;
    free_list_ = item;
}

// 0x005BA3B0
bool SlabBank::get_holds(const void* pointer) const
{
    auto* p = static_cast<const uint8_t*>(pointer);
    return outset_ <= p && p < finish_;
}

namespace slot_pool_setup {

namespace {
std::array<SlabBank*, 2> g_pools; // (the memory card's path buffer follows)

SlabBank*& pool_at(int index) { return core::at(g_pools, index); }
} // namespace

// 0x005BC3C0
void set_up(int index, int count)
{
    if (index != 0)
        return;
    void* memory = alloc(sizeof(SlabBank));
    pool_at(0) = memory ? memory::construct_in<SlabBank>(memory, 12, count, static_cast<const char*>("HashItem")) : nullptr;
}

// 0x005BC450
void* create_item(int index)
{
    if (SlabBank* pool = pool_at(index))
        return pool->carve();
    // Only pool 0 has a known item size; the original asks for 0 bytes for the others.
    return alloc(index == 0 ? 12 : 0);
}

// 0x005BC3B0
SlabBank* get_pool(int index) { return pool_at(index); }

// 0x005BC4A0
void return_entry(int index, void* item)
{
    if (SlabBank* pool = pool_at(index))
        pool->dealloc(item);
    else
        dealloc(item);
}

} // namespace slot_pool_setup

void* PoolPair::carve(unsigned int size)
{
    if (SlabBank* pool = core::at(pools, current))
        return pool->carve();
    return alloc(size);
}

void PoolPair::dealloc(void* item)
{
    for (const int which : {current, current ^ 1}) {
        SlabBank* pool = core::at(pools, which);
        if (pool && pool->get_holds(item)) {
            pool->dealloc(item);
            return;
        }
    }
    memory::dealloc(item);
}

} // namespace memory

namespace {

struct PoolAdapter {
    memory::SlabBank* make_new(int item_size, int count, const char* name)
    {
        return new (this) memory::SlabBank(item_size, count, name);
    }
};

} // namespace
