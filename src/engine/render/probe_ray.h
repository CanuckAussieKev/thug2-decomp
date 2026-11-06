#pragma once

// The collision feeler (0xB0 bytes): a line asked what it hits in the world - the static collision and, if wanted,
// the movable objects - with the result kept: the hit record, and copies of its distance, point and normal; the
// composite object hit held by a smart pointer. (Other code still calls these through their addresses: park_floor,
// rail_manager, camera_util, editor_camera, avoid, composite, level_editor.)

#include <cstddef>
#include <cstdint>

#include "base/core/layout_32bit.h"
#include "base/maths/vec4.h"
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

constexpr std::size_t kProbeRayHitOffset = 0x24;
constexpr std::size_t kProbeRayReachOffset = 0x74;
constexpr std::size_t kProbeRayEntityOffset = 0x98;
constexpr std::size_t kProbeRayHitCacheOffset = 0xAC;
constexpr std::size_t kProbeRayBytes = 0xB0;

class ProbeRay {
public:
    // 0x00417760: no callback, cache or object; faces flagged 0x10 ignored; the hit's normal and point (0, 0, 0, 1).
    // The line is left unset.
    ProbeRay();
    // 0x00417250: the same with the line from `start` to `end`.
    ProbeRay(const maths::Vec4* start, const maths::Vec4* end);

    // 0x0041C350: the destructor's body: the object hit let go (its smart pointer).
    void destruct();

    // The face flags a probe ignores and those it requires.
    struct FaceMasks {
        FaceFlag ignore{};
        FaceFlag require{};
    };

    // 0x004170F0: faces with any of `ignore` set, or without all of `require`, are passed over.
    void set_excluded(FaceMasks face_masks);
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
    [[nodiscard]] bool is_hit_triangle_collision() const;
    // 0x00417140: the owner of the movable object being tested (for the callback; 0 otherwise).
    [[nodiscard]] objects::GameEntity* get_tested_owner() const;
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
    void debug_draw(uint32_t red, uint32_t green, uint32_t b, int frames);

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

    // The fields other code reads and sets.
    maths::Seg& line() { return line_; }
    [[nodiscard]] const maths::Seg& line() const { return line_; }
    FeelerCallback& callback() { return callback_; }
    [[nodiscard]] const FeelerCallback& callback() const { return callback_; }
    void set_callback(const FeelerCallback& value) { callback_ = value; }
    CollisionHit& hit() { return hit_; }
    [[nodiscard]] const CollisionHit& hit() const { return hit_; }
    [[nodiscard]] float reach() const { return reach_; }
    maths::Vec4& contact_pos() { return contact_pos_; }
    [[nodiscard]] const maths::Vec4& contact_pos() const { return contact_pos_; }
    maths::Vec4& surface_dir() { return surface_dir_; }
    [[nodiscard]] const maths::Vec4& surface_dir() const { return surface_dir_; }
    objects::SharedRef<objects::GameEntity>& entity() { return entity_; }
    [[nodiscard]] const objects::SharedRef<objects::GameEntity>& entity() const { return entity_; }
    [[nodiscard]] uint32_t entity_key() const { return entity_key_; }
    [[nodiscard]] void* user_data() const { return user_data_; }
    void set_user_data(void* value) { user_data_ = value; }
    void set_hit_cache(HitMeshCache* value) { hit_cache_ = value; }

private:
    maths::Seg line_{};                              // +0x00 start, +0x10 end
    FeelerCallback callback_;                        // +0x20
    CollisionHit hit_{};                             // +0x24
    float reach_{};                                  // +0x74 the hit's, copied after a query
    maths::Vec4 contact_pos_{};                      // +0x78 "
    maths::Vec4 surface_dir_{};                      // +0x88 "
    objects::SharedRef<objects::GameEntity> entity_; // +0x98 the object hit (a movable's owner)
    uint32_t entity_key_{};                          // +0xA4 its id
    void* user_data_{};                              // +0xA8 (its user's: a trigger component)
    HitMeshCache* hit_cache_;                        // +0xAC null: the current cache
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(ProbeRay, hit_) == kProbeRayHitOffset && offsetof(ProbeRay, reach_) == kProbeRayReachOffset &&
                          offsetof(ProbeRay, entity_) == kProbeRayEntityOffset && offsetof(ProbeRay, hit_cache_) == kProbeRayHitCacheOffset &&
                          sizeof(ProbeRay) == kProbeRayBytes),
                      "layout must match the original");
    }
};

// NOT ORIGINAL - the cache queries use when the feeler has none; the rider sets it around its feelers.
void set_current_cache(HitMeshCache* cache);

// NOT ORIGINAL - who is told after each feeler query (ProbeRay::get_hit_shape): the feeler, its result and the return
// address into the code that asked. [features] debug_collision's log installs itself (game/rider/debug_collision.h,
// from app/main.cpp before any of the original's code runs). Not set: no one.
struct FeelerResult {
    int hit{};
    uintptr_t caller{}; // (a code address: pointer-sized)
};
using FeelerQueryObserver = void (*)(const void* feeler, FeelerResult feeler_result);
void set_feeler_query_observer(FeelerQueryObserver observer);
} // namespace render
