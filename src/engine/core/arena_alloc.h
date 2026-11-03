#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include <gsl/pointers>

#include "platform/all_ones.h"

// The lowest layer of the memory manager: regions of memory, the allocators that carve them up from either
// end, and the header in front of every allocated block.
//
// A Region is a range of memory shared by at most two allocators: one growing up from the start, one growing
// down from the end. Each allocator (memory::Arena and friends derive from Allocator) keeps its own "top" - how
// far it has claimed - and the region refuses claims that would make the two cross.
namespace memory {

class MemSource;

// NOT ORIGINAL - the heaps' memory as one run of bytes: the main region, which holds every block any heap hands out
// (the other regions are blocks of its heaps), set when the memory manager makes it. Headers and blocks are found by
// their offset in it - a block's header is the 16 bytes before the block, the next header the bytes after it -
// each reach checked against the run (a pointer that isn't a heap block ends the game with a report instead of
// reading whatever lies before it).
std::span<uint8_t> heap_memory();
void set_heap_memory(std::span<uint8_t> memory);
// Where `place` is in the heaps' memory (its end included).
uint32_t heap_offset(const void* place);

// In front of every block: the allocator that owns it, its size, and a marker.
struct ChunkPrefix {
    // The fields other code reads and sets.
    [[nodiscard]] MemSource* allocator() const { return allocator_; }
    void set_allocator(MemSource* value) { allocator_ = value; }
    uint32_t& size() { return size_; }
    [[nodiscard]] uint32_t size() const { return size_; }
    [[nodiscard]] uint32_t marker() const { return marker_; }
    void set_marker(uint32_t value) { marker_ = value; }

private:
    MemSource* allocator_;
    uint32_t size_;
    uint32_t marker_; // kBlockInUse, or kBlockFreed once released
    ChunkPrefix* next_free_; // while the block is on a heap's free list: the next free block (the original kept

public:
                             // the link in `allocator`; the fourth word was unused)

    static constexpr uint32_t kBlockInUse = 0xCF801FE1;
    static constexpr uint32_t kBlockFreed = 0xDEADDEAD;

    // The header in front of `block`.
    static ChunkPrefix* get_of(void* block);
    // The header made in the heaps' memory at `offset` (a block being split or given back in part).
    static gsl::owner<ChunkPrefix*> make_at(uint32_t offset);
    // Where this header is in the heaps' memory; its block's start (16 bytes on) and end (the next header's place:
    // block sizes are multiples of the header's 16 bytes).
    [[nodiscard]] uint32_t offset() const { return heap_offset(this); }
    [[nodiscard]] uint32_t block_offset() const;
    [[nodiscard]] uint32_t end_offset() const { return block_offset() + size_; }
    // The block itself.
    void* gate_off();
    // Whether `other` is the header right after this block.
    [[nodiscard]] bool is_followed_by(const ChunkPrefix* other) const { return end_offset() == other->offset(); }
    [[nodiscard]] ChunkPrefix* next_free() const { return next_free_; }
    void set_next_free(ChunkPrefix* value) { next_free_ = value; }
};
constexpr uint32_t kBlockHeaderSize = sizeof(ChunkPrefix);
constexpr size_t kBlockHeaderBytes = 0x10;
inline uint32_t ChunkPrefix::block_offset() const { return offset() + kBlockHeaderSize; }

// Memory debugging: the heap reports each block it hands out, splits or resizes (before and after the change).
// Compiled out of this build (the empty function at 0x005480A0), kept to show where the original reports.
inline void debug_track_block(ChunkPrefix* /*block*/) {}
static_assert(sizeof(ChunkPrefix) == kBlockHeaderBytes, "every block's header is 16 bytes");

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

    // 0x005BC5F0 (the original took its start and end)
    explicit Zone(std::span<uint8_t> memory);
    // 0x005BC4D0 / 0x005BC6D0 (vtable 0x00658A58): nothing - the memory isn't the region's.
    virtual ~Zone() = default;
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    // Attach / detach the allocator on its side (by its direction).
    void add_allocator(MemSource* allocator);
    void remove_allocator(MemSource* allocator);
    // The space between the two allocators' tops.
    int free_byte_count();
    // Move `allocator`'s top by `size` (in its direction). Returns the claimed space - from the old top growing up,
    // from the new top growing down - or an empty span if it would cross the other allocator.
    std::span<uint8_t> carve(MemSource* allocator, int size, int unused);
    // NOT ORIGINAL - the region's size, and where `place` is in it (its end included).
    [[nodiscard]] uint32_t byte_size() const { return static_cast<uint32_t>(memory_.size()); }
    [[nodiscard]] uint32_t offset_of(const void* place) const;

