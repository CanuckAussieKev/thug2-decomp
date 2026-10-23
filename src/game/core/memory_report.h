#pragma once

// NOT ORIGINAL - what a level costs the memory manager and the process, logged once a level is up (next to the
// script pools' use, game/front/start_level.cpp) so converters can measure how much a level takes:
//   memory in <level>: main block <size> KB, free <now> KB (least <low-water mark> KB); heaps: <name> <used> KB
//   (peak <peak> KB) ...; process: private <committed> MB, address space free <total> MB (largest <block> MB)
// The main block's heaps (top down / bottom up) hold the level's scene, collision and node data; the textures and
// vertex buffers are Direct3D's, outside it (the process figures show them).
namespace Mem {

void LogMemoryUse(const char* when);

} // namespace Mem
