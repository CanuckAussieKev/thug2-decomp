#include "game/core/manager.h"

#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/sys/game_config.h"
#include "hook/game.h"
#include "hook/game_global.h"
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

// The one memory manager (made by the exe's static initialisers: standalone start-up order, step 6).
GAME_GLOBAL(Manager*, g_memoryManager, 0x007D1800);
GAME_GLOBAL(bool, g_managerReady, 0x007CC979); // the memory manager is set up

Manager* Manager::Get()
{
    return g_memoryManager;
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

// --- creating the manager and its heaps -----------------------------------------------------------------------

namespace Mem {

namespace {

// Where the manager and its main region and heaps live (static storage in the game).
void* const kManagerStorage = reinterpret_cast<void*>(0x007D11F8);
void* const kMainRegionStorage = reinterpret_cast<void*>(0x007D11E0);
void* const kTopDownStorage = reinterpret_cast<void*>(0x007D1130);
void* const kBottomUpStorage = reinterpret_cast<void*>(0x007D1098);

bool& ManagerReady() { return g_managerReady; }

// The game CRT's malloc (the main block comes from it).
void* CrtMalloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }

AllocRegion* NewAllocRegion(uint32_t size)
{
    void* memory = Calloc(sizeof(AllocRegion));
    return memory ? new (memory) AllocRegion(size) : nullptr;
}

} // namespace

Manager*& Manager::Instance()
{
    return g_memoryManager;
}

GAME_GLOBAL(uint8_t*, g_heapBlockLimit, 0x007D17E4);
GAME_GLOBAL(uint8_t*, g_heapBlockEnd, 0x007D17E8);
GAME_GLOBAL(uint8_t*, g_heapBlockStart, 0x007D17EC);
GAME_GLOBAL(uintptr_t, g_dataEnd, 0x007D17F0);
GAME_GLOBAL(uintptr_t, g_dataStart, 0x007D17F4);
GAME_GLOBAL(uintptr_t, g_codeStart, 0x007D17F8);

// 0x005BBC00
Manager* Manager::Construct()
{
    m_current = nullptr;
    for (NamedHeap& named : m_namedHeaps) {
        named.region = nullptr;
        named.heap = nullptr;
        named.name = 0;
        named.used = false;
    }
    m_unknown5E4 = 0;
    m_unknown5E0 = 0;

    // Not original - the block is 64 MB and what the bigger script pools take more (game/core/memory_sizes.h:
    // pool sizes as THUGPRO's), so every heap in it keeps the room it had.
    // Original behaviour: 64 MB (0x4000000).
    const uint32_t blockSize = Sizes::MainBlockSize();
    auto* block = static_cast<uint8_t*>(CrtMalloc(blockSize));
    g_heapBlockStart = block;
    g_heapBlockEnd = block + blockSize;
    g_heapBlockLimit = block + blockSize;
    uint8_t* end = g_heapBlockLimit;
    void* region = Clear(sizeof(Region), kMainRegionStorage);
    m_mainRegion = region ? new (region) Region(reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(block) + 15) & ~15u),
                                                reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(end) & ~15u))
                          : nullptr;
    void* top = Clear(sizeof(Heap), kTopDownStorage);
    m_topDownHeap = top ? new (top) Heap(m_mainRegion, Allocator::kGrowDown, "Top Down") : nullptr;
    void* bottom = Clear(sizeof(Heap), kBottomUpStorage);
    m_bottomUpHeap = bottom ? new (bottom) Heap(m_mainRegion, Allocator::kGrowUp, "BottomUp") : nullptr;
    m_heaps[1] = m_bottomUpHeap;
    m_heaps[0] = m_topDownHeap;
    m_heapCount = 2;

    // (the code and data bounds are never set in this build: all zero)
    auto codeStart = g_codeStart;
    auto dataStart = g_dataStart;
    auto dataEnd = g_dataEnd;
    uint32_t dataK = static_cast<uint32_t>(dataEnd - dataStart) >> 10;
    uint32_t codeK = static_cast<uint32_t>(dataStart - codeStart) >> 10;
    Dbg::Printf("code [%p - %p] (%dK) + data [%p - %p] (%dK) = %dK \n", codeStart, dataStart, codeK, dataStart, dataEnd,
                dataK, dataK + codeK);

    m_contextDepth = 0;
    m_internetRegion = nullptr;
    m_netMiscRegion = nullptr;
    m_cutsceneRegion = nullptr;
    m_cutsceneBottomUpHeap = nullptr;
    m_cutsceneTopDownHeap = nullptr;
    return this;
}

