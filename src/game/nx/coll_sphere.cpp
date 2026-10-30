// The sphere queries and the sphere feeler. See coll_sphere.h. The arithmetic follows the original's x87 code:
// values are rounded to floats where it stores them, kept in double precision where it keeps them on its stack.

#include "game/nx/coll_sphere.h"

#include <cmath>

#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "game/gfx/engine_scene_list.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

namespace {

GAME_GLOBAL(CCollCache*, g_sphereCache, 0x00691990);

// The cache the sphere feelers use when they have none (0x00691990).
CCollCache*& s_sphereCache = g_sphereCache;

constexpr float kTiny = 0.01f; // 0x0064544C: a zero divisor replaced

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
Mth::Vector Minus(const Mth::Vector& a, const Mth::Vector& b)
{
    return {Difference(a.x, b.x), Difference(a.y, b.y), Difference(a.z, b.z), Difference(a.w, b.w)};
}

// The scene grid holding the sphere (0x0048C940) and the grids along a line (0x0048C980).
CCollGrid* GridOf(const CollSphere*) { return GetFirstCollGrid(); }
CCollGrid** GridsAlong(const Mth::Line* line)
{
    return GetCollGridsOnLine(line);
}

bool CollideWithSphere(CCollObj* object, const CollSphere* sphere, const Mth::BBox* box, SphereHits* hits)
{
    return object->Call<bool>(16, sphere, box, hits);
}

// Whether a value is a parameter along a segment (NaN isn't).
bool Within01(float value) { return !(value < 0.0f) && !(value > 1.0f) && value == value; }

// A line from `start` to `end`.
Mth::Line Segment(const Mth::Vector& start, const Mth::Vector& end)
{
    Mth::Line line;
    line.start = start;
    line.end = end;
    return line;
}

} // namespace

// 0x0040E280
bool SegmentCrossesSphere(const Mth::Line* segment, const CollSphere* sphere, int* count, Mth::Vector* points, int)
{
    const Mth::Vector& start = segment->start;
    double mx = static_cast<double>(start.x) - sphere->centre.x;
    double my = static_cast<double>(start.y) - sphere->centre.y;
    double mz = static_cast<double>(start.z) - sphere->centre.z;
    Mth::Vector d = Minus(segment->end, start);
    float a = static_cast<float>((static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y) +
                                 static_cast<double>(d.x) * d.x);
    float b = static_cast<float>((d.z * mz + d.x * mx) + d.y * my);
    double radius = sphere->radius;
    float discriminant = static_cast<float>(static_cast<double>(b) * b -
                                            (((mz * mz + mx * mx) + my * my) - radius * radius) * a);
    if (discriminant < 0.0f) {
        *count = 0;
        return false;
    }
    if (discriminant > 0.0f) {
        double root = std::sqrt(static_cast<double>(discriminant));
        double inverse = 1.0 / a;
        float t0 = static_cast<float>((-static_cast<double>(b) - root) * inverse);
        float t1 = static_cast<float>((root - b) * inverse);
        if (t0 > 1.0f || t1 < 0.0f) {
            *count = 0;
            return false;
        }
        if (!(t0 >= 0.0f)) {
            *count = 1;
            points[0] = start + t1 * d;
        } else if (t1 > 1.0f) {
            *count = 1;
            points[0] = start + t0 * d;
        } else {
            *count = 2;
            points[0] = start + t0 * d;
            points[1] = start + t1 * d;
        }
        return true;
    }
    // grazing (or NaN)
    float t = -(b / a);
    if (!(0.0f <= t) || !(t <= 1.0f)) {
        *count = 0;
        return false;
    }
    *count = 1;
    points[0] = start + t * d;
    return true;
}

