#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Replacing original game functions with our own.
//
// Each decompiled function registers itself with REPLACE(address, function). On startup every registered
// replacement is installed (with MinHook) by writing a jump at the original function's entry point, so all
// callers in the exe end up in our code. MinHook also builds a trampoline for each one - the original's first
// instructions followed by a jump back into it - so the original stays callable through Original().
//
// The replacement must use the same calling convention and arguments as the original. Check the original's
// disassembly: VC7.1 with whole-program optimisation can give internal functions non-standard conventions.
// At install time the number of argument bytes each replacement pops (from its C++ type) is compared with the
// original's RET instructions; a replacement that doesn't match isn't installed.

struct Replacement {
    uint32_t address;
    void* function;
    // Bytes of arguments the replacement removes from the stack when it returns (its RET n), from its C++
    // type; -1 if not known (naked assembly stubs).
    int callee_pop;
    const char* name;
    const char* file; // source file of the REPLACE line (thug2-decomp.ini can disable by file name)
};

void RegisterReplacement(const Replacement& r);
void InstallReplacements();

// Permanently put back the original code at `address` (for one-shot hooks).
void RestoreOriginal(uint32_t address);

// Entry point of the original code of the function at `address`: its trampoline once our replacement is
// installed, otherwise `address` itself. Safe to call from any thread at any time.
void* OriginalFunction(uint32_t address);

// While one of these exists, *every* call to `address` - including calls made from inside other original
// functions - runs the original code instead of our replacement. Original() alone only affects your own
// call; use this when a test wants an original function to use original helpers too:
//
//   { OriginalCode a(0x00401290), b(0x004013E0); Original<EncodeFn>(0x00401700)(...); }
//
// Only for self-tests at a controlled point on the main thread: other threads calling the function meanwhile
// would also get the original.
class OriginalCode {
public:
    explicit OriginalCode(uint32_t address);
    ~OriginalCode();
    OriginalCode(const OriginalCode&) = delete;
    OriginalCode& operator=(const OriginalCode&) = delete;

private:
    uint32_t m_address;
};

// The original game function at `address`, typed. Always runs the original code (see OriginalFunction).
template <typename Fn>
Fn Original(uint32_t address)
{
    return reinterpret_cast<Fn>(OriginalFunction(address));
}

struct ReplacementRegistrar {
    ReplacementRegistrar(uint32_t address, void* function, int callee_pop, const char* name, const char* file)
    {
        RegisterReplacement({address, function, callee_pop, name, file});
    }
};

// Code address of a free function or a (non-virtual, single-inheritance) member function.
template <typename F>
void* CodeAddress(F f)
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    void* p;
    std::memcpy(&p, &f, sizeof(p));
    return p;
}

// How many bytes of arguments a function of type F pops when it returns: none for cdecl; all of them for
// stdcall and thiscall (member functions), plus the hidden return-value pointer when a member function returns
// a class by value. -1 for anything else (not checked).
template <typename T>
constexpr int kArgumentBytes =
    std::is_reference_v<T> ? 4 : static_cast<int>((sizeof(T) + 3) & ~std::size_t(3)); // references are pointers
template <typename R>
constexpr int kHiddenReturnBytes = std::is_class_v<R> ? 4 : 0;

template <typename F>
struct CalleePop {
    static constexpr int value = -1;
};
template <typename R, typename... A>
struct CalleePop<R(__cdecl*)(A...)> {
    static constexpr int value = 0;
};
template <typename R, typename... A>
struct CalleePop<R(__stdcall*)(A...)> {
    static constexpr int value = (0 + ... + kArgumentBytes<A>);
};
template <typename R, typename C, typename... A>
struct CalleePop<R (C::*)(A...)> {
    static constexpr int value = kHiddenReturnBytes<R> + (0 + ... + kArgumentBytes<A>);
};
template <typename R, typename C, typename... A>
struct CalleePop<R (C::*)(A...) const> {
    static constexpr int value = kHiddenReturnBytes<R> + (0 + ... + kArgumentBytes<A>);
};
template <typename F>
constexpr int kCalleePop = CalleePop<std::decay_t<F>>::value;

// REPLACE(0x00401B90, Crc::GenerateCRCFromString);
// REPLACE(0x00402340, &Mth::Vector::operator+=);
// Overloads need a cast to pick one: REPLACE(addr, static_cast<Vector (*)(const Vector&, float)>(&operator*));
// Constructors can't be pointed to, so wrap them in a small adapter that placement-news the object.
#define REPLACE_CONCAT2(a, b) a##b
#define REPLACE_CONCAT(a, b) REPLACE_CONCAT2(a, b)
#define REPLACE(address, ...)                                                                        \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(__VA_ARGS__), kCalleePop<decltype(__VA_ARGS__)>, #__VA_ARGS__, __FILE__)

// For naked assembly stubs, whose C++ type says nothing about the stack: the argument-size check is skipped.
#define REPLACE_UNCHECKED(address, ...)                                                              \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(__VA_ARGS__), -1, #__VA_ARGS__, __FILE__)
