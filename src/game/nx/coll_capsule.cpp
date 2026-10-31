// The capsule queries. See coll_capsule.h. The arithmetic follows the original's x87 code: values are rounded to
// floats where it stores them, kept in double precision where it keeps them on its stack.

#include <cstdint>

#include "game/nx/coll_capsule.h"

#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"

namespace Nx {

namespace {

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

} // namespace

// 0x0040FC00
bool CapsuleHitsTriangle(const Mth::Capsule* capsule, const Mth::Vector* v0, const Mth::Vector* v1,
                         const Mth::Vector* v2, Mth::Vector* point, Mth::Vector* normal, Mth::Vector* offset)
{
    // The face's normal: e1 x e2 (e1 = v1 - v0, e2 = v2 - v0).
    const double e2x = static_cast<double>(v2->x) - v0->x;
    const double e2y = static_cast<double>(v2->y) - v0->y;
    const double e2z = static_cast<double>(v2->z) - v0->z;
    const float e1x = Difference(v1->x, v0->x), e1y = Difference(v1->y, v0->y);
    const double e1z = static_cast<double>(v1->z) - v0->z;
    const float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    const float ny = static_cast<float>(e1z * e2x - e2z * e1x);
    const double nz = e2y * e1x - e1y * e2x;
    normal->w = 0.0f;
    normal->x = nx;
    normal->y = ny;
    normal->z = static_cast<float>(nz);
    if ((nz * nz + static_cast<double>(ny) * ny) + static_cast<double>(nx) * nx <= 0.0)
        return false;
    normal->Normalize();

    float s, t, along;
    const double distance = SegmentTriangleDistanceSquared(capsule->line, *normal, *v0, *v1, *v2, &s, &t, &along);

    // The face's nearest point: v0 + s e1 + t e2.
    const float e2tX = static_cast<float>((static_cast<double>(v2->x) - v0->x) * t);
    const double e2tY = (static_cast<double>(v2->y) - v0->y) * t;
    const float e2tZ = Difference(v2->z, v0->z) * t;
    const float e2tW = Difference(v2->w, v0->w) * t;
    const float e1sX = static_cast<float>((static_cast<double>(v1->x) - v0->x) * s);
    const double e1sY = (static_cast<double>(v1->y) - v0->y) * s;
    const double e1sZ = static_cast<double>(Difference(v1->z, v0->z)) * s;
    const float e1sW = Difference(v1->w, v0->w) * s;
    const float px = static_cast<float>(static_cast<double>(e1sX) + v0->x);
    const float py = static_cast<float>(e1sY + v0->y);
    const float pz = static_cast<float>(e1sZ + v0->z);
    const float pw = static_cast<float>(static_cast<double>(e1sW) + v0->w);
    point->x = static_cast<float>(static_cast<double>(px) + e2tX);
    point->y = static_cast<float>(e2tY + py);
    point->z = static_cast<float>(static_cast<double>(pz) + e2tZ);
    point->w = static_cast<float>(static_cast<double>(pw) + e2tW);

    // The segment's nearest point: start + along (end - start); the offset from it to the face's.
    const Mth::Vector& start = capsule->line.start;
    const Mth::Vector& end = capsule->line.end;
    const float dX = static_cast<float>((static_cast<double>(end.x) - start.x) * along);
    const double dY = (static_cast<double>(end.y) - start.y) * along;
    const double dZ = static_cast<double>(Difference(end.z, start.z)) * along;
    const float dW = Difference(end.w, start.w) * along;
    const float qx = static_cast<float>(static_cast<double>(dX) + start.x);
    const float qy = static_cast<float>(dY + start.y);
    const float qz = static_cast<float>(dZ + start.z);
    const double qw = static_cast<double>(dW) + start.w;
    offset->w = static_cast<float>(point->w - qw);
    offset->x = Difference(point->x, qx);
    offset->y = Difference(point->y, qy);
    offset->z = Difference(point->z, qz);

    // Only from the face's front, and within the radius.
    const double facing = (static_cast<double>(offset->y) * normal->y + static_cast<double>(normal->x) * offset->x) +
                          static_cast<double>(offset->z) * normal->z;
    if (facing >= 0.0)
        return false;
    const double radius = capsule->radius;
    return distance <= radius * radius;
}

// 0x004114E0 (slot 17, an instance's too: its faces are taken unplaced)
bool CCollStatic::CollideWithCapsule(const Mth::Capsule* capsule, const Mth::BBox* box, CapsuleHits* hits)
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
        if (!(hits->sweep == 0.0f)) // (a moving capsule isn't tested)
            continue;
        Mth::Vector point, normal, offset;
        if (!CapsuleHitsTriangle(capsule, &v0, &v1, &v2, &point, &normal, &offset))
            continue;
        if (hits->count == CapsuleHits::kMost)
            return true;
        CapsuleHit& entry = hits->hits[hits->count];
        entry.normal = normal;
        entry.offset = offset;
        entry.faceFlag40 = static_cast<uint8_t>(flags & 0x40);
        entry.checksum = m_checksum;
        entry.point = point;
        if (hits->sweep > 0.0f) // (never here: the sweep is 0)
            entry.t = 0.0f;
        ++hits->count;
        if (hits->mode == 2)
            return true;
    }
    return hits->count > 0;
}

} // namespace Nx
