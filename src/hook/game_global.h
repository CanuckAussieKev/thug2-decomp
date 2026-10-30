#pragma once

#include <cstdint>
#include <cstring>

#include "hook/game.h"

// The game's global variables, by name. One definition each, in the .cpp that owns it; other files declare it with
// GAME_GLOBAL_EXTERN in that subsystem's header:
//
//   GAME_GLOBAL(Mem::Manager*, g_memoryManager, 0x007D1800);              // zero at start (.bss)
//   GAME_GLOBAL(float, kLipBack, 0x006467A8, 3.0f);                       // its start value (.data / .rdata)
//   GAME_GLOBAL_EXTERN(Mem::Manager*, g_memoryManager);                   // in the header
//
// The standalone build (THUG2_STANDALONE) gives each one storage of its own, starting with the value given (or zero).
// The hooked build shares the exe's: the name is a reference to the variable at its address, so the exe's remaining
// code and the self-tests (which set state at the original's addresses) see the same variable as ours. There the start
// value is only checked: thug2_check compares it with the reference build's bytes (CheckGlobalStartValues), so the
// standalone build starts every global as the original does.
//
// GAME_GLOBAL_UNCHECKED: a start value the check can't compare byte for byte (pointers to strings or functions -
// ours aren't at the original's addresses).

#ifdef THUG2_STANDALONE

template <typename T>
using GameGlobal = T;
#define GAME_GLOBAL(type, name, address, ...) GameGlobal<type> name{__VA_ARGS__}
#define GAME_GLOBAL_UNCHECKED(type, name, address, ...) GameGlobal<type> name{__VA_ARGS__}

#else

template <typename T>
using GameGlobal = T&;

struct GlobalStartValue {
    uint32_t address;
    uint32_t size;
    const void* value; // the start value given in the source (null: unchecked)
    const char* name;
    const char* file;
};
void RegisterGlobal(const GlobalStartValue& g);

// (the start value, brace-initialised exactly as the standalone build's variable is)
template <typename T>
struct GlobalStart {
    T value;
};

template <typename T>
struct GlobalRegistrar {
    GlobalRegistrar(uint32_t address, const GlobalStart<T>* start, const char* name, const char* file, bool checked)
    {
        RegisterGlobal({address, static_cast<uint32_t>(sizeof(T)), checked ? &start->value : nullptr, name, file});
    }
};

#define GAME_GLOBAL_CONCAT2(a, b) a##b
#define GAME_GLOBAL_CONCAT(a, b) GAME_GLOBAL_CONCAT2(a, b)
#define GAME_GLOBAL_DEFINE(type, name, address, checked, ...)                                                  \
    GameGlobal<type> name = GameVariable<type>(address);                                                       \
    static const GlobalStart<type> GAME_GLOBAL_CONCAT(g_globalStart_, __LINE__){{__VA_ARGS__}};               \
    static GlobalRegistrar<type> GAME_GLOBAL_CONCAT(g_global_, __LINE__)(                                      \
        address, &GAME_GLOBAL_CONCAT(g_globalStart_, __LINE__), #name, __FILE__, checked)
#define GAME_GLOBAL(type, name, address, ...) GAME_GLOBAL_DEFINE(type, name, address, true, __VA_ARGS__)
#define GAME_GLOBAL_UNCHECKED(type, name, address, ...) GAME_GLOBAL_DEFINE(type, name, address, false, __VA_ARGS__)

// For thug2_check, with the reference build's image just loaded (its data as in the file): every checked global's
// start value compared with the image's bytes, a line per difference. Returns how many differ; `checked` / `total`
// are counted.
int CheckGlobalStartValues(int* checked, int* total);

#endif

#define GAME_GLOBAL_EXTERN(type, name) extern GameGlobal<type> name
