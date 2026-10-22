#pragma once

// The park's rails (0x0058AFC0-0x0058B7D5): the park generator keeps the rails of the park's pieces as rail sets
// ("In set" / "Out set") - lists of rails (rail strings), each a list of rail points. A set has its own item pools for
// its points and rails; while one is made or freed, the set's pool is lent to the class's pool pair (CPoolable: rails
// 0x007CE594, points 0x007CE5A4) and taken back after. The park generator (park_scene.cpp, park_nodes.cpp) uses them
// as RailSet / Rail / RailPoint (park_scene.h).

#include <cstddef>
#include <cstdint>

#include "game/core/pool.h"
#include "game/mth/vector.h"

namespace Ed {

struct CParkRailSet;

// A rail point (0x30 bytes).
struct CParkRailPoint {
    uint32_t m_type;            // +0x00: a climbing node's type
    uint32_t m_terrain;         // +0x04: a rail node's TerrainType
    Mth::Vector m_pos;          // +0x08 (w = 1 once placed)
    Mth::Vector m_angles;       // +0x18: a climbing rail's (y: its heading, radians)
    uint32_t m_cluster;         // +0x28 (set when the rail is copied into the park)
    CParkRailPoint* mp_next;    // +0x2C
};
static_assert(offsetof(CParkRailPoint, m_pos) == 0x08 && offsetof(CParkRailPoint, m_angles) == 0x18 &&
                  offsetof(CParkRailPoint, mp_next) == 0x2C && sizeof(CParkRailPoint) == 0x30,
              "layout must match the original");

// A rail (a rail string, 0x20 bytes).
struct CParkRailString {
    enum : uint8_t {
        kLoop = 0x1,      // the last point links back to the first: a segment for every point
        kClimbable = 0x2, // (set by NewRail)
    };
    static constexpr uint32_t kClimbingNode = 0x30C19600; // ClimbingNode (the class)

    uint32_t m_class;           // +0x00: RailNode or ClimbingNode
    uint8_t m_flags;            // +0x04
    uint8_t pad05[3];
    uint32_t m_name;            // +0x08: the piece (sector) it belongs to (FindRail looks it up)
    CParkRailPoint* mp_points;  // +0x0C
    CParkRailPoint* mp_last;    // +0x10
    uint16_t m_numPoints;       // +0x14
    uint8_t pad16[2];
    CParkRailString* mp_next;   // +0x18
    CParkRailSet* mp_set;       // +0x1C: whose pools it uses

    // 0x0058AFC0: `point` on the end.
    void AddPoint(CParkRailPoint* point);
    // 0x0058B040: its segments (a loop's points; otherwise one fewer, 0 for a single point).
    int CountSegments() const;
    // 0x0058B420: `source`'s points copied in (from its set's pool) - turned by `rot` quarter turns about y, moved by
    // `offset`, each given `cluster`; a climbing rail's headings `angle` plus the turn - then its class, name and
    // flags 0x1 / 0x2.
    void CopyFrom(const CParkRailString* source, const Mth::Vector* offset, int rot, uint32_t cluster, float angle);
    // 0x0058B620: every point given back to its set's pool; empty.
    void DeletePoints();
    // 0x0058B220: CPoolable's delete (the pool pair 0x007CE594).
    static void __cdecl Free(void* item);
};
static_assert(offsetof(CParkRailString, mp_points) == 0x0C && offsetof(CParkRailString, m_numPoints) == 0x14 &&
                  offsetof(CParkRailString, mp_set) == 0x1C && sizeof(CParkRailString) == 0x20,
              "layout must match the original");

// A rail set (0x34 bytes: its rails and the pools they and their points come from).
struct CParkRailSet {
    CParkRailString* mp_rails;  // +0x00
    CParkRailString* mp_last;   // +0x04
    uint16_t m_numRails;        // +0x08
    uint8_t pad0A[2];
    Mem::Pool* mp_pointPool;    // +0x0C (0x30-byte items)
    Mem::Pool* mp_railPool;     // +0x10 (0x20-byte items)
    char m_name[0x20];          // +0x14: "In set" / "Out set"

    // 0x0058AFF0: no rails, no pools (the last rail isn't cleared). Returns this.
    CParkRailSet* Construct();
    // 0x0058B010: `rail` on the end.
    void AddRail(CParkRailString* rail);
    // 0x0058B070: the points of all its rails.
    int CountPoints() const;
    // 0x0058B090: the `index`th (from 0) of its rails named `name`, or null.
    CParkRailString* FindRail(uint32_t name, int index);
    // 0x0058B0C0: named "In set" (`in`) or "Out set", and its pools made: room for `points` points ("<name>
    // RailPoint") and `rails` rails ("<name> RailString").
    void Init(int points, int rails, bool in);
    // 0x0058B280: its pools deleted (rails', then points').
    void FreePools();
    // 0x0058B2D0 / 0x0058B370: a new point / rail from its pool (the rail's set is this one).
    CParkRailPoint* NewPoint();
    CParkRailString* NewRail();
    // 0x0058B5B0: `rail` (null is fine; not in the set) given back to its pool.
    void FreeRail(CParkRailString* rail);
    // 0x0058B700: every rail (and its points) given back; empty.
    void Clear();
};
static_assert(offsetof(CParkRailSet, m_numRails) == 0x08 && offsetof(CParkRailSet, mp_pointPool) == 0x0C &&
                  offsetof(CParkRailSet, m_name) == 0x14 && sizeof(CParkRailSet) == 0x34,
              "layout must match the original");

} // namespace Ed
