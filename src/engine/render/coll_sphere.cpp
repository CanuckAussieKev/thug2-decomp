// The sphere queries and the sphere feeler. See coll_sphere.h. The arithmetic follows the original's x87 code:
// values are rounded to floats where it stores them, kept in double precision where it keeps them on its stack.

#include "engine/render/coll_sphere.h"

#include <cmath>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/gfx/engine_scene_list.h"

namespace render {

namespace {

HitMeshCache* g_sphere_cache;

// The cache the sphere feelers use when they have none (0x00691990).
HitMeshCache*& s_sphere_cache = g_sphere_cache;

constexpr float kTiny = 0.01f; // 0x0064544C: a zero divisor replaced

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
maths::Vec4 get_minus(const maths::Vec4& a, const maths::Vec4& b)
{
    return {get_difference(a.x, b.x), get_difference(a.y, b.y), get_difference(a.z, b.z), get_difference(a.w, b.w)};
}

// The scene grid holding the sphere (0x0048C940) and the grids along a line (0x0048C980).
CollGrid* grid_of(const CollSphere*) { return get_first_coll_grid(); }
CollGrid** grids_along(const maths::Seg* line)
{
    return get_coll_grids_on_line(line);
}

bool collide_with_sphere(HitMesh* object, const CollSphere* sphere, const maths::BBox* box, SphereHits* hits)
{
    return object->get_call<bool>(16, sphere, box, hits);
}

// Whether a value is a parameter along a segment (NaN isn't).
bool within01(float value) { return !(value < 0.0f) && !(value > 1.0f) && value == value; }

// A line from `start` to `end`.
maths::Seg get_segment(const maths::Vec4& start, const maths::Vec4& end)
{
    maths::Seg line;
    line.start = start;
    line.end = end;
    return line;
}

} // namespace

// 0x0040E280
bool segment_crosses_sphere(const maths::Seg* segment, const CollSphere* sphere, int* count, maths::Vec4* points, int)
{
    const maths::Vec4& start = segment->start;
    double mx = static_cast<double>(start.x) - sphere->centre.x;
    double my = static_cast<double>(start.y) - sphere->centre.y;
    double mz = static_cast<double>(start.z) - sphere->centre.z;
    maths::Vec4 d = get_minus(segment->end, start);
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
bool swept_sphere_hits_edge(const maths::Seg* edge, const maths::Vec4* edge_motion, const CollSphere* sphere,
                         const maths::Vec4* motion, float* t, float /*limit*/, int* count, maths::Vec4* point)
{
    if (segment_crosses_sphere(edge, sphere, count, point, 0))
        return true;
    maths::Capsule capsule(*edge, sphere->radius);
    const maths::Vec4& centre = sphere->centre;
    // the centre's path relative to the edge
    maths::Seg path;
    path.start = centre;
    path.end = {static_cast<float>((static_cast<double>(motion->x) - edge_motion->x) + centre.x),
                get_difference(motion->y, edge_motion->y) + centre.y, get_difference(motion->z, edge_motion->z) + centre.z,
                get_difference(motion->w, edge_motion->w) + centre.w};
    float s, u;
    if (!capsule.intersects_segment(path, &s, &u, nullptr))
        return false;
    if (within01(s)) {
        *count = within01(u) ? 2 : 1;
    } else if (within01(u)) {
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
    const maths::Vec4& start = edge->start;
    maths::Vec4 moved{static_cast<float>(static_cast<double>(time) * edge_motion->x + start.x),
                      time * edge_motion->y + start.y, time * edge_motion->z + start.z, time * edge_motion->w + start.w};
    maths::Vec4 along = get_minus(edge->end, start);
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
        maths::Vec4 part = fraction * along;
        *point = {moved.x + part.x, moved.y + part.y, moved.z + part.z, moved.w + part.w};
    }
    *count = 1;
    return true;
}

// 0x0040E940
bool swept_sphere_hits_corner(const maths::Vec4* corner, const CollSphere* sphere, const maths::Vec4* motion,
                           const maths::Vec4* edge_motion, const maths::Vec4* v0, const maths::Vec4* v1,
                           const maths::Vec4* v2, const maths::Vec4* edge_cross, const maths::Vec4* edge, float* t,
                           float limit, int* count, maths::Vec4* point, maths::Vec4*)
{
    const maths::Vec4& centre = sphere->centre;
    float mx = get_difference(corner->x, centre.x), my = get_difference(corner->y, centre.y),
          mz = get_difference(corner->z, centre.z);
    double radius = sphere->radius;
    double distance2 = (static_cast<double>(mz) * mz + static_cast<double>(mx) * mx) + static_cast<double>(my) * my;
    if (radius * radius > distance2) {
        *t = 0.0f; // (the corner already inside the sphere)
        return false;
    }
    float ux = get_difference(motion->x, edge_motion->x);
    double uy = static_cast<double>(motion->y) - edge_motion->y;
    double uz_exact = static_cast<double>(motion->z) - edge_motion->z;
    float uz = static_cast<float>(uz_exact);
    double towards = (uz_exact * mz + uy * my) + static_cast<double>(ux) * mx;
    if (towards <= 0.0)
        return false; // (moving away from the corner)
    const maths::Vec4& c = *edge_cross;
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
    maths::Seg line;
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
    maths::Vec4 touched[2];
    if (!swept_sphere_hits_edge(&line, edge_motion, sphere, motion, t, limit, count, touched))
        return false;
    *point = touched[0];
    return true;
}

// 0x0040EC90
bool swept_sphere_hits_triangle(const CollSphere* sphere, const maths::Vec4* v0, const maths::Vec4* v1,
                             const maths::Vec4* v2, float* t, float limit, const maths::Vec4* motion,
                             maths::Vec4* point, maths::Vec4* normal)
{
    const maths::Vec4& centre = sphere->centre;
    maths::Vec4 e01 = get_minus(*v1, *v0), e12 = get_minus(*v2, *v1), e20 = get_minus(*v0, *v2);
    double e2x = static_cast<double>(v2->x) - v0->x;
    double e2y = static_cast<double>(v2->y) - v0->y;
    double e2z = static_cast<double>(v2->z) - v0->z;
    float e1x = get_difference(v1->x, v0->x), e1y = get_difference(v1->y, v0->y);
    double e1z = static_cast<double>(v1->z) - v0->z;
    float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    float ny = static_cast<float>(e1z * e2x - e2z * e1x);
    double nz_exact = e2y * e1x - e1y * e2x;
    *normal = {nx, ny, static_cast<float>(nz_exact), 0.0f};
    double length2 = (nz_exact * nz_exact + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx;
    if (length2 <= 0.0)
        return false;
    const maths::Vec4& n = *normal;
    float centre_along = static_cast<float>((static_cast<double>(n.z) * centre.z + static_cast<double>(n.x) * centre.x) +
                                           static_cast<double>(centre.y) * n.y);
    float reach2 = static_cast<float>(static_cast<double>(sphere->radius) * sphere->radius * length2);
    float plane_along = static_cast<float>((static_cast<double>(v0->z) * n.z + static_cast<double>(n.y) * v0->y) +
                                          static_cast<double>(n.x) * v0->x);
    float distance = centre_along - plane_along;
    const maths::Vec4 zero{0.0f, 0.0f, 0.0f, 1.0f};
    // each edge crossed with the (unnormalised) normal: pointing out of the triangle
    auto across = [&](const maths::Vec4& e) {
        return maths::Vec4{static_cast<float>(static_cast<double>(e.z) * n.y - static_cast<double>(e.y) * n.z),
                           static_cast<float>(static_cast<double>(e.x) * n.z - static_cast<double>(e.z) * n.x),
                           static_cast<float>(static_cast<double>(e.y) * n.x - static_cast<double>(e.x) * n.y), 0.0f};
    };
    maths::Vec4 c01 = across(e01), c12 = across(e12), c20 = across(e20);
    normal->make_unit();
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
    auto edge = [&](const maths::Vec4& a, const maths::Vec4& b) {
        maths::Seg line = get_segment(a, b);
        int touched;
        maths::Vec4 hit[2];
        if (!swept_sphere_hits_edge(&line, &zero, sphere, motion, t, limit, &touched, hit))
            return false;
        *point = hit[0];
        return true;
    };
    auto corner = [&](const maths::Vec4* at, maths::Vec4* cross, maths::Vec4* e) {
        int touched = 0;
        return swept_sphere_hits_corner(at, sphere, motion, &zero, v0, v1, v2, cross, e, t, limit, &touched, point,
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
            point_triangle_distance_squared(centre, *v0, *v1, *v2, &s, &u);
            maths::Vec4 across2 = u * (*v2 - *v0);
            *point = (*v0 + s * (*v1 - *v0)) + across2;
            maths::Capsule path(centre, *motion, sphere->radius);
            return path.contains_point(*point, nullptr);
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
    maths::Vec4 move{motion->x, motion->y, motion->z, get_difference(motion->w, 1.0f)};
    bool above = centre_along > plane_along;
    double approach = (static_cast<double>(move.x) * n.x + static_cast<double>(move.z) * n.z) +
                      static_cast<double>(move.y) * n.y;
    if (above ? !(approach < 0.0) && approach == approach : !(approach > 0.0) && approach == approach)
        return false;
    maths::Vec4 front = centre - sphere->radius * *normal;
    double plane_at = (static_cast<double>(v0->z) * n.z + static_cast<double>(n.y) * v0->y) +
                     static_cast<double>(n.x) * v0->x;
    double front_at = (static_cast<double>(front.x) * n.x + static_cast<double>(front.z) * n.z) +
                     static_cast<double>(front.y) * n.y;
    float time = static_cast<float>((plane_at - front_at) / static_cast<float>(approach));
    maths::Vec4 meet{static_cast<float>(static_cast<double>(move.x) * time + front.x), move.y * time + front.y,
                     move.z * time + front.z, move.w * time + front.w};
    auto along_of = [&](const maths::Vec4& cr) {
        return (static_cast<double>(meet.z) * cr.z + static_cast<double>(meet.y) * cr.y) +
               static_cast<double>(meet.x) * cr.x;
    };
    bool in01 = inside01(along_of(c01)), in12 = inside12(along_of(c12)), in20 = inside20(along_of(c20));
    auto corner_line = [&](const maths::Vec4* at) {
        if (!line_hits_sphere(*at, zero, *sphere, *motion, t, limit))
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
        return corner_line(v2);
    if (in12 && in20)
        return edge(*v0, *v1);
    if (in12)
        return corner_line(v0);
    if (in20)
        return corner_line(v1);
    return false;
}

// 0x0040F9E0
bool sphere_hits_triangle(const CollSphere* sphere, const maths::Vec4* v0, const maths::Vec4* v1, const maths::Vec4* v2,
                        maths::Vec4* point, maths::Vec4* normal)
{
    double e2x = static_cast<double>(v2->x) - v0->x;
    double e2y = static_cast<double>(v2->y) - v0->y;
    double e2z = static_cast<double>(v2->z) - v0->z;
    float e1x = get_difference(v1->x, v0->x), e1y = get_difference(v1->y, v0->y);
    double e1z = static_cast<double>(v1->z) - v0->z;
    float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    float ny = static_cast<float>(e1z * e2x - e2z * e1x);
    double nz_exact = e2y * e1x - e1y * e2x;
    *normal = {nx, ny, static_cast<float>(nz_exact), 0.0f};
    if ((nz_exact * nz_exact + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx <= 0.0)
        return false;
    const maths::Vec4& c = sphere->centre;
    double dx = static_cast<double>(v0->x) - c.x, dy = static_cast<double>(v0->y) - c.y,
           dz = static_cast<double>(v0->z) - c.z;
    if ((dy * normal->y + dx * normal->x) + dz * normal->z >= 0.0)
        return false; // (behind the face)
    normal->make_unit();
    float s, u;
    double distance2 = point_triangle_distance_squared(c, *v0, *v1, *v2, &s, &u);
    // v0 + s (v1 - v0) + u (v2 - v0), rounded as the original
    double f2x = static_cast<double>(v2->x) - v0->x, f2y = static_cast<double>(v2->y) - v0->y;
    float f2z = get_difference(v2->z, v0->z), f2w = get_difference(v2->w, v0->w);
    float g2x = static_cast<float>(f2x * u);
    double g2y = f2y * u;
    float g2z = f2z * u, g2w = f2w * u;
    double f1x = static_cast<double>(v1->x) - v0->x, f1y = static_cast<double>(v1->y) - v0->y;
    float f1z = get_difference(v1->z, v0->z), f1w = get_difference(v1->w, v0->w);
    float g1x = static_cast<float>(f1x * s);
    double g1y = f1y * s;
    double g1z = static_cast<double>(f1z) * s;
    float g1w = f1w * s;
    maths::Vec4 base{g1x + v0->x, static_cast<float>(g1y + v0->y), static_cast<float>(g1z + v0->z), g1w + v0->w};
    *point = {base.x + g2x, static_cast<float>(g2y + base.y), base.z + g2z, base.w + g2w};
    double radius = sphere->radius;
    return distance2 <= radius * radius;
}

// 0x004110B0 (slot 16 of a sector and of an instance - the instance's faces taken as they are, unplaced)
bool StaticCollider::collide_with_sphere(const CollSphere* sphere, const maths::BBox* box, SphereHits* hits)
{
    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry_->get_faces_in_box(box, &count, buffer, 0x800);
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const TriMeshSource* geometry = geometry_;
        uint32_t face = faces[i];
        uint16_t flags = *reinterpret_cast<const uint16_t*>(geometry->faces_ + face * (geometry->small_faces_ ? 8 : 10));
        if ((flags & hits->ignore_flags_) || (~flags & hits->require_flags_))
            continue;
        maths::Vec4 v0, v1, v2;
        geometry->read_stored_vertex(geometry->face_vertex(face, 0), &v0);
        geometry->read_stored_vertex(geometry->face_vertex(face, 1), &v1);
        geometry->read_stored_vertex(geometry->face_vertex(face, 2), &v2);
        maths::Vec4 point, normal;
        float t = 0.0f;
        bool hit = hits->sweep == 0.0f ? sphere_hits_triangle(sphere, &v0, &v1, &v2, &point, &normal)
                                       : swept_sphere_hits_triangle(sphere, &v0, &v1, &v2, &t, hits->sweep,
                                                                 &hits->direction, &point, &normal);
        if (!hit)
            continue;
        if (hits->count == SphereHits::kMost)
            return true;
        SphereHit& entry = hits->hits[hits->count];
        entry.normal = normal;
        entry.face_flag40_ = static_cast<uint8_t>(flags & 0x40);
        entry.checksum = checksum_;
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
bool collide_sphere_with_statics(const CollSphere* sphere, SphereHits* hits, HitMeshCache* cache)
{
    const maths::Vec4& c = sphere->centre;
    float r = sphere->radius;
    float low = -r;
    maths::BBox box({low + c.x, low + c.y, low + c.z, r + c.w});
    box.append_spot({r + c.x, r + c.y, r + c.z, r + c.w});
    if (hits->sweep != 0.0f) {
        const maths::Vec4& d = hits->direction;
        float s = hits->sweep;
        double moved_x = static_cast<double>(s) * d.x + c.x;
        double moved_y = static_cast<double>(s * d.y) + c.y;
        float moved_z = s * d.z + c.z, moved_w = s * d.w + c.w;
        box.append_spot({static_cast<float>(moved_x + low), static_cast<float>(moved_y + low), moved_z + low, moved_w + r});
        box.append_spot({static_cast<float>(moved_x + r), static_cast<float>(moved_y) + r, moved_z + r, moved_w + r});
    }
    bool use_cache = cache && cache->box_.get_holds(box);
    if (!use_cache) {
        CollGrid* grid = grid_of(sphere);
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
            if (grown.intersects(*static_cast<const maths::BBox*>(entry.geometry)))
                collide_with_sphere(entry.object, sphere, &grown, hits);
        }
        return hits->count > 0;
    }
    maths::Seg diagonal = get_segment(grown.min, grown.max);
    CollGrid** grids = grids_along(&diagonal);
    if (!grids)
        return false;
    for (; *grids; ++grids) {
        int count;
        for (HitMesh** objects = (*grids)->objects_along_line(&diagonal, &count); *objects; ++objects) {
            if ((*objects)->get_call<bool>(13, &grown))
                collide_with_sphere(*objects, sphere, &grown, hits);
        }
    }
    return hits->count > 0;
}

// 0x00418330
SphereFeeler* SphereFeeler::make_new(const maths::Vec4* centre, float radius)
{
    bound_ball_.centre = *centre;
    bound_ball_.radius = radius;
    hits_.direction = {0.0f, 0.0f, 0.0f, 1.0f};
    hits_.sweep = 0.0f;
    hit_cache_ = nullptr;
    hits_.count = 0;
    hits_.mode = 0;
    unknownb50_ = 0;
    hits_.require_flags_ = 0;
    hits_.ignore_flags_ = 0x10;
    return this;
}

// 0x00418300
bool SphereFeeler::get_query(int)
{
    hits_.mode = 1;
    return collide_sphere_with_statics(&bound_ball_, &hits_, hit_cache_ ? hit_cache_ : s_sphere_cache);
}

} // namespace Nx
