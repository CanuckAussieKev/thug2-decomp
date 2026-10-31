// Generated once from the reference build's tables; maintained by hand since.
// The code half of the script member-function tables (33 tables, 483 functions): what the original's
// static initialisers write into them before WinMain - each record's function (a member function pointer: the code,
// three zero words) and the terminator's {0, 0, 0, -1}. They run at start-up in the original's order
// (game/sys/startup_init.cpp).

#include <cstdint>

#include "game/ed/goal_editor.h"
#include "game/ed/park_editor.h"
#include "game/front/level_editor_actions.h"
#include "game/gfx/anim_blend.h"
#include "game/obj/anim_blender.h"
#include "game/obj/avoid.h"
#include "game/obj/car_physics.h"
#include "game/obj/color_cycle.h"
#include "game/obj/fam.h"
#include "game/obj/flexible_particle.h"
#include "game/obj/input_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/obj/object_proximity.h"
#include "game/obj/pager_message.h"
#include "game/obj/particle_component.h"
#include "game/obj/ped_logic.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/sound_component.h"
#include "game/obj/stream_component.h"
#include "game/obj/suspend_component.h"
#include "game/obj/vehicle_component.h"
#include "game/obj/vibration_component.h"
#include "game/skater/balance_trick.h"
#include "game/skater/core_physics.h"
#include "game/skater/physics_control.h"
#include "game/skater/run_timer.h"
#include "game/skater/skater_state.h"
#include "game/skater/stats_manager.h"
#include "game/skater/trick_component.h"
#include "game/skater/vehicle_camera.h"
#include "game/skater/vehicle_sound.h"
#include "game/skater/walk_physics.h"
#include "game/script/names.h"
#include "game/core/code_address.h"

namespace Ed {
extern uint32_t g_goalEditorMemberFunctions[216];
} // namespace Ed

namespace Obj {
extern uint32_t g_motionPathMemberFunctions[294];
extern uint32_t g_animBlenderMemberFunctions[180];
extern uint32_t g_objectProximityMemberFunctions[42];
extern uint32_t g_soundComponentMemberFunctions[12];
extern uint32_t g_trickComponentMemberFunctions[24];
extern uint32_t g_pagerMessageMemberFunctions[36];
extern uint32_t g_balanceTrickMemberFunctions[12];
extern uint32_t g_compositeObjectMemberFunctions[186];
extern uint32_t g_objectMemberFunctions[102];
extern uint32_t g_famMemberFunctions[18];
extern uint32_t g_pedLogicMemberFunctions[174];
extern uint32_t g_avoidMemberFunctions[54];
extern uint32_t g_flexibleParticleMemberFunctions[156];
extern uint32_t g_particleComponentMemberFunctions[126];
extern uint32_t g_rigidBodyMemberFunctions[78];
extern uint32_t g_vehicleWheelMemberFunctions[60];
extern uint32_t g_vehicleCameraMemberFunctions[12];
extern uint32_t g_statsManagerMemberFunctions[60];
extern uint32_t g_colorCycleMemberFunctions[30];
extern uint32_t g_vehicleSoundMemberFunctions[24];
extern uint32_t g_carPhysicsMemberFunctions[18];
extern uint32_t g_physicsControlMemberFunctions[66];
extern uint32_t g_walkMemberFunctions[114];
extern uint32_t g_runTimerMemberFunctions[42];
extern uint32_t g_loopingSoundMemberFunctions[6];
extern uint32_t g_skaterStateMemberFunctions[114];
extern uint32_t g_suspendComponentMemberFunctions[12];
extern uint32_t g_inputComponentMemberFunctions[90];
extern uint32_t g_skeletonComponentMemberFunctions[36];
extern uint32_t g_streamComponentMemberFunctions[30];
extern uint32_t g_vibrationComponentMemberFunctions[12];
} // namespace Obj

namespace Skater {
extern uint32_t g_coreMemberFunctions[660];
} // namespace Skater

namespace Script {

namespace {

// 0x00640E30: Obj::g_motionPathMemberFunctions (0x00681EB8), 48 functions.
void FillMotionPathMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CMotionPathMembers::IsMovingFromScript),
        CodeAddress(&Obj::CMotionPathMembers::IsRotatingFromScript),
        CodeAddress(&Obj::CMotionComponent::Obj_StorePos),
        CodeAddress(&Obj::CMotionComponent::Obj_StoreNode),
        CodeAddress(&Obj::CMotionPathMembers::StopMoving),
        CodeAddress(&Obj::CMotionPathMembers::StopRotating),
        CodeAddress(&Obj::CMotionPathMembers::SetPathVelocity),
        CodeAddress(&Obj::CMotionPathMembers::SetPathMinStopVel),
        CodeAddress(&Obj::CMotionPathMembers::SetPathAcceleration),
        CodeAddress(&Obj::CMotionPathMembers::SetPathDeceleration),
        CodeAddress(&Obj::CMotionComponent::Obj_Rotate),
        CodeAddress(&Obj::CMotionComponent::Obj_RotX),
        CodeAddress(&Obj::CMotionComponent::Obj_RotY),
        CodeAddress(&Obj::CMotionComponent::Obj_RotZ),
        CodeAddress(&Obj::CMotionPathMembers::PathHeading),
        CodeAddress(&Obj::CMotionComponent::Obj_SetConstantHeight),
        CodeAddress(&Obj::CMotionComponent::Obj_GetNextObjOnPath),
        CodeAddress(&Obj::CMotionComponent::Obj_GetRandomLink),
        CodeAddress(&Obj::CMotionPathMembers::RandomPathMode),
        CodeAddress(&Obj::CMotionComponent::Obj_SetPathTurnDist),
        CodeAddress(&Obj::CMotionComponent::Obj_FollowPath),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtObject),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtNode),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtNodeLinked),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtNodeStored),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtPos),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtPosStored),
        CodeAddress(&Obj::CMotionComponent::Obj_LookAtRelPos),
        CodeAddress(&Obj::CMotionPathMembers::StopAlongPath),
        CodeAddress(&Obj::CMotionPathMembers::StartAlongPath),
        CodeAddress(&Obj::CMotionComponent::Obj_FollowPathLinked),
        CodeAddress(&Obj::CMotionComponent::Obj_FollowPathStored),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveToPosStored),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveForward),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveLeft),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveToPos),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveToRelPos),
        CodeAddress(&Obj::CMotionComponent::Obj_MoveToLink),
        CodeAddress(&Obj::CMotionComponent::Obj_StickToGround),
        CodeAddress(&Obj::CMotionPathMembers::WaitJumpFinished),
        CodeAddress(&Obj::CMotionPathMembers::WaitRotate),
        CodeAddress(&Obj::CMotionPathMembers::WaitMove),
        CodeAddress(&Obj::CMotionPathMembers::WaitStop),
        CodeAddress(&Obj::CMotionComponent::Obj_SkipToRestart),
        CodeAddress(&Obj::CMotionComponent::Obj_Hover),
        CodeAddress(&Obj::CMotionComponent::Obj_GetSpeed),
        CodeAddress(&Obj::CMotionComponent::Obj_Jump),
        CodeAddress(&Obj::CMotionComponent::Obj_FollowLeader),
    };
    FillMemberFunctionCode(Obj::g_motionPathMemberFunctions, code, 48);
}

