#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/shape_tests.h"

// Collision (Nx): collision objects (HitMesh - a level sector's faces, a movable object's), the queries that test
// lines and boxes against them, and the caches that keep the objects near a box for repeated queries.
namespace render {

class HitMesh;

// A collision cache (0xFCC bytes): the collision objects whose boxes touch its box, the static ones (level
// sectors) first, then the movable ones; each with the geometry the queries use. Every cache is registered (8 at
// most) so a dying object can be taken out of all of them.
class HitMeshCache {
public:
    struct TableRow {
        HitMesh* object;
        const maths::BBox* bounds; // (a static object's world box, a movable's own)
    };

    // 0x0040AB50: an empty box, no objects; registered.
    HitMeshCache();
    // 0x0040AB40: unregistered.
    ~HitMeshCache();
    HitMeshCache(const HitMeshCache&) = delete;
    HitMeshCache& operator=(const HitMeshCache&) = delete;
    // 0x0040ABA0: refilled for `box`: the box kept if any collision grid lists cover it, and the objects of those
    // lists and the movable ones (neither flag 2 nor 4) whose boxes touch it grown by 0.5.
    void tick_frame(const maths::BBox& box);
    // 0x0040A910: `object` out of the static ones (the rest shifted down).
    void remove_static(HitMesh* object);
    // 0x0040A990: `object` out of the movable ones (the last entry moved into its place).
    void remove_movable(HitMesh* object);

    // The fields other code reads and sets.
    maths::BBox& box() { return box_; }
    [[nodiscard]] const maths::BBox& box() const { return box_; }
    std::array<TableRow, 500>& entries() { return entries_; }
    [[nodiscard]] const std::array<TableRow, 500>& entries() const { return entries_; }
    void set_num(int value) { num_ = value; }
    [[nodiscard]] int statics() const { return statics_; }
    void set_statics(int value) { statics_ = value; }
    [[nodiscard]] int movables() const { return movables_; }
    void set_movables(int value) { movables_ = value; }

private:
    maths::BBox box_;       // +0x000
    std::array<TableRow, 500> entries_{}; // +0x020
    int num_{};               // +0xFC0
    int statics_{};           // +0xFC4
    int movables_{};          // +0xFC8
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(HitMeshCache) == 0xFCC && offsetof(HitMeshCache, num_) == 0xFC0, "layout must match the original");
    }
};

// The registered caches (0x006918EC, in use: the bits of 0x0069190C). 0x0040AA00 / 0x0040AA40: one registered (no
// room: ignored) / unregistered. 0x0040AA80: `object` out of every cache's movable ones; 0x0040AAC0: out of both.
void register_coll_cache(HitMeshCache* cache);
void unregister_coll_cache(HitMeshCache* cache);
void remove_movable_from_caches(HitMesh* object);
void remove_from_caches(HitMesh* object);

// 0x0040ADD0: a node's collision shape type from its checksum - 1 for a box (0xE460ABDE) and two others
// (0xDD42AA66, 0x2BA71070), 4 for a sector (0x6AADF154), 0 for anything else.
int shape_type_from_checksum(uint32_t shape);

} // namespace render
