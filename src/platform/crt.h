#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "platform/compiler.h"

// The C runtime and the system libraries (Windows, DirectX, Winsock, Bink) the game calls: the linked library, or -
// where the compiler's library behaves differently from the game's - our own version of the game's (qsort's order of
// equal elements, rand's sequence, _stricmp's result). Bink has no import library: its functions are
// looked up by name (library_function).

namespace crt {

// A DLL's exported function by name (the DLL loaded on first use; null if either is missing).
void* library_function(const char* dll, const char* name);

// --- Winsock ---
uint32_t get_inet_addr(const char* address); // inet_addr
const char* get_inet_ntoa(uint32_t address); // inet_ntoa (the address in network order)
void* get_host_by_name(const char* name);    // gethostbyname (a hostent*)

// --- The C runtime: its heap, files and exit ---
inline void* alloc_block(size_t size) { return std::malloc(size); }
inline void dealloc(void* memory) { std::free(memory); }
THUG2_MSVC_WARNING(suppress : 4996) // (std::fopen: MSVC calls it unsafe)
inline FILE* get_fopen(const char* path, const char* mode) { return std::fopen(path, mode); }
inline int get_fclose(FILE* file) { return std::fclose(file); }
inline size_t get_fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return std::fread(buffer, size, count, file);
}
inline int get_fseek(FILE* file, long offset, int origin) { return std::fseek(file, offset, origin); }
inline long get_ftell(FILE* file) { return std::ftell(file); }
// exit(): the atexit functions, the streams flushed, the process ended.
[[noreturn]] inline void get_exit(int code)
{
    std::exit(code);
    std::abort(); // (never reached)
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

// sprintf: the compiler's - the same as the game's for integer and string conversions. Not for floats: the game's C runtime prints those differently (-0 as 0, halves rounded up in
// the last digit, digits past the 17th as zeros, 1.#INF00 / 1.#QNAN0), and no call here formats one.
int linked_sprintf(char* out, const char* format, ...);
template <typename... Args>
int get_sprintf(char* out, const char* format, Args... args)
{
    return linked_sprintf(out, format, args...);
}

// --- The game's C runtime's behaviour, as ours ---
using Weigh = int(__cdecl*)(const void*, const void*);
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
