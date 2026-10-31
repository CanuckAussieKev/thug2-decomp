#include "engine/core/arena_alloc.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/code_address.h"
#include "engine/core/game_vtables.h"
#include "game/save_cards/save_types.h"
#include "engine/core/stubs.h"

namespace memory {

// The virtual tables of the list node, the region and the allocated region (defined after their functions, below).
extern void** const kNodeVtable;
extern void** const kRegionVtable;
extern void** const kAllocRegionVtable;

namespace {

extern void** const kHeadVtable; // lists::Foremost<Context>
extern void** const kAllocatorVtable;

} // namespace

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
        // The allocator's free (virtual slot 8).
        reinterpret_cast<void(__thiscall*)(MemSource*, ChunkPrefix*)>(allocator->vtable_[8])(allocator, header);
        header->marker = ChunkPrefix::kBlockFreed;
    }
    return allocator;
}

// --- regions -------------------------------------------------------------------------------------------

// 0x005BC5F0
Zone::Zone(uint8_t* start, uint8_t* end)
{
    finish_ = end;
    min_available_ = static_cast<int>(end - start);
    vtable_ = kRegionVtable;
    outset_ = start;
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
    uint8_t* bottom = allocators_[0] ? allocators_[0]->top_side_ : outset_;
    uint8_t* top = allocators_[1] ? allocators_[1]->top_side_ : finish_;
    return static_cast<int>(top - bottom);
}

// 0x005BC560
uint8_t* Zone::carve(MemSource* allocator, int size, int /*unused*/)
{
    int side = (allocator->direction_ >> 1) & 1; // 0 growing up, 1 growing down
    MemSource* other = allocators_[side ^ 1];
    uint8_t* new_top = allocator->top_side_ + allocator->direction_ * size;
    uint8_t* result = new_top;
    if (side) {
        uint8_t* limit = other ? other->top_side_ : outset_;
        if (new_top < limit)
            return nullptr;
    } else {
        uint8_t* limit = other ? other->top_side_ : finish_;
        result = allocator->top_side_;
        if (new_top > limit)
            return nullptr;
    }
    allocator->top_side_ = new_top;
    int available = free_byte_count();
    if (available < min_available_)
        min_available_ = available;
    return result;
}

// 0x005BC620
MemoryArena::MemoryArena(uint32_t size)
{
    vtable_ = kAllocRegionVtable;
    if (size) {
        uint32_t rounded = size & ~0xFu;
        auto* start = static_cast<uint8_t*>(alloc(rounded)); // (New makes sure the memory system is set up)
        finish_ = start + rounded;
        outset_ = start;
        min_available_ = static_cast<int>(finish_ - start);
        allocators_[1] = nullptr;
        allocators_[0] = nullptr;
    }
}

// 0x005BC6B0
void MemoryArena::destroy_body()
{
    vtable_ = kAllocRegionVtable;
    dealloc(outset_);
    vtable_ = kRegionVtable;
}

// --- allocators ----------------------------------------------------------------------------------------

// 0x005BA0E0 (lists::Elem destructor: take it out of its list)
void ListNode::uncouple() { unlink_as(kNodeVtable); }

// 0x005BC990 / 0x005BCD30: the other node types' destructors
void ListNode::unlink_as(void** node_vtable)
{
    vtable = node_vtable;
    prev->next = next;
    next->prev = prev;
    next = this;
    prev = this;
}

// 0x005BC4D0
void Zone::destroy_body() { vtable_ = kRegionVtable; }

// 0x005BA260
MemSource::MemSource(Zone* region, int direction, const char* name)
{
    vtable_ = kAllocatorVtable;
    used_blocks_ = max_used_blocks_ = free_blocks_ = max_free_blocks_ = 0;
    used_size_ = max_used_size_ = free_size_ = max_free_size_ = 0;
    region_ = region;
    direction_ = direction;
    label_ = name;
    zone_chain_ = {kHeadVtable, reinterpret_cast<void*>(-1), 0, &zone_chain_, &zone_chain_};
    base_context_.node = {kNodeVtable, &base_context_, 0, &base_context_.node, &base_context_.node};
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
void MemSource::destroy_body()
{
    vtable_ = kAllocatorVtable;
    region_->remove_allocator(this);
    base_context_.node.uncouple();
    zone_chain_.uncouple();
}

// 0x005BA1E0: a new, empty context on top of the stack
void MemSource::enter_heap_scope()
{
    auto* context = static_cast<CtxInfo*>(alloc_zeroed(sizeof(CtxInfo)));
    if (context)
        context->node = {kNodeVtable, context, 0, &context->node, &context->node};
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
        dealloc(context);
    }
    ListNode* next = zone_chain_.next;
    context_ = next->data != reinterpret_cast<void*>(-1) && next ? static_cast<CtxInfo*>(next->data) : nullptr;
}

} // namespace memory

