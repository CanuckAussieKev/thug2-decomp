#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/arena_alloc.h"

// The memory manager: owns the game's regions and heaps, keeps a stack of "current" heaps (where allocations
// without an explicit heap go), and finds heaps by name checksum. One instance (0x007D1800).
namespace MemSys {

class Hub {
public:
    // A heap created at run time by name (CFunc_MemInitHeap and friends).
    struct LabeledZone {
        Zone* region;
        Arena* heap;
        uint32_t name;    // checksum
        char text[0x80];  // the name
        bool used;
        uint8_t pad[3];
    };
    static_assert(sizeof(LabeledZone) == 0x90, "layout must match the original");
    static constexpr int kMaxHeaps = 48;
    static constexpr int kMaxNamedHeaps = 7;
    static constexpr int kSkaterSlots = 8;

    static Hub* Pull();
    static Hub*& Sole();

    // 0x005BBC00: the 64 MB block (ours bigger: game/core/memory_sizes.h), the main region and its two heaps (in static storage).
    Hub* MakeNew();
    // The rest of the standard heaps (front end, network, script, skater info, theme, skater, named texture heaps).
    void InitStandardHeaps();
    // A new heap in `region`, registered.
    Arena* AddHeap(Zone* region, int direction, const char* name);
    void CreateNetScratchZone();
    void InitInternetHeaps();
    void InitCutsceneHeaps(uint32_t size);
    // Heaps for skaters 1 to count-1 (multiplayer; at most 8 skaters).
    void InitExtraSkaterHeaps(int count);
    // A heap of its own region, found by name checksum.
    void CreateLabeledZone(uint32_t name, uint32_t size, const char* text, int unused);

    // Allocation through a heap (null: the current one); `haltIfExhausted` reaches the heap as is.
    void* Carve(uint32_t size, int haltIfExhausted, MemSource* heap);
    void* AllocateZeroed(uint32_t size, int haltIfExhausted, MemSource* heap);
    void* ExtendBlockUpward(uint32_t size, void* block, MemSource* heap);
    void* TrimBlockEnd(uint32_t size, void* block, MemSource* heap);
    void Dealloc(void* block);
    // The current heap's largest possible allocation.
    uint32_t Obtainable();

    // The current-heap stack.
    void EnterHeapScope(MemSource* heap);
    void LeaveHeapScope();
    const char* CurrentHeapName();

    // The registry of all heaps.
    MemSource* FirstZone();
    MemSource* FollowingZone(MemSource* heap);
    // Take it out of the registry and delete it.
    void DeleteHeap(MemSource* heap);

    // Heaps by name checksum.
    LabeledZone* FindNamedHeap(uint32_t name);
    Arena* FetchHeapByKey(uint32_t name, int unused);
    bool ReleaseLabeledZone(uint32_t name, int unused);
    Arena* GetMemoryPool(uint32_t name);
    const char* GetZoneLabel(uint32_t name);

    // Deleting the optional heaps.
    void ReleaseNetScratchZone();
    void DeleteCutsceneHeaps();
    void DeleteExtraSkaterHeaps();
    void DeleteStandardHeaps();
    // The three that are constructed in place (not freed): the main heaps and region.
    void DestroyMainHeaps();

    MemSource* m_heaps[kMaxHeaps];
    int m_heapCount;
    MemSource** m_ongoing;            // the top of m_allocSourceStack
    MemSource* m_allocSourceStack[16];
    int m_contextDepth;
    Zone* m_primaryZone;
    Arena* m_topDownHeap;
    Arena* m_bottomUpHeap;
    Zone* m_frontEndRegion;
    Arena* m_frontEndHeap;
    Zone* m_scriptRegion;
    Arena* m_scriptHeap;
    Zone* m_multiplayerZone;
    Arena* m_networkHeap;
    Zone* m_networkExtrasZone;
    Arena* m_netMiscHeap;
    Zone* m_onlineZone;
    Arena* m_internetTopDownHeap;
    Arena* m_internetBottomUpHeap;
    Zone* m_cinematicZone;
    Arena* m_cinematicHighHeap;
    Arena* m_cinematicLowHeap;
    Zone* m_devToolsZone;
    Arena* m_debugHeap;
    uint32_t m_unknown158[2];
    Zone* m_skaterRegions[kSkaterSlots];
    Arena* m_skaterHeaps[kSkaterSlots];
    Zone* m_skaterGeomRegions[kSkaterSlots];
    Arena* m_skaterGeomHeaps[kSkaterSlots];
    LabeledZone m_namedHeaps[kMaxNamedHeaps];
    Zone* m_skaterInfoRegion;
    Arena* m_skaterInfoHeap;
    Zone* m_themeRegion;
    Arena* m_themeHeap;
    uint32_t m_unknown5E0;
    uint32_t m_unknown5E4;
};
static_assert(offsetof(Hub, m_heapCount) == 0xC0 && offsetof(Hub, m_contextDepth) == 0x108 &&
                  offsetof(Hub, m_debugHeap) == 0x154 && offsetof(Hub, m_skaterRegions) == 0x160 &&
                  offsetof(Hub, m_namedHeaps) == 0x1E0 && offsetof(Hub, m_skaterInfoRegion) == 0x5D0 &&
                  offsetof(Hub, m_themeHeap) == 0x5DC && sizeof(Hub) == 0x5E8,
              "layout must match the original");

// 0x005BC2C0: create the manager (on the first allocation), with the bottom-up heap current.
void SetUpManager();
// 0x004E1950: SetUpManager, once.
int EnsureManager();
// 0x005BC340
void ShutDownManager();

extern bool g_managerReady;
} // namespace Mem
