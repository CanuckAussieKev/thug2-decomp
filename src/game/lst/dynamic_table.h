#pragma once

// Lst::DynamicTable<T> (0x10 bytes): a growing array of pointers (Mem::Malloc'd; `grow` more slots each time it's
// full). One copy of its code serves the item types (the score's trick log ...): DynamicTable is that code on `void*`
// items. DynamicTableDestroyer frees a table's items (they're plain blocks) and empties it.

#include <cstdint>

namespace Lst {

struct DynamicTable {
    void** m_items;  // +0x00
    int m_capacity;  // +0x04
    int m_count;     // +0x08
    int m_grow;      // +0x0C

    // 0x00561470: `capacity` slots, all null; none used. Returns this.
    DynamicTable* Construct(int capacity, int grow);
    // 0x005614C0: the slots freed (not the items).
    void DestroyBody();
    // 0x005614D0: `item` at the end (when full, `grow` more slots first: the old ones copied, the new ones null).
    void Add(void* item);
};
static_assert(sizeof(DynamicTable) == 0x10, "layout must match the original");

struct DynamicTableDestroyer {
    DynamicTable* m_table;
    // 0x00562920: each item freed (Mem::Free); the table left empty.
    void DeleteTableContents();
};

} // namespace Lst
