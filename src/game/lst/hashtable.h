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
    // A table as the exe's static storage starts: all zero (no buckets). The exe's static initialiser then makes it
    // with HashTable(bits); the standalone build does it in Sys::RunStartupInitialisers (game/sys/startup_init.h).
    struct Unconstructed {};
    constexpr HashTable(Unconstructed) : m_bits(0), m_buckets(nullptr), m_size(0), m_iterBucket(0), m_iterItem(nullptr), m_flag(false) {}
    // Frees the buckets (after removing every item).
    ~HashTable();

    // The value stored under `key`, or null. (Inlined at most uses in the original.)
    void* GetItem(uint32_t key) const;
    // 0x00462050: the same, as a function (the key by reference; the second argument, always 1, is unused).
    void* GetItem(const uint32_t& key, bool unused) const;

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
