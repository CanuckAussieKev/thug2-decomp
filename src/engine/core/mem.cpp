#include "engine/core/mem.h"

#include <cstring>

#include "engine/core/manager.h"

namespace memory {

namespace {

bool& system_ready() { return manager_ready_flag(); }

void ensure_system_ready()
{
    if (!system_ready())
        ensure_manager();
}

void* zeroed(void* memory, unsigned int size)
{
    if (memory)
        std::memset(memory, 0, size);
    return memory;
}

} // namespace

void* carve(unsigned int size, int mode, Arena* heap) { return Hub::get_pull()->carve(size, mode, heap); }

Arena* bottom_up_heap() { return Hub::get_pull()->bottom_up_heap_; }
Arena* top_down_heap() { return Hub::get_pull()->top_down_heap_; }

// 0x00401C90
void* get_clear(unsigned int size, void* dest)
{
    if (dest)
        std::memset(dest, 0, size);
    return dest;
}

// 0x004015D0
void* alloc(unsigned int size)
{
    ensure_system_ready();
    return carve(size, 1, nullptr);
}

// 0x00401000
void dealloc(void* memory) { Hub::get_pull()->dealloc(memory); }

// 0x00401CC0
void* alloc_zeroed(unsigned int size) { return zeroed(carve(size, 1, nullptr), size); }

// 0x00401D00
void* alloc_zeroed(unsigned int size, int mode) { return zeroed(carve(size, mode, nullptr), size); }

// 0x00401D40
void* alloc_zeroed(unsigned int size, Arena* heap, int mode) { return zeroed(carve(size, mode, heap), size); }

void enter_heap_scope(Arena* heap) { Hub::get_pull()->enter_heap_scope(heap); }

void leave_heap_scope() { Hub::get_pull()->leave_heap_scope(); }

} // namespace memory