// 0x005BB4E0
Heap* Manager::AddHeap(Region* region, int direction, const char* name)
{
    void* memory = Calloc(sizeof(Heap));
    Heap* heap = memory ? new (memory) Heap(region, direction, name) : nullptr;
    m_heaps[m_heapCount++] = heap;
    return heap;
}

// 0x005BBE80
void Manager::InitStandardHeaps()
{
    m_frontEndRegion = NewAllocRegion(GameConfig::g_configBootstrap ? 1000000 : 1100000);
    Dbg::Printf("allocated mp_frontend_region at %p\n", m_frontEndRegion);
    m_networkHeap = AddHeap(m_frontEndRegion, Allocator::kGrowDown, "Network");
    Dbg::Printf("Setup TOP_DOWN mp_network_heap at %p\n", m_networkHeap);
    m_frontEndHeap = AddHeap(m_frontEndRegion, Allocator::kGrowUp, "FrontEnd");
    Dbg::Printf("Setup mp_frontend_heap at %p\n", m_frontEndHeap);

    // Not original - grown by what the bigger script pools take more (game/core/memory_sizes.h: pool sizes as
    // THUGPRO's; the room left for scripts stays the original's).
    // Original behaviour: 0x434770 bytes.
    m_scriptRegion = NewAllocRegion(Sizes::ScriptRegionSize());
    Dbg::Printf("allocated mp_script_region at %p\n", m_scriptRegion);
    m_scriptHeap = AddHeap(m_scriptRegion, Allocator::kGrowUp, "Script");
    Dbg::Printf("Setup mp_script_heap at %p\n", m_scriptHeap);

    m_skaterInfoRegion = NewAllocRegion(82000);
    Dbg::Printf("allocated mp_skater_info_region at %p\n", m_skaterInfoRegion);
    m_skaterInfoHeap = AddHeap(m_skaterInfoRegion, Allocator::kGrowUp, "skt_info");
    Dbg::Printf("Setup mp_skater_info_heap at %p\n", m_skaterInfoHeap);

    m_themeRegion = NewAllocRegion(0x3C00);
    Dbg::Printf("allocated mp_theme_region at %p\n", m_themeRegion);
    m_themeHeap = AddHeap(m_themeRegion, Allocator::kGrowUp, "theme");
    Dbg::Printf("Setup mp_theme_heap at %p\n", m_themeHeap);

    m_skaterRegions[0] = NewAllocRegion(0x1AFA4);
    Dbg::Printf("allocated mp_skater_region at %p\n", m_skaterRegions[0]);
    m_skaterHeaps[0] = AddHeap(m_skaterRegions[0], Allocator::kGrowUp, "skater");
    Dbg::Printf("Setup mp_skater_heap at %p\n", m_skaterHeaps[0]);

    m_skaterGeomRegions[0] = NewAllocRegion(0x73F78);
    Dbg::Printf("allocated mp_skater_geom_region at %p\n", m_skaterGeomRegions[0]);
    m_skaterGeomHeaps[0] = AddHeap(m_skaterGeomRegions[0], Allocator::kGrowUp, "skater_geom");
    Dbg::Printf("Setup mp_skater_geom_heap at %p\n", m_skaterGeomHeaps[0]);

    for (int i = 1; i < kMaxSkaters; ++i) {
        m_skaterRegions[i] = nullptr;
        m_skaterHeaps[i] = nullptr;
        m_skaterGeomRegions[i] = nullptr;
        m_skaterGeomHeaps[i] = nullptr;
    }
    InitNamedHeap(0x0B1BA9DE, 0x16800, "PermanentTextureHeap", 0);
    InitNamedHeap(0x33B42C96, 0x49C00, "UnloadableTextureHeap", 0);
}

// 0x005BB560 (made with the bottom-up heap current, on the game's manager)
void Manager::InitNetMiscHeap()
{
    if (m_netMiscRegion)
        return;
    Manager* manager = Get();
    manager->PushContext(manager->m_bottomUpHeap);
    m_netMiscRegion = NewAllocRegion(160000);
    m_netMiscHeap = AddHeap(m_netMiscRegion, Allocator::kGrowUp, "NetMisc");
    Get()->PopContext();
}

