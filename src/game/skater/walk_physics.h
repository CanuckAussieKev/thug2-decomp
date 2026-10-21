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
