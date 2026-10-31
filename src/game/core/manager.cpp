#include "game/core/manager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/sys/game_config.h"
#include "game/sys/crt.h"

namespace MemSys {

namespace {

// Virtual calls on allocators and regions, by vtable slot.
template <typename R, typename... A>
R CallSlot(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

void* HeapAllocate(MemSource* heap, uint32_t size, int haltIfExhausted)
{
    return CallSlot<void*>(heap, 3, size, haltIfExhausted);
}

// Delete a heap / region through its scalar deleting destructor.
void DeleteAllocator(MemSource* heap) { CallSlot<void*>(heap, 2, 1u); }
void DeleteRegion(Zone* region) { CallSlot<void*>(region, 0, 1u); }

MemSource* CurrentHeap(Hub* manager) { return *manager->m_ongoing; }

} // namespace

// The one memory manager (made by the exe's static initialisers: standalone start-up order, step 6).
Hub* g_memoryManager;
bool g_managerReady; // the memory manager is set up

Hub* Hub::Pull()
{
    return g_memoryManager;
}

// 0x005BAFC0
void* Hub::Carve(uint32_t size, int haltIfExhausted, MemSource* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* block = HeapAllocate(heap, size, haltIfExhausted);
    if (block)
        MarkBlockInUse(block);
    return block;
}

// 0x005BB000
void* Hub::AllocateZeroed(uint32_t size, int haltIfExhausted, MemSource* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* block = HeapAllocate(heap, size, haltIfExhausted);
    if (block) {
        MarkBlockInUse(block);
        std::memset(block, 0, size);
    }
    return block;
}

// 0x005BB060
void* Hub::ExtendBlockUpward(uint32_t size, void* block, MemSource* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* result = CallSlot<void*>(heap, 6, size, block);
    if (result)
        MarkBlockInUse(result);
    return result;
}

// 0x005BB0A0
void* Hub::TrimBlockEnd(uint32_t size, void* block, MemSource* heap)
{
    if (!heap)
        heap = CurrentHeap(this);
    void* result = CallSlot<void*>(heap, 7, size, block);
    if (result)
        MarkBlockInUse(result);
    return result;
}

// 0x005BABA0
void Hub::Dealloc(void* block)
{
    if (block)
        FreeBlock(block);
}

// 0x005BB050
uint32_t Hub::Obtainable()
{
    return CallSlot<uint32_t>(CurrentHeap(this), 4);
}

// 0x005BB0E0
void Hub::EnterHeapScope(MemSource* heap)
{
    m_ongoing = &m_contextStack[m_contextDepth];
    *m_ongoing = heap;
    ++m_contextDepth;
}

// 0x005BB110
void Hub::LeaveHeapScope()
{
    --m_contextDepth;
    m_contextStack[m_contextDepth] = reinterpret_cast<MemSource*>(-1);
    m_ongoing = m_contextDepth ? &m_contextStack[m_contextDepth - 1] : nullptr;
}

// 0x005BB150
const char* Hub::CurrentHeapName()
{
    return CurrentHeap(this)->m_label;
}

// 0x005BAC10
MemSource* Hub::FirstZone()
{
    return m_heapCount ? m_heaps[0] : nullptr;
}

// 0x005BAC20 (the last heap is never found, so its "next" is null too)
MemSource* Hub::FollowingZone(MemSource* heap)
{
    for (int i = 0; i < m_heapCount - 1; ++i) {
        if (m_heaps[i] == heap)
            return m_heaps[i + 1];
    }
    return nullptr;
}

// 0x005BABC0
void Hub::DeleteHeap(MemSource* heap)
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
Hub::LabeledZone* Hub::FindNamedHeap(uint32_t name)
{
    for (LabeledZone& named : m_namedHeaps) {
        if (named.used && named.name == name)
            return &named;
    }
    return nullptr;
}

// 0x005BAF50
Arena* Hub::GetNamedHeap(uint32_t name, int /*unused*/)
{
    LabeledZone* named = FindNamedHeap(name);
    return named ? named->heap : nullptr;
}

// 0x005BAF70
bool Hub::ReleaseLabeledZone(uint32_t name, int /*unused*/)
{
    LabeledZone* named = FindNamedHeap(name);
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
Arena* Hub::GetMemoryPool(uint32_t name)
{
    if (LabeledZone* named = FindNamedHeap(name))
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
    LabeledZone* named = FindNamedHeap(name);
    return named ? named->heap : nullptr;
}

// 0x005BB160
const char* Hub::GetZoneLabel(uint32_t name)
{
    if (LabeledZone* named = FindNamedHeap(name))
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
void Hub::ReleaseNetScratchZone()
{
    if (!m_netMiscRegion)
        return;
    DeleteHeap(m_netMiscHeap);
    if (m_netMiscRegion)
        DeleteRegion(m_netMiscRegion);
    m_netMiscRegion = nullptr;
}

// 0x005BAC90
void Hub::DeleteCutsceneHeaps()
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
void Hub::DeleteExtraSkaterHeaps()
{
    for (int i = 1; i < kMaxSkaters; ++i) {
        if (m_skaterRegions[i]) {
            DebugOut::Printf("DELETING SKATER HEAP %d\n", i);
            DeleteHeap(m_skaterHeaps[i]);
            if (m_skaterRegions[i])
                DeleteRegion(m_skaterRegions[i]);
            m_skaterHeaps[i] = nullptr;
            m_skaterRegions[i] = nullptr;
        }
        if (m_skaterGeomRegions[i]) {
            DebugOut::Printf("DELETING SKATER GEOM HEAP %d\n", i);
            DeleteHeap(m_skaterGeomHeaps[i]);
            if (m_skaterGeomRegions[i])
                DeleteRegion(m_skaterGeomRegions[i]);
            m_skaterGeomHeaps[i] = nullptr;
            m_skaterGeomRegions[i] = nullptr;
        }
    }
}

// 0x005BADD0 (the pointers are left as they were)
void Hub::DeleteStandardHeaps()
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
void Hub::DestroyMainHeaps()
{
    CallSlot<void*>(m_topDownHeap, 2, 0u);
    CallSlot<void*>(m_bottomUpHeap, 2, 0u);
    CallSlot<void*>(m_mainRegion, 0, 0u);
}

// --- the global allocation functions (on the current heap) -------------------------------------------------

// 0x005BBAE0
void* AllocBlock(unsigned int size)
{
    return Hub::Pull()->Carve(size, 1, nullptr);
}

// 0x005BBB10
uint32_t MallocAvailable()
{
    return Hub::Pull()->Obtainable();
}

// 0x005BBB30
void* TrimBlockEnd(unsigned int size, void* memory)
{
    void* result = CallSlot<void*>(CurrentHeap(Hub::Pull()), 7, size, memory);
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
void* Regrow(void* memory, unsigned int size)
{
    void* block = nullptr;
    if (size) {
        block = HeapAllocate(CurrentHeap(Hub::Pull()), size, 1);
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

// --- creating the manager and its heaps -----------------------------------------------------------------------

namespace MemSys {

// Where the manager and its main region and heaps live (static storage in the game, as words).
uint32_t g_managerStorage[(sizeof(Hub) + 3) / 4];
uint32_t g_mainRegionStorage[(sizeof(Zone) + 3) / 4];
uint32_t g_topDownStorage[(sizeof(Arena) + 3) / 4];
uint32_t g_bottomUpStorage[(sizeof(Arena) + 3) / 4];

namespace {

void* const kManagerStorage = g_managerStorage;
void* const kMainRegionStorage = g_mainRegionStorage;
void* const kTopDownStorage = g_topDownStorage;
void* const kBottomUpStorage = g_bottomUpStorage;

bool& ManagerReady() { return g_managerReady; }

// The game CRT's malloc (the main block comes from it).
void* CrtMalloc(size_t size) { return Crt::AllocBlock(size); }

MemoryArena* NewAllocRegion(uint32_t size)
{
    void* memory = AllocZeroed(sizeof(MemoryArena));
    return memory ? new (memory) MemoryArena(size) : nullptr;
}

} // namespace

Hub*& Hub::Sole()
{
    return g_memoryManager;
}

uint8_t* g_heapBlockLimit;
uint8_t* g_heapBlockEnd;
uint8_t* g_heapBlockStart;
uintptr_t g_dataEnd;
uintptr_t g_dataStart;
uintptr_t g_codeStart;

// 0x005BBC00
Hub* Hub::MakeNew()
{
    m_ongoing = nullptr;
    for (LabeledZone& named : m_namedHeaps) {
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
    void* region = Clear(sizeof(Zone), kMainRegionStorage);
    m_mainRegion = region ? new (region) Zone(reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(block) + 15) & ~15u),
                                                reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(end) & ~15u))
                          : nullptr;
    void* top = Clear(sizeof(Arena), kTopDownStorage);
    m_topDownHeap = top ? new (top) Arena(m_mainRegion, MemSource::kGrowDown, "Top Down") : nullptr;
    void* bottom = Clear(sizeof(Arena), kBottomUpStorage);
    m_bottomUpHeap = bottom ? new (bottom) Arena(m_mainRegion, MemSource::kGrowUp, "BottomUp") : nullptr;
    m_heaps[1] = m_bottomUpHeap;
    m_heaps[0] = m_topDownHeap;
    m_heapCount = 2;

    // (the code and data bounds are never set in this build: all zero)
    auto codeStart = g_codeStart;
    auto dataStart = g_dataStart;
    auto dataEnd = g_dataEnd;
    uint32_t dataK = static_cast<uint32_t>(dataEnd - dataStart) >> 10;
    uint32_t codeK = static_cast<uint32_t>(dataStart - codeStart) >> 10;
    DebugOut::Printf("code [%p - %p] (%dK) + data [%p - %p] (%dK) = %dK \n", codeStart, dataStart, codeK, dataStart, dataEnd,
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
Arena* Hub::AddHeap(Zone* region, int direction, const char* name)
{
    void* memory = AllocZeroed(sizeof(Arena));
    Arena* heap = memory ? new (memory) Arena(region, direction, name) : nullptr;
    m_heaps[m_heapCount++] = heap;
    return heap;
}

// 0x005BBE80
void Hub::InitStandardHeaps()
{
    m_frontEndRegion = NewAllocRegion(GameConfig::g_configBootstrap ? 1000000 : 1100000);
    DebugOut::Printf("allocated mp_frontend_region at %p\n", m_frontEndRegion);
    m_networkHeap = AddHeap(m_frontEndRegion, MemSource::kGrowDown, "Network");
    DebugOut::Printf("Setup TOP_DOWN mp_network_heap at %p\n", m_networkHeap);
    m_frontEndHeap = AddHeap(m_frontEndRegion, MemSource::kGrowUp, "FrontEnd");
    DebugOut::Printf("Setup mp_frontend_heap at %p\n", m_frontEndHeap);

    // Not original - grown by what the bigger script pools take more (game/core/memory_sizes.h: pool sizes as
    // THUGPRO's; the room left for scripts stays the original's).
    // Original behaviour: 0x434770 bytes.
    m_scriptRegion = NewAllocRegion(Sizes::ScriptRegionSize());
    DebugOut::Printf("allocated mp_script_region at %p\n", m_scriptRegion);
    m_scriptHeap = AddHeap(m_scriptRegion, MemSource::kGrowUp, "Script");
    DebugOut::Printf("Setup mp_script_heap at %p\n", m_scriptHeap);

    m_skaterInfoRegion = NewAllocRegion(82000);
    DebugOut::Printf("allocated mp_skater_info_region at %p\n", m_skaterInfoRegion);
    m_skaterInfoHeap = AddHeap(m_skaterInfoRegion, MemSource::kGrowUp, "skt_info");
    DebugOut::Printf("Setup mp_skater_info_heap at %p\n", m_skaterInfoHeap);

    m_themeRegion = NewAllocRegion(0x3C00);
    DebugOut::Printf("allocated mp_theme_region at %p\n", m_themeRegion);
    m_themeHeap = AddHeap(m_themeRegion, MemSource::kGrowUp, "theme");
    DebugOut::Printf("Setup mp_theme_heap at %p\n", m_themeHeap);

    m_skaterRegions[0] = NewAllocRegion(0x1AFA4);
    DebugOut::Printf("allocated mp_skater_region at %p\n", m_skaterRegions[0]);
    m_skaterHeaps[0] = AddHeap(m_skaterRegions[0], MemSource::kGrowUp, "skater");
    DebugOut::Printf("Setup mp_skater_heap at %p\n", m_skaterHeaps[0]);

    m_skaterGeomRegions[0] = NewAllocRegion(0x73F78);
    DebugOut::Printf("allocated mp_skater_geom_region at %p\n", m_skaterGeomRegions[0]);
    m_skaterGeomHeaps[0] = AddHeap(m_skaterGeomRegions[0], MemSource::kGrowUp, "skater_geom");
    DebugOut::Printf("Setup mp_skater_geom_heap at %p\n", m_skaterGeomHeaps[0]);

    for (int i = 1; i < kMaxSkaters; ++i) {
        m_skaterRegions[i] = nullptr;
        m_skaterHeaps[i] = nullptr;
        m_skaterGeomRegions[i] = nullptr;
        m_skaterGeomHeaps[i] = nullptr;
    }
    CreateLabeledZone(0x0B1BA9DE, 0x16800, "PermanentTextureHeap", 0);
    CreateLabeledZone(0x33B42C96, 0x49C00, "UnloadableTextureHeap", 0);
}

// 0x005BB560 (made with the bottom-up heap current, on the game's manager)
void Hub::CreateNetScratchZone()
{
    if (m_netMiscRegion)
        return;
    Hub* manager = Pull();
    manager->EnterHeapScope(manager->m_bottomUpHeap);
    m_netMiscRegion = NewAllocRegion(160000);
    m_netMiscHeap = AddHeap(m_netMiscRegion, MemSource::kGrowUp, "NetMisc");
    Pull()->LeaveHeapScope();
}

// 0x005BB660 (made with the bottom-up heap current, on the game's manager)
void Hub::InitInternetHeaps()
{
    if (m_internetRegion)
        return;
    Hub* manager = Pull();
    manager->EnterHeapScope(manager->m_bottomUpHeap);
    m_internetRegion = NewAllocRegion(450000);
    m_internetTopDownHeap = AddHeap(m_internetRegion, MemSource::kGrowDown, "InternetTopDown");
    m_internetBottomUpHeap = AddHeap(m_internetRegion, MemSource::kGrowUp, "InternetBottomUp");
    Pull()->LeaveHeapScope();
}

// 0x005BB780
void Hub::InitCutsceneHeaps(uint32_t size)
{
    if (m_cutsceneRegion)
        return;
    m_cutsceneRegion = NewAllocRegion(size);
    m_cutsceneBottomUpHeap = AddHeap(m_cutsceneRegion, MemSource::kGrowUp, "CutsceneBottomUp");
    m_cutsceneTopDownHeap = AddHeap(m_cutsceneRegion, MemSource::kGrowDown, "CutsceneTopDown");
}

// 0x005BB820
void Hub::InitExtraSkaterHeaps(int count)
{
    DebugOut::Printf("Init Skater Heaps\n");
    if (count > kMaxSkaters)
        count = kMaxSkaters;
    for (int i = 1; i < count; ++i) {
        m_skaterRegions[i] = NewAllocRegion(0x1AFA4);
        DebugOut::Printf("EXTRA: allocated mp_skater_region at %p\n", m_skaterRegions[i]);
        m_skaterHeaps[i] = AddHeap(m_skaterRegions[i], MemSource::kGrowUp, "skaterX");
        DebugOut::Printf("EXTRA: Setup mp_skater_heap at %p\n", m_skaterHeaps[i]);
        m_skaterGeomRegions[i] = NewAllocRegion(0x73F78);
        DebugOut::Printf("EXTRA: allocated mp_skater_geom_region at %p\n", m_skaterGeomRegions[i]);
        m_skaterGeomHeaps[i] = AddHeap(m_skaterGeomRegions[i], MemSource::kGrowUp, "skaterGeomX");
        DebugOut::Printf("EXTRA: Setup mp_skater_geom_heap at %p\n", m_skaterGeomHeaps[i]);
    }
    DebugOut::Printf("END Init Skater Heaps\n");
}

// 0x005BB9E0
void Hub::CreateLabeledZone(uint32_t name, uint32_t size, const char* text, int /*unused*/)
{
    LabeledZone* named = nullptr;
    for (LabeledZone& slot : m_namedHeaps) {
        if (!slot.used) {
            named = &slot;
            break;
        }
    }
    // (no free slot: crashes here, as in the original)
    named->name = name;
    std::memcpy(named->text, text, std::strlen(text) + 1); // (unbounded, as in the original)
    named->region = NewAllocRegion(size);
    named->heap = AddHeap(named->region, MemSource::kGrowUp, named->text);
    named->used = true;
}

// 0x005BC2C0
void SetUpManager()
{
    if (Hub::Pull())
        return;
    void* memory = Clear(sizeof(Hub), kManagerStorage);
    Hub* manager = memory ? static_cast<Hub*>(memory)->MakeNew() : nullptr;
    Hub::Sole() = manager;
    manager->EnterHeapScope(manager->m_bottomUpHeap);
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
    Hub* manager = Hub::Pull();
    if (!manager)
        return;
    manager->DeleteStandardHeaps();
    manager->LeaveHeapScope();
    manager->DestroyMainHeaps();
    Hub::Sole() = nullptr;
}

} // namespace Mem
