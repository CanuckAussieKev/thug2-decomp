// The rect queries and the rect feeler. See coll_rect.h.

#include "engine/render/coll_rect.h"

#include <cmath>
#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tests.h"
#include "engine/render/coll_tri.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

// The cache the rect feelers use when they have none (0x0069198C).
HitMeshCache*& rect_cache()
{
    static HitMeshCache* rect_cache{};
    return rect_cache;
}

// Hit ends this close on every axis are the same point; normals and directions this close are parallel.
constexpr float kSamePoint = 0.1f; // 0x00645D74
constexpr float kParallel = 0.99f; // 0x00645450

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The scene grid holding the rect (0x0048C940; null: none) and the grids along a line (0x0048C980).
CollGrid* grid_of(const CollRect*) { return get_first_coll_grid(); }
std::span<CollGrid* const> grids_along(const maths::Seg* line) { return get_coll_grids_on_line(line); }

bool collide_with_rect(HitMesh* object, const CollRect* rect, const maths::BBox* box, RectHits* hits)
{
    return object->collide_with_rect(rect, box, hits);
}

// One face's crossing (points and kinds from rect_hits_triangle) added to the hits: the face's normal (v1 - v0) x
// (v2 - v0) normalised (an entry is only counted when it has one), its flag 0x40 and the object's checksum, and each
// end's triangle edge. For an instance (`placed`) the points are scaled and placed, the normals turned back.
void add_rect_hit(RectHits* hits, const maths::Vec4& v0, const maths::Vec4& v1, const maths::Vec4& v2,
                  std::span<const maths::Vec4, 2> points, std::span<const int, 2> kinds, uint16_t face_flags, uint32_t checksum,
                  const CollScaled* placed)
{
    RectHit& hit = core::at(hits->hits, hits->count);
    const double e2x = static_cast<double>(v2.x) - v0.x;
    const double e2y = static_cast<double>(v2.y) - v0.y;
    const double e2z = static_cast<double>(v2.z) - v0.z;
    const float e1x = get_difference(v1.x, v0.x);
    const float e1y = get_difference(v1.y, v0.y);
    const double e1z = static_cast<double>(v1.z) - v0.z;
    maths::Vec4 normal{.x = static_cast<float>(e1y * e2z - e1z * e2y), .y = static_cast<float>(e1z * e2x - e2z * e1x),
                       .z = static_cast<float>(e2y * e1x - e1y * e2x), .w = 0.0f};
    hit.normal = normal.make_unit();
    const double length_squared =
        (static_cast<double>(hit.normal.x) * hit.normal.x + static_cast<double>(hit.normal.y) * hit.normal.y) +
        static_cast<double>(hit.normal.z) * hit.normal.z;
    if (length_squared <= 0.0)
        return;
    hit.face_flag40_ = static_cast<uint8_t>(face_flags & 0x40);
    hit.checksum = checksum;
    core::at(hit.ends, 0).point = core::at(points, 0);
    core::at(hit.ends, 1).point = core::at(points, 1);
    if (placed) {
        for (RectHitEnd& end : hit.ends) {
            const maths::Vec4 scaled{.x = end.point.x * placed->size_factor_.x, .y = placed->size_factor_.y * end.point.y,
                                     .z = placed->size_factor_.z * end.point.z, .w = placed->size_factor_.w * end.point.w};
            placed->basis_frame_.carry_point_through(end.point, scaled);
        }
        maths::Vec4 turned{};
        placed->basis_frame_.rotate_vector(turned, hit.normal);
        hit.normal = turned;
    }
    for (int e = 1; e >= 0; --e) {
        RectHitEnd& end = core::at(hit.ends, e);
        end.on_triangle_edge_ = core::at(kinds, e) != 0;
        maths::Vec4 edge{};
        switch (core::at(kinds, e)) {
        case 1:
            edge = {.x = e1x, .y = e1y, .z = static_cast<float>(e1z), .w = get_difference(v1.w, v0.w)};
            break;
        case 2:
            edge = {.x = get_difference(v2.x, v1.x), .y = get_difference(v2.y, v1.y), .z = get_difference(v2.z, v1.z),
                    .w = get_difference(v2.w, v1.w)};
            break;
        case 3:
            edge = {.x = get_difference(v0.x, v2.x), .y = get_difference(v0.y, v2.y), .z = get_difference(v0.z, v2.z),
                    .w = get_difference(v0.w, v2.w)};
            break;
        default:
            continue;
        }
        end.edge = edge.make_unit();
        if (placed) {
            maths::Vec4 turned{};
            placed->basis_frame_.rotate_vector(turned, end.edge);
            end.edge = turned;
        }
    }
    ++hits->count;
}

