#pragma once

#include <cstddef>
#include <cstdint>
#include "hook/game_global.h"

namespace Mem {
class Heap;
class Region;
struct PoolPair;
} // namespace Mem

// The park editor's memory budget (0x0059E100-0x0059ED5F): the park editor's own heap, the item pools of its
// map list and clipboard, and the "memory used" figures its menu shows as bars - how full the main heap, the
// top-down heap, the park heap, the script component / vector / array pools and the DMA piece budget are.
// Sizes and pads come from the script (ParkEditorMemoryValue).
namespace Ed {

// The figures (0x007CE618, 0x6C bytes, copied out by GetMemoryStats).
struct MemoryStats {
    int parkHeapUsed;      // +0x00 the park heap's used size
    int parkHeapFree;      // +0x04 its region's space + its free blocks - park_heap_pad
    int mainHeapFree;      // +0x08 the main (bottom-up) heap's, less the pads (or its largest block, see below)
    bool mainHeapFragmented; // +0x0C the main heap's largest block is less than its free space and the pad
    uint8_t pad0D[3];
    float mainHeapUsage;   // +0x10 1 - free / main_heap_base (1 when nothing is free)
    float topDownUsage;    // +0x14 2 - available / top_down_required; 0 unless it's the most
    float parkHeapUsage;   // +0x18 1 - free / park_heap_base
    float componentUsage;  // +0x1C the park's components / the component pool's room (less component_use_base)
    float vectorUsage;     // +0x20 the same for vectors
    float arrayUsage;      // +0x24 the same for arrays (CArray)
    float dmaUsage;        // +0x28 DMA pieces / max_dma_pieces
    float maxUsage;        // +0x2C the largest of them
    int pieceCounts[3];    // +0x30 filled by the park's piece counter (0x00592AB0) - unknown
    int components;        // +0x3C the park's script components
    int vectors;           // +0x40 vectors
    int arrays;            // +0x44 arrays
    int marker48;          // +0x48 (marker 0x9BF91DBE) the main heap's free memory
    int marker4C;          // +0x4C (marker 0xE113C765)
    int outerShellMem;     // +0x50 (markers 0x2D987043 / 0x7DE18688: the front end's use)
    int netAnimsMem;       // +0x54 (markers 0xAFFF3067 / 0x65B63305: the network animations' use)
    int marker58FreeSize;  // +0x58 (marker 0xCC63F828) the main heap's free blocks
    int marker5C;          // +0x5C (marker 0xCC63F828)
    int unknown60;
    int componentsUsedAtMarker; // +0x64 (marker 0x9BF91DBE) the component pool's items in use
    int vectorsUsedAtMarker;    // +0x68 the vector pool's
};
static_assert(sizeof(MemoryStats) == 0x6C && offsetof(MemoryStats, maxUsage) == 0x2C &&
                  offsetof(MemoryStats, components) == 0x3C && offsetof(MemoryStats, marker48) == 0x48 &&
                  offsetof(MemoryStats, vectorsUsedAtMarker) == 0x68,
              "layout must match the original");

MemoryStats& Stats();            // 0x007CE618
Mem::PoolPair& ClipboardPools(); // 0x007CE5B4 (CClipboard's CPoolable pools)
Mem::Region*& ParkRegion();      // 0x007CE684
Mem::Heap*& ParkHeap();          // 0x007CE688
int& DmaPieces();                // 0x007CE690 the DMA pieces the park's pieces use
bool& CountsDirty();             // 0x007CE694 the piece counts need counting again

// 0x0059E100
Mem::Heap* __cdecl GetParkHeap();
// 0x0059E180: the memory manager's bottom-up heap.
Mem::Heap* __cdecl GetMainHeap();
// 0x0059E110: all figures 0, no DMA pieces.
void __cdecl ClearStats();
// 0x0059E170: the piece counts are counted again at the next GetMemoryStats.
void __cdecl SetCountsDirty();
// 0x0059E190 / 0x0059E1E0 / 0x0059E130: a piece's DMA pieces (its type's integer in the global structure
// 0x9547DA8D, 1 if missing) added / taken away (pieces with flag bit 8 don't count; taking away needs bit 2).
void __cdecl AddPieceDma(const void* piece);
void __cdecl RemovePieceDma(const void* piece);
void __cdecl RemoveDma(uint32_t pieceType);
// 0x0059E210 / 0x0059E290 / 0x0059E310: the item pools of CMapListNode (12 bytes, 0x007CE5F0), CClipboardEntry
// (0x18, 0x007CE5C4) and CClipboard (0x18, 0x007CE5B4), made in the current pool slot.
void __cdecl CreateMapListNodePool(int count, const char* name);
void __cdecl CreateClipboardEntryPool(int count, const char* name);
void __cdecl CreateClipboardPool(int count, const char* name);
// 0x0059E390: a memory marker of the script (MemoryMarker-like): note or report the main heap's free memory.
void __cdecl Marker(uint32_t marker);
// 0x0059E4E0: delete the three pools (those it made), the park heap and its region.
void __cdecl ShutDown();
// 0x0059E5C0: a memory figure from the script: run the script 0x3CB03619 once with 0x756F5456=<name's
// checksum> (and 0x7F8C98FE=<index> if index >= 0), then its integer 0x083FDB95.
int __cdecl GetMemoryValue(const char* name, int index);
// 0x0059E730: work out the figures (counting the park's pieces again if they changed) and copy them to `out`.
MemoryStats* __cdecl GetMemoryStats(MemoryStats* out); // (returns out)
// 0x0059EAD0: whether `count` * `size` floor pieces' memory (40000 bytes each) would leave the main heap under 95%
// used (from the last figures).
bool __cdecl MainHeapHasRoom(int count, int size);
// 0x0059EB30: whether pieces `first` to `last` (last - first of them) fit: the DMA budget, the park heap and the
// main heap (floor_piece_size_park / _main each), the main heap left under 90% used.
bool __cdecl PiecesFit(int first, int last);
// 0x0059EC00: the park heap (park_heap_size, its own region in the top-down heap for the level 0xCD3682C1,
// else in the bottom-up one) and the three pools in it; figures cleared.
void __cdecl Init(uint32_t level);

GAME_GLOBAL_EXTERN(Mem::Heap*, g_parkHeap);

} // namespace Ed
