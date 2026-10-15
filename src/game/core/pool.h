#pragma once

#include <cstdint>

namespace Mem {

// A pool of fixed-size items carved out of one allocation, with the free items kept in a linked list (each
// free item's first 4 bytes point to the next free item).
class Pool {
public:
    // Allocates item_size * count bytes from the default heap. `name` (copied, up to 63 characters) is for
    // debug output; null gives "unnamed".
    Pool(int item_size, int count, const char* name);

    // The next free item, or null if the pool is full.
    void* Allocate();
    // Return an item to the pool.
    void Free(void* item);
    // True if `pointer` lies inside the pool's block.
    bool Contains(const void* pointer) const;

    uint8_t* m_start;
    uint8_t* m_end;
    int m_count;
    int m_itemSize;
    int m_used;
    void* m_freeList;
    char m_name[64];
    int m_peakUsed;
};

} // namespace Mem
