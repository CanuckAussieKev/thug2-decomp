#pragma once

#include <cstdint>

namespace lists {

// A hash table keyed by 32-bit checksums, storing non-null pointers. It has 2^bits buckets; each bucket is
// an item stored inline, with further items chained from it (chained items come from item pool 0, see
// memory::slot_pool_setup). An empty slot has a null value. The key's low bits pick the bucket.
//
// The game uses it as a template over the value type; the code is the same for every type, so it is
// written once here over void*.
class KeyMap {
public:
    struct Item {
        uint32_t key;
        void* value;
        Item* next;
    };
    using HookFn = void (*)(void* value, void* data);

    explicit KeyMap(int bits);
    // A table as the exe's static storage starts: all zero (no buckets). The exe's static initialiser then makes it
    // with KeyMap(bits); done in sys::run_startup_initialisers (engine/sys/startup_init.h).
    struct Unconstructed {};
    constexpr KeyMap(Unconstructed)
        : bits_(0), buckets_(nullptr), extent_(0), iter_bucket_(0), iter_item_(nullptr), flag_(false)
    {}
    // Frees the buckets (after removing every item).
    ~KeyMap();

    // The value stored under `key`, or null. (Inlined at most uses in the original.)
    void* fetch_entry(uint32_t key) const;
    // 0x00462050: the same, as a function (the key by reference; the second argument, always 1, is unused).
    void* fetch_entry(const uint32_t& key, bool unused) const;

    // Add an item. Keys aren't checked for duplicates. Always returns true.
    bool store_entry(const uint32_t& key, void* value);
    // Remove the first item with this key (does nothing if there is none).
    void erase_entry(const uint32_t& key);
    // Remove every item.
    void empty_table();
    // Call `callback(value, data)` for every item.
    void run_hook(HookFn callback, void* data);

    // Iteration: set iter_bucket_ = -1 and iter_item_ = null, then each step_iterator() that returns true
    // leaves the next item in iter_item_.
    bool step_iterator();

    int bits_;
    Item* buckets_;
    int extent_;
    int iter_bucket_;
    Item* iter_item_;
    bool flag_; // cleared by the constructor; its use isn't known yet
};

} // namespace lists
