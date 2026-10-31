#include <cstdint>
#include <new>

#include "game/core/arena_alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cfuncs_engine.h"
#include "game/core/code_address.h"

namespace MemSys {

namespace {

extern void** const kHeapVtable;

int g_heapBusy;
ChunkPrefix* g_forcedFreeBlock;

// Set while a heap is falling back on others or resizing a block, so a fallback heap doesn't fall back in turn.
int& HeapBusy() { return g_heapBusy; }
// For debugging: when set (never, in the shipped game), Allocate only takes this free block.
ChunkPrefix*& ForcedFreeBlock() { return g_forcedFreeBlock; }

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

uint8_t* EndOf(ChunkPrefix* header)
{
    return reinterpret_cast<uint8_t*>(header) + kBlockHeaderSize + header->size;
}

// The memory manager's heap lookup by name checksum, and the heap dump (the script command, without parameters).
Arena* FindHeap(uint32_t name)
{
    return Hub::Pull()->FetchHeapByKey(name, 0);
}
void DumpHeaps() { ScriptCommands::CFunc_DumpHeaps(nullptr, nullptr); }

} // namespace

// 0x005BA520
Arena::Arena(Zone* region, int direction, const char* name) : MemSource(region, direction, name)
{
    m_useFallbacks = 0;
    m_unknown64 = false;
    m_vtable = kHeapVtable;
    m_unknown68 = -1;
}

// 0x005BA780
void* Arena::Carve(uint32_t size, bool haltIfExhausted)
{
    uint32_t rounded = RoundUp16(size);

    // The smallest free block that fits (the first of equal sizes).
    ChunkPrefix* best = nullptr;
    ChunkPrefix* bestPrevious = nullptr;
    ChunkPrefix* previous = nullptr;
    for (ChunkPrefix* block = m_context->freeList; block; previous = block, block = block->NextFree()) {
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
            auto* tail = reinterpret_cast<ChunkPrefix*>(reinterpret_cast<uint8_t*>(best) + kBlockHeaderSize + rounded);
            best->size = rounded;
            tail->allocator = this;
            tail->size = static_cast<uint32_t>(rest);
            CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
            VirtualFree(tail);
        }
        return best->GateOff();
    }

    // Claim new space from the region.
    auto* header = reinterpret_cast<ChunkPrefix*>(
        m_region->Carve(this, static_cast<int>(kBlockHeaderSize + rounded), m_useFallbacks == 0 && haltIfExhausted));
    if (!header) {
        if (m_useFallbacks && !HeapBusy()) {
            HeapBusy() = 1;
            for (int i = 0; i < 5; ++i) {
                Arena* other = m_fallbacks[i];
                if (other && other->m_region->FreeByteCount() > m_fallbackReserve[i] + static_cast<int>(size)) {
                    if (void* block = other->VirtualAllocate(size, false)) {
                        HeapBusy() = 0;
                        return block;
                    }
                }
            }
            HeapBusy() = 0;
        }
        if (haltIfExhausted) {
            DebugOut::Printf("----------------------------------\n");
            DebugOut::Printf("failed to allocate %d bytes\n", rounded);
            DumpHeaps();
        }
        return nullptr;
    }
    header->allocator = this;
    header->size = rounded;
    CountUp(m_usedBlocks, m_maxUsedBlocks, 1);
    CountUp(m_usedSize, m_maxUsedSize, static_cast<int>(kBlockHeaderSize + rounded));
    DebugTrackBlock(header);
    return header->GateOff();
}

// 0x005BA4F0
uint32_t Arena::LargestFree()
{
    uint32_t largest = 0;
    for (ChunkPrefix* block = m_context->freeList; block; block = block->NextFree()) {
        if (block->size >= largest)
            largest = block->size;
    }
    auto available = static_cast<uint32_t>(m_region->FreeByteCount());
    return available > largest ? available : largest;
}

// 0x005BA9A0
void* Arena::ExtendBlockDownward(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    ChunkPrefix* header = FetchChunkPrefix(block);
    uint32_t rounded = RoundUp16(newSize);
    // The new space comes just below the block (with its own header, which becomes the block's).
    ChunkPrefix* extended = FetchChunkPrefix(VirtualAllocate(rounded - header->size, true));
    DebugTrackBlock(header);
    DebugTrackBlock(extended);
    --m_usedBlocks;
    extended->size = kBlockHeaderSize + rounded;
    DebugTrackBlock(extended);
    HeapBusy() = 0;
    return extended->GateOff();
}

// 0x005BAA20
void* Arena::ExtendBlockUpward(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    ChunkPrefix* header = FetchChunkPrefix(block);
    uint32_t rounded = RoundUp16(newSize);
    if (rounded <= header->size) {
        HeapBusy() = 0;
        return block;
    }
    ChunkPrefix* extra = FetchChunkPrefix(VirtualAllocate(rounded - header->size, true));
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
void* Arena::TrimBlockEnd(uint32_t newSize, void* block)
{
    HeapBusy() = 1;
    ChunkPrefix* header = FetchChunkPrefix(block);
    uint32_t cut = (header->size - newSize) & ~15u;
    if (cut <= kBlockHeaderSize) {
        HeapBusy() = 0;
        return block;
    }
    auto* tail = reinterpret_cast<ChunkPrefix*>(static_cast<uint8_t*>(block) + header->size - cut);
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
void Arena::Dealloc(ChunkPrefix* header)
{
    CountUp(m_freeBlocks, m_maxFreeBlocks, 1);
    CountUp(m_freeSize, m_maxFreeSize, static_cast<int>(header->size));
    --m_usedBlocks;
    m_usedSize -= header->size;
    if (this == FindHeap(0x502D3FD2))
        DebugOut::Printf(" *********************************************************************************** FREE %d\n",
                    header->size);

    ChunkPrefix* before = nullptr;   // the free block before `previous`
    ChunkPrefix* previous = nullptr; // the last free block below this one
    ChunkPrefix* next = m_context->freeList;
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
    if (m_direction == kGrowUp && !next && m_topSide == EndOf(header)) {
        if (before)
            before->NextFree() = nullptr;
        else
            m_context->freeList = nullptr;
        m_topSide -= (header->size + kBlockHeaderSize) * m_direction;
    } else if (m_direction == kGrowDown && !before && m_topSide == reinterpret_cast<uint8_t*>(header)) {
        m_topSide += kBlockHeaderSize + header->size;
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
    MemSys::Arena* MakeNew(MemSys::Zone* region, int direction, const char* name)
    {
        return new (this) MemSys::Arena(region, direction, name);
    }
    // 0x005BA560 (scalar deleting destructor; the heap adds nothing to the allocator's destructor)
    MemSys::Arena* Teardown(unsigned int flags)
    {
        auto* heap = reinterpret_cast<MemSys::Arena*>(this);
        heap->DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(heap);
        return heap;
    }
};

} // namespace

namespace MemSys {
namespace {
static VtableSlot kHeapVtable_slots[] = {&MemSys::MemSource::EnterHeapScope, &MemSys::MemSource::LeaveHeapScope,
                &HeapAdapters::Teardown, &MemSys::Arena::Carve, &MemSys::Arena::LargestFree, &MemSys::Arena::ExtendBlockDownward,
                &MemSys::Arena::ExtendBlockUpward, &MemSys::Arena::TrimBlockEnd, &MemSys::Arena::Dealloc};
void** const kHeapVtable = reinterpret_cast<void**>(kHeapVtable_slots);
} // namespace
} // namespace Mem
