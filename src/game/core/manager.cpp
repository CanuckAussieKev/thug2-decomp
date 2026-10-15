#include "game/core/manager.h"

#include <cstring>

#include "game/core/dbg.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mem {

namespace {

// Virtual calls on allocators and regions, by vtable slot.
template <typename R, typename... A>
R CallSlot(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

void* HeapAllocate(Allocator* heap, uint32_t size, int assertOnFail)
{
    return CallSlot<void*>(heap, 3, size, assertOnFail);
}

// Delete a heap / region through its scalar deleting destructor.
void DeleteAllocator(Allocator* heap) { CallSlot<void*>(heap, 2, 1u); }
void DeleteRegion(Region* region) { CallSlot<void*>(region, 0, 1u); }

Allocator* CurrentHeap(Manager* manager) { return *manager->m_current; }

} // namespace

Manager* Manager::Get()
{
    return GameVariable<Manager*>(0x007D1800);
}

// 0x005BAFC0
void* Manager::Allocate(uint32_t size, int assertOnFail, Allocator* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* block = HeapAllocate(heap, size, assertOnFail);
    if (block)
        MarkBlockInUse(block);
    return block;
}

// 0x005BB000
void* Manager::AllocateZeroed(uint32_t size, int assertOnFail, Allocator* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* block = HeapAllocate(heap, size, assertOnFail);
    if (block) {
        MarkBlockInUse(block);
        std::memset(block, 0, size);
    }
    return block;
}

// 0x005BB060
void* Manager::ReallocateUp(uint32_t size, void* block, Allocator* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* result = CallSlot<void*>(heap, 6, size, block);
    if (result)
        MarkBlockInUse(result);
    return result;
}

// 0x005BB0A0
void* Manager::ReallocateShrink(uint32_t size, void* block, Allocator* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* result = CallSlot<void*>(heap, 7, size, block);
    if (result)
        MarkBlockInUse(result);
    return result;
}

// 0x005BABA0
void Manager::Free(void* block)
{
    if (block)
        FreeBlock(block);
}

// 0x005BB050
uint32_t Manager::Available()
{
    return CallSlot<uint32_t>(CurrentHeap(this), 4);
}

// 0x005BB0E0
void Manager::PushContext(Allocator* heap)
{
    m_current = &m_contextStack[m_contextDepth];
    *m_current = heap;
    ++m_contextDepth;
}

// 0x005BB110
void Manager::PopContext()
{
    --m_contextDepth;
    m_contextStack[m_contextDepth] = reinterpret_cast<Allocator*>(-1);
    m_current = m_contextDepth ? &m_contextStack[m_contextDepth - 1] : nullptr;
}

// 0x005BB150
const char* Manager::CurrentHeapName()
{
    return CurrentHeap(this)->m_name;
}

// 0x005BAC10
Allocator* Manager::FirstHeap()
{
    return m_heapCount ? m_heaps[0] : nullptr;
}

// 0x005BAC20 (the last heap is never found, so its "next" is null too)
Allocator* Manager::NextHeap(Allocator* heap)
{
    for (int i = 0; i < m_heapCount - 1; ++i) {
        if (m_heaps[i] == heap)
            return m_heaps[i + 1];
    }
    return nullptr;
}

// 0x005BABC0
void Manager::DeleteHeap(Allocator* heap)
{
    for (int i = 0; i < m_heapCount; ++i) {
        if (m_heaps[i] == heap) {
            --m_heapCount;
            m_heaps[i] = m_heaps[m_heapCount];
            break;
        }
    }
    if (heap)
        DeleteAllocator(heap);
}

// 0x005BAF00
Manager::NamedHeap* Manager::FindNamedHeap(uint32_t name)
{
    for (NamedHeap& named : m_namedHeaps) {
        if (named.used && named.name == name)
            return &named;
    }
    return nullptr;
}

// 0x005BAF50
Heap* Manager::GetNamedHeap(uint32_t name, int /*unused*/)
{
    NamedHeap* named = FindNamedHeap(name);
    return named ? named->heap : nullptr;
}

// 0x005BAF70
bool Manager::DeleteNamedHeap(uint32_t name, int /*unused*/)
{
    NamedHeap* named = FindNamedHeap(name);
    if (!named)
        return false;
    DeleteHeap(named->heap);
    if (named->region)
        DeleteRegion(named->region);
    named->region = nullptr;
    named->heap = nullptr;
    named->name = 0;
    named->used = false;
    return true;
}

namespace {

// The standard heaps' name checksums.
enum HeapName : uint32_t {
    kTopDownHeap = 0x8FDB68AF,
    kBottomUpHeap = 0x0C7800B0,
    kFrontEndHeap = 0x62F3A0F3,
    kScriptHeap = 0x0FA33D9B,
    kNetworkHeap = 0xE3551D2E,
    kNetMiscHeap = 0x96D29D93,
    kInternetTopDownHeap = 0xFCD5166B,
    kInternetBottomUpHeap = 0x90020867,
    kCutsceneTopDown = 0x25D71469,
    kCutsceneBottomUp = 0x7ED56B49,
    kDebugHeap = 0x70CB0238,
    kSkater0 = 0xC3909393,
    kSkater1 = 0xB497A305,
    kSkater2 = 0x2D9EF2BF,
    kSkater3 = 0x5A99C229,
    kSkaterGeom0 = 0x572A9F4C,
    kSkaterGeom1 = 0x202DAFDA,
    kSkaterGeom2 = 0xB924FE60,
    kSkaterGeom3 = 0xCE23CEF6,
    kSkaterInfo = 0x08682D24,
    kThemeHeap = 0x17ECB880,
};

} // namespace

// 0x005BB310
Heap* Manager::GetHeap(uint32_t name)
{
    if (NamedHeap* named = FindNamedHeap(name))
        return named->heap;
    switch (name) {
    case kTopDownHeap: return m_topDownHeap;
    case kBottomUpHeap: return m_bottomUpHeap;
    case kFrontEndHeap: return m_frontEndHeap;
    case kScriptHeap: return m_scriptHeap;
    case kNetworkHeap: return m_networkHeap;
    case kNetMiscHeap: return m_netMiscHeap;
    case kInternetTopDownHeap: return m_internetTopDownHeap;
    case kInternetBottomUpHeap: return m_internetBottomUpHeap;
    case kCutsceneTopDown: return m_cutsceneTopDownHeap;
    case kCutsceneBottomUp: return m_cutsceneBottomUpHeap;
    case kDebugHeap: return m_debugHeap;
    case kSkater0: return m_skaterHeaps[0];
    case kSkater1: return m_skaterHeaps[1];
    case kSkater2: return m_skaterHeaps[2];
    case kSkater3: return m_skaterHeaps[3];
    case kSkaterGeom0: return m_skaterGeomHeaps[0];
    case kSkaterGeom1: return m_skaterGeomHeaps[1];
    case kSkaterGeom2: return m_skaterGeomHeaps[2];
    case kSkaterGeom3: return m_skaterGeomHeaps[3];
    case kSkaterInfo: return m_skaterInfoHeap;
    case kThemeHeap: return m_themeHeap;
    default: break;
    }
    // (looked up again, as in the original)
    NamedHeap* named = FindNamedHeap(name);
    return named ? named->heap : nullptr;
}

// 0x005BB160
const char* Manager::GetHeapName(uint32_t name)
{
    if (NamedHeap* named = FindNamedHeap(name))
        return named->text;
    switch (name) {
    case kTopDownHeap: return "TopDownHeap";
    case kBottomUpHeap: return "BottomUpHeap";
    case kFrontEndHeap: return "FrontEndHeap";
    case kScriptHeap: return "ScriptHeap";
    case kNetworkHeap: return "NetworkHeap";
    case kNetMiscHeap: return "NetMiscHeap";
    case kInternetTopDownHeap: return "InternetTopDownHeap";
    case kInternetBottomUpHeap: return "InternetBottomUpHeap";
    case kCutsceneTopDown: return "CutsceneTopDown";
    case kCutsceneBottomUp: return "CutsceneBottomUp";
    case kDebugHeap: return "DebugHeap";
    case kSkater0: return "Skater0";
    case kSkater1: return "Skater1";
    case kSkater2: return "Skater2";
    case kSkater3: return "Skater3";
    case kSkaterGeom0: return "SkaterGeom0";
    case kSkaterGeom1: return "SkaterGeom1";
    case kSkaterGeom2: return "SkaterGeom2";
    case kSkaterGeom3: return "SkaterGeom3";
    case kSkaterInfo: return "SkaterInfo";
    case kThemeHeap: return "ThemeHeap";
    default: return "Unrecognized Heap";
    }
}

// 0x005BAC50
void Manager::DeleteNetMiscHeap()
{
    if (!m_netMiscRegion)
        return;
    DeleteHeap(m_netMiscHeap);
    if (m_netMiscRegion)
        DeleteRegion(m_netMiscRegion);
    m_netMiscRegion = nullptr;
}

// 0x005BAC90
void Manager::DeleteCutsceneHeaps()
{
    if (!m_cutsceneRegion)
        return;
    DeleteHeap(m_cutsceneBottomUpHeap);
    m_cutsceneBottomUpHeap = nullptr;
    DeleteHeap(m_cutsceneTopDownHeap);
    m_cutsceneTopDownHeap = nullptr;
    if (m_cutsceneRegion)
        DeleteRegion(m_cutsceneRegion);
    m_cutsceneRegion = nullptr;
}

// 0x005BACF0: the heaps of skaters 1 to 7 (multiplayer)
void Manager::DeleteExtraSkaterHeaps()
{
    for (int i = 1; i < kMaxSkaters; ++i) {
        if (m_skaterRegions[i]) {
            Dbg::Printf("DELETING SKATER HEAP %d\n", i);
            DeleteHeap(m_skaterHeaps[i]);
            if (m_skaterRegions[i])
                DeleteRegion(m_skaterRegions[i]);
            m_skaterHeaps[i] = nullptr;
            m_skaterRegions[i] = nullptr;
        }
        if (m_skaterGeomRegions[i]) {
            Dbg::Printf("DELETING SKATER GEOM HEAP %d\n", i);
            DeleteHeap(m_skaterGeomHeaps[i]);
            if (m_skaterGeomRegions[i])
                DeleteRegion(m_skaterGeomRegions[i]);
            m_skaterGeomHeaps[i] = nullptr;
            m_skaterGeomRegions[i] = nullptr;
        }
    }
}

// 0x005BADD0 (the pointers are left as they were)
void Manager::DeleteStandardHeaps()
{
    DeleteHeap(m_frontEndHeap);
    if (m_frontEndRegion)
        DeleteRegion(m_frontEndRegion);
    DeleteHeap(m_scriptHeap);
    if (m_scriptRegion)
        DeleteRegion(m_scriptRegion);
    DeleteHeap(m_networkHeap);
    if (m_networkRegion)
        DeleteRegion(m_networkRegion);
    DeleteHeap(m_skaterInfoHeap);
    if (m_skaterInfoRegion)
        DeleteRegion(m_skaterInfoRegion);
    DeleteHeap(m_skaterHeaps[0]);
    if (m_skaterRegions[0])
        DeleteRegion(m_skaterRegions[0]);
    DeleteHeap(m_skaterGeomHeaps[0]);
    if (m_skaterGeomRegions[0])
        DeleteRegion(m_skaterGeomRegions[0]);
    DeleteExtraSkaterHeaps();
}

// 0x005BBE50
void Manager::DestroyMainHeaps()
{
    CallSlot<void*>(m_topDownHeap, 2, 0u);
    CallSlot<void*>(m_bottomUpHeap, 2, 0u);
    CallSlot<void*>(m_mainRegion, 0, 0u);
}

// --- the global allocation functions (on the current heap) -------------------------------------------------

// 0x005BBAE0
void* Malloc(unsigned int size)
{
    return Manager::Get()->Allocate(size, 1, nullptr);
}

// 0x005BBB10
uint32_t MallocAvailable()
{
    return Manager::Get()->Available();
}

// 0x005BBB30
void* ReallocateShrink(unsigned int size, void* memory)
{
    void* result = CallSlot<void*>(CurrentHeap(Manager::Get()), 7, size, memory);
    if (result)
        MarkBlockInUse(result);
    return result;
}

// 0x005BBB60
void FreeMalloc(void* memory)
{
    if (memory)
        FreeBlock(memory);
}

// 0x005BBB80
unsigned int MallocSize(void* memory)
{
    return memory ? GetBlockSize(memory) : 0;
}

// 0x005BBBA0: a new block of `size` with the old one's contents (copies `size` bytes, even from a smaller
// block - as in the original)
void* Realloc(void* memory, unsigned int size)
{
    void* block = nullptr;
    if (size) {
        block = HeapAllocate(CurrentHeap(Manager::Get()), size, 1);
        if (block)
            MarkBlockInUse(block);
    }
    if (memory) {
        if (block)
            std::memmove(block, memory, size);
        FreeBlock(memory);
    }
    return block;
}

} // namespace Mem

