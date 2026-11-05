#pragma once

#include <cstdint>
#include <span>

#include "base/core/gathered_call.h"

namespace lists {

// A hash table keyed by 32-bit checksums, storing non-null pointers. It has 2^bits buckets; each bucket is
// an item stored inline, with further items chained from it (chained items come from item pool 0, see
// memory::slot_pool_setup). An empty slot has a null value. The key's low bits pick the bucket.
//
// The game uses it as a template over the value type; the code is the same for every type, so it is
// written once here over void*.
class KeyMap {
public:
    KeyMap(const KeyMap&) = delete;
    KeyMap& operator=(const KeyMap&) = delete;
    struct Item {
        uint32_t key;
        void* value;
        Item* next;
    };
    using HookFn = void (*)(void* value, void* data);
    // A hook's call: the item's value and the data given to run_hook.
    struct HookCall {
        void* value{};
        void* data{};
    };
    // A typed hook's value and data types.
    template <typename F>
    struct HookTypes;
    template <typename R, typename V, typename D>
    struct HookTypes<R (*)(V*, D*)> {
        using Value = V;
        using Data = D;
    };

    explicit KeyMap(int bits);
    // A table as the exe's static storage starts: all zero (no buckets). The exe's static initialiser then makes it
    // with KeyMap(bits); done in sys::run_startup_initialisers (engine/sys/startup_init.h).
    struct Unconstructed {};
    constexpr KeyMap(Unconstructed /*unused*/) noexcept : bits_(0), buckets_(nullptr), extent_(0), flag_(false) {}
    // Frees the buckets (after removing every item).
    ~KeyMap();

    // The value stored under `key`, or null. (Inlined at most uses in the original.)
    [[nodiscard]] void* fetch_entry(uint32_t key) const;
    // 0x00462050: the same, as a function (the key by reference; the second argument, always 1, is unused).
    [[nodiscard]] void* fetch_entry(const uint32_t& key, bool unused) const;

    // Add an item. Keys aren't checked for duplicates. Always returns true.
    bool store_entry(const uint32_t& key, void* value);
    // Remove the first item with this key (does nothing if there is none).
    void erase_entry(const uint32_t& key);
    // Remove every item.
    void empty_table();
    // Call `callback(value, data)` for every item.
    void run_hook(HookFn callback, void* data) const;
    // The same with a typed callback `Hook` (value and data as its own pointer types; what it returns is dropped).
    template <auto Hook>
    void run_hook(HookTypes<decltype(Hook)>::Data* data)
    {
        run_hook(&core::gathered<HookCall, &typed_hook<Hook>, void, void*, void*>, data);
    }

    // Iteration: set iter_bucket_ = -1 and iter_item_ = null, then each step_iterator() that returns true
    // leaves the next item in iter_item_.
    bool step_iterator();

    // NOT ORIGINAL: the buckets as a run of 1 << bits_ (none for a table without buckets).
    [[nodiscard]] std::span<Item> bucket_run() const
    {
        return {buckets_, buckets_ ? std::size_t{1} << bits_ : std::size_t{0}};
    }

    // The fields other code reads and sets.
    [[nodiscard]] int bits() const { return bits_; }
    [[nodiscard]] Item* buckets() const { return buckets_; }
    void set_buckets(Item* value) { buckets_ = value; }
    [[nodiscard]] int extent() const { return extent_; }
    void set_iter_bucket(int value) { iter_bucket_ = value; }
    [[nodiscard]] Item* iter_item() const { return iter_item_; }
    void set_iter_item(Item* value) { iter_item_ = value; }

private:
    // run_hook<Hook>'s callback: the call's value and data as Hook's own pointer types (what it returns is dropped).
    template <auto Hook>
    static void typed_hook(const HookCall& call)
    {
        using Types = HookTypes<decltype(Hook)>;
        (void)Hook(static_cast<Types::Value*>(call.value), static_cast<Types::Data*>(call.data));
    }
    int bits_;
    Item* buckets_;
    int extent_;
    int iter_bucket_{};
    Item* iter_item_{};
    bool flag_; // cleared by the constructor; its use isn't known yet
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        constexpr size_t kItemSize = 12;
        constexpr size_t kIterItemOffset = 0x10;
        constexpr size_t kFlagOffset = 0x14;
        static_assert(sizeof(KeyMap::Item) == kItemSize && offsetof(KeyMap, iter_item_) == kIterItemOffset &&
                          offsetof(KeyMap, flag_) == kFlagOffset,
                      "layout must match the original");
    }
};

} // namespace lists
