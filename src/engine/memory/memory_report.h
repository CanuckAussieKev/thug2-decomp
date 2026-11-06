#pragma once

// NOT ORIGINAL - what a level costs the memory manager and the process, logged once a level is up (next to the
// script pools' use, game/front_end/start_level.cpp) so converters can measure how much a level takes:
//   memory in <level>: main block <size> KB, free <now> KB (least <low-water mark> KB); heaps: <name> <used> KB
//   (peak <peak> KB) ...; process: private <committed> MB, address space free <total> MB (largest <block> MB)
// The main block's heaps (top down / bottom up) hold the level's scene, collision and node data; the textures and
// vertex buffers are Direct3D's, outside it (the process figures show them).
// Then every heap, one line each (tools/game/heap_report.ts reads and compares them):
//   memory heaps in <level> (<32|64>-bit build): <count> heaps
//   memory heap in <level>: region=<bytes> used=<bytes> peak=<bytes> blocks=<n> peak_blocks=<n> name=<heap>
namespace memory {

void log_memory_use(const char* when);

} // namespace memory
