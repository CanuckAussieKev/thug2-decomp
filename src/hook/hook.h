#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

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
    // Written and compiled but not hooked (REPLACE_SOURCE_ONLY): too small for a jump, or the hook isn't
    // wanted. The argument-size check still applies.
    bool sourceOnly = false;
    // REPLACE_FALLBACK: a test-runner hook, installed only while no other replacement is hooked at the same
    // address (none written yet, or it's disabled in thug2-decomp.ini / source only).
    bool fallback = false;
};

void RegisterReplacement(const Replacement& r);
void InstallReplacements();

// For each registered replacement: how many argument bytes the original pops on return (its RET n), found by
// walking the original's code - or -1 if that can't be determined (or the replacement opted out). Needs the
// exe's original code at its addresses: call before InstallReplacements (tools/check also uses it on a copy
// of the exe image).
struct ArgumentSizeCheck {
    Replacement replacement;
    int original_pop;
    bool Mismatch() const
    {
        return replacement.callee_pop >= 0 && original_pop >= 0 && replacement.callee_pop != original_pop;
    }
};
std::vector<ArgumentSizeCheck> CheckArgumentSizes();
// For tools that load the exe image somewhere other than 0x400000: the code at game address A is read from
// A + offset.
void SetCodeReadOffset(intptr_t offset);

// If InstallReplacements found replacements that can't be used safely (argument size differs from the
// original, or the jump couldn't be written), show a message naming them and end the game. Running on with
// the original code in their place would look like a working game while our code isn't running at all.
// Called from the first hooks the game reaches after DllMain (a message box inside DllMain can deadlock).
// Listing a replacement in thug2-decomp.ini's disable= skips it deliberately instead.
void StopIfReplacementsFailed();

// Write `size` bytes over the exe's code or read-only data at `address` (made writable for the write). For game
// source that still has to change library code or data the exe owns (the GameSpy host names, a CFunction table
// entry) until that code is linked or the data is source too.
bool WriteGameCode(uint32_t address, const void* bytes, size_t size);

// Permanently put back the original code at `address` (for one-shot hooks).
void RestoreOriginal(uint32_t address);

// Entry point of the original code of the function at `address`: its trampoline once our replacement is
// installed, otherwise `address` itself. Safe to call from any thread at any time.
void* OriginalFunction(uint32_t address);
// The function registered for `address` (the hooked one when there are two), null if none: tests stand in for
// ours as well as the original.
void* ReplacementFunction(uint32_t address);

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
    ReplacementRegistrar(uint32_t address, void* function, int callee_pop, const char* name, const char* file,
                         bool sourceOnly = false, bool fallback = false)
    {
        RegisterReplacement({address, function, callee_pop, name, file, sourceOnly, fallback});
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
struct CalleePop<R(__cdecl*)(A..., ...)> { // variadic functions are always cdecl
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

// The goal is source for the whole game (to build it without the original exe), so every function gets
// rewritten - also those that can't be hooked: shorter than the 5-byte jump (3-byte stubs), or ones we
// don't want hooked. They're registered (counted, argument sizes checked) but not installed:
// REPLACE_SOURCE_ONLY(0x005B9760, Script::StubReturnTrue); // 3 bytes
#define REPLACE_SOURCE_ONLY(address, ...)                                                            \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(__VA_ARGS__), kCalleePop<decltype(__VA_ARGS__)>, #__VA_ARGS__, __FILE__, true)

// The test runner's stage hooks (src/hook/selftest_runner.cpp) on game functions that will get source of their
// own: installed only while that source isn't hooked at the address. The game source then calls
// RunStageOnce (hook/selftest.h) itself. _UNCHECKED for naked stubs.
#define REPLACE_FALLBACK(address, ...)                                                                   static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                        address, CodeAddress(__VA_ARGS__), kCalleePop<decltype(__VA_ARGS__)>, #__VA_ARGS__, __FILE__, false, true)
#define REPLACE_FALLBACK_UNCHECKED(address, ...)                                                         static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                        address, CodeAddress(__VA_ARGS__), -1, #__VA_ARGS__, __FILE__, false, true)

// For naked assembly stubs, whose C++ type says nothing about the stack: the argument-size check is skipped.
#define REPLACE_UNCHECKED(address, ...)                                                              \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(__VA_ARGS__), -1, #__VA_ARGS__, __FILE__)

#ifdef THUG2_STANDALONE
// The standalone build: nothing to hook - the lines are only the map back to the original addresses.
#undef REPLACE
#undef REPLACE_SOURCE_ONLY
#undef REPLACE_FALLBACK
#undef REPLACE_FALLBACK_UNCHECKED
#undef REPLACE_UNCHECKED
#define REPLACE(address, ...) static_assert(true)
#define REPLACE_SOURCE_ONLY(address, ...) static_assert(true)
#define REPLACE_FALLBACK(address, ...) static_assert(true)
#define REPLACE_FALLBACK_UNCHECKED(address, ...) static_assert(true)
#define REPLACE_UNCHECKED(address, ...) static_assert(true)
#endif
