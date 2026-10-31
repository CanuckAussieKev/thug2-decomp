#include "game/lst/key_map.h"

#include <cstddef>
#include <cstdint>
#include <new>

#include "game/core/mem.h"
#include "game/core/slab_bank.h"

namespace ListKit {

static_assert(sizeof(KeyMap::Item) == 12 && offsetof(KeyMap, m_iterItem) == 0x10 &&
                  offsetof(KeyMap, m_flag) == 0x14,
              "layout must match the original");

// 0x0049FD90
KeyMap::KeyMap(int bits)
{
    m_bits = bits;
    int count = 1 << bits;
    auto* buckets = static_cast<Item*>(MemSys::Alloc(static_cast<unsigned int>(count) * sizeof(Item)));
    if (buckets) {
        for (int i = 0; i < count; ++i)
            buckets[i] = {0, nullptr, nullptr};
    }
    m_buckets = buckets;
    m_extent = 0;
    m_flag = false;
}

// 0x005B89D0
KeyMap::~KeyMap()
{
    if (m_buckets) {
        EmptyTable();
        MemSys::Dealloc(m_buckets);
        m_buckets = nullptr;
    }
}

void* KeyMap::FetchEntry(uint32_t key) const
{
    for (const Item* item = &m_buckets[key & ((1u << m_bits) - 1)]; item; item = item->next) {
        if (item->key == key && item->value)
            return item->value;
    }
    return nullptr;
}

// 0x00462050
void* KeyMap::FetchEntry(const uint32_t& key, bool /*unused*/) const
{
    return FetchEntry(key);
}

// 0x00462740
bool KeyMap::StoreEntry(const uint32_t& key, void* value)
{
    Item* bucket = &m_buckets[key & ((1u << m_bits) - 1)];
    if (bucket->value) {
        // The bucket's own slot is taken: chain a new item right after it.
        auto* item = static_cast<Item*>(MemSys::SlotPoolSetup::CreateItem(0));
        *item = {key, value, bucket->next};
        bucket->next = item;
    } else {
        bucket->value = value;
        bucket->key = key;
    }
    ++m_extent;
    return true;
}

// 0x005B80D0
void KeyMap::EraseEntry(const uint32_t& key)
{
    if (!m_buckets)
        return;
    Item* item = &m_buckets[key & ((1u << m_bits) - 1)];
    Item* previous = nullptr;
    while (item->key != key || !item->value) {
        previous = item;
        item = item->next;
        if (!item)
            return;
    }
    if (previous) {
        previous->next = item->next;
        MemSys::SlotPoolSetup::FreeItem(0, item);
    } else {
        // The bucket's own slot: empty it, keeping any chain behind it.
        item->key = 0;
        item->value = nullptr;
    }
    --m_extent;
}

// 0x0049FD30
void KeyMap::EmptyTable()
{
    if (!m_buckets)
        return;
    Item* bucket = m_buckets;
    for (unsigned int i = 1u << m_bits; i != 0; --i, ++bucket) {
        for (Item* item = bucket->next; item;) {
            Item* next = item->next;
            MemSys::SlotPoolSetup::FreeItem(0, item);
            item = next;
        }
        *bucket = {0, nullptr, nullptr};
    }
    m_extent = 0;
}

// 0x005B8060
void KeyMap::RunHook(HookFn callback, void* data)
{
    Item* bucket = m_buckets;
    for (unsigned int i = 1u << m_bits; i != 0; --i, ++bucket) {
        // The chained items first, then the bucket's own slot.
        for (Item* item = bucket->next; item;) {
            Item* next = item->next;
            if (item->value)
                callback(item->value, data);
            item = next;
        }
        if (bucket->value)
            callback(bucket->value, data);
    }
}

// 0x00588190
bool KeyMap::StepIterator()
{
    if (m_iterItem)
        m_iterItem = m_iterItem->next;
    else if (m_iterBucket >= 0)
        return false; // already finished
    if (m_iterItem)
        return true;

    // Move on to the next bucket with anything in it.
    Item* bucket;
    do {
        if (++m_iterBucket >= 1 << m_bits)
            return false;
        bucket = &m_buckets[m_iterBucket];
        m_iterItem = bucket;
    } while (!bucket->value && !bucket->next);
    if (!bucket->value)
        m_iterItem = bucket->next; // empty slot with a chain behind it
    return true;
}

} // namespace Lst

namespace {

struct HashTableAdapter {
    ListKit::KeyMap* MakeNew(int bits) { return new (this) ListKit::KeyMap(bits); }
    void Teardown() { reinterpret_cast<ListKit::KeyMap*>(this)->~KeyMap(); }
};

} // namespace
