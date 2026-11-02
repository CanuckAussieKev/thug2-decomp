#pragma once

#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
#include <utility>

// Occlusion polygons and view culling (Nx engine, PC). Levels place four-cornered occlusion polygons; each frame
// up to eight of them become occluders - five planes (the polygon's own and one through the eye and each edge) -
// and a sphere wholly behind all five planes of one is hidden. Occluders that hide nothing for two frames are
// dropped for others.
namespace render {

// An occlusion polygon (0x00787360, up to 512).
struct OccluderPolygon {
    uint8_t in_use_; // an occluder now
    uint8_t enabled;
    std::array<uint8_t, 2> pad;
    uint32_t checksum; // its name
    std::array<std::array<float, 4>, 4> corners;
    std::array<float, 4> normal;
};
static_assert(sizeof(OccluderPolygon) == 0x58, "layout must match the original");

// An occluder (0x00792360, up to 8): its polygon, the planes, and what it hid in each of the last two frames.
struct Occluder {
    OccluderPolygon* poly;
    std::array<std::array<float, 4>, 5> planes; // x, y, z, distance
    std::array<int, 2> hidden;
};
static_assert(sizeof(Occluder) == 0x5C, "layout must match the original");

// 0x004D8BB0 (qsort's comparison): occluders that hid more first (a disabled polygon's counts cleared first).
int compare_occluders(const void* a, const void* b);
// 0x004D8C10: sort the occluders, drop those that hid nothing in two frames, clear this frame's counts.
void sort_occluders();
// 0x004D8C90: enable or disable the polygons with this checksum.
void activate_occluder_poly(uint32_t checksum, bool enabled);
// 0x004D8CC0: forget every polygon and occluder.
void clear_occluder_polys();
// 0x004D8CE0: make up to four more enabled polygons occluders (while there's room), carrying on from the last.
void add_occluders();
// 0x004D8D70: add a polygon (four corners; its normal from the first, second and fourth).
void insert_occluder_poly(const maths::Vec4* p0, const maths::Vec4* p1, const maths::Vec4* p2, const maths::Vec4* p3,
                          uint32_t checksum);
// 0x004D8ED0: the frame's occluders (`frame` 0 / 1) and their planes as seen from `eye`.
void collect_occluders(const maths::Vec4* eye, int frame);
// 0x004D9AC0: whether a sphere (moved by the object's transform, 0x007CA968) is hidden by an occluder - which is
// credited with `weight`.
// NOT ORIGINAL: while set, nothing is occluded (the view frustum still culls). Set at start-up from [Graphics]
// OcclusionCulling=0 (on by default, as the original; 0 because modern hardware draws the few extra meshes without
// effort and designer-placed occluders sometimes hide things in plain sight); the level editor sets it while open
// (O) and puts it back after.
// A level's occluders are placed for the skater's view; from the editor's free camera (above the level, outside it)
// they hide distant scenery and objects that are in plain sight.
bool& occlusion_off();
bool sphere_occluded(const maths::Vec4* centre, float radius, int weight);

// 0x004DB570: whether a sphere is outside the view (1) or not (0).
int sphere_outside_view(const maths::Vec4* centre, float radius);
// 0x004DB790: whether a sphere (moved by the object's transform) is in view; leaves how far its nearest point
// is in front of the camera (0x007CA978).
bool sphere_in_view(const maths::Vec4* centre, float radius);
// 0x004DB6D0: with distance clipping on (0x007D643A), whether a bounding sphere (centre +4, radius +0x10) is
// wholly beyond the far clip distance.
bool beyond_clip_distance(const void* bounds);
// A mesh's or scene's bounding sphere as those tests take it: its centre's three floats, the radius after them
// (where the original's Vec4 view of the centre found it as w).
inline maths::Vec4 sphere_of(const std::array<float, 3>& centre, float radius)
{
    return {.x = std::get<0>(centre), .y = std::get<1>(centre), .z = std::get<2>(centre), .w = radius};
}

extern float g_last_sphere_distance;

} // namespace render
