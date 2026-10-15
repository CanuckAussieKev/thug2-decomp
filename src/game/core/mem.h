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

} // namespace Mem
