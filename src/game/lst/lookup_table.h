#pragma once

// Lst::LookupTable<T> (0x14 bytes): items by key in a singly linked list of {key, item, next} nodes (0xC bytes),
// with a cursor for walking it by index. One copy of its code serves every item type (the goal manager's goals,
// the profile manager's profiles ...): LookupTable is that code on `void*` items, TypedLookupTable<T> the typed view.

#include <cstdint>

namespace Lst {

struct LookupTable {
    struct Node {
        uint32_t key;
        void* item;
        Node* next;
    };
    int m_count;       // +0x00
    Node* m_first;     // +0x04
    Node* m_last;      // +0x08
    Node* m_cursor;    // +0x0C: the node at m_cursorIndex (null: start again from the first)
    int m_cursorIndex; // +0x10

    // 0x00557F10: the item at `*index`, walking on from the cursor (or from the first node when the cursor is past
    // it or unset); its key into `key` if given. (No bounds check: an index past the end walks off the list, as the
    // original.)
    void* GetItemByIndex(const int* index, uint32_t* key);
    // 0x00561430: the item with key `*key` (the cursor left on it), or null (the cursor cleared).
    void* GetItem(const uint32_t* key);
    // 0x005628B0: a node for `item` with key `*key` added at the end (the cursor cleared). True.
    bool PutItem(const uint32_t* key, void* item);
    // 0x00562970: the first node with key `*key` taken out and freed (the cursor cleared); nothing if none.
    void FlushItem(const uint32_t* key);
};
static_assert(sizeof(LookupTable) == 0x14, "layout must match the original");

template <typename T>
struct TypedLookupTable : LookupTable {
    T* GetByIndex(int index, uint32_t* key = nullptr) { return static_cast<T*>(GetItemByIndex(&index, key)); }
    T* Get(uint32_t key) { return static_cast<T*>(GetItem(&key)); }
    bool Put(uint32_t key, T* item) { return PutItem(&key, item); }
    void Remove(uint32_t key) { FlushItem(&key); }
};

// 0x0055B990 (LookupTableDestroyer<CGoal>::DeleteTableContents): the table's items taken out one by one (always the
// first) and destroyed (their vtable's deleting destructor, with flag 1) - as many as the table had.
struct LookupTableDestroyer {
    LookupTable* m_table;
    void DeleteTableContents();
    // 0x005633E0 (for plain items - the score's trick counts): each first item taken out and freed (Mem::Free).
    void FreeTableContents();
};

} // namespace Lst