// A sector's (or an instance's, in its space) faces against the rect: each crossing a hit. True as soon as one is
// found with the list full.
bool rect_against_faces(const HitMesh* object, const CollRect* rect, const maths::BBox* box, RectHits* hits,
                        const CollScaled* placed)
{
    std::array<uint16_t, 0x800> buffer{};
    const std::span<const uint16_t> faces = object->geometry_->get_faces_in_box(box, buffer);
    const int count = static_cast<int>(faces.size());
    for (uint32_t i = 0; i < ints::as_unsigned(count); ++i) {
        const TriMeshSource* geometry = object->geometry_;
        const uint32_t face = core::at(faces, i);
        const uint16_t flags =
            geometry->face_flags(static_cast<int>(face));
        if ((flags & hits->ignore_flags_) || (~flags & hits->require_flags_))
            continue;
        maths::Vec4 v0{}, v1{}, v2{};
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 0)), &v0);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 1)), &v1);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 2)), &v2);
        std::array<maths::Vec4, 2> points{};
        std::array<int, 2> kinds{};
        if (!rect_hits_triangle(*rect, v0, v1, v2, points, kinds))
            continue;
        if (hits->count == RectHits::kMost)
            return true;
        add_rect_hit(hits, v0, v1, v2, points, kinds, flags, object->checksum_, placed);
    }
    return hits->count > 0;
}

// 0x00411840 (thiscall on the vector): turned by the matrix's rotation, in place. Returns it.
struct VectorTurn : maths::Vec4 {
    maths::Vec4* turn_by(const maths::Mat4* matrix)
    {
        maths::Vec4 turned{};
        matrix->rotate_vector(turned, *this);
        *static_cast<maths::Vec4*>(this) = turned;
        return this;
    }
};

bool get_near(float a, float b) { return std::fabs(static_cast<double>(a) - b) <= kSamePoint; }
bool same_point(const maths::Vec4& a, const maths::Vec4& b)
{
    return get_near(a.x, b.x) && get_near(a.y, b.y) && get_near(a.z, b.z);
}

double dot3(float ax, float ay, float az, float bx, float by, float bz)
{
    return (static_cast<double>(ax) * bx + static_cast<double>(ay) * by) + static_cast<double>(az) * bz;
}

} // namespace

// 0x00417AD0
RectHit* RectHit::copy_from(const RectHit* other)
{
    for (int e = 0; e < 2; ++e) {
        core::at(ends, e).point = core::at(other->ends, e).point;
        core::at(ends, e).edge = core::at(other->ends, e).edge;
        core::at(ends, e).on_triangle_edge_ = core::at(other->ends, e).on_triangle_edge_;
    }
    normal = other->normal;
    checksum = other->checksum;
    face_flag40_ = other->face_flag40_;
    return this;
}

// 0x00417B60
RectHit* RectHit::get_assign(const RectHit* other) { return copy_from(other); }

// 0x0040FEE0 (the far corner's x part, and its y part for the box, summed in double precision first)
bool collide_rect_with_statics(const CollRect* rect, RectHits* hits, HitMeshCache* cache)
{
    const maths::Vec4& o = rect->origin;
    const maths::Vec4& a = rect->side_a_;
    const maths::Vec4& b = rect->side_b_;
    maths::BBox box(o);
    box.append_spot({.x = a.x + o.x, .y = a.y + o.y, .z = a.z + o.z, .w = a.w + o.w});
    box.append_spot({.x = o.x + b.x, .y = b.y + o.y, .z = b.z + o.z, .w = b.w + o.w});
    box.append_spot({.x = static_cast<float>((static_cast<double>(o.x) + a.x) + b.x),
                     .y = static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), .z = (a.z + o.z) + b.z, .w = (a.w + o.w) + b.w});
    const bool use_cache = cache && cache->box_.get_holds(box);
    if (!use_cache) {
        CollGrid* grid = grid_of(rect);
        if (!grid)
            return false;
        grid->objects_in_box(&box); // (the answer isn't used)
    }
    maths::BBox grown;
    grown.min = {.x = box.min.x - 0.5f, .y = box.min.y - 0.5f, .z = box.min.z - 0.5f, .w = box.min.w - 1.0f};
    grown.max = {.x = box.max.x + 0.5f, .y = box.max.y + 0.5f, .z = box.max.z + 0.5f, .w = box.max.w + 1.0f};
    hits->count = 0;
    if (use_cache) {
        for (int i = 0; i < cache->statics_; ++i) {
            const HitMeshCache::TableRow& entry = core::at(cache->entries_, i);
            if (grown.intersects(*entry.bounds))
                collide_with_rect(entry.object, rect, &grown, hits);
        }
        return hits->count > 0;
    }
    // the rect's diagonal
    maths::Seg diagonal;
    diagonal.start = o;
    diagonal.end = {.x = static_cast<float>((static_cast<double>(a.x) + o.x) + b.x), .y = (a.y + o.y) + b.y, .z = (a.z + o.z) + b.z,
                    .w = (a.w + o.w) + b.w};
    const std::span<CollGrid* const> grids = grids_along(&diagonal);
    if (grids.empty())
        return false;
    for (CollGrid* const grid : grids) {
        int count = 0;
        for (HitMesh* const object : grid->objects_along_line(&diagonal, &count)) {
            if (object->inside_bounds(&grown))
                collide_with_rect(object, rect, &grown, hits);
        }
    }
    return hits->count > 0;
}

