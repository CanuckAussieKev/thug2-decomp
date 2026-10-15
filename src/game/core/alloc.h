#pragma once

#include <cstdint>

// The lowest layer of the memory manager: regions of memory, the allocators that carve them up from either
// end, and the header in front of every allocated block.
//
// A Region is a range of memory shared by at most two allocators: one growing up from the start, one growing
// down from the end. Each allocator (Mem::Heap and friends derive from Allocator) keeps its own "top" - how
// far it has claimed - and the region refuses claims that would make the two cross.
namespace Mem {

class Allocator;

// In front of every block: the allocator that owns it, its size, and a marker.
struct BlockHeader {
    Allocator* allocator;
    uint32_t size;
    uint32_t marker; // kBlockInUse, or kBlockFreed once released
    uint32_t unknown0C;

    static constexpr uint32_t kBlockInUse = 0xCF801FE1;
    static constexpr uint32_t kBlockFreed = 0xDEADDEAD;

    static BlockHeader* Of(void* block) { return static_cast<BlockHeader*>(block) - 1; }
};
static_assert(sizeof(BlockHeader) == 0x10, "every block's header is 16 bytes");

// Mark a block as in use (returns it).
void* MarkBlockInUse(void* block);
BlockHeader* GetBlockHeader(void* block);
uint32_t GetBlockSize(void* block);
// Give the block back to its allocator (unless it was already freed - then nothing); returns the allocator.
Allocator* FreeBlock(void* block);

class Region {
public:
    Region(uint8_t* start, uint8_t* end);
    // Attach / detach the allocator on its side (by its direction).
    void AddAllocator(Allocator* allocator);
    void RemoveAllocator(Allocator* allocator);
    // The space between the two allocators' tops.
    int MemAvailable();
    // Move `allocator`'s top by `size` (in its direction). Returns the start of the claimed space - the old top
    // growing up, the new top growing down - or null if it would cross the other allocator.
    uint8_t* Allocate(Allocator* allocator, int size, int unused);

    void** m_vtable;          // 0x00658A58
    uint8_t* m_start;
    int m_minAvailable;       // the least MemAvailable has been (starts as the whole region)
    uint8_t* m_end;
    Allocator* m_allocators[2]; // growing up, growing down

protected:
    Region() {} // fields left as they are (AllocRegion sets them, unless its size is 0)
};
static_assert(sizeof(Region) == 0x18, "layout must match the original");

// A region whose memory is itself allocated from the memory manager.
class AllocRegion : public Region {
public:
    explicit AllocRegion(uint32_t size); // rounded down to 16 bytes
    void DestroyBody();
};

// A node of the engine's intrusive lists (Lst::Node): `data` is the item (-1 marks a list's head).
struct ListNode {
    void** vtable;
    void* data;
    int priority;
    ListNode* next;
    ListNode* prev;

    void Unlink();
};

class Allocator {
public:
    enum Direction : int { kGrowUp = 1, kGrowDown = -1 };

    // A context: an entry in the allocator's stack; heaps keep a free list in each.
    struct Context {
        ListNode node; // data = the context itself
        void* freeList;
    };

    Allocator(Region* region, int direction, const char* name);
    void DestroyBody();
    // (virtual slots 0 and 1)
    void PushContext();
    void PopContext();

    void** m_vtable;         // 0x0065845C (base), 0x0065848C (Heap)
    uint32_t m_unknown04[8];
    Region* m_region;
    int m_direction;         // Direction
    uint8_t* m_top;          // how far it has claimed in the region
    Context* m_context;      // the current context
    const char* m_name;
    ListNode m_contexts;     // head of the context stack (newest first)
    Context m_baseContext;
};
static_assert(sizeof(Allocator) == 0x64, "layout must match the original");

} // namespace Mem