// 0x006413A0: Obj::g_animBlenderMemberFunctions (0x00682350), 29 functions.
void FillAnimBlenderMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CAnimBlenderComponent::AddChannelFromScript),
        CodeAddress(&Obj::CAnimBlenderComponent::WaitOnChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::RemoveChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::BlendChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::ChannelExists),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&Obj::CAnimBlenderComponent::PassToChannel),
    };
    FillMemberFunctionCode(Obj::g_animBlenderMemberFunctions, code, 29);
}

// 0x00641700: Obj::g_objectProximityMemberFunctions (0x00682620), 6 functions.
void FillObjectProximityMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CObjectProximityComponent::Obj_SetInnerRadius),
        CodeAddress(&Obj::CObjectProximityComponent::Obj_SetOuterRadius),
        CodeAddress(&Obj::CObjectProximityComponent::Obj_SetInnerAvoidRadius),
        CodeAddress(&Obj::CObjectProximityComponent::Obj_SetOuterAvoidRadius),
        CodeAddress(&Obj::CObjectProximityComponent::Obj_SetAttentionRadius),
        CodeAddress(&Obj::CObjectProximityComponent::Obj_ObjectInAttentionRadius),
    };
    FillMemberFunctionCode(Obj::g_objectProximityMemberFunctions, code, 6);
}

