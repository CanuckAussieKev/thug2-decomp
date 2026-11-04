#pragma once

// The park's rails (0x0058AFC0-0x0058B7D5): the park generator keeps the rails of the park's pieces as rail sets
// ("In set" / "Out set") - lists of rails (rail strings), each a list of rail points. A set has its own item pools for
// its points and rails; while one is made or freed, the set's pool is lent to the class's pool pair (CPoolable: rails
// 0x007CE594, points 0x007CE5A4) and taken back after. The park generator (park_scene.cpp, park_nodes.cpp) uses them
// as RailGroup / Rail / RailPoint (park_scene.h).

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/memory/slab_bank.h"
#include "engine/maths/vec4.h"
#include "engine/core/checksums.h"

namespace park_editing {

constexpr std::size_t kRailLabelSize = 0x20;      // a rail's set label ("In set" / "Out set")

struct ParkRailSet;

constexpr std::size_t kParkRailPointAtPointOffset = 0x08;
constexpr std::size_t kParkRailPointHeadingsOffset = 0x18;
constexpr std::size_t kParkRailPointFollowingOffset = 0x2C;
constexpr std::size_t kParkRailPointByteSize = 0x30;
// A rail point (0x30 bytes).
struct ParkRailPoint {
    uint32_t kind_tag_;        // +0x00: a climbing node's type
    uint32_t surface_type_;    // +0x04: a rail node's TerrainType
    maths::Vec4 at_point_;     // +0x08 (w = 1 once placed)
    maths::Vec4 headings_;     // +0x18: a climbing rail's (y: its heading, radians)
    uint32_t cluster_;         // +0x28 (set when the rail is copied into the park)
    ParkRailPoint* following_; // +0x2C
};
static_assert(offsetof(ParkRailPoint, at_point_) == kParkRailPointAtPointOffset && offsetof(ParkRailPoint, headings_) == kParkRailPointHeadingsOffset &&
                  offsetof(ParkRailPoint, following_) == kParkRailPointFollowingOffset && sizeof(ParkRailPoint) == kParkRailPointByteSize,
              "layout must match the original");

constexpr std::size_t kParkRailStringPointChainOffset = 0x0C;
constexpr std::size_t kParkRailStringVertexTotalOffset = 0x14;
constexpr std::size_t kParkRailStringPointGroupOffset = 0x1C;
constexpr std::size_t kParkRailStringByteSize = 0x20;
// A rail (a rail string, 0x20 bytes).
struct ParkRailString {
    static constexpr uint8_t kLoop = 0x1;      // the last point links back to the first: a segment for every point
    static constexpr uint8_t kClimbable = 0x2; // (set by NewRail)
    static constexpr uint32_t kClimbingNode = 0x30C19600; // ClimbingNode (the class)

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t get_class() const { return class_; }
    void set_class(uint32_t value) { class_ = value; }
    uint8_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint8_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] uint32_t label() const { return label_; }
    void set_label(uint32_t value) { label_ = value; }
    [[nodiscard]] ParkRailPoint* point_chain() const { return point_chain_; }
    void set_point_chain(ParkRailPoint* value) { point_chain_ = value; }
    [[nodiscard]] uint16_t vertex_total() const { return vertex_total_; }
    void set_vertex_total(uint16_t value) { vertex_total_ = value; }
    [[nodiscard]] ParkRailString* following() const { return following_; }
    void set_following(ParkRailString* value) { following_ = value; }
    void set_point_group(ParkRailSet* value) { point_group_ = value; }

private:
    uint32_t class_;    // +0x00: RailNode or ClimbingNode
    uint8_t flag_bits_; // +0x04
    std::array<uint8_t, 3> pad05;
    uint32_t label_;             // +0x08: the piece (sector) it belongs to (find_rail looks it up)
    ParkRailPoint* point_chain_; // +0x0C
    ParkRailPoint* tail_node_;   // +0x10
    uint16_t vertex_total_;      // +0x14
    std::array<uint8_t, 2> pad16;
    ParkRailString* following_; // +0x18
    ParkRailSet* point_group_;  // +0x1C: whose pools it uses

public:

