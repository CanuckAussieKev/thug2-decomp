// The rect queries and the rect feeler. See coll_rect.h.

#include "game/nx/coll_rect.h"

#include <cmath>
#include <cstring>

#include "game/gfx/debug_lines.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// The cache the rect feelers use when they have none (0x0069198C).
CCollCache*& s_rectCache = GameVariable<CCollCache*>(0x0069198C);

// Hit ends this close on every axis are the same point; normals and directions this close are parallel.
constexpr float kSamePoint = 0.1f;   // 0x00645D74
constexpr float kParallel = 0.99f;   // 0x00645450

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The scene grid holding the rect (0x0048C940; null: none) and the grids along a line (0x0048C980).
CCollGrid* GridOf(const CollRect* rect) { return GameFunction<CCollGrid*(__cdecl*)(const CollRect*)>(0x0048C940)(rect); }
CCollGrid** GridsAlong(const Mth::Line* line)
{
    return GameFunction<CCollGrid**(__cdecl*)(const Mth::Line*)>(0x0048C980)(line);
}

bool CollideWithRect(CCollObj* object, const CollRect* rect, const Mth::BBox* box, RectHits* hits)
{
    return object->Call<bool>(15, rect, box, hits);
}

// One face's crossing (points and kinds from RectHitsTriangle) added to the hits: the face's normal (v1 - v0) x
// (v2 - v0) normalised (an entry is only counted when it has one), its flag 0x40 and the object's checksum, and each
// end's triangle edge. For an instance (`placed`) the points are scaled and placed, the normals turned back.
void AddRectHit(RectHits* hits, const Mth::Vector& v0, const Mth::Vector& v1, const Mth::Vector& v2,
                const Mth::Vector* points, const int* kinds, uint16_t faceFlags, uint32_t checksum,
                const CCollScaled* placed)
{
    RectHit& hit = hits->hits[hits->count];
    double e2x = static_cast<double>(v2.x) - v0.x;
    double e2y = static_cast<double>(v2.y) - v0.y;
    double e2z = static_cast<double>(v2.z) - v0.z;
    float e1x = Difference(v1.x, v0.x);
    float e1y = Difference(v1.y, v0.y);
    double e1z = static_cast<double>(v1.z) - v0.z;
    Mth::Vector normal{static_cast<float>(e1y * e2z - e1z * e2y), static_cast<float>(e1z * e2x - e2z * e1x),
                       static_cast<float>(e2y * e1x - e1y * e2x), 0.0f};
    hit.normal = normal.Normalize();
    double lengthSquared = (static_cast<double>(hit.normal.x) * hit.normal.x +
                            static_cast<double>(hit.normal.y) * hit.normal.y) +
                           static_cast<double>(hit.normal.z) * hit.normal.z;
    if (lengthSquared <= 0.0)
        return;
    hit.faceFlag40 = static_cast<uint8_t>(faceFlags & 0x40);
    hit.checksum = checksum;
    hit.ends[0].point = points[0];
    hit.ends[1].point = points[1];
    if (placed) {
        for (RectHitEnd& end : hit.ends) {
            Mth::Vector scaled{end.point.x * placed->m_scale.x, placed->m_scale.y * end.point.y,
                               placed->m_scale.z * end.point.z, placed->m_scale.w * end.point.w};
            placed->m_matrix.TransformAsPos(end.point, scaled);
        }
        Mth::Vector turned;
        placed->m_matrix.RotateVector(turned, hit.normal);
        hit.normal = turned;
    }
    for (int e = 1; e >= 0; --e) {
        RectHitEnd& end = hit.ends[e];
        end.onTriangleEdge = kinds[e] != 0;
        Mth::Vector edge;
        switch (kinds[e]) {
        case 1:
            edge = {e1x, e1y, static_cast<float>(e1z), Difference(v1.w, v0.w)};
            break;
        case 2:
            edge = {Difference(v2.x, v1.x), Difference(v2.y, v1.y), Difference(v2.z, v1.z), Difference(v2.w, v1.w)};
            break;
        case 3:
            edge = {Difference(v0.x, v2.x), Difference(v0.y, v2.y), Difference(v0.z, v2.z), Difference(v0.w, v2.w)};
            break;
        default:
            continue;
        }
        end.edge = edge.Normalize();
        if (placed) {
            Mth::Vector turned;
            placed->m_matrix.RotateVector(turned, end.edge);
            end.edge = turned;
        }
    }
    ++hits->count;
}

