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

namespace Mem {

// Item pools shared by the engine's container classes, by index. Pool 0 holds hash table items (12 bytes
// each); the other slots are unused in this game.
namespace PoolManager {

// Create pool `index` with room for `count` items. Only index 0 is set up (as "HashItem").
void SetUp(int index, int count);
// Take an item from pool `index`; without that pool, allocate it from the default heap instead.
void* CreateItem(int index);
// Give an item back to wherever CreateItem got it.
void FreeItem(int index, void* item);

} // namespace PoolManager

} // namespace Mem
