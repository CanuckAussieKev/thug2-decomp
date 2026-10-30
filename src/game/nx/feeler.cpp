// The collision feeler. See feeler.h.

#include "game/nx/feeler.h"

#include <intrin.h>

#include <cmath>

#include "game/gfx/debug_lines.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tri.h"
#include "game/nx/coll_world.h"
#include "game/obj/object.h"
#include "game/skater/debug_collision.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

GAME_GLOBAL(CCollCache*, g_currentCache, 0x00691974);
namespace {


// The cache queries use when the feeler has none.
CCollCache*& s_currentCache = g_currentCache;

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
CFeeler* CFeeler::Construct()
{
    m_object.mp_prev = nullptr;
    m_object.mp_next = nullptr;
    m_object.mp_object = nullptr;
    m_callback = nullptr;
    mp_cache = nullptr;
    ResetHit(m_hit);
    return this;
}

// 0x00417250 (the original first sets the hit's normal to (1, 1, 1, 1), then to (0, 0, 0, 1))
CFeeler* CFeeler::Construct(const Mth::Vector* start, const Mth::Vector* end)
{
    m_object.mp_prev = nullptr;
    m_object.mp_next = nullptr;
    m_object.mp_object = nullptr;
    m_line.start = *start;
    m_line.end = *end;
    m_callback = nullptr;
    mp_cache = nullptr;
    ResetHit(m_hit);
    return this;
}

// 0x004170F0
void CFeeler::SetIgnore(uint16_t ignore, uint16_t require)
{
    m_hit.ignoreFlags = ignore;
    m_hit.requireFlags = require;
}

// 0x00417190
void CFeeler::SetStart(const Mth::Vector* start) { m_line.start = *start; }

// 0x004171B0
void CFeeler::SetEnd(const Mth::Vector* end) { m_line.end = *end; }

// 0x004171D0
void CFeeler::SetLine(const Mth::Vector* start, const Mth::Vector* end)
{
    m_line.start = *start;
    m_line.end = *end;
}

// 0x00417110
void __cdecl CFeeler::CallbackRelay(CollisionHit* hit)
{
    auto* feeler = reinterpret_cast<CFeeler*>(hit->callbackData);
    feeler->m_callback(feeler);
}

// 0x004172F0
int CFeeler::GetCollision(bool movables, bool farthest)
{
    Mth::Line line = m_line;
    m_hit.callbackData = reinterpret_cast<uint32_t>(this);
    m_hit.hit = false;
    m_hit.faceFlag40 = 0;
    m_hit.unknown38 = 0;
    m_hit.owner = 0;
    CollisionHitCallback callback = m_callback ? &CallbackRelay : nullptr;
    CCollCache* cache = mp_cache ? mp_cache : s_currentCache;
    m_object.Release();
    m_objectId = 0;
    bool hitStatic = farthest ? CollideWithStaticsFarthest(&line, &m_hit, callback, cache)
                              : CollideWithStatics(&line, &m_hit, callback, cache);
    if (movables) {
        m_object.Set(farthest ? CollideWithMovablesFarthest(&line, &m_hit, callback, cache)
                              : CollideWithMovables(&line, &m_hit, callback, cache));
        if (m_object)
            m_objectId = m_object->m_id;
    }
    m_distance = m_hit.distance;
    m_point = m_hit.point;
    m_normal = m_hit.normal;
    int result = hitStatic || m_object ? 1 : 0;
    // NOT ORIGINAL: [features] debug_collision logs what was hit (src/game/skater/debug_collision.h).
    DebugCollision::NoteFeelerQuery(this, result,
                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress())));
    return result;
}

// 0x00417710
int CFeeler::GetCollisionAlong(const Mth::Vector* start, const Mth::Vector* end, bool movables)
{
    m_line.start = *start;
    m_line.end = *end;
    return GetCollision(movables, false);
}

// 0x00417120 (the original's EAX keeps its upper bytes from before when nothing was hit)
bool CFeeler::IsHitTriangleCollision()
{
    if (!m_hit.hit)
        return true;
    return m_hit.object->Call<bool>(1);
}

// 0x00417140
uint32_t CFeeler::GetTestedOwner() { return m_hit.owner; }

// 0x00417210
bool CFeeler::HitObjectAlive()
{
    if (m_object)
        return true;
    if (m_objectId != 0) {
        m_objectId = 0;
        m_hit.objectChecksum = 0;
    }
    return false;
}

// 0x00417240
Obj::CCompositeObject* CFeeler::GetHitObject() { return m_object.Get(); }

