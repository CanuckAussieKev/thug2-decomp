#pragma once

#include <cstdint>
#include <new>
#include <utility>

namespace memory {

// A pool of fixed-size items carved out of one allocation, with the free items kept in a linked list (each
// free item's first 4 bytes point to the next free item).
class SlabBank {
public:
    // Allocates item_size * count bytes from the default heap. `name` (copied, up to 63 characters) is for
    // debug output; null gives "unnamed".
    SlabBank(int item_size, int count, const char* name);

    // The next free item, or null if the pool is full.
    void* carve();
    // Return an item to the pool.
    void dealloc(void* item);
    // True if `pointer` lies inside the pool's block.
    bool get_holds(const void* pointer) const;

    uint8_t* outset_;
    uint8_t* finish_;
    int num_;
    int slot_bytes_;
    int taken_count_;
    void* free_list_;
    char label_[64];
    int peak_used_;
};

} // namespace memory

namespace memory {

// Item pools shared by the engine's container classes, by index. Pool 0 holds hash table items (12 bytes
// each); the other slots are unused in this game.
namespace slot_pool_setup {

// Create pool `index` with room for `count` items. Only index 0 is set up (as "HashItem").
void set_up(int index, int count);
// Take an item from pool `index`; without that pool, allocate it from the default heap instead.
void* create_item(int index);
// Give an item back to wherever create_item got it.
void return_entry(int index, void* item);
// Pool `index` (null if there isn't one).
SlabBank* get_pool(int index);

} // namespace slot_pool_setup

} // namespace memory

namespace memory {

// A class's pair of item pools (the engine's CPoolable): new items come from the current pool, or from the
// default heap when it doesn't exist; freed items go back to whichever pool holds them.
struct PoolPair {
    int current;
    int unknown4;
    SlabBank* pools[2];

    void* carve(unsigned int size);
    void dealloc(void* item);

    // NOT ORIGINAL - the typed interface (callers don't cast or size the memory themselves).
    // Memory for one `Item`, not constructed (as the game's pooled items: plain data, or set up by their own
    // make_new); null when the pool is full and the heap too.
    template <typename Item>
    Item* allocate()
    {
        return static_cast<Item*>(carve(sizeof(Item)));
    }
    // One `Item` constructed in the pool's memory (null, nothing constructed, when there's none).
    template <typename Item, typename... Args>
    Item* create(Args&&... args)
    {
        void* memory = carve(sizeof(Item));
        return memory ? new (memory) Item(std::forward<Args>(args)...) : nullptr;
    }
    // An item made by create(): destroyed and given back (null: nothing).
    template <typename Item>
    void destroy(Item* item)
    {
        if (!item)
            return;
        item->~Item();
        dealloc(item);
    }
};

} // namespace memory
