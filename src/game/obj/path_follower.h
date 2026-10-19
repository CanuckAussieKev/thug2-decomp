#pragma once

// Following a path of linked nodes (vtable 0x00647A84; 0x00469B00-0x0046AA9F): along each straight segment from
// node to node, round the corners on an arc (when the node's corner distance allows), or blending the heading
// where an arc can't be made; the owner's component 0x0A015E17 picks the next link (at random or the first) and
// is told of each node passed.

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Obj {

class CCompositeObject;

class CPathFollower {
public:
    enum EState : int {
        STATE_START = 0,     // the segment set up on the next update
        STATE_STRAIGHT = 1,  // along the segment
        STATE_ARC = 2,       // round the corner on the arc
        STATE_BLEND = 3,     // into the next segment, the heading blended (no arc)
        STATE_DONE = 4,      // at the end of the path
    };
    enum : uint32_t {
        FLAG_CORNER = 1,     // the corner is cut (m_cut short of the node)
        FLAG_LAST_NODE = 2,  // the node has no links: the path ends there
        FLAG_TELL_NODE = 4,  // the node passed is still to be told to the component (half way round)
    };

    // 0x0046A1D0: the constructor (returns this): `owner`, an identity frame, the points at the origin.
    CPathFollower* Construct(CCompositeObject* owner);
    // 0x00469B50 (vtable slot 0): the scalar deleting destructor.
    CPathFollower* Destroy(unsigned int flags);

    // 0x0046A3F0: start at `position`, heading for the node `node` (or past it, to the first one at least a unit
    // away); the heading set.
    void Start(uint32_t node, const Mth::Vector* position);
    // 0x0046A640: `distance` further (times the heading's dot product with `direction`, if given - 1 when that's
    // 0). True at the end of the path.
    bool Update(float distance, const Mth::Vector* direction);
    // 0x0046A1B0: the heading.
    void GetHeading(Mth::Vector* out);

    // 0x00469B00: the target node's position, and whether the path ends there.
    void ReadNodePosition();
    // 0x0046A120: on to the next node (a random link, or the first); false if there's none.
    bool NextNode();
    // 0x00469F60: the position the distance along the segment.
    void UpdatePosition();
    // 0x0046A000: whether (and by how much) the corner at the target node is cut; true unless the node has no
    // link to see the corner by.
    bool CheckCorner();
    // 0x0046A250: the segment's length, the distance along it (with the carry-over), the heading; CheckCorner.
    bool StartSegment();
    // 0x00469B80: the arc round the corner: 0 when the corner isn't cut or the arc would be too tight, 2 when
    // there's no arc to make (no turn, a straight line), 1 when the arc is set (centre, frame, angle, length).
    int ComputeArc();
    // 0x0046A320: `distance` round the arc: the frame turned, the position on the arc.
    void ArcStep(float distance);
    // 0x0046A550: the corner reached: onto the arc, into a blend, or (no arc) straight on to the next segment.
    void StartCorner();

    void** m_vtable;               // +0x00 0x00647A84
    Mth::Vector m_pos;             // +0x04
    CCompositeObject* m_owner;     // +0x14
    Mth::Matrix m_frame;           // +0x18: rows: from the arc's centre, the arc's axis, the heading (+0x38), -
    uint32_t m_lastNode;           // +0x58
    uint32_t m_node;               // +0x5C: the node headed for
    uint32_t m_unknown60;
    Mth::Vector m_from;            // +0x64: the segment's start
    Mth::Vector m_to;              // +0x74: the node's position
    Mth::Vector m_unknown84;       // +0x84 (0, 0, 0, 1)
    float m_cornerDistance;        // +0x94 (none: 0)
    uint32_t m_unknown98;
    float m_cut;                   // +0x9C: how far before the node the corner is left
    float m_length;                // +0xA0: the segment's
    float m_along;                 // +0xA4
    uint32_t m_flags;              // +0xA8
    float m_carry;                 // +0xAC: the distance past the last segment's end
    Mth::Vector m_center;          // +0xB0: the arc's centre (the blend's start)
    Mth::Vector m_corner;          // +0xC0: the point on the next segment as far from the node as the cut
    float m_arcAngle;              // +0xD0
    float m_arcLength;             // +0xD4
    float m_radius;                // +0xD8
    int m_state;                   // +0xDC
    float m_step;                  // +0xE0: the last arc step's angle
};
static_assert(offsetof(CPathFollower, m_owner) == 0x14 && offsetof(CPathFollower, m_frame) == 0x18 &&
                  offsetof(CPathFollower, m_lastNode) == 0x58 && offsetof(CPathFollower, m_from) == 0x64 &&
                  offsetof(CPathFollower, m_cornerDistance) == 0x94 && offsetof(CPathFollower, m_flags) == 0xA8 &&
                  offsetof(CPathFollower, m_center) == 0xB0 && offsetof(CPathFollower, m_state) == 0xDC &&
                  offsetof(CPathFollower, m_step) == 0xE0,
              "layout must match the original");

} // namespace Obj
