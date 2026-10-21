#pragma once

#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Nx {
class CFeeler;
}
namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Obj {
struct CRailNode;
struct Record34;
} // namespace Obj

// The Walk component (type 0x726E85AA, vtable 0x0064C75C, 0x1960 bytes; made by 0x0052A5C0, its member function table
// 0x006847F8 registered by 0x00525E40): the skater on foot - walking, running, jumping, hanging, climbing. Its layout
// is only partly known, so fields are reached by offset (the names in the .cpp files say what each is).
//
// NOTE for the merge: the skater-5 agent's branch declares this class in a file of the same name (its pieces at
// 0x00524E10-0x00529F62, walk_physics.cpp); the skater-6 agent's pieces (0x00529F80-0x0052D6A0, walk_moves.cpp) are
// the block below - keep both blocks.
namespace Obj {

class CWalkComponent : public CBaseComponent {
public:
    // --- walk_moves.cpp (skater-6: 0x00529F80-0x0052D6A0) ---
    // 0x0052A5C0: a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0052A190: the constructor: the base's, its vtable and type, the eight contacts (0xCC each at +0xA0), the
    // collision cache (+0x790), the trigger feeler (+0x17B4), no components yet, no control (0, 0, 0, 1), no extra
    // velocity (0, 0, 0, 1). Returns this.
    CWalkComponent* Construct();
    // 0x0052A290 (vtable slot 0): the scalar deleting destructor (the body is 0x005288B0).
    CWalkComponent* Destroy(unsigned int flags);
    // 0x0052D4B0 (vtable slot 1): one frame: the animation state (+0x78, the previous kept at +0x7C) started afresh,
    // the frame's length, the step smoothing's height taken off, then the state's update (0 UpdateGround, 1
    // UpdateAir, 2 hanging 0x00440BF0, 3 on a ladder 0x004425B0, 4 UpdateAnimWait); unless the walk was halted,
    // the step smoothing eased down and put back, the state's checks (an acid drop after half a second on the
    // ground), the animation state sent to the object as an event, the animation speed and the camera.
    void Update();
    // 0x0052D300 (state 0): riding, the contacts, moved by the velocity and onto the ground; inside a moving object
    // it takes that object's velocity.
    void UpdateGround();
    // 0x0052CEF0 (state 1): moved by the velocity, the extra velocity (+0x189C) and gravity; ledges and ladders
    // grabbed; inside a moving object it is put back and stopped, standing on it.
    void UpdateAir();
    // 0x0052AD10 (state 4: ANIMWAIT): placed along an animation - between two points (+0x18E4, +0x1904) by how far
    // the animation is, turned toward +0x191C, the height +0x9C eased - and carried by what it rides on.
    void UpdateAnimWait();
    // 0x0052A700: the air's move from `from` to `to` against the world. Into a wall (face flag 0x2): 0.1 off it and
    // the rest of the move slid along it (0.05 off anything that meets). Onto anything else: landed - on the ground
    // (its triggers set off, what it stands on told), the velocity kept only along the facing and relative to what
    // it landed on (stopped when heading away, or slow without control), the Land animation.
    void CollideAirMove(const Mth::Vector* from, const Mth::Vector* to);
    // 0x0052B370: onto the ground under it (snap_up_height above to snap_down_height below): big steps told to the
    // object, the step smoothing, the ground's triggers when it changes. None (or a wall): off the edge into the air
    // (SkaterOffEdge) unless a hang, a drop or a ladder takes over.
    void SnapToGround();
    // 0x0052AF40: walking begins (from skating): on the ground (`onGround`) or in the air - then thrown along the
    // turn under way off a vert ramp, the heading from the velocity (or the object's axes), off what it rode.
    void Begin(bool onGround);
    // 0x0052B7D0: a jump (also off a hang: ReleaseHang): up at jump_velocity times how long jump was held (or
    // hang_jump_factor off a hang or ladder); running, pushed along the control's direction; off what it rode, in the
    // air, SkaterJump.
    void Jump();
    // 0x0052BC70: a jump over the barrier ahead (barrier_jump_*): when a contact ahead, faced head-on, has nothing
    // in the way at the highest barrier's height - up fast enough to clear the barrier's top. Whether it jumped.
    bool TryBarrierJump();
    // 0x0052C2C0: the eight contacts (feelers around it, and along the fall in the air) found again; on the ground a
    // barrier jump after barrier_jump_delay, a jump, the push down steep ground; pushed out of what is close, the
    // velocity slid along what it walks into (stopped in a corner or when turned back), the run animation's speed
    // kept up. False when it touches nothing it moves into (or jumped).
    bool UpdateContacts();
    // 0x00529F80: the walk camera's override facing for the animation state (+0x78): along the hang (turned 0.95
    // radians for moving left or right), the facing on a ladder or while waiting for an animation; else none.
    void UpdateCameraOverride();
    // 0x0052A2C0: the animation speed for the speed mode (+0x700): 1 the run (speed over +0x17B0), 2 hanging and
    // 3 ladder moves (over walk_parameters' hang_move_animation_speed / ladder_move_speed - the ladder's also sent
    // over the network when it changed by 0.1 or more).
    void UpdateAnimSpeed();
    // 0x0052A620: carried by what it stands on (its ride tracker at the ride component's +0x18): the move added to
    // the position; a turn also turns the facing (kept level).
    void UpdateRide();
    // 0x0052AB90: off what it stood on: that object's velocity (+0x1888) added to `velocity` (and its y to
    // `vertical`), the position moved on by it for the frame when heading the same way; the ride tracker let go.
    void LeaveRide(Mth::Vector* velocity, float* vertical);
    // 0x0052B2D0: into the air: state 1, the heading (+0x1878) the object's velocity flattened, the ride left,
    // +0x1888 the velocity flattened.
    void GoToAir();
    // 0x0052A180 (Walk_AnimWaitComplete), 0x0052C280 (ReleaseHang), 0x0052C290 (Walk_ForceToAirFromHang: into the air
    // and the object's AIR event): member functions; 1 (true). 0x0052C2B0: GoToAir.
    int AnimWaitComplete(Script::CStruct* params, Script::CScript* script);
    int ReleaseHang(Script::CStruct* params, Script::CScript* script);
    int ForceToAirFromHang(Script::CStruct* params, Script::CScript* script);
    void GoToAirFor(void* unused);

