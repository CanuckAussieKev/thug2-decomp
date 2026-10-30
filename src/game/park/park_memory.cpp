// The park editor's memory budget (see park_memory.h).

#include "game/park/park_memory.h"

#include <cstring>
#include <new>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Ed {

GAME_GLOBAL(double, g_parkMemoryFullUsage, 0x0064FA10, 0.95);
GAME_GLOBAL(double, g_parkMemoryNearlyFullUsage, 0x0064FA18, 0.9);
GAME_GLOBAL(MemoryStats, g_parkMemoryStats, 0x007CE618);
GAME_GLOBAL(Mem::Region*, g_parkRegion, 0x007CE684);
GAME_GLOBAL(Mem::Heap*, g_parkHeap, 0x007CE688);
GAME_GLOBAL(int, g_parkMemoryUnknown7CE68C, 0x007CE68C);
GAME_GLOBAL(int, g_dmaPieces, 0x007CE690);
GAME_GLOBAL(bool, g_countsDirty, 0x007CE694);

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
Mem::PoolPair& ComponentPools() { return Script::g_componentPools; }
Mem::PoolPair& VectorPools() { return Script::g_vectorPools; }
Mem::PoolPair& ArrayPools() { return Script::g_arrayPools; }
// The park editor's: CClipboard, CClipboardEntry, CMapListNode. The second word's bytes say whether each pool
// was made here (and is deleted by ShutDown).
Mem::PoolPair& ClipboardEntryPools() { return g_clipboardEntryPools; }
Mem::PoolPair& MapListNodePools() { return g_nodePools; }
uint8_t& PoolMadeHere(Mem::PoolPair& pair) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current]; }
Mem::Pool* CurrentPool(Mem::PoolPair& pair) { return pair.pools[pair.current]; }

int& Unknown7CE68C() { return g_parkMemoryUnknown7CE68C; }

// The park manager and its generator, whose +0x20 is the most players the park is set up for.
Ed::CParkGenerator* ParkGenerator() { return Ed::CParkManager::sInstance()->mp_generator; }
int ParkMaxPlayers() { return ParkGenerator()->GetMaxPlayers(); }

// A heap's free memory: its region's unclaimed space and its free blocks.
int FreeMemory(Mem::Heap* heap) { return heap->m_region->MemAvailable() + heap->m_freeSize; }

void MakePool(Mem::PoolPair& pair, int itemSize, int count, const char* name)
{
    void* memory = Mem::New(sizeof(Mem::Pool));
    Mem::Pool* pool = memory ? new (memory) Mem::Pool(itemSize, count, name) : nullptr;
    pair.pools[pair.current] = pool;
    PoolMadeHere(pair) = 1;
}

