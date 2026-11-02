#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <ctime>
#include <string>

#include "engine/core/format.h"
#include "platform/code_address.h"
#include "platform/compiler.h"
#include <gsl/pointers>

// The C runtime and the system libraries (Windows, DirectX, Winsock, Bink) the game calls: the linked library, or -
// where the compiler's library behaves differently from the game's - our own version of the game's (qsort's order of
// equal elements, rand's sequence, _stricmp's result). Bink has no import library: its functions are
// looked up by name (library_function).

namespace crt {

// A DLL's exported function by name (the DLL loaded on first use; null if either is missing).
void* library_function(const char* dll, const char* name);
// The same as a function of type F.
template <typename F>
F library_function(const char* dll, const char* name)
{
    return function_at<F>(library_function(dll, name));
}

// --- Winsock ---
uint32_t get_inet_addr(const char* address); // inet_addr
const char* get_inet_ntoa(uint32_t address); // inet_ntoa (the address in network order)
void* get_host_by_name(const char* name);    // gethostbyname (a hostent*)
// gethostbyname's first IPv4 address, its bytes in network order as a word (0xFFFFFFFF: none).
uint32_t host_first_address(const char* name);

// --- The C runtime: its heap, files and exit ---
// NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the C runtime's allocator, as the library / original wants
inline gsl::owner<void*> alloc_block(size_t size) { return std::malloc(size); }
// NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the C runtime's allocator, as the library / original wants
inline void dealloc(gsl::owner<void*> memory) { std::free(memory); }
THUG2_MSVC_WARNING(suppress : 4996) // (std::fopen: MSVC calls it unsafe)
inline gsl::owner<FILE*> get_fopen(const char* path, const char* mode) { return std::fopen(path, mode); }
inline int get_fclose(gsl::owner<FILE*> file) { return std::fclose(file); }
inline size_t get_fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return std::fread(buffer, size, count, file);
}
inline int get_fseek(FILE* file, long offset, int origin) { return std::fseek(file, offset, origin); }
inline long get_ftell(FILE* file) { return std::ftell(file); }
// The game's exit() at a fatal error (no Direct3D device, a texture that can't be made) - not original: the C
// streams flushed and the process ended without the C runtime's shutdown (std::quick_exit). The original's exit() ran
// its atexit functions, which freed nothing the game still used; ours would run the static owners' destructors (the
// singletons held by std::unique_ptr / memory::HeapUnique) under the game's live objects and its other threads - as
// the normal end does (sys::fast_exit), the process ends with everything in place.
[[noreturn]] inline void exit_fatal(int code)
{
    std::fflush(nullptr);
    std::quick_exit(code);
}
// _cexit(): exit()'s clean-up without ending the process.
void c_exit();
// _exit(): the process ended at once.
[[noreturn]] inline void exit_now(int code)
{
    std::_Exit(code);
    std::abort(); // (never reached)
}
// time(nullptr), as the game's 32-bit time_t.
unsigned get_instant();
// atof (platform/crt.cpp's game_atof: the game's conversion).
double game_atof(const char* text);
inline double get_atof(const char* text) { return game_atof(text); }

// sprintf: the linked C runtime's text (text::format, the same byte for byte) - the same as the game's for integer and
// string conversions. Not for floats: the game's C runtime prints those differently (-0 as 0, halves rounded up in
// the last digit, digits past the 17th as zeros, 1.#INF00 / 1.#QNAN0), and no call here formats one. The text is
// written whole, with no limit, as sprintf wrote it; the format is checked against the arguments when it's compiled.
template <typename... Args>
int linked_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    const std::string made = text::format(format, args...);
    std::memcpy(out, made.c_str(), made.size() + 1); // (unbounded, as sprintf)
    return static_cast<int>(made.size());
}
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return linked_sprintf(out, format, args...);
}
// sprintf with a format that comes from data (a script's text): checked only when it runs (text::format_data).
template <typename... Args>
int sprintf_from_data(char* out, const char* format, Args... args)
{
    const std::string made = text::format_data(format ? format : "", args...);
    std::memcpy(out, made.c_str(), made.size() + 1); // (unbounded, as sprintf)
    return static_cast<int>(made.size());
}

// --- The game's C runtime's behaviour, as ours ---
using Weigh = int (*)(const void*, const void*);
// Its qsort: the same comparisons in the same order, so equal elements end up where the game's leave them.
void game_qsort(void* base, size_t count, size_t width, Weigh compare);
// Its _stricmp in the C locale: ASCII letters folded to lower case, -1 / 0 / 1.
int game_stricmp(const char* a, const char* b);
// Its rand(): a linear congruential sequence per thread, seeded with 1.
struct GameRandom {
    uint32_t state = 1;
    int get_next()
    {
        state = state * 214013u + 2531011u;
        return static_cast<int>((state >> 16) & 0x7FFF);
    }
};
void game_srand(unsigned seed);
int game_rand();

inline void get_qsort(void* base, size_t count, size_t width, Weigh compare)
{
    game_qsort(base, count, width, compare);
}
inline int stricmp(const char* a, const char* b) { return game_stricmp(a, b); }
inline void get_srand(unsigned seed) { game_srand(seed); }
inline int get_rand() { return game_rand(); }

} // namespace crt
