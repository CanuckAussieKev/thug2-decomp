// The collision objects' line tests and the hit record they fill.

#include "game/nx/coll_line.h"

#include <cmath>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"

namespace Engine3D {

bool g_wantFarthest;

namespace {

// Set while a caller (0x00410930) wants the farthest hit rather than the nearest.
bool& s_wantFarthest = g_wantFarthest;

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rejection threshold of a line test: the line's distance from the geometry's minimum corner times the
// geometry's rejection scale, truncated (to the low 32 bits of the 64-bit conversion, as _ftol).
uint32_t Threshold(double distance, const CTriMeshSource* geometry)
{
    return static_cast<uint32_t>(Truncate(distance * geometry->RejectionScale()));
}

// A face's three corners (raw vertices).
void Corners(const CTriMeshSource* geometry, uint32_t face, Maths::Vec4* v0, Maths::Vec4* v1, Maths::Vec4* v2)
{
    geometry->ReadStoredVertex(geometry->FaceVertex(face, 0), v0);
    geometry->ReadStoredVertex(geometry->FaceVertex(face, 1), v1);
    geometry->ReadStoredVertex(geometry->FaceVertex(face, 2), v2);
}

} // namespace

// 0x004F8160
CollisionHit* CollisionHit::CopyFrom(const CollisionHit& other)
{
    normal = other.normal;
    point = other.point;
    face = other.face;
    distance = other.distance;
    ignoreFlags = other.ignoreFlags;
    requireFlags = other.requireFlags;
    faceFlags = other.faceFlags;
    terrain = other.terrain;
    object = other.object;
    objectChecksum = other.objectChecksum;
    unknown38 = other.unknown38;
    callback = other.callback;
    callbackData = other.callbackData;
    owner = other.owner;
    hit = other.hit;
    faceFlag40 = other.faceFlag40;
    scene = other.scene;
    return this;
}

// 0x004131F0
bool ReportHit(const Maths::Seg& line, CHitMesh* object, const FaceHit& face, float distance, CollisionHit* hit)
{
    bool better = s_wantFarthest ? !(distance < hit->distance) : distance <= hit->distance;
    CollisionHit saved;
    if (!better)
        saved.CopyFrom(*hit);
    bool taken = false;
    uint32_t flags = object->Call<uint32_t>(2, face.face);
    if (!(flags & hit->ignoreFlags) && !(~flags & hit->requireFlags)) {
        hit->hit = true;
        hit->normal = face.normal;
        hit->point = face.point;
        hit->distance = distance;
        hit->face = face.face;
        hit->faceFlags = static_cast<uint16_t>(flags);
        hit->terrain = static_cast<uint8_t>(object->Call<uint32_t>(3, face.face));
        hit->object = object;
        hit->faceFlag40 = static_cast<uint8_t>(flags & 0x40);
        hit->objectChecksum = object->m_checksum;
        // the point recomputed along the line
        double dx = static_cast<double>(line.end.x) - line.start.x;
        double dy = static_cast<double>(line.end.y) - line.start.y;
        float dz = Difference(line.end.z, line.start.z);
        float px = static_cast<float>(dx * distance);
        float py = static_cast<float>(dy * distance);
        hit->point.w = 1.0f;
        hit->point.z = static_cast<float>(static_cast<double>(dz) * distance + line.start.z);
        hit->point.x = static_cast<float>(static_cast<double>(px) + line.start.x);
        hit->point.y = static_cast<float>(static_cast<double>(py) + line.start.y);
        if (hit->callback)
            hit->callback(hit);
        taken = true;
    }
    if (!better) {
        hit->CopyFrom(saved);
        return false;
    }
    return taken;
}

// 0x004137B0 (the face index handed on is left unset by the original - stack garbage; 0 here)
bool CHitMovingBox::CollideWithLine(const Maths::Seg* line, const Maths::Vec4* /*direction*/, CollisionHit* hit,
                                   const Maths::BBox* /*lineBox*/)
{
    Maths::Seg local; // (its constructor, 0x005902E0, does nothing)
    BoxToLocal(reinterpret_cast<const Maths::BBox*>(line), reinterpret_cast<Maths::BBox*>(&local));
    Maths::BBox around(local.start);
    around.AppendSpot(local.end);
    if (!m_box.Intersects(around))
        return false;
    Maths::Vec4 point, normal;
    if (!m_box.SegmentCrossing(local, point, normal))
        return false;
    float hx = Difference(point.x, local.start.x), hy = Difference(point.y, local.start.y),
          hz = Difference(point.z, local.start.z);
    float lx = Difference(local.end.x, local.start.x), ly = Difference(local.end.y, local.start.y),
          lz = Difference(local.end.z, local.start.z);
    Maths::Vec4 turned;
    m_orientation.RotateVector(turned, point);
    FaceHit face;
    face.point = {static_cast<float>(static_cast<double>(turned.x) + m_anchorPoint.x),
                  static_cast<float>(static_cast<double>(turned.y) + m_anchorPoint.y),
                  static_cast<float>(static_cast<double>(turned.z) + m_anchorPoint.z),
                  static_cast<float>(static_cast<double>(turned.w) + m_anchorPoint.w)};
    m_orientation.RotateVector(face.normal, normal);
    face.face = 0;
    float distance = static_cast<float>(
        std::sqrt((static_cast<double>(hz) * hz + static_cast<double>(hy) * hy) + static_cast<double>(hx) * hx) /
        std::sqrt((static_cast<double>(lz) * lz + static_cast<double>(ly) * ly) + static_cast<double>(lx) * lx));
    return ReportHit(*line, this, face, distance, hit);
}

// 0x00413B50
bool CStaticCollider::CollideWithLine(const Maths::Seg* line, const Maths::Vec4* direction, CollisionHit* hit,
                                  const Maths::BBox* lineBox)
{
    // the line's distance from the geometry's minimum corner, for the rejection test
    double ux = direction->x, uy = direction->y;
    float uz = direction->z;
    double length = std::sqrt((static_cast<double>(uz) * uz + uy * uy) + ux * ux);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        ux *= inverse;
        uy *= inverse;
        uz = static_cast<float>(uz * inverse);
    }
    CTriMeshSource* geometry = m_geometry;
    float cx = Difference(geometry->m_lowerLimit.x, line->start.x);
    float cy = Difference(geometry->m_lowerLimit.y, line->start.y);
    double czExact = static_cast<double>(geometry->m_lowerLimit.z) - line->start.z;
    float cz = static_cast<float>(czExact);
    float along = static_cast<float>((czExact * uz + cy * uy) + cx * ux);
    float px = static_cast<float>(ux * along);
    double py = uy * along;
    double pz = static_cast<double>(uz) * along;
    float rx = Difference(cx, px);
    float ry = static_cast<float>(cy - py);
    double rz = cz - pz;
    uint32_t threshold =
        Threshold(std::sqrt((rz * rz + static_cast<double>(ry) * ry) + static_cast<double>(rx) * rx), geometry);
    if (threshold > 0xFF)
        return false;

    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry->GetFacesInBox(lineBox, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        geometry = m_geometry;
        uint32_t face = faces[i];
        uint8_t rejection = geometry->m_smallFaces ? geometry->m_faces[face * 8 + 7] : m_octets[face];
        if (threshold > rejection)
            continue;
        Maths::Vec4 v0, v1, v2;
        Corners(geometry, face, &v0, &v1, &v2);
        float t;
        if (!RayHitsTriangle(line->start, *direction, v0, v1, v2, &t))
            continue;
        // the normal (v1 - v0) x (v2 - v0), rounded where the original stores
        double e0x = static_cast<double>(v1.x) - v0.x;
        float e0y = Difference(v1.y, v0.y), e0z = Difference(v1.z, v0.z);
        float e1x = Difference(v2.x, v0.x), e1y = Difference(v2.y, v0.y);
        double e1zExact = static_cast<double>(v2.z) - v0.z;
        float e1z = static_cast<float>(e1zExact);
        FaceHit faceHit;
        faceHit.normal = {static_cast<float>(e1zExact * e0y - static_cast<double>(e1y) * e0z),
                          static_cast<float>(static_cast<double>(e0z) * e1x - e1z * e0x),
                          static_cast<float>(e1y * e0x - static_cast<double>(e0y) * e1x), 0.0f};
        faceHit.point = v0;
        faceHit.face = face;
        faceHit.normal.MakeUnit();
        if (ReportHit(*line, this, faceHit, t, hit))
            any = true;
    }
    return any;
}

