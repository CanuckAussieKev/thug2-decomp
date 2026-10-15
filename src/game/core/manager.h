#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// The memory manager: owns the game's regions and heaps, keeps a stack of "current" heaps (where allocations
// without an explicit heap go), and finds heaps by name checksum. One instance (0x007D1800).
namespace Mem {

class Manager {
public:
    // A heap created at run time by name (CFunc_MemInitHeap and friends).
    struct NamedHeap {
        Region* region;
        Heap* heap;
        uint32_t name;    // checksum
        char text[0x80];  // the name
        bool used;
        uint8_t pad[3];
    };
    static_assert(sizeof(NamedHeap) == 0x90, "layout must match the original");
    static constexpr int kMaxHeaps = 48;
    static constexpr int kMaxNamedHeaps = 7;
    static constexpr int kMaxSkaters = 8;

    static Manager* Get();

    // Allocation through a heap (null: the current one); `assertOnFail` reaches the heap as is.
    void* Allocate(uint32_t size, int assertOnFail, Allocator* heap);
    void* AllocateZeroed(uint32_t size, int assertOnFail, Allocator* heap);
    void* ReallocateUp(uint32_t size, void* block, Allocator* heap);
    void* ReallocateShrink(uint32_t size, void* block, Allocator* heap);
    void Free(void* block);
    // The current heap's largest possible allocation.
    uint32_t Available();

    // The current-heap stack.
    void PushContext(Allocator* heap);
    void PopContext();
    const char* CurrentHeapName();

    // The registry of all heaps.
    Allocator* FirstHeap();
    Allocator* NextHeap(Allocator* heap);
    // Take it out of the registry and delete it.
    void DeleteHeap(Allocator* heap);

    // Heaps by name checksum.
    NamedHeap* FindNamedHeap(uint32_t name);
    Heap* GetNamedHeap(uint32_t name, int unused);
    bool DeleteNamedHeap(uint32_t name, int unused);
    Heap* GetHeap(uint32_t name);
    const char* GetHeapName(uint32_t name);

    // Deleting the optional heaps.
    void DeleteNetMiscHeap();
    void DeleteCutsceneHeaps();
    void DeleteExtraSkaterHeaps();
    void DeleteStandardHeaps();
    // The three that are constructed in place (not freed): the main heaps and region.
    void DestroyMainHeaps();

    Allocator* m_heaps[kMaxHeaps];
    int m_heapCount;
    Allocator** m_current;            // the top of m_contextStack
    Allocator* m_contextStack[16];
    int m_contextDepth;
    Region* m_mainRegion;
    Heap* m_topDownHeap;
    Heap* m_bottomUpHeap;
    Region* m_frontEndRegion;
    Heap* m_frontEndHeap;
    Region* m_scriptRegion;
    Heap* m_scriptHeap;
    Region* m_networkRegion;
    Heap* m_networkHeap;
    Region* m_netMiscRegion;
    Heap* m_netMiscHeap;
    Region* m_internetRegion;
    Heap* m_internetTopDownHeap;
    Heap* m_internetBottomUpHeap;
    Region* m_cutsceneRegion;
    Heap* m_cutsceneTopDownHeap;
    Heap* m_cutsceneBottomUpHeap;
    Region* m_debugRegion;
    Heap* m_debugHeap;
    uint32_t m_unknown158[2];
    Region* m_skaterRegions[kMaxSkaters];
    Heap* m_skaterHeaps[kMaxSkaters];
    Region* m_skaterGeomRegions[kMaxSkaters];
    Heap* m_skaterGeomHeaps[kMaxSkaters];
    NamedHeap m_namedHeaps[kMaxNamedHeaps];
    Region* m_skaterInfoRegion;
    Heap* m_skaterInfoHeap;
    Region* m_themeRegion;
    Heap* m_themeHeap;
};
static_assert(offsetof(Manager, m_heapCount) == 0xC0 && offsetof(Manager, m_contextDepth) == 0x108 &&
                  offsetof(Manager, m_debugHeap) == 0x154 && offsetof(Manager, m_skaterRegions) == 0x160 &&
                  offsetof(Manager, m_namedHeaps) == 0x1E0 && offsetof(Manager, m_skaterInfoRegion) == 0x5D0 &&
                  offsetof(Manager, m_themeHeap) == 0x5DC,
              "layout must match the original");

} // namespace Mem
