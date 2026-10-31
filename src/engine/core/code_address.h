#pragma once

#include <cstring>

// Code addresses as plain pointers: a function's, a (non-virtual, single-inheritance) member function's, and the
// tables of them the game builds by hand (its virtual tables and script member-function tables).

// The code address of a free function or a simple member function.
template <typename F>
void* code_address(F f)
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    void* p;
    std::memcpy(&p, &f, sizeof(p));
    return p;
}

extern "C" int __cdecl _purecall(void); // (the C runtime's: a pure virtual slot)

// One slot of a hand-built virtual table:
//
//   static VtableSlot g_region_vtable_slots[] = {&AllocAdapters::region_destroy, &_purecall};
//   void** const kRegionVtable = reinterpret_cast<void**>(g_region_vtable_slots);
struct VtableSlot {
    void* code;
    template <typename F>
    VtableSlot(F f) : code(code_address(f))
    {}
};
static_assert(sizeof(VtableSlot) == sizeof(void*), "a table of VtableSlots is a table of code pointers");