// 0x0040E560
bool SweptSphereHitsEdge(const Mth::Line* edge, const Mth::Vector* edgeMotion, const CollSphere* sphere,
                         const Mth::Vector* motion, float* t, float /*limit*/, int* count, Mth::Vector* point)
{
    if (SegmentCrossesSphere(edge, sphere, count, point, 0))
        return true;
    Mth::Capsule capsule(*edge, sphere->radius);
    const Mth::Vector& centre = sphere->centre;
    // the centre's path relative to the edge
    Mth::Line path;
    path.start = centre;
    path.end = {static_cast<float>((static_cast<double>(motion->x) - edgeMotion->x) + centre.x),
                Difference(motion->y, edgeMotion->y) + centre.y, Difference(motion->z, edgeMotion->z) + centre.z,
                Difference(motion->w, edgeMotion->w) + centre.w};
    float s, u;
    if (!capsule.IntersectsSegment(path, &s, &u, nullptr))
        return false;
    if (Within01(s)) {
        *count = Within01(u) ? 2 : 1;
    } else if (Within01(u)) {
        *count = 1;
        s = u;
    }
    *t = s;
    if (*count == 2 && u < s)
        *t = u;
    // the centre then, the edge then, and the edge's point nearest the centre
    float time = *t;
    double ax = static_cast<double>(time) * motion->x + centre.x;
    double ay = static_cast<double>(time * motion->y) + centre.y;
    double az = static_cast<double>(time * motion->z) + centre.z;
    const Mth::Vector& start = edge->start;
    Mth::Vector moved{static_cast<float>(static_cast<double>(time) * edgeMotion->x + start.x),
                      time * edgeMotion->y + start.y, time * edgeMotion->z + start.z, time * edgeMotion->w + start.w};
    Mth::Vector along = Minus(edge->end, start);
    float k = static_cast<float>((static_cast<double>(along.z) * moved.z + static_cast<double>(along.y) * moved.y) +
                                 static_cast<double>(along.x) * moved.x);
    float far = static_cast<float>(((static_cast<double>(along.z) * along.z + static_cast<double>(along.y) * along.y) +
                                    static_cast<double>(along.x) * along.x) +
                                   k);
    float m = static_cast<float>((along.z * az + along.y * ay) + along.x * ax);
    if (m < k) {
        *point = moved;
    } else if (m > far) {
        *point = {along.x + moved.x, along.y + moved.y, along.z + moved.z, along.w + moved.w};
    } else {
        float fraction = static_cast<float>((static_cast<double>(m) - k) / (static_cast<double>(far) - k));
        Mth::Vector part = fraction * along;
        *point = {moved.x + part.x, moved.y + part.y, moved.z + part.z, moved.w + part.w};
    }
    *count = 1;
    return true;
}

// 0x0040E940
bool SweptSphereHitsCorner(const Mth::Vector* corner, const CollSphere* sphere, const Mth::Vector* motion,
                           const Mth::Vector* edgeMotion, const Mth::Vector* v0, const Mth::Vector* v1,
                           const Mth::Vector* v2, const Mth::Vector* edgeCross, const Mth::Vector* edge, float* t,
                           float limit, int* count, Mth::Vector* point, Mth::Vector*)
{
    const Mth::Vector& centre = sphere->centre;
    float mx = Difference(corner->x, centre.x), my = Difference(corner->y, centre.y),
          mz = Difference(corner->z, centre.z);
    double radius = sphere->radius;
    double distance2 = (static_cast<double>(mz) * mz + static_cast<double>(mx) * mx) + static_cast<double>(my) * my;
    if (radius * radius > distance2) {
        *t = 0.0f; // (the corner already inside the sphere)
        return false;
    }
    float ux = Difference(motion->x, edgeMotion->x);
    double uy = static_cast<double>(motion->y) - edgeMotion->y;
    double uzExact = static_cast<double>(motion->z) - edgeMotion->z;
    float uz = static_cast<float>(uzExact);
    double towards = (uzExact * mz + uy * my) + static_cast<double>(ux) * mx;
    if (towards <= 0.0)
        return false; // (moving away from the corner)
    const Mth::Vector& c = *edgeCross;
    double k1 = (static_cast<double>(c.z) * corner->z + static_cast<double>(c.x) * corner->x) +
                static_cast<double>(c.y) * corner->y;
    double k2 = (static_cast<double>(centre.z) * c.z + static_cast<double>(c.y) * centre.y) +
                static_cast<double>(c.x) * centre.x;
    double k3 = (uy * c.y + static_cast<double>(uz) * c.z) + static_cast<double>(ux) * c.x;
    if (k3 == 0.0)
        k3 = kTiny;
    double s = (k1 - k2) / k3;
    // where the path crosses the edge's plane
    double px = static_cast<double>(ux) * s + centre.x;
    double py = static_cast<double>(static_cast<float>(uy * s)) + centre.y;
    double pz = static_cast<double>(static_cast<float>(static_cast<double>(uz) * s)) + centre.z;
    float n1 = static_cast<float>((static_cast<double>(edge->z) * corner->z + static_cast<double>(edge->x) * corner->x) +
                                  static_cast<double>(edge->y) * corner->y);
    float n2 = static_cast<float>((px * edge->x + pz * edge->z) + py * edge->y);
    Mth::Line line;
    if (!(n2 >= n1)) { // (NaN too)
        // the edge arriving at the corner
        if (*corner == *v0)
            line.start = *v2;
        else if (*corner == *v1)
            line.start = *v0;
        else if (*corner == *v2)
            line.start = *v1;
        else
            line.start = *corner; // (the original leaves it unset)
        line.end = *corner;
    } else {
        // the edge leaving it
        line.start = *corner;
        if (*corner == *v0)
            line.end = *v1;
        else if (*corner == *v1)
            line.end = *v2;
        else if (*corner == *v2)
            line.end = *v0;
        else
            line.end = *corner; // (the original leaves it unset)
    }
    Mth::Vector touched[2];
    if (!SweptSphereHitsEdge(&line, edgeMotion, sphere, motion, t, limit, count, touched))
        return false;
    *point = touched[0];
    return true;
}

