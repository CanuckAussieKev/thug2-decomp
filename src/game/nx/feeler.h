#pragma once

// The collision feeler (0xB0 bytes): a line asked what it hits in the world - the static collision and, if wanted,
// the movable objects - with the result kept: the hit record, and copies of its distance, point and normal; the
// composite object hit held by a smart pointer. (Other code still calls these through their addresses: park_floor,
// rail_manager, camera_util, editor_camera, avoid, composite, level_editor.)

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/nx/coll_line.h"
#include "game/obj/smtptr.h"

namespace Obj {
class CCompositeObject;
}

namespace Nx {

class CCollCache;
class CFeeler;

// Called (cdecl) with the feeler for every face its query accepts.
using FeelerCallback = void(__cdecl*)(CFeeler* feeler);

class CFeeler {
public:
    // 0x00417760: no callback, cache or object; faces flagged 0x10 ignored; the hit's normal and point (0, 0, 0, 1).
    // The line is left unset. Returns this.
    CFeeler* Construct();
    // 0x00417250: the same with the line from `start` to `end`. Returns this.
    CFeeler* Construct(const Mth::Vector* start, const Mth::Vector* end);

    // 0x0041C350: the destructor's body: the object hit let go (its smart pointer).
    void Destruct();

    // 0x004170F0: faces with any of `ignore` set, or without all of `require`, are passed over.
    void SetIgnore(uint16_t ignore, uint16_t require);
    // 0x00417190 / 0x004171B0 / 0x004171D0
    void SetStart(const Mth::Vector* start);
    void SetEnd(const Mth::Vector* end);
    void SetLine(const Mth::Vector* start, const Mth::Vector* end);

    // 0x004172F0: the line against the static collision (the farthest hit with `farthest`, else the nearest), then
    // with `movables` the movable objects (a nearer / farther one wins); through the feeler's cache, or the current
    // one (0x00691974). The callback, if any, is called for each accepted face. The object hit (a movable's
    // owner) and its id kept; the hit's distance, point and normal copied. 1 if anything was hit, else 0.
    int GetCollision(bool movables, bool farthest);
    // 0x00417710: the line set, then GetCollision(movables, nearest).
    int GetCollisionAlong(const Mth::Vector* start, const Mth::Vector* end, bool movables);

    // 0x00417120: whether the static object hit is triangle collision (its slot 1) - true when nothing static was
    // hit.
    bool IsHitTriangleCollision();
    // 0x00417140: the owner of the movable object being tested (for the callback; 0 otherwise).
    uint32_t GetTestedOwner();
    // 0x00417210: whether the object hit is still there (its smart pointer not cleared); if it's gone, its id and
    // the hit's object checksum are cleared.
    bool HitObjectAlive();
    // 0x00417240
    Obj::CCompositeObject* GetHitObject();
    // 0x004174A0: the hit face's per-vertex bytes (its geometry's, / 255) blended at the hit point: along the line
    // from the first corner through the point to the opposite edge, the edge's two blended by where it's met. 0.5
    // without a hit. Returned unrounded, as the original leaves it in ST0. (What the bytes are isn't known yet.)
    double GetHitVertexValue() const;
    // 0x00417150: the line drawn in colour (r, g, b) - compiled out of this build.
    void DebugDraw(uint32_t r, uint32_t g, uint32_t b, int frames);

    // 0x0043A930 (operator=): every field of `other` copied (the hit record by CollisionHit::CopyFrom); the object
    // hit's smart pointer let go of first, then pointed at `other`'s (so a feeler assigned to itself loses it).
    // Returns this.
    CFeeler* Assign(const CFeeler& other);
    // 0x004F8200 (the copy constructor): every field of `other` copied (the hit record by CollisionHit::CopyFrom), the
    // object hit's smart pointer made a new one on the same object. Returns this.
    CFeeler* CopyConstruct(const CFeeler& other);

    // 0x00417110 (code Ghidra never made a function of; cdecl): the queries' callback while the feeler has one -
    // the feeler's own, given the feeler (the hit's callback data).
    static void __cdecl CallbackRelay(CollisionHit* hit);

    Mth::Line m_line;                              // +0x00 start, +0x10 end
    FeelerCallback m_callback;                     // +0x20
    CollisionHit m_hit;                            // +0x24
    float m_distance;                              // +0x74 the hit's, copied after a query
    Mth::Vector m_point;                           // +0x78 "
    Mth::Vector m_normal;                          // +0x88 "
    Obj::CSmtPtr<Obj::CCompositeObject> m_object;  // +0x98 the object hit (a movable's owner)
    uint32_t m_objectId;                           // +0xA4 its id
    uint32_t m_unknownA8;                          // +0xA8
    CCollCache* mp_cache;                          // +0xAC null: the current cache
};
static_assert(offsetof(CFeeler, m_hit) == 0x24 && offsetof(CFeeler, m_distance) == 0x74 &&
                  offsetof(CFeeler, m_object) == 0x98 && offsetof(CFeeler, mp_cache) == 0xAC && sizeof(CFeeler) == 0xB0,
              "layout must match the original");

} // namespace Nx
