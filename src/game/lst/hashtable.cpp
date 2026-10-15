#include "game/lst/hashtable.h"

#include <cstddef>
#include <new>

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "hook/hook.h"

namespace Lst {

static_assert(sizeof(HashTable::Item) == 12 && offsetof(HashTable, m_iterItem) == 0x10 &&
                  offsetof(HashTable, m_flag) == 0x14,
              "layout must match the original");

// 0x0049FD90
HashTable::HashTable(int bits)
{
    m_bits = bits;
    int count = 1 << bits;
    auto* buckets = static_cast<Item*>(Mem::New(static_cast<unsigned int>(count) * sizeof(Item)));
    if (buckets) {
        for (int i = 0; i < count; ++i)
            buckets[i] = {0, nullptr, nullptr};
    }
    m_buckets = buckets;
    m_size = 0;
    m_flag = false;
}

// 0x005B89D0
HashTable::~HashTable()
{
    if (m_buckets) {
        FlushAllItems();
        Mem::Free(m_buckets);
        m_buckets = nullptr;
    }
}

void* HashTable::GetItem(uint32_t key) const
{
    for (const Item* item = &m_buckets[key & ((1u << m_bits) - 1)]; item; item = item->next) {
        if (item->key == key && item->value)
            return item->value;
    }
    return nullptr;
}

// 0x00462740
bool HashTable::PutItem(const uint32_t& key, void* value)
{
    Item* bucket = &m_buckets[key & ((1u << m_bits) - 1)];
    if (bucket->value) {
        // The bucket's own slot is taken: chain a new item right after it.
        auto* item = static_cast<Item*>(Mem::PoolManager::CreateItem(0));
        *item = {key, value, bucket->next};
        bucket->next = item;
    } else {
        bucket->value = value;
        bucket->key = key;
    }
    ++m_size;
    return true;
}

// 0x005B80D0
void HashTable::FlushItem(const uint32_t& key)
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
        Mem::PoolManager::FreeItem(0, item);
    } else {
        // The bucket's own slot: empty it, keeping any chain behind it.
        item->key = 0;
        item->value = nullptr;
    }
    --m_size;
}

// 0x0049FD30
void HashTable::FlushAllItems()
{
    if (!m_buckets)
        return;
    Item* bucket = m_buckets;
    for (unsigned int i = 1u << m_bits; i != 0; --i, ++bucket) {
        for (Item* item = bucket->next; item;) {
            Item* next = item->next;
            Mem::PoolManager::FreeItem(0, item);
            item = next;
        }
        *bucket = {0, nullptr, nullptr};
    }
    m_size = 0;
}

// 0x005B8060
void HashTable::HandleCallback(Callback callback, void* data)
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
bool HashTable::IterateNext()
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
    Lst::HashTable* Construct(int bits) { return new (this) Lst::HashTable(bits); }
    void Destroy() { reinterpret_cast<Lst::HashTable*>(this)->~HashTable(); }
};

} // namespace

REPLACE(0x0049FD90, &HashTableAdapter::Construct);
REPLACE(0x005B89D0, &HashTableAdapter::Destroy);
REPLACE(0x00462740, &Lst::HashTable::PutItem);
REPLACE(0x005B80D0, &Lst::HashTable::FlushItem);
REPLACE(0x0049FD30, &Lst::HashTable::FlushAllItems);
REPLACE(0x005B8060, &Lst::HashTable::HandleCallback);
REPLACE(0x00588190, &Lst::HashTable::IterateNext);