    // --- walk_hang.cpp (objects-5: 0x0043DA70-0x004425B0, WalkHangUtil.cpp in the original) ---
    // Hanging: the skater holds a rail (the rail manager's node pair +0x1764 / +0x1768, the fraction along it
    // +0x18CC) and can shimmy along it, pull up onto the ledge or drop; a rail is grabbed from a drop or a jump
    // (FindRail with the walk component as the checker). Ladders are rail-manager nodes too (climbing nodes): climbed
    // along +0x18CC, onto the top or off the bottom. While an animation plays (+0x1944 set) a callback waits at
    // +0x1948 for its end - one of the small functions here.

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
    // (dropped onto a rail: hanging - the offset, the rail script, tagging allowed again).
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
    bool NextHangRail(float* t, CRailNode* start, CRailNode* end, CRailNode** newStart, CRailNode** newEnd);
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
                       Mth::Vector segment, CRailNode* start, CRailNode* end);
    // 0x0043FC70: the position on the hang: the rail's point along +0x18CC, raised by +0x18D0 and out along the
    // facing by +0x18D4. Riding something that turns (the ride tracker's +0xAC), the facing set across the rail
    // again, flat (the side it was on; its w the side's sign - the original's).
    void PlaceOnHang();
    // 0x0043FE30: whether hanging at `point` on the rail FindRail found (`rail`, its 0x34-byte record: the point
    // set, its facing out from the wall) has room: both sides clear (SideClear with `feeler`), shimmying neither way
    // blocked (from the hang point: lowered by hang_vert_origin_offset, out by hang_horiz_origin_offset), and a
    // feeler from the position to the critical point clear - or, on the ground, up by hop_obstruction_feeler_up
    // and over from there.
    bool HangClear(Nx::CFeeler* feeler, const Mth::Vector* point, Record34* rail);
    // 0x00440130: shimmying `move` along the hang (left for more than 0): unless blocked (ShimmyBlocked; the Hang
    // animation), on along the rail - past an end onto the next rail (NextHangRail; FaceHangRail the rest of the
    // way) or stopped there; HangMoveLeft / HangMoveRight. A feeler from the old critical point to the new one: if
    // it hits, all put back (the Hang animation).
    void Shimmy(float move);
    // 0x00440690: FindRail's checker for a hang: the rail segment `start`-`end` (`onRail` on it closest to the line,
    // `onLine` on the line, `along` it). Not too steep (hang_max_rail_ascent), not above the line when in the air;
    // the facing across it towards the skater's facing (`away`: from it), turned by the rail's one-sided flags
    // (0x40 / 0x80); room to hang (HangClear) facing that way or, if the flags allow, the other. Into `result`: the
    // facing, how far along, out and up from the rail, and the animation (SWING, or WALL when a feeler out from the
    // hang point hits).
    bool CheckRail(const Mth::Vector* start, const Mth::Vector* end, const Mth::Vector* onRail, const Mth::Vector* onLine,
                   float along, Record34* result, bool away);
    // 0x00440BF0 (state 2): hanging, each frame: carried by what it rides on (else the ride let go); with the control
    // pushed forward a pull-up (twice: pull_up_offset_forward ahead, then half that); turned round: let go (in the
    // air, SkaterExitHang); else shimmied by the control's sideways part (hang_move_speed, eased by
    // hang_move_lerp_rate, below hang_move_cutoff the Hang animation).
    void UpdateHang();
};

} // namespace Obj