// A sector's (or an instance's, in its space) faces against the rect: each crossing a hit. True as soon as one is
// found with the list full.
bool RectAgainstFaces(const CCollObj* object, const CollRect* rect, const Mth::BBox* box, RectHits* hits,
                      const CCollScaled* placed)
{
    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = object->m_geometry->GetFacesInBox(box, &count, buffer, 0x800);
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const CCollObjTriData* geometry = object->m_geometry;
        uint32_t face = faces[i];
        uint16_t flags = *reinterpret_cast<const uint16_t*>(geometry->m_faces + face * (geometry->m_smallFaces ? 8 : 10));
        if ((flags & hits->ignoreFlags) || (~flags & hits->requireFlags))
            continue;
        Mth::Vector v0, v1, v2;
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 0), &v0);
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 1), &v1);
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 2), &v2);
        Mth::Vector points[2];
        int kinds[2];
        if (!RectHitsTriangle(*rect, v0, v1, v2, points, kinds))
            continue;
        if (hits->count == RectHits::kMost)
            return true;
        AddRectHit(hits, v0, v1, v2, points, kinds, flags, object->m_checksum, placed);
    }
    return hits->count > 0;
}

// 0x00411840 (thiscall on the vector): turned by the matrix's rotation, in place. Returns it.
struct VectorTurn : Mth::Vector {
    Mth::Vector* TurnBy(const Mth::Matrix* matrix)
    {
        Mth::Vector turned;
        matrix->RotateVector(turned, *this);
        *static_cast<Mth::Vector*>(this) = turned;
        return this;
    }
};

bool Near(float a, float b) { return std::fabs(static_cast<double>(a) - b) <= kSamePoint; }
bool SamePoint(const Mth::Vector& a, const Mth::Vector& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

double Dot3(float ax, float ay, float az, float bx, float by, float bz)
{
    return (static_cast<double>(ax) * bx + static_cast<double>(ay) * by) + static_cast<double>(az) * bz;
}

} // namespace

// 0x00417AD0
RectHit* RectHit::CopyFrom(const RectHit* other)
{
    for (int e = 0; e < 2; ++e) {
        ends[e].point = other->ends[e].point;
        ends[e].edge = other->ends[e].edge;
        ends[e].onTriangleEdge = other->ends[e].onTriangleEdge;
    }
    normal = other->normal;
    checksum = other->checksum;
    faceFlag40 = other->faceFlag40;
    return this;
}

// 0x00417B60
RectHit* RectHit::Assign(const RectHit* other) { return CopyFrom(other); }

// 0x0040FEE0 (the far corner's x part, and its y part for the box, summed in double precision first)
bool CollideRectWithStatics(const CollRect* rect, RectHits* hits, CCollCache* cache)
{
    const Mth::Vector& o = rect->origin;
    const Mth::Vector& a = rect->sideA;
    const Mth::Vector& b = rect->sideB;
    Mth::BBox box(o);
    box.AddPoint({a.x + o.x, a.y + o.y, a.z + o.z, a.w + o.w});
    box.AddPoint({o.x + b.x, b.y + o.y, b.z + o.z, b.w + o.w});
    box.AddPoint({static_cast<float>((static_cast<double>(o.x) + a.x) + b.x),
                  static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), (a.z + o.z) + b.z, (a.w + o.w) + b.w});
    bool useCache = cache && cache->m_box.Contains(box);
    if (!useCache) {
        CCollGrid* grid = GridOf(rect);
        if (!grid)
            return false;
        grid->ObjectsInBox(&box); // (the answer isn't used)
    }
    Mth::BBox grown;
    grown.min = {box.min.x - 0.5f, box.min.y - 0.5f, box.min.z - 0.5f, box.min.w - 1.0f};
    grown.max = {box.max.x + 0.5f, box.max.y + 0.5f, box.max.z + 0.5f, box.max.w + 1.0f};
    hits->count = 0;
    if (useCache) {
        for (int i = 0; i < cache->m_statics; ++i) {
            const CCollCache::Entry& entry = cache->m_entries[i];
            if (grown.Intersects(*static_cast<const Mth::BBox*>(entry.geometry)))
                CollideWithRect(entry.object, rect, &grown, hits);
        }
        return hits->count > 0;
    }
    // the rect's diagonal
    Mth::Line diagonal;
    diagonal.start = o;
    diagonal.end = {static_cast<float>((static_cast<double>(a.x) + o.x) + b.x), (a.y + o.y) + b.y, (a.z + o.z) + b.z,
                    (a.w + o.w) + b.w};
    CCollGrid** grids = GridsAlong(&diagonal);
    if (!grids)
        return false;
    for (; *grids; ++grids) {
        int count;
        for (CCollObj** objects = (*grids)->ObjectsAlongLine(&diagonal, &count); *objects; ++objects) {
            if ((*objects)->Call<bool>(13, &grown))
                CollideWithRect(*objects, rect, &grown, hits);
        }
    }
    return hits->count > 0;
}

