#include <cstdint>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cfuncs_engine.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace memory {

namespace {

int g_heap_busy;
ChunkPrefix* g_forced_free_block;

// Set while a heap is falling back on others or resizing a block, so a fallback heap doesn't fall back in turn.
int& heap_busy() { return g_heap_busy; }
// For debugging: when set (never, in the shipped game), Allocate only takes this free block.
ChunkPrefix*& forced_free_block() { return g_forced_free_block; }

void count_up(int& value, int& max, int amount)
{
    value += amount;
    if (value > max)
        max = value;
}

uint32_t round_up16(uint32_t size) { return (size + 15) & ~15u; }

uint8_t* end_of(ChunkPrefix* header) { return raw::bytes(header) + kBlockHeaderSize + header->size; }

// The memory manager's heap lookup by name checksum, and the heap dump (the script command, without parameters).
Arena* find_heap(uint32_t name) { return Hub::get_pull()->fetch_heap_by_key(name, 0); }
void dump_heaps() { commands::cfunc_dump_heaps(nullptr, nullptr); }

} // namespace

// 0x005BA520
Arena::Arena(Zone* region, int direction, const char* name) : MemSource(region, direction, name) {}

// 0x005BA780
void* Arena::carve(uint32_t size, bool halt_if_exhausted)
{
    const uint32_t rounded = round_up16(size);

    // The smallest free block that fits (the first of equal sizes).
    ChunkPrefix* best = nullptr;
    ChunkPrefix* best_previous = nullptr;
    ChunkPrefix* previous = nullptr;
    for (ChunkPrefix* block = context_->free_list_; block; previous = block, block = block->next_free()) {
        if (block->size < rounded)
            continue;
        const bool take = forced_free_block() ? block == forced_free_block() : !best || block->size < best->size;
        if (take) {
            best = block;
            best_previous = previous;
        }
    }
    if (best) {
        if (best_previous)
            best_previous->next_free() = best->next_free();
        else
            context_->free_list_ = best->next_free();
        best->allocator = this;
        --free_blocks_;
        count_up(used_blocks_, max_used_blocks_, 1);
        free_size_ -= static_cast<int>(best->size);
        count_up(used_size_, max_used_size_, static_cast<int>(best->size));

        // Split off what isn't needed, as a free block of its own.
        const int rest = static_cast<int>(best->size - kBlockHeaderSize - rounded);
        if (rest > 0) {
            auto* tail = raw::field_at<ChunkPrefix>(best, kBlockHeaderSize + rounded);
            best->size = rounded;
            tail->allocator = this;
            tail->size = static_cast<uint32_t>(rest);
            count_up(used_blocks_, max_used_blocks_, 1);
            dealloc(tail);
        }
        return best->gate_off();
    }

    // Claim new space from the region.
    auto* header = raw::as<ChunkPrefix>(region_->carve(this, static_cast<int>(kBlockHeaderSize + rounded), use_fallbacks_ == 0 && halt_if_exhausted));
    if (!header) {
        if (use_fallbacks_ && !heap_busy()) {
            heap_busy() = 1;
            for (int i = 0; i < 5; ++i) {
                Arena* other = core::at(fallbacks_, i);
                if (other && other->region_->free_byte_count() > core::at(fallback_reserve_, i) + static_cast<int>(size)) {
                    if (void* block = other->carve(size, false)) {
                        heap_busy() = 0;
                        return block;
                    }
                }
            }
            heap_busy() = 0;
        }
        if (halt_if_exhausted) {
            debug::printf("----------------------------------\n");
            debug::printf("failed to allocate %d bytes\n", rounded);
            dump_heaps();
        }
        return nullptr;
    }
    header->allocator = this;
    header->size = rounded;
    count_up(used_blocks_, max_used_blocks_, 1);
    count_up(used_size_, max_used_size_, static_cast<int>(kBlockHeaderSize + rounded));
    debug_track_block(header);
    return header->gate_off();
}

// 0x005BA4F0
uint32_t Arena::largest_free()
{
    uint32_t largest = 0;
    for (ChunkPrefix* block = context_->free_list_; block; block = block->next_free()) {
        if (block->size >= largest)
            largest = block->size;
    }
    auto available = static_cast<uint32_t>(region_->free_byte_count());
    return available > largest ? available : largest;
}

