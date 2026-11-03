#pragma once

#include <cstddef>
#include <span>
#include <cstdint>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

// The rail manager (GameObjects::GrindRailHub, 0x0057C170-0x0057EE65): a level's (or a moving object's) grind rails and
// climbing nodes, made from the node array's rail_nodes / ClimbingNodes. The skate module's is at +0x24; an object's
// rails are in its own space (on_object_, frame_xform_). Each node keeps the box of its segment to the next node,
// so skaters find rails by testing boxes. Also the box / interval helpers its code uses.
namespace script {
class Array;
class Struct;
} // namespace script

namespace objects {

struct LadderHit;
struct RailHit;

// GrindPoint::flag_bits_
constexpr uint32_t kRailFlagLipOverride = 0x1;
constexpr uint32_t kRailFlagDefaultLine = 0x2;
constexpr uint32_t kRailFlagActive = 0x4; // CreatedAtStart, or switched on later
constexpr uint32_t kRailFlagNoClimbing = 0x8;
constexpr uint32_t kRailFlagClimbing = 0x10; // a climbing node (Class ClimbingNode)
constexpr uint32_t kRailFlagLadder = 0x20;   // of type Ladder
constexpr uint32_t kRailFlagHangLeft = 0x40;
constexpr uint32_t kRailFlagHangRight = 0x80;

// A rail or climbing node (0x54 bytes).
struct GrindPoint {
    maths::Vec4 lower_limit_; // +0x00 the box of its segment (to the next node; its own point with none)
    maths::Vec4 upper_limit_; // +0x10
    maths::Vec4 at_point_;    // +0x20
    maths::Vec4 climb_;       // +0x30 (climbing nodes)
    GrindPoint* succ_;        // +0x40
    GrindPoint* last_value_;  // +0x44
    uint32_t flag_bits_;      // +0x48 (4 active, 0x10 a climbing node, ...)
    uint32_t label_;          // +0x4C its node's name (checksum)
    uint8_t surface_type_;    // +0x50
    std::array<uint8_t, 3> pad51;

    // 0x0057C200: whether it's the node `name`.
    bool is_named(uint32_t name);
    // 0x0057CC30: which side of the rail has a wall: a feeler 8 long across the segment (horizontally), at a quarter
    // of the way to the next node and 6 below, cast both ways - a wall one way only: -1 or 1, flipped when the
    // segment runs against `direction`; else 0.
    int get_wall_side(const maths::Vec4* direction);
};
static_assert(offsetof(GrindPoint, at_point_) == 0x20 && offsetof(GrindPoint, succ_) == 0x40 &&
                  offsetof(GrindPoint, label_) == 0x4C && sizeof(GrindPoint) == 0x54,
              "layout must match the original");

class GrindRailHub {
public:
    // 0x0057CC10: no nodes, not on an object.
    GrindRailHub();
    // 0x0057C330 (tidy_up): the nodes freed (memory::free_malloc), none, not on an object.
    ~GrindRailHub() { tidy_up(); }
    GrindRailHub(const GrindRailHub&) = delete;
    GrindRailHub& operator=(const GrindRailHub&) = delete;
    void tidy_up();
    // 0x0057C4F0: its rails ride an object: positions are in `matrix`'s space.
    maths::Mat4& change_body_transform(const maths::Mat4& matrix);
    // 0x0057CB20: `in` in the world (through the object's matrix when on an object). Returns `out`.
    maths::Vec4* to_world(maths::Vec4* out, const maths::Vec4* in);
    // 0x0057CB60: `node`'s position in the world. Returns `out`.
    maths::Vec4* get_absolute_pos(maths::Vec4* out, const GrindPoint* node);
    // 0x0057CFB0: a climbing node's orientation (its quaternion's matrix, no translation), in the world. Returns
    // `out`.
    maths::Mat4* get_climb_matrix(maths::Mat4* out, const GrindPoint* node);
    // 0x0057D090: the node being added (adding_) set up from its node `params`: no links, its name, its flags,
    // position, and a climbing node's orientation (from its angles) or a rail's terrain; its box its point.
    void append_vertex(uint32_t name, script::Struct* params);
    // 0x0057D260: overlapping rails removed: a later node whose segment runs along an earlier one's (within 6,
    // along it for more than 6 on two axes or matching on one) is switched off - the lower one of the two.
    // 0x0057D7A0: that, but not in the park editor (its level 0xD443A2BC).
    void remove_overlaps();
    void remove_overlaps_if_not_park();
    // 0x0057D7C0: the node `name` switched on or off (`entireRail`: it and every node after it). 0x0057D840: whether
    // it's on (no such node: false).
    void change_live(uint32_t name, int active, bool entire_rail);
    bool is_operative(uint32_t name);
    // 0x0057D880: a ladder node within `radius` (across) and `height` (up) of `pos`, from the last node back, that
    // `checker` accepts (`up`: 0x004418B0 with the node, the one it leads to and the node's orientation; else
    // 0x00441260 with the other node first and its orientation; both with `extra`) - into `found` (the node climbed
    // from: the node, or the other one).
    bool find_ladder(const maths::Vec4* pos, float radius, float height, bool up, void* checker, LadderHit* result,
                     GrindPoint** found);
    // 0x0057DA20: the ladder nearest the line `start`-`end` (within Climb_Max_Snap of it; TRG_Ladder03's counted 4
    // times as far) that `checker` accepts (0x004413E0: climbing up from the lower node, with the closest point on
    // the ladder and how far along it); its record into `result`, the node climbed from into `found`. Whether one was.
    bool find_ladder_on_line(const maths::Vec4* start, const maths::Vec4* end, void* checker, struct LadderHit* result,
                             GrindPoint** found);
    // 0x0057DDA0: the rail nearest the line `start`-`end` (within `margin`; the part along `direction` counting a
    // third) that's clear (RailClear) and that `checker` accepts (0x00440690: the segment's ends, the closest points
    // on the rail and the line, in the world, how far along); its record (with the node) into `result`. Whether one
    // was.
    bool find_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* direction, void* checker,
                   struct RailHit* result, float margin);
    // 0x0057E170: a rail starting (or ending) within 1 of `from` on every axis, other than `from`, switched on, without
    // any of `exclude_flags`, and clear (RailClear): its node going on from there, or the node before it - into `found`.
    bool find_joining_rail(const GrindPoint* from, uint32_t exclude_flags, GrindPoint** found);
    // 0x0057E960: the rails made from the node array `nodes`: every RailNode / ClimbingNode the skate module doesn't
    // leave out (0x00565BB0) added (append_vertex; the TrickObject ones - or all, with the script integer 0xB675F8F6 -
    // registered as trick objects of their cluster: 0x005896D0 / 0x00589080 on 0x007CE500), then linked by their
    // Links (a node's next: its first link, or a DefaultLine one; the linked node's previous: likewise).
    void add_from_node_array(script::Array* nodes);
    // 0x0057E2D0: the rail to grind for a skater moving from `start` to `end`: the best by distance from the line
    // (within Rail_Max_Snap), worse the less it lines up with the move (flat), doubled when its wall is on the other
    // side than `side` (if given); with `min_alignment` other than 1, only rails lining up at least that well. Rails
    // of one node count by their point. Into `point` (on the rail) and `found`. (Quirk kept: a better rail out of
    // reach still takes the best score and drops what was found.)
    bool find_grind_rail(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point, GrindPoint** found,
                         const GrindPoint* exclude, float min_alignment, int side);
    // 0x0057CBD0: the node named `name` (null: none).
    GrindPoint* find_node(uint32_t name);
    // 0x0057C3F0: the node `name` moved to `pos`: its segment's box again, and (the original's) a check of the node's
    // own previous link at its own index count times (found: the previous node's box - never).
    void move_node(uint32_t name, const maths::Vec4* pos);

