#pragma once

#include <cstdint>

// The lowest layer of the memory manager: regions of memory, the allocators that carve them up from either
// end, and the header in front of every allocated block.
//
// A Region is a range of memory shared by at most two allocators: one growing up from the start, one growing
// down from the end. Each allocator (memory::Arena and friends derive from Allocator) keeps its own "top" - how
// far it has claimed - and the region refuses claims that would make the two cross.
namespace memory {

class MemSource;

// In front of every block: the allocator that owns it, its size, and a marker.
struct ChunkPrefix {
    MemSource* allocator;
    uint32_t size;
    uint32_t marker; // kBlockInUse, or kBlockFreed once released
    uint32_t unknown0c_;

    static constexpr uint32_t kBlockInUse = 0xCF801FE1;
    static constexpr uint32_t kBlockFreed = 0xDEADDEAD;

    static ChunkPrefix* get_of(void* block) { return static_cast<ChunkPrefix*>(block) - 1; }
    void* gate_off() { return this + 1; }
    // While a block is on a heap's free list, the first field links the list instead.
    ChunkPrefix*& next_free() { return *reinterpret_cast<ChunkPrefix**>(this); }
};
constexpr uint32_t kBlockHeaderSize = sizeof(ChunkPrefix);

// Memory debugging: the heap reports each block it hands out, splits or resizes (before and after the change).
// Compiled out of this build (the empty function at 0x005480A0), kept to show where the original reports.
inline void debug_track_block(ChunkPrefix* /*block*/) {}
static_assert(sizeof(ChunkPrefix) == 0x10, "every block's header is 16 bytes");

// Mark a block as in use (returns it).
void* mark_block_in_use(void* block);
ChunkPrefix* fetch_chunk_prefix(void* block);
uint32_t get_block_size(void* block);
// Give the block back to its allocator (unless it was already freed - then nothing); returns the allocator.
MemSource* free_block(void* block);

class Zone {
public:
    Zone(uint8_t* start, uint8_t* end);
    // Attach / detach the allocator on its side (by its direction).
    void add_allocator(MemSource* allocator);
    void remove_allocator(MemSource* allocator);
    // The space between the two allocators' tops.
    int free_byte_count();
    // Move `allocator`'s top by `size` (in its direction). Returns the start of the claimed space - the old top
    // growing up, the new top growing down - or null if it would cross the other allocator.
    uint8_t* carve(MemSource* allocator, int size, int unused);
    // The destructor's body (only the vtable to set back: the memory isn't the region's).
    void destroy_body();

    void** vtable_; // 0x00658A58
    uint8_t* outset_;
    int min_available_; // the least free_byte_count has been (starts as the whole region)
    uint8_t* finish_;
    MemSource* allocators_[2]; // growing up, growing down

protected:
    Zone() {} // fields left as they are (MemoryArena sets them, unless its size is 0)
};
static_assert(sizeof(Zone) == 0x18, "layout must match the original");

// A region whose memory is itself allocated from the memory manager.
class MemoryArena : public Zone {
public:
    explicit MemoryArena(uint32_t size); // rounded down to 16 bytes
    void destroy_body();
};

// A node of the engine's intrusive lists (lists::Elem): `data` is the item (-1 marks a list's head).
struct ListNode {
    void** vtable;
    void* data;
    int priority;
    ListNode* next;
    ListNode* prev;

    void uncouple();
    // The same, for the node types of other classes (each lists::Elem<T> has its own vtable).
    void unlink_as(void** node_vtable);
};

class MemSource {
public:
    enum Direction : int { kGrowUp = 1, kGrowDown = -1 };

    // A context: an entry in the allocator's stack; heaps keep a free list in each.
    struct CtxInfo {
        ListNode node;           // data = the context itself
        ChunkPrefix* free_list_; // free blocks, in address order
    };

    MemSource(Zone* region, int direction, const char* name);
    void destroy_body();
    // (virtual slots 0 and 1)
    void enter_heap_scope();
    void leave_heap_scope();

    // The virtual allocate (slot 3) and free (slot 8).
    void* virtual_allocate(uint32_t size, bool halt_if_exhausted)
    {
        return reinterpret_cast<void*(__thiscall*)(MemSource*, uint32_t, bool)>(vtable_[3])(this, size,
                                                                                            halt_if_exhausted);
    }
    void virtual_free(ChunkPrefix* header)
    {
        reinterpret_cast<void(__thiscall*)(MemSource*, ChunkPrefix*)>(vtable_[8])(this, header);
    }

    void** vtable_; // 0x0065845C (base), 0x0065848C (Heap)
    // Statistics: block counts and sizes in use and free, and the most there have been.
    int used_blocks_;
    int max_used_blocks_;
    int free_blocks_;
    int max_free_blocks_;
    int used_size_;
    int max_used_size_;
    int free_size_;
    int max_free_size_;
    Zone* region_;
    int direction_;     // Direction
    uint8_t* top_side_; // how far it has claimed in the region
    CtxInfo* context_;  // the current context
    const char* label_;
    ListNode zone_chain_; // head of the context stack (newest first)
    CtxInfo base_context_;
};
static_assert(sizeof(MemSource) == 0x64, "layout must match the original");

// The general-purpose allocator: a free list per context (address ordered, neighbours merged), and more space
// claimed from the region when no free block fits. A heap can fall back on up to five others when its region is
// full (each kept `reserve` bytes clear).
class Arena : public MemSource {
public:
    Arena(Zone* region, int direction, const char* name);

    // The virtual methods (vtable 0x0065848C, slots 3 to 8).
    void* carve(uint32_t size, bool halt_if_exhausted);
    uint32_t largest_free();
    // Grow a block downwards (a top-down heap: the new space is just below it); the data isn't moved.
    void* extend_block_downward(uint32_t new_size, void* block);
    // Grow a block upwards in place, if the space just after it can be had; null if not.
    void* extend_block_upward(uint32_t new_size, void* block);
    // Give back the end of a block.
    void* trim_block_end(uint32_t new_size, void* block);
    void dealloc(ChunkPrefix* header);

    bool unknown64_;
    uint8_t pad65_[3];
    int unknown68_;
    int use_fallbacks_;
    Arena* fallbacks_[5];
    int fallback_reserve_[5];
};
static_assert(sizeof(Arena) == 0x98, "allocated as 0x98 bytes by the memory manager");

} // namespace memory
