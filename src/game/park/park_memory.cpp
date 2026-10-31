// The park editor's memory budget (see park_memory.h).

#include "game/park/park_memory.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/arena_alloc.h"
#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/ed/park.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"

namespace ParkEditing {

double g_parkMemoryFullUsage{0.95};
double g_parkMemoryNearlyFullUsage{0.9};
MemoryStats g_parkMemoryStats;
MemSys::Zone* g_parkRegion;
MemSys::Arena* g_parkHeap;
int g_parkMemoryUnknown7CE68C;
int g_dmaPieces;
bool g_countsDirty;

namespace {

constexpr uint32_t kPieceDmaTable = 0x9547DA8D; // the global structure of DMA pieces per piece type
constexpr uint32_t kMemoryValueScript = 0x3CB03619;
constexpr uint32_t kValueName = 0x756F5456;
constexpr uint32_t kValueIndex = 0x7F8C98FE;
constexpr uint32_t kValueResult = 0x083FDB95;
constexpr uint32_t kTopDownLevel = 0xCD3682C1; // the level whose park heap is in the top-down heap
constexpr int kFloorPieceMem = 40000;

// A park piece as these functions see it (the park editor's piece class, not decompiled yet).
struct PieceView {
    uint8_t unknown0[0xC];
    uint32_t flags;    // +0x0C bit 2: its DMA pieces were counted; bit 8: they don't count
    uint8_t unknown10[0x28];
    uint32_t type;     // +0x38 the piece type's checksum
};

// The script's object pools (CPoolable pairs): components, vectors, arrays.
MemSys::PoolPair& ComponentPools() { return Scripts::g_componentPools; }
MemSys::PoolPair& VectorPools() { return Scripts::g_vectorPools; }
MemSys::PoolPair& ArrayPools() { return Scripts::g_arrayPools; }
// The park editor's: CClipboard, CClipboardEntry, CMapListNode. The second word's bytes say whether each pool
// was made here (and is deleted by PowerDown).
MemSys::PoolPair& ClipboardEntryPools() { return g_clipboardEntryPools; }
MemSys::PoolPair& MapListNodePools() { return g_nodePools; }
uint8_t& PoolMadeHere(MemSys::PoolPair& pair) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current]; }
MemSys::SlabBank* CurrentPool(MemSys::PoolPair& pair) { return pair.pools[pair.current]; }

int& Unknown7CE68C() { return g_parkMemoryUnknown7CE68C; }

// The park manager and its generator, whose +0x20 is the most players the park is set up for.
ParkEditing::CParkAssembler* ParkGenerator() { return ParkEditing::CParkLayout::sSingleton()->m_builder; }
int ParkMaxPlayers() { return ParkGenerator()->GetPlayerLimit(); }

// A heap's free memory: its region's unclaimed space and its free blocks.
int FreeMemory(MemSys::Arena* heap) { return heap->m_region->FreeByteCount() + heap->m_freeSize; }

void MakePool(MemSys::PoolPair& pair, int itemSize, int count, const char* name)
{
    void* memory = MemSys::Alloc(sizeof(MemSys::SlabBank));
    MemSys::SlabBank* pool = memory ? new (memory) MemSys::SlabBank(itemSize, count, name) : nullptr;
    pair.pools[pair.current] = pool;
    PoolMadeHere(pair) = 1;
}

void DeletePool(MemSys::PoolPair& pair)
{
    MemSys::SlabBank* pool = CurrentPool(pair);
    if (!pool)
        return;
    if (PoolMadeHere(pair)) {
        MemSys::Dealloc(pool->m_outset); // ~Pool (0x005BA4E0)
        MemSys::Hub::Pull()->Dealloc(pool);
    }
    pair.pools[pair.current] = nullptr;
}

// 1 - used fraction style figures: the x87 code rounds the free amount to a float, divides in double.
double Usage(float base, int free, int total) { return base - static_cast<double>(static_cast<float>(free)) / total; }

void UpdateMax(MemoryStats& stats, double value, float stored)
{
    if (value > stats.maxUsage) // (compared before rounding)
        stats.maxUsage = stored;
}

