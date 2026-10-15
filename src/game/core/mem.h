#pragma once

namespace Mem {

// Fill `size` bytes at `dest` with zero and return `dest`. Does nothing for a null pointer.
// Note the argument order: size first.
void* Clear(unsigned int size, void* dest);

} // namespace Mem
