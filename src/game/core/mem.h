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

// The memory manager's allocate (not rewritten yet): `size` bytes from `heap` (null = the current heap).
// Doesn't set the system up first - use New for that.
void* Allocate(unsigned int size, int mode, Heap* heap);

// The current-heap allocator file loading uses (not rewritten yet), its matching free, and giving back the
// unused end of such a block.
void* Malloc(unsigned int size);
void FreeMalloc(void* memory);
void* ReallocateShrink(unsigned int size, void* memory);
// The size of a Malloc block (0 for null).
unsigned int MallocSize(void* memory);

// The manager's two main heaps (at +0x110 and +0x114).
Heap* TopDownHeap();
Heap* BottomUpHeap();

// Until PopContext, allocations come from `heap`. (Calls the game's manager; not rewritten yet.)
void PushContext(Heap* heap);
void PopContext();
// A heap pointer stored in the memory manager at byte `offset`. Temporary, until the manager's layout is
// decompiled and the heaps get names.
Heap* ManagerHeapAt(unsigned int offset);

} // namespace Mem