// A pool's figure: the park's items over the pool's room left (less a base the script gives); the pool is read
// before the script runs, the park's count after.
float PoolUsage(MemSys::PoolPair& pair, const char* baseName, const int& items)
{
    MemSys::SlabBank* pool = CurrentPool(pair);
    int count = pool->m_num;
    int used = pool->m_takenCount;
    int base = GetMemoryValue(baseName, -1);
    return static_cast<float>(static_cast<double>(items) / (count - used - base));
}

} // namespace

MemSys::PoolPair& ClipboardPools() { return g_clipboardPools; }
MemoryStats& Attributes() { return g_parkMemoryStats; }
MemSys::Zone*& ParkRegion() { return g_parkRegion; }
MemSys::Arena*& ParkHeap() { return g_parkHeap; }
int& DmaPieces() { return g_dmaPieces; }
bool& CountsDirty() { return g_countsDirty; }

// 0x0059E100
MemSys::Arena* __cdecl GetParkHeap() { return ParkHeap(); }

// 0x0059E180
MemSys::Arena* __cdecl GetMainHeap() { return MemSys::Hub::Pull()->m_bottomUpHeap; }

// 0x0059E110
void __cdecl ClearStats()
{
    std::memset(&Attributes(), 0, sizeof(MemoryStats));
    DmaPieces() = 0;
}

// 0x0059E170
void __cdecl SetCountsDirty() { CountsDirty() = true; }

// 0x0059E130
void __cdecl RemoveDma(uint32_t segmentKind)
{
    Scripts::CStruct* table = Scripts::ReadSubStruct(kPieceDmaTable);
    int pieces = 1;
    table->FetchIntValue(segmentKind, &pieces, 0);
    DmaPieces() -= pieces;
}

// 0x0059E190
void __cdecl AddPieceDma(const void* piece)
{
    auto* view = static_cast<const PieceView*>(piece);
    if (view->flags & 0x100)
        return;
    uint32_t type = view->type;
    Scripts::CStruct* table = Scripts::ReadSubStruct(kPieceDmaTable);
    int pieces = 1;
    table->FetchIntValue(type, &pieces, 0);
    DmaPieces() += pieces;
}

// 0x0059E1E0
void __cdecl RemovePieceDma(const void* piece)
{
    auto* view = static_cast<const PieceView*>(piece);
    if (!(view->flags & 0x100) && (view->flags & 0x4))
        RemoveDma(view->type);
}

// 0x0059E210
void __cdecl CreateMapListNodePool(int count, const char* name) { MakePool(MapListNodePools(), 0xC, count, name); }

// 0x0059E290
void __cdecl CreateClipboardEntryPool(int count, const char* name)
{
    MakePool(ClipboardEntryPools(), 0x18, count, name);
}

// 0x0059E310
void __cdecl CreateClipboardPool(int count, const char* name) { MakePool(ClipboardPools(), 0x18, count, name); }

// 0x0059E390
void __cdecl Marker(uint32_t marker)
{
    MemSys::Arena* heap = MemSys::Hub::Pull()->m_bottomUpHeap;
    MemoryStats& stats = Attributes();
    switch (marker) {
    case 0xAF6240B2:
        ClearStats();
        break;
    case 0x2D987043:
        stats.outerShellMem = FreeMemory(heap);
        break;
    case 0x7DE18688:
        stats.outerShellMem -= FreeMemory(heap);
        DebugOut::Printf("OUTER SHELL MEM = %d !!!!!!!!!!!!!!!!!!!!!\n", stats.outerShellMem); // "OUTER SHELL MEM = %d !!!..."
        break;
    case 0xAFFF3067:
        stats.netAnimsMem = FreeMemory(heap);
        break;
    case 0x65B63305:
        stats.netAnimsMem -= FreeMemory(heap);
        DebugOut::Printf("NETANIMS MEM = %d !!!!!!!!!!!!!!!!!!!!!\n", stats.netAnimsMem); // "NETANIMS MEM = %d !!!..."
        break;
    case 0x9BF91DBE:
        stats.marker48 = FreeMemory(heap);
        stats.componentsUsedAtMarker = CurrentPool(ComponentPools())->m_takenCount;
        stats.vectorsUsedAtMarker = CurrentPool(VectorPools())->m_takenCount;
        break;
    case 0xCC63F828:
        stats.marker5C = FreeMemory(heap);
        stats.marker58FreeSize = heap->m_freeSize;
        break;
    case 0xE113C765:
        stats.marker4C = FreeMemory(heap);
        break;
    default:
        break;
    }
}