    // The fields other code reads and sets.
    std::span<uint8_t>& memory() { return memory_; }
    [[nodiscard]] const std::span<uint8_t>& memory() const { return memory_; }
    [[nodiscard]] int min_available() const { return min_available_; }
    void set_min_available(int value) { min_available_ = value; }
    std::array<MemSource*, 2>& allocators() { return allocators_; }
    [[nodiscard]] const std::array<MemSource*, 2>& allocators() const { return allocators_; }

private:
    std::span<uint8_t> memory_; // the region's bytes (the original's start and end pointers)
    int min_available_{}; // the least free_byte_count has been (starts as the whole region)
    std::array<MemSource*, 2> allocators_{}; // growing up, growing down

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
    // What a list's head keeps as its item: -1 (some heads are filled with 0xFF bytes rather than made: the value,
    // not just the marker, is what they compare).
    static void* head_marker() noexcept { return platform::all_ones_pointer(); }

    // The fields other code reads and sets.
    [[nodiscard]] void* data() const { return data_; }
    void set_data(void* value) { data_ = value; }
    [[nodiscard]] int priority() const { return priority_; }
    void set_priority(int value) { priority_ = value; }
    [[nodiscard]] ListNode* next() const { return next_; }
    void set_next(ListNode* value) { next_ = value; }
    [[nodiscard]] ListNode* prev() const { return prev_; }
    void set_prev(ListNode* value) { prev_ = value; }

private:
    void* data_;
    int priority_;
    ListNode* next_;
    ListNode* prev_;

public:

    // 0x005BA0E0 (and the other node types' destructors, 0x005BC990 / 0x005BCD30...): out of its list, linked to
    // itself.
    void uncouple();
    // A list's head: data -1, priority 0, linked to itself.
    void make_head();
    // A node for `item`, linked to itself.
    void make_node(void* item, int item_priority = 0);
    // Whether this is a list's head.
    [[nodiscard]] bool is_head() const { return data_ == head_marker(); }

    // A node of its own (zeroed from the current heap, then made for `item`; null without memory) and its deleting
    // destructor (out of its list, freed; null-safe).
    static ListNode* create(void* item, int item_priority = 0);
    static ListNode* create_head();
    static void destroy(ListNode* node);
};
constexpr size_t kListNodeBytes = 0x10;
static_assert(sizeof(ListNode) == kListNodeBytes, "a list node is four words");

class MemSource {
public:
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): indexes into tables and arrays
    enum Direction : int8_t { kGrowUp = 1, kGrowDown = -1 };

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

    // The fields other code reads and sets.
    int& used_blocks() { return used_blocks_; }
    [[nodiscard]] int used_blocks() const { return used_blocks_; }
    int& max_used_blocks() { return max_used_blocks_; }
    [[nodiscard]] int max_used_blocks() const { return max_used_blocks_; }
    int& free_blocks() { return free_blocks_; }
    [[nodiscard]] int free_blocks() const { return free_blocks_; }
    int& max_free_blocks() { return max_free_blocks_; }
    [[nodiscard]] int max_free_blocks() const { return max_free_blocks_; }
    int& used_size() { return used_size_; }
    [[nodiscard]] int used_size() const { return used_size_; }
    int& max_used_size() { return max_used_size_; }
    [[nodiscard]] int max_used_size() const { return max_used_size_; }
    int& free_size() { return free_size_; }
    [[nodiscard]] int free_size() const { return free_size_; }
    int& max_free_size() { return max_free_size_; }
    [[nodiscard]] int max_free_size() const { return max_free_size_; }
    [[nodiscard]] Zone* region() const { return region_; }
    [[nodiscard]] int direction() const { return direction_; }
    uint32_t& top_side() { return top_side_; }
    [[nodiscard]] uint32_t top_side() const { return top_side_; }
    [[nodiscard]] CtxInfo* context() const { return context_; }
    [[nodiscard]] const char* label() const { return label_; }

private:
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
    uint32_t top_side_{}; // how far it has claimed in the region (an offset in it; the original's was a pointer)
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

    // The fields other code reads and sets.
    void set_unknown64(bool value) { unknown64_ = value; }
    void set_unknown68(int value) { unknown68_ = value; }

private:
    static constexpr size_t kFallbackCount = 5; // the heaps tried, in order, when this one is full
    bool unknown64_{false};
    int unknown68_{-1};
    int use_fallbacks_{0};
    std::array<Arena*, kFallbackCount> fallbacks_{};
    std::array<int, kFallbackCount> fallback_reserve_{};
};

} // namespace memory
