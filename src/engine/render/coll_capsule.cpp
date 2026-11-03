// The capsule queries. See coll_capsule.h. The arithmetic follows the original's x87 code: values are rounded to
// floats where it stores them, kept in double precision where it keeps them on its stack.

#include <cstdint>
#include <array>
#include <span>

#include "engine/render/coll_capsule.h"

#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tests.h"
#include "engine/render/coll_tri.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

} // namespace

// 0x0040FC00
bool capsule_hits_triangle(const maths::Capsule* capsule, const maths::Vec4* v0, const maths::Vec4* v1,
                           const maths::Vec4* v2, maths::Vec4* point, maths::Vec4* normal, maths::Vec4* offset)
{
    // The face's normal: e1 x e2 (e1 = v1 - v0, e2 = v2 - v0).
    const double e2x = static_cast<double>(v2->x) - v0->x;
    const double e2y = static_cast<double>(v2->y) - v0->y;
    const double e2z = static_cast<double>(v2->z) - v0->z;
    const float e1x = get_difference(v1->x, v0->x), e1y = get_difference(v1->y, v0->y);
    const double e1z = static_cast<double>(v1->z) - v0->z;
    const auto nx = static_cast<float>(e1y * e2z - e1z * e2y);
    const auto ny = static_cast<float>(e1z * e2x - e2z * e1x);
    const double nz = e2y * e1x - e1y * e2x;
    normal->w = 0.0f;
    normal->x = nx;
    normal->y = ny;
    normal->z = static_cast<float>(nz);
    if ((nz * nz + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx <= 0.0)
        return false;
    normal->make_unit();

    float s = 0.0f, t = 0.0f, along = 0.0f;
    const double distance = segment_triangle_distance_squared(capsule->line, *normal, *v0, *v1, *v2, {.out = &s, .t_out_ = &t, .along_out_ = &along});

    // The face's nearest point: v0 + s e1 + t e2.
    const auto e2t_x = static_cast<float>((static_cast<double>(v2->x) - v0->x) * t);
    const double e2t_y = (static_cast<double>(v2->y) - v0->y) * t;
    const float e2t_z = get_difference(v2->z, v0->z) * t;
    const float e2t_w = get_difference(v2->w, v0->w) * t;
    const auto e1s_x = static_cast<float>((static_cast<double>(v1->x) - v0->x) * s);
    const double e1s_y = (static_cast<double>(v1->y) - v0->y) * s;
    const double e1s_z = static_cast<double>(get_difference(v1->z, v0->z)) * s;
    const float e1s_w = get_difference(v1->w, v0->w) * s;
    const auto px = static_cast<float>(static_cast<double>(e1s_x) + v0->x);
    const auto py = static_cast<float>(e1s_y + v0->y);
    const auto pz = static_cast<float>(e1s_z + v0->z);
    const auto pw = static_cast<float>(static_cast<double>(e1s_w) + v0->w);
    point->x = static_cast<float>(static_cast<double>(px) + e2t_x);
    point->y = static_cast<float>(e2t_y + py);
    point->z = static_cast<float>(static_cast<double>(pz) + e2t_z);
    point->w = static_cast<float>(static_cast<double>(pw) + e2t_w);

    // The segment's nearest point: start + along (end - start); the offset from it to the face's.
    const maths::Vec4& start = capsule->line.start;
    const maths::Vec4& end = capsule->line.end;
    const auto d_x = static_cast<float>((static_cast<double>(end.x) - start.x) * along);
    const double d_y = (static_cast<double>(end.y) - start.y) * along;
    const double d_z = static_cast<double>(get_difference(end.z, start.z)) * along;
    const float d_w = get_difference(end.w, start.w) * along;
    const auto qx = static_cast<float>(static_cast<double>(d_x) + start.x);
    const auto qy = static_cast<float>(d_y + start.y);
    const auto qz = static_cast<float>(d_z + start.z);
    const double qw = static_cast<double>(d_w) + start.w;
    offset->w = static_cast<float>(point->w - qw);
    offset->x = get_difference(point->x, qx);
    offset->y = get_difference(point->y, qy);
    offset->z = get_difference(point->z, qz);

    // Only from the face's front, and within the radius.
    const double facing = (static_cast<double>(offset->y) * normal->y + static_cast<double>(normal->x) * offset->x) +
                          static_cast<double>(offset->z) * normal->z;
    if (facing >= 0.0)
        return false;
    const double radius = capsule->radius;
    return distance <= radius * radius;
}

// 0x004114E0 (slot 17, an instance's too: its faces are taken unplaced)
bool HitMesh::collide_faces_with_capsule(const maths::Capsule* capsule, const maths::BBox* box, CapsuleHits* hits)
{
    std::array<uint16_t, 0x800> buffer{};
    const std::span<const uint16_t> faces = geometry_->get_faces_in_box(box, buffer);
    const int count = static_cast<int>(faces.size());
    for (uint32_t i = 0; i < ints::as_unsigned(count); ++i) {
        const TriMeshSource* geometry = geometry_;
        const uint32_t face = core::at(faces, i);
        const uint16_t flags =
            geometry->face_flags(static_cast<int>(face));
        if ((flags & hits->ignore_flags_) || (~flags & hits->require_flags_))
            continue;
        maths::Vec4 v0{}, v1{}, v2{};
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 0)), &v0);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 1)), &v1);
        geometry->read_stored_vertex(static_cast<int>(geometry->face_vertex(static_cast<int>(face), 2)), &v2);
        if (!(hits->sweep == 0.0f)) // (a moving capsule isn't tested)
            continue;
        maths::Vec4 point{}, normal{}, offset{};
        if (!capsule_hits_triangle(capsule, &v0, &v1, &v2, &point, &normal, &offset))
            continue;
        if (hits->count == CapsuleHits::kMost)
            return true;
        CapsuleHit& entry = core::at(hits->hits, hits->count);
        entry.normal = normal;
        entry.offset = offset;
        entry.face_flag40_ = static_cast<uint8_t>(flags & 0x40);
        entry.checksum = checksum_;
        entry.point = point;
        if (hits->sweep > 0.0f) // (never here: the sweep is 0)
            entry.t = 0.0f;
        ++hits->count;
        if (hits->mode == 2)
            return true;
    }
    return hits->count > 0;
}

} // namespace render
