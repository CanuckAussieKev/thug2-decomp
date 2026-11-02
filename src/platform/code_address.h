#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

// Code addresses as plain pointers: a function's or a (non-virtual, single-inheritance) member function's - for the
// tables the game builds by hand (the script member-function tables) and the callbacks it registers.

// The code address of a free function or a simple member function.
template <typename F>
void* code_address(F f) noexcept
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    void* p = nullptr;
    // NOLINTNEXTLINE(bugprone-bitwise-pointer-cast): a code address taken as an integer, on purpose
    std::memcpy(static_cast<void*>(&p), static_cast<const void*>(&f), sizeof(p));
    return p;
}

// A code address as a number (a log's caller).
inline uintptr_t code_address_value(const void* address) noexcept { return std::bit_cast<uintptr_t>(address); }

// A code address back as a function of type F (code_address's inverse): a callback the game keeps as a plain
// pointer, a DLL's export.
template <typename F>
F function_at(const void* address) noexcept
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    F f{};
    // NOLINTNEXTLINE(bugprone-bitwise-pointer-cast): a code address taken back as a function, on purpose
    std::memcpy(static_cast<void*>(&f), static_cast<const void*>(&address), sizeof(f));
    return f;
}

// A function kept in a 32-bit cell (a script symbol's native function, a compiled call's operand).
template <typename F>
F function_at(uint32_t cell) noexcept
{
    static_assert(sizeof(F) == sizeof(uint32_t), "a 32-bit build");
    return std::bit_cast<F>(cell);
}
