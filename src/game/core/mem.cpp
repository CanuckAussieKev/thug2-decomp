#include "game/core/mem.h"

#include <cstring>

#include "game/core/manager.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

bool& SystemReady() { return GameVariable<bool>(0x007CC979); }

void EnsureSystemReady()
{
    if (!SystemReady())
        EnsureManager();
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
    return Manager::Get()->Allocate(size, mode, heap);
}

Heap* BottomUpHeap() { return Manager::Get()->m_bottomUpHeap; }
Heap* TopDownHeap() { return Manager::Get()->m_topDownHeap; }

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
    Manager::Get()->Free(memory);
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
    Manager::Get()->PushContext(heap);
}

void PopContext()
{
    Manager::Get()->PopContext();
}

} // namespace Mem

// No self-tests: these need the game's memory manager, which doesn't exist yet when self-tests run.
REPLACE(0x00401000, Mem::Free);
REPLACE(0x004015D0, Mem::New);
REPLACE(0x00401C90, Mem::Clear);
REPLACE(0x00401CC0, static_cast<void* (*)(unsigned int)>(&Mem::Calloc));
REPLACE(0x00401D00, static_cast<void* (*)(unsigned int, int)>(&Mem::Calloc));
REPLACE(0x00401D40, static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc));
