#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// The engine's spawned polygons (the SpawnPoly script command): textured or plain quads at a place, turned to face a
// direction, their size and corner colours moved from a start to an end over their lifetime, each drawn by a PC
// primitive of its own; kept in the table 0x007004AC (keyed by address).
namespace Nx {

struct CPcPrimitive;

// A spawned polygon (0xB8 bytes; the script command's SpawnedPolyDetails, from the pool pair 0x00700500).
struct sSpawnedPoly {
    Mth::Vector pos;            // +0x00
    Mth::Matrix matrix;         // +0x10: its turn (row 0 the direction, rows 1 / 2 across; row 3 (0, 0, 0, 1))
    float dims[2];              // +0x50: the size now
    float startDims[2];         // +0x58
    float endDims[2];           // +0x60
    uint8_t startColours[4][4]; // +0x68: per corner (RGBA)
    uint8_t endColours[4][4];   // +0x78
    uint8_t colours[4][4];      // +0x88: now
    uint32_t flags;             // +0x98: 1 done; 2 the primitive's +0x14
    float age;                  // +0x9C: seconds
    float lifetime;             // +0xA0
    uint8_t* texture;           // +0xA4 (null: untextured)
    CPcPrimitive* primitive;    // +0xA8
    uint32_t blendMode;         // +0xAC
    float justX;                // +0xB0 (-1 left, 0 centre, 1 right)
    float justY;                // +0xB4 (-1 top, 0 centre, 1 bottom)

    // 0x00491990: construct: zeroed, blend mode 0x54628ED7. Returns this.
    sSpawnedPoly* Construct();
};
static_assert(offsetof(sSpawnedPoly, dims) == 0x50 && offsetof(sSpawnedPoly, colours) == 0x88 &&
                  offsetof(sSpawnedPoly, primitive) == 0xA8 && sizeof(sSpawnedPoly) == 0xB8,
              "layout must match the original");

namespace Engine {

// 0x00492450: a polygon spawned: its primitive made (0x158 bytes; none when out of memory), its turn from
// `direction` (row 0) and `up` (both normalised first; rows 2 and 1 their cross products), its texture, the start
// colours and size, its four corners about its centre (the turn applied - each corner's w as the stack left it in the
// original, 0 here: row 3 is (0, 0, 0, 1)) - with texture coordinates when textured - then readied and put in the
// table.
void __cdecl SpawnPoly(sSpawnedPoly* poly, Mth::Vector* direction, Mth::Vector* up);
// 0x004919C0: each polygon not done aged: past its lifetime it's done (its primitive destroyed, taken out of the table
// and freed - 0x004472D0); else its colours and size moved on and its corners set again, placed by its
// justification.
void __cdecl UpdateSpawnedPolys();
// 0x00491E10: every polygon's primitive destroyed, the polygon taken out of the table and freed (the pool pair's).
void __cdecl KillAllSpawnedPolys();

} // namespace Engine

} // namespace Nx
