#pragma once

#include <cstring>

// Code addresses as plain pointers: a function's or a (non-virtual, single-inheritance) member function's - for the
// tables the game builds by hand (the script member-function tables) and the callbacks it registers.

// The code address of a free function or a simple member function.
template <typename F>
void* code_address(F f)
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    void* p;
    std::memcpy(&p, &f, sizeof(p));
    return p;
}