// 0x0059E4E0
void __cdecl PowerDown()
{
    DeletePool(ClipboardPools());
    DeletePool(ClipboardEntryPools());
    DeletePool(MapListNodePools());
    MemSys::Hub::Pull()->DeleteHeap(reinterpret_cast<MemSys::MemSource*>(ParkHeap()));
    if (MemSys::Zone* region = ParkRegion()) // its scalar deleting destructor (vtable slot 0)
        reinterpret_cast<void(__thiscall*)(MemSys::Zone*, int)>(region->m_vtable[0])(region, 1);
    ParkHeap() = nullptr;
    ParkRegion() = nullptr;
}

// 0x0059E5C0
int __cdecl GetMemoryValue(const char* name, int index)
{
    Scripts::CStruct* params = Scripts::NewStruct();
    if (params)
        params->PrepareAll();
    Scripts::CStruct* results = Scripts::NewStruct();
    if (results)
        results->PrepareAll();
    int value = 0;
    params->PutNameKey(kValueName, Checksums::HashFromText(name));
    if (index > -1)
        params->PutInt(kValueIndex, index);
    Scripts::RunScriptOnce(kMemoryValueScript, params, nullptr, results);
    results->FetchIntValue(kValueResult, &value, 1);
    if (results) {
        results->Clear();
        Scripts::DeleteStruct(results);
    }
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    return value;
}

// 0x0059E730
MemoryStats* __cdecl GetMemoryStats(MemoryStats* out)
{
    MemoryStats& stats = Attributes();
    if (CountsDirty()) {
        CountsDirty() = false;
        ParkGenerator()->GetNodeCounts(stats.pieceCounts);
    }
    MemSys::Arena* mainHeap = MemSys::Hub::Pull()->m_bottomUpHeap;
    MemSys::Zone* mainRegion = mainHeap->m_region;
    // The main heap's pad grows by 0x8EF1C bytes for each player after the first.
    int mainPad = GetMemoryValue("main_heap_pad", -1); // "main_heap_pad"
    mainPad += ParkMaxPlayers() * 0x8EF1C - 0x8EF1C;
    stats.mainHeapFree = mainRegion->FreeByteCount() + mainHeap->m_freeSize;
    stats.mainHeapFree -= GetMemoryValue("anim_set_mem", -1) + mainPad; // "anim_set_mem"
    int parkPad = GetMemoryValue("park_heap_pad", -1); // "park_heap_pad"
    stats.parkHeapUsed = ParkHeap()->m_usedSize;
    stats.parkHeapFree = ParkRegion()->FreeByteCount() + (ParkHeap()->m_freeSize - parkPad);

    // Fragmented: the largest block is all that can be counted on.
    if (static_cast<int>(mainHeap->LargestFree()) < stats.mainHeapFree &&
        static_cast<int>(mainHeap->LargestFree()) < mainPad && stats.mainHeapFree >= mainPad) {
        stats.mainHeapFree = static_cast<int>(mainHeap->LargestFree());
        stats.mainHeapFragmented = true;
    } else {
        stats.mainHeapFragmented = false;
    }
    int theme = ParkEditing::CParkLayout::sSingleton()->GetShellSize();
    stats.mainHeapFree -= GetMemoryValue("theme_pad", theme); // "theme_pad"

    float mainUsage = 1.0f;
    if (stats.mainHeapFree > 0) {
        float free = static_cast<float>(stats.mainHeapFree);
        int base = GetMemoryValue("main_heap_base", -1); // "main_heap_base"
        mainUsage = static_cast<float>(1.0 - static_cast<double>(free) / base);
    }
    stats.mainHeapUsage = mainUsage;
    stats.maxUsage = mainUsage;

    float parkFree = static_cast<float>(stats.parkHeapFree);
    double park = 1.0 - static_cast<double>(parkFree) /
                            GetMemoryValue("park_heap_base", -1); // "park_heap_base"
    stats.parkHeapUsage = static_cast<float>(park);
    UpdateMax(stats, park, stats.parkHeapUsage);

    float topDownFree = static_cast<float>(MemSys::Hub::Pull()->m_topDownHeap->m_region->FreeByteCount());
    double topDown = 2.0 - static_cast<double>(topDownFree) /
                               GetMemoryValue("top_down_required", -1); // "top_down_required"
    stats.topDownUsage = static_cast<float>(topDown);
    if (topDown > stats.maxUsage)
        stats.maxUsage = stats.topDownUsage;
    else
        stats.topDownUsage = 0.0f;

    // The pools: compared after rounding.
    stats.componentUsage = PoolUsage(ComponentPools(), "component_use_base", stats.components); // "component_use_base"
    if (stats.maxUsage < stats.componentUsage)
        stats.maxUsage = stats.componentUsage;
    stats.vectorUsage = PoolUsage(VectorPools(), "vector_use_base", stats.vectors); // "vector_use_base"
    if (stats.maxUsage < stats.vectorUsage)
        stats.maxUsage = stats.vectorUsage;
    stats.arrayUsage = PoolUsage(ArrayPools(), "CArray_use_base", stats.arrays); // "CArray_use_base"
    if (stats.maxUsage < stats.arrayUsage)
        stats.maxUsage = stats.arrayUsage;

    float dma = static_cast<float>(DmaPieces());
    double dmaUsage = static_cast<double>(dma) / GetMemoryValue("max_dma_pieces", -1); // "max_dma_pieces"
    stats.dmaUsage = static_cast<float>(dmaUsage);
    UpdateMax(stats, dmaUsage, stats.dmaUsage);

    *out = stats;
    return out;
}