// 0x00413FF0
bool CCollStaticInstance::CollideWithLine(const Maths::Seg* line, const Maths::Vec4* /*direction*/,
                                          CollisionHit* hit, const Maths::BBox* /*lineBox*/)
{
    Maths::Seg local; // (its constructor, 0x005902E0, does nothing)
    LineToLocal(line, &local);
    Maths::Vec4 way{Difference(local.end.x, local.start.x), Difference(local.end.y, local.start.y),
                    Difference(local.end.z, local.start.z), Difference(local.end.w, local.start.w)};
    Maths::BBox around(local.start);
    around.AppendSpot(local.end);
    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = m_geometry->GetFacesInBox(&around, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        CTriMeshSource* geometry = m_geometry;
        uint32_t face = faces[i];
        Maths::Vec4 v0, v1, v2;
        Corners(geometry, face, &v0, &v1, &v2);
        float t;
        if (!RayHitsTriangle(local.start, way, v0, v1, v2, &t))
            continue;
        double e0x = static_cast<double>(v1.x) - v0.x, e0y = static_cast<double>(v1.y) - v0.y,
               e0z = static_cast<double>(v1.z) - v0.z;
        float e1x = Difference(v2.x, v0.x), e1y = Difference(v2.y, v0.y);
        double e1z = static_cast<double>(v2.z) - v0.z;
        FaceHit faceHit;
        faceHit.face = face;
        float nx = static_cast<float>(e1z * e0y - e1y * e0z);
        float ny = static_cast<float>(e0z * e1x - e1z * e0x);
        double nzExact = e1y * e0x - e0y * e1x;
        float nz = static_cast<float>(nzExact);
        double length = std::sqrt((static_cast<double>(nx) * nx + nzExact * nzExact) + static_cast<double>(ny) * ny);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            nx = static_cast<float>(nx * inverse);
            ny = static_cast<float>(ny * inverse);
            nz = static_cast<float>(nzExact * inverse);
        }
        // the corner scaled and placed, the normal turned
        Maths::Vec4 scaled{static_cast<float>(static_cast<double>(v0.x) * m_sizeFactor.x),
                           static_cast<float>(static_cast<double>(v0.y) * m_sizeFactor.y),
                           static_cast<float>(static_cast<double>(v0.z) * m_sizeFactor.z),
                           static_cast<float>(static_cast<double>(v0.w) * m_sizeFactor.w)};
        m_basisFrame.CarryPointThrough(faceHit.point, scaled);
        const Maths::Vec4* r = m_basisFrame.row;
        faceHit.normal = {
            static_cast<float>((static_cast<double>(nz) * r[2].x + static_cast<double>(ny) * r[1].x) +
                               static_cast<double>(nx) * r[0].x),
            static_cast<float>((static_cast<double>(nx) * r[0].y + static_cast<double>(nz) * r[2].y) +
                               static_cast<double>(ny) * r[1].y),
            static_cast<float>((static_cast<double>(nx) * r[0].z + static_cast<double>(nz) * r[2].z) +
                               static_cast<double>(ny) * r[1].z),
            0.0f};
        if (ReportHit(*line, this, faceHit, t, hit))
            any = true;
    }
    return any;
}

