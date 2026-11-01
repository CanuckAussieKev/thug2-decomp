// The collision objects' line tests and the hit record they fill.

#include "engine/render/coll_line.h"

#include <cmath>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tests.h"
#include "engine/render/coll_tri.h"

namespace render {

bool g_want_farthest;

namespace {

// Set while a caller (0x00410930) wants the farthest hit rather than the nearest.
bool& s_want_farthest = g_want_farthest;

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rejection threshold of a line test: the line's distance from the geometry's minimum corner times the
// geometry's rejection scale, truncated (to the low 32 bits of the 64-bit conversion, as _ftol).
uint32_t get_threshold(double distance, const TriMeshSource* geometry)
{
    return static_cast<uint32_t>(truncate(distance * geometry->rejection_scale()));
}

// A face's three corners (raw vertices).
void get_corners(const TriMeshSource* geometry, uint32_t face, maths::Vec4* v0, maths::Vec4* v1, maths::Vec4* v2)
{
    geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 0)), v0);
    geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 1)), v1);
    geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 2)), v2);
}

} // namespace

// 0x004F8160
CollisionHit* CollisionHit::copy_from(const CollisionHit& other)
{
    normal = other.normal;
    point = other.point;
    face = other.face;
    distance = other.distance;
    ignore_flags_ = other.ignore_flags_;
    require_flags_ = other.require_flags_;
    face_flags_ = other.face_flags_;
    terrain = other.terrain;
    object = other.object;
    object_checksum_ = other.object_checksum_;
    unknown38 = other.unknown38;
    callback = other.callback;
    callback_data_ = other.callback_data_;
    owner = other.owner;
    hit = other.hit;
    face_flag40_ = other.face_flag40_;
    scene = other.scene;
    return this;
}

// 0x004131F0
bool report_hit(const maths::Seg& line, HitMesh* object, const FaceHit& face, float distance, CollisionHit* hit)
{
    const bool better = s_want_farthest ? !(distance < hit->distance) : distance <= hit->distance;
    CollisionHit saved;
    if (!better)
        saved.copy_from(*hit);
    bool taken = false;
    const uint32_t flags = object->get_triangle_attrs(static_cast<int>(face.face));
    if (!(flags & hit->ignore_flags_) && !(~flags & hit->require_flags_)) {
        hit->hit = true;
        hit->normal = face.normal;
        hit->point = face.point;
        hit->distance = distance;
        hit->face = face.face;
        hit->face_flags_ = static_cast<uint16_t>(flags);
        hit->terrain = static_cast<uint8_t>(object->get_face_terrain(static_cast<int>(face.face)));
        hit->object = object;
        hit->face_flag40_ = static_cast<uint8_t>(flags & 0x40);
        hit->object_checksum_ = object->checksum_;
        // the point recomputed along the line
        const double dx = static_cast<double>(line.end.x) - line.start.x;
        const double dy = static_cast<double>(line.end.y) - line.start.y;
        const float dz = get_difference(line.end.z, line.start.z);
        auto px = static_cast<float>(dx * distance);
        auto py = static_cast<float>(dy * distance);
        hit->point.w = 1.0f;
        hit->point.z = static_cast<float>(static_cast<double>(dz) * distance + line.start.z);
        hit->point.x = static_cast<float>(static_cast<double>(px) + line.start.x);
        hit->point.y = static_cast<float>(static_cast<double>(py) + line.start.y);
        if (hit->callback)
            hit->callback(hit);
        taken = true;
    }
    if (!better) {
        hit->copy_from(saved);
        return false;
    }
    return taken;
}

