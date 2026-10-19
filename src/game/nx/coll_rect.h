#pragma once

// The rect queries: a parallelogram (CollRect, coll_tests.h) against the static collision - where it crosses the
// triangles, as segments - and the rect feeler that asks and joins the pieces up.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/nx/coll_tests.h"

namespace Nx {

class CCollCache;

// One end of a rect hit: the point, and the triangle edge it lies on (unit; left as it was when the end is on a
// rect side instead).
struct RectHitEnd {
    Mth::Vector point;
    Mth::Vector edge;
    bool onTriangleEdge;
    uint8_t pad[3];
};
static_assert(sizeof(RectHitEnd) == 0x24, "layout must match the original");

// Where the rect crosses a triangle (0x60 bytes): a segment, the face's normal, its object's checksum and flag 0x40.
struct RectHit {
    // 0x00417AD0 / 0x00417B60: every field copied (not the padding). Returns this.
    RectHit* CopyFrom(const RectHit* other);
    RectHit* Assign(const RectHit* other);

    RectHitEnd ends[2];
    Mth::Vector normal;      // +0x48
    uint32_t checksum;       // +0x58
    uint8_t faceFlag40;      // +0x5C
    uint8_t pad[3];
};
static_assert(sizeof(RectHit) == 0x60 && offsetof(RectHit, normal) == 0x48, "layout must match the original");

// What a rect query fills (0xC08 bytes): up to 32 hits; faces with an ignored flag or without a required one are
// passed over.
struct RectHits {
    static constexpr int kMost = 32;

    RectHit hits[kMost];
    int count;               // +0xC00
    uint16_t ignoreFlags;    // +0xC04
    uint16_t requireFlags;   // +0xC06
};
static_assert(sizeof(RectHits) == 0xC08, "layout must match the original");

// 0x0040FEE0 (cdecl): the rect against the static collision objects (slot 15 of each), the count reset first:
// through `cache` when it holds the rect's box (its statics whose boxes touch that box grown by 0.5 - w by 1),
// else the scenes' grids along the rect's diagonal (each object's slot 13 first; nothing when the rect's scene has
// no grid). Whether anything was hit.
bool CollideRectWithStatics(const CollRect* rect, RectHits* hits, CCollCache* cache);

// The rect feeler (0x1844 bytes): a rect, what it hits, and the hits joined into longer segments.
class CRectFeeler {
public:
    // 0x004182D0: no hits, faces flagged 0x10 ignored, no cache. Returns this.
    CRectFeeler* Construct();
    // 0x00417840: the rect's query, through the feeler's cache or the current rect cache (0x0069198C). (The
    // argument isn't used.)
    bool Query(int unused);
    // 0x00417C00: the hits joined: duplicates (both ends within 0.1, either way round, normals within 0.99) dropped;
    // each remaining hit extended by the later ones lying along it (normals and directions within 0.99, an end
    // within 0.1 of one of its ends) into m_joined.
    void JoinHits();
    // 0x00417860: the rect drawn, then the joined hits (or the hits) - compiled out of this build.
    void DebugDraw(uint32_t r, uint32_t g, uint32_t b, int frames);

    CollRect m_rect;                    // +0x0000
    RectHits m_hits;                    // +0x0030
    RectHit m_joined[RectHits::kMost];  // +0x0C38
    int m_joinedCount;                  // +0x1838
    uint32_t m_unknown183C;             // +0x183C
    CCollCache* mp_cache;               // +0x1840
};
static_assert(offsetof(CRectFeeler, m_joined) == 0xC38 && offsetof(CRectFeeler, mp_cache) == 0x1840,
              "layout must match the original");

} // namespace Nx