// 0x006417D0: Skater::g_coreMemberFunctions (0x006826C8), 109 functions.
void FillCoreMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Skater::CorePhysicsComponent::Jump),
        CodeAddress(&Skater::CorePhysicsComponent::Flipped),
        CodeAddress(&Skater::CorePhysicsComponent::Switched),
        CodeAddress(&Skater::CorePhysicsComponent::Crouched),
        CodeAddress(&Skater::CorePhysicsComponent::FirstTimeOnThisRail),
        CodeAddress(&Skater::CorePhysicsComponent::StartSkitch),
        CodeAddress(&Skater::CorePhysicsComponent::Skitching),
        CodeAddress(&Skater::CorePhysicsComponent::StopSkitch),
        CodeAddress(&Skater::CorePhysicsComponent::CancelWallpush),
        CodeAddress(&Skater::CorePhysicsComponent::AirTimeLessThan),
        CodeAddress(&Skater::CorePhysicsComponent::AirTimeGreaterThan),
        CodeAddress(&Skater::CorePhysicsComponent::GetAirtime),
        CodeAddress(&Skater::CorePhysicsComponent::GetAirTimeLeft),
        CodeAddress(&Skater::CorePhysicsComponent::Braking),
        CodeAddress(&Skater::CorePhysicsComponent::CanBrakeOff),
        CodeAddress(&Skater::CorePhysicsComponent::CanBrakeOn),
        CodeAddress(&Skater::CorePhysicsComponent::CanKick),
        CodeAddress(&Skater::CorePhysicsComponent::CanKickOn),
        CodeAddress(&Skater::CorePhysicsComponent::CanKickOff),
        CodeAddress(&Skater::CorePhysicsComponent::ForceAutoKickOn),
        CodeAddress(&Skater::CorePhysicsComponent::ForceAutoKickOff),
        CodeAddress(&Skater::CorePhysicsComponent::AutoKickIsOff),
        CodeAddress(&Skater::CorePhysicsComponent::RestoreAutoKick),
        CodeAddress(&Skater::CorePhysicsComponent::SkaterVehicleOn),
        CodeAddress(&Skater::CorePhysicsComponent::SkaterVehicleOff),
        CodeAddress(&Skater::CorePhysicsComponent::IsSkaterOnVehicle),
        CodeAddress(&Skater::CorePhysicsComponent::SetSkaterVehicle),
        CodeAddress(&Skater::CorePhysicsComponent::DoCarPlantBoost),
        CodeAddress(&Skater::CorePhysicsComponent::LipPushOff),
        CodeAddress(&Skater::CorePhysicsComponent::OrientToNormal),
        CodeAddress(&Skater::CorePhysicsComponent::SetSpeed),
        CodeAddress(&Skater::CorePhysicsComponent::NoSpin),
        CodeAddress(&Skater::CorePhysicsComponent::CanSpin),
        CodeAddress(&Skater::CorePhysicsComponent::InBail),
        CodeAddress(&Skater::CorePhysicsComponent::NotInBail),
        CodeAddress(&Skater::CorePhysicsComponent::IsInBail),
        CodeAddress(&Skater::CorePhysicsComponent::BailOn),
        CodeAddress(&Skater::CorePhysicsComponent::BailOff),
        CodeAddress(&Skater::CorePhysicsComponent::BailIsOn),
        CodeAddress(&Skater::CorePhysicsComponent::SetState),
        CodeAddress(&Skater::CorePhysicsComponent::SetRailSound),
        CodeAddress(&Skater::CorePhysicsComponent::LockVelocityDirection),
        CodeAddress(&Skater::CorePhysicsComponent::SetRollingFriction),
        CodeAddress(&Skater::CorePhysicsComponent::SetGrindTweak),
        CodeAddress(&Skater::CorePhysicsComponent::BadLedge),
        CodeAddress(&Skater::CorePhysicsComponent::SkateInAble),
        CodeAddress(&Skater::CorePhysicsComponent::NearGround),
        CodeAddress(&Skater::CorePhysicsComponent::TriggerGrindOff),
        CodeAddress(&Skater::CorePhysicsComponent::LastSpinWas),
        CodeAddress(&Skater::CorePhysicsComponent::LandedFromSpine),
        CodeAddress(&Skater::CorePhysicsComponent::LandedFromVert),
        CodeAddress(&Skater::CorePhysicsComponent::SetLandedFromVert),
        CodeAddress(&Skater::CorePhysicsComponent::ResetLandedFromVert),
        CodeAddress(&Skater::CorePhysicsComponent::InVertAir),
        CodeAddress(&Skater::CorePhysicsComponent::AllowLipNoGrind),
        CodeAddress(&Skater::CorePhysicsComponent::ClearAllowLipNoGrind),
        CodeAddress(&Skater::CorePhysicsComponent::NoRailTricks),
        CodeAddress(&Skater::CorePhysicsComponent::AllowRailTricks),
        CodeAddress(&Skater::CorePhysicsComponent::TurnToFaceVelocity),
        CodeAddress(&Skater::CorePhysicsComponent::CancelRotateDisplayandUpdateMatrix),
        CodeAddress(&Skater::CorePhysicsComponent::OverrideLimits),
        CodeAddress(&Skater::CorePhysicsComponent::SetSpecialFriction),
        CodeAddress(&Skater::CorePhysicsComponent::OverrideCancelGround),
        CodeAddress(&Skater::CorePhysicsComponent::SetExtraPush),
        CodeAddress(&Skater::CorePhysicsComponent::GetMatrixNormal),
        CodeAddress(&Skater::CorePhysicsComponent::GetLastGroundPos),
        CodeAddress(&Skater::CorePhysicsComponent::GetLastGroundHeading),
        CodeAddress(&Skater::CorePhysicsComponent::SetVertAir),
        CodeAddress(&Skater::CorePhysicsComponent::IsFacingWall),
        CodeAddress(&Skater::CorePhysicsComponent::TurnFromWall),
        CodeAddress(&Skater::CorePhysicsComponent::IsFacingVert),
        CodeAddress(&Skater::CorePhysicsComponent::SetAmmo),
        CodeAddress(&Skater::CorePhysicsComponent::GetAmmo),
        CodeAddress(&Skater::CorePhysicsComponent::SetAmmoType),
        CodeAddress(&Skater::CorePhysicsComponent::GetAmmoType),
        CodeAddress(&Skater::CorePhysicsComponent::GetPitchBailDir),
        CodeAddress(&Skater::CorePhysicsComponent::ForceBreakVert),
        CodeAddress(&Skater::CorePhysicsComponent::InRevert),
        CodeAddress(&Skater::CorePhysicsComponent::OutRevert),
        CodeAddress(&Skater::CorePhysicsComponent::IsInRevert),
        CodeAddress(&Skater::CorePhysicsComponent::ProjectilesOn),
        CodeAddress(&Skater::CorePhysicsComponent::ProjectilesOff),
        CodeAddress(&Skater::CorePhysicsComponent::FocusOn),
        CodeAddress(&Skater::CorePhysicsComponent::FocusOff),
        CodeAddress(&Skater::CorePhysicsComponent::IsSkaterFocused),
        CodeAddress(&Skater::CorePhysicsComponent::ResetProjectileInput),
        CodeAddress(&Skater::CorePhysicsComponent::EnterPowerslide),
        CodeAddress(&Skater::CorePhysicsComponent::ExitPowerslide),
        CodeAddress(&Skater::CorePhysicsComponent::WalkingWallplant),
        CodeAddress(&Skater::CorePhysicsComponent::MotoSkateboardOn),
        CodeAddress(&Skater::CorePhysicsComponent::MotoSkateboardOff),
        CodeAddress(&Skater::CorePhysicsComponent::IsOnMotoskateboard),
        CodeAddress(&Skater::CorePhysicsComponent::SkaterIsFlipping),
        CodeAddress(&Skater::CorePhysicsComponent::ResetIsFlipping),
        CodeAddress(&Skater::CorePhysicsComponent::SetThrowingProjectile),
        CodeAddress(&Skater::CorePhysicsComponent::UnsetThrowingProjectile),
        CodeAddress(&Skater::CorePhysicsComponent::IsThrowingProjectile),
        CodeAddress(&Skater::CorePhysicsComponent::CheckNatasExitDirection),
        CodeAddress(&Skater::CorePhysicsComponent::EndManualTurnaround),
        CodeAddress(&Skater::CorePhysicsComponent::GetFocusTimeLeft),
        CodeAddress(&Skater::CorePhysicsComponent::GetFocusTimesAllowed),
        CodeAddress(&Skater::CorePhysicsComponent::SetFocusTimesAllowed),
        CodeAddress(&Skater::CorePhysicsComponent::EnterTantrum),
        CodeAddress(&Skater::CorePhysicsComponent::ExitTantrum),
        CodeAddress(&Skater::CorePhysicsComponent::InTantrum),
        CodeAddress(&Skater::CorePhysicsComponent::DoingFlip),
        CodeAddress(&Skater::CorePhysicsComponent::EnterTagging),
        CodeAddress(&Skater::CorePhysicsComponent::ExitTagging),
        CodeAddress(&Skater::CorePhysicsComponent::IsTagging),
    };
    FillMemberFunctionCode(Skater::g_coreMemberFunctions, code, 109);
}