// 0x004137B0 (the face index handed on is left unset by the original - stack garbage; 0 here)
bool HitMovingBox::collide_with_line(const maths::Seg* line, const maths::Vec4* /*direction*/, CollisionHit* hit,
                                     const maths::BBox* /*line_box*/)
{
    maths::Seg local{}; // (its constructor, 0x005902E0, does nothing)
    box_to_local(reinterpret_cast<const maths::BBox*>(line), reinterpret_cast<maths::BBox*>(&local));
    maths::BBox around(local.start);
    around.append_spot(local.end);
    if (!box_.intersects(around))
        return false;
    maths::Vec4 point{}, normal{};
    if (!box_.segment_crossing(local, point, normal))
        return false;
    const float hx = get_difference(point.x, local.start.x);
    const float hy = get_difference(point.y, local.start.y);
    const float hz = get_difference(point.z, local.start.z);
    const float lx = get_difference(local.end.x, local.start.x);
    const float ly = get_difference(local.end.y, local.start.y);
    const float lz = get_difference(local.end.z, local.start.z);
    maths::Vec4 turned{};
    orientation_.rotate_vector(turned, point);
    FaceHit face{};
    face.point = {static_cast<float>(static_cast<double>(turned.x) + anchor_point_.x),
                  static_cast<float>(static_cast<double>(turned.y) + anchor_point_.y),
                  static_cast<float>(static_cast<double>(turned.z) + anchor_point_.z),
                  static_cast<float>(static_cast<double>(turned.w) + anchor_point_.w)};
    orientation_.rotate_vector(face.normal, normal);
    face.face = 0;
    auto distance = static_cast<float>(
        std::sqrt((static_cast<double>(hz) * hz + static_cast<double>(hy) * hy) + static_cast<double>(hx) * hx) /
        std::sqrt((static_cast<double>(lz) * lz + static_cast<double>(ly) * ly) + static_cast<double>(lx) * lx));
    return report_hit(*line, this, face, distance, hit);
}

// 0x00413B50
bool StaticCollider::collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                                       const maths::BBox* line_box)
{
    // the line's distance from the geometry's minimum corner, for the rejection test
    double ux = direction->x, uy = direction->y;
    float uz = direction->z;
    const double length = std::sqrt((static_cast<double>(uz) * uz + uy * uy) + ux * ux);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        ux *= inverse;
        uy *= inverse;
        uz = static_cast<float>(uz * inverse);
    }
    TriMeshSource* geometry = geometry_;
    const float cx = get_difference(geometry->lower_limit_.x, line->start.x);
    const float cy = get_difference(geometry->lower_limit_.y, line->start.y);
    const double cz_exact = static_cast<double>(geometry->lower_limit_.z) - line->start.z;
    auto cz = static_cast<float>(cz_exact);
    auto along = static_cast<float>((cz_exact * uz + cy * uy) + cx * ux);
    auto px = static_cast<float>(ux * along);
    const double py = uy * along;
    const double pz = static_cast<double>(uz) * along;
    const float rx = get_difference(cx, px);
    auto ry = static_cast<float>(cy - py);
    const double rz = cz - pz;
    const uint32_t threshold =
        get_threshold(std::sqrt((rz * rz + static_cast<double>(ry) * ry) + static_cast<double>(rx) * rx), geometry);
    if (threshold > 0xFF)
        return false;

    int count = 0;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry->get_faces_in_box(line_box, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        geometry = geometry_;
        const uint32_t face = faces[i];
        const uint8_t rejection = geometry->small_faces_ ? geometry->faces_[face * 8 + 7] : octets_[face];
        if (threshold > rejection)
            continue;
        maths::Vec4 v0{}, v1{}, v2{};
        get_corners(geometry, face, &v0, &v1, &v2);
        float t = 0.0f;
        if (!ray_hits_triangle(line->start, *direction, v0, v1, v2, &t))
            continue;
        // the normal (v1 - v0) x (v2 - v0), rounded where the original stores
        const double e0x = static_cast<double>(v1.x) - v0.x;
        const float e0y = get_difference(v1.y, v0.y);
        const float e0z = get_difference(v1.z, v0.z);
        const float e1x = get_difference(v2.x, v0.x);
        const float e1y = get_difference(v2.y, v0.y);
        const double e1z_exact = static_cast<double>(v2.z) - v0.z;
        auto e1z = static_cast<float>(e1z_exact);
        FaceHit face_hit{};
        face_hit.normal = {static_cast<float>(e1z_exact * e0y - static_cast<double>(e1y) * e0z),
                           static_cast<float>(static_cast<double>(e0z) * e1x - e1z * e0x),
                           static_cast<float>(e1y * e0x - static_cast<double>(e0y) * e1x), 0.0f};
        face_hit.point = v0;
        face_hit.face = face;
        face_hit.normal.make_unit();
        if (report_hit(*line, this, face_hit, t, hit))
            any = true;
    }
    return any;
}

