#pragma once

// The rect queries: a parallelogram (CollRect, coll_tests.h) against the static collision - where it crosses the
// triangles, as segments - and the rect feeler that asks and joins the pieces up.

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/render/coll_tests.h"

namespace render {

class HitMeshCache;

// One end of a rect hit: the point, and the triangle edge it lies on (unit; left as it was when the end is on a
// rect side instead).
struct RectHitEnd {
    maths::Vec4 point;
    maths::Vec4 edge;
    bool on_triangle_edge_;
    std::array<uint8_t, 3> pad;
};
constexpr std::size_t kRectHitEndBytes = 0x24;

static_assert(sizeof(RectHitEnd) == kRectHitEndBytes, "layout must match the original");

// Where the rect crosses a triangle (0x60 bytes): a segment, the face's normal, its object's checksum and flag 0x40.
struct RectHit {

    std::array<RectHitEnd, 2> ends;
    maths::Vec4 normal;   // +0x48
    uint32_t checksum;    // +0x58
    uint8_t face_flag40_; // +0x5C
    std::array<uint8_t, 3> pad;
};

// 0x00417AD0 / 0x00417B60: every field copied (not the padding). Returns this.
RectHit* copy_from(RectHit& hit, const RectHit* other);

RectHit* get_assign(RectHit& hit, const RectHit* other);
constexpr std::size_t kRectHitBytes = 0x60;
constexpr std::size_t kRectHitNormalOffset = 0x48;

static_assert(sizeof(RectHit) == kRectHitBytes && offsetof(RectHit, normal) == kRectHitNormalOffset, "layout must match the original");

// What a rect query fills (0xC08 bytes): up to 32 hits; faces with an ignored flag or without a required one are
// passed over.
struct RectHits {
    static constexpr int kMost = 32;

    std::array<RectHit, kMost> hits;
    int count;               // +0xC00
    uint16_t ignore_flags_;  // +0xC04
    uint16_t require_flags_; // +0xC06
};
constexpr std::size_t kRectHitsBytes = 0xC08;

static_assert(sizeof(RectHits) == kRectHitsBytes, "layout must match the original");

// 0x0040FEE0 (cdecl): the rect against the static collision objects (slot 15 of each), the count reset first:
// through `cache` when it holds the rect's box (its statics whose boxes touch that box grown by 0.5 - w by 1),
// else the scenes' grids along the rect's diagonal (each object's slot 13 first; nothing when the rect's scene has
// no grid). Whether anything was hit.
bool collide_rect_with_statics(const CollRect* rect, RectHits* hits, HitMeshCache* cache);

constexpr std::size_t kBoxProbeJoinedOffset = 0xC38;
constexpr std::size_t kBoxProbeHitCacheOffset = 0x1840;

// The rect feeler (0x1844 bytes): a rect, what it hits, and the hits joined into longer segments.
class BoxProbe {
public:
    // 0x004182D0: no hits, faces flagged 0x10 ignored, no cache.
    BoxProbe();
    // 0x00417840: the rect's query, through the feeler's cache or the current rect cache (0x0069198C). (The
    // argument isn't used.)
    bool get_query(int unused);
    // 0x00417C00: the hits joined: duplicates (both ends within 0.1, either way round, normals within 0.99) dropped;
    // each remaining hit extended by the later ones lying along it (normals and directions within 0.99, an end
    // within 0.1 of one of its ends) into joined_.
    void join_hits();
    // 0x00417860: the rect drawn, then the joined hits (or the hits) - compiled out of this build.
    void debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames);

    // The fields other code reads and sets.
    CollRect& rect() { return rect_; }
    [[nodiscard]] const CollRect& rect() const { return rect_; }
    RectHits& hits() { return hits_; }
    [[nodiscard]] const RectHits& hits() const { return hits_; }
    std::array<RectHit, RectHits::kMost>& joined() { return joined_; }
    [[nodiscard]] const std::array<RectHit, RectHits::kMost>& joined() const { return joined_; }
    [[nodiscard]] int joined_count() const { return joined_count_; }
    void set_hit_cache(HitMeshCache* value) { hit_cache_ = value; }

private:
    CollRect rect_{};                   // +0x0000
    RectHits hits_{};                   // +0x0030
    std::array<RectHit, RectHits::kMost> joined_{}; // +0x0C38
    int joined_count_{0};               // +0x1838
    uint32_t unknown183c_{};          // +0x183C
    HitMeshCache* hit_cache_{nullptr}; // +0x1840
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(BoxProbe, joined_) == kBoxProbeJoinedOffset && offsetof(BoxProbe, hit_cache_) == kBoxProbeHitCacheOffset,
                      "layout must match the original");
    }
};

} // namespace render
