#pragma once

// The park's rails (0x0058AFC0-0x0058B7D5): the park generator keeps the rails of the park's pieces as rail sets
// ("In set" / "Out set") - lists of rail strings, each a list of rail points. A set has its own item pools for its
// points and strings; while one is made or freed, the set's pool is lent to the class's pool pair (CPoolable:
// strings 0x007CE594, points 0x007CE5A4) and taken back after. The park generator (0x00591D90-0x00594F70, not
// rewritten here) calls these.

#include <cstddef>
#include <cstdint>

#include "game/core/pool.h"
#include "game/mth/vector.h"

namespace Ed {

struct CParkRailSet;

// A rail point (0x30 bytes).
struct CParkRailPoint {
    uint32_t m_unknown00;
    uint32_t m_unknown04;
    Mth::Vector m_pos;          // +0x08 (w = 1 once placed)
    uint32_t m_unknown18;
    float m_angle;              // +0x1C: a climbing string's heading (radians)
    uint32_t m_unknown20;
    uint32_t m_unknown24;
    uint32_t m_owner;           // +0x28 (set when the string is copied into the park)
    CParkRailPoint* mp_next;    // +0x2C
};
static_assert(offsetof(CParkRailPoint, m_pos) == 0x08 && offsetof(CParkRailPoint, m_angle) == 0x1C &&
                  offsetof(CParkRailPoint, mp_next) == 0x2C && sizeof(CParkRailPoint) == 0x30,
              "layout must match the original");

// A rail string (0x20 bytes).
struct CParkRailString {
    enum : uint8_t {
        kLoop = 0x1,  // closed: a segment for every point
        kFlag2 = 0x2, // (set by NewString)
    };
    static constexpr uint32_t kClimbingNode = 0x30C19600; // ClimbingNode (the string's type)

    uint32_t m_type;            // +0x00: the node type (ClimbingNode for a climbing string)
    uint8_t m_flags;            // +0x04
    uint8_t pad05[3];
    uint32_t m_id;              // +0x08 (FindString looks it up)
    CParkRailPoint* mp_first;   // +0x0C
    CParkRailPoint* mp_last;    // +0x10
    uint16_t m_count;           // +0x14: points
    uint8_t pad16[2];
    CParkRailString* mp_next;   // +0x18
    CParkRailSet* mp_set;       // +0x1C: whose pools it uses

    // 0x0058AFC0: `point` on the end.
    void AddPoint(CParkRailPoint* point);
    // 0x0058B040: its segments (a loop's points; otherwise one fewer, 0 for a single point).
    int GetSegmentCount() const;
    // 0x0058B420: `source`'s points copied in (from its set's pool) - turned by `rot` quarter turns about y, moved by
    // `offset`, each owned by `owner`; a climbing string's headings `angle` plus the turn - then its type, id and
    // flags 0x1 / 0x2.
    void CopyFrom(const CParkRailString* source, const Mth::Vector* offset, int rot, uint32_t owner, float angle);
    // 0x0058B620: every point given back to its set's pool; empty.
    void DeletePoints();
    // 0x0058B220: CPoolable's delete (the pool pair 0x007CE594).
    static void __cdecl Free(void* item);
};
static_assert(offsetof(CParkRailString, mp_first) == 0x0C && offsetof(CParkRailString, m_count) == 0x14 &&
                  offsetof(CParkRailString, mp_set) == 0x1C && sizeof(CParkRailString) == 0x20,
              "layout must match the original");

// A rail set (its strings and the pools they and their points come from).
struct CParkRailSet {
    CParkRailString* mp_first;  // +0x00
    CParkRailString* mp_last;   // +0x04
    uint16_t m_count;           // +0x08: strings
    uint8_t pad0A[2];
    Mem::Pool* mp_pointPool;    // +0x0C (0x30-byte items)
    Mem::Pool* mp_stringPool;   // +0x10 (0x20-byte items)
    char m_name[8];             // +0x14: "In set" / "Out set"

    // 0x0058AFF0: no strings, no pools (the last string isn't cleared). Returns this.
    CParkRailSet* Construct();
    // 0x0058B010: `string` on the end.
    void AddString(CParkRailString* string);
    // 0x0058B070: the points of all its strings.
    int CountPoints() const;
    // 0x0058B090: the `index`th (from 0) of its strings with id `id`, or null.
    CParkRailString* FindString(uint32_t id, int index);
    // 0x0058B0C0: named "In set" (`in`) or "Out set", and its pools made: room for `points` points ("<name>
    // RailPoint") and `strings` strings ("<name> RailString").
    void CreatePools(int points, int strings, bool in);
    // 0x0058B280: its pools deleted (strings', then points').
    void DestroyPools();
    // 0x0058B2D0 / 0x0058B370: a new point / string from its pool (the string's set is this one).
    CParkRailPoint* NewPoint();
    CParkRailString* NewString();
    // 0x0058B5B0: `string` (null is fine) given back to its pool.
    void DeleteString(CParkRailString* string);
    // 0x0058B700: every string (and its points) given back; empty.
    void DeleteStrings();
};
static_assert(offsetof(CParkRailSet, m_count) == 0x08 && offsetof(CParkRailSet, mp_pointPool) == 0x0C &&
                  offsetof(CParkRailSet, m_name) == 0x14,
              "layout must match the original");

} // namespace Ed