// 0x005BB660 (made with the bottom-up heap current, on the game's manager)
void Manager::InitInternetHeaps()
{
    if (m_internetRegion)
        return;
    Manager* manager = Get();
    manager->PushContext(manager->m_bottomUpHeap);
    m_internetRegion = NewAllocRegion(450000);
    m_internetTopDownHeap = AddHeap(m_internetRegion, Allocator::kGrowDown, "InternetTopDown");
    m_internetBottomUpHeap = AddHeap(m_internetRegion, Allocator::kGrowUp, "InternetBottomUp");
    Get()->PopContext();
}

// 0x005BB780
void Manager::InitCutsceneHeaps(uint32_t size)
{
    if (m_cutsceneRegion)
        return;
    m_cutsceneRegion = NewAllocRegion(size);
    m_cutsceneBottomUpHeap = AddHeap(m_cutsceneRegion, Allocator::kGrowUp, "CutsceneBottomUp");
    m_cutsceneTopDownHeap = AddHeap(m_cutsceneRegion, Allocator::kGrowDown, "CutsceneTopDown");
}

// 0x005BB820
void Manager::InitExtraSkaterHeaps(int count)
{
    Dbg::Printf("Init Skater Heaps\n");
    if (count > kMaxSkaters)
        count = kMaxSkaters;
    for (int i = 1; i < count; ++i) {
        m_skaterRegions[i] = NewAllocRegion(0x1AFA4);
        Dbg::Printf("EXTRA: allocated mp_skater_region at %p\n", m_skaterRegions[i]);
        m_skaterHeaps[i] = AddHeap(m_skaterRegions[i], Allocator::kGrowUp, "skaterX");
        Dbg::Printf("EXTRA: Setup mp_skater_heap at %p\n", m_skaterHeaps[i]);
        m_skaterGeomRegions[i] = NewAllocRegion(0x73F78);
        Dbg::Printf("EXTRA: allocated mp_skater_geom_region at %p\n", m_skaterGeomRegions[i]);
        m_skaterGeomHeaps[i] = AddHeap(m_skaterGeomRegions[i], Allocator::kGrowUp, "skaterGeomX");
        Dbg::Printf("EXTRA: Setup mp_skater_geom_heap at %p\n", m_skaterGeomHeaps[i]);
    }
    Dbg::Printf("END Init Skater Heaps\n");
}

// 0x005BB9E0
void Manager::InitNamedHeap(uint32_t name, uint32_t size, const char* text, int /*unused*/)
{
    NamedHeap* named = nullptr;
    for (NamedHeap& slot : m_namedHeaps) {
        if (!slot.used) {
            named = &slot;
            break;
        }
    }
    // (no free slot: crashes here, as in the original)
    named->name = name;
    std::memcpy(named->text, text, std::strlen(text) + 1); // (unbounded, as in the original)
    named->region = NewAllocRegion(size);
    named->heap = AddHeap(named->region, Allocator::kGrowUp, named->text);
    named->used = true;
}

// 0x005BC2C0
void SetUpManager()
{
    if (Manager::Get())
        return;
    void* memory = Clear(sizeof(Manager), kManagerStorage);
    Manager* manager = memory ? static_cast<Manager*>(memory)->Construct() : nullptr;
    Manager::Instance() = manager;
    manager->PushContext(manager->m_bottomUpHeap);
}

// 0x004E1950
int EnsureManager()
{
    if (!ManagerReady()) {
        SetUpManager();
        ManagerReady() = true;
    }
    return 0;
}

// 0x005BC340
void ShutDownManager()
{
    Manager* manager = Manager::Get();
    if (!manager)
        return;
    manager->DeleteStandardHeaps();
    manager->PopContext();
    manager->DestroyMainHeaps();
    Manager::Instance() = nullptr;
}

} // namespace Mem

REPLACE(0x005BBC00, &Mem::Manager::Construct);
REPLACE(0x005BBE80, &Mem::Manager::InitStandardHeaps);
REPLACE(0x005BB4E0, &Mem::Manager::AddHeap);
REPLACE(0x005BB560, &Mem::Manager::InitNetMiscHeap);
REPLACE(0x005BB660, &Mem::Manager::InitInternetHeaps);
REPLACE(0x005BB780, &Mem::Manager::InitCutsceneHeaps);
REPLACE(0x005BB820, &Mem::Manager::InitExtraSkaterHeaps);
REPLACE(0x005BB9E0, &Mem::Manager::InitNamedHeap);
REPLACE(0x005BC2C0, Mem::SetUpManager);
REPLACE(0x004E1950, Mem::EnsureManager);
REPLACE(0x005BC340, Mem::ShutDownManager);