// 0x00410C90
bool CCollStatic::CollideWithRect(const CollRect* rect, const Mth::BBox* box, RectHits* hits)
{
    return RectAgainstFaces(this, rect, box, hits, nullptr);
}

// 0x004128B0
bool CCollStaticInstance::CollideWithRect(const CollRect* rect, const Mth::BBox*, RectHits* hits)
{
    CollRect local;
    RectToLocal(rect, &local);
    const Mth::Vector& o = local.origin;
    const Mth::Vector& a = local.sideA;
    const Mth::Vector& b = local.sideB;
    Mth::BBox box(o);
    box.AddPoint({a.x + o.x, a.y + o.y, a.z + o.z, a.w + o.w});
    box.AddPoint({b.x + o.x, b.y + o.y, b.z + o.z, b.w + o.w});
    box.AddPoint({static_cast<float>(b.x + (static_cast<double>(a.x) + o.x)),
                  static_cast<float>((static_cast<double>(a.y) + o.y) + b.y), (a.z + o.z) + b.z, (a.w + o.w) + b.w});
    return RectAgainstFaces(this, &local, &box, hits, this);
}

// 0x00410AA0
void CCollScaled::RectToLocal(const CollRect* rect, CollRect* out) const
{
    m_inverse.TransformAsPos(out->origin, rect->origin);
    Mth::Vector corner;
    const Mth::Vector& o = rect->origin;
    m_inverse.TransformAsPos(corner, {o.x + rect->sideA.x, rect->sideA.y + o.y, rect->sideA.z + o.z,
                                      rect->sideA.w + o.w});
    out->sideA = {corner.x - out->origin.x, corner.y - out->origin.y, corner.z - out->origin.z,
                  corner.w - out->origin.w};
    m_inverse.TransformAsPos(corner, {rect->sideB.x + o.x, rect->sideB.y + o.y, rect->sideB.z + o.z,
                                      rect->sideB.w + o.w});
    out->sideB = {corner.x - out->origin.x, corner.y - out->origin.y, corner.z - out->origin.z,
                  corner.w - out->origin.w};
}

// 0x0040FEC0
void CCollObj::SetChecksum(uint32_t checksum)
{
    m_checksum = checksum;
    if (m_geometry)
        m_geometry->m_checksum = checksum;
}

// 0x00410BC0
uint8_t* CCollStatic::TakeBuffer(uint8_t* block, const CCollObjTriData* geometry)
{
    m_unknown14 = static_cast<int>(geometry->m_unknown3C);
    if (geometry->m_smallFaces)
        return block;
    m_buffer = block;
    return block + geometry->m_numFaces;
}

// 0x004182D0
CRectFeeler* CRectFeeler::Construct()
{
    mp_cache = nullptr;
    m_hits.count = 0;
    m_joinedCount = 0;
    m_hits.requireFlags = 0;
    m_hits.ignoreFlags = 0x10;
    return this;
}

// 0x00417840
bool CRectFeeler::Query(int)
{
    return CollideRectWithStatics(&m_rect, &m_hits, mp_cache ? mp_cache : s_rectCache);
}

