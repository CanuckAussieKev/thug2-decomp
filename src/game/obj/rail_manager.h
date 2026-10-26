#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// The rail manager (Obj::CRailManager, 0x0057C170-0x0057EE65): a level's (or a moving object's) grind rails and
// climbing nodes, made from the node array's RailNodes / ClimbingNodes. The skate module's is at +0x24; an object's
// rails are in its own space (m_onObject, m_objectMatrix). Each node keeps the box of its segment to the next node,
// so skaters find rails by testing boxes. Also the box / interval helpers its code uses.
namespace Script {
class CArray;
class CStruct;
} // namespace Script

namespace Obj {

// CRailNode::m_flags
constexpr uint32_t RAIL_FLAG_LIP_OVERRIDE = 0x1;
constexpr uint32_t RAIL_FLAG_DEFAULT_LINE = 0x2;
constexpr uint32_t RAIL_FLAG_ACTIVE = 0x4;       // CreatedAtStart, or switched on later
constexpr uint32_t RAIL_FLAG_NO_CLIMBING = 0x8;
constexpr uint32_t RAIL_FLAG_CLIMBING = 0x10;    // a climbing node (Class ClimbingNode)
constexpr uint32_t RAIL_FLAG_LADDER = 0x20;      // of type Ladder
constexpr uint32_t RAIL_FLAG_HANG_LEFT = 0x40;
constexpr uint32_t RAIL_FLAG_HANG_RIGHT = 0x80;

// A rail or climbing node (0x54 bytes).
struct CRailNode {
    Mth::Vector m_min;       // +0x00 the box of its segment (to the next node; its own point with none)
    Mth::Vector m_max;       // +0x10
    Mth::Vector m_pos;       // +0x20
    Mth::Vector m_climb;     // +0x30 (climbing nodes)
    CRailNode* m_next;       // +0x40
    CRailNode* m_prev;       // +0x44
    uint32_t m_flags;        // +0x48 (4 active, 0x10 a climbing node, ...)
    uint32_t m_name;         // +0x4C its node's name (checksum)
    uint8_t m_terrain;       // +0x50
    uint8_t pad51[3];

    // 0x0057C200: whether it's the node `name`.
    bool IsNamed(uint32_t name);
    // 0x0057CC30: which side of the rail has a wall: a feeler 8 long across the segment (horizontally), at a quarter
    // of the way to the next node and 6 below, cast both ways - a wall one way only: -1 or 1, flipped when the
    // segment runs against `direction`; else 0.
    int GetWallSide(const Mth::Vector* direction);
};
static_assert(offsetof(CRailNode, m_pos) == 0x20 && offsetof(CRailNode, m_next) == 0x40 &&
                  offsetof(CRailNode, m_name) == 0x4C && sizeof(CRailNode) == 0x54,
              "layout must match the original");

class CRailManager {
public:
    // 0x0057CC10 (the constructor): no nodes, not on an object; returns this. 0x0057C330: the nodes freed
    // (Mem::FreeMalloc), none, not on an object.
    CRailManager* Init();
    void Cleanup();
    // 0x0057C4F0: its rails ride an object: positions are in `matrix`'s space.
    Mth::Matrix& SetObjectMatrix(const Mth::Matrix& matrix);
    // 0x0057CB20: `in` in the world (through the object's matrix when on an object). Returns `out`.
    Mth::Vector* ToWorld(Mth::Vector* out, const Mth::Vector* in);
    // 0x0057CB60: `node`'s position in the world. Returns `out`.
    Mth::Vector* GetWorldPos(Mth::Vector* out, const CRailNode* node);
    // 0x0057CFB0: a climbing node's orientation (its quaternion's matrix, no translation), in the world. Returns
    // `out`.
    Mth::Matrix* GetClimbMatrix(Mth::Matrix* out, const CRailNode* node);
    // 0x0057D090: the node being added (m_adding) set up from its node `params`: no links, its name, its flags,
    // position, and a climbing node's orientation (from its angles) or a rail's terrain; its box its point.
    void AddNode(uint32_t name, Script::CStruct* params);
    // 0x0057D260: overlapping rails removed: a later node whose segment runs along an earlier one's (within 6,
    // along it for more than 6 on two axes or matching on one) is switched off - the lower one of the two.
    // 0x0057D7A0: that, but not in the park editor (its level 0xD443A2BC).
    void RemoveOverlaps();
    void RemoveOverlapsIfNotPark();
    // 0x0057D7C0: the node `name` switched on or off (`wholeRail`: it and every node after it). 0x0057D840: whether
    // it's on (no such node: false).
    void SetActive(uint32_t name, int active, bool wholeRail);
    bool IsActive(uint32_t name);
    // 0x0057D880: a ladder node within `radius` (across) and `height` (up) of `pos`, from the last node back, that
    // `checker` accepts (`up`: 0x004418B0 with the node, the one it leads to and the node's orientation; else
    // 0x00441260 with the other node first and its orientation; both with `extra`) - into `found` (the node climbed
    // from: the node, or the other one).
    bool FindLadder(const Mth::Vector* pos, float radius, float height, bool up, void* checker, uint32_t extra,
                    CRailNode** found);
    // 0x0057DA20: the ladder nearest the line `start`-`end` (within Climb_Max_Snap of it; TRG_Ladder03's counted 4
    // times as far) that `checker` accepts (0x004413E0: climbing up from the lower node, with the closest point on
    // the ladder and how far along it); its record into `result`, the node climbed from into `found`. Whether one was.
    bool FindLadderOnLine(const Mth::Vector* start, const Mth::Vector* end, void* checker, struct Record24* result,
                          CRailNode** found);
    // 0x0057DDA0: the rail nearest the line `start`-`end` (within `margin`; the part along `direction` counting a
    // third) that's clear (RailClear) and that `checker` accepts (0x00440690: the segment's ends, the closest points
    // on the rail and the line, in the world, how far along); its record (with the node) into `result`. Whether one
    // was.
    bool FindRail(const Mth::Vector* start, const Mth::Vector* end, const Mth::Vector* direction, void* checker,
                  struct Record34* result, float margin);
    // 0x0057E170: a rail starting (or ending) within 1 of `from` on every axis, other than `from`, switched on, without
    // any of `excludeFlags`, and clear (RailClear): its node going on from there, or the node before it - into `found`.
    bool FindJoiningRail(const CRailNode* from, uint32_t excludeFlags, CRailNode** found);
    // 0x0057E960: the rails made from the node array `nodes`: every RailNode / ClimbingNode the skate module doesn't
    // leave out (0x00565BB0) added (AddNode; the TrickObject ones - or all, with the script integer 0xB675F8F6 -
    // registered as trick objects of their cluster: 0x005896D0 / 0x00589080 on 0x007CE500), then linked by their
    // Links (a node's next: its first link, or a DefaultLine one; the linked node's previous: likewise).
    void AddFromNodeArray(Script::CArray* nodes);
    // 0x0057E2D0: the rail to grind for a skater moving from `start` to `end`: the best by distance from the line
    // (within Rail_Max_Snap), worse the less it lines up with the move (flat), doubled when its wall is on the other
    // side than `side` (if given); with `minAlignment` other than 1, only rails lining up at least that well. Rails
    // of one node count by their point. Into `point` (on the rail) and `found`. (Quirk kept: a better rail out of
    // reach still takes the best score and drops what was found.)
    bool FindGrindRail(const Mth::Vector* start, const Mth::Vector* end, Mth::Vector* point, CRailNode** found,
                       const CRailNode* exclude, float minAlignment, int side);
    // 0x0057CBD0: the node named `name` (null: none).
    CRailNode* FindNode(uint32_t name);
    // 0x0057C3F0: the node `name` moved to `pos`: its segment's box again, and (the original's) a check of the node's
    // own previous link at its own index count times (found: the previous node's box - never).
    void MoveNode(uint32_t name, const Mth::Vector* pos);