// 0x0059EAD0
bool __cdecl MainHeapHasRoom(int count, int size)
{
    int free = Attributes().mainHeapFree - count * size * kFloorPieceMem;
    double usage = Usage(1.0f, free, GetMemoryValue("main_heap_base", -1)); // "main_heap_base"
    return !(usage >= g_parkMemoryFullUsage); // 0.95 (unordered: true)
}

// 0x0059EB30
bool __cdecl PiecesFit(int first, int last)
{
    int dma = DmaPieces();
    int pieces = last - first;
    if (pieces > GetMemoryValue("max_dma_pieces", -1) - dma) // "max_dma_pieces"
        return false;
    int park = GetMemoryValue("floor_piece_size_park", -1) * pieces; // "floor_piece_size_park"
    int main = GetMemoryValue("floor_piece_size_main", -1) * pieces; // "floor_piece_size_main"
    MemoryStats stats;
    GetMemoryStats(&stats);
    if (Attributes().parkHeapFree < park || Attributes().mainHeapFree < main)
        return false;
    double usage = Usage(1.0f, Attributes().mainHeapFree - main,
                         GetMemoryValue("main_heap_base", -1)); // "main_heap_base"
    return !(usage >= g_parkMemoryNearlyFullUsage); // 0.9 (unordered: true)
}

// 0x0059EC00
void __cdecl PrepareAll(uint32_t level)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    const char* name = "Park Editor"; // "Park Editor"
    int direction;
    if (level == kTopDownLevel) {
        manager->EnterHeapScope(manager->m_topDownHeap);
        direction = -1;
    } else {
        manager->EnterHeapScope(manager->m_bottomUpHeap);
        direction = 1;
    }
    void* memory = MemSys::AllocZeroed(sizeof(MemSys::MemoryArena));
    MemSys::MemoryArena* region = nullptr;
    if (memory) {
        int size = GetMemoryValue("park_heap_size", -1); // "park_heap_size"
        region = new (memory) MemSys::MemoryArena(static_cast<uint32_t>(size));
    }
    ParkRegion() = region;
    ParkHeap() = manager->AddHeap(region, direction, name);
    CreateMapListNodePool(16000, "CMapListNode"); // "CMapListNode"
    DebugOut::Printf("pool is at %p\n", CurrentPool(MapListNodePools())); // "pool is at %p\n"
    CreateClipboardEntryPool(1000, "CClipboardEntry"); // "CClipboardEntry"
    CreateClipboardPool(6, "CClipboard");         // "CClipboard"
    manager->LeaveHeapScope();
    Unknown7CE68C() = 0;
    DmaPieces() = 0;
    std::memset(&Attributes(), 0, sizeof(MemoryStats));
}

} // namespace Ed
