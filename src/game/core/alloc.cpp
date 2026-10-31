#include "game/core/alloc.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/mc/save_types.h"
#include "game/core/stubs.h"

namespace MemSys {

// The virtual tables of the list node, the region and the allocated region (defined after their functions, below).
extern void** const kNodeVtable;
extern void** const kRegionVtable;
extern void** const kAllocRegionVtable;

namespace {

extern void** const kHeadVtable;       // ListKit::Foremost<Context>
extern void** const kAllocatorVtable;

} // namespace

// --- block headers -------------------------------------------------------------------------------------

// 0x005BA080
void* MarkBlockInUse(void* block)
{
    ChunkPrefix::Of(block)->marker = ChunkPrefix::kBlockInUse;
    return block;
}

// 0x005BA090
ChunkPrefix* GetBlockHeader(void* block)
{
    return ChunkPrefix::Of(block);
}

// 0x005BA0A0
uint32_t GetBlockSize(void* block)
{
    return ChunkPrefix::Of(block)->size;
}

// 0x005BA0B0
MemSource* FreeBlock(void* block)
{
    ChunkPrefix* header = ChunkPrefix::Of(block);
    MemSource* allocator = header->allocator;
    if (header->marker != ChunkPrefix::kBlockFreed) {
        // The allocator's free (virtual slot 8).
        reinterpret_cast<void(__thiscall*)(MemSource*, ChunkPrefix*)>(allocator->m_vtable[8])(allocator, header);
        header->marker = ChunkPrefix::kBlockFreed;
    }
    return allocator;
}

// --- regions -------------------------------------------------------------------------------------------

// 0x005BC5F0
Zone::Zone(uint8_t* start, uint8_t* end)
{
    m_finish = end;
    m_minAvailable = static_cast<int>(end - start);
    m_vtable = kRegionVtable;
    m_outset = start;
    m_allocators[1] = nullptr;
    m_allocators[0] = nullptr;
}

// 0x005BC4E0
void Zone::AddAllocator(MemSource* allocator)
{
    m_allocators[(allocator->m_direction >> 1) & 1] = allocator;
    allocator->m_topSide = allocator->m_direction == MemSource::kGrowUp ? m_outset : m_finish;
}

// 0x005BC510
void Zone::RemoveAllocator(MemSource* allocator)
{
    m_allocators[(allocator->m_direction >> 1) & 1] = nullptr;
}

// 0x005BC530
int Zone::FreeByteCount()
{
    uint8_t* bottom = m_allocators[0] ? m_allocators[0]->m_topSide : m_outset;
    uint8_t* top = m_allocators[1] ? m_allocators[1]->m_topSide : m_finish;
    return static_cast<int>(top - bottom);
}

// 0x005BC560
uint8_t* Zone::Carve(MemSource* allocator, int size, int /*unused*/)
{
    int side = (allocator->m_direction >> 1) & 1; // 0 growing up, 1 growing down
    MemSource* other = m_allocators[side ^ 1];
    uint8_t* newTop = allocator->m_topSide + allocator->m_direction * size;
    uint8_t* result = newTop;
    if (side) {
        uint8_t* limit = other ? other->m_topSide : m_outset;
        if (newTop < limit)
            return nullptr;
    } else {
        uint8_t* limit = other ? other->m_topSide : m_finish;
        result = allocator->m_topSide;
        if (newTop > limit)
            return nullptr;
    }
    allocator->m_topSide = newTop;
    int available = FreeByteCount();
    if (available < m_minAvailable)
        m_minAvailable = available;
    return result;
}

// 0x005BC620
MemoryArena::MemoryArena(uint32_t size)
{
    m_vtable = kAllocRegionVtable;
    if (size) {
        uint32_t rounded = size & ~0xFu;
        auto* start = static_cast<uint8_t*>(Alloc(rounded)); // (New makes sure the memory system is set up)
        m_finish = start + rounded;
        m_outset = start;
        m_minAvailable = static_cast<int>(m_finish - start);
        m_allocators[1] = nullptr;
        m_allocators[0] = nullptr;
    }
}

// 0x005BC6B0
void MemoryArena::DestroyBody()
{
    m_vtable = kAllocRegionVtable;
    Dealloc(m_outset);
    m_vtable = kRegionVtable;
}

// --- allocators ----------------------------------------------------------------------------------------

// 0x005BA0E0 (ListKit::Elem destructor: take it out of its list)
void ListNode::Uncouple()
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
void Zone::DestroyBody()
{
    m_vtable = kRegionVtable;
}

// 0x005BA260
MemSource::MemSource(Zone* region, int direction, const char* name)
{
    m_vtable = kAllocatorVtable;
    m_usedBlocks = m_maxUsedBlocks = m_freeBlocks = m_maxFreeBlocks = 0;
    m_usedSize = m_maxUsedSize = m_freeSize = m_maxFreeSize = 0;
    m_region = region;
    m_direction = direction;
    m_label = name;
    m_zoneChain = {kHeadVtable, reinterpret_cast<void*>(-1), 0, &m_zoneChain, &m_zoneChain};
    m_baseContext.node = {kNodeVtable, &m_baseContext, 0, &m_baseContext.node, &m_baseContext.node};
    m_baseContext.freeList = nullptr;
    region->AddAllocator(this);

    // The base context starts the stack.
    m_baseContext.node.prev = &m_zoneChain;
    m_baseContext.node.next = m_zoneChain.next;
    m_zoneChain.next->prev = &m_baseContext.node;
    m_zoneChain.next = &m_baseContext.node;
    m_context = &m_baseContext;
}

// 0x005BA140
void MemSource::DestroyBody()
{
    m_vtable = kAllocatorVtable;
    m_region->RemoveAllocator(this);
    m_baseContext.node.Uncouple();
    m_zoneChain.Uncouple();
}

// 0x005BA1E0: a new, empty context on top of the stack
void MemSource::EnterHeapScope()
{
    auto* context = static_cast<CtxInfo*>(AllocZeroed(sizeof(CtxInfo)));
    if (context)
        context->node = {kNodeVtable, context, 0, &context->node, &context->node};
    // (an allocation failure crashes here, as in the original)
    context->freeList = nullptr;
    context->node.prev = &m_zoneChain;
    context->node.next = m_zoneChain.next;
    m_zoneChain.next->prev = &context->node;
    m_zoneChain.next = &context->node;
    m_context = context;
}

// 0x005BA360: drop the current context; the next one down becomes current
void MemSource::LeaveHeapScope()
{
    if (CtxInfo* context = m_context) {
        context->node.Uncouple();
        Dealloc(context);
    }
    ListNode* next = m_zoneChain.next;
    m_context = next->data != reinterpret_cast<void*>(-1) && next ? static_cast<CtxInfo*>(next->data) : nullptr;
}

} // namespace Mem

