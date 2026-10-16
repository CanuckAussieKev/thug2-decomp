#include <cstdint>
#include <new>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

void** const kHeapVtable = reinterpret_cast<void**>(0x0065848C);

// Set while a heap is falling back on others or resizing a block, so a fallback heap doesn't fall back in turn.
int& HeapBusy() { return GameVariable<int>(0x007D0FDC); }
// For debugging: when set (never, in the shipped game), Allocate only takes this free block.
BlockHeader*& ForcedFreeBlock() { return GameVariable<BlockHeader*>(0x007D0FE0); }

void CountUp(int& value, int& max, int amount)
{
    value += amount;
    if (value > max)
        max = value;
}

uint32_t RoundUp16(uint32_t size)
{
    return (size + 15) & ~15u;
}

uint8_t* EndOf(BlockHeader* header)
{
    return reinterpret_cast<uint8_t*>(header) + kBlockHeaderSize + header->size;
}

// The memory manager's heap lookup by name checksum (not rewritten yet), and the heap dump.
Heap* FindHeap(uint32_t name)
{
    return GameFunction<Heap*(__thiscall*)(void*, uint32_t, int)>(0x005BAF50)(GameVariable<void*>(0x007D1800), name, 0);
}
void DumpHeaps() { GameFunction<bool(__cdecl*)(void*, void*)>(0x00448840)(nullptr, nullptr); }

} // namespace

// 0x005BA520
Heap::Heap(Region* region, int direction, const char* name) : Allocator(region, direction, name)
{
    m_useFallbacks = 0;
    m_unknown64 = false;
    m_vtable = kHeapVtable;
    m_unknown68 = -1;
}

// 0x005BA780
void* Heap::Allocate(uint32_t size, bool assertOnFail)
{
    uint32_t rounded = RoundUp16(size);

    // The smallest free block that fits (the first of equal sizes).
    BlockHeader* best = nullptr;
    BlockHeader* bestPrevious = nullptr;
    BlockHeader* previous = nullptr;
    for (BlockHeader* block = m_context->freeList; block; previous = block, block = block->NextFree()) {
        if (block->size < rounded)
            continue;
        bool take = ForcedFreeBlock() ? block == ForcedFreeBlock() : !best || block->size < best->size;
        if (take) {
            best = block;
            bestPrevious = previous;
        }
    }
    if (best) {
        if (bestPrevious)
            bestPrevious->NextFree() = best->NextFree();
        else
            m_context->freeList = best->NextFree();
        best->allocator = this;
        --m_freeBlocks;
        CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
        m_freeSize -= best->size;
        CountUp(m_usedSize, m_maxUsedSize, best->size);

        // Split off what isn't needed, as a free block of its own.
        int rest = static_cast<int>(best->size - kBlockHeaderSize - rounded);
        if (rest > 0) {
            auto* tail = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(best) + kBlockHeaderSize + rounded);
            best->size = rounded;
            tail->allocator = this;
            tail->size = static_cast<uint32_t>(rest);
            CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
            VirtualFree(tail);
        }
        return best->Block();
    }

    // Claim new space from the region.
    auto* header = reinterpret_cast<BlockHeader*>(
        m_region->Allocate(this, static_cast<int>(kBlockHeaderSize + rounded), m_useFallbacks == 0 && assertOnFail));
    if (!header) {
        if (m_useFallbacks && !HeapBusy()) {
            HeapBusy() = 1;
            for (int i = 0; i < 5; ++i) {
                Heap* other = m_fallbacks[i];
                if (other && other->m_region->MemAvailable() > m_fallbackReserve[i] + static_cast<int>(size)) {
                    if (void* block = other->VirtualAllocate(size, false)) {
                        HeapBusy() = 0;
                        return block;
                    }
                }
            }
            HeapBusy() = 0;
        }
        if (assertOnFail) {
            Dbg::Printf("----------------------------------\n");
            Dbg::Printf("failed to allocate %d bytes\n", rounded);
            DumpHeaps();
        }
        return nullptr;
    }
    header->allocator = this;
    header->size = rounded;
    CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
    CountUp(m_usedSize, m_maxUsedSize, static_cast<int>(kBlockHeaderSize + rounded));
    DebugTrackBlock(header);
    return header->Block();
}

// 0x005BA4F0
uint32_t Heap::LargestFree()
{
    uint32_t largest = 0;
    for (BlockHeader* block = m_context->freeList; block; block = block->NextFree()) {
        if (block->size >= largest)
            largest = block->size;
    }
    auto available = static_cast<uint32_t>(m_region->MemAvailable());
    return available > largest ? available : largest;
}

// 0x005BA9A0
void* Heap::ReallocateDown(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    BlockHeader* header = GetBlockHeader(block);
    uint32_t rounded = RoundUp16(newSize);
    // The new space comes just below the block (with its own header, which becomes the block's).
    BlockHeader* extended = GetBlockHeader(VirtualAllocate(rounded - header->size, true));
    DebugTrackBlock(header);
    DebugTrackBlock(extended);
    --m_usedBlocks;
    extended->size = kBlockHeaderSize + rounded;
    DebugTrackBlock(extended);
    HeapBusy() = 0;
    return extended->Block();
}

