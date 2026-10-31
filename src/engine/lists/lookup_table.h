#pragma once

// lists::ResolveMap<T> (0x14 bytes): items by key in a singly linked list of {key, item, next} nodes (0xC bytes),
// with a cursor for walking it by index. One copy of its code serves every item type (the goal manager's goals,
// the profile manager's profiles ...): ResolveMap is that code on `void*` items, TypedLookupTable<T> the typed view.

#include <cstdint>

namespace lists {

struct ResolveMap {
    struct Elem {
        uint32_t key;
        void* item;
        Elem* next;
    };
    int num_;       // +0x00
    Elem* first_;     // +0x04
    Elem* last_;      // +0x08
    Elem* cursor_;    // +0x0C: the node at cursor_index_ (null: start again from the first)
    int cursor_index_; // +0x10

    // 0x00557F10: the item at `*index`, walking on from the cursor (or from the first node when the cursor is past
    // it or unset); its key into `key` if given. (No bounds check: an index past the end walks off the list, as the
    // original.)
    void* fetch_entry_at(const int* index, uint32_t* key);
    // 0x00561430: the item with key `*key` (the cursor left on it), or null (the cursor cleared).
    void* fetch_entry(const uint32_t* key);
    // 0x005628B0: a node for `item` with key `*key` added at the end (the cursor cleared). True.
    bool store_entry(const uint32_t* key, void* item);
    // 0x00562970: the first node with key `*key` taken out and freed (the cursor cleared); nothing if none.
    void erase_entry(const uint32_t* key);
};
static_assert(sizeof(ResolveMap) == 0x14, "layout must match the original");

template <typename T>
struct TypedLookupTable : ResolveMap {
    T* get_by_index(int index, uint32_t* key = nullptr) { return static_cast<T*>(fetch_entry_at(&index, key)); }
    T* get_pull(uint32_t key) { return static_cast<T*>(fetch_entry(&key)); }
    bool place_in(uint32_t key, T* item) { return store_entry(&key, item); }
    void drop_out(uint32_t key) { erase_entry(&key); }
};

// 0x0055B990 (TableItemEraser<Objective>::empty_table_items): the table's items taken out one by one (always the
// first) and destroyed (their vtable's deleting destructor, with flag 1) - as many as the table had.
struct TableItemEraser {
    ResolveMap* lookup_table_;
    void empty_table_items();
    // 0x005633E0 (for plain items - the score's trick counts): each first item taken out and freed (memory::Dealloc).
    void free_table_contents();
};

} // namespace Lst
