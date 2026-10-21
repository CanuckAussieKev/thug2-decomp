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

// The Walk component (type 0x726E85AA, vtable 0x0064C75C; made by 0x0052A5C0, its member function table 0x006847F8
// registered by 0x00525E40): the skater on foot - walking, running, jumping, hanging, climbing. Only the pieces at
// 0x00524E10-0x00529F62 are here (walk_physics.cpp); its layout is only partly known, so fields are reached by
// offset (named in walk_physics.cpp). Its tuning is the global structure walk_parameters.
//
// Vtable: 0 the scalar deleting destructor (0x0052A290), 1 Update (0x0052D4B0), 2 InitFromStructure, 3 Finalize.
namespace Obj {

class CWalkComponent : public CBaseComponent {
public:
    // Its states (+0x18; the names are the table at 0x0064C618): 0 WALKING_GROUND, 1 WALKING_AIR, 2 / 3 / 4 the
    // hanging and climbing ones.

    // --- life (walk_physics.cpp) ---
    // 0x00525E40: its member function table (0x006847F8) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x005288B0: the destructor body: its smart pointer (+0x184C) let go, its collision cache (+0x790) and its
    // eight contacts (+0xA0, 0xCC each, the last first) destroyed, then the base's.
    void DestroyBody();
    // 0x00526060 (slot 2): its camera from camera= (an object id: the object and its WalkCamera component).
    void InitFromStructure(Script::CStruct* params);
    // 0x00525FB0 (slot 3): the object's Input, animblender, Model, Trigger, SkaterPhysicsControl, MovableContact,
    // SkaterState, SkaterCorePhysics and TaggingControl components.
    void Finalize();
    // 0x00525E60: its camera object and that object's WalkCamera component.
    void SetCamera(CCompositeObject* camera);
    // 0x005260E0: the state changed (the previous one kept, the time it changed): what leaving the air, landing,
    // hanging or climbing resets; entering a hang from the air debounces the pad's hang controls
    // (hang_control_debounce_time).
    void SetState(int state);
    // 0x005260C0: the model's offset (+0x1F8) back to none.
    void ClearModelOffset();

    // --- small queries ---
    // 0x00524E10: the d-pad steers it (the left stick centred - the input's +0x228 / +0x22C exactly 0 - and the
    // flag +0x98 set).
    bool UsingDpad();
    // 0x00524E60: the camera's offset: its facing (+0x48) x -18 in states 2-4, and in the air for 150 ms after
    // leaving states 2 / 3; else (0, 0, 0, 1). Returns `out`.
    Mth::Vector* GetCameraOffset(Mth::Vector* out);
    // 0x00528560: its facing. Returns `out`.
    Mth::Vector* GetFacing(Mth::Vector* out);
    // 0x00528220: the flat direction `index` sevenths of a turn from its facing. Returns `out`.
    Mth::Vector* GetTurnedFacing(Mth::Vector* out, int index);
    // 0x00528370: the run adjustment (+0x1868): 1 while the score holds a combo, else down at run_adjust_rate a
    // second to 0.
    void UpdateRunAdjustment();
    // 0x00528400: on the ground and 100 ms after landing: the speed kept from rising past the last frame's
    // (+0x704 / +0x708); a stopped run becomes `stopAnim` if that's Skid, else Stand.
    void LimitSpeed(uint32_t stopAnim);
    // 0x005284C0 (`this` isn't used): gravity (walk_parameters); with the moon cheat on, half of it (the cheat
    // told, the global 0x007CE4FD set).
    static double __cdecl GetGravity();
    // 0x00528280 (stdcall; `this` isn't used): the ground below `position`: `feeler` from feeler_height x 2 above
    // to stand_pos_search_depth below; its point (true) or `position` (false) into `out`.
    static bool __stdcall FindStandPosition(const Mth::Vector* position, Mth::Vector* out, Nx::CFeeler* feeler);

    // --- member functions (by their records) ---
    bool Walk_Ground(Script::CStruct* params, Script::CScript* script);              // 0x00525E90
    bool Walk_ScaleAnimSpeed(Script::CStruct* params, Script::CScript* script);      // 0x00525EA0
    bool ReleaseHang(Script::CStruct* params, Script::CScript* script);              // 0x00525F60
    bool Walk_GetStateTime(Script::CStruct* params, Script::CScript* script);        // 0x00528580
    bool Walk_GetSpeedScale(Script::CStruct* params, Script::CScript* script);       // 0x005285C0
    bool Walk_GetHangInitAnimType(Script::CStruct* params, Script::CScript* script); // 0x005286A0
    bool Walk_GetStateDuration(Script::CStruct* params, Script::CScript* script);    // 0x005286C0
    bool Walk_GetPreviousState(Script::CStruct* params, Script::CScript* script);    // 0x00528710
    bool Walk_GetState(Script::CStruct* params, Script::CScript* script);            // 0x00528740
    bool Walk_GetHangAngle(Script::CStruct* params, Script::CScript* script);        // 0x00528770
    bool Walk_SetForwardControlLock(Script::CStruct* params, Script::CScript* script); // 0x00528860
};

// One of the Walk component's eight contacts (+0xA0, 0xCC bytes): a feeler at +0x1C. 0x00528880: constructed (its
// feeler's). Returns this. 0x00528890: the destructor (the feeler's object let go).
struct WalkContact {
    WalkContact* Construct();
    void Destroy();
};

} // namespace Obj