// 0x005BAA20
void* Heap::ReallocateUp(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    BlockHeader* header = GetBlockHeader(block);
    uint32_t rounded = RoundUp16(newSize);
    if (rounded <= header->size) {
        HeapBusy() = 0;
        return block;
    }
    BlockHeader* extra = GetBlockHeader(VirtualAllocate(rounded - header->size, true));
    if (static_cast<uint8_t*>(block) + header->size != reinterpret_cast<uint8_t*>(extra)) {
        // Not right after the block: give it back.
        HeapBusy() = 0;
        VirtualFree(extra);
        return nullptr;
    }
    DebugTrackBlock(header);
    DebugTrackBlock(extra);
    uint32_t extraSize = extra->size;
    --m_usedBlocks;
    header->size += kBlockHeaderSize + extraSize;
    DebugTrackBlock(header);
    HeapBusy() = 0;
    return block;
}

// 0x005BAAE0
void* Heap::ReallocateShrink(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    BlockHeader* header = GetBlockHeader(block);
    uint32_t cut = (header->size - newSize) & ~15u;
    if (cut <= kBlockHeaderSize) {
        HeapBusy() = 0;
        return block;
    }
    auto* tail = reinterpret_cast<BlockHeader*>(static_cast<uint8_t*>(block) + header->size - cut);
    tail->size = cut - kBlockHeaderSize;
    tail->allocator = this;
    DebugTrackBlock(tail);
    CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
    VirtualFree(tail);
    DebugTrackBlock(header);
    header->size -= cut;
    DebugTrackBlock(header);
    HeapBusy() = 0;
    return block;
}

// 0x005BA5A0: onto the free list (in address order), merged with free neighbours; a free block at the heap's
// top goes back to the region
void Heap::Free(BlockHeader* header)
{
    CountUp(m_freeBlocks, m_maxFreeBlocks, 1);
    CountUp(m_freeSize, m_maxFreeSize, static_cast<int>(header->size));
    --m_usedBlocks;
    m_usedSize -= header->size;
    if (this == FindHeap(0x502D3FD2))
        Dbg::Printf(" *********************************************************************************** FREE %d\n",
                    header->size);

    BlockHeader* before = nullptr;   // the free block before `previous`
    BlockHeader* previous = nullptr; // the last free block below this one
    BlockHeader* next = m_context->freeList;
    while (next && next < header) {
        before = previous;
        previous = next;
        next = next->NextFree();
    }

    header->NextFree() = next;
    if (next && EndOf(header) == reinterpret_cast<uint8_t*>(next)) {
        header->NextFree() = next->NextFree();
        header->size += next->size + kBlockHeaderSize;
        --m_freeBlocks;
        CountUp(m_freeSize, m_maxFreeSize, kBlockHeaderSize);
        m_usedSize -= kBlockHeaderSize;
    }
    // From here `before` is the free block before `header` in the list.
    if (!previous) {
        m_context->freeList = header;
        before = nullptr;
    } else if (EndOf(previous) == reinterpret_cast<uint8_t*>(header)) {
        previous->size += header->size + kBlockHeaderSize;
        previous->NextFree() = header->NextFree();
        --m_freeBlocks;
        CountUp(m_freeSize, m_maxFreeSize, kBlockHeaderSize);
        m_usedSize -= kBlockHeaderSize;
        header = previous;
    } else {
        previous->NextFree() = header;
        before = previous;
    }

    // At the heap's top? Then it goes back to the region.
    if (m_direction == kGrowUp && !next && m_top == EndOf(header)) {
        if (before)
            before->NextFree() = nullptr;
        else
            m_context->freeList = nullptr;
        m_top -= (header->size + kBlockHeaderSize) * m_direction;
    } else if (m_direction == kGrowDown && !before && m_top == reinterpret_cast<uint8_t*>(header)) {
        m_top += kBlockHeaderSize + header->size;
        m_context->freeList = header->NextFree();
    } else {
        return;
    }
    --m_freeBlocks;
    m_freeSize -= header->size;
    m_usedSize -= kBlockHeaderSize;
}

} // namespace Mem

namespace {

struct HeapAdapters {
    Mem::Heap* Construct(Mem::Region* region, int direction, const char* name)
    {
        return new (this) Mem::Heap(region, direction, name);
    }
    // 0x005BA560 (scalar deleting destructor; the heap adds nothing to the allocator's destructor)
    Mem::Heap* Destroy(unsigned int flags)
    {
        auto* heap = reinterpret_cast<Mem::Heap*>(this);
        heap->DestroyBody();
        if (flags & 1)
            Mem::Free(heap);
        return heap;
    }
};

} // namespace

REPLACE(0x005BA520, &HeapAdapters::Construct);
REPLACE(0x005BA560, &HeapAdapters::Destroy);
REPLACE(0x005BA780, &Mem::Heap::Allocate);
REPLACE(0x005BA4F0, &Mem::Heap::LargestFree);
REPLACE(0x005BA9A0, &Mem::Heap::ReallocateDown);
REPLACE(0x005BAA20, &Mem::Heap::ReallocateUp);
REPLACE(0x005BAAE0, &Mem::Heap::ReallocateShrink);
REPLACE(0x005BA5A0, &Mem::Heap::Free);
