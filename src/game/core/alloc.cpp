#include "game/core/alloc.h"

#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

void** const kNodeVtable = reinterpret_cast<void**>(0x00658458);       // Lst::Node<Context>
void** const kHeadVtable = reinterpret_cast<void**>(0x00658480);       // Lst::Head<Context>
void** const kAllocatorVtable = reinterpret_cast<void**>(0x0065845C);
void** const kRegionVtable = reinterpret_cast<void**>(0x00658A58);
void** const kAllocRegionVtable = reinterpret_cast<void**>(0x00658A5C);

} // namespace

// --- block headers -------------------------------------------------------------------------------------

// 0x005BA080
void* MarkBlockInUse(void* block)
{
    BlockHeader::Of(block)->marker = BlockHeader::kBlockInUse;
    return block;
}

// 0x005BA090
BlockHeader* GetBlockHeader(void* block)
{
    return BlockHeader::Of(block);
}

// 0x005BA0A0
uint32_t GetBlockSize(void* block)
{
    return BlockHeader::Of(block)->size;
}

// 0x005BA0B0
Allocator* FreeBlock(void* block)
{
    BlockHeader* header = BlockHeader::Of(block);
    Allocator* allocator = header->allocator;
    if (header->marker != BlockHeader::kBlockFreed) {
        // The allocator's free (virtual slot 8).
        reinterpret_cast<void(__thiscall*)(Allocator*, BlockHeader*)>(allocator->m_vtable[8])(allocator, header);
        header->marker = BlockHeader::kBlockFreed;
    }
    return allocator;
}

// --- regions -------------------------------------------------------------------------------------------

// 0x005BC5F0
Region::Region(uint8_t* start, uint8_t* end)
{
    m_end = end;
    m_minAvailable = static_cast<int>(end - start);
    m_vtable = kRegionVtable;
    m_start = start;
    m_allocators[1] = nullptr;
    m_allocators[0] = nullptr;
}

// 0x005BC4E0
void Region::AddAllocator(Allocator* allocator)
{
    m_allocators[(allocator->m_direction >> 1) & 1] = allocator;
    allocator->m_top = allocator->m_direction == Allocator::kGrowUp ? m_start : m_end;
}

// 0x005BC510
void Region::RemoveAllocator(Allocator* allocator)
{
    m_allocators[(allocator->m_direction >> 1) & 1] = nullptr;
}

// 0x005BC530
int Region::MemAvailable()
{
    uint8_t* bottom = m_allocators[0] ? m_allocators[0]->m_top : m_start;
    uint8_t* top = m_allocators[1] ? m_allocators[1]->m_top : m_end;
    return static_cast<int>(top - bottom);
}

// 0x005BC560
uint8_t* Region::Allocate(Allocator* allocator, int size, int /*unused*/)
{
    int side = (allocator->m_direction >> 1) & 1; // 0 growing up, 1 growing down
    Allocator* other = m_allocators[side ^ 1];
    uint8_t* newTop = allocator->m_top + allocator->m_direction * size;
    uint8_t* result = newTop;
    if (side) {
        uint8_t* limit = other ? other->m_top : m_start;
        if (newTop < limit)
            return nullptr;
    } else {
        uint8_t* limit = other ? other->m_top : m_end;
        result = allocator->m_top;
        if (newTop > limit)
            return nullptr;
    }
    allocator->m_top = newTop;
    int available = MemAvailable();
    if (available < m_minAvailable)
        m_minAvailable = available;
    return result;
}

// 0x005BC620
AllocRegion::AllocRegion(uint32_t size)
{
    m_vtable = kAllocRegionVtable;
    if (size) {
        uint32_t rounded = size & ~0xFu;
        auto* start = static_cast<uint8_t*>(New(rounded)); // (New makes sure the memory system is set up)
        m_end = start + rounded;
        m_start = start;
        m_minAvailable = static_cast<int>(m_end - start);
        m_allocators[1] = nullptr;
        m_allocators[0] = nullptr;
    }
}

// 0x005BC6B0
void AllocRegion::DestroyBody()
{
    m_vtable = kAllocRegionVtable;
    Free(m_start);
    m_vtable = kRegionVtable;
}

// --- allocators ----------------------------------------------------------------------------------------

// 0x005BA0E0 (Lst::Node destructor: take it out of its list)
void ListNode::Unlink()
{
    UnlinkAs(kNodeVtable);
}

// 0x005BC990 / 0x005BCD30: the other node types' destructors
void ListNode::UnlinkAs(void** nodeVtable)
{
    vtable = nodeVtable;
    prev->next = next;
    next->prev = prev;
    next = this;
    prev = this;
}

// 0x005BC4D0
void Region::DestroyBody()
{
    m_vtable = kRegionVtable;
}

// 0x005BA260
Allocator::Allocator(Region* region, int direction, const char* name)
{
    m_vtable = kAllocatorVtable;
    m_usedBlocks = m_maxUsedBlocks = m_freeBlocks = m_maxFreeBlocks = 0;
    m_usedSize = m_maxUsedSize = m_freeSize = m_maxFreeSize = 0;
    m_region = region;
    m_direction = direction;
    m_name = name;
    m_contexts = {kHeadVtable, reinterpret_cast<void*>(-1), 0, &m_contexts, &m_contexts};
    m_baseContext.node = {kNodeVtable, &m_baseContext, 0, &m_baseContext.node, &m_baseContext.node};
    m_baseContext.freeList = nullptr;
    region->AddAllocator(this);

    // The base context starts the stack.
    m_baseContext.node.prev = &m_contexts;
    m_baseContext.node.next = m_contexts.next;
    m_contexts.next->prev = &m_baseContext.node;
    m_contexts.next = &m_baseContext.node;
    m_context = &m_baseContext;
}