    // 0x0058AFC0: `point` on the end.
    void append_spot(ParkRailPoint* point);
    // 0x0058B040: its segments (a loop's points; otherwise one fewer, 0 for a single point).
    [[nodiscard]] int count_segments() const;
    // How a rail string is copied: its quarter turns, its cluster and its angle.
    struct RailCopy {
        int rot{};
        uint32_t cluster{};
        float angle{};
    };

    // 0x0058B420: `source`'s points copied in (from its set's pool) - turned by `rot` quarter turns about y, moved by
    // `offset`, each given `cluster`; a climbing rail's headings `angle` plus the turn - then its class, name and
    // flags 0x1 / 0x2.
    void copy_from(const ParkRailString* source, const maths::Vec4* offset, RailCopy rail_copy);
    // 0x0058B620: every point given back to its set's pool; empty.
    void delete_points();
    // 0x0058B220: CPoolable's delete (the pool pair 0x007CE594).
    static void dealloc(void* item);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ParkRailString, point_chain_) == kParkRailStringPointChainOffset && offsetof(ParkRailString, vertex_total_) == kParkRailStringVertexTotalOffset &&
                          offsetof(ParkRailString, point_group_) == kParkRailStringPointGroupOffset && sizeof(ParkRailString) == kParkRailStringByteSize,
                      "layout must match the original");
    }
};

constexpr std::size_t kParkRailSetRailTotalOffset = 0x08;
constexpr std::size_t kParkRailSetPointPoolOffset = 0x0C;
constexpr std::size_t kParkRailSetLabelOffset = 0x14;
constexpr std::size_t kParkRailSetByteSize = 0x34;
// A rail set (0x34 bytes: its rails and the pools they and their points come from).
struct ParkRailSet {
    // The fields other code reads and sets.
    [[nodiscard]] ParkRailString* rail_groups() const { return rail_groups_; }
    [[nodiscard]] memory::SlabBank* point_pool() const { return point_pool_; }

private:
    ParkRailString* rail_groups_{nullptr}; // +0x00
    ParkRailString* tail_node_{}; // +0x04
    uint16_t rail_total_{0};      // +0x08
    std::array<uint8_t, 2> pad0a_{};
    memory::SlabBank* point_pool_{nullptr}; // +0x0C (0x30-byte items)
    memory::SlabBank* rail_pool_{nullptr};  // +0x10 (0x20-byte items)
    std::array<char, kRailLabelSize> label_{};           // +0x14: "In set" / "Out set"

public:

    // 0x0058AFF0: no rails, no pools (the last rail isn't cleared).
    ParkRailSet();
    // 0x0058B010: `rail` on the end.
    void add_rail(ParkRailString* string);
    // 0x0058B070: the points of all its rails.
    [[nodiscard]] int rail_point_total() const;
    // 0x0058B090: the `index`th (from 0) of its rails named `name`, or null.
    ParkRailString* find_rail(checksums::Name rail_name, int index);
    // How many rail points and rail strings the set holds.
    struct RailCounts {
        int points{};
        int strings{};
    };

    // 0x0058B0C0: named "In set" (`in`) or "Out set", and its pools made: room for `points` points ("<name>
    // RailPoint") and `rails` rails ("<name> RailString").
    void prepare_all(RailCounts rail_counts, bool in);
    // 0x0058B280: its pools deleted (rails', then points').
    void free_pools();
    // 0x0058B2D0 / 0x0058B370: a new point / rail from its pool (the rail's set is this one).
    ParkRailPoint* append_rail_vertex();
    ParkRailString* get_new_rail();
    // 0x0058B5B0: `rail` (null is fine; not in the set) given back to its pool.
    void free_rail(ParkRailString* string);
    // 0x0058B700: every rail (and its points) given back; empty.
    void get_clear();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ParkRailSet, rail_total_) == kParkRailSetRailTotalOffset && offsetof(ParkRailSet, point_pool_) == kParkRailSetPointPoolOffset &&
                          offsetof(ParkRailSet, label_) == kParkRailSetLabelOffset && sizeof(ParkRailSet) == kParkRailSetByteSize,
                      "layout must match the original");
    }
};

} // namespace park_editing
