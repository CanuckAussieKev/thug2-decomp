#include "engine/lists/key_map.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "engine/core/mem.h"
#include "engine/core/checked_index.h"
#include "engine/core/slab_bank.h"
#include "engine/core/raw_view.h"

namespace lists {

static_assert(sizeof(KeyMap::Item) == 12 && offsetof(KeyMap, iter_item_) == 0x10 && offsetof(KeyMap, flag_) == 0x14,
              "layout must match the original");

// 0x0049FD90
KeyMap::KeyMap(int bits)
    : bits_(bits), buckets_(static_cast<Item*>(memory::alloc(static_cast<unsigned int>(1 << bits) * sizeof(Item)))),
      extent_(0), flag_(false)
{
    std::ranges::fill(bucket_run(), Item{.key = 0, .value = nullptr, .next = nullptr});
}

// 0x005B89D0
KeyMap::~KeyMap()
{
    if (buckets_) {
        empty_table();
        memory::dealloc(buckets_);
        buckets_ = nullptr;
    }
}

void* KeyMap::fetch_entry(uint32_t key) const
{
    for (const Item* item = &core::at(bucket_run(), key & ((1u << bits_) - 1)); item; item = item->next) {
        if (item->key == key && item->value)
            return item->value;
    }
    return nullptr;
}

// 0x00462050
void* KeyMap::fetch_entry(const uint32_t& key, bool /*unused*/) const { return fetch_entry(key); }

// 0x00462740
bool KeyMap::store_entry(const uint32_t& key, void* value)
{
    Item* bucket = &core::at(bucket_run(), key & ((1u << bits_) - 1));
    if (bucket->value) {
        // The bucket's own slot is taken: chain a new item right after it.
        auto* item = static_cast<Item*>(memory::slot_pool_setup::create_item(0));
        *item = {.key = key, .value = value, .next = bucket->next};
        bucket->next = item;
    } else {
        bucket->value = value;
        bucket->key = key;
    }
    ++extent_;
    return true;
}

// 0x005B80D0
void KeyMap::erase_entry(const uint32_t& key)
{
    if (!buckets_)
        return;
    Item* item = &core::at(bucket_run(), key & ((1u << bits_) - 1));
    Item* previous = nullptr;
    while (item->key != key || !item->value) {
        previous = item;
        item = item->next;
        if (!item)
            return;
    }
    if (previous) {
        previous->next = item->next;
        memory::slot_pool_setup::return_entry(0, item);
    } else {
        // The bucket's own slot: empty it, keeping any chain behind it.
        item->key = 0;
        item->value = nullptr;
    }
    --extent_;
}

// 0x0049FD30
void KeyMap::empty_table()
{
    if (!buckets_)
        return;
    for (Item& bucket : bucket_run()) {
        for (Item* item = bucket.next; item;) {
            Item* next = item->next;
            memory::slot_pool_setup::return_entry(0, item);
            item = next;
        }
        bucket = {.key = 0, .value = nullptr, .next = nullptr};
    }
    extent_ = 0;
}

// 0x005B8060
void KeyMap::run_hook(HookFn callback, void* data)
{
    for (const Item& bucket : bucket_run()) {
        // The chained items first, then the bucket's own slot.
        for (const Item* item = bucket.next; item;) {
            const Item* next = item->next;
            if (item->value)
                callback(item->value, data);
            item = next;
        }
        if (bucket.value)
            callback(bucket.value, data);
    }
}

// 0x00588190
bool KeyMap::step_iterator()
{
    if (iter_item_)
        iter_item_ = iter_item_->next;
    else if (iter_bucket_ >= 0)
        return false; // already finished
    if (iter_item_)
        return true;

    // Move on to the next bucket with anything in it.
    Item* bucket = nullptr;
    for (bool again = true; again; again = !bucket->value && !bucket->next) {
        if (++iter_bucket_ >= 1 << bits_)
            return false;
        bucket = &core::at(bucket_run(), iter_bucket_);
        iter_item_ = bucket;
    }
    if (!bucket->value)
        iter_item_ = bucket->next; // empty slot with a chain behind it
    return true;
}

} // namespace lists
