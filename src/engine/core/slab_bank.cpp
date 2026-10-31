#include "engine/core/slab_bank.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>

#include "engine/core/mem.h"
#include "platform/run_log.h"

namespace memory {

static_assert(offsetof(SlabBank, free_list_) == 0x14 && offsetof(SlabBank, label_) == 0x18 &&
                  offsetof(SlabBank, peak_used_) == 0x58,
              "layout must match the original");

// 0x005BA420
SlabBank::SlabBank(int item_size, int count, const char* name)
{
    slot_bytes_ = item_size;
    num_ = count;
    auto* block = static_cast<uint8_t*>(alloc(static_cast<unsigned int>(item_size * count)));
    finish_ = block + num_ * slot_bytes_;
    outset_ = block;
    taken_count_ = 0;
    peak_used_ = 0;
    free_list_ = block;

    // Link every item to the next; the last one ends the list.
    uint8_t* item = block;
    for (int i = 0; i < num_; ++i, item += slot_bytes_)
        *reinterpret_cast<void**>(item) = i < num_ - 1 ? item + slot_bytes_ : nullptr;

    // The original copies with no length limit, like strcpy.
    const char* source = name ? name : "unnamed";
    char* out = label_;
    while ((*out++ = *source++) != '\0') {
    }
}

// 0x005BA3D0
void* SlabBank::carve()
{
    void* item = free_list_;
    if (!item) {
        // Not original - the log names a pool that ran dry, once per pool (most callers don't check for null).
        static const SlabBank* reported[16];
        static int g_reported_count;
        bool seen = false;
        for (int i = 0; i < g_reported_count; ++i)
            seen = seen || reported[i] == this;
        // (a pool without a name is a self-test's: the game names every pool, "unnamed" for none)
        if (!seen && g_reported_count < 16 && label_[0] != '\0') {
            reported[g_reported_count++] = this;
            get_log("pool %s full: all %d items (%d bytes each) in use", label_, num_, slot_bytes_);
        }
        return nullptr;
    }
    ++taken_count_;
    if (taken_count_ > peak_used_) {
        peak_used_ = taken_count_;
        // Not original either - a pool reaching 80 % is logged once per pool (which pools a big level comes near:
        // the script pools' sizes are ours, engine/core/memory_sizes.h).
        if (static_cast<int64_t>(taken_count_) * 5 == static_cast<int64_t>(num_) * 4 && num_ >= 50 &&
            label_[0] != '\0') {
            static const SlabBank* g_nearly_full[32];
            static int g_nearly_full_count;
            bool seen = false;
            for (int i = 0; i < g_nearly_full_count; ++i)
                seen = seen || g_nearly_full[i] == this;
            if (!seen && g_nearly_full_count < 32) {
                g_nearly_full[g_nearly_full_count++] = this;
                get_log("pool %s 80%% full: %d of %d items in use", label_, taken_count_, num_);
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

SlabBank* g_pools[2]; // (the memory card's path buffer follows)

static SlabBank*& pool_at(int index) { return g_pools[index]; }

// 0x005BC3C0
void set_up(int index, int count)
{
    if (index != 0)
        return;
    void* memory = alloc(sizeof(SlabBank));
    pool_at(0) = memory ? new (memory) SlabBank(12, count, "HashItem") : nullptr;
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
SlabBank* get_pool(int index)
{
    return pool_at(index);
}

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
    if (SlabBank* pool = pools[current])
        return pool->carve();
    return alloc(size);
}

void PoolPair::dealloc(void* item)
{
    for (int which : {current, current ^ 1}) {
        SlabBank* pool = pools[which];
        if (pool && pool->get_holds(item)) {
            pool->dealloc(item);
            return;
        }
    }
    memory::dealloc(item);
}

} // namespace Mem

namespace {

struct PoolAdapter {
    memory::SlabBank* make_new(int item_size, int count, const char* name)
    {
        return new (this) memory::SlabBank(item_size, count, name);
    }
};

} // namespace