// 0x00410C90
bool StaticCollider::collide_with_rect(const CollRect* rect, const maths::BBox* box, RectHits* hits)
{
    return rect_against_faces(this, rect, box, hits, nullptr);
}

// 0x004128B0
bool CollStaticInstance::collide_with_rect(const CollRect* rect, const maths::BBox*, RectHits* hits)
{
    CollRect local;
    rect_to_local(rect, &local);
    const maths::Vec4& o = local.origin;
    const maths::Vec4& a = local.side_a_;
    const maths::Vec4& b = local.side_b_;
    maths::BBox box(o);
    box.append_spot({.x = a.x + o.x, .y = a.y + o.y, .z = a.z + o.z, .w = a.w + o.w});
    box.append_spot({.x = b.x + o.x, .y = b.y + o.y, .z = b.z + o.z, .w = b.w + o.w});
    box.append_spot({.x = static_cast<float>(b.x + (static_cast<double>(a.x) + o.x)),
                     .y = static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), .z = (a.z + o.z) + b.z, .w = (a.w + o.w) + b.w});
    return rect_against_faces(this, &local, &box, hits, this);
}

// 0x00410AA0
void CollScaled::rect_to_local(const CollRect* rect, CollRect* out) const
{
    inverse_.carry_point_through(out->origin, rect->origin);
    maths::Vec4 corner{};
    const maths::Vec4& o = rect->origin;
    inverse_.carry_point_through(
        corner, {.x = o.x + rect->side_a_.x, .y = rect->side_a_.y + o.y, .z = rect->side_a_.z + o.z, .w = rect->side_a_.w + o.w});
    out->side_a_ = {.x = corner.x - out->origin.x, .y = corner.y - out->origin.y, .z = corner.z - out->origin.z,
                    .w = corner.w - out->origin.w};
    inverse_.carry_point_through(
        corner, {.x = rect->side_b_.x + o.x, .y = rect->side_b_.y + o.y, .z = rect->side_b_.z + o.z, .w = rect->side_b_.w + o.w});
    out->side_b_ = {.x = corner.x - out->origin.x, .y = corner.y - out->origin.y, .z = corner.z - out->origin.z,
                    .w = corner.w - out->origin.w};
}

// 0x0040FEC0
void HitMesh::set_checksum(uint32_t checksum)
{
    checksum_ = checksum;
    if (geometry_)
        geometry_->set_checksum(checksum);
}

// 0x00410BC0
uint8_t* StaticCollider::take_buffer(uint8_t* block, const TriMeshSource* geometry)
{
    unknown14_ = static_cast<int>(geometry->unknown3c());
    if (geometry->small_faces())
        return block;
    octets_ = block;
    return std::span<uint8_t>(block, geometry->tri_count()).subspan(geometry->tri_count()).data();
}

// 0x004182D0
BoxProbe::BoxProbe()
{
    hits_.count = 0;
    hits_.require_flags_ = 0;
    hits_.ignore_flags_ = 0x10;
}

// 0x00417840
bool BoxProbe::get_query(int)
{
    return collide_rect_with_statics(&rect_, &hits_, hit_cache_ ? hit_cache_ : rect_cache());
}

