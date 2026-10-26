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
    void* Block() { return this + 1; }
    // While a block is on a heap's free list, the first field links the list instead.
    BlockHeader*& NextFree() { return *reinterpret_cast<BlockHeader**>(this); }
};
constexpr uint32_t kBlockHeaderSize = sizeof(BlockHeader);

// Memory debugging: the heap reports each block it hands out, splits or resizes (before and after the change).
// Compiled out of this build (the empty function at 0x005480A0), kept to show where the original reports.
inline void DebugTrackBlock(BlockHeader* /*block*/) {}
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
    // The destructor's body (only the vtable to set back: the memory isn't the region's).
    void DestroyBody();

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
    // The same, for the node types of other classes (each Lst::Node<T> has its own vtable).
    void UnlinkAs(void** nodeVtable);
};

class Allocator {
public:
    enum Direction : int { kGrowUp = 1, kGrowDown = -1 };

    // A context: an entry in the allocator's stack; heaps keep a free list in each.
    struct Context {
        ListNode node; // data = the context itself
        BlockHeader* freeList; // free blocks, in address order
    };

    Allocator(Region* region, int direction, const char* name);
    void DestroyBody();
    // (virtual slots 0 and 1)
    void PushContext();
    void PopContext();

    // The virtual allocate (slot 3) and free (slot 8).
    void* VirtualAllocate(uint32_t size, bool assertOnFail)
    {
        return reinterpret_cast<void*(__thiscall*)(Allocator*, uint32_t, bool)>(m_vtable[3])(this, size, assertOnFail);
    }
    void VirtualFree(BlockHeader* header)
    {
        reinterpret_cast<void(__thiscall*)(Allocator*, BlockHeader*)>(m_vtable[8])(this, header);
    }

    void** m_vtable;         // 0x0065845C (base), 0x0065848C (Heap)
    // Statistics: block counts and sizes in use and free, and the most there have been.
    int m_usedBlocks;
    int m_maxUsedBlocks;
    int m_freeBlocks;
    int m_maxFreeBlocks;
    int m_usedSize;
    int m_maxUsedSize;
    int m_freeSize;
    int m_maxFreeSize;
    Region* m_region;
    int m_direction;         // Direction
    uint8_t* m_top;          // how far it has claimed in the region
    Context* m_context;      // the current context
    const char* m_name;
    ListNode m_contexts;     // head of the context stack (newest first)
    Context m_baseContext;
};
static_assert(sizeof(Allocator) == 0x64, "layout must match the original");

// The general-purpose allocator: a free list per context (address ordered, neighbours merged), and more space
// claimed from the region when no free block fits. A heap can fall back on up to five others when its region is
// full (each kept `reserve` bytes clear).
class Heap : public Allocator {
public:
    Heap(Region* region, int direction, const char* name);

    // The virtual methods (vtable 0x0065848C, slots 3 to 8).
    void* Allocate(uint32_t size, bool assertOnFail);
    uint32_t LargestFree();
    // Grow a block downwards (a top-down heap: the new space is just below it); the data isn't moved.
    void* ReallocateDown(uint32_t newSize, void* block);
    // Grow a block upwards in place, if the space just after it can be had; null if not.
    void* ReallocateUp(uint32_t newSize, void* block);
    // Give back the end of a block.
    void* ReallocateShrink(uint32_t newSize, void* block);
    void Free(BlockHeader* header);

    bool m_unknown64;
    uint8_t m_pad65[3];
    int m_unknown68;
    int m_useFallbacks;
    Heap* m_fallbacks[5];
    int m_fallbackReserve[5];
};
static_assert(sizeof(Heap) == 0x98, "allocated as 0x98 bytes by the memory manager");

} // namespace Mem
