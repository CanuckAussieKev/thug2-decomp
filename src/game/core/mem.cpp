#include "game/core/mem.h"

#include <cstring>

#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

class Manager;

// The memory manager singleton and its functions (not rewritten yet).
Manager* GetManager() { return GameVariable<Manager*>(0x007D1800); }
bool& SystemReady() { return GameVariable<bool>(0x007CC979); }

void Release(void* memory)
{
    GameFunction<void(__thiscall*)(Manager*, void*)>(0x005BABA0)(GetManager(), memory);
}

void EnsureSystemReady()
{
    if (!SystemReady())
        GameFunction<void(__cdecl*)()>(0x004E1950)();
}

void* Zeroed(void* memory, unsigned int size)
{
    if (memory)
        std::memset(memory, 0, size);
    return memory;
}

} // namespace

void* Allocate(unsigned int size, int mode, Heap* heap)
{
    return GameFunction<void*(__thiscall*)(Manager*, unsigned int, int, Heap*)>(0x005BAFC0)(GetManager(), size,
                                                                                         mode, heap);
}

void* Malloc(unsigned int size) { return GameFunction<void*(__cdecl*)(unsigned int)>(0x005BBAE0)(size); }

void* ReallocateShrink(unsigned int size, void* memory)
{
    return GameFunction<void*(__cdecl*)(unsigned int, void*)>(0x005BBB30)(size, memory);
}

unsigned int MallocSize(void* memory)
{
    return GameFunction<unsigned int(__cdecl*)(void*)>(0x005BBB80)(memory);
}

void FreeMalloc(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x005BBB60)(memory); }

Heap* BottomUpHeap() { return ManagerHeapAt(0x114); }
Heap* TopDownHeap() { return ManagerHeapAt(0x110); }

// 0x00401C90
void* Clear(unsigned int size, void* dest)
{
    if (dest)
        std::memset(dest, 0, size);
    return dest;
}

// 0x004015D0
void* New(unsigned int size)
{
    EnsureSystemReady();
    return Allocate(size, 1, nullptr);
}

// 0x00401000
void Free(void* memory)
{
    Release(memory);
}

// 0x00401CC0
void* Calloc(unsigned int size)
{
    return Zeroed(Allocate(size, 1, nullptr), size);
}

// 0x00401D00
void* Calloc(unsigned int size, int mode)
{
    return Zeroed(Allocate(size, mode, nullptr), size);
}

// 0x00401D40
void* Calloc(unsigned int size, Heap* heap, int mode)
{
    return Zeroed(Allocate(size, mode, heap), size);
}

void PushContext(Heap* heap)
{
    GameFunction<void(__thiscall*)(Manager*, Heap*)>(0x005BB0E0)(GetManager(), heap);
}

void PopContext()
{
    GameFunction<void(__thiscall*)(Manager*)>(0x005BB110)(GetManager());
}

Heap* ManagerHeapAt(unsigned int offset)
{
    return *reinterpret_cast<Heap**>(reinterpret_cast<char*>(GetManager()) + offset);
}

} // namespace Mem

// No self-tests: these need the game's memory manager, which doesn't exist yet when self-tests run.
REPLACE(0x00401000, Mem::Free);
REPLACE(0x004015D0, Mem::New);
REPLACE(0x00401C90, Mem::Clear);
REPLACE(0x00401CC0, static_cast<void* (*)(unsigned int)>(&Mem::Calloc));
REPLACE(0x00401D00, static_cast<void* (*)(unsigned int, int)>(&Mem::Calloc));
REPLACE(0x00401D40, static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc));
