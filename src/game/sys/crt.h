#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// The C runtime and the system libraries (Windows, DirectX, Winsock, Bink) the game calls: the linked library, or -
// where the compiler's library behaves differently from the game's - our own version of the game's (qsort's order of
// equal elements, rand's sequence, _stricmp's result, _CIasin / _CIacos on the x87). Bink has no import library: its
// functions are looked up by name (LibraryFunction).

namespace Crt {

// A DLL's exported function by name (the DLL loaded on first use; null if either is missing).
void* LibraryFunction(const char* dll, const char* name);

// --- Winsock ---
uint32_t InetAddr(const char* address);    // inet_addr
const char* InetNtoa(uint32_t address);    // inet_ntoa (the address in network order)
void* GetHostByName(const char* name);     // gethostbyname (a hostent*)

// --- The C runtime: its heap, files and exit ---
inline void* Malloc(size_t size) { return std::malloc(size); }
inline void Free(void* memory) { std::free(memory); }
inline FILE* Fopen(const char* path, const char* mode)
{
    return std::fopen(path, mode);
}
inline int Fclose(FILE* file) { return std::fclose(file); }
inline size_t Fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return std::fread(buffer, size, count, file);
}
inline int Fseek(FILE* file, long offset, int origin)
{
    return std::fseek(file, offset, origin);
}
inline long Ftell(FILE* file) { return std::ftell(file); }
// exit(): the atexit functions, the streams flushed, the process ended.
[[noreturn]] inline void Exit(int code)
{
    std::exit(code);
    std::abort(); // (never reached)
}
// _cexit(): exit()'s clean-up without ending the process.
void CExit();
// _exit(): the process ended at once.
[[noreturn]] inline void ExitNow(int code)
{
    std::_Exit(code);
    std::abort(); // (never reached)
}
// time(nullptr), as the game's 32-bit time_t.
unsigned Time();
// atof (game/sys/crt.cpp's GameAtof: the game's conversion).
double GameAtof(const char* text);
inline double Atof(const char* text) { return GameAtof(text); }

// sprintf: the compiler's - the same as the game's for integer and string conversions. Not for floats: the game's C runtime prints those differently (-0 as 0, halves rounded up in
// the last digit, digits past the 17th as zeros, 1.#INF00 / 1.#QNAN0), and no call here formats one.
int LinkedSprintf(char* out, const char* format, ...);
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return LinkedSprintf(out, format, args...);
}

// --- The game's C runtime's behaviour, as ours ---
using Compare = int(__cdecl*)(const void*, const void*);
// Its qsort: the same comparisons in the same order, so equal elements end up where the game's leave them.
void GameQsort(void* base, size_t count, size_t width, Compare compare);
// Its _stricmp in the C locale: ASCII letters folded to lower case, -1 / 0 / 1.
int GameStricmp(const char* a, const char* b);
// Its rand(): a linear congruential sequence per thread, seeded with 1.
struct GameRandom {
    uint32_t state = 1;
    int Next()
    {
        state = state * 214013u + 2531011u;
        return static_cast<int>((state >> 16) & 0x7FFF);
    }
};
void GameSrand(unsigned seed);
int GameRand();
// Its _CIasin / _CIacos: the argument and the result in ST0 (call them with CALL from x87 code).
void GameAsinX87();
void GameAcosX87();

inline void Qsort(void* base, size_t count, size_t width, Compare compare)
{
    GameQsort(base, count, width, compare);
}
inline int Stricmp(const char* a, const char* b)
{
    return GameStricmp(a, b);
}
inline void Srand(unsigned seed) { GameSrand(seed); }
inline int Rand() { return GameRand(); }
// The addresses x87 code calls (fld value; call [function]).
inline void* AsinX87() { return reinterpret_cast<void*>(GameAsinX87); }
inline void* AcosX87() { return reinterpret_cast<void*>(GameAcosX87); }

} // namespace Crt