// 0x00417C00
void CRectFeeler::JoinHits()
{
    int count = m_hits.count;
    const RectHit* hits = m_hits.hits;
    bool used[RectHits::kMost];
    Mth::Vector direction[RectHits::kMost];
    // each hit's direction (x, y, z made unit when they have a length; w as it is)
    for (int i = count - 1; i >= 0; --i) {
        used[i] = false;
        const Mth::Vector& p0 = hits[i].ends[0].point;
        const Mth::Vector& p1 = hits[i].ends[1].point;
        Mth::Vector& d = direction[i];
        d = {Difference(p1.x, p0.x), Difference(p1.y, p0.y), Difference(p1.z, p0.z), Difference(p1.w, p0.w)};
        double length = std::sqrt((static_cast<double>(d.y) * d.y + static_cast<double>(d.x) * d.x) +
                                  static_cast<double>(d.z) * d.z);
        if (length > 0.0) {
            double inverse = 1.0 / length;
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
            bool same = (SamePoint(later.ends[0].point, earlier.ends[0].point) &&
                         SamePoint(later.ends[1].point, earlier.ends[1].point)) ||
                        (SamePoint(later.ends[0].point, earlier.ends[1].point) &&
                         SamePoint(later.ends[1].point, earlier.ends[0].point));
            if (same && Dot3(earlier.normal.z, earlier.normal.y, earlier.normal.x, later.normal.z, later.normal.y,
                             later.normal.x) > kParallel)
                used[k] = true;
        }
    }
    // each hit left, extended by the earlier ones it lines up with
    m_joinedCount = 0;
    for (int j = count - 1; j >= 0; --j) {
        RectHit joined;
        joined.CopyFrom(&hits[j]);
        if (used[j])
            continue;
        for (int k = j - 1; k >= 0; --k) {
            if (used[k])
                continue;
            const RectHit& other = hits[k];
            if (Dot3(joined.normal.z, joined.normal.x, joined.normal.y, other.normal.z, other.normal.x,
                     other.normal.y) < kParallel)
                continue;
            if (std::fabs(Dot3(direction[j].x, direction[j].z, direction[k].y, direction[k].x, direction[k].z,
                               direction[j].y)) < kParallel)
                continue;
            const RectHitEnd& near0 = other.ends[0];
            const RectHitEnd& near1 = other.ends[1];
            Mth::Vector& start = joined.ends[0].point;
            Mth::Vector& end = joined.ends[1].point;
            if (SamePoint(start, near0.point)) {
                joined.ends[0] = near1;
            } else if (SamePoint(start, near1.point)) {
                joined.ends[0] = near0;
            } else if (SamePoint(end, near0.point)) {
                joined.ends[1] = near1;
            } else if (SamePoint(end, near1.point)) {
                joined.ends[1] = near0;
            } else {
                continue;
            }
            used[k] = true;
        }
        m_joined[m_joinedCount++].Assign(&joined);
    }
}

// 0x00417860 (the colour as in CFeeler::DebugDraw; each hit drawn in a colour of its own)
void CRectFeeler::DebugDraw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    const Mth::Vector& o = m_rect.origin;
    const Mth::Vector& a = m_rect.sideA;
    const Mth::Vector& s = m_rect.sideB;
    uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    Mth::Vector oa{o.x + a.x, a.y + o.y, a.z + o.z, a.w + o.w};
    Mth::Vector oab{oa.x + s.x, oa.y + s.y, oa.z + s.z, oa.w + s.w};
    Mth::Vector ob{o.x + s.x, s.y + o.y, s.z + o.z, s.w + o.w};
    Gfx::AddDebugLine(&o, &oa, colour, colour, frames);
    Gfx::AddDebugLine(&oa, &oab, colour, colour, frames);
    Gfx::AddDebugLine(&oab, &ob, colour, colour, frames);
    Gfx::AddDebugLine(&ob, &o, colour, colour, frames);
    const RectHit* hits = m_joinedCount > 0 ? m_joined : m_hits.hits;
    int count = m_joinedCount > 0 ? m_joinedCount : m_hits.count;
    for (int i = count - 1; i >= 0; --i) {
        b += 0x1F;
        g += 0x63;
        r += 0xC9;
        uint32_t hitColour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
        Gfx::AddDebugLine(&hits[i].ends[0].point, &hits[i].ends[1].point, hitColour, hitColour, 1);
    }
}

} // namespace Nx

REPLACE(0x00417AD0, &Nx::RectHit::CopyFrom);
REPLACE(0x00417B60, &Nx::RectHit::Assign);
REPLACE(0x0040FEE0, Nx::CollideRectWithStatics);
REPLACE(0x00410C90, &Nx::CCollStatic::CollideWithRect);
REPLACE(0x004128B0, &Nx::CCollStaticInstance::CollideWithRect);
REPLACE(0x00410AA0, &Nx::CCollScaled::RectToLocal);
REPLACE(0x00411840, &Nx::VectorTurn::TurnBy);
REPLACE(0x0040FEC0, &Nx::CCollObj::SetChecksum);
REPLACE(0x00410BC0, &Nx::CCollStatic::TakeBuffer);
REPLACE(0x004182D0, &Nx::CRectFeeler::Construct);
REPLACE(0x00417840, &Nx::CRectFeeler::Query);
REPLACE(0x00417C00, &Nx::CRectFeeler::JoinHits);
REPLACE(0x00417860, &Nx::CRectFeeler::DebugDraw);
