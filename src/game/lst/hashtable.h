#pragma once

#include <cstdint>

namespace Lst {

// A hash table keyed by 32-bit checksums, storing non-null pointers. It has 2^bits buckets; each bucket is
// an item stored inline, with further items chained from it (chained items come from item pool 0, see
// Mem::PoolManager). An empty slot has a null value. The key's low bits pick the bucket.
//
// The game uses it as a template over the value type; the code is the same for every type, so it is
// written once here over void*.
class HashTable {
public:
    struct Item {
        uint32_t key;
        void* value;
        Item* next;
    };
    using Callback = void(__cdecl*)(void* value, void* data);

    explicit HashTable(int bits);

    // Add an item. Keys aren't checked for duplicates. Always returns true.
    bool PutItem(const uint32_t& key, void* value);
    // Remove the first item with this key (does nothing if there is none).
    void FlushItem(const uint32_t& key);
    // Remove every item.
    void FlushAllItems();
    // Call `callback(value, data)` for every item.
    void HandleCallback(Callback callback, void* data);

    // Iteration: set m_iterBucket = -1 and m_iterItem = null, then each IterateNext() that returns true
    // leaves the next item in m_iterItem.
    bool IterateNext();

    int m_bits;
    Item* m_buckets;
    int m_size;
    int m_iterBucket;
    Item* m_iterItem;
    bool m_flag; // cleared by the constructor; its use isn't known yet
};

} // namespace Lst