// 0x00413FF0
bool CollStaticInstance::collide_with_line(const maths::Seg* line, const maths::Vec4* /*direction*/, CollisionHit* hit,
                                           const maths::BBox* /*line_box*/)
{
    maths::Seg local{}; // (its constructor, 0x005902E0, does nothing)
    line_to_local(line, &local);
    const maths::Vec4 way{get_difference(local.end.x, local.start.x), get_difference(local.end.y, local.start.y),
                          get_difference(local.end.z, local.start.z), get_difference(local.end.w, local.start.w)};
    maths::BBox around(local.start);
    around.append_spot(local.end);
    int count = 0;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry_->get_faces_in_box(&around, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const TriMeshSource* geometry = geometry_;
        const uint32_t face = faces[i];
        maths::Vec4 v0{}, v1{}, v2{};
        get_corners(geometry, face, &v0, &v1, &v2);
        float t = 0.0f;
        if (!ray_hits_triangle(local.start, way, v0, v1, v2, &t))
            continue;
        const double e0x = static_cast<double>(v1.x) - v0.x;
        const double e0y = static_cast<double>(v1.y) - v0.y;
        const double e0z = static_cast<double>(v1.z) - v0.z;
        const float e1x = get_difference(v2.x, v0.x);
        const float e1y = get_difference(v2.y, v0.y);
        const double e1z = static_cast<double>(v2.z) - v0.z;
        FaceHit face_hit{};
        face_hit.face = face;
        auto nx = static_cast<float>(e1z * e0y - e1y * e0z);
        auto ny = static_cast<float>(e0z * e1x - e1z * e0x);
        const double nz_exact = e1y * e0x - e0y * e1x;
        auto nz = static_cast<float>(nz_exact);
        const double length =
            std::sqrt((static_cast<double>(nx) * nx + nz_exact * nz_exact) + static_cast<double>(ny) * ny);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            nx = static_cast<float>(nx * inverse);
            ny = static_cast<float>(ny * inverse);
            nz = static_cast<float>(nz_exact * inverse);
        }
        // the corner scaled and placed, the normal turned
        const maths::Vec4 scaled{static_cast<float>(static_cast<double>(v0.x) * size_factor_.x),
                                 static_cast<float>(static_cast<double>(v0.y) * size_factor_.y),
                                 static_cast<float>(static_cast<double>(v0.z) * size_factor_.z),
                                 static_cast<float>(static_cast<double>(v0.w) * size_factor_.w)};
        basis_frame_.carry_point_through(face_hit.point, scaled);
        const maths::Vec4* r = basis_frame_.row;
        face_hit.normal = {static_cast<float>((static_cast<double>(nz) * r[2].x + static_cast<double>(ny) * r[1].x) +
                                              static_cast<double>(nx) * r[0].x),
                           static_cast<float>((static_cast<double>(nx) * r[0].y + static_cast<double>(nz) * r[2].y) +
                                              static_cast<double>(ny) * r[1].y),
                           static_cast<float>((static_cast<double>(nx) * r[0].z + static_cast<double>(nz) * r[2].z) +
                                              static_cast<double>(ny) * r[1].z),
                           0.0f};
        if (report_hit(*line, this, face_hit, t, hit))
            any = true;
    }
    return any;
}

