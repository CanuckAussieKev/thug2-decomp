#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

#include <gsl/pointers>

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

// NOT ORIGINAL - an `Object` constructed in `block` (not null): placement new as a call - `new (block) Object` with
// no arguments (default-initialised, as that spelling is), `new (block) Object(args...)` with them. The caller's
// pointer is passed as writable memory (clang-tidy's misc-const-correctness doesn't count a bare placement new as a
// write through it). The object is the caller's to end (gsl::owner): `delete` for a HeapObject, else HeapUnique (its
// destructor and the block's release).
template <typename Object, typename... Args>
gsl::owner<Object*> construct_in(void* block, Args&&... args)
{
    if constexpr (sizeof...(Args) == 0) {
        return new (block) Object;
    } else {
        return new (block) Object(std::forward<Args>(args)...);
    }
}
// NOT ORIGINAL - a new Object (its class's operator new, if it has one) for a holder that owns it: `new Object` with
// no arguments (default-initialised), `new Object(args...)` with them; make_owned_value is `new Object()`. A call,
// so the holder is spelt gsl::owner<Object*> (auto would drop the owner).
template <typename Object, typename... Args>
gsl::owner<Object*> make_owned(Args&&... args)
{
    if constexpr (sizeof...(Args) == 0) {
        return new Object;
    } else {
        return new Object(std::forward<Args>(args)...);
    }
}
// A block from an allocator that returns void* (the C runtime's, the heaps') taken as the T it is for, owned.
template <typename T>
gsl::owner<T> adopt(void* block)
{
    return static_cast<T>(block);
}
template <typename Object>
gsl::owner<Object*> make_owned_value()
{
    return new Object();
}
// `new (block) Object()`: value-initialised.
template <typename Object>
gsl::owner<Object*> construct_value_in(void* block)
{
    return new (block) Object();
}
// The same with the global placement new (`::new (block) Object`), past a class's own operator new.
template <typename Object, typename... Args>
gsl::owner<Object*> construct_global_in(void* block, Args&&... args)
{
    if constexpr (sizeof...(Args) == 0) {
        return ::new (block) Object;
    } else {
        return ::new (block) Object(std::forward<Args>(args)...);
    }
}

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

// NOT ORIGINAL - an object constructed in a game-heap block (placement new), owned: destroyed, then its block given
// back by dealloc (to the heap it came from, as HeapPtr's). For the classes without the game heaps' operator new that
// the game makes in a zeroed block (alloc_zeroed, then the constructor).
struct HeapDestroy {
    template <typename Object>
    void operator()(Object* object) const noexcept
    {
        object->~Object();
        dealloc(object);
    }
};
template <typename Object>
using HeapUnique = std::unique_ptr<Object, HeapDestroy>;
// NOT ORIGINAL - an owned object destroyed while its owner still points at it, then the pointer cleared - as the
// original's delete of a member (code its destructor runs may still reach it through the owner).
// std::unique_ptr::reset clears the pointer first.
template <typename Owned>
void destroy_then_clear(Owned& owned) noexcept
{
    if (auto* object = owned.get()) {
        owned.get_deleter()(object);
        (void)owned.release();
    }
}
// A zeroed block from the current heap with an `Object` constructed in it (null when the heap is full: nothing is
// constructed then).
template <typename Object, typename... Args>
HeapUnique<Object> make_zeroed(Args&&... args)
{
    void* block = alloc_zeroed(static_cast<unsigned int>(sizeof(Object)));
    if constexpr (sizeof...(Args) == 0) {
        return HeapUnique<Object>(block ? construct_value_in<Object>(block) : nullptr);
    } else {
        return HeapUnique<Object>(block ? construct_in<Object>(block, std::forward<Args>(args)...) : nullptr);
    }
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
