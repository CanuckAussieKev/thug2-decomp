#pragma once

// ListKit::GrowableMap<T> (0x10 bytes): a growing array of pointers (MemSys::AllocBlock'd; `grow` more slots each time it's
// full). One copy of its code serves the item types (the score's trick log ...): GrowableMap is that code on `void*`
// items. TableReleaser frees a table's items (they're plain blocks) and empties it.

#include <cstdint>

namespace ListKit {

struct GrowableMap {
    void** m_items;  // +0x00
    int m_capacity;  // +0x04
    int m_num;     // +0x08
    int m_grow;      // +0x0C

    // 0x00561470: `capacity` slots, all null; none used. Returns this.
    GrowableMap* MakeNew(int capacity, int grow);
    // 0x005614C0: the slots freed (not the items).
    void DestroyBody();
    // 0x005614D0: `item` at the end (when full, `grow` more slots first: the old ones copied, the new ones null).
    void AppendOne(void* item);
};
static_assert(sizeof(GrowableMap) == 0x10, "layout must match the original");

struct TableReleaser {
    GrowableMap* m_lookupTable;
    // 0x00562920: each item freed (MemSys::Dealloc); the table left empty.
    void EmptyTableItems();
};

} // namespace Lst