// 0x00414460
bool HitMovingTriangles::collide_with_line(const maths::Seg* line, const maths::Vec4* /*direction*/, CollisionHit* hit,
                                           const maths::BBox* /*line_box*/)
{
    maths::Seg local{}; // (its constructor, 0x005902E0, does nothing)
    box_to_local(reinterpret_cast<const maths::BBox*>(line), reinterpret_cast<maths::BBox*>(&local));
    const maths::Vec4 way{get_difference(local.end.x, local.start.x), get_difference(local.end.y, local.start.y),
                          get_difference(local.end.z, local.start.z), get_difference(local.end.w, local.start.w)};
    maths::BBox around(local.start);
    around.append_spot(local.end);
    TriMeshSource* geometry = geometry_;
    uint32_t threshold = 0;
    if (geometry->small_faces_) {
        maths::Vec4 unit = way;
        unit.make_unit();
        const double cx = static_cast<double>(geometry->lower_limit_.x) - local.start.x;
        const double cy = static_cast<double>(geometry->lower_limit_.y) - local.start.y;
        const float cz = get_difference(geometry->lower_limit_.z, local.start.z);
        const double along = (cx * unit.x + static_cast<double>(cz) * unit.z) + cy * unit.y;
        const double px = unit.x * along;
        auto py = static_cast<float>(unit.y * along);
        auto pz = static_cast<float>(unit.z * along);
        auto rx = static_cast<float>(cx - px);
        const double ry = cy - py;
        const double rz = static_cast<double>(cz) - pz;
        threshold = get_threshold(std::sqrt((static_cast<double>(rx) * rx + rz * rz) + ry * ry), geometry);
        if (threshold > 0xFF)
            return false;
    }
    int count = 0;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry->get_faces_in_box(&around, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        geometry = geometry_;
        const uint32_t face = faces[i];
        if (geometry->small_faces_ && threshold > geometry->faces_[face * 8 + 7])
            continue;
        maths::Vec4 v0{}, v1{}, v2{};
        get_corners(geometry, face, &v0, &v1, &v2);
        float t = 0.0f;
        if (!ray_hits_triangle(local.start, way, v0, v1, v2, &t))
            continue;
        const double e0x = static_cast<double>(v1.x) - v0.x;
        const double e0y = static_cast<double>(v1.y) - v0.y;
        const double e0z = static_cast<double>(v1.z) - v0.z;
        const float e1x = get_difference(v2.x, v0.x);
        const float e1y = get_difference(v2.y, v0.y);
        const double e1z = static_cast<double>(v2.z) - v0.z;
        FaceHit face_hit{};
        face_hit.face = face;
        auto nx = static_cast<float>(e1z * e0y - e1y * e0z);
        auto ny = static_cast<float>(e0z * e1x - e1z * e0x);
        auto nz = static_cast<float>(e1y * e0x - e0y * e1x);
        // the length and its inverse rounded to floats; x kept unrounded for the turn
        const double length =
            std::sqrt((static_cast<double>(nx) * nx + static_cast<double>(nz) * nz) + static_cast<double>(ny) * ny);
        double unit_x = nx;
        if (length > 0.0) {
            auto inverse = static_cast<float>(1.0 / static_cast<float>(length));
            unit_x = static_cast<double>(nx) * inverse;
            ny = static_cast<float>(static_cast<double>(ny) * inverse);
            nz = static_cast<float>(static_cast<double>(nz) * inverse);
        }
        // the corner turned and placed (z rounded before it's placed), the normal turned
        const maths::Vec4* r = orientation_.row;
        const double placed_x = (static_cast<double>(v0.z) * r[2].x + static_cast<double>(v0.y) * r[1].x) +
                                static_cast<double>(v0.x) * r[0].x;
        const double placed_y = (static_cast<double>(v0.z) * r[2].y + static_cast<double>(v0.y) * r[1].y) +
                                static_cast<double>(v0.x) * r[0].y;
        auto placed_z = static_cast<float>((static_cast<double>(v0.z) * r[2].z + static_cast<double>(v0.y) * r[1].z) +
                                           static_cast<double>(v0.x) * r[0].z);
        face_hit.point = {static_cast<float>(placed_x + anchor_point_.x),
                          static_cast<float>(placed_y + anchor_point_.y),
                          static_cast<float>(static_cast<double>(placed_z) + anchor_point_.z),
                          static_cast<float>(static_cast<double>(v0.w) + anchor_point_.w)};
        face_hit.normal = {
            static_cast<float>((static_cast<double>(nz) * r[2].x + static_cast<double>(ny) * r[1].x) + unit_x * r[0].x),
            static_cast<float>((unit_x * r[0].y + static_cast<double>(nz) * r[2].y) + static_cast<double>(ny) * r[1].y),
            static_cast<float>((unit_x * r[0].z + static_cast<double>(nz) * r[2].z) + static_cast<double>(ny) * r[1].z),
            0.0f};
        if (report_hit(*line, this, face_hit, t, hit))
            any = true;
    }
    return any;
}

} // namespace render
