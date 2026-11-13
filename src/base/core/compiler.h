#pragma once

// NOT ORIGINAL - the few compiler-specific things the code needs, each behind one name here (MSVC, and GCC / Clang
// for a MinGW build). Everything else is standard C++.

#ifdef _MSC_VER
#include <intrin.h>

// A function that must stay a real call (crash tests).
#define THUG2_NOINLINE __declspec(noinline)
// A tiny function always inlined: the hot loops' element accessors, which the compiler's heuristics left as calls.
#define THUG2_FORCEINLINE __forceinline
// The address the current function returns to (a debug tag).
#define THUG2_RETURN_ADDRESS() _ReturnAddress()
// An MSVC warning switched for the rest of the file / the next line: THUG2_MSVC_WARNING(disable : 4996).
#define THUG2_MSVC_WARNING(...) __pragma(warning(__VA_ARGS__))

#else

#define THUG2_NOINLINE __attribute__((noinline))
#define THUG2_FORCEINLINE inline __attribute__((always_inline))
#define THUG2_RETURN_ADDRESS() __builtin_return_address(0)
#define THUG2_MSVC_WARNING(...)

#endif