// 0x0040EC90
bool SweptSphereHitsTriangle(const CollSphere* sphere, const Mth::Vector* v0, const Mth::Vector* v1,
                             const Mth::Vector* v2, float* t, float limit, const Mth::Vector* motion,
                             Mth::Vector* point, Mth::Vector* normal)
{
    const Mth::Vector& centre = sphere->centre;
    Mth::Vector e01 = Minus(*v1, *v0), e12 = Minus(*v2, *v1), e20 = Minus(*v0, *v2);
    double e2x = static_cast<double>(v2->x) - v0->x;
    double e2y = static_cast<double>(v2->y) - v0->y;
    double e2z = static_cast<double>(v2->z) - v0->z;
    float e1x = Difference(v1->x, v0->x), e1y = Difference(v1->y, v0->y);
    double e1z = static_cast<double>(v1->z) - v0->z;
    float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    float ny = static_cast<float>(e1z * e2x - e2z * e1x);
    double nzExact = e2y * e1x - e1y * e2x;
    *normal = {nx, ny, static_cast<float>(nzExact), 0.0f};
    double length2 = (nzExact * nzExact + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx;
    if (length2 <= 0.0)
        return false;
    const Mth::Vector& n = *normal;
    float centreAlong = static_cast<float>((static_cast<double>(n.z) * centre.z + static_cast<double>(n.x) * centre.x) +
                                           static_cast<double>(centre.y) * n.y);
    float reach2 = static_cast<float>(static_cast<double>(sphere->radius) * sphere->radius * length2);
    float planeAlong = static_cast<float>((static_cast<double>(v0->z) * n.z + static_cast<double>(n.y) * v0->y) +
                                          static_cast<double>(n.x) * v0->x);
    float distance = centreAlong - planeAlong;
    const Mth::Vector zero{0.0f, 0.0f, 0.0f, 1.0f};
    // each edge crossed with the (unnormalised) normal: pointing out of the triangle
    auto across = [&](const Mth::Vector& e) {
        return Mth::Vector{static_cast<float>(static_cast<double>(e.z) * n.y - static_cast<double>(e.y) * n.z),
                           static_cast<float>(static_cast<double>(e.x) * n.z - static_cast<double>(e.z) * n.x),
                           static_cast<float>(static_cast<double>(e.y) * n.x - static_cast<double>(e.x) * n.y), 0.0f};
    };
    Mth::Vector c01 = across(e01), c12 = across(e12), c20 = across(e20);
    normal->Normalize();
    // which side of each edge `p` is (inside: at or behind the edge's line)
    auto inside01 = [&](double along) {
        return (static_cast<double>(c01.y) * v0->y + static_cast<double>(c01.z) * v0->z) +
                   static_cast<double>(c01.x) * v0->x <= along;
    };
    auto inside12 = [&](double along) {
        return (static_cast<double>(c12.z) * v1->z + static_cast<double>(c12.x) * v1->x) +
                   static_cast<double>(c12.y) * v1->y <= along;
    };
    auto inside20 = [&](double along) {
        return (static_cast<double>(c20.x) * v2->x + static_cast<double>(c20.z) * v2->z) +
                   static_cast<double>(c20.y) * v2->y <= along;
    };
    auto edge = [&](const Mth::Vector& a, const Mth::Vector& b) {
        Mth::Line line = Segment(a, b);
        int touched;
        Mth::Vector hit[2];
        if (!SweptSphereHitsEdge(&line, &zero, sphere, motion, t, limit, &touched, hit))
            return false;
        *point = hit[0];
        return true;
    };
    auto corner = [&](const Mth::Vector* at, Mth::Vector* cross, Mth::Vector* e) {
        int touched = 0;
        return SweptSphereHitsCorner(at, sphere, motion, &zero, v0, v1, v2, cross, e, t, limit, &touched, point,
                                     normal);
    };

    if (!(static_cast<double>(distance) * distance <= reach2) && distance == distance && reach2 == reach2) {
        // apart from the plane: by where the centre lies
        bool in01 = inside01((static_cast<double>(c01.z) * centre.z + static_cast<double>(c01.x) * centre.x) +
                             static_cast<double>(c01.y) * centre.y);
        bool in12 = inside12((static_cast<double>(c12.z) * centre.z + static_cast<double>(c12.x) * centre.x) +
                             static_cast<double>(c12.y) * centre.y);
        bool in20 = inside20((static_cast<double>(c20.z) * centre.z + static_cast<double>(c20.x) * centre.x) +
                             static_cast<double>(c20.y) * centre.y);
        if (in01 && in12 && in20) {
            float s, u;
            PointTriangleDistanceSquared(centre, *v0, *v1, *v2, &s, &u);
            Mth::Vector across2 = u * (*v2 - *v0);
            *point = (*v0 + s * (*v1 - *v0)) + across2;
            Mth::Capsule path(centre, *motion, sphere->radius);
            return path.ContainsPoint(*point, nullptr);
        }
        if (in01 && in12)
            return edge(*v2, *v0);
        if (in01 && in20)
            return edge(*v1, *v2);
        if (in01)
            return corner(v2, &c20, &e20);
        if (in12 && in20)
            return edge(*v0, *v1);
        if (in12)
            return corner(v0, &c01, &e01);
        if (in20)
            return corner(v1, &c12, &e12);
        return false;
    }

    // within reach of the plane: where the sphere's front meets it along the move
    Mth::Vector move{motion->x, motion->y, motion->z, Difference(motion->w, 1.0f)};
    bool above = centreAlong > planeAlong;
    double approach = (static_cast<double>(move.x) * n.x + static_cast<double>(move.z) * n.z) +
                      static_cast<double>(move.y) * n.y;
    if (above ? !(approach < 0.0) && approach == approach : !(approach > 0.0) && approach == approach)
        return false;
    Mth::Vector front = centre - sphere->radius * *normal;
    double planeAt = (static_cast<double>(v0->z) * n.z + static_cast<double>(n.y) * v0->y) +
                     static_cast<double>(n.x) * v0->x;
    double frontAt = (static_cast<double>(front.x) * n.x + static_cast<double>(front.z) * n.z) +
                     static_cast<double>(front.y) * n.y;
    float time = static_cast<float>((planeAt - frontAt) / static_cast<float>(approach));
    Mth::Vector meet{static_cast<float>(static_cast<double>(move.x) * time + front.x), move.y * time + front.y,
                     move.z * time + front.z, move.w * time + front.w};
    auto alongOf = [&](const Mth::Vector& cr) {
        return (static_cast<double>(meet.z) * cr.z + static_cast<double>(meet.y) * cr.y) +
               static_cast<double>(meet.x) * cr.x;
    };
    bool in01 = inside01(alongOf(c01)), in12 = inside12(alongOf(c12)), in20 = inside20(alongOf(c20));
    auto cornerLine = [&](const Mth::Vector* at) {
        if (!LineHitsSphere(*at, zero, *sphere, *motion, t, limit))
            return false;
        *point = *at + *t * zero;
        return true;
    };
    if (in01 && in12 && in20) {
        if (time > limit)
            return false;
        *t = time;
        *point = meet + time * zero;
        return true;
    }
    if (in01 && in12)
        return edge(*v2, *v0);
    if (in01 && in20)
        return edge(*v1, *v2);
    if (in01)
        return cornerLine(v2);
    if (in12 && in20)
        return edge(*v0, *v1);
    if (in12)
        return cornerLine(v0);
    if (in20)
        return cornerLine(v1);
    return false;
}

// 0x0040F9E0
bool SphereHitsTriangle(const CollSphere* sphere, const Mth::Vector* v0, const Mth::Vector* v1, const Mth::Vector* v2,
                        Mth::Vector* point, Mth::Vector* normal)
{
    double e2x = static_cast<double>(v2->x) - v0->x;
    double e2y = static_cast<double>(v2->y) - v0->y;
    double e2z = static_cast<double>(v2->z) - v0->z;
    float e1x = Difference(v1->x, v0->x), e1y = Difference(v1->y, v0->y);
    double e1z = static_cast<double>(v1->z) - v0->z;
    float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    float ny = static_cast<float>(e1z * e2x - e2z * e1x);
    double nzExact = e2y * e1x - e1y * e2x;
    *normal = {nx, ny, static_cast<float>(nzExact), 0.0f};
    if ((nzExact * nzExact + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx <= 0.0)
        return false;
    const Mth::Vector& c = sphere->centre;
    double dx = static_cast<double>(v0->x) - c.x, dy = static_cast<double>(v0->y) - c.y,
           dz = static_cast<double>(v0->z) - c.z;
    if ((dy * normal->y + dx * normal->x) + dz * normal->z >= 0.0)
        return false; // (behind the face)
    normal->Normalize();
    float s, u;
    double distance2 = PointTriangleDistanceSquared(c, *v0, *v1, *v2, &s, &u);
    // v0 + s (v1 - v0) + u (v2 - v0), rounded as the original
    double f2x = static_cast<double>(v2->x) - v0->x, f2y = static_cast<double>(v2->y) - v0->y;
    float f2z = Difference(v2->z, v0->z), f2w = Difference(v2->w, v0->w);
    float g2x = static_cast<float>(f2x * u);
    double g2y = f2y * u;
    float g2z = f2z * u, g2w = f2w * u;
    double f1x = static_cast<double>(v1->x) - v0->x, f1y = static_cast<double>(v1->y) - v0->y;
    float f1z = Difference(v1->z, v0->z), f1w = Difference(v1->w, v0->w);
    float g1x = static_cast<float>(f1x * s);
    double g1y = f1y * s;
    double g1z = static_cast<double>(f1z) * s;
    float g1w = f1w * s;
    Mth::Vector base{g1x + v0->x, static_cast<float>(g1y + v0->y), static_cast<float>(g1z + v0->z), g1w + v0->w};
    *point = {base.x + g2x, static_cast<float>(g2y + base.y), base.z + g2z, base.w + g2w};
    double radius = sphere->radius;
    return distance2 <= radius * radius;
}

// 0x004110B0 (slot 16 of a sector and of an instance - the instance's faces taken as they are, unplaced)
bool CCollStatic::CollideWithSphere(const CollSphere* sphere, const Mth::BBox* box, SphereHits* hits)
{
    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = m_geometry->GetFacesInBox(box, &count, buffer, 0x800);
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const CCollObjTriData* geometry = m_geometry;
        uint32_t face = faces[i];
        uint16_t flags = *reinterpret_cast<const uint16_t*>(geometry->m_faces + face * (geometry->m_smallFaces ? 8 : 10));
        if ((flags & hits->ignoreFlags) || (~flags & hits->requireFlags))
            continue;
        Mth::Vector v0, v1, v2;
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 0), &v0);
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 1), &v1);
        geometry->GetRawVertexPos(geometry->FaceVertex(face, 2), &v2);
        Mth::Vector point, normal;
        float t = 0.0f;
        bool hit = hits->sweep == 0.0f ? SphereHitsTriangle(sphere, &v0, &v1, &v2, &point, &normal)
                                       : SweptSphereHitsTriangle(sphere, &v0, &v1, &v2, &t, hits->sweep,
                                                                 &hits->direction, &point, &normal);
        if (!hit)
            continue;
        if (hits->count == SphereHits::kMost)
            return true;
        SphereHit& entry = hits->hits[hits->count];
        entry.normal = normal;
        entry.faceFlag40 = static_cast<uint8_t>(flags & 0x40);
        entry.checksum = m_checksum;
        entry.point = point;
        if (hits->sweep > 0.0f)
            entry.t = t;
        ++hits->count;
        if (hits->mode == 2)
            return true;
    }
    return hits->count > 0;
}

