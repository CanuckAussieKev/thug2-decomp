#pragma once

#include <bit>
#include <cstdint>

// NOT ORIGINAL - pointers kept in 32-bit integer cells, as the original's layouts and the script values keep them
// (the game is 32-bit x86 by design: a cell holds a pointer exactly). Each use is a place where a pointer still
// travels as an integer - a script value's cell, a callback's user word, an id that is an address, a hash of an
// address; stage 9's round 3 gives the cells their pointer types where they are fields. Nothing is converted: the
// same 32 bits.
namespace cell {

static_assert(sizeof(void*) == sizeof(uint32_t), "a pointer fits a 32-bit cell (x86)");

// The pointer as its cell's 32 bits.
template <typename P>
uint32_t of(P* pointer) noexcept
{
    return std::bit_cast<uint32_t>(pointer);
}

// The pointer a cell holds.
template <typename T>
T* pointer(uint32_t value) noexcept
{
    return std::bit_cast<T*>(value);
}

// The pointer as a number for address arithmetic (alignment, the distance between two objects).
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

} // namespace cell
