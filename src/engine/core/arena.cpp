#include <cstdint>
#include <new>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cfuncs_engine.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace memory {

namespace {
constexpr int kAlign16 = 15;                  // rounding a size up to 16 bytes: add this, then mask it off
constexpr unsigned kAlign16MaskUnsigned = 15u;
constexpr uint32_t kTeammatePro = 0x502D3FD2; // TeammatePro (the heap whose frees the debug output reports)

// Set while a heap is falling back on others or resizing a block, so a fallback heap doesn't fall back in turn.
int& heap_busy()
{
    static int heap_busy{};
    return heap_busy;
}
// For debugging: when set (never, in the shipped game), Allocate only takes this free block.
ChunkPrefix*& forced_free_block()
{
    static ChunkPrefix* forced_free_block{};
    return forced_free_block;
}

void count_up(int& value, int& max, int amount)
{
    value += amount;
    if (value > max)
        max = value;
}

uint32_t round_up16(uint32_t size) { return (size + kAlign16) & ~kAlign16MaskUnsigned; }

// Where a block of the heap's region ends, in the region (the allocator's top is an offset there).
uint32_t end_in_region(const Zone& region, ChunkPrefix* header) { return region.offset_of(header->gate_off()) + header->size(); }
// A block's header made in the memory at `place`.
gsl::owner<ChunkPrefix*> header_at(uint8_t* place) { return memory::construct_in<ChunkPrefix>(place); }

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
    for (ChunkPrefix* block = context()->free_list_; block; previous = block, block = block->next_free()) {
        if (block->size() < rounded)
            continue;
        const bool take = forced_free_block() ? block == forced_free_block() : !best || block->size() < best->size();
        if (take) {
            best = block;
            best_previous = previous;
        }
    }
    if (best) {
        if (best_previous)
            best_previous->set_next_free(best->next_free());
        else
            context()->free_list_ = best->next_free();
        best->set_allocator(this);
        --free_blocks();
        count_up(used_blocks(), max_used_blocks(), 1);
        free_size() -= static_cast<int>(best->size());
        count_up(used_size(), max_used_size(), static_cast<int>(best->size()));

        // Split off what isn't needed, as a free block of its own.
        const int rest = static_cast<int>(best->size() - kBlockHeaderSize - rounded);
        if (rest > 0) {
            gsl::owner<ChunkPrefix*> tail = ChunkPrefix::make_at(best->block_offset() + rounded);
            best->size() = rounded;
            tail->set_allocator(this);
            tail->size() = static_cast<uint32_t>(rest);
            count_up(used_blocks(), max_used_blocks(), 1);
            dealloc(tail);
        }
        return best->gate_off();
    }