// 0x006423F0: Obj::g_soundComponentMemberFunctions (0x00683118), 1 functions.
void FillSoundComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSoundComponent::Obj_PlaySound),
    };
    FillMemberFunctionCode(Obj::g_soundComponentMemberFunctions, code, 1);
}

// 0x00642440: Obj::g_trickComponentMemberFunctions (0x00683148), 3 functions.
void FillTrickComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CTrickComponent::Pressed),
        CodeAddress(&Obj::CTrickComponent::GetCurrentBailEndpose),
        CodeAddress(&Obj::CTrickComponent::SetCurrentBailEndpose),
    };
    FillMemberFunctionCode(Obj::g_trickComponentMemberFunctions, code, 3);
}

// 0x006424C0: Obj::g_pagerMessageMemberFunctions (0x006831A8), 5 functions.
void FillPagerMessageMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CPagerMessageComponent::SendPagerMessage),
        CodeAddress(&Obj::CPagerMessageComponent::GetPagerMessage),
        CodeAddress(&Obj::CPagerMessageComponent::GetPagerMessageCount),
        CodeAddress(&Obj::CPagerMessageComponent::GetFirstUnreadMessageNum),
        CodeAddress(&Obj::CPagerMessageComponent::ClearPagerMessages),
    };
    FillMemberFunctionCode(Obj::g_pagerMessageMemberFunctions, code, 5);
}

// 0x00642580: Obj::g_balanceTrickMemberFunctions (0x00683238), 1 functions.
void FillBalanceTrickMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSkaterBalanceTrickComponent::DoingBalanceTrick),
    };
    FillMemberFunctionCode(Obj::g_balanceTrickMemberFunctions, code, 1);
}

// 0x006425D0: Obj::g_compositeObjectMemberFunctions (0x00683268), 30 functions.
void FillCompositeObjectMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CCompositeObject::Obj_GetPosition),
        CodeAddress(&Obj::CCompositeObject::Obj_PrintDetails),
        CodeAddress(&Obj::CCompositeObject::Obj_CreateComponent),
        CodeAddress(&Obj::CCompositeObject::Obj_ComponentExists),
        CodeAddress(&Obj::CCompositeObject::Obj_SetPosition),
        CodeAddress(&Obj::CCompositeObject::Obj_SetOrientation),
        CodeAddress(&Obj::CCompositeObject::Obj_GetQuat),
        CodeAddress(&Obj::CCompositeObject::Obj_ForceUpdate),
        CodeAddress(&Obj::CCompositeObject::Obj_GetVelocity),
        CodeAddress(&Obj::CCompositeObject::GetSpeed),
        CodeAddress(&Obj::CCompositeObject::Obj_Hide),
        CodeAddress(&Obj::CCompositeObject::Obj_Unhide),
        CodeAddress(&Obj::CCompositeObject::Obj_IsHidden),
        CodeAddress(&Obj::CCompositeObject::Obj_MoveLocal),
        CodeAddress(&Obj::CCompositeObject::Obj_Suspend),
        CodeAddress(&Obj::CCompositeObject::Obj_Unsuspend),
        CodeAddress(&Obj::CCompositeObject::Obj_Pause),
        CodeAddress(&Obj::CCompositeObject::Obj_Unpause),
        CodeAddress(&Obj::CCompositeObject::Obj_GetCollision),
        CodeAddress(&Obj::CCompositeObject::Obj_GetOrientation),
        CodeAddress(&Obj::CCompositeObject::Obj_GetDistToNode),
        CodeAddress(&Obj::CCompositeObject::Obj_GetDistanceToObject),
        CodeAddress(&Obj::CCompositeObject::Obj_GetOrientationToObject),
        CodeAddress(&Obj::CCompositeObject::Obj_MovingBackward),
        CodeAddress(&Obj::CCompositeObject::Obj_SpeedNear),
        CodeAddress(&Obj::CCompositeObject::Obj_SpeedAbove),
        CodeAddress(&Obj::CCompositeObject::Obj_SpeedBelow),
        CodeAddress(&Obj::CCompositeObject::Obj_NearestOutsideAngle),
        CodeAddress(&Obj::CCompositeObject::Obj_GetType),
        CodeAddress(&Obj::CCompositeObject::Obj_HasComponent),
    };
    FillMemberFunctionCode(Obj::g_compositeObjectMemberFunctions, code, 30);
}

// 0x00642940: Obj::g_objectMemberFunctions (0x00683550), 16 functions.
void FillObjectMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CObject::Obj_GetTags),
        CodeAddress(&Obj::CObject::Obj_GetSingleTag),
        CodeAddress(&Obj::CObject::Obj_GetNamedTags),
        CodeAddress(&Obj::CObject::Obj_SetTags),
        CodeAddress(&Obj::CObject::Obj_RemoveTags),
        CodeAddress(&Obj::CObject::Obj_LeaveScript),
        CodeAddress(&Obj::CObject::Obj_GetId),
        CodeAddress(&Obj::CObject::Obj_FlagSet),
        CodeAddress(&Obj::CObject::Obj_FlagNotSet),
        CodeAddress(&Obj::CObject::Obj_ClearFlag),
        CodeAddress(&Obj::CObject::Obj_SetFlag),
        CodeAddress(&Obj::CObject::Obj_KillSpawnedScript),
        CodeAddress(&Obj::CObject::Obj_SpawnScript),
        CodeAddress(&Obj::CObject::Obj_SwitchScript),
        CodeAddress(&Obj::CObject::Obj_Visible),
        CodeAddress(&Obj::CObject::Obj_Invisible),
    };
    FillMemberFunctionCode(Obj::g_objectMemberFunctions, code, 16);
}

// 0x00642B30: Obj::g_famMemberFunctions (0x006836E8), 2 functions.
void FillFamMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CFamComponent::Obj_PlayFam),
        CodeAddress(&Obj::CFamComponent::Obj_StopLastFam),
    };
    FillMemberFunctionCode(Obj::g_famMemberFunctions, code, 2);
}

