#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"

namespace objects {
class GameEntity;
}
namespace render {

class HitMesh;
struct WorldScene;
struct CollisionHit;

// Called (cdecl) with the record each time a line test accepts a hit.
using CollisionHitCallback = void (*)(CollisionHit* hit);

// What a line test fills in (0x50 bytes; the caller sets the flags, the distance to beat and the callback): the
// nearest hit so far. (Our name: the original's isn't known.)
struct CollisionHit {
    maths::Vec4 normal;      // +0x00 the face's normal (world space)
    maths::Vec4 point;       // +0x10 where the line hits (w = 1)
    uint32_t face;           // +0x20 the face's index in its geometry
    float distance;          // +0x24 along the line, 0..1 (the one to beat)
    FaceFlag ignore_flags_;  // +0x28 faces with any of these flags are passed over
    FaceFlag require_flags_; // +0x2A ... and faces without all of these
    FaceFlag face_flags_;    // +0x2C the hit face's
    uint8_t terrain;         // +0x2E the hit face's (low byte)
    uint8_t pad2f_;
    HitMesh* object;               // +0x30 what was hit
    uint32_t object_checksum_;     // +0x34 its checksum
    uint32_t unknown38;            // +0x38
    CollisionHitCallback callback; // +0x3C
    void* callback_data_;          // +0x40 (for the callback: the feeler testing)
    objects::GameEntity* owner;    // +0x44 while a movable object is tested: its owner (slot 10), else null
    bool hit;                      // +0x48 a face was accepted
    uint8_t face_flag40_;          // +0x49 the hit face's flag 0x40 (0 or 0x40)
    std::array<uint8_t, 2> pad4a_;
    WorldScene* scene; // +0x4C the scene whose grid held the static object hit (collide_with_statics)

};

// 0x004F8160: every field copied (not the padding). Returns this.
CollisionHit* copy_from(CollisionHit& hit, const CollisionHit& other);
constexpr std::size_t kCollisionHitBytes = 0x50;
constexpr std::size_t kCollisionHitHitOffset = 0x48;

static_assert(sizeof(CollisionHit) == kCollisionHitBytes && offsetof(CollisionHit, hit) == kCollisionHitHitOffset, "layout must match the original");

// A face the line crosses, as a line test hands it on: its normal, where it's crossed, and its index.
struct FaceHit {
    maths::Vec4 normal;
    maths::Vec4 point;
    uint32_t face;
};
constexpr std::size_t kFaceHitBytes = 0x24;

static_assert(sizeof(FaceHit) == kFaceHitBytes, "layout must match the original");

// 0x004131F0 (cdecl): a face the line hits at `distance` (0..1 along it) offered to `hit`. It's taken (the
// record filled - the point recomputed from the line - and the callback called) unless the face's flags include
// one of the ignored ones or lack a required one; but unless it's nearer than the record's distance (or not
// nearer, while 0x00691910 asks for the farthest), the record is put back as it was afterwards and false
// returned - the callback has still run.
bool report_hit(const maths::Seg& line, HitMesh* object, const FaceHit& face, float distance, CollisionHit* hit);

} // namespace render
