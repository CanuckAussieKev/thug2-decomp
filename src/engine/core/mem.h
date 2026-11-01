#pragma once

#include <cstddef>
#include <memory>

namespace memory {

class Arena;

// Fill `size` bytes at `dest` with zero and return `dest`. Does nothing for a null pointer.
// Note the argument order: size first.
void* get_clear(unsigned int size, void* dest);

// General allocation from the default heap (the engine's operator new). Makes sure the memory system is
// set up first.
void* alloc(unsigned int size);
// Release memory from any of the allocation functions.
void dealloc(void* memory);

// Allocate and zero. `mode` is passed straight to the heap; nearly every caller uses 1 (meaning not yet
// known). A null heap means the default heap.
void* alloc_zeroed(unsigned int size);
void* alloc_zeroed(unsigned int size, int mode);
void* alloc_zeroed(unsigned int size, Arena* heap, int mode);

// A base that gives a class the game heaps' operator new / delete: zeroed memory from the current heap (as the game
// allocates most of its objects) and back to it. `new` gives null when the heap is full - operator new is noexcept,
// so no constructor runs then.
struct HeapObject {
    static void* operator new(std::size_t size) noexcept { return alloc_zeroed(static_cast<unsigned int>(size)); }
    static void operator delete(void* memory) { dealloc(memory); }
    // Construction in place (inside another object) as usual.
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void*, void*) noexcept {}
};

// NOT ORIGINAL - a block from the game's heaps (alloc / alloc_zeroed / alloc_block / carve), owned: given back by
// dealloc when it goes. A block always goes back to the heap it came from (its header names it), whatever heap scope
// is current then - so it may be freed outside the scope it was allocated in. Nothing is constructed or destroyed:
// for plain data (buffers, records).
struct HeapFree {
    void operator()(void* block) const noexcept { dealloc(block); }
};
template <typename Data>
using HeapPtr = std::unique_ptr<Data, HeapFree>;
template <typename Data>
HeapPtr<Data> heap_ptr(void* block)
{
    return HeapPtr<Data>(static_cast<Data*>(block));
}

// Memory profiling: the game brackets allocations with these, naming what is allocated. Compiled out of this
// build - both calls go to the empty function at 0x005480A0 (see stubs::do_nothing) - so they do nothing here
// either; they're kept to show where the original profiles.
inline void push_mem_profile(const char* /*name*/) {}
inline void pop_mem_profile() {}

// The memory manager's allocate: `size` bytes from `heap` (null = the current heap).
// Doesn't set the system up first - use New for that.
void* carve(unsigned int size, int mode, Arena* heap);

// The current-heap allocator file loading uses, its matching free, and giving back the
// unused end of such a block.
void* alloc_block(unsigned int size);
void free_malloc(void* memory);
void* trim_block_end(unsigned int size, void* memory);
// The size of a Malloc block (0 for null).
unsigned int malloc_size(void* memory);
// The current heap's largest possible allocation.
unsigned int malloc_available();
// A new block of `size` bytes holding the old block's contents (copies `size` bytes); the old one is freed.
void* regrow(void* memory, unsigned int size);

// The manager's two main heaps (at +0x110 and +0x114).
Arena* top_down_heap();
Arena* bottom_up_heap();

// Until leave_heap_scope, allocations come from `heap`.
void enter_heap_scope(Arena* heap);
void leave_heap_scope();

} // namespace memory
