#include "engine/core/mem.h"

#include <cstring>

#include "engine/core/manager.h"

namespace MemSys {

namespace {

bool& SystemReady() { return g_managerReady; }

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

void* Carve(unsigned int size, int mode, Arena* heap)
{
    return Hub::Pull()->Carve(size, mode, heap);
}

Arena* BottomUpHeap() { return Hub::Pull()->m_bottomUpHeap; }
Arena* TopDownHeap() { return Hub::Pull()->m_topDownHeap; }

// 0x00401C90
void* Clear(unsigned int size, void* dest)
{
    if (dest)
        std::memset(dest, 0, size);
    return dest;
}

// 0x004015D0
void* Alloc(unsigned int size)
{
    EnsureSystemReady();
    return Carve(size, 1, nullptr);
}

// 0x00401000
void Dealloc(void* memory)
{
    Hub::Pull()->Dealloc(memory);
}

// 0x00401CC0
void* AllocZeroed(unsigned int size)
{
    return Zeroed(Carve(size, 1, nullptr), size);
}

// 0x00401D00
void* AllocZeroed(unsigned int size, int mode)
{
    return Zeroed(Carve(size, mode, nullptr), size);
}

// 0x00401D40
void* AllocZeroed(unsigned int size, Arena* heap, int mode)
{
    return Zeroed(Carve(size, mode, heap), size);
}

void EnterHeapScope(Arena* heap)
{
    Hub::Pull()->EnterHeapScope(heap);
}

void LeaveHeapScope()
{
    Hub::Pull()->LeaveHeapScope();
}

} // namespace Mem