    Mth::Matrix m_objectMatrix; // +0x00
    CRailNode* m_nodes;         // +0x40
    Script::CArray* m_nodeArray; // +0x44 the node array they were made from
    int m_count;                // +0x48
    int m_adding;               // +0x4C the node being added (0x0057D090)
    bool m_onObject;            // +0x50
    uint8_t pad51[3];
};
static_assert(offsetof(CRailManager, m_nodes) == 0x40 && offsetof(CRailManager, m_count) == 0x48 &&
                  offsetof(CRailManager, m_onObject) == 0x50,
              "layout must match the original");

// The box helpers (cdecl):
// 0x0057C260: the box of `a` and `b` (min and max per axis; w 1).
void __cdecl BoxOf(const Mth::Vector* a, const Mth::Vector* b, Mth::Vector* min, Mth::Vector* max);
// 0x0057C170: the span of four values (the largest minus the smallest).
double __cdecl Span4(float a, float b, float c, float d);
// 0x0057C360: how much the intervals [a, b] and [c, d] overlap: |b - a| + |d - c| (as a float) minus their span.
double __cdecl Overlap(float a, float b, float c, float d);
// 0x0057C3A0: whether a, b, c, d step by less than `tolerance` each.
int __cdecl NearlyEqual4(float a, float b, float c, float d, float tolerance);
// 0x0057C600: whether a rail from `a` to `b` can be grabbed: always in the park editor's level 0xD443A2BC; else
// feelers across its middle, below it - see the source.
bool __cdecl RailClear(Mth::Vector a, Mth::Vector b);
// NOT ORIGINAL: a hook the level editor sets (level_editor_chains.h - rails marked grindable on flat ground): RailClear
// passes a segment it accepts (its ends' places), as the park editor's level passes every rail. Null (the default):
// none - the original's behaviour.
using RailClearOverride = bool (*)(const Mth::Vector* a, const Mth::Vector* b);
void SetRailClearOverride(RailClearOverride override);
// 0x0057C5A0 (stdcall): whether two nodes are within 1 of each other on every axis.
bool __stdcall NodesTogether(const CRailNode* a, const CRailNode* b);

// 0x0057C210: a box's min and max swapped (the two halves of 0x20 bytes).
struct BoxPair {
    Mth::Vector first, second;
    void Swap();
};
// 0x0057C500 / 0x0057C540: copies of 0x24- and 0x34-byte records (compiler-made assignments). Return this.
struct Record24 {
    uint32_t words[9];
    Record24* Assign(const Record24* other);
};
struct Record34 {
    uint32_t words[13];
    Record34* Assign(const Record34* other);
};

} // namespace Obj