// 0x00417C00
void BoxProbe::join_hits()
{
    const int count = hits_.count;
    const auto& hits = hits_.hits;
    std::array<bool, RectHits::kMost> used{};
    std::array<maths::Vec4, RectHits::kMost> direction{};
    // each hit's direction (x, y, z made unit when they have a length; w as it is)
    for (int i = count - 1; i >= 0; --i) {
        core::at(used, i) = false;
        const maths::Vec4& p0 = core::at(core::at(hits, i).ends, 0).point;
        const maths::Vec4& p1 = core::at(core::at(hits, i).ends, 1).point;
        maths::Vec4& d = core::at(direction, i);
        d = {.x = get_difference(p1.x, p0.x), .y = get_difference(p1.y, p0.y), .z = get_difference(p1.z, p0.z),
             .w = get_difference(p1.w, p0.w)};
        const double length = std::sqrt((static_cast<double>(d.y) * d.y + static_cast<double>(d.x) * d.x) +
                                        static_cast<double>(d.z) * d.z);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            d.x = static_cast<float>(inverse * d.x);
            d.y = static_cast<float>(inverse * d.y);
            d.z = static_cast<float>(inverse * d.z);
        }
    }
    // duplicates of a later hit (the same ends, either way round, the normals within 0.99) dropped
    for (int j = count - 1; j > 0; --j) {
        if (core::at(used, j))
            continue;
        const RectHit& later = core::at(hits, j);
        for (int k = j - 1; k >= 0; --k) {
            const RectHit& earlier = core::at(hits, k);
            const bool same = (same_point(core::at(later.ends, 0).point, core::at(earlier.ends, 0).point) &&
                               same_point(core::at(later.ends, 1).point, core::at(earlier.ends, 1).point)) ||
                              (same_point(core::at(later.ends, 0).point, core::at(earlier.ends, 1).point) &&
                               same_point(core::at(later.ends, 1).point, core::at(earlier.ends, 0).point));
            if (same && dot3(earlier.normal.z, earlier.normal.y, earlier.normal.x, later.normal.z, later.normal.y,
                             later.normal.x) > kParallel)
                core::at(used, k) = true;
        }
    }
    // each hit left, extended by the earlier ones it lines up with
    joined_count_ = 0;
    for (int j = count - 1; j >= 0; --j) {
        RectHit joined{};
        joined.copy_from(&core::at(hits, j));
        if (core::at(used, j))
            continue;
        for (int k = j - 1; k >= 0; --k) {
            if (core::at(used, k))
                continue;
            const RectHit& other = core::at(hits, k);
            if (dot3(joined.normal.z, joined.normal.x, joined.normal.y, other.normal.z, other.normal.x,
                     other.normal.y) < kParallel)
                continue;
            if (std::fabs(dot3(core::at(direction, j).x, core::at(direction, j).z, core::at(direction, k).y, core::at(direction, k).x, core::at(direction, k).z,
                               core::at(direction, j).y)) < kParallel)
                continue;
            const RectHitEnd& near0 = core::at(other.ends, 0);
            const RectHitEnd& close_a = core::at(other.ends, 1);
            const maths::Vec4& start = core::at(joined.ends, 0).point;
            const maths::Vec4& end = core::at(joined.ends, 1).point;
            if (same_point(start, near0.point)) {
                core::at(joined.ends, 0) = close_a;
            } else if (same_point(start, close_a.point)) {
                core::at(joined.ends, 0) = near0;
            } else if (same_point(end, near0.point)) {
                core::at(joined.ends, 1) = close_a;
            } else if (same_point(end, close_a.point)) {
                core::at(joined.ends, 1) = near0;
            } else {
                continue;
            }
            core::at(used, k) = true;
        }
        core::at(joined_, joined_count_++).get_assign(&joined);
    }
}

// 0x00417860 (the colour as in ProbeRay::debug_draw; each hit drawn in a colour of its own)
void BoxProbe::debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    const maths::Vec4& o = rect_.origin;
    const maths::Vec4& a = rect_.side_a_;
    const maths::Vec4& s = rect_.side_b_;
    const uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    const maths::Vec4 oa{.x = o.x + a.x, .y = a.y + o.y, .z = a.z + o.z, .w = a.w + o.w};
    const maths::Vec4 oab{.x = oa.x + s.x, .y = oa.y + s.y, .z = oa.z + s.z, .w = oa.w + s.w};
    const maths::Vec4 ob{.x = o.x + s.x, .y = s.y + o.y, .z = s.z + o.z, .w = s.w + o.w};
    gfx::push_diag_line(&o, &oa, colour, colour, frames);
    gfx::push_diag_line(&oa, &oab, colour, colour, frames);
    gfx::push_diag_line(&oab, &ob, colour, colour, frames);
    gfx::push_diag_line(&ob, &o, colour, colour, frames);
    const std::span<const RectHit> hits = joined_count_ > 0 ? std::span<const RectHit>(joined_) : std::span<const RectHit>(hits_.hits);
    const int count = joined_count_ > 0 ? joined_count_ : hits_.count;
    for (int i = count - 1; i >= 0; --i) {
        b += 0x1F;
        g += 0x63;
        r += 0xC9;
        const uint32_t hit_colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
        gfx::push_diag_line(&core::at(core::at(hits, i).ends, 0).point, &core::at(core::at(hits, i).ends, 1).point, hit_colour, hit_colour, 1);
    }
}

} // namespace render