// 0x00642B90: Obj::g_pedLogicMemberFunctions (0x00683730), 28 functions.
void FillPedLogicMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CPedLogicComponent::Ped_SetLogicState),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetLogicState),
        CodeAddress(&Obj::CPedLogicComponent::Ped_InitPath),
        CodeAddress(&Obj::CPedLogicComponent::Ped_StartMoving),
        CodeAddress(&Obj::CPedLogicComponent::Ped_StopMoving),
        CodeAddress(&Obj::CPedLogicComponent::Ped_IsMoving),
        CodeAddress(&Obj::CPedLogicComponent::Ped_SetStickDistances),
        CodeAddress(&Obj::CPedLogicComponent::Ped_SetIsGrinding),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetCurrentVelocity),
        CodeAddress(&Obj::CPedLogicComponent::Ped_StoreMaxVelocity),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetOriginalMaxVelocity),
        CodeAddress(&Obj::CPedLogicComponent::Ped_ApplySpeedBoost),
        CodeAddress(&Obj::CPedLogicComponent::Ped_RemoveSpeedBoost),
        CodeAddress(&Obj::CPedLogicComponent::Ped_Bail),
        CodeAddress(&Obj::CPedLogicComponent::Ped_SetIsBailing),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetJumpToNextNode),
        CodeAddress(&Obj::CPedLogicComponent::Ped_HitWaypoint),
        CodeAddress(&Obj::CPedLogicComponent::Ped_SetIsSkater),
        CodeAddress(&Obj::CPedLogicComponent::Ped_NearVertNode),
        CodeAddress(&Obj::CPedLogicComponent::Ped_PlayJumpSound),
        CodeAddress(&Obj::CPedLogicComponent::Ped_PlayLandSound),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetCurrentNodeNames),
        CodeAddress(&Obj::CPedLogicComponent::Ped_TurnToCurrentWaypointOrientation),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetGender),
        CodeAddress(&Obj::CPedLogicComponent::Ped_ActivateAuxScript),
        CodeAddress(&Obj::CPedLogicComponent::Ped_DeactivateAuxScript),
        CodeAddress(&Obj::CPedLogicComponent::Ped_HideBoard),
        CodeAddress(&Obj::CPedLogicComponent::Ped_GetVoicePrefix),
    };
    FillMemberFunctionCode(Obj::g_pedLogicMemberFunctions, code, 28);
}

// 0x00642ED0: Obj::g_avoidMemberFunctions (0x006839E8), 8 functions.
void FillAvoidMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CAvoidComponent::Jump),
        CodeAddress(&Obj::CAvoidComponent::DebugBox),
        CodeAddress(&Obj::CAvoidComponent::StepJump),
        CodeAddress(&Obj::CAvoidComponent::JumpFinished),
        CodeAddress(&Obj::CAvoidComponent::StorePosition),
        CodeAddress(&Obj::CAvoidComponent::StoreTarget),
        CodeAddress(&Obj::CAvoidComponent::HoldMotion),
        CodeAddress(&Obj::CAvoidComponent::RestoreMotion),
    };
    FillMemberFunctionCode(Obj::g_avoidMemberFunctions, code, 8);
}

// 0x00642FE0: Obj::g_flexibleParticleMemberFunctions (0x00683AC0), 25 functions.
void FillFlexibleParticleMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::DestroyParticles),
        CodeAddress(&Obj::CFlexibleParticleComponent::EmptySystem),
        CodeAddress(&Obj::CFlexibleParticleComponent::SetScripts),
        CodeAddress(&Obj::CFlexibleParticleComponent::SetScripts),
        CodeAddress(&Obj::CFlexibleParticleComponent::ShouldEmit),
        CodeAddress(&Obj::CFlexibleParticleComponent::EmitOn),
        CodeAddress(&Obj::CFlexibleParticleComponent::EmitOff),
        CodeAddress(&Obj::CFlexibleParticleComponent::DestroySystem),
    };
    FillMemberFunctionCode(Obj::g_flexibleParticleMemberFunctions, code, 25);
}

// 0x006432D0: Obj::g_particleComponentMemberFunctions (0x00683D30), 20 functions.
void FillParticleComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CParticleComponent::SetStartPos),
        CodeAddress(&Obj::CParticleComponent::SetMidPos),
        CodeAddress(&Obj::CParticleComponent::SetEndPos),
        CodeAddress(&Obj::CParticleComponent::SetStartBox),
        CodeAddress(&Obj::CParticleComponent::SetMidBox),
        CodeAddress(&Obj::CParticleComponent::SetEndBox),
        CodeAddress(&Obj::CParticleComponent::SetEmitTarget),
        CodeAddress(&Obj::CParticleComponent::SetEmitRate),
        CodeAddress(&Obj::CParticleComponent::SetLifetime),
        CodeAddress(&Obj::CParticleComponent::SetFloat19AF74D1),
        CodeAddress(&Obj::CParticleComponent::SetMidPointPct),
        CodeAddress(&Obj::CParticleComponent::SetStartRadius),
        CodeAddress(&Obj::CParticleComponent::SetMidRadius),
        CodeAddress(&Obj::CParticleComponent::SetEndRadius),
        CodeAddress(&Obj::CParticleComponent::SetStartColor),
        CodeAddress(&Obj::CParticleComponent::SetMidColor),
        CodeAddress(&Obj::CParticleComponent::SetEndColor),
        CodeAddress(&Obj::CParticleComponent::SetFlag5446AF14),
        CodeAddress(&Obj::CParticleComponent::SetColor55835CF2),
        CodeAddress(&Obj::CParticleComponent::GetParticleParams),
    };
    FillMemberFunctionCode(Obj::g_particleComponentMemberFunctions, code, 20);
}

