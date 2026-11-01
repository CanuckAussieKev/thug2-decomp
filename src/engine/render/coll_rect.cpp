// The rect queries and the rect feeler. See coll_rect.h.

#include "engine/render/coll_rect.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/gfx/engine_scene_list.h"

namespace render {

namespace {

HitMeshCache* g_rect_cache;

// The cache the rect feelers use when they have none (0x0069198C).
HitMeshCache*& s_rect_cache = g_rect_cache;

// Hit ends this close on every axis are the same point; normals and directions this close are parallel.
constexpr float kSamePoint = 0.1f; // 0x00645D74
constexpr float kParallel = 0.99f; // 0x00645450

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The scene grid holding the rect (0x0048C940; null: none) and the grids along a line (0x0048C980).
CollGrid* grid_of(const CollRect*) { return get_first_coll_grid(); }
CollGrid** grids_along(const maths::Seg* line) { return get_coll_grids_on_line(line); }

bool collide_with_rect(HitMesh* object, const CollRect* rect, const maths::BBox* box, RectHits* hits)
{
    return object->collide_with_rect(rect, box, hits);
}

// One face's crossing (points and kinds from rect_hits_triangle) added to the hits: the face's normal (v1 - v0) x
// (v2 - v0) normalised (an entry is only counted when it has one), its flag 0x40 and the object's checksum, and each
// end's triangle edge. For an instance (`placed`) the points are scaled and placed, the normals turned back.
void add_rect_hit(RectHits* hits, const maths::Vec4& v0, const maths::Vec4& v1, const maths::Vec4& v2,
                  const maths::Vec4* points, const int* kinds, uint16_t face_flags, uint32_t checksum,
                  const CollScaled* placed)
{
    RectHit& hit = hits->hits[hits->count];
    const double e2x = static_cast<double>(v2.x) - v0.x;
    const double e2y = static_cast<double>(v2.y) - v0.y;
    const double e2z = static_cast<double>(v2.z) - v0.z;
    const float e1x = get_difference(v1.x, v0.x);
    const float e1y = get_difference(v1.y, v0.y);
    const double e1z = static_cast<double>(v1.z) - v0.z;
    maths::Vec4 normal{static_cast<float>(e1y * e2z - e1z * e2y), static_cast<float>(e1z * e2x - e2z * e1x),
                       static_cast<float>(e2y * e1x - e1y * e2x), 0.0f};
    hit.normal = normal.make_unit();
    const double length_squared =
        (static_cast<double>(hit.normal.x) * hit.normal.x + static_cast<double>(hit.normal.y) * hit.normal.y) +
        static_cast<double>(hit.normal.z) * hit.normal.z;
    if (length_squared <= 0.0)
        return;
    hit.face_flag40_ = static_cast<uint8_t>(face_flags & 0x40);
    hit.checksum = checksum;
    hit.ends[0].point = points[0];
    hit.ends[1].point = points[1];
    if (placed) {
        for (RectHitEnd& end : hit.ends) {
            const maths::Vec4 scaled{end.point.x * placed->size_factor_.x, placed->size_factor_.y * end.point.y,
                                     placed->size_factor_.z * end.point.z, placed->size_factor_.w * end.point.w};
            placed->basis_frame_.carry_point_through(end.point, scaled);
        }
        maths::Vec4 turned{};
        placed->basis_frame_.rotate_vector(turned, hit.normal);
        hit.normal = turned;
    }
    for (int e = 1; e >= 0; --e) {
        RectHitEnd& end = hit.ends[e];
        end.on_triangle_edge_ = kinds[e] != 0;
        maths::Vec4 edge{};
        switch (kinds[e]) {
        case 1:
            edge = {e1x, e1y, static_cast<float>(e1z), get_difference(v1.w, v0.w)};
            break;
        case 2:
            edge = {get_difference(v2.x, v1.x), get_difference(v2.y, v1.y), get_difference(v2.z, v1.z),
                    get_difference(v2.w, v1.w)};
            break;
        case 3:
            edge = {get_difference(v0.x, v2.x), get_difference(v0.y, v2.y), get_difference(v0.z, v2.z),
                    get_difference(v0.w, v2.w)};
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
    int count = 0;
    uint16_t buffer[0x800];
    const uint16_t* faces = object->geometry_->get_faces_in_box(box, &count, buffer, 0x800);
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const TriMeshSource* geometry = object->geometry_;
        const uint32_t face = faces[i];
        const uint16_t flags =
            *reinterpret_cast<const uint16_t*>(geometry->faces_ + face * (geometry->small_faces_ ? 8 : 10));
        if ((flags & hits->ignore_flags_) || (~flags & hits->require_flags_))
            continue;
        maths::Vec4 v0{}, v1{}, v2{};
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 0)), &v0);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 1)), &v1);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 2)), &v2);
        maths::Vec4 points[2];
        int kinds[2];
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
        ends[e].point = other->ends[e].point;
        ends[e].edge = other->ends[e].edge;
        ends[e].on_triangle_edge_ = other->ends[e].on_triangle_edge_;
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
    box.append_spot({a.x + o.x, a.y + o.y, a.z + o.z, a.w + o.w});
    box.append_spot({o.x + b.x, b.y + o.y, b.z + o.z, b.w + o.w});
    box.append_spot({static_cast<float>((static_cast<double>(o.x) + a.x) + b.x),
                     static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), (a.z + o.z) + b.z, (a.w + o.w) + b.w});
    const bool use_cache = cache && cache->box_.get_holds(box);
    if (!use_cache) {
        CollGrid* grid = grid_of(rect);
        if (!grid)
            return false;
        grid->objects_in_box(&box); // (the answer isn't used)
    }
    maths::BBox grown;
    grown.min = {box.min.x - 0.5f, box.min.y - 0.5f, box.min.z - 0.5f, box.min.w - 1.0f};
    grown.max = {box.max.x + 0.5f, box.max.y + 0.5f, box.max.z + 0.5f, box.max.w + 1.0f};
    hits->count = 0;
    if (use_cache) {
        for (int i = 0; i < cache->statics_; ++i) {
            const HitMeshCache::TableRow& entry = cache->entries_[i];
            if (grown.intersects(*entry.bounds))
                collide_with_rect(entry.object, rect, &grown, hits);
        }
        return hits->count > 0;
    }
    // the rect's diagonal
    maths::Seg diagonal;
    diagonal.start = o;
    diagonal.end = {static_cast<float>((static_cast<double>(a.x) + o.x) + b.x), (a.y + o.y) + b.y, (a.z + o.z) + b.z,
                    (a.w + o.w) + b.w};
    CollGrid** grids = grids_along(&diagonal);
    if (!grids)
        return false;
    for (; *grids; ++grids) {
        int count = 0;
        for (HitMesh** objects = (*grids)->objects_along_line(&diagonal, &count); *objects; ++objects) {
            if ((*objects)->inside_bounds(&grown))
                collide_with_rect(*objects, rect, &grown, hits);
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
    box.append_spot({a.x + o.x, a.y + o.y, a.z + o.z, a.w + o.w});
    box.append_spot({b.x + o.x, b.y + o.y, b.z + o.z, b.w + o.w});
    box.append_spot({static_cast<float>(b.x + (static_cast<double>(a.x) + o.x)),
                     static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), (a.z + o.z) + b.z, (a.w + o.w) + b.w});
    return rect_against_faces(this, &local, &box, hits, this);
}