    // Claim new space from the region.
    const std::span<uint8_t> space = region()->carve(this, static_cast<int>(kBlockHeaderSize + rounded), use_fallbacks_ == 0 && halt_if_exhausted);
    ChunkPrefix* const header = space.empty() ? nullptr : header_at(space.data());
    if (!header) {
        if (use_fallbacks_ && !heap_busy()) {
            heap_busy() = 1;
            for (int i = 0; i < kFallbackCount; ++i) {
                Arena* other = core::at(fallbacks_, i);
                if (other && other->region()->free_byte_count() > core::at(fallback_reserve_, i) + static_cast<int>(size)) {
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
    header->set_allocator(this);
    header->size() = rounded;
    count_up(used_blocks(), max_used_blocks(), 1);
    count_up(used_size(), max_used_size(), static_cast<int>(kBlockHeaderSize + rounded));
    debug_track_block(header);
    return header->gate_off();
}

// 0x005BA4F0
uint32_t Arena::largest_free()
{
    uint32_t largest = 0;
    for (ChunkPrefix* block = context()->free_list_; block; block = block->next_free()) {
        if (block->size() >= largest)
            largest = block->size();
    }
    auto available = static_cast<uint32_t>(region()->free_byte_count());
    return available > largest ? available : largest;
}

// 0x005BA9A0
void* Arena::extend_block_downward(uint32_t new_size, void* block)
{
    heap_busy() = 1;
    ChunkPrefix* header = fetch_chunk_prefix(block);
    const uint32_t rounded = round_up16(new_size);
    // The new space comes just below the block (with its own header, which becomes the block's).
    ChunkPrefix* extended = fetch_chunk_prefix(carve(rounded - header->size(), true));
    debug_track_block(header);
    debug_track_block(extended);
    --used_blocks();
    extended->size() = kBlockHeaderSize + rounded;
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
    if (rounded <= header->size()) {
        heap_busy() = 0;
        return block;
    }
    ChunkPrefix* extra = fetch_chunk_prefix(carve(rounded - header->size(), true));
    if (!header->is_followed_by(extra)) {
        // Not right after the block: give it back.
        heap_busy() = 0;
        dealloc(extra);
        return nullptr;
    }
    debug_track_block(header);
    debug_track_block(extra);
    const uint32_t extra_size = extra->size();
    --used_blocks();
    header->size() += kBlockHeaderSize + extra_size;
    debug_track_block(header);
    heap_busy() = 0;
    return block;
}

// 0x005BAAE0
void* Arena::trim_block_end(uint32_t new_size, void* block)
{
    heap_busy() = 1;
    ChunkPrefix* header = fetch_chunk_prefix(block);
    const uint32_t cut = (header->size() - new_size) & ~15u;
    if (cut <= kBlockHeaderSize) {
        heap_busy() = 0;
        return block;
    }
    gsl::owner<ChunkPrefix*> tail = ChunkPrefix::make_at(header->block_offset() + (header->size() - cut));
    tail->size() = cut - kBlockHeaderSize;
    tail->set_allocator(this);
    debug_track_block(tail);
    count_up(used_blocks(), max_used_blocks(), 1);
    dealloc(tail);
    debug_track_block(header);
    header->size() -= cut;
    debug_track_block(header);
    heap_busy() = 0;
    return block;
}

// 0x005BA5A0: onto the free list (in address order), merged with free neighbours; a free block at the heap's
// top goes back to the region
void Arena::dealloc(ChunkPrefix* header)
{
    count_up(free_blocks(), max_free_blocks(), 1);
    count_up(free_size(), max_free_size(), static_cast<int>(header->size()));
    --used_blocks();
    used_size() -= static_cast<int>(header->size());
    if (this == find_heap(kTeammatePro))
        debug::printf(" *********************************************************************************** FREE %d\n",
                      header->size());

    ChunkPrefix* before = nullptr;   // the free block before `previous`
    ChunkPrefix* previous = nullptr; // the last free block below this one
    ChunkPrefix* next = context()->free_list_;
    while (next && next < header) {
        before = previous;
        previous = next;
        next = next->next_free();
    }

    header->set_next_free(next);
    if (next && header->is_followed_by(next)) {
        header->set_next_free(next->next_free());
        header->size() += next->size() + kBlockHeaderSize;
        --free_blocks();
        count_up(free_size(), max_free_size(), kBlockHeaderSize);
        used_size() -= kBlockHeaderSize;
    }
    // From here `before` is the free block before `header` in the list.
    if (!previous) {
        context()->free_list_ = header;
        before = nullptr;
    } else if (previous->is_followed_by(header)) {
        previous->size() += header->size() + kBlockHeaderSize;
        previous->set_next_free(header->next_free());
        --free_blocks();
        count_up(free_size(), max_free_size(), kBlockHeaderSize);
        used_size() -= kBlockHeaderSize;
        header = previous;
    } else {
        previous->set_next_free(header);
        before = previous;
    }

    // At the heap's top? Then it goes back to the region.
    if (direction() == kGrowUp && !next && top_side() == end_in_region(*region(), header)) {
        if (before)
            before->set_next_free(nullptr);
        else
            context()->free_list_ = nullptr;
        top_side() -= header->size() + kBlockHeaderSize; // (growing up: the original's size times the direction)
    } else if (direction() == kGrowDown && !before && top_side() == region()->offset_of(header)) {
        top_side() += kBlockHeaderSize + header->size();
        context()->free_list_ = header->next_free();
    } else {
        return;
    }
    --free_blocks();
    free_size() -= static_cast<int>(header->size());
    used_size() -= kBlockHeaderSize;
}

} // namespace memory
