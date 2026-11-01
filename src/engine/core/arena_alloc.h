#pragma once

#include <cstddef>
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
    // Zeroed memory from the current heap, given back to the memory manager (the main region is made in place).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* memory);
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void*, void*) noexcept {}

    // 0x005BC5F0
    Zone(uint8_t* start, uint8_t* end);
    // 0x005BC4D0 / 0x005BC6D0 (vtable 0x00658A58): nothing - the memory isn't the region's.
    virtual ~Zone() = default;
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    // Attach / detach the allocator on its side (by its direction).
    void add_allocator(MemSource* allocator);
    void remove_allocator(MemSource* allocator);
    // The space between the two allocators' tops.
    int free_byte_count();
    // Move `allocator`'s top by `size` (in its direction). Returns the start of the claimed space - the old top
    // growing up, the new top growing down - or null if it would cross the other allocator.
    uint8_t* carve(MemSource* allocator, int size, int unused);
    uint8_t* outset_{};
    int min_available_{}; // the least free_byte_count has been (starts as the whole region)
    uint8_t* finish_{};
    MemSource* allocators_[2]{}; // growing up, growing down

protected:
    Zone() = default; // fields zero (MemoryArena sets them, unless its size is 0; the original left them as they were)
};

// A region whose memory is itself allocated from the memory manager.
class MemoryArena final : public Zone {
public:
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    // 0x005BC620: rounded down to 16 bytes.
    explicit MemoryArena(uint32_t size);
    // 0x005BC6B0: its memory given back.
    ~MemoryArena() override;
};

// A node of the engine's intrusive lists (lists::Elem / lists::Foremost): `data` is the item (-1 marks a list's
// head), the links run round in a ring through the head. The original gave each node type a one-slot table (its
// deleting destructor: out of the list, freed); here a node is plain data and whoever owns it says how it goes -
// destroy() for a node of its own, the owner's destructor for a node inside an object.
struct ListNode {
    void* data;
    int priority;
    ListNode* next;
    ListNode* prev;

    // 0x005BA0E0 (and the other node types' destructors, 0x005BC990 / 0x005BCD30...): out of its list, linked to
    // itself.
    void uncouple();
    // A list's head: data -1, priority 0, linked to itself.
    void make_head();
    // A node for `item`, linked to itself.
    void make_node(void* item, int item_priority = 0);
    // Whether this is a list's head.
    bool is_head() const { return data == reinterpret_cast<void*>(-1); }

    // A node of its own (zeroed from the current heap, then made for `item`; null without memory) and its deleting
    // destructor (out of its list, freed; null-safe).
    static ListNode* create(void* item, int item_priority = 0);
    static ListNode* create_head();
    static void destroy(ListNode* node);
};
static_assert(sizeof(ListNode) == 0x10, "a list node is four words");

class MemSource {
public:
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): indexes into tables and arrays
    enum Direction : int { kGrowUp = 1, kGrowDown = -1 };

    // A context: an entry in the allocator's stack; heaps keep a free list in each.
    struct CtxInfo {
        ListNode node;           // data = the context itself
        ChunkPrefix* free_list_; // free blocks, in address order
    };

    // Zeroed memory from the current heap, given back to the memory manager (the main heaps are made in place).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* memory);
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void*, void*) noexcept {}

    // 0x005BA260
    MemSource(Zone* region, int direction, const char* name);
    // 0x005BA140 (vtable 0x0065845C; the heap's 0x005BA560 adds nothing): off its region, the lists unlinked.
    virtual ~MemSource();
    MemSource(const MemSource&) = delete;
    MemSource& operator=(const MemSource&) = delete;
    // 0x005BA1E0 / 0x005BA360 (slots 0 and 1 of every allocator's table - the same functions: not virtual here).
    void enter_heap_scope();
    void leave_heap_scope();

    // Slot 3: `size` bytes (null if they can't be had - or a halt, if asked).
    virtual void* carve(uint32_t size, bool halt_if_exhausted) = 0;
    // Slot 4: the largest block that could be allocated (0 in the base).
    virtual uint32_t largest_free() { return 0; }
    // Slots 5-7: a block grown downwards / upwards, or its end given back (null in the base).
    virtual void* extend_block_downward(uint32_t /*new_size*/, void* /*block*/) { return nullptr; }
    virtual void* extend_block_upward(uint32_t /*new_size*/, void* /*block*/) { return nullptr; }
    virtual void* trim_block_end(uint32_t /*new_size*/, void* /*block*/) { return nullptr; }
    // Slot 8: a block given back.
    virtual void dealloc(ChunkPrefix* header) = 0;

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
    uint8_t* top_side_{}; // how far it has claimed in the region
    CtxInfo* context_;  // the current context
    const char* label_;
    ListNode zone_chain_{}; // head of the context stack (newest first)
    CtxInfo base_context_{};
};

// The general-purpose allocator: a free list per context (address ordered, neighbours merged), and more space
// claimed from the region when no free block fits. A heap can fall back on up to five others when its region is
// full (each kept `reserve` bytes clear).
class Arena final : public MemSource {
public:
    // 0x005BA520
    Arena(Zone* region, int direction, const char* name);

    // The heap's (vtable 0x0065848C, slots 3 to 8).
    void* carve(uint32_t size, bool halt_if_exhausted) override;
    uint32_t largest_free() override;
    // Grow a block downwards (a top-down heap: the new space is just below it); the data isn't moved.
    void* extend_block_downward(uint32_t new_size, void* block) override;
    // Grow a block upwards in place, if the space just after it can be had; null if not.
    void* extend_block_upward(uint32_t new_size, void* block) override;
    // Give back the end of a block.
    void* trim_block_end(uint32_t new_size, void* block) override;
    void dealloc(ChunkPrefix* header) override;

    bool unknown64_{false};
    int unknown68_{-1};
    int use_fallbacks_{0};
    Arena* fallbacks_[5]{};
    int fallback_reserve_[5]{};
};

} // namespace memory