// 0x00643530: Obj::g_rigidBodyMemberFunctions (0x00683F28), 12 functions.
void FillRigidBodyMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_IgnoreSkater),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_Wake),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_Sleep),
        CodeAddress(&Obj::CRigidBodyComponent::Rigidbody_Kick),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_SetVelocities),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_Disable),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_Enable),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_Reset),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_DisablePlayerCollision),
        CodeAddress(&Obj::CRigidBodyComponent::RigidBody_EnablePlayerCollision),
        CodeAddress(&Obj::CRigidBodyComponent::GetCenterOfMassPos),
        CodeAddress(&Obj::CRigidBodyComponent::Rigidbody_MatchVelocityTo),
    };
    FillMemberFunctionCode(Obj::g_rigidBodyMemberFunctions, code, 12);
}

// 0x006436B0: Obj::g_vehicleWheelMemberFunctions (0x00684060), 9 functions.
void FillVehicleWheelMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CVehicleComponent::Kick),
        CodeAddress(&Obj::CVehicleComponent::Wake),
        CodeAddress(&Obj::CVehicleComponent::MoveToRestart),
        CodeAddress(&Obj::CVehicleComponent::PlaceBeforeCamera),
        CodeAddress(&Obj::CVehicleComponent::AdjustGravity),
        CodeAddress(&Obj::CVehicleComponent::HandbrakeActive),
        CodeAddress(&Obj::CVehicleComponent::AllWheelsAreInContact),
        CodeAddress(&Obj::CVehicleComponent::KickRandomly),
        CodeAddress(&Obj::CVehicleComponent::IsSkaterVisible),
    };
    FillMemberFunctionCode(Obj::g_vehicleWheelMemberFunctions, code, 9);
}

// 0x006437E0: Obj::g_vehicleCameraMemberFunctions (0x00684150), 1 functions.
void FillVehicleCameraMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CVehicleCameraComponent::VehicleCamera_Reset),
    };
    FillMemberFunctionCode(Obj::g_vehicleCameraMemberFunctions, code, 1);
}

// 0x00643830: Obj::g_statsManagerMemberFunctions (0x00684180), 9 functions.
void FillStatsManagerMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CStatsManagerComponent::ReInit),
        CodeAddress(&Obj::CStatsManagerComponent::ResetGoals),
        CodeAddress(&Obj::CStatsManagerComponent::ActivateGoals),
        CodeAddress(&Obj::CStatsManagerComponent::DeactivateGoals),
        CodeAddress(&Obj::CStatsManagerComponent::GetGoalStatusMember),
        CodeAddress(&Obj::CStatsManagerComponent::UnlockAmGoals),
        CodeAddress(&Obj::CStatsManagerComponent::UnlockProGoals),
        CodeAddress(&Obj::CStatsManagerComponent::GetLastAirComboScore),
        CodeAddress(&Obj::CStatsManagerComponent::ClearLastAirComboScore),
    };
    FillMemberFunctionCode(Obj::g_statsManagerMemberFunctions, code, 9);
}

// 0x00643960: Ed::g_goalEditorMemberFunctions (0x00684270), 35 functions.
void FillGoalEditorMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Ed::CGoalEditorComponent::EditedGoalAddGap),
        CodeAddress(&Ed::CGoalEditorComponent::EditedGoalRemoveGap),
        CodeAddress(&Ed::CGoalEditorComponent::EditedGoalGotGap),
        CodeAddress(&Ed::CGoalEditorComponent::AddKeyComboSet),
        CodeAddress(&Ed::CGoalEditorComponent::RemoveKeyComboSet),
        CodeAddress(&Ed::CGoalEditorComponent::GetKeyComboSet),
        CodeAddress(&Ed::CGoalEditorComponent::RemoveGoalSpecificFlag),
        CodeAddress(&Ed::CGoalEditorComponent::SetGoalSpecificParams),
        CodeAddress(&Ed::CGoalEditorComponent::FlagGoalAsWon),
        CodeAddress(&Ed::CGoalEditorComponent::FindUnfinishedGoal),
        CodeAddress(&Ed::CGoalEditorComponent::GoalExists),
        CodeAddress(&Ed::CGoalEditorComponent::NewEditorGoal),
        CodeAddress(&Ed::CGoalEditorComponent::RemovedCreatedGoal),
        CodeAddress(&Ed::CGoalEditorComponent::GoalHasAllPositionsSet),
        CodeAddress(&Ed::CGoalEditorComponent::SetCurrentEditorGoal),
        CodeAddress(&Ed::CGoalEditorComponent::SetEditorGoalType),
        CodeAddress(&Ed::CGoalEditorComponent::SetEditorGoalDescription),
        CodeAddress(&Ed::CGoalEditorComponent::SetEditorGoalWinMessage),
        CodeAddress(&Ed::CGoalEditorComponent::GetCurrentEditedGoalInfo),
        CodeAddress(&Ed::CGoalEditorComponent::GetEditedGoalsInfo),
        CodeAddress(&Ed::CGoalEditorComponent::GetNumEditedGoals),
        CodeAddress(&Ed::CGoalEditorComponent::GetCurrentEditedGoalId),
        CodeAddress(&Ed::CGoalEditorComponent::WriteEditedGoalNodePositions),
        CodeAddress(&Ed::CGoalEditorComponent::AddEditedGoalToGoalManager),
        CodeAddress(&Ed::CGoalEditorComponent::SetEditorGoalName),
        CodeAddress(&Ed::CGoalEditorComponent::SetEditorPedName),
        CodeAddress(&Ed::CGoalEditorComponent::MaxEditedGoalsReached),
        CodeAddress(&Ed::CGoalEditorComponent::EditGoal),
        CodeAddress(&Ed::CGoalEditorComponent::NukeAllGoals),
        CodeAddress(&Ed::CGoalEditorComponent::GetMaxGoalsPerLevel),
        CodeAddress(&Ed::CGoalEditorComponent::SetGoalScore),
        CodeAddress(&Ed::CGoalEditorComponent::SetGoalTimeLimit),
        CodeAddress(&Ed::CGoalEditorComponent::SetGoalControlType),
        CodeAddress(&Ed::CGoalEditorComponent::RefreshGoalCursorPosition),
        CodeAddress(&Ed::CGoalEditorComponent::GetCreatedGoals),
    };
    FillMemberFunctionCode(Ed::g_goalEditorMemberFunctions, code, 35);
}

