#pragma once

// Following a path of linked nodes (0x00469B00-0x0046AA9F; the original's vtable 0x00647A84 held only its destructor): along each straight segment from
// node to node, round the corners on an arc (when the node's corner distance allows), or blending the heading
// where an arc can't be made; the owner's component 0x0A015E17 picks the next link (at random or the first) and
// is told of each node passed.

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace objects {

class GameEntity;

class PathFollower : public memory::HeapObject {
public:
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum FollowPhase : int {
        kStateStart = 0,    // the segment set up on the next update
        kStateStraight = 1, // along the segment
        kStateArc = 2,      // round the corner on the arc
        kStateBlend = 3,    // into the next segment, the heading blended (no arc)
        kStateDone = 4,     // at the end of the path
    };
    static constexpr uint32_t kFlagCorner = 1;   // the corner is cut (cut_ short of the node)
    static constexpr uint32_t kFlagLastNode = 2; // the node has no links: the path ends there
    static constexpr uint32_t kFlagTellNode =
        4; // the node passed is still to be told to the component (half way round)

    // 0x0046A1D0: `owner`, an identity frame, the points at the origin. (0x00469B50, the destructor, does nothing.)
    explicit PathFollower(GameEntity* owner);

    // 0x0046A3F0: start at `position`, heading for the node `node` (or past it, to the first one at least a unit
    // away); the heading set.
    void kickoff(uint32_t node, const maths::Vec4* position);
    // 0x0046A640: `distance` further (times the heading's dot product with `direction`, if given - 1 when that's
    // 0). True at the end of the path.
    bool tick_frame(float distance, const maths::Vec4* direction);
    // 0x0046A1B0: the heading.
    void get_heading(maths::Vec4* out);

    // 0x00469B00: the target node's position, and whether the path ends there.
    void read_node_position();
    // 0x0046A120: on to the next node (a random link, or the first); false if there's none.
    bool get_next_node();
    // 0x00469F60: the position the distance along the segment.
    void refresh_placement();
    // 0x0046A000: whether (and by how much) the corner at the target node is cut; true unless the node has no
    // link to see the corner by.
    bool check_corner();
    // 0x0046A250: the segment's length, the distance along it (with the carry-over), the heading; check_corner.
    bool start_segment();
    // 0x00469B80: the arc round the corner: 0 when the corner isn't cut or the arc would be too tight, 2 when
    // there's no arc to make (no turn, a straight line), 1 when the arc is set (centre, frame, angle, length).
    int compute_arc();
    // 0x0046A320: `distance` round the arc: the frame turned, the position on the arc.
    void arc_step(float distance);
    // 0x0046A550: the corner reached: onto the arc, into a blend, or (no arc) straight on to the next segment.
    void start_corner();

    maths::Vec4 at_point_{}; // +0x04 (offsets: the original's, which has a vtable at +0x00)
    GameEntity* owner_;    // +0x14
    maths::Mat4 tick_{};   // +0x18: rows: from the arc's centre, the arc's axis, the heading (+0x38), -
    uint32_t last_node_{}; // +0x58
    uint32_t list_link_{}; // +0x5C: the node headed for
    uint32_t unknown60_{};
    maths::Vec4 segment_origin_{}; // +0x64: the segment's start
    maths::Vec4 end_point_{};      // +0x74: the node's position
    maths::Vec4 unknown84_{};      // +0x84 (0, 0, 0, 1)
    float corner_distance_{};      // +0x94 (none: 0)
    uint32_t unknown98_{};
    float cut_{};              // +0x9C: how far before the node the corner is left
    float byte_count_{};       // +0xA0: the segment's
    float along_{};            // +0xA4
    uint32_t flag_bits_{};     // +0xA8
    float carry_{};            // +0xAC: the distance past the last segment's end
    maths::Vec4 center_{};     // +0xB0: the arc's centre (the blend's start)
    maths::Vec4 bend_point_{}; // +0xC0: the point on the next segment as far from the node as the cut
    float arc_angle_{};        // +0xD0
    float arc_length_{};       // +0xD4
    float radius_{};           // +0xD8
    int cur_phase_{};          // +0xDC
    float step_{};             // +0xE0: the last arc step's angle
};

} // namespace objects
