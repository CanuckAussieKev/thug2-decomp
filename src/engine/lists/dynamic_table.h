#pragma once

// lists::GrowableMap<T> (0x10 bytes): a growing array of pointers (memory::alloc_block'd; `grow` more slots each time it's
// full). One copy of its code serves the item types (the score's trick log ...): GrowableMap is that code on `void*`
// items. TableReleaser frees a table's items (they're plain blocks) and empties it.

#include <cstdint>

namespace lists {

struct GrowableMap {
    void** items_; // +0x00
    int capacity_; // +0x04
    int num_;      // +0x08
    int grow_;     // +0x0C

    // 0x00561470: `capacity` slots, all null; none used.
    GrowableMap(int capacity, int grow);
    // 0x005614C0: the slots freed (not the items).
    void destroy_body();
    // 0x005614D0: `item` at the end (when full, `grow` more slots first: the old ones copied, the new ones null).
    void append_one(void* item);
};

struct TableReleaser {
    GrowableMap* lookup_table_;
    // 0x00562920: each item freed (memory::Dealloc); the table left empty.
    void empty_table_items();
};

} // namespace lists