// 0x00643D60: Obj::g_colorCycleMemberFunctions (0x006845D0), 4 functions.
void FillColorCycleMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CColorCycleComponent::On),
        CodeAddress(&Obj::CColorCycleComponent::Off),
        CodeAddress(&Obj::CColorCycleComponent::SetPeriodFromScript),
        CodeAddress(&Obj::CColorCycleComponent::SetColorsFromScript),
    };
    FillMemberFunctionCode(Obj::g_colorCycleMemberFunctions, code, 4);
}

// 0x00643E00: Obj::g_vehicleSoundMemberFunctions (0x00684648), 3 functions.
void FillVehicleSoundMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSkaterVehicleSoundComponent::SetSkaterVehicleSounds),
        CodeAddress(&Obj::CSkaterVehicleSoundComponent::NoSkaterVehicleSounds),
        CodeAddress(&Obj::CSkaterVehicleSoundComponent::StopSkaterVehicleSounds),
    };
    FillMemberFunctionCode(Obj::g_vehicleSoundMemberFunctions, code, 3);
}

// 0x00643E80: Obj::g_carPhysicsMemberFunctions (0x006846A8), 2 functions.
void FillCarPhysicsMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CCarPhysicsComponent::ShowBody),
        CodeAddress(&Obj::CCarPhysicsComponent::DoNothing),
    };
    FillMemberFunctionCode(Obj::g_carPhysicsMemberFunctions, code, 2);
}

// 0x00643EE0: Obj::g_physicsControlMemberFunctions (0x006846F0), 10 functions.
void FillPhysicsControlMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::PausePhysics),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::UnPausePhysics),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::GetTimeSincePhysicsSwitch),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::GetPreviousPhysicsStateDuration),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::SetBoardMissing),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::UnsetBoardMissing),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::IsBoardMissing),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::SetForceWalk),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::UnSetForceWalk),
        CodeAddress(&Obj::CSkaterPhysicsControlComponent::IsForceWalkOn),
    };
    FillMemberFunctionCode(Obj::g_physicsControlMemberFunctions, code, 10);
}

// 0x00644020: Obj::g_walkMemberFunctions (0x006847F8), 18 functions.
void FillWalkMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CWalkComponent::Walk_Ground),
        CodeAddress(&Obj::CSkaterStateComponent::InAir),
        CodeAddress(&Obj::CSkaterStateComponent::OnWall),
        CodeAddress(&Obj::CSkaterStateComponent::OnLip),
        CodeAddress(&Obj::CSkaterStateComponent::OnRail),
        CodeAddress(&Obj::CWalkComponent::Walk_GetStateTime),
        CodeAddress(&Obj::CWalkComponent::ReleaseHang),
        CodeAddress(&Obj::CWalkComponent::Walk_GetSpeedScale),
        CodeAddress(&Obj::CWalkComponent::Walk_ScaleAnimSpeed),
        CodeAddress(&Obj::CWalkComponent::AnimWaitComplete),
        CodeAddress(&Obj::CWalkComponent::Walk_GetHangInitAnimType),
        CodeAddress(&Obj::CWalkComponent::Walk_GetStateDuration),
        CodeAddress(&Obj::CWalkComponent::Walk_GetPreviousState),
        CodeAddress(&Obj::CWalkComponent::Walk_GetState),
        CodeAddress(&Obj::CWalkComponent::Walk_GetHangAngle),
        CodeAddress(&Obj::CWalkComponent::Walk_ReleaseHang),
        CodeAddress(&Obj::CWalkComponent::Walk_SetForwardControlLock),
        CodeAddress(&Obj::CWalkComponent::ForceToAirFromHang),
    };
    FillMemberFunctionCode(Obj::g_walkMemberFunctions, code, 18);
}

// 0x00644240: Obj::g_runTimerMemberFunctions (0x006849C0), 6 functions.
void FillRunTimerMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSkaterRunTimerComponent::Pause),
        CodeAddress(&Obj::CSkaterRunTimerComponent::Unpause),
        CodeAddress(&Obj::CSkaterRunTimerComponent::GetFactorComplete),
        CodeAddress(&Obj::CSkaterRunTimerComponent::GetRunTimerControllerId),
        CodeAddress(&Obj::CSkaterRunTimerComponent::GetRunTimerId),
        CodeAddress(&Obj::CSkaterRunTimerComponent::ComboStarted),
    };
    FillMemberFunctionCode(Obj::g_runTimerMemberFunctions, code, 6);
}

// 0x00644310: Obj::g_loopingSoundMemberFunctions (0x00684A68), 0 functions.
void FillLoopingSoundMemberFunctions()
{
    FillMemberFunctionCode(Obj::g_loopingSoundMemberFunctions, nullptr, 0);
}

// 0x00644340: Obj::g_skaterStateMemberFunctions (0x00684A80), 18 functions.
void FillSkaterStateMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CWalkComponent::Walk_Ground),
        CodeAddress(&Obj::CSkaterStateComponent::InAir),
        CodeAddress(&Obj::CSkaterStateComponent::OnWall),
        CodeAddress(&Obj::CSkaterStateComponent::OnLip),
        CodeAddress(&Obj::CSkaterStateComponent::OnRail),
        CodeAddress(&Obj::CSkaterStateComponent::InWallplant),
        CodeAddress(&Obj::CSkaterStateComponent::InBailState),
        CodeAddress(&Obj::CSkaterStateComponent::Walking),
        CodeAddress(&Obj::CSkaterStateComponent::Skating),
        CodeAddress(&Obj::CSkaterStateComponent::Driving),
        CodeAddress(&Obj::CSkaterStateComponent::SetDriving),
        CodeAddress(&Obj::CSkaterStateComponent::UnsetDriving),
        CodeAddress(&Obj::CSkaterStateComponent::DoingTrick),
        CodeAddress(&Obj::CSkaterStateComponent::HasPowerup),
        CodeAddress(&Obj::CSkaterStateComponent::PickedUpPowerup),
        CodeAddress(&Obj::CSkaterStateComponent::GetTerrain),
        CodeAddress(&Obj::CSkaterStateComponent::GetSparksPos),
        CodeAddress(&Obj::CSkaterStateComponent::SetSparksPos),
    };
    FillMemberFunctionCode(Obj::g_skaterStateMemberFunctions, code, 18);
}

