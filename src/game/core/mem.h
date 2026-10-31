#pragma once

namespace MemSys {

class Arena;

// Fill `size` bytes at `dest` with zero and return `dest`. Does nothing for a null pointer.
// Note the argument order: size first.
void* Clear(unsigned int size, void* dest);

// General allocation from the default heap (the engine's operator new). Makes sure the memory system is
// set up first.
void* Alloc(unsigned int size);
// Release memory from any of the allocation functions.
void Dealloc(void* memory);

// Allocate and zero. `mode` is passed straight to the heap; nearly every caller uses 1 (meaning not yet
// known). A null heap means the default heap.
void* AllocZeroed(unsigned int size);
void* AllocZeroed(unsigned int size, int mode);
void* AllocZeroed(unsigned int size, Arena* heap, int mode);

// Memory profiling: the game brackets allocations with these, naming what is allocated. Compiled out of this
// build - both calls go to the empty function at 0x005480A0 (see Stubs::DoNothing) - so they do nothing here
// either; they're kept to show where the original profiles.
inline void PushMemProfile(const char* /*name*/) {}
inline void PopMemProfile() {}

// The memory manager's allocate: `size` bytes from `heap` (null = the current heap).
// Doesn't set the system up first - use New for that.
void* Carve(unsigned int size, int mode, Arena* heap);

// The current-heap allocator file loading uses, its matching free, and giving back the
// unused end of such a block.
void* AllocBlock(unsigned int size);
void FreeMalloc(void* memory);
void* TrimBlockEnd(unsigned int size, void* memory);
// The size of a Malloc block (0 for null).
unsigned int MallocSize(void* memory);
// The current heap's largest possible allocation.
unsigned int MallocAvailable();
// A new block of `size` bytes holding the old block's contents (copies `size` bytes); the old one is freed.
void* Regrow(void* memory, unsigned int size);

// The manager's two main heaps (at +0x110 and +0x114).
Arena* TopDownHeap();
Arena* BottomUpHeap();

// Until LeaveHeapScope, allocations come from `heap`.
void EnterHeapScope(Arena* heap);
void LeaveHeapScope();

} // namespace Mem