    // NOT ORIGINAL - the nodes as a span of their count.
    [[nodiscard]] std::span<GrindPoint> node_run() const { return {nodes_, static_cast<size_t>(num_)}; }

    // The fields other code reads and sets.
    [[nodiscard]] GrindPoint* nodes() const { return nodes_; }
    void set_nodes(GrindPoint* value) { nodes_ = value; }
    [[nodiscard]] script::Array* node_array() const { return node_array_; }
    [[nodiscard]] int num() const { return num_; }
    void set_num(int value) { num_ = value; }
    void set_adding(int value) { adding_ = value; }
    [[nodiscard]] bool on_object() const { return on_object_; }

private:
    maths::Mat4 frame_xform_{}; // +0x00
    GrindPoint* nodes_{nullptr};         // +0x40
    script::Array* node_array_{nullptr}; // +0x44 the node array they were made from
    int num_{0};                         // +0x48
    int adding_{};              // +0x4C the node being added (0x0057D090)
    bool on_object_{false};     // +0x50
    std::array<uint8_t, 3> pad51{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(GrindRailHub, nodes_) == 0x40 && offsetof(GrindRailHub, num_) == 0x48 &&
                          offsetof(GrindRailHub, on_object_) == 0x50,
                      "layout must match the original");
    }
};

// The box helpers (cdecl):
// 0x0057C260: the box of `a` and `b` (min and max per axis; w 1).
void box_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* min, maths::Vec4* max);
// 0x0057C170: the span of four values (the largest minus the smallest).
double span4(float a, float b, float c, float d);
// 0x0057C360: how much the intervals [a, b] and [c, d] overlap: |b - a| + |d - c| (as a float) minus their span.
double get_overlap(float a, float b, float c, float d);
// 0x0057C3A0: whether a, b, c, d step by less than `tolerance` each.
int nearly_equal4(float a, float b, float c, float d, float tolerance);
// 0x0057C600: whether a rail from `a` to `b` can be grabbed: always in the park editor's level 0xD443A2BC; else
// feelers across its middle, below it - see the source.
bool rail_clear(maths::Vec4 a, maths::Vec4 b);
// NOT ORIGINAL: a hook the level editor sets (level_editor_chains.h - rails marked grindable on flat ground): RailClear
// passes a segment it accepts (its ends' places), as the park editor's level passes every rail. Null (the default):
// none - the original's behaviour.
using RailClearOverride = bool (*)(const maths::Vec4* a, const maths::Vec4* b);
void set_rail_clear_override(RailClearOverride override);
// 0x0057C5A0 (stdcall): whether two nodes are within 1 of each other on every axis.
bool nodes_together(const GrindPoint* a, const GrindPoint* b);

// What a ladder test finds (the walk's checks fill it): where on the ladder, the way the skater faces, how far along.
struct LadderHit {
    maths::Vec4 point;  // +0x00
    maths::Vec4 facing; // +0x10
    float along;        // +0x20

};

// 0x0057C500: a copy (the compiler-made assignment). Returns this.
LadderHit* get_assign(LadderHit& hit, const LadderHit* other);
// What a hang test finds: where on the rail, the way the skater faces, how far along, out and up, the hang's
// animation (SWING, or WALL) and the node.
struct RailHit {
    maths::Vec4 point;  // +0x00
    maths::Vec4 facing; // +0x10
    float along;        // +0x20
    float out;          // +0x24
    float up;           // +0x28
    uint32_t anim;      // +0x2C
    GrindPoint* node;   // +0x30

};

// 0x0057C540: a copy (the compiler-made assignment). Returns this.
RailHit* get_assign(RailHit& hit, const RailHit* other);

} // namespace objects