// 0x00414460
bool CHitMovingTriangles::CollideWithLine(const Maths::Seg* line, const Maths::Vec4* /*direction*/, CollisionHit* hit,
                                  const Maths::BBox* /*lineBox*/)
{
    Maths::Seg local; // (its constructor, 0x005902E0, does nothing)
    BoxToLocal(reinterpret_cast<const Maths::BBox*>(line), reinterpret_cast<Maths::BBox*>(&local));
    Maths::Vec4 way{Difference(local.end.x, local.start.x), Difference(local.end.y, local.start.y),
                    Difference(local.end.z, local.start.z), Difference(local.end.w, local.start.w)};
    Maths::BBox around(local.start);
    around.AppendSpot(local.end);
    CTriMeshSource* geometry = m_geometry;
    uint32_t threshold = 0;
    if (geometry->m_smallFaces) {
        Maths::Vec4 unit = way;
        unit.MakeUnit();
        double cx = static_cast<double>(geometry->m_lowerLimit.x) - local.start.x;
        double cy = static_cast<double>(geometry->m_lowerLimit.y) - local.start.y;
        float cz = Difference(geometry->m_lowerLimit.z, local.start.z);
        double along = (cx * unit.x + static_cast<double>(cz) * unit.z) + cy * unit.y;
        double px = unit.x * along;
        float py = static_cast<float>(unit.y * along);
        float pz = static_cast<float>(unit.z * along);
        float rx = static_cast<float>(cx - px);
        double ry = cy - py;
        double rz = static_cast<double>(cz) - pz;
        threshold = Threshold(std::sqrt((static_cast<double>(rx) * rx + rz * rz) + ry * ry), geometry);
        if (threshold > 0xFF)
            return false;
    }
    int count;
    uint16_t buffer[0x800];
    const uint16_t* faces = geometry->GetFacesInBox(&around, &count, buffer, 0x800);
    bool any = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        geometry = m_geometry;
        uint32_t face = faces[i];
        if (geometry->m_smallFaces && threshold > geometry->m_faces[face * 8 + 7])
            continue;
        Maths::Vec4 v0, v1, v2;
        Corners(geometry, face, &v0, &v1, &v2);
        float t;
        if (!RayHitsTriangle(local.start, way, v0, v1, v2, &t))
            continue;
        double e0x = static_cast<double>(v1.x) - v0.x, e0y = static_cast<double>(v1.y) - v0.y,
               e0z = static_cast<double>(v1.z) - v0.z;
        float e1x = Difference(v2.x, v0.x), e1y = Difference(v2.y, v0.y);
        double e1z = static_cast<double>(v2.z) - v0.z;
        FaceHit faceHit;
        faceHit.face = face;
        float nx = static_cast<float>(e1z * e0y - e1y * e0z);
        float ny = static_cast<float>(e0z * e1x - e1z * e0x);
        float nz = static_cast<float>(e1y * e0x - e0y * e1x);
        // the length and its inverse rounded to floats; x kept unrounded for the turn
        double length = std::sqrt((static_cast<double>(nx) * nx + static_cast<double>(nz) * nz) +
                                  static_cast<double>(ny) * ny);
        double unitX = nx;
        if (length > 0.0) {
            float inverse = static_cast<float>(1.0 / static_cast<float>(length));
            unitX = static_cast<double>(nx) * inverse;
            ny = static_cast<float>(static_cast<double>(ny) * inverse);
            nz = static_cast<float>(static_cast<double>(nz) * inverse);
        }
        // the corner turned and placed (z rounded before it's placed), the normal turned
        const Maths::Vec4* r = m_orientation.row;
        double placedX = (static_cast<double>(v0.z) * r[2].x + static_cast<double>(v0.y) * r[1].x) +
                         static_cast<double>(v0.x) * r[0].x;
        double placedY = (static_cast<double>(v0.z) * r[2].y + static_cast<double>(v0.y) * r[1].y) +
                         static_cast<double>(v0.x) * r[0].y;
        float placedZ = static_cast<float>((static_cast<double>(v0.z) * r[2].z + static_cast<double>(v0.y) * r[1].z) +
                                           static_cast<double>(v0.x) * r[0].z);
        faceHit.point = {static_cast<float>(placedX + m_anchorPoint.x), static_cast<float>(placedY + m_anchorPoint.y),
                         static_cast<float>(static_cast<double>(placedZ) + m_anchorPoint.z),
                         static_cast<float>(static_cast<double>(v0.w) + m_anchorPoint.w)};
        faceHit.normal = {
            static_cast<float>((static_cast<double>(nz) * r[2].x + static_cast<double>(ny) * r[1].x) + unitX * r[0].x),
            static_cast<float>((unitX * r[0].y + static_cast<double>(nz) * r[2].y) + static_cast<double>(ny) * r[1].y),
            static_cast<float>((unitX * r[0].z + static_cast<double>(nz) * r[2].z) + static_cast<double>(ny) * r[1].z),
            0.0f};
        if (ReportHit(*line, this, faceHit, t, hit))
            any = true;
    }
    return any;
}

} // namespace Nx
