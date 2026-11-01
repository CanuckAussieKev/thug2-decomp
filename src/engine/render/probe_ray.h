#pragma once

// The collision feeler (0xB0 bytes): a line asked what it hits in the world - the static collision and, if wanted,
// the movable objects - with the result kept: the hit record, and copies of its distance, point and normal; the
// composite object hit held by a smart pointer. (Other code still calls these through their addresses: park_floor,
// rail_manager, camera_util, editor_camera, avoid, composite, level_editor.)

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/render/coll_line.h"
#include "engine/objects/smtptr.h"

namespace objects {
class GameEntity;
}

namespace render {

class HitMeshCache;
class ProbeRay;

// Called (cdecl) with the feeler for every face its query accepts.
using FeelerCallback = void (*)(ProbeRay* feeler);

class ProbeRay {
public:
    // 0x00417760: no callback, cache or object; faces flagged 0x10 ignored; the hit's normal and point (0, 0, 0, 1).
    // The line is left unset.
    ProbeRay();
    // 0x00417250: the same with the line from `start` to `end`.
    ProbeRay(const maths::Vec4* start, const maths::Vec4* end);

    // 0x0041C350: the destructor's body: the object hit let go (its smart pointer).
    void destruct();

    // 0x004170F0: faces with any of `ignore` set, or without all of `require`, are passed over.
    void set_excluded(uint16_t ignore, uint16_t require);
    // 0x00417190 / 0x004171B0 / 0x004171D0
    void change_origin(const maths::Vec4* start);
    void change_finish(const maths::Vec4* end);
    void change_strand(const maths::Vec4* start, const maths::Vec4* end);

    // 0x004172F0: the line against the static collision (the farthest hit with `farthest`, else the nearest), then
    // with `movables` the movable objects (a nearer / farther one wins); through the feeler's cache, or the current
    // one (0x00691974). The callback, if any, is called for each accepted face. The object hit (a movable's
    // owner) and its id kept; the hit's distance, point and normal copied. 1 if anything was hit, else 0.
    int get_hit_shape(bool movables, bool farthest);
    // 0x00417710: the line set, then get_hit_shape(movables, nearest).
    int get_collision_along(const maths::Vec4* start, const maths::Vec4* end, bool movables);

    // 0x00417120: whether the static object hit is triangle collision (its slot 1) - true when nothing static was
    // hit.
    bool is_hit_triangle_collision();
    // 0x00417140: the owner of the movable object being tested (for the callback; 0 otherwise).
    uint32_t get_tested_owner();
    // 0x00417210: whether the object hit is still there (its smart pointer not cleared); if it's gone, its id and
    // the hit's object checksum are cleared.
    bool hit_object_alive();
    // 0x00417240
    objects::GameEntity* get_hit_object();
    // 0x004174A0: the hit face's per-vertex bytes (its geometry's, / 255) blended at the hit point: along the line
    // from the first corner through the point to the opposite edge, the edge's two blended by where it's met. 0.5
    // without a hit. Returned unrounded, as the original leaves it in ST0. (What the bytes are isn't known yet.)
    [[nodiscard]] double get_hit_vertex_value() const;
    // 0x00417150: the line drawn in colour (r, g, b) - compiled out of this build.
    void debug_draw(uint32_t r, uint32_t g, uint32_t b, int frames);

    // 0x0043A930 (operator=): every field of `other` copied (the hit record by CollisionHit::copy_from); the object
    // hit's smart pointer let go of first, then pointed at `other`'s (so a feeler assigned to itself loses it).
    // Returns this.
    ProbeRay* get_assign(const ProbeRay& other);
    // 0x004F8200 (the copy constructor): every field of `other` copied (the hit record by CollisionHit::copy_from), the
    // object hit's smart pointer made a new one on the same object. Returns this.
    ProbeRay* copy_construct(const ProbeRay& other);

    // 0x00417110 (code Ghidra never made a function of; cdecl): the queries' callback while the feeler has one -
    // the feeler's own, given the feeler (the hit's callback data).
    static void callback_relay(CollisionHit* hit);

    maths::Seg line_{};                              // +0x00 start, +0x10 end
    FeelerCallback callback_;                        // +0x20
    CollisionHit hit_{};                             // +0x24
    float reach_{};                                  // +0x74 the hit's, copied after a query
    maths::Vec4 contact_pos_{};                      // +0x78 "
    maths::Vec4 surface_dir_{};                      // +0x88 "
    objects::SharedRef<objects::GameEntity> entity_; // +0x98 the object hit (a movable's owner)
    uint32_t entity_key_{};                          // +0xA4 its id
    uint32_t unknowna8_{};                           // +0xA8
    HitMeshCache* hit_cache_;                        // +0xAC null: the current cache
};
static_assert(offsetof(ProbeRay, hit_) == 0x24 && offsetof(ProbeRay, reach_) == 0x74 &&
                  offsetof(ProbeRay, entity_) == 0x98 && offsetof(ProbeRay, hit_cache_) == 0xAC &&
                  sizeof(ProbeRay) == 0xB0,
              "layout must match the original");

extern HitMeshCache* g_current_cache;
} // namespace render
