#include "engine/core/arena_alloc.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/checked_index.h"

#include <gsl/assert>

namespace memory {

// --- the heaps' memory and the block headers -----------------------------------------------------------

namespace {
std::span<uint8_t> g_heap_memory;

// How far `to` is from `from`, in bytes.
std::ptrdiff_t byte_distance(const void* from, const void* to)
{
    return static_cast<const uint8_t*>(to) - static_cast<const uint8_t*>(from);
}

// A place in `memory` (its end included), checked.
uint32_t offset_in(std::span<uint8_t> memory, const void* place)
{
    const std::ptrdiff_t offset = byte_distance(memory.data(), place);
    Expects(offset >= 0 && static_cast<std::size_t>(offset) <= memory.size());
    return static_cast<uint32_t>(offset);
}

// The header's 16 bytes at `offset` in the heaps' memory, checked to be inside it.
void* header_place(uint32_t offset)
{
    const std::span<uint8_t> memory = g_heap_memory;
    Expects(static_cast<std::size_t>(offset) + kBlockHeaderSize <= memory.size());
    return &core::at(memory, offset);
}
} // namespace

std::span<uint8_t> heap_memory() { return g_heap_memory; }
void set_heap_memory(std::span<uint8_t> memory) { g_heap_memory = memory; }
uint32_t heap_offset(const void* place) { return offset_in(g_heap_memory, place); }

ChunkPrefix* ChunkPrefix::get_of(void* block)
{
    const uint32_t offset = heap_offset(block);
    Expects(offset >= kBlockHeaderSize);
    return static_cast<ChunkPrefix*>(header_place(offset - kBlockHeaderSize));
}

gsl::owner<ChunkPrefix*> ChunkPrefix::make_at(uint32_t offset)
{
    return memory::construct_in<ChunkPrefix>(header_place(offset));
}

void* ChunkPrefix::gate_off() { return g_heap_memory.subspan(block_offset()).data(); }

// 0x005BA080
void* mark_block_in_use(void* block)
{
    ChunkPrefix::get_of(block)->marker = ChunkPrefix::kBlockInUse;
    return block;
}

// 0x005BA090
ChunkPrefix* fetch_chunk_prefix(void* block) { return ChunkPrefix::get_of(block); }

// 0x005BA0A0
uint32_t get_block_size(void* block) { return ChunkPrefix::get_of(block)->size; }

// 0x005BA0B0
MemSource* free_block(void* block)
{
    ChunkPrefix* header = ChunkPrefix::get_of(block);
    MemSource* allocator = header->allocator;
    if (header->marker != ChunkPrefix::kBlockFreed) {
        allocator->dealloc(header);
        header->marker = ChunkPrefix::kBlockFreed;
    }
    return allocator;
}

// --- regions -------------------------------------------------------------------------------------------

void* Zone::operator new(std::size_t size) noexcept { return alloc_zeroed(static_cast<unsigned int>(size)); }
void Zone::operator delete(void* memory) { memory::dealloc(memory); }

// 0x005BC5F0
Zone::Zone(std::span<uint8_t> memory) : memory_(memory), min_available_(static_cast<int>(memory.size()))
{
    core::at(allocators_, 1) = nullptr;
    core::at(allocators_, 0) = nullptr;
}

// 0x005BC4E0
void Zone::add_allocator(MemSource* allocator)
{
    core::at(allocators_, (allocator->direction_ >> 1) & 1) = allocator;
    allocator->top_side_ = allocator->direction_ == MemSource::kGrowUp ? 0 : byte_size();
}

uint32_t Zone::offset_of(const void* place) const { return offset_in(memory_, place); }

// 0x005BC510
void Zone::remove_allocator(MemSource* allocator) { core::at(allocators_, (allocator->direction_ >> 1) & 1) = nullptr; }

// 0x005BC530
int Zone::free_byte_count()
{
    const uint32_t bottom = core::at(allocators_, 0) ? core::at(allocators_, 0)->top_side_ : 0;
    const uint32_t top = core::at(allocators_, 1) ? core::at(allocators_, 1)->top_side_ : byte_size();
    return static_cast<int>(top - bottom);
}

// 0x005BC560
std::span<uint8_t> Zone::carve(MemSource* allocator, int size, int /*unused*/)
{
    const int side = (allocator->direction_ >> 1) & 1; // 0 growing up, 1 growing down
    const MemSource* other = core::at(allocators_, side ^ 1);
    const int64_t new_top = int64_t{allocator->top_side_} + (int64_t{allocator->direction_} * size);
    int64_t result = new_top;
    if (side) {
        const int64_t limit = other ? other->top_side_ : 0;
        if (new_top < limit)
            return {};
    } else {
        const int64_t limit = other ? other->top_side_ : byte_size();
        result = allocator->top_side_;
        if (new_top > limit)
            return {};
    }
    allocator->top_side_ = static_cast<uint32_t>(new_top);
    const int available = free_byte_count();
    if (available < min_available_)
        min_available_ = available;
    return memory_.subspan(static_cast<std::size_t>(result), static_cast<std::size_t>(size));
}

// 0x005BC620
MemoryArena::MemoryArena(uint32_t size)
{
    if (size) {
        const uint32_t rounded = size & ~0xFu;
        // (New makes sure the memory system is set up)
        memory_ = std::span<uint8_t>(static_cast<uint8_t*>(alloc(rounded)), rounded);
        min_available_ = static_cast<int>(rounded);
        core::at(allocators_, 1) = nullptr;
        core::at(allocators_, 0) = nullptr;
    }
}

// 0x005BC6B0
MemoryArena::~MemoryArena() { memory::dealloc(memory_.data()); }

// --- allocators ----------------------------------------------------------------------------------------

// 0x005BA0E0 (lists::Elem's destructor: out of its list; the other node types' are the same - 0x005BC990,
// 0x005BCD30...)
void ListNode::uncouple()
{
    prev->next = next;
    next->prev = prev;
    next = this;
    prev = this;
}

void ListNode::make_head() { make_node(memory::ListNode::head_marker()); }

void ListNode::make_node(void* item, int item_priority)
{
    data = item;
    priority = item_priority;
    next = this;
    prev = this;
}

ListNode* ListNode::create(void* item, int item_priority)
{
    auto* node = static_cast<ListNode*>(alloc_zeroed(sizeof(ListNode)));
    if (node)
        node->make_node(item, item_priority);
    return node;
}

ListNode* ListNode::create_head() { return create(memory::ListNode::head_marker()); }

// 0x005BA100 / 0x005BA230 (the node's and the head's scalar deleting destructors, flag 1)
void ListNode::destroy(ListNode* node)
{
    if (!node)
        return;
    node->uncouple();
    memory::dealloc(node);
}

void* MemSource::operator new(std::size_t size) noexcept { return alloc_zeroed(static_cast<unsigned int>(size)); }
void MemSource::operator delete(void* memory) { memory::dealloc(memory); }

// 0x005BA260
MemSource::MemSource(Zone* region, int direction, const char* name)
    : region_(region), direction_(direction), label_(name)
{
    used_blocks_ = max_used_blocks_ = free_blocks_ = max_free_blocks_ = 0;
    used_size_ = max_used_size_ = free_size_ = max_free_size_ = 0;
    zone_chain_.make_head();
    base_context_.node.make_node(&base_context_);
    base_context_.free_list_ = nullptr;
    region->add_allocator(this);

    // The base context starts the stack.
    base_context_.node.prev = &zone_chain_;
    base_context_.node.next = zone_chain_.next;
    zone_chain_.next->prev = &base_context_.node;
    zone_chain_.next = &base_context_.node;
    context_ = &base_context_;
}

// 0x005BA140
MemSource::~MemSource()
{
    region_->remove_allocator(this);
    base_context_.node.uncouple();
    zone_chain_.uncouple();
}

// 0x005BA1E0: a new, empty context on top of the stack
void MemSource::enter_heap_scope()
{
    auto* context = static_cast<CtxInfo*>(alloc_zeroed(sizeof(CtxInfo)));
    if (context)
        context->node.make_node(context);
    // (an allocation failure crashes here, as in the original)
    context->free_list_ = nullptr;
    context->node.prev = &zone_chain_;
    context->node.next = zone_chain_.next;
    zone_chain_.next->prev = &context->node;
    zone_chain_.next = &context->node;
    context_ = context;
}

// 0x005BA360: drop the current context; the next one down becomes current
void MemSource::leave_heap_scope()
{
    if (CtxInfo* context = context_) {
        context->node.uncouple();
        memory::dealloc(context);
    }
    const ListNode* next = zone_chain_.next;
    context_ = next->data != memory::ListNode::head_marker() && next ? static_cast<CtxInfo*>(next->data) : nullptr;
}

} // namespace memory