// 0x00644560: Obj::g_suspendComponentMemberFunctions (0x00684C48), 1 functions.
void FillSuspendComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSuspendComponent::Obj_ResetNoSuspendCount),
    };
    FillMemberFunctionCode(Obj::g_suspendComponentMemberFunctions, code, 1);
}

// 0x006445B0: Obj::g_inputComponentMemberFunctions (0x00684C78), 14 functions.
void FillInputComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CInputComponent::LockButtonFromScript),
        CodeAddress(&Obj::CInputComponent::DisablePlayerInput),
        CodeAddress(&Obj::CInputComponent::EnablePlayerInput),
        CodeAddress(&Obj::CInputComponent::NetDisablePlayerInput),
        CodeAddress(&Obj::CInputComponent::NetEnablePlayerInput),
        CodeAddress(&Obj::CInputComponent::PlayerInputIsDisabled),
        CodeAddress(&Obj::CInputComponent::LeftPressed),
        CodeAddress(&Obj::CInputComponent::RightPressed),
        CodeAddress(&Obj::CInputComponent::UpPressed),
        CodeAddress(&Obj::CInputComponent::DownPressed),
        CodeAddress(&Obj::CInputComponent::GetSticks),
        CodeAddress(&Obj::CInputComponent::HeldLongerThan),
        CodeAddress(&Obj::CInputComponent::EnableInputEvents),
        CodeAddress(&Obj::CInputComponent::DisableInputEvents),
    };
    FillMemberFunctionCode(Obj::g_inputComponentMemberFunctions, code, 14);
}

// 0x00644760: Obj::g_skeletonComponentMemberFunctions (0x00684DE0), 5 functions.
void FillSkeletonComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CSkeletonComponent::Obj_SetBoneActive),
        CodeAddress(&Obj::CSkeletonComponent::Obj_SetBoneScale),
        CodeAddress(&Obj::CSkeletonComponent::Obj_GetBonePosition),
        CodeAddress(&Obj::CSkeletonComponent::TextureSplat),
        CodeAddress(&Obj::CSkeletonComponent::Skeleton_SpawnCompositeObject),
    };
    FillMemberFunctionCode(Obj::g_skeletonComponentMemberFunctions, code, 5);
}

// 0x00644820: Obj::g_streamComponentMemberFunctions (0x00684E70), 4 functions.
void FillStreamComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CStreamComponent::Obj_PlayStream),
        CodeAddress(&Obj::CStreamComponent::Obj_AddStream),
        CodeAddress(&Obj::CStreamComponent::Obj_RemoveStream),
        CodeAddress(&Obj::CStreamComponent::Obj_StopStream),
    };
    FillMemberFunctionCode(Obj::g_streamComponentMemberFunctions, code, 4);
}

// 0x006448C0: Obj::g_vibrationComponentMemberFunctions (0x00684EE8), 1 functions.
void FillVibrationComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Obj::CVibrationComponent::Vibrate),
    };
    FillMemberFunctionCode(Obj::g_vibrationComponentMemberFunctions, code, 1);
}

} // namespace

void FillMemberFunctionTables()
{
    FillMotionPathMemberFunctions(); // 0x00640E30
    FillAnimBlenderMemberFunctions(); // 0x006413A0
    FillObjectProximityMemberFunctions(); // 0x00641700
    FillCoreMemberFunctions(); // 0x006417D0
    FillSoundComponentMemberFunctions(); // 0x006423F0
    FillTrickComponentMemberFunctions(); // 0x00642440
    FillPagerMessageMemberFunctions(); // 0x006424C0
    FillBalanceTrickMemberFunctions(); // 0x00642580
    FillCompositeObjectMemberFunctions(); // 0x006425D0
    FillObjectMemberFunctions(); // 0x00642940
    FillFamMemberFunctions(); // 0x00642B30
    FillPedLogicMemberFunctions(); // 0x00642B90
    FillAvoidMemberFunctions(); // 0x00642ED0
    FillFlexibleParticleMemberFunctions(); // 0x00642FE0
    FillParticleComponentMemberFunctions(); // 0x006432D0
    FillRigidBodyMemberFunctions(); // 0x00643530
    FillVehicleWheelMemberFunctions(); // 0x006436B0
    FillVehicleCameraMemberFunctions(); // 0x006437E0
    FillStatsManagerMemberFunctions(); // 0x00643830
    FillGoalEditorMemberFunctions(); // 0x00643960
    FillColorCycleMemberFunctions(); // 0x00643D60
    FillVehicleSoundMemberFunctions(); // 0x00643E00
    FillCarPhysicsMemberFunctions(); // 0x00643E80
    FillPhysicsControlMemberFunctions(); // 0x00643EE0
    FillWalkMemberFunctions(); // 0x00644020
    FillRunTimerMemberFunctions(); // 0x00644240
    FillLoopingSoundMemberFunctions(); // 0x00644310
    FillSkaterStateMemberFunctions(); // 0x00644340
    FillSuspendComponentMemberFunctions(); // 0x00644560
    FillInputComponentMemberFunctions(); // 0x006445B0
    FillSkeletonComponentMemberFunctions(); // 0x00644760
    FillStreamComponentMemberFunctions(); // 0x00644820
    FillVibrationComponentMemberFunctions(); // 0x006448C0
}

} // namespace Script
