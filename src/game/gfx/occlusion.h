#pragma once

#include <cstdint>

#include "game/mth/vector.h"

// Occlusion polygons and view culling (Nx engine, PC). Levels place four-cornered occlusion polygons; each frame
// up to eight of them become occluders - five planes (the polygon's own and one through the eye and each edge) -
// and a sphere wholly behind all five planes of one is hidden. Occluders that hide nothing for two frames are
// dropped for others.
namespace Nx {

// An occlusion polygon (0x00787360, up to 512).
struct OcclusionPoly {
    uint8_t inUse;     // an occluder now
    uint8_t enabled;
    uint8_t pad[2];
    uint32_t checksum; // its name
    float corners[4][4];
    float normal[4];
};
static_assert(sizeof(OcclusionPoly) == 0x58, "layout must match the original");

// An occluder (0x00792360, up to 8): its polygon, the planes, and what it hid in each of the last two frames.
struct Occluder {
    OcclusionPoly* poly;
    float planes[5][4]; // x, y, z, distance
    int hidden[2];
};
static_assert(sizeof(Occluder) == 0x5C, "layout must match the original");

// 0x004D8BB0 (qsort's comparison): occluders that hid more first (a disabled polygon's counts cleared first).
int __cdecl CompareOccluders(const void* a, const void* b);
// 0x004D8C10: sort the occluders, drop those that hid nothing in two frames, clear this frame's counts.
void __cdecl SortOccluders();
// 0x004D8C90: enable or disable the polygons with this checksum.
void __cdecl EnableOcclusionPoly(uint32_t checksum, bool enabled);
// 0x004D8CC0: forget every polygon and occluder.
void __cdecl RemoveAllOcclusionPolys();
// 0x004D8CE0: make up to four more enabled polygons occluders (while there's room), carrying on from the last.
void __cdecl AddOccluders();
// 0x004D8D70: add a polygon (four corners; its normal from the first, second and fourth).
void __cdecl AddOcclusionPoly(const Mth::Vector* p0, const Mth::Vector* p1, const Mth::Vector* p2,
                              const Mth::Vector* p3, uint32_t checksum);
// 0x004D8ED0: the frame's occluders (`frame` 0 / 1) and their planes as seen from `eye`.
void __cdecl BuildOccluders(const Mth::Vector* eye, int frame);
// 0x004D9AC0: whether a sphere (moved by the object's transform, 0x007CA968) is hidden by an occluder - which is
// credited with `weight`.
// NOT ORIGINAL: while set, nothing is occluded (the view frustum still culls). Set at start-up from [Graphics]
// OcclusionCulling=0 (the default: modern hardware draws the few extra meshes without effort, and designer-placed
// occluders sometimes hide things in plain sight); the level editor sets it while open (O) and puts it back after.
// A level's occluders are placed for the skater's view; from the editor's free camera (above the level, outside it)
// they hide distant scenery and objects that are in plain sight.
bool& OcclusionOff();
bool __cdecl SphereOccluded(const Mth::Vector* centre, float radius, int weight);

// 0x004DB570: whether a sphere is outside the view (1) or not (0).
int __cdecl SphereOutsideView(const Mth::Vector* centre, float radius);
// 0x004DB790: whether a sphere (moved by the object's transform) is in view; leaves how far its nearest point
// is in front of the camera (0x007CA978).
bool __cdecl SphereInView(const Mth::Vector* centre, float radius);
// 0x004DB6D0: with distance clipping on (0x007D643A), whether a bounding sphere (centre +4, radius +0x10) is
// wholly beyond the far clip distance.
bool __cdecl BeyondClipDistance(const void* bounds);

} // namespace Nx
