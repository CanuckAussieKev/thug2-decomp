#pragma once

#include <bit>
#include <cstdint>

// Code addresses as plain pointers: a function's or a (non-virtual, single-inheritance) member function's - for the
// tables the game builds by hand (the script member-function tables) and the callbacks it registers.

// The code address of a free function or a simple member function.
template <typename F>
void* code_address(F function) noexcept
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    // (through the address as a number: the same bits, never one pointer type read as another)
    return std::bit_cast<void*>(std::bit_cast<uintptr_t>(function));
}

// A code address as a number (a log's caller).
inline uintptr_t code_address_value(const void* address) noexcept { return std::bit_cast<uintptr_t>(address); }

// A code address back as a function of type F (code_address's inverse): a callback the game keeps as a plain
// pointer, a DLL's export.
template <typename F>
F function_at(const void* address) noexcept
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    return std::bit_cast<F>(std::bit_cast<uintptr_t>(address));
}
