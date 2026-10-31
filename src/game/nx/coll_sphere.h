#pragma once

// The sphere queries: a sphere - standing, or moving along a direction - against triangles and the static
// collision, and the sphere feeler that asks.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/nx/coll_tests.h"

namespace Engine3D {

class CHitMeshCache;

// Where a sphere touches a face (0x2C bytes): the point, how far along its move (when moving), the face's normal,
// its object's checksum and flag 0x40.
struct SphereHit {
    Maths::Vec4 point;
    float t;                 // +0x10
    Maths::Vec4 normal;      // +0x14
    uint32_t checksum;       // +0x24
    uint8_t faceFlag40;      // +0x28
    uint8_t pad[3];
};
static_assert(sizeof(SphereHit) == 0x2C, "layout must match the original");

// What a sphere query fills (0x5A4 bytes): up to 32 hits; the move (a direction and how far along it - none when 0),
// the flags of faces to pass over or to require, and whether to stop at the first hit (mode 2).
struct SphereHits {
    static constexpr int kMost = 32;

    uint32_t unknown00;
    SphereHit hits[kMost];   // +0x004
    Maths::Vec4 direction;   // +0x584
    float sweep;             // +0x594
    int count;               // +0x598
    uint16_t ignoreFlags;    // +0x59C
    uint16_t requireFlags;   // +0x59E
    int mode;                // +0x5A0
};
static_assert(sizeof(SphereHits) == 0x5A4 && offsetof(SphereHits, count) == 0x598, "layout must match the original");

// 0x0040E280 (cdecl): where the segment crosses the sphere's surface: 0, 1 or 2 points (*count) in order along
// it; whether any. (The fifth argument isn't used.)
bool SegmentCrossesSphere(const Maths::Seg* segment, const CollSphere* sphere, int* count, Maths::Vec4* points,
                          int unused);
// 0x0040E560 (cdecl): a sphere moving by `motion` against a segment moving by `edgeMotion` (in practice (0, 0, 0,
// 1): the edge stands, the point's w counts the time): already touching (SegmentCrossesSphere) - those points; else
// where the capsule round the segment meets the centre's path - *t the first time within 0..1, the point the
// segment's nearest to the centre then (*count 1). Whether it touches.
bool SweptSphereHitsEdge(const Maths::Seg* edge, const Maths::Vec4* edgeMotion, const CollSphere* sphere,
                         const Maths::Vec4* motion, float* t, float limit, int* count, Maths::Vec4* point);
// 0x0040E940 (cdecl): the moving sphere against the triangle's corner `corner` and the edge leaving it towards the
// sphere's path (chosen by where the path crosses the plane through the edge `edgeCross` stands on): the corner
// inside the sphere already - *t 0 and no hit; moving away from it - no hit; else SweptSphereHitsEdge on that edge.
// (`normal` isn't used; a corner that isn't one of the triangle's leaves the edge's other end unset.)
bool SweptSphereHitsCorner(const Maths::Vec4* corner, const CollSphere* sphere, const Maths::Vec4* motion,
                           const Maths::Vec4* edgeMotion, const Maths::Vec4* v0, const Maths::Vec4* v1,
                           const Maths::Vec4* v2, const Maths::Vec4* edgeCross, const Maths::Vec4* edge, float* t,
                           float limit, int* count, Maths::Vec4* point, Maths::Vec4* normal);
// 0x0040EC90 (cdecl): the sphere moving by `motion` (up to `limit` of it) against the triangle: the face's unit
// normal to `normal`; where it touches first to `point` (*t how far along). A sphere already within its radius of
// the plane: by where its centre lies against the three edges - the face (its nearest point, if the move's capsule
// holds it), an edge or a corner; else the point where the sphere's front first meets the plane, again by the
// edges.
bool SweptSphereHitsTriangle(const CollSphere* sphere, const Maths::Vec4* v0, const Maths::Vec4* v1,
                             const Maths::Vec4* v2, float* t, float limit, const Maths::Vec4* motion,
                             Maths::Vec4* point, Maths::Vec4* normal);
// 0x0040F9E0 (cdecl): the standing sphere against the triangle (from its front only): the face's unit normal to
// `normal`, its nearest point to `point`; whether that's within the radius.
bool SphereHitsTriangle(const CollSphere* sphere, const Maths::Vec4* v0, const Maths::Vec4* v1, const Maths::Vec4* v2,
                        Maths::Vec4* point, Maths::Vec4* normal);

// 0x00410240 (cdecl): the sphere (moving, when `hits` has a sweep) against the static collision objects (slot 16),
// as the rect query: its box (and the moved sphere's), through `cache` when it holds it, else the scenes' grids.
bool CollideSphereWithStatics(const CollSphere* sphere, SphereHits* hits, CHitMeshCache* cache);

// The sphere feeler (0xB60 bytes).
class CSphereFeeler {
public:
    // 0x00418330: the sphere; no move (direction (0, 0, 0, 1)), no hits, faces flagged 0x10 ignored, no cache.
    // Returns this.
    CSphereFeeler* MakeNew(const Maths::Vec4* centre, float radius);
    // 0x00418300: the query (mode 1), through the feeler's cache or the current sphere cache (0x00691990). (The
    // argument isn't used.)
    bool Query(int unused);

    CollSphere m_boundBall;            // +0x000
    SphereHits m_hits;              // +0x014
    uint8_t m_unknown5B8[0x598];    // +0x5B8
    uint32_t m_unknownB50;          // +0xB50
    uint32_t m_unknownB54[2];       // +0xB54
    CHitMeshCache* m_hitCache;           // +0xB5C
};
static_assert(offsetof(CSphereFeeler, m_hits) == 0x14 && offsetof(CSphereFeeler, m_hitCache) == 0xB5C,
              "layout must match the original");

} // namespace Nx