REPLACE(0x005BAFC0, &Mem::Manager::Allocate);
REPLACE(0x005BB000, &Mem::Manager::AllocateZeroed);
REPLACE(0x005BB060, &Mem::Manager::ReallocateUp);
REPLACE(0x005BB0A0, &Mem::Manager::ReallocateShrink);
REPLACE(0x005BABA0, &Mem::Manager::Free);
REPLACE(0x005BB050, &Mem::Manager::Available);
REPLACE(0x005BB0E0, &Mem::Manager::PushContext);
REPLACE(0x005BB110, &Mem::Manager::PopContext);
REPLACE(0x005BB150, &Mem::Manager::CurrentHeapName);
REPLACE(0x005BAC10, &Mem::Manager::FirstHeap);
REPLACE(0x005BAC20, &Mem::Manager::NextHeap);
REPLACE(0x005BABC0, &Mem::Manager::DeleteHeap);
REPLACE(0x005BAF00, &Mem::Manager::FindNamedHeap);
REPLACE(0x005BAF50, &Mem::Manager::GetNamedHeap);
REPLACE(0x005BAF70, &Mem::Manager::DeleteNamedHeap);
REPLACE(0x005BB310, &Mem::Manager::GetHeap);
REPLACE(0x005BB160, &Mem::Manager::GetHeapName);
REPLACE(0x005BAC50, &Mem::Manager::DeleteNetMiscHeap);
REPLACE(0x005BAC90, &Mem::Manager::DeleteCutsceneHeaps);
REPLACE(0x005BACF0, &Mem::Manager::DeleteExtraSkaterHeaps);
REPLACE(0x005BADD0, &Mem::Manager::DeleteStandardHeaps);
REPLACE(0x005BBE50, &Mem::Manager::DestroyMainHeaps);
REPLACE(0x005BBAE0, Mem::Malloc);
REPLACE(0x005BBB10, Mem::MallocAvailable);
REPLACE(0x005BBB30, static_cast<void* (*)(unsigned int, void*)>(&Mem::ReallocateShrink));
REPLACE(0x005BBB60, Mem::FreeMalloc);
REPLACE(0x005BBB80, Mem::MallocSize);
REPLACE(0x005BBBA0, Mem::Realloc);
