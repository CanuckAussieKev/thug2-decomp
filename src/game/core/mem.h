#pragma once

namespace Mem {

class Heap;

// Fill `size` bytes at `dest` with zero and return `dest`. Does nothing for a null pointer.
// Note the argument order: size first.
void* Clear(unsigned int size, void* dest);

// General allocation from the default heap (the engine's operator new). Makes sure the memory system is
// set up first.
void* New(unsigned int size);
// Release memory from any of the allocation functions.
void Free(void* memory);

// Allocate and zero. `mode` is passed straight to the heap; nearly every caller uses 1 (meaning not yet
// known). A null heap means the default heap.
void* Calloc(unsigned int size);
void* Calloc(unsigned int size, int mode);
void* Calloc(unsigned int size, Heap* heap, int mode);

// The memory manager's allocate: `size` bytes from `heap` (null = the current heap).
// Doesn't set the system up first - use New for that.
void* Allocate(unsigned int size, int mode, Heap* heap);

// The current-heap allocator file loading uses, its matching free, and giving back the
// unused end of such a block.
void* Malloc(unsigned int size);
void FreeMalloc(void* memory);
void* ReallocateShrink(unsigned int size, void* memory);
// The size of a Malloc block (0 for null).
unsigned int MallocSize(void* memory);
// The current heap's largest possible allocation.
unsigned int MallocAvailable();
// A new block of `size` bytes holding the old block's contents (copies `size` bytes); the old one is freed.
void* Realloc(void* memory, unsigned int size);

// The manager's two main heaps (at +0x110 and +0x114).
Heap* TopDownHeap();
Heap* BottomUpHeap();

// Until PopContext, allocations come from `heap`.
void PushContext(Heap* heap);
void PopContext();

} // namespace Mem
