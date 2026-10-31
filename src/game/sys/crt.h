#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "hook/game.h"

// The C runtime and the system libraries (Windows, DirectX, Winsock, Bink) the game calls.
//
// In the hooked build every call goes where the original's goes - the exe's statically linked C runtime, its import
// thunks and its import table: memory, files and exit handlers cross between the exe's remaining code and ours, and
// the self-tests stand in for those addresses. In the standalone build (THUG2_STANDALONE) the linked library is
// called, or - where the compiler's library behaves differently from the game's - our own version of the game's
// (qsort's order of equal elements, rand's sequence, _stricmp's result, _CIasin / _CIacos on the x87); the self-tests
// (selftests_crt.cpp) prove those equal to the exe's.
//
//   GAME_LIBRARY(address, type, function)  a library function (the exe's at `address` / `function`)
//   GAME_IMPORT(slot, type, function)      a DLL function through the exe's import table slot / `function`
//   GAME_DLL_IMPORT(slot, type, dll, name) the same for a DLL with no import library (Bink): looked up by name
// Each is a function pointer of `type`; `function` is type-checked in both builds.
#ifdef THUG2_STANDALONE
#define GAME_LIBRARY(address, type, function) (static_cast<type>(function))
#define GAME_IMPORT(slot, type, function) (static_cast<type>(function))
#define GAME_DLL_IMPORT(slot, type, dll, name) (reinterpret_cast<type>(Crt::LibraryFunction(dll, name)))
#else
#define GAME_LIBRARY(address, type, function) ((void)sizeof(static_cast<type>(function)), GameFunction<type>(address))
#define GAME_IMPORT(slot, type, function) ((void)sizeof(static_cast<type>(function)), GameVariable<type>(slot))
#define GAME_DLL_IMPORT(slot, type, dll, name) (GameVariable<type>(slot))
#endif

namespace Crt {

// A DLL's exported function by name (the DLL loaded on first use; null if either is missing).
void* LibraryFunction(const char* dll, const char* name);

// --- Winsock (through the exe's import thunks) ---
uint32_t InetAddr(const char* address);    // inet_addr
const char* InetNtoa(uint32_t address);    // inet_ntoa (the address in network order)
void* GetHostByName(const char* name);     // gethostbyname (a hostent*)

// --- The C runtime the exe's code shares: its heap, files and exit ---
inline void* Malloc(size_t size) { return GAME_LIBRARY(0x00624031, void*(__cdecl*)(size_t), std::malloc)(size); }
inline void Free(void* memory) { GAME_LIBRARY(0x0062411B, void(__cdecl*)(void*), std::free)(memory); }
inline FILE* Fopen(const char* path, const char* mode)
{
    return GAME_LIBRARY(0x006241E8, FILE*(__cdecl*)(const char*, const char*), std::fopen)(path, mode);
}
inline int Fclose(FILE* file) { return GAME_LIBRARY(0x00623E04, int(__cdecl*)(FILE*), std::fclose)(file); }
inline size_t Fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return GAME_LIBRARY(0x00623F3E, size_t(__cdecl*)(void*, size_t, size_t, FILE*), std::fread)(buffer, size, count, file);
}
inline int Fseek(FILE* file, long offset, int origin)
{
    return GAME_LIBRARY(0x006240D2, int(__cdecl*)(FILE*, long, int), std::fseek)(file, offset, origin);
}
inline long Ftell(FILE* file) { return GAME_LIBRARY(0x00625388, long(__cdecl*)(FILE*), std::ftell)(file); }
// exit(): the atexit functions, the streams flushed, the process ended.
[[noreturn]] inline void Exit(int code)
{
    GAME_LIBRARY(0x00624AFE, void(__cdecl*)(int), std::exit)(code);
    std::abort(); // (never reached)
}
// _cexit(): exit()'s clean-up without ending the process.
void CExit();
// _exit(): the process ended at once.
[[noreturn]] inline void ExitNow(int code)
{
    GAME_LIBRARY(0x00624B0F, void(__cdecl*)(int), std::_Exit)(code);
    std::abort(); // (never reached)
}
// time(nullptr), as the game's 32-bit time_t.
unsigned Time();
// atof (game/sys/crt.cpp's GameAtof in the standalone build).
double GameAtof(const char* text);
inline double Atof(const char* text) { return GAME_LIBRARY(0x006253C9, double(__cdecl*)(const char*), GameAtof)(text); }

// sprintf: the compiler's in the standalone build - the same as the game's for integer and string conversions
// (selftests_crt.cpp). Not for floats: the game's C runtime prints those differently (-0 as 0, halves rounded up in
// the last digit, digits past the 17th as zeros, 1.#INF00 / 1.#QNAN0), and no call here formats one.
int LinkedSprintf(char* out, const char* format, ...);
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return GAME_LIBRARY(0x006234FE, int(__cdecl*)(char*, const char*, ...), LinkedSprintf)(out, format, args...);
}

// --- The game's C runtime's behaviour, as ours (used in the standalone build; proven equal by the self-tests) ---
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
    GAME_LIBRARY(0x006242A0, void(__cdecl*)(void*, size_t, size_t, Compare), GameQsort)(base, count, width, compare);
}
inline int Stricmp(const char* a, const char* b)
{
    return GAME_LIBRARY(0x00626EFB, int(__cdecl*)(const char*, const char*), GameStricmp)(a, b);
}
inline void Srand(unsigned seed) { GAME_LIBRARY(0x00623B96, void(__cdecl*)(unsigned), GameSrand)(seed); }
inline int Rand() { return GAME_LIBRARY(0x00623BA3, int(__cdecl*)(), GameRand)(); }
// The addresses x87 code calls (fld value; call [function]).
inline void* AsinX87() { return reinterpret_cast<void*>(GAME_LIBRARY(0x00623560, void(__cdecl*)(), GameAsinX87)); }
inline void* AcosX87() { return reinterpret_cast<void*>(GAME_LIBRARY(0x00623630, void(__cdecl*)(), GameAcosX87)); }

} // namespace Crt