// 0x00410240 (the moved sphere's corners summed in double precision on x and y)
bool CollideSphereWithStatics(const CollSphere* sphere, SphereHits* hits, CCollCache* cache)
{
    const Mth::Vector& c = sphere->centre;
    float r = sphere->radius;
    float low = -r;
    Mth::BBox box({low + c.x, low + c.y, low + c.z, r + c.w});
    box.AddPoint({r + c.x, r + c.y, r + c.z, r + c.w});
    if (hits->sweep != 0.0f) {
        const Mth::Vector& d = hits->direction;
        float s = hits->sweep;
        double movedX = static_cast<double>(s) * d.x + c.x;
        double movedY = static_cast<double>(s * d.y) + c.y;
        float movedZ = s * d.z + c.z, movedW = s * d.w + c.w;
        box.AddPoint({static_cast<float>(movedX + low), static_cast<float>(movedY + low), movedZ + low, movedW + r});
        box.AddPoint({static_cast<float>(movedX + r), static_cast<float>(movedY) + r, movedZ + r, movedW + r});
    }
    bool useCache = cache && cache->m_box.Contains(box);
    if (!useCache) {
        CCollGrid* grid = GridOf(sphere);
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
                CollideWithSphere(entry.object, sphere, &grown, hits);
        }
        return hits->count > 0;
    }
    Mth::Line diagonal = Segment(grown.min, grown.max);
    CCollGrid** grids = GridsAlong(&diagonal);
    if (!grids)
        return false;
    for (; *grids; ++grids) {
        int count;
        for (CCollObj** objects = (*grids)->ObjectsAlongLine(&diagonal, &count); *objects; ++objects) {
            if ((*objects)->Call<bool>(13, &grown))
                CollideWithSphere(*objects, sphere, &grown, hits);
        }
    }
    return hits->count > 0;
}

