// The collision feeler. See feeler.h.

#include "game/nx/feeler.h"

#include <intrin.h>

#include <cmath>
#include <cstdint>

#include "game/gfx/debug_lines.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tri.h"
#include "game/nx/coll_world.h"
#include "game/obj/object.h"
#include "game/skater/debug_collision.h"

namespace Engine3D {

CHitMeshCache* g_currentCache;
namespace {

// The cache queries use when the feeler has none.
CHitMeshCache*& s_currentCache = g_currentCache;

void ResetHit(CollisionHit& hit)
{
    hit.ignoreFlags = 0x10;
    hit.requireFlags = 0;
    hit.normal = {0.0f, 0.0f, 0.0f, 1.0f};
    hit.point = {0.0f, 0.0f, 0.0f, 1.0f};
}

// GetHitVertexValue without a hit (0x006453BC), and a vertex byte's scale (1 / 255, 0x00645D70).
constexpr float kNoHitValue = 0.5f;
constexpr float kByteScale = 1.0f / 255.0f;

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
double LengthSquared(double x, double y, double z) { return (z * z + y * y) + x * x; }

} // namespace

// 0x00417760
CProbeRay* CProbeRay::Construct()
{
    m_object.m_preceding = nullptr;
    m_object.m_following = nullptr;
    m_object.m_ownerObject = nullptr;
    m_callback = nullptr;
    m_hitCache = nullptr;
    ResetHit(m_hit);
    return this;
}

// 0x00417250 (the original first sets the hit's normal to (1, 1, 1, 1), then to (0, 0, 0, 1))
CProbeRay* CProbeRay::Construct(const Maths::Vector* start, const Maths::Vector* end)
{
    m_object.m_preceding = nullptr;
    m_object.m_following = nullptr;
    m_object.m_ownerObject = nullptr;
    m_line.start = *start;
    m_line.end = *end;
    m_callback = nullptr;
    m_hitCache = nullptr;
    ResetHit(m_hit);
    return this;
}

// 0x004170F0
void CProbeRay::SetExcluded(uint16_t ignore, uint16_t require)
{
    m_hit.ignoreFlags = ignore;
    m_hit.requireFlags = require;
}

// 0x00417190
void CProbeRay::SetStart(const Maths::Vector* start) { m_line.start = *start; }

// 0x004171B0
void CProbeRay::SetEnd(const Maths::Vector* end) { m_line.end = *end; }

// 0x004171D0
void CProbeRay::SetLine(const Maths::Vector* start, const Maths::Vector* end)
{
    m_line.start = *start;
    m_line.end = *end;
}

// 0x00417110
void __cdecl CProbeRay::CallbackRelay(CollisionHit* hit)
{
    auto* feeler = reinterpret_cast<CProbeRay*>(hit->callbackData);
    feeler->m_callback(feeler);
}

// 0x004172F0
int CProbeRay::GetHitShape(bool movables, bool farthest)
{
    Maths::Line line = m_line;
    m_hit.callbackData = reinterpret_cast<uint32_t>(this);
    m_hit.hit = false;
    m_hit.faceFlag40 = 0;
    m_hit.unknown38 = 0;
    m_hit.owner = 0;
    CollisionHitCallback callback = m_callback ? &CallbackRelay : nullptr;
    CHitMeshCache* cache = m_hitCache ? m_hitCache : s_currentCache;
    m_object.Release();
    m_entityKey = 0;
    bool hitStatic = farthest ? CollideWithStaticsFarthest(&line, &m_hit, callback, cache)
                              : CollideWithStatics(&line, &m_hit, callback, cache);
    if (movables) {
        m_object.Set(farthest ? CollideWithMovablesFarthest(&line, &m_hit, callback, cache)
                              : CollideWithMovables(&line, &m_hit, callback, cache));
        if (m_object)
            m_entityKey = m_object->m_id;
    }
    m_reach = m_hit.distance;
    m_contactPos = m_hit.point;
    m_surfaceDir = m_hit.normal;
    int result = hitStatic || m_object ? 1 : 0;
    // NOT ORIGINAL: [features] debug_collision logs what was hit (src/game/skater/debug_collision.h).
    DebugCollision::NoteFeelerQuery(this, result,
                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress())));
    return result;
}

// 0x00417710
int CProbeRay::GetCollisionAlong(const Maths::Vector* start, const Maths::Vector* end, bool movables)
{
    m_line.start = *start;
    m_line.end = *end;
    return GetHitShape(movables, false);
}

// 0x00417120 (the original's EAX keeps its upper bytes from before when nothing was hit)
bool CProbeRay::IsHitTriangleCollision()
{
    if (!m_hit.hit)
        return true;
    return m_hit.object->Call<bool>(1);
}