void DeletePool(Mem::PoolPair& pair)
{
    Mem::Pool* pool = CurrentPool(pair);
    if (!pool)
        return;
    if (PoolMadeHere(pair)) {
        Mem::Free(pool->m_start); // ~Pool (0x005BA4E0)
        Mem::Manager::Get()->Free(pool);
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
float PoolUsage(Mem::PoolPair& pair, const char* baseName, const int& items)
{
    Mem::Pool* pool = CurrentPool(pair);
    int count = pool->m_count;
    int used = pool->m_used;
    int base = GetMemoryValue(baseName, -1);
    return static_cast<float>(static_cast<double>(items) / (count - used - base));
}

} // namespace

Mem::PoolPair& ClipboardPools() { return g_clipboardPools; }
MemoryStats& Stats() { return g_parkMemoryStats; }
Mem::Region*& ParkRegion() { return g_parkRegion; }
Mem::Heap*& ParkHeap() { return g_parkHeap; }
int& DmaPieces() { return g_dmaPieces; }
bool& CountsDirty() { return g_countsDirty; }

// 0x0059E100
Mem::Heap* __cdecl GetParkHeap() { return ParkHeap(); }

// 0x0059E180
Mem::Heap* __cdecl GetMainHeap() { return Mem::Manager::Get()->m_bottomUpHeap; }

// 0x0059E110
void __cdecl ClearStats()
{
    std::memset(&Stats(), 0, sizeof(MemoryStats));
    DmaPieces() = 0;
}

// 0x0059E170
void __cdecl SetCountsDirty() { CountsDirty() = true; }

// 0x0059E130
void __cdecl RemoveDma(uint32_t pieceType)
{
    Script::CStruct* table = Script::GetStructure(kPieceDmaTable);
    int pieces = 1;
    table->GetInteger(pieceType, &pieces, 0);
    DmaPieces() -= pieces;
}

// 0x0059E190
void __cdecl AddPieceDma(const void* piece)
{
    auto* view = static_cast<const PieceView*>(piece);
    if (view->flags & 0x100)
        return;
    uint32_t type = view->type;
    Script::CStruct* table = Script::GetStructure(kPieceDmaTable);
    int pieces = 1;
    table->GetInteger(type, &pieces, 0);
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
    Mem::Heap* heap = Mem::Manager::Get()->m_bottomUpHeap;
    MemoryStats& stats = Stats();
    switch (marker) {
    case 0xAF6240B2:
        ClearStats();
        break;
    case 0x2D987043:
        stats.outerShellMem = FreeMemory(heap);
        break;
    case 0x7DE18688:
        stats.outerShellMem -= FreeMemory(heap);
        Dbg::Printf(reinterpret_cast<const char*>(0x0064F904), stats.outerShellMem); // "OUTER SHELL MEM = %d !!!..."
        break;
    case 0xAFFF3067:
        stats.netAnimsMem = FreeMemory(heap);
        break;
    case 0x65B63305:
        stats.netAnimsMem -= FreeMemory(heap);
        Dbg::Printf(reinterpret_cast<const char*>(0x0064F930), stats.netAnimsMem); // "NETANIMS MEM = %d !!!..."
        break;
    case 0x9BF91DBE:
        stats.marker48 = FreeMemory(heap);
        stats.componentsUsedAtMarker = CurrentPool(ComponentPools())->m_used;
        stats.vectorsUsedAtMarker = CurrentPool(VectorPools())->m_used;
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
void __cdecl ShutDown()
{
    DeletePool(ClipboardPools());
    DeletePool(ClipboardEntryPools());
    DeletePool(MapListNodePools());
    Mem::Manager::Get()->DeleteHeap(reinterpret_cast<Mem::Allocator*>(ParkHeap()));
    if (Mem::Region* region = ParkRegion()) // its scalar deleting destructor (vtable slot 0)
        reinterpret_cast<void(__thiscall*)(Mem::Region*, int)>(region->m_vtable[0])(region, 1);
    ParkHeap() = nullptr;
    ParkRegion() = nullptr;
}

// 0x0059E5C0
int __cdecl GetMemoryValue(const char* name, int index)
{
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params->Init();
    Script::CStruct* results = Script::NewStruct();
    if (results)
        results->Init();
    int value = 0;
    params->AddChecksum(kValueName, Crc::GenerateCRCFromString(name));
    if (index > -1)
        params->AddInteger(kValueIndex, index);
    Script::RunScriptOnce(kMemoryValueScript, params, nullptr, results);
    results->GetInteger(kValueResult, &value, 1);
    if (results) {
        results->Clear();
        Script::DeleteStruct(results);
    }
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    return value;
}

// 0x0059E730
MemoryStats* __cdecl GetMemoryStats(MemoryStats* out)
{
    MemoryStats& stats = Stats();
    if (CountsDirty()) {
        CountsDirty() = false;
        ParkGenerator()->GetNodeCounts(stats.pieceCounts);
    }
    Mem::Heap* mainHeap = Mem::Manager::Get()->m_bottomUpHeap;
    Mem::Region* mainRegion = mainHeap->m_region;
    // The main heap's pad grows by 0x8EF1C bytes for each player after the first.
    int mainPad = GetMemoryValue(reinterpret_cast<const char*>(0x0064FA00), -1); // "main_heap_pad"
    mainPad += ParkMaxPlayers() * 0x8EF1C - 0x8EF1C;
    stats.mainHeapFree = mainRegion->MemAvailable() + mainHeap->m_freeSize;
    stats.mainHeapFree -= GetMemoryValue(reinterpret_cast<const char*>(0x0064F9F0), -1) + mainPad; // "anim_set_mem"
    int parkPad = GetMemoryValue(reinterpret_cast<const char*>(0x0064F9E0), -1); // "park_heap_pad"
    stats.parkHeapUsed = ParkHeap()->m_usedSize;
    stats.parkHeapFree = ParkRegion()->MemAvailable() + (ParkHeap()->m_freeSize - parkPad);

    // Fragmented: the largest block is all that can be counted on.
    if (static_cast<int>(mainHeap->LargestFree()) < stats.mainHeapFree &&
        static_cast<int>(mainHeap->LargestFree()) < mainPad && stats.mainHeapFree >= mainPad) {
        stats.mainHeapFree = static_cast<int>(mainHeap->LargestFree());
        stats.mainHeapFragmented = true;
    } else {
        stats.mainHeapFragmented = false;
    }
    int theme = Ed::CParkManager::sInstance()->GetShellSize();
    stats.mainHeapFree -= GetMemoryValue(reinterpret_cast<const char*>(0x0064F9D4), theme); // "theme_pad"

    float mainUsage = 1.0f;
    if (stats.mainHeapFree > 0) {
        float free = static_cast<float>(stats.mainHeapFree);
        int base = GetMemoryValue(reinterpret_cast<const char*>(0x0064F9C4), -1); // "main_heap_base"
        mainUsage = static_cast<float>(1.0 - static_cast<double>(free) / base);
    }
    stats.mainHeapUsage = mainUsage;
    stats.maxUsage = mainUsage;

    float parkFree = static_cast<float>(stats.parkHeapFree);
    double park = 1.0 - static_cast<double>(parkFree) /
                            GetMemoryValue(reinterpret_cast<const char*>(0x0064F9B4), -1); // "park_heap_base"
    stats.parkHeapUsage = static_cast<float>(park);
    UpdateMax(stats, park, stats.parkHeapUsage);

    float topDownFree = static_cast<float>(Mem::Manager::Get()->m_topDownHeap->m_region->MemAvailable());
    double topDown = 2.0 - static_cast<double>(topDownFree) /
                               GetMemoryValue(reinterpret_cast<const char*>(0x0064F9A0), -1); // "top_down_required"
    stats.topDownUsage = static_cast<float>(topDown);
    if (topDown > stats.maxUsage)
        stats.maxUsage = stats.topDownUsage;
    else
        stats.topDownUsage = 0.0f;

    // The pools: compared after rounding.
    stats.componentUsage = PoolUsage(ComponentPools(), reinterpret_cast<const char*>(0x0064F98C), stats.components); // "component_use_base"
    if (stats.maxUsage < stats.componentUsage)
        stats.maxUsage = stats.componentUsage;
    stats.vectorUsage = PoolUsage(VectorPools(), reinterpret_cast<const char*>(0x0064F97C), stats.vectors); // "vector_use_base"
    if (stats.maxUsage < stats.vectorUsage)
        stats.maxUsage = stats.vectorUsage;
    stats.arrayUsage = PoolUsage(ArrayPools(), reinterpret_cast<const char*>(0x0064F96C), stats.arrays); // "CArray_use_base"
    if (stats.maxUsage < stats.arrayUsage)
        stats.maxUsage = stats.arrayUsage;

    float dma = static_cast<float>(DmaPieces());
    double dmaUsage = static_cast<double>(dma) / GetMemoryValue(reinterpret_cast<const char*>(0x0064F95C), -1); // "max_dma_pieces"
    stats.dmaUsage = static_cast<float>(dmaUsage);
    UpdateMax(stats, dmaUsage, stats.dmaUsage);

    *out = stats;
    return out;
}

// 0x0059EAD0
bool __cdecl MainHeapHasRoom(int count, int size)
{
    int free = Stats().mainHeapFree - count * size * kFloorPieceMem;
    double usage = Usage(1.0f, free, GetMemoryValue(reinterpret_cast<const char*>(0x0064F9C4), -1)); // "main_heap_base"
    return !(usage >= g_parkMemoryFullUsage); // 0.95 (unordered: true)
}

// 0x0059EB30
bool __cdecl PiecesFit(int first, int last)
{
    int dma = DmaPieces();
    int pieces = last - first;
    if (pieces > GetMemoryValue(reinterpret_cast<const char*>(0x0064F95C), -1) - dma) // "max_dma_pieces"
        return false;
    int park = GetMemoryValue(reinterpret_cast<const char*>(0x0064FA38), -1) * pieces; // "floor_piece_size_park"
    int main = GetMemoryValue(reinterpret_cast<const char*>(0x0064FA20), -1) * pieces; // "floor_piece_size_main"
    MemoryStats stats;
    GetMemoryStats(&stats);
    if (Stats().parkHeapFree < park || Stats().mainHeapFree < main)
        return false;
    double usage = Usage(1.0f, Stats().mainHeapFree - main,
                         GetMemoryValue(reinterpret_cast<const char*>(0x0064F9C4), -1)); // "main_heap_base"
    return !(usage >= g_parkMemoryNearlyFullUsage); // 0.9 (unordered: true)
}

// 0x0059EC00
void __cdecl Init(uint32_t level)
{
    Mem::Manager* manager = Mem::Manager::Get();
    const char* name = reinterpret_cast<const char*>(0x0064FA7C); // "Park Editor"
    int direction;
    if (level == kTopDownLevel) {
        manager->PushContext(manager->m_topDownHeap);
        direction = -1;
    } else {
        manager->PushContext(manager->m_bottomUpHeap);
        direction = 1;
    }
    void* memory = Mem::Calloc(sizeof(Mem::AllocRegion));
    Mem::AllocRegion* region = nullptr;
    if (memory) {
        int size = GetMemoryValue(reinterpret_cast<const char*>(0x0064FA88), -1); // "park_heap_size"
        region = new (memory) Mem::AllocRegion(static_cast<uint32_t>(size));
    }
    ParkRegion() = region;
    ParkHeap() = manager->AddHeap(region, direction, name);
    CreateMapListNodePool(16000, reinterpret_cast<const char*>(0x0064FA6C)); // "CMapListNode"
    Dbg::Printf(reinterpret_cast<const char*>(0x0064F8F4), CurrentPool(MapListNodePools())); // "pool is at %p\n"
    CreateClipboardEntryPool(1000, reinterpret_cast<const char*>(0x0064FA5C)); // "CClipboardEntry"
    CreateClipboardPool(6, reinterpret_cast<const char*>(0x0064FA50));         // "CClipboard"
    manager->PopContext();
    Unknown7CE68C() = 0;
    DmaPieces() = 0;
    std::memset(&Stats(), 0, sizeof(MemoryStats));
}

} // namespace Ed

REPLACE(0x0059E100, Ed::GetParkHeap);
REPLACE(0x0059E110, Ed::ClearStats);
REPLACE(0x0059E130, Ed::RemoveDma);
REPLACE(0x0059E170, Ed::SetCountsDirty);
REPLACE(0x0059E180, Ed::GetMainHeap);
REPLACE(0x0059E190, Ed::AddPieceDma);
REPLACE(0x0059E1E0, Ed::RemovePieceDma);
REPLACE(0x0059E210, Ed::CreateMapListNodePool);
REPLACE(0x0059E290, Ed::CreateClipboardEntryPool);
REPLACE(0x0059E310, Ed::CreateClipboardPool);
REPLACE(0x0059E390, Ed::Marker);
REPLACE(0x0059E4E0, Ed::ShutDown);
REPLACE(0x0059E5C0, Ed::GetMemoryValue);
REPLACE(0x0059E730, Ed::GetMemoryStats);
REPLACE(0x0059EAD0, Ed::MainHeapHasRoom);
REPLACE(0x0059EB30, Ed::PiecesFit);
REPLACE(0x0059EC00, Ed::Init);