// 0x005BA9A0
void* Arena::extend_block_downward(uint32_t new_size, void* block)
{
    heap_busy() = 1;
    ChunkPrefix* header = fetch_chunk_prefix(block);
    const uint32_t rounded = round_up16(new_size);
    // The new space comes just below the block (with its own header, which becomes the block's).
    ChunkPrefix* extended = fetch_chunk_prefix(carve(rounded - header->size, true));
    debug_track_block(header);
    debug_track_block(extended);
    --used_blocks_;
    extended->size = kBlockHeaderSize + rounded;
    debug_track_block(extended);
    heap_busy() = 0;
    return extended->gate_off();
}

// 0x005BAA20
void* Arena::extend_block_upward(uint32_t new_size, void* block)
{
    heap_busy() = 1;
    ChunkPrefix* header = fetch_chunk_prefix(block);
    const uint32_t rounded = round_up16(new_size);
    if (rounded <= header->size) {
        heap_busy() = 0;
        return block;
    }
    ChunkPrefix* extra = fetch_chunk_prefix(carve(rounded - header->size, true));
    if (static_cast<uint8_t*>(block) + header->size != raw::bytes(extra)) {
        // Not right after the block: give it back.
        heap_busy() = 0;
        dealloc(extra);
        return nullptr;
    }
    debug_track_block(header);
    debug_track_block(extra);
    const uint32_t extra_size = extra->size;
    --used_blocks_;
    header->size += kBlockHeaderSize + extra_size;
    debug_track_block(header);
    heap_busy() = 0;
    return block;
}

// 0x005BAAE0
void* Arena::trim_block_end(uint32_t new_size, void* block)
{
    heap_busy() = 1;
    ChunkPrefix* header = fetch_chunk_prefix(block);
    const uint32_t cut = (header->size - new_size) & ~15u;
    if (cut <= kBlockHeaderSize) {
        heap_busy() = 0;
        return block;
    }
    auto* tail = raw::field_at<ChunkPrefix>(static_cast<uint8_t*>(block), header->size - cut);
    tail->size = cut - kBlockHeaderSize;
    tail->allocator = this;
    debug_track_block(tail);
    count_up(used_blocks_, max_used_blocks_, 1);
    dealloc(tail);
    debug_track_block(header);
    header->size -= cut;
    debug_track_block(header);
    heap_busy() = 0;
    return block;
}

// 0x005BA5A0: onto the free list (in address order), merged with free neighbours; a free block at the heap's
// top goes back to the region
void Arena::dealloc(ChunkPrefix* header)
{
    count_up(free_blocks_, max_free_blocks_, 1);
    count_up(free_size_, max_free_size_, static_cast<int>(header->size));
    --used_blocks_;
    used_size_ -= static_cast<int>(header->size);
    if (this == find_heap(0x502D3FD2))
        debug::printf(" *********************************************************************************** FREE %d\n",
                      header->size);

    ChunkPrefix* before = nullptr;   // the free block before `previous`
    ChunkPrefix* previous = nullptr; // the last free block below this one
    ChunkPrefix* next = context_->free_list_;
    while (next && next < header) {
        before = previous;
        previous = next;
        next = next->next_free();
    }

    header->next_free() = next;
    if (next && end_of(header) == raw::bytes(next)) {
        header->next_free() = next->next_free();
        header->size += next->size + kBlockHeaderSize;
        --free_blocks_;
        count_up(free_size_, max_free_size_, kBlockHeaderSize);
        used_size_ -= kBlockHeaderSize;
    }
    // From here `before` is the free block before `header` in the list.
    if (!previous) {
        context_->free_list_ = header;
        before = nullptr;
    } else if (end_of(previous) == raw::bytes(header)) {
        previous->size += header->size + kBlockHeaderSize;
        previous->next_free() = header->next_free();
        --free_blocks_;
        count_up(free_size_, max_free_size_, kBlockHeaderSize);
        used_size_ -= kBlockHeaderSize;
        header = previous;
    } else {
        previous->next_free() = header;
        before = previous;
    }

    // At the heap's top? Then it goes back to the region.
    if (direction_ == kGrowUp && !next && top_side_ == end_of(header)) {
        if (before)
            before->next_free() = nullptr;
        else
            context_->free_list_ = nullptr;
        top_side_ -= (header->size + kBlockHeaderSize) * direction_;
    } else if (direction_ == kGrowDown && !before && top_side_ == raw::bytes(header)) {
        top_side_ += kBlockHeaderSize + header->size;
        context_->free_list_ = header->next_free();
    } else {
        return;
    }
    --free_blocks_;
    free_size_ -= static_cast<int>(header->size);
    used_size_ -= kBlockHeaderSize;
}

} // namespace memory