// 0x00417140
uint32_t CProbeRay::GetTestedOwner() { return m_hit.owner; }

// 0x00417210
bool CProbeRay::HitObjectAlive()
{
    if (m_object)
        return true;
    if (m_entityKey != 0) {
        m_entityKey = 0;
        m_hit.objectChecksum = 0;
    }
    return false;
}

// 0x00417240
GameObjects::CGameEntity* CProbeRay::GetHitObject() { return m_object.Get(); }

// 0x004174A0 (the corners about the hit point; the original's x87 code keeps the lengths and the blend in double
// precision)
double CProbeRay::GetHitVertexValue() const
{
    if (!m_hit.hit)
        return kNoHitValue;
    CHitMesh* object = m_hit.object;
    const CTriMeshSource* geometry = object->m_geometry;
    const Maths::Vector& at = m_hit.point;
    Maths::Vector corner[3];
    float value[3];
    for (int k = 0; k < 3; ++k) {
        uint32_t vertex = geometry->FaceVertex(static_cast<int>(m_hit.face), k);
        Maths::Vector position;
        object->Call<Maths::Vector*>(9, &position, static_cast<int>(vertex)); // (slot 9 returns it through a pointer)
        corner[k] = {Difference(position.x, at.x), Difference(position.y, at.y), Difference(position.z, at.z), 1.0f};
        value[k] = static_cast<float>(geometry->m_vertexBytes[vertex]) * kByteScale;
    }
    // The line from the first corner through the hit point, where it meets the opposite edge's line.
    const Maths::Vector hitPoint{0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Line fromCorner(corner[0], hitPoint), opposite(corner[1], corner[2]);
    Maths::Vector onLine{0.0f, 0.0f, 0.0f, 0.0f}, onEdge; // (the original's are left unset when the lines are parallel)
    float s, t;
    Maths::ClosestPointsBetweenLines(fromCorner, opposite, onLine, onEdge, s, t, false);
    auto minus = [](const Maths::Vector& a, const Maths::Vector& b, double* x, double* y, double* z) {
        *x = static_cast<double>(a.x) - b.x;
        *y = static_cast<double>(a.y) - b.y;
        *z = static_cast<double>(a.z) - b.z;
    };
    double x, y, z;
    minus(onLine, corner[1], &x, &y, &z);
    const float alongEdgeSquared = static_cast<float>(LengthSquared(x, y, z));
    minus(corner[2], corner[1], &x, &y, &z);
    const float edgeSquared = static_cast<float>(LengthSquared(x, y, z));
    minus(onLine, corner[0], &x, &y, &z);
    const double toEdge = std::sqrt(LengthSquared(x, y, z));
    const double toHit = std::sqrt(LengthSquared(corner[0].x, corner[0].y, corner[0].z));
    const double fraction = std::sqrt(static_cast<double>(alongEdgeSquared)) /
                            std::sqrt(static_cast<double>(edgeSquared));
    const double onEdgeValue = (static_cast<double>(value[2]) - value[1]) * fraction + value[1];
    return (onEdgeValue - value[0]) * (toHit / toEdge) + value[0];
}

// 0x00417150 (the colour: alpha 0xFF over the low byte of b, then g and r ORed in whole, as the original)
void CProbeRay::DebugDraw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    Gfx2D::PushDiagLine(&m_line.start, &m_line.end, colour, colour, frames);
}

// 0x0041C350
void CProbeRay::Destruct() { m_object.Release(); }

// 0x004F8200
CProbeRay* CProbeRay::CopyConstruct(const CProbeRay& other)
{
    m_line = other.m_line;
    m_callback = other.m_callback;
    m_hit.CopyFrom(other.m_hit);
    m_reach = other.m_reach;
    m_contactPos = other.m_contactPos;
    m_surfaceDir = other.m_surfaceDir;
    m_object.ConstructCopy(&other.m_object);
    m_entityKey = other.m_entityKey;
    m_unknownA8 = other.m_unknownA8;
    m_hitCache = other.m_hitCache;
    return this;
}

// 0x0043A930
CProbeRay* CProbeRay::Assign(const CProbeRay& other)
{
    m_line = other.m_line;
    m_callback = other.m_callback;
    m_hit.CopyFrom(other.m_hit);
    m_reach = other.m_reach;
    m_contactPos = other.m_contactPos;
    m_surfaceDir = other.m_surfaceDir;
    m_object.Assign(&other.m_object);
    m_entityKey = other.m_entityKey;
    m_unknownA8 = other.m_unknownA8;
    m_hitCache = other.m_hitCache;
    return this;
}

} // namespace Nx
