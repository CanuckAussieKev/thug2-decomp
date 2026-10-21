#pragma once

#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

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
};

} // namespace Obj