// 0x00410AA0
void CollScaled::rect_to_local(const CollRect* rect, CollRect* out) const
{
    inverse_.carry_point_through(out->origin, rect->origin);
    maths::Vec4 corner{};
    const maths::Vec4& o = rect->origin;
    inverse_.carry_point_through(
        corner, {o.x + rect->side_a_.x, rect->side_a_.y + o.y, rect->side_a_.z + o.z, rect->side_a_.w + o.w});
    out->side_a_ = {corner.x - out->origin.x, corner.y - out->origin.y, corner.z - out->origin.z,
                    corner.w - out->origin.w};
    inverse_.carry_point_through(
        corner, {rect->side_b_.x + o.x, rect->side_b_.y + o.y, rect->side_b_.z + o.z, rect->side_b_.w + o.w});
    out->side_b_ = {corner.x - out->origin.x, corner.y - out->origin.y, corner.z - out->origin.z,
                    corner.w - out->origin.w};
}

// 0x0040FEC0
void HitMesh::set_checksum(uint32_t checksum)
{
    checksum_ = checksum;
    if (geometry_)
        geometry_->checksum_ = checksum;
}

// 0x00410BC0
uint8_t* StaticCollider::take_buffer(uint8_t* block, const TriMeshSource* geometry)
{
    unknown14_ = static_cast<int>(geometry->unknown3c_);
    if (geometry->small_faces_)
        return block;
    octets_ = block;
    return block + geometry->tri_count_;
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
    return collide_rect_with_statics(&rect_, &hits_, hit_cache_ ? hit_cache_ : s_rect_cache);
}

