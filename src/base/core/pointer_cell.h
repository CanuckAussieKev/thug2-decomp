#pragma once

#include <cstdint>

#include <gsl/assert>

#include "base/core/address.h"

// NOT ORIGINAL - pointers kept in 32-bit integer cells, as the original's layouts and the script values keep them.
// Each use is a place where a pointer still travels as a 32-bit integer - a script value's cell, a callback's user
// word, an id that is an address, a link in a file used in place; the cells that are fields are still to be given
// their pointer types. On x86 a cell holds a pointer exactly: nothing is converted, the same 32 bits. On x64 a
// pointer doesn't fit one: a cell may only hold a pointer whose address is below 4 GB, and cell::of stops the game
// (GSL's contract check) on any other instead of dropping its top half - each use is a boundary still to be given
// a 64-bit form (docs/design/x64-port.md). Address arithmetic at the pointer's own width is core::address /
// core::at_address (base/core/address.h).
namespace cell {

// The pointer as its cell's 32 bits.
template <typename P>
uint32_t of(P* pointer) noexcept
{
    const uintptr_t value = core::address(pointer);
    if constexpr (sizeof(uintptr_t) > sizeof(uint32_t)) {
        Expects(value <= UINT32_MAX); // (x64: a pointer above 4 GB doesn't fit a cell)
    }
    return static_cast<uint32_t>(value);
}

// The pointer a cell holds.
template <typename T>
T* pointer(uint32_t value) noexcept
{
    return core::at_address<T>(value);
}

} // namespace cell
