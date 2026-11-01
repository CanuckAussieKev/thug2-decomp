#include "engine/core/arena_alloc.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"

namespace memory {

// --- block headers -------------------------------------------------------------------------------------

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
Zone::Zone(uint8_t* start, uint8_t* end) : outset_(start), min_available_(static_cast<int>(end - start)), finish_(end)
{
    allocators_[1] = nullptr;
    allocators_[0] = nullptr;
}

// 0x005BC4E0
void Zone::add_allocator(MemSource* allocator)
{
    allocators_[(allocator->direction_ >> 1) & 1] = allocator;
    allocator->top_side_ = allocator->direction_ == MemSource::kGrowUp ? outset_ : finish_;
}

// 0x005BC510
void Zone::remove_allocator(MemSource* allocator) { allocators_[(allocator->direction_ >> 1) & 1] = nullptr; }

// 0x005BC530
int Zone::free_byte_count()
{
    const uint8_t* bottom = allocators_[0] ? allocators_[0]->top_side_ : outset_;
    const uint8_t* top = allocators_[1] ? allocators_[1]->top_side_ : finish_;
    return static_cast<int>(top - bottom);
}

// 0x005BC560
uint8_t* Zone::carve(MemSource* allocator, int size, int /*unused*/)
{
    const int side = (allocator->direction_ >> 1) & 1; // 0 growing up, 1 growing down
    const MemSource* other = allocators_[side ^ 1];
    uint8_t* new_top = allocator->top_side_ + allocator->direction_ * size;
    uint8_t* result = new_top;
    if (side) {
        const uint8_t* limit = other ? other->top_side_ : outset_;
        if (new_top < limit)
            return nullptr;
    } else {
        const uint8_t* limit = other ? other->top_side_ : finish_;
        result = allocator->top_side_;
        if (new_top > limit)
            return nullptr;
    }
    allocator->top_side_ = new_top;
    const int available = free_byte_count();
    if (available < min_available_)
        min_available_ = available;
    return result;
}

// 0x005BC620
MemoryArena::MemoryArena(uint32_t size)
{
    if (size) {
        const uint32_t rounded = size & ~0xFu;
        auto* start = static_cast<uint8_t*>(alloc(rounded)); // (New makes sure the memory system is set up)
        finish_ = start + rounded;
        outset_ = start;
        min_available_ = static_cast<int>(finish_ - start);
        allocators_[1] = nullptr;
        allocators_[0] = nullptr;
    }
}

// 0x005BC6B0
MemoryArena::~MemoryArena() { memory::dealloc(outset_); }

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

void ListNode::make_head() { make_node(reinterpret_cast<void*>(-1)); }

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

ListNode* ListNode::create_head() { return create(reinterpret_cast<void*>(-1)); }

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
    context_ = next->data != reinterpret_cast<void*>(-1) && next ? static_cast<CtxInfo*>(next->data) : nullptr;
}

} // namespace memory