// 0x00417C00
void BoxProbe::join_hits()
{
    const int count = hits_.count;
    const RectHit* hits = hits_.hits;
    bool used[RectHits::kMost];
    maths::Vec4 direction[RectHits::kMost];
    // each hit's direction (x, y, z made unit when they have a length; w as it is)
    for (int i = count - 1; i >= 0; --i) {
        used[i] = false;
        const maths::Vec4& p0 = hits[i].ends[0].point;
        const maths::Vec4& p1 = hits[i].ends[1].point;
        maths::Vec4& d = direction[i];
        d = {get_difference(p1.x, p0.x), get_difference(p1.y, p0.y), get_difference(p1.z, p0.z),
             get_difference(p1.w, p0.w)};
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
        if (used[j])
            continue;
        const RectHit& later = hits[j];
        for (int k = j - 1; k >= 0; --k) {
            const RectHit& earlier = hits[k];
            const bool same = (same_point(later.ends[0].point, earlier.ends[0].point) &&
                               same_point(later.ends[1].point, earlier.ends[1].point)) ||
                              (same_point(later.ends[0].point, earlier.ends[1].point) &&
                               same_point(later.ends[1].point, earlier.ends[0].point));
            if (same && dot3(earlier.normal.z, earlier.normal.y, earlier.normal.x, later.normal.z, later.normal.y,
                             later.normal.x) > kParallel)
                used[k] = true;
        }
    }
    // each hit left, extended by the earlier ones it lines up with
    joined_count_ = 0;
    for (int j = count - 1; j >= 0; --j) {
        RectHit joined{};
        joined.copy_from(&hits[j]);
        if (used[j])
            continue;
        for (int k = j - 1; k >= 0; --k) {
            if (used[k])
                continue;
            const RectHit& other = hits[k];
            if (dot3(joined.normal.z, joined.normal.x, joined.normal.y, other.normal.z, other.normal.x,
                     other.normal.y) < kParallel)
                continue;
            if (std::fabs(dot3(direction[j].x, direction[j].z, direction[k].y, direction[k].x, direction[k].z,
                               direction[j].y)) < kParallel)
                continue;
            const RectHitEnd& near0 = other.ends[0];
            const RectHitEnd& close_a = other.ends[1];
            const maths::Vec4& start = joined.ends[0].point;
            const maths::Vec4& end = joined.ends[1].point;
            if (same_point(start, near0.point)) {
                joined.ends[0] = close_a;
            } else if (same_point(start, close_a.point)) {
                joined.ends[0] = near0;
            } else if (same_point(end, near0.point)) {
                joined.ends[1] = close_a;
            } else if (same_point(end, close_a.point)) {
                joined.ends[1] = near0;
            } else {
                continue;
            }
            used[k] = true;
        }
        joined_[joined_count_++].get_assign(&joined);
    }
}

// 0x00417860 (the colour as in ProbeRay::debug_draw; each hit drawn in a colour of its own)
void BoxProbe::debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    const maths::Vec4& o = rect_.origin;
    const maths::Vec4& a = rect_.side_a_;
    const maths::Vec4& s = rect_.side_b_;
    const uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    const maths::Vec4 oa{o.x + a.x, a.y + o.y, a.z + o.z, a.w + o.w};
    const maths::Vec4 oab{oa.x + s.x, oa.y + s.y, oa.z + s.z, oa.w + s.w};
    const maths::Vec4 ob{o.x + s.x, s.y + o.y, s.z + o.z, s.w + o.w};
    gfx::push_diag_line(&o, &oa, colour, colour, frames);
    gfx::push_diag_line(&oa, &oab, colour, colour, frames);
    gfx::push_diag_line(&oab, &ob, colour, colour, frames);
    gfx::push_diag_line(&ob, &o, colour, colour, frames);
    const RectHit* hits = joined_count_ > 0 ? joined_ : hits_.hits;
    const int count = joined_count_ > 0 ? joined_count_ : hits_.count;
    for (int i = count - 1; i >= 0; --i) {
        b += 0x1F;
        g += 0x63;
        r += 0xC9;
        const uint32_t hit_colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
        gfx::push_diag_line(&hits[i].ends[0].point, &hits[i].ends[1].point, hit_colour, hit_colour, 1);
    }
}

} // namespace render
