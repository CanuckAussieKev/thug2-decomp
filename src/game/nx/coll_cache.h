#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"

// Collision (Nx): collision objects (CCollObj - a level sector's faces, a movable object's), the queries that test
// lines and boxes against them, and the caches that keep the objects near a box for repeated queries.
namespace Nx {

class CCollObj;

// A collision cache (0xFCC bytes): the collision objects whose boxes touch its box, the static ones (level
// sectors) first, then the movable ones; each with the geometry the queries use. Every cache is registered (8 at
// most) so a dying object can be taken out of all of them.
class CCollCache {
public:
    struct Entry {
        CCollObj* object;
        void* geometry;
    };

    // 0x0040AB50: an empty box, no objects; registered (returns this).
    CCollCache* Construct();
    // 0x0040AB40: unregistered.
    void Destruct();
    // 0x0040ABA0: refilled for `box`: the box kept if any collision grid lists cover it, and the objects of those
    // lists and the movable ones (neither flag 2 nor 4) whose boxes touch it grown by 0.5.
    void Update(const Mth::BBox& box);
    // 0x0040A910: `object` out of the static ones (the rest shifted down).
    void RemoveStatic(CCollObj* object);
    // 0x0040A990: `object` out of the movable ones (the last entry moved into its place).
    void RemoveMovable(CCollObj* object);

    Mth::BBox m_box;            // +0x000
    Entry m_entries[500];       // +0x020
    int m_count;                // +0xFC0
    int m_statics;              // +0xFC4
    int m_movables;             // +0xFC8
};
static_assert(sizeof(CCollCache) == 0xFCC && offsetof(CCollCache, m_count) == 0xFC0, "layout must match the original");

// The registered caches (0x006918EC, in use: the bits of 0x0069190C). 0x0040AA00 / 0x0040AA40: one registered (no
// room: ignored) / unregistered. 0x0040AA80: `object` out of every cache's movable ones; 0x0040AAC0: out of both.
void RegisterCollCache(CCollCache* cache);
void UnregisterCollCache(CCollCache* cache);
void RemoveMovableFromCaches(CCollObj* object);
void RemoveFromCaches(CCollObj* object);

// 0x0040ADD0: a node's collision shape type from its checksum - 1 for a box (0xE460ABDE) and two others
// (0xDD42AA66, 0x2BA71070), 4 for a sector (0x6AADF154), 0 for anything else.
int ShapeTypeFromChecksum(uint32_t shape);

} // namespace Nx
