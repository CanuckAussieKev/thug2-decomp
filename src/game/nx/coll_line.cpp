// The collision objects' line tests and the hit record they fill.

#include "game/nx/coll_line.h"

#include <cmath>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_obj.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// Set while a caller (0x00410930) wants the farthest hit rather than the nearest.
bool& s_wantFarthest = GameVariable<bool>(0x00691910);

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

} // namespace

// 0x004F8160 (outside this range: written here for ReportHit, not registered)
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
    unknown44 = other.unknown44;
    hit = other.hit;
    faceFlag40 = other.faceFlag40;
    unknown4C = other.unknown4C;
    return this;
}

// 0x004131F0
bool ReportHit(const Mth::Line& line, CCollObj* object, const FaceHit& face, float distance, CollisionHit* hit)
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
bool CCollMovBBox::CollideWithLine(const Mth::Line* line, const Mth::Vector* /*direction*/, CollisionHit* hit,
                                   const Mth::BBox* /*lineBox*/)
{
    Mth::Line local; // (its constructor, 0x005902E0, does nothing)
    BoxToLocal(reinterpret_cast<const Mth::BBox*>(line), reinterpret_cast<Mth::BBox*>(&local));
    Mth::BBox around(local.start);
    around.AddPoint(local.end);
    if (!m_box.Intersects(around))
        return false;
    Mth::Vector point, normal;
    if (!m_box.LineIntersect(local, point, normal))
        return false;
    float hx = Difference(point.x, local.start.x), hy = Difference(point.y, local.start.y),
          hz = Difference(point.z, local.start.z);
    float lx = Difference(local.end.x, local.start.x), ly = Difference(local.end.y, local.start.y),
          lz = Difference(local.end.z, local.start.z);
    Mth::Vector turned;
    m_orientation.RotateVector(turned, point);
    FaceHit face;
    face.point = {static_cast<float>(static_cast<double>(turned.x) + m_position.x),
                  static_cast<float>(static_cast<double>(turned.y) + m_position.y),
                  static_cast<float>(static_cast<double>(turned.z) + m_position.z),
                  static_cast<float>(static_cast<double>(turned.w) + m_position.w)};
    m_orientation.RotateVector(face.normal, normal);
    face.face = 0;
    float distance = static_cast<float>(
        std::sqrt((static_cast<double>(hz) * hz + static_cast<double>(hy) * hy) + static_cast<double>(hx) * hx) /
        std::sqrt((static_cast<double>(lz) * lz + static_cast<double>(ly) * ly) + static_cast<double>(lx) * lx));
    return ReportHit(*line, this, face, distance, hit);
}

} // namespace Nx

REPLACE(0x004131F0, Nx::ReportHit);
REPLACE(0x004137B0, &Nx::CCollMovBBox::CollideWithLine);
