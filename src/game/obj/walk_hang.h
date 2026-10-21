#pragma once

#include <cstdint>

#include "game/mth/vector.h"

namespace Nx {
class CFeeler;
}
namespace Obj {
class CBaseComponent;
class CCompositeObject;
struct CRailNode;
} // namespace Obj

// The walk component's hanging, ladder and rail-climbing code (WalkHangUtil.cpp in the original: 0x0043DA20-
// 0x004425B0). The walk component itself (CWalkComponent, made by 0x0052A5C0; its code 0x00524xxx-0x0052Axxx) isn't
// rewritten yet and its layout is only partly known: these functions reach its fields by offset (walk_hang.cpp names
// the ones they use) and call its other code through its addresses.
//
// Hanging: the skater holds a rail (the rail manager's node pair +0x1764 / +0x1768, the fraction along it +0x18CC)
// and can shimmy along it, pull up onto the ledge or drop; a rail is grabbed from a drop or a jump (FindRail with the
// walk component as the checker). Ladders are rail-manager nodes too (climbing nodes): climbed along +0x18CC, onto
// the top or off the bottom. While an animation plays (+0x1944 set) a callback waits at +0x1948 for its end - one of
// the small functions here.
namespace Walk {

class Component {
public:
    // 0x0043DA70 (cdecl): a walk parameter (the global structure walk_parameters' float `name`).
    static float __cdecl Param(uint32_t name);
    // 0x0043DAA0: the running speed: run_speed to combo_run_speed by the blend at +0x1868 (unrounded, as the
    // original leaves it in ST0).
    double RunSpeed();
    // 0x0043DC00: how far below the rail the skater's origin hangs on a ladder-like rail: (1 - its +0x20) *
    // hang_vert_origin_offset when the node at +0x1778's +0x208 has flag 8, else 0 (unrounded).
    double HangHeightOffset();
    // 0x0043DC60: the hang's critical point above the origin: hang_critical_point_vert_offset - hang_vert_origin_offset
    // * max_cas_scaling (unrounded).
    double CriticalPointUp();
    // The animation-end callbacks: 0x0043DB00 (pulled up onto a ledge: walking again, the feeler's hit given to the
    // triggers and the movable contact), 0x0043DB90 (dropped from a hang: in the air, its facing kept), 0x0043DCF0
    // (dropped onto a rail: hanging - the offset, the rail script, tagging allowed again), 0x00441020 / 0x00441070
    // (off a ladder's top / bottom), 0x00441860 (off a rail onto the ground).
    void EndPullUp();
    void EndDropFromHang();
    void EndDropToHang();
    // 0x0043DDD0: whether a hanging skater's side is clear: `feeler` from the critical point (`hang`: the point and
    // the direction out from the wall, +0x10) moved out along `side` (hang_obstruction_feeler_side), then up by
    // hang_obstruction_feeler_up - and back the other way. True if neither hits.
    bool SideClear(Nx::CFeeler* feeler, const Mth::Vector* hang, const Mth::Vector* side);
    // 0x0043DFC0: hanging on the rail +0x1764 / +0x1768 `distance` from its start (`fromEnd`: its end), facing away
    // from it - the side `direction` says; turning round more than 60 degrees tells +0x1760 (0x005250A0). The hang
    // offset (+0x71C) from the new facing.
    void FaceHangRail(float distance, bool fromEnd, const Mth::Vector* direction);
    // 0x0043E230: the rail joining `start` / `end` (the segment the skater is at, `*t` along it): past the start
    // (t < 0) the segment before it, else the one after `end` - if switched on and grabbable, else a joining rail
    // (FindJoiningRail, without flags 8 / 0x20) whose direction is close enough (a dot product of at least -0.866,
    // or it's refused: "BAD ANGLE ON COINCIDENT RAIL"). Its nodes into `newStart` / `newEnd`, `*t` flipped when it
    // runs the other way. Whether there's one.
    bool NextHangRail(float* t, Obj::CRailNode* start, Obj::CRailNode* end, Obj::CRailNode** newStart,
                      Obj::CRailNode** newEnd);
    // 0x0043E530: a pull-up onto the ledge above the hang: the ground near `target` (0x00528280; failing that, 12 to
    // one side, then 12 to the other - `target` moved), seven feelers round it (none may hit) and one from behind the
    // skater up past the pull_up_obstruction_height (neither may hit). Then the animated move (PullUpFromHang) towards
    // it, ending walking (the ledge's feeler kept for the triggers) or, with no ground found, in the air. Whether it
    // starts.
    bool PullUp(Mth::Vector* target);
    // 0x0043EC40: a drop from walking to hang on a rail below (not while +0x1528's +0x70, not faster than
    // drop_to_hang_speed_factor x the running speed, not while tagging): FindRail along the move from the last
    // position, facing away (the skate module's rails, else each moving rail manager's, in its object's space - riding
    // that object), within Drop_To_Climb_Max_Snap. Then the animated move (DropToHang) onto it.
    bool DropToHang();
    // 0x0043F030: grabbing a rail between `start` and `end` (raised by hang_vert_origin_offset) while falling no faster
    // than 50: FindRail within Climb_Max_Snap, as DropToHang; then hanging at once (the rail script, the skater core's
    // +0x1528 flag +0xA0 set).
    bool GrabRail(Mth::Vector start, Mth::Vector end);
    // 0x0043F3A0: whether shimmying along the hang is blocked: feelers from the hang point `point` (up, out of the wall
    // by `out`) sideways along the move (`move`, `left` or not), at three heights and down at the critical point -
    // near the segment's end (`segment`, `start` / `end`, `along` it) turned onto the next segment first. True if any
    // hits.
    bool ShimmyBlocked(const Mth::Vector* out, float along, const Mth::Vector* point, bool left, const Mth::Vector* move,
                       Mth::Vector segment, Obj::CRailNode* start, Obj::CRailNode* end);
};

// A composite object's movable contact component (the walk component's +0x1784, the skater core's +0x152C): what the
// skater rides on.
class MovableContact {
public:
    // 0x0043EB80: riding `object` from now: an old ride let go (its object kept at +0x6C, the time at +0x70), a new
    // ride tracker (Obj::CRideTracker) made for it.
    void StartRiding(Obj::CCompositeObject* object);
};

// 0x0043D990 (cdecl): out = a + (b - a) * t, each component rounded as the original's x87 code does. Returns out.
Mth::Vector* __cdecl Lerp(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t);
// 0x0043DA20 (thiscall on the skater core physics component): whether its +0x228 angle is within 45 degrees of a
// half turn, with nothing at +0x234 and +0x22C not 0. Returns 1 / 0.
struct SkaterTurn {
    int TurnedRound() const;
};

} // namespace Walk

namespace Obj {
// 0x0043D930: a component of type 0x03AC8065 (WaitAndDie): the base component made (0x28 bytes, cleared), its vtable
// 0x00646898 and its type. (Its other code, 0x0043D850-0x0043D8E0, isn't rewritten here.)
CBaseComponent* __cdecl CreateWaitAndDieComponent();
} // namespace Obj
