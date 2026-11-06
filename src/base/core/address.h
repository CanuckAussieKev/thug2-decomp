#pragma once

#include <bit>
#include <cstdint>

// NOT ORIGINAL - a pointer as a number and back, at the pointer's own width (32 bits on x86, 64 on x64): for address
// arithmetic (alignment, the distance between two objects, offsets in a file read in place) and for the operating
// system's and the libraries' pointer-sized values (a handle as a number, a texture id). Nothing is narrowed: the
// number is a uintptr_t. A pointer kept in a 32-bit cell of the original's layouts is cell:: (base/core/pointer_cell.h).
namespace core {

// The pointer as a number.
template <typename P>
uintptr_t address(P* pointer) noexcept
{
    return std::bit_cast<uintptr_t>(pointer);
}

// The pointer at an address computed as a number.
template <typename T>
T* at_address(uintptr_t value) noexcept
{
    return std::bit_cast<T*>(value);
}

} // namespace core