// 0x004174A0 (the corners about the hit point; the original's x87 code keeps the lengths and the blend in double
// precision)
double CFeeler::GetHitVertexValue() const
{
    if (!m_hit.hit)
        return kNoHitValue;
    CCollObj* object = m_hit.object;
    const CCollObjTriData* geometry = object->m_geometry;
    const Mth::Vector& at = m_hit.point;
    Mth::Vector corner[3];
    float value[3];
    for (int k = 0; k < 3; ++k) {
        uint32_t vertex = geometry->FaceVertex(static_cast<int>(m_hit.face), k);
        Mth::Vector position;
        object->Call<Mth::Vector*>(9, &position, static_cast<int>(vertex)); // (slot 9 returns it through a pointer)
        corner[k] = {Difference(position.x, at.x), Difference(position.y, at.y), Difference(position.z, at.z), 1.0f};
        value[k] = static_cast<float>(geometry->m_vertexBytes[vertex]) * kByteScale;
    }
    // The line from the first corner through the hit point, where it meets the opposite edge's line.
    const Mth::Vector hitPoint{0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Line fromCorner(corner[0], hitPoint), opposite(corner[1], corner[2]);
    Mth::Vector onLine{0.0f, 0.0f, 0.0f, 0.0f}, onEdge; // (the original's are left unset when the lines are parallel)
    float s, t;
    Mth::ClosestPointsBetweenLines(fromCorner, opposite, onLine, onEdge, s, t, false);
    auto minus = [](const Mth::Vector& a, const Mth::Vector& b, double* x, double* y, double* z) {
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
void CFeeler::DebugDraw(uint32_t r, uint32_t g, uint32_t b, int frames)
{
    uint32_t colour = ((((b | 0xFFFFFF00u) << 8) | g) << 8) | r;
    Gfx::AddDebugLine(&m_line.start, &m_line.end, colour, colour, frames);
}

// 0x0041C350
void CFeeler::Destruct() { m_object.Release(); }

// 0x004F8200
CFeeler* CFeeler::CopyConstruct(const CFeeler& other)
{
    m_line = other.m_line;
    m_callback = other.m_callback;
    m_hit.CopyFrom(other.m_hit);
    m_distance = other.m_distance;
    m_point = other.m_point;
    m_normal = other.m_normal;
    m_object.ConstructCopy(&other.m_object);
    m_objectId = other.m_objectId;
    m_unknownA8 = other.m_unknownA8;
    mp_cache = other.mp_cache;
    return this;
}

// 0x0043A930
CFeeler* CFeeler::Assign(const CFeeler& other)
{
    m_line = other.m_line;
    m_callback = other.m_callback;
    m_hit.CopyFrom(other.m_hit);
    m_distance = other.m_distance;
    m_point = other.m_point;
    m_normal = other.m_normal;
    m_object.Assign(&other.m_object);
    m_objectId = other.m_objectId;
    m_unknownA8 = other.m_unknownA8;
    mp_cache = other.mp_cache;
    return this;
}

} // namespace Nx

REPLACE(0x00417760, static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct));
REPLACE(0x00417250,
        static_cast<Nx::CFeeler* (Nx::CFeeler::*)(const Mth::Vector*, const Mth::Vector*)>(&Nx::CFeeler::Construct));
REPLACE(0x004170F0, &Nx::CFeeler::SetIgnore);
REPLACE(0x0041C350, &Nx::CFeeler::Destruct);
REPLACE(0x00417190, &Nx::CFeeler::SetStart);
REPLACE(0x004171B0, &Nx::CFeeler::SetEnd);
REPLACE(0x004171D0, &Nx::CFeeler::SetLine);
REPLACE(0x00417110, Nx::CFeeler::CallbackRelay);
REPLACE(0x004172F0, &Nx::CFeeler::GetCollision);
REPLACE(0x00417710, &Nx::CFeeler::GetCollisionAlong);
REPLACE(0x00417120, &Nx::CFeeler::IsHitTriangleCollision);
REPLACE(0x00417140, &Nx::CFeeler::GetTestedOwner);
REPLACE(0x00417210, &Nx::CFeeler::HitObjectAlive);
REPLACE(0x00417240, &Nx::CFeeler::GetHitObject);
REPLACE(0x00417150, &Nx::CFeeler::DebugDraw);
REPLACE(0x004174A0, &Nx::CFeeler::GetHitVertexValue);
REPLACE(0x0043A930, &Nx::CFeeler::Assign);
REPLACE(0x004F8200, &Nx::CFeeler::CopyConstruct);