namespace {

using MemSys::MemSource;
using MemSys::MemoryArena;
using MemSys::ListNode;
using MemSys::Zone;

// Constructors and destructors through member functions (they return `this` in EAX).
struct AllocAdapters {
    Zone* RegionConstruct(uint8_t* start, uint8_t* end) { return new (this) Zone(start, end); }
    MemoryArena* AllocRegionConstruct(uint32_t size) { return new (this) MemoryArena(size); }
    // 0x005BC6D0 (Region's virtual [0]): destroyed, and freed with flags & 1.
    Zone* RegionDestroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<Zone*>(this);
        region->DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(region);
        return region;
    }
    MemoryArena* AllocRegionDestroy(unsigned int flags)
    {
        auto* region = reinterpret_cast<MemoryArena*>(this);
        region->DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(region);
        return region;
    }
    MemSource* AllocatorConstruct(Zone* region, int direction, const char* name)
    {
        return new (this) MemSource(region, direction, name);
    }
    MemSource* AllocatorDestroy(unsigned int flags)
    {
        auto* allocator = reinterpret_cast<MemSource*>(this);
        allocator->DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(allocator);
        return allocator;
    }
    // 0x005BA100 / 0x005BA230: the list node's and list head's scalar deleting destructors
    ListNode* NodeDestroy(unsigned int flags)
    {
        auto* node = reinterpret_cast<ListNode*>(this);
        node->Uncouple();
        if (flags & 1)
            MemSys::Dealloc(node);
        return node;
    }
    // The node destructors of two other node types (vtables 0x0065797C and 0x00658A70).
    void UnlinkNode65797C() { reinterpret_cast<ListNode*>(this)->UnlinkAs(Vt::kSaveNode); }
    // (the node of Sio::InputPort's list)
    void UnlinkNode658A70() { reinterpret_cast<ListNode*>(this)->UnlinkAs(Vt::kDeviceNode); }
    // 0x005BA4E0: free what the first field points to
    void FreePointee() { MemSys::Dealloc(*reinterpret_cast<void**>(this)); }
};

} // namespace

namespace MemSys {

namespace {
static VtableSlot kHeadVtable_slots[] = {&AllocAdapters::NodeDestroy};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots);
static VtableSlot kAllocatorVtable_slots[] = {&MemSys::MemSource::EnterHeapScope, &MemSys::MemSource::LeaveHeapScope, &AllocAdapters::AllocatorDestroy, &_purecall, Stubs::ReturnZero, Stubs::ReturnZero2, Stubs::ReturnZero2, Stubs::ReturnZero2, &_purecall};
void** const kAllocatorVtable = reinterpret_cast<void**>(kAllocatorVtable_slots);
} // namespace

static VtableSlot kNodeVtable_slots[] = {&AllocAdapters::NodeDestroy};
void** const kNodeVtable = reinterpret_cast<void**>(kNodeVtable_slots);          // ListKit::Elem<Context>
static VtableSlot kRegionVtable_slots[] = {&AllocAdapters::RegionDestroy};
void** const kRegionVtable = reinterpret_cast<void**>(kRegionVtable_slots);
static VtableSlot kAllocRegionVtable_slots[] = {&AllocAdapters::AllocRegionDestroy};
void** const kAllocRegionVtable = reinterpret_cast<void**>(kAllocRegionVtable_slots);

} // namespace Mem

namespace Vt {
static VtableSlot kSaveNode_slots[] = {&MemCard::ListDestructors::NodeDestroy};
void** const kSaveNode = reinterpret_cast<void**>(kSaveNode_slots);
} // namespace Vt