// 0x00418330
CSphereFeeler* CSphereFeeler::Construct(const Mth::Vector* centre, float radius)
{
    m_sphere.centre = *centre;
    m_sphere.radius = radius;
    m_hits.direction = {0.0f, 0.0f, 0.0f, 1.0f};
    m_hits.sweep = 0.0f;
    mp_cache = nullptr;
    m_hits.count = 0;
    m_hits.mode = 0;
    m_unknownB50 = 0;
    m_hits.requireFlags = 0;
    m_hits.ignoreFlags = 0x10;
    return this;
}

// 0x00418300
bool CSphereFeeler::Query(int)
{
    m_hits.mode = 1;
    return CollideSphereWithStatics(&m_sphere, &m_hits, mp_cache ? mp_cache : s_sphereCache);
}

} // namespace Nx

REPLACE(0x0040E280, Nx::SegmentCrossesSphere);
REPLACE(0x0040E560, Nx::SweptSphereHitsEdge);
REPLACE(0x0040E940, Nx::SweptSphereHitsCorner);
REPLACE(0x0040EC90, Nx::SweptSphereHitsTriangle);
REPLACE(0x0040F9E0, Nx::SphereHitsTriangle);
REPLACE(0x004110B0, &Nx::CCollStatic::CollideWithSphere);
REPLACE(0x00410240, Nx::CollideSphereWithStatics);
REPLACE(0x00418330, &Nx::CSphereFeeler::Construct);
REPLACE(0x00418300, &Nx::CSphereFeeler::Query);
