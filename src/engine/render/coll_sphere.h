#pragma once

// The sphere queries: a sphere - standing, or moving along a direction - against triangles and the static
// collision, and the sphere feeler that asks.

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/render/coll_tests.h"

namespace render {

class HitMeshCache;

// Where a sphere touches a face (0x2C bytes): the point, how far along its move (when moving), the face's normal,
// its object's checksum and flag 0x40.
struct SphereHit {
    maths::Vec4 point;
    float t;              // +0x10
    maths::Vec4 normal;   // +0x14
    uint32_t checksum;    // +0x24
    uint8_t face_flag40_; // +0x28
    std::array<uint8_t, 3> pad;
};
static_assert(sizeof(SphereHit) == 0x2C, "layout must match the original");

// What a sphere query fills (0x5A4 bytes): up to 32 hits; the move (a direction and how far along it - none when 0),
// the flags of faces to pass over or to require, and whether to stop at the first hit (mode 2).
struct SphereHits {
    static constexpr int kMost = 32;

    uint32_t unknown00;
    std::array<SphereHit, kMost> hits;   // +0x004
    maths::Vec4 direction;   // +0x584
    float sweep;             // +0x594
    int count;               // +0x598
    uint16_t ignore_flags_;  // +0x59C
    uint16_t require_flags_; // +0x59E
    int mode;                // +0x5A0
};
static_assert(sizeof(SphereHits) == 0x5A4 && offsetof(SphereHits, count) == 0x598, "layout must match the original");

// 0x0040E280 (cdecl): where the segment crosses the sphere's surface: 0, 1 or 2 points (*count) in order along
// it; whether any. (The fifth argument isn't used.)
bool segment_crosses_sphere(const maths::Seg* segment, const CollSphere* sphere, int* count,
                            std::span<maths::Vec4, 2> points, int unused);
// 0x0040E560 (cdecl): a sphere moving by `motion` against a segment moving by `edge_motion` (in practice (0, 0, 0,
// 1): the edge stands, the point's w counts the time): already touching (segment_crosses_sphere) - those points; else
// where the capsule round the segment meets the centre's path - *t the first time within 0..1, the point the
// segment's nearest to the centre then (*count 1). Whether it touches.
bool swept_sphere_hits_edge(const maths::Seg* edge, const maths::Vec4* edge_motion, const CollSphere* sphere,
                            const maths::Vec4* motion, float* t, float limit, int* count,
                            std::span<maths::Vec4, 2> point);
// The triangle a corner is of: its three corners.
struct CornerTriangle {
    const maths::Vec4* v0{};
    const maths::Vec4* v1{};
    const maths::Vec4* v2{};
};

// The edge a corner test follows: the edge the plane stands on and the edge itself.
struct CornerEdge {
    const maths::Vec4* edge_cross_{};
    const maths::Vec4* edge{};
};

// 0x0040E940 (cdecl): the moving sphere against the triangle's corner `corner` and the edge leaving it towards the
// sphere's path (chosen by where the path crosses the plane through the edge `edge_cross` stands on): the corner
// inside the sphere already - *t 0 and no hit; moving away from it - no hit; else swept_sphere_hits_edge on that edge.
// (`normal` isn't used; a corner that isn't one of the triangle's leaves the edge's other end unset.)
bool swept_sphere_hits_corner(const maths::Vec4* corner, const CollSphere* sphere, const maths::Vec4* motion,
                              const maths::Vec4* edge_motion, CornerTriangle triangle, CornerEdge edge_line, float* t,
                              float limit, int* count, maths::Vec4* point, maths::Vec4* normal);
// 0x0040EC90 (cdecl): the sphere moving by `motion` (up to `limit` of it) against the triangle: the face's unit
// normal to `normal`; where it touches first to `point` (*t how far along). A sphere already within its radius of
// the plane: by where its centre lies against the three edges - the face (its nearest point, if the move's capsule
// holds it), an edge or a corner; else the point where the sphere's front first meets the plane, again by the
// edges.
bool swept_sphere_hits_triangle(const CollSphere* sphere, const maths::Vec4* v0, const maths::Vec4* v1,
                                const maths::Vec4* v2, float* t, float limit, const maths::Vec4* motion,
                                maths::Vec4* point, maths::Vec4* normal);
// Where a sphere's contact goes: its point and its normal.
struct SphereContact {
    maths::Vec4* point{};
    maths::Vec4* normal{};
};

// 0x0040F9E0 (cdecl): the standing sphere against the triangle (from its front only): the face's unit normal to
// `normal`, its nearest point to `point`; whether that's within the radius.
bool sphere_hits_triangle(const CollSphere* sphere, const maths::Vec4* v0, const maths::Vec4* v1, const maths::Vec4* v2, SphereContact contact);

// 0x00410240 (cdecl): the sphere (moving, when `hits` has a sweep) against the static collision objects (slot 16),
// as the rect query: its box (and the moved sphere's), through `cache` when it holds it, else the scenes' grids.
bool collide_sphere_with_statics(const CollSphere* sphere, SphereHits* hits, HitMeshCache* cache);

// The sphere feeler (0xB60 bytes).
class SphereFeeler {
public:
    // 0x00418330: the sphere; no move (direction (0, 0, 0, 1)), no hits, faces flagged 0x10 ignored, no cache.
    SphereFeeler(const maths::Vec4* centre, float radius);
    // 0x00418300: the query (mode 1), through the feeler's cache or the current sphere cache (0x00691990). (The
    // argument isn't used.)
    bool get_query(int unused);

    // The fields other code reads and sets.
    CollSphere& bound_ball() { return bound_ball_; }
    [[nodiscard]] const CollSphere& bound_ball() const { return bound_ball_; }
    [[nodiscard]] const SphereHits& hits() const { return hits_; }
    void set_hit_cache(HitMeshCache* value) { hit_cache_ = value; }

private:
    CollSphere bound_ball_{};     // +0x000
    SphereHits hits_{};           // +0x014
    std::array<uint8_t, 0x598> unknown5b8_{}; // +0x5B8
    uint32_t unknownb50_{0};      // +0xB50
    std::array<uint32_t, 2> unknownb54_{};  // +0xB54
    HitMeshCache* hit_cache_{nullptr}; // +0xB5C
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(SphereFeeler, hits_) == 0x14 && offsetof(SphereFeeler, hit_cache_) == 0xB5C,
                      "layout must match the original");
    }
};

} // namespace render
