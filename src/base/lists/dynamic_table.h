#pragma once

// lists::GrowableMap<T> (0x10 bytes): a growing array of pointers (memory::alloc_block'd; `grow` more slots each time it's
// full). One copy of its code serves the item types (the score's trick log ...): GrowableMap is that code on `void*`
// items. TableReleaser frees a table's items (they're plain blocks) and empties it.

#include <cstdint>
#include <span>

namespace lists {

struct GrowableMap {
    // The fields other code reads and sets.
    [[nodiscard]] void** items() const { return items_; }
    int& num() { return num_; }
    [[nodiscard]] int num() const { return num_; }

private:
    void** items_; // +0x00
    int capacity_; // +0x04
    int num_{0};   // +0x08
    int grow_;     // +0x0C

public:

    // NOT ORIGINAL: the slots as a run of capacity_.
    [[nodiscard]] std::span<void*> item_run() const { return {items_, static_cast<std::size_t>(capacity_)}; }

    // The slots to start with and how many more each time it is full.
    struct Sizes {
        int capacity{};
        int grow{};
    };
    // 0x00561470: `sizes.capacity` slots, all null; none used.
    explicit GrowableMap(Sizes sizes);
    // 0x005614C0: the slots freed (not the items).
    void destroy_body();
    // 0x005614D0: `item` at the end (when full, `grow` more slots first: the old ones copied, the new ones null).
    void append_one(void* item);
};

struct TableReleaser {
    explicit TableReleaser(GrowableMap* table) : lookup_table_(table) {}

private:
    GrowableMap* lookup_table_;

public:
    // 0x00562920: each item freed (memory::Dealloc); the table left empty.
    void empty_table_items();
};

} // namespace lists