// 0x005BA140
void Allocator::DestroyBody()
{
    m_vtable = kAllocatorVtable;
    m_region->RemoveAllocator(this);
    m_baseContext.node.Unlink();
    m_contexts.Unlink();
}

// 0x005BA1E0: a new, empty context on top of the stack
void Allocator::PushContext()
{
    auto* context = static_cast<Context*>(Calloc(sizeof(Context)));
    if (context)
        context->node = {kNodeVtable, context, 0, &context->node, &context->node};
    // (an allocation failure crashes here, as in the original)
    context->freeList = nullptr;
    context->node.prev = &m_contexts;
    context->node.next = m_contexts.next;
    m_contexts.next->prev = &context->node;
    m_contexts.next = &context->node;
    m_context = context;
}

// 0x005BA360: drop the current context; the next one down becomes current
void Allocator::PopContext()
{
    if (Context* context = m_context) {
        context->node.Unlink();
        Free(context);
    }
    ListNode* next = m_contexts.next;
    m_context = next->data != reinterpret_cast<void*>(-1) && next ? static_cast<Context*>(next->data) : nullptr;
}

} // namespace Mem

namespace {

using Mem::Allocator;
using Mem::AllocRegion;
using Mem::ListNode;
using Mem::Region;

// Constructors and destructors through member functions (they return `this` in EAX).
struct AllocAdapters {
    Region* RegionConstruct(uint8_t* start, uint8_t* end) { return new (this) Region(start, end); }
    AllocRegion* AllocRegionConstruct(uint32_t size) { return new (this) AllocRegion(size); }
    // 0x005BC6D0 (Region's virtual [0]): destroyed, and freed with flags & 1.
    Region* RegionDestroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<Region*>(this);
        region->DestroyBody();
        if (flags & 1)
            Mem::Free(region);
        return region;
    }
    AllocRegion* AllocRegionDestroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<AllocRegion*>(this);
        region->DestroyBody();
        if (flags & 1)
            Mem::Free(region);
        return region;
    }
    Allocator* AllocatorConstruct(Region* region, int direction, const char* name)
    {
        return new (this) Allocator(region, direction, name);
    }
    Allocator* AllocatorDestroy(unsigned int flags)
    {
        auto* allocator = reinterpret_cast<Allocator*>(this);
        allocator->DestroyBody();
        if (flags & 1)
            Mem::Free(allocator);
        return allocator;
    }
    // 0x005BA100 / 0x005BA230: the list node's and list head's scalar deleting destructors
    ListNode* NodeDestroy(unsigned int flags)
    {
        auto* node = reinterpret_cast<ListNode*>(this);
        node->Unlink();
        if (flags & 1)
            Mem::Free(node);
        return node;
    }
    // The node destructors of two other node types (vtables 0x0065797C and 0x00658A70).
    void UnlinkNode65797C() { reinterpret_cast<ListNode*>(this)->UnlinkAs(reinterpret_cast<void**>(0x0065797C)); }
    // (the node of Sio::Device's list)
    void UnlinkNode658A70() { reinterpret_cast<ListNode*>(this)->UnlinkAs(reinterpret_cast<void**>(0x00658A70)); }
    // 0x005BA4E0: free what the first field points to
    void FreePointee() { Mem::Free(*reinterpret_cast<void**>(this)); }
};

} // namespace

REPLACE(0x005BA080, Mem::MarkBlockInUse);
REPLACE(0x005BA090, Mem::GetBlockHeader);
REPLACE(0x005BA0A0, Mem::GetBlockSize);
REPLACE(0x005BA0B0, Mem::FreeBlock);
REPLACE(0x005BC5F0, &AllocAdapters::RegionConstruct);
REPLACE(0x005BC4E0, &Mem::Region::AddAllocator);
REPLACE(0x005BC510, &Mem::Region::RemoveAllocator);
REPLACE(0x005BC530, &Mem::Region::MemAvailable);
REPLACE(0x005BC560, &Mem::Region::Allocate);
REPLACE(0x005BC620, &AllocAdapters::AllocRegionConstruct);
REPLACE(0x005BC6B0, &Mem::AllocRegion::DestroyBody);
REPLACE(0x005BC6D0, &AllocAdapters::RegionDestroy);
REPLACE(0x005BC700, &AllocAdapters::AllocRegionDestroy);
REPLACE(0x005BA0E0, &Mem::ListNode::Unlink);
REPLACE(0x005BC990, &AllocAdapters::UnlinkNode65797C);
REPLACE(0x005BCD30, &AllocAdapters::UnlinkNode658A70);
REPLACE(0x005BC4D0, &Mem::Region::DestroyBody);
REPLACE(0x005BA100, &AllocAdapters::NodeDestroy);
REPLACE(0x005BA230, &AllocAdapters::NodeDestroy);
REPLACE(0x005BA260, &AllocAdapters::AllocatorConstruct);
REPLACE(0x005BA140, &Mem::Allocator::DestroyBody);
REPLACE(0x005BA330, &AllocAdapters::AllocatorDestroy);
REPLACE(0x005BA1E0, &Mem::Allocator::PushContext);
REPLACE(0x005BA360, &Mem::Allocator::PopContext);
REPLACE(0x005BA4E0, &AllocAdapters::FreePointee);
