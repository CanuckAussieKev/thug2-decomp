#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "hook/game.h"
#include "hook/hook.h"

// The exe's strings, function addresses and virtual tables, written as source (standalone step 4). Like GAME_GLOBAL
// (game_global.h): in the hooked build each one is still the exe's (the exe's address: identical pointers, the self-tests
// see what they always did), in the standalone build (THUG2_STANDALONE) ours; thug2_check proves the two the same
// (CheckGameCode):
//
//   Dbg::Printf(GAME_TEXT(0x0064C414, "No contact found for splat?\n"));  // the exe's bytes there are this text
//   task->function = GAME_CODE(0x004E5320, &Obj::CFeeler::Callback);     // 0x004E5320's REPLACE line registers it
//   GAME_VTABLE(kRegionVtable, 0x00658A58, &AllocAdapters::RegionDestroy); // slot by slot: each slot's original
//                                                                        // function is REPLACEd by the one given
//
// GAME_TEXT and GAME_CODE are expressions (const char* / void*); GAME_VTABLE defines `void** const name` at namespace
// scope. A slot whose original is library code (the C runtime's _purecall...) is GAME_LIBRARY_SLOT(address) for now
// (step 5 links the library).

// A string literal as a template argument.
template <size_t N>
struct GameTextLiteral {
    char text[N];
    constexpr GameTextLiteral(const char (&s)[N])
    {
        for (size_t i = 0; i < N; ++i)
            text[i] = s[i];
    }
};

// A library function's address as a vtable slot (until the library is linked).
struct GameLibrarySlot {
    uint32_t address;
};
#define GAME_LIBRARY_SLOT(address) (GameLibrarySlot{address})

#ifdef THUG2_STANDALONE

#define GAME_TEXT(address, literal) (static_cast<const char*>(literal))
#define GAME_CODE(address, function) (CodeAddress(function))

struct GameSlot {
    void* code;
    template <typename F>
    GameSlot(F f) : code(CodeAddress(f))
    {
    }
    GameSlot(GameLibrarySlot) : code(nullptr) {} // (step 5)
};
static_assert(sizeof(GameSlot) == sizeof(void*), "a table of GameSlots is a table of code pointers");
#define GAME_VTABLE(name, address, ...)                                                                        \
    static GameSlot name##_slots[] = {__VA_ARGS__};                                                            \
    void** const name = reinterpret_cast<void**>(name##_slots)

#else

void RegisterGameText(uint32_t address, const char* text, uint32_t size, const char* file);
void RegisterGameCode(uint32_t address, void* ours, const char* file);

template <uint32_t Address, GameTextLiteral Text>
struct GameTextAt {
    static inline const bool registered = (RegisterGameText(Address, Text.text, sizeof(Text.text), __FILE__), true);
    static const char* Get()
    {
        (void)registered; // (instantiates the registration)
        return reinterpret_cast<const char*>(static_cast<uintptr_t>(Address));
    }
};
#define GAME_TEXT(address, literal) (GameTextAt<address, GameTextLiteral(literal)>::Get())

template <uint32_t Address, auto Function>
struct GameCodeAt {
    static inline const bool registered = (RegisterGameCode(Address, CodeAddress(Function), __FILE__), true);
    static void* Get()
    {
        (void)registered;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(Address));
    }
};
#define GAME_CODE(address, function) (GameCodeAt<address, function>::Get())

// A slot: our function's code address, or a library function's original address.
struct GameSlot {
    uint32_t code;
    template <typename F>
    GameSlot(F f) : code(reinterpret_cast<uint32_t>(CodeAddress(f)))
    {
    }
    GameSlot(GameLibrarySlot s) : code(s.address | 0x80000000u) {} // (marked: compared with the original's address)
};
struct GameVtableRegistrar {
    GameVtableRegistrar(uint32_t address, const char* name, std::initializer_list<GameSlot> slots, const char* file);
};
#define GAME_VTABLE_CONCAT2(a, b) a##b
#define GAME_VTABLE_CONCAT(a, b) GAME_VTABLE_CONCAT2(a, b)
#define GAME_VTABLE(name, address, ...)                                                                        \
    void** const name = reinterpret_cast<void**>(static_cast<uintptr_t>(address));                            \
    static GameVtableRegistrar GAME_VTABLE_CONCAT(g_vtable_, __LINE__)(address, #name, {__VA_ARGS__}, __FILE__)

// For thug2_check: every GAME_TEXT's text against the image's bytes, every GAME_CODE's function against its address's
// REPLACE line, every GAME_VTABLE slot against the REPLACE line of the original's slot. A line per difference; returns
// how many differ. `checked` counts what was compared.
int CheckGameCode(int* checked);

#endif