namespace {

using memory::ListNode;
using memory::MemoryArena;
using memory::MemSource;
using memory::Zone;

// Constructors and destructors through member functions (they return `this` in EAX).
struct AllocAdapters {
    Zone* region_construct(uint8_t* start, uint8_t* end) { return new (this) Zone(start, end); }
    MemoryArena* alloc_region_construct(uint32_t size) { return new (this) MemoryArena(size); }
    // 0x005BC6D0 (Region's virtual [0]): destroyed, and freed with flags & 1.
    Zone* region_destroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<Zone*>(this);
        region->destroy_body();
        if (flags & 1)
            memory::dealloc(region);
        return region;
    }
    MemoryArena* alloc_region_destroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<MemoryArena*>(this);
        region->destroy_body();
        if (flags & 1)
            memory::dealloc(region);
        return region;
    }
    MemSource* allocator_construct(Zone* region, int direction, const char* name)
    {
        return new (this) MemSource(region, direction, name);
    }
    MemSource* allocator_destroy(unsigned int flags)
    {
        auto* allocator = reinterpret_cast<MemSource*>(this);
        allocator->destroy_body();
        if (flags & 1)
            memory::dealloc(allocator);
        return allocator;
    }
    // 0x005BA100 / 0x005BA230: the list node's and list head's scalar deleting destructors
    ListNode* node_destroy(unsigned int flags)
    {
        auto* node = reinterpret_cast<ListNode*>(this);
        node->uncouple();
        if (flags & 1)
            memory::dealloc(node);
        return node;
    }
    // The node destructors of two other node types (vtables 0x0065797C and 0x00658A70).
    void unlink_node_65797c() { reinterpret_cast<ListNode*>(this)->unlink_as(vt::kSaveNode); }
    // (the node of sio::InputPort's list)
    void unlink_node_658a_70() { reinterpret_cast<ListNode*>(this)->unlink_as(vt::kDeviceNode); }
    // 0x005BA4E0: free what the first field points to
    void free_pointee() { memory::dealloc(*reinterpret_cast<void**>(this)); }
};

} // namespace

namespace memory {

namespace {
static VtableSlot g_head_vtable_slots[] = {&AllocAdapters::node_destroy};
void** const kHeadVtable = reinterpret_cast<void**>(g_head_vtable_slots);
static VtableSlot g_allocator_vtable_slots[] = {&memory::MemSource::enter_heap_scope,
                                                &memory::MemSource::leave_heap_scope,
                                                &AllocAdapters::allocator_destroy,
                                                &_purecall,
                                                stubs::return_zero,
                                                stubs::return_zero2,
                                                stubs::return_zero2,
                                                stubs::return_zero2,
                                                &_purecall};
void** const kAllocatorVtable = reinterpret_cast<void**>(g_allocator_vtable_slots);
} // namespace

static VtableSlot g_node_vtable_slots[] = {&AllocAdapters::node_destroy};
void** const kNodeVtable = reinterpret_cast<void**>(g_node_vtable_slots); // lists::Elem<Context>
static VtableSlot g_region_vtable_slots[] = {&AllocAdapters::region_destroy};
void** const kRegionVtable = reinterpret_cast<void**>(g_region_vtable_slots);
static VtableSlot g_alloc_region_vtable_slots[] = {&AllocAdapters::alloc_region_destroy};
void** const kAllocRegionVtable = reinterpret_cast<void**>(g_alloc_region_vtable_slots);

} // namespace memory

namespace vt {
static VtableSlot g_save_node_slots[] = {&save_cards::ListDestructors::node_destroy};
void** const kSaveNode = reinterpret_cast<void**>(g_save_node_slots);
} // namespace vt
