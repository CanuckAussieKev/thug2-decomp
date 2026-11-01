#pragma once

// The park's rails (0x0058AFC0-0x0058B7D5): the park generator keeps the rails of the park's pieces as rail sets
// ("In set" / "Out set") - lists of rails (rail strings), each a list of rail points. A set has its own item pools for
// its points and rails; while one is made or freed, the set's pool is lent to the class's pool pair (CPoolable: rails
// 0x007CE594, points 0x007CE5A4) and taken back after. The park generator (park_scene.cpp, park_nodes.cpp) uses them
// as RailGroup / Rail / RailPoint (park_scene.h).

#include <cstddef>
#include <cstdint>

#include "engine/core/slab_bank.h"
#include "engine/maths/vec4.h"

namespace park_editing {

struct ParkRailSet;

// A rail point (0x30 bytes).
struct ParkRailPoint {
    uint32_t kind_tag_;        // +0x00: a climbing node's type
    uint32_t surface_type_;    // +0x04: a rail node's TerrainType
    maths::Vec4 at_point_;     // +0x08 (w = 1 once placed)
    maths::Vec4 headings_;     // +0x18: a climbing rail's (y: its heading, radians)
    uint32_t cluster_;         // +0x28 (set when the rail is copied into the park)
    ParkRailPoint* following_; // +0x2C
};
static_assert(offsetof(ParkRailPoint, at_point_) == 0x08 && offsetof(ParkRailPoint, headings_) == 0x18 &&
                  offsetof(ParkRailPoint, following_) == 0x2C && sizeof(ParkRailPoint) == 0x30,
              "layout must match the original");

// A rail (a rail string, 0x20 bytes).
struct ParkRailString {
    static constexpr uint8_t kLoop = 0x1;      // the last point links back to the first: a segment for every point
    static constexpr uint8_t kClimbable = 0x2; // (set by NewRail)
    static constexpr uint32_t kClimbingNode = 0x30C19600; // ClimbingNode (the class)

    uint32_t class_;    // +0x00: RailNode or ClimbingNode
    uint8_t flag_bits_; // +0x04
    uint8_t pad05[3];
    uint32_t label_;             // +0x08: the piece (sector) it belongs to (find_rail looks it up)
    ParkRailPoint* point_chain_; // +0x0C
    ParkRailPoint* tail_node_;   // +0x10
    uint16_t vertex_total_;      // +0x14
    uint8_t pad16[2];
    ParkRailString* following_; // +0x18
    ParkRailSet* point_group_;  // +0x1C: whose pools it uses

    // 0x0058AFC0: `point` on the end.
    void append_spot(ParkRailPoint* point);
    // 0x0058B040: its segments (a loop's points; otherwise one fewer, 0 for a single point).
    [[nodiscard]] int count_segments() const;
    // 0x0058B420: `source`'s points copied in (from its set's pool) - turned by `rot` quarter turns about y, moved by
    // `offset`, each given `cluster`; a climbing rail's headings `angle` plus the turn - then its class, name and
    // flags 0x1 / 0x2.
    void copy_from(const ParkRailString* source, const maths::Vec4* offset, int rot, uint32_t cluster, float angle);
    // 0x0058B620: every point given back to its set's pool; empty.
    void delete_points();
    // 0x0058B220: CPoolable's delete (the pool pair 0x007CE594).
    static void dealloc(void* item);
};
static_assert(offsetof(ParkRailString, point_chain_) == 0x0C && offsetof(ParkRailString, vertex_total_) == 0x14 &&
                  offsetof(ParkRailString, point_group_) == 0x1C && sizeof(ParkRailString) == 0x20,
              "layout must match the original");

// A rail set (0x34 bytes: its rails and the pools they and their points come from).
struct ParkRailSet {
    ParkRailString* rail_groups_{nullptr}; // +0x00
    ParkRailString* tail_node_{}; // +0x04
    uint16_t rail_total_{0};      // +0x08
    uint8_t pad0a_[2]{};
    memory::SlabBank* point_pool_{nullptr}; // +0x0C (0x30-byte items)
    memory::SlabBank* rail_pool_{nullptr};  // +0x10 (0x20-byte items)
    char label_[0x20]{};           // +0x14: "In set" / "Out set"

    // 0x0058AFF0: no rails, no pools (the last rail isn't cleared).
    ParkRailSet();
    // 0x0058B010: `rail` on the end.
    void add_rail(ParkRailString* rail);
    // 0x0058B070: the points of all its rails.
    [[nodiscard]] int rail_point_total() const;
    // 0x0058B090: the `index`th (from 0) of its rails named `name`, or null.
    ParkRailString* find_rail(uint32_t name, int index);
    // 0x0058B0C0: named "In set" (`in`) or "Out set", and its pools made: room for `points` points ("<name>
    // RailPoint") and `rails` rails ("<name> RailString").
    void prepare_all(int points, int rails, bool in);
    // 0x0058B280: its pools deleted (rails', then points').
    void free_pools();
    // 0x0058B2D0 / 0x0058B370: a new point / rail from its pool (the rail's set is this one).
    ParkRailPoint* append_rail_vertex();
    ParkRailString* get_new_rail();
    // 0x0058B5B0: `rail` (null is fine; not in the set) given back to its pool.
    void free_rail(ParkRailString* rail);
    // 0x0058B700: every rail (and its points) given back; empty.
    void get_clear();
};
static_assert(offsetof(ParkRailSet, rail_total_) == 0x08 && offsetof(ParkRailSet, point_pool_) == 0x0C &&
                  offsetof(ParkRailSet, label_) == 0x14 && sizeof(ParkRailSet) == 0x34,
              "layout must match the original");

} // namespace park_editing
