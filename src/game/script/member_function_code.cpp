// Generated once from the reference build's tables; maintained by hand since.
// The code half of the script member-function tables (33 tables, 483 functions): what the original's
// static initialisers write into them before WinMain - each record's function (a member function pointer: the code,
// three zero words) and the terminator's {0, 0, 0, -1}. They run at start-up in the original's order
// (game/sys/startup_init.cpp).

#include <cstdint>

#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editor.h"
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
#include "game/obj/entity.h"
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

namespace ParkEditing {
extern uint32_t g_goalEditorMemberFunctions[216];
} // namespace Ed

namespace GameObjects {
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

namespace Scripts {

namespace {

// 0x00640E30: GameObjects::g_motionPathMemberFunctions (0x00681EB8), 48 functions.
void FillMotionPathMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CMotionPathMembers::IsMovingFromScript),
        CodeAddress(&GameObjects::CMotionPathMembers::IsRotatingFromScript),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_StorePos),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_StoreNode),
        CodeAddress(&GameObjects::CMotionPathMembers::StopMoving),
        CodeAddress(&GameObjects::CMotionPathMembers::StopRotating),
        CodeAddress(&GameObjects::CMotionPathMembers::SetPathVelocity),
        CodeAddress(&GameObjects::CMotionPathMembers::SetPathMinStopVel),
        CodeAddress(&GameObjects::CMotionPathMembers::SetPathAcceleration),
        CodeAddress(&GameObjects::CMotionPathMembers::SetPathDeceleration),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_Rotate),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_RotX),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_RotY),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_RotZ),
        CodeAddress(&GameObjects::CMotionPathMembers::RouteFacing),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_SetConstantHeight),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_GetNextObjOnPath),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_GetRandomLink),
        CodeAddress(&GameObjects::CMotionPathMembers::RandomPathMode),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_SetPathTurnDist),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_FollowPath),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtObject),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtNode),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtNodeLinked),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtNodeStored),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtPos),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtPosStored),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_LookAtRelPos),
        CodeAddress(&GameObjects::CMotionPathMembers::StopAlongPath),
        CodeAddress(&GameObjects::CMotionPathMembers::StartAlongPath),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_FollowPathLinked),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_FollowPathStored),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveToPosStored),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveForward),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveLeft),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveToPos),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveToRelPos),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_MoveToLink),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_StickToGround),
        CodeAddress(&GameObjects::CMotionPathMembers::WaitJumpFinished),
        CodeAddress(&GameObjects::CMotionPathMembers::WaitRotate),
        CodeAddress(&GameObjects::CMotionPathMembers::WaitMove),
        CodeAddress(&GameObjects::CMotionPathMembers::WaitStop),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_SkipToRestart),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_Hover),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_GetSpeed),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_Jump),
        CodeAddress(&GameObjects::CKinematicsModule::Obj_FollowLeader),
    };
    FillMemberFunctionCode(GameObjects::g_motionPathMemberFunctions, code, 48);
}

// 0x006413A0: GameObjects::g_animBlenderMemberFunctions (0x00682350), 29 functions.
void FillAnimBlenderMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CAnimBlenderComponent::AddChannelFromScript),
        CodeAddress(&GameObjects::CAnimBlenderComponent::WaitOnChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::RemoveChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::BlendChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::ChannelExists),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
        CodeAddress(&GameObjects::CAnimBlenderComponent::PassToChannel),
    };
    FillMemberFunctionCode(GameObjects::g_animBlenderMemberFunctions, code, 29);
}

// 0x00641700: GameObjects::g_objectProximityMemberFunctions (0x00682620), 6 functions.
void FillObjectProximityMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_SetInnerRadius),
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_SetOuterRadius),
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_SetInnerAvoidRadius),
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_SetOuterAvoidRadius),
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_SetAttentionRadius),
        CodeAddress(&GameObjects::CObjectProximityComponent::Obj_ObjectInAttentionRadius),
    };
    FillMemberFunctionCode(GameObjects::g_objectProximityMemberFunctions, code, 6);
}

// 0x006417D0: Skater::g_coreMemberFunctions (0x006826C8), 109 functions.
void FillCoreMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&Skater::SkaterPhysicsModule::Jump),
        CodeAddress(&Skater::SkaterPhysicsModule::Flipped),
        CodeAddress(&Skater::SkaterPhysicsModule::Switched),
        CodeAddress(&Skater::SkaterPhysicsModule::Crouched),
        CodeAddress(&Skater::SkaterPhysicsModule::FirstTimeOnThisRail),
        CodeAddress(&Skater::SkaterPhysicsModule::StartSkitch),
        CodeAddress(&Skater::SkaterPhysicsModule::Skitching),
        CodeAddress(&Skater::SkaterPhysicsModule::EndTowRide),
        CodeAddress(&Skater::SkaterPhysicsModule::CancelWallpush),
        CodeAddress(&Skater::SkaterPhysicsModule::AirTimeLessThan),
        CodeAddress(&Skater::SkaterPhysicsModule::AirTimeGreaterThan),
        CodeAddress(&Skater::SkaterPhysicsModule::GetAirtime),
        CodeAddress(&Skater::SkaterPhysicsModule::GetAirTimeLeft),
        CodeAddress(&Skater::SkaterPhysicsModule::Braking),
        CodeAddress(&Skater::SkaterPhysicsModule::CanBrakeOff),
        CodeAddress(&Skater::SkaterPhysicsModule::CanBrakeOn),
        CodeAddress(&Skater::SkaterPhysicsModule::CanKick),
        CodeAddress(&Skater::SkaterPhysicsModule::CanKickOn),
        CodeAddress(&Skater::SkaterPhysicsModule::CanKickOff),
        CodeAddress(&Skater::SkaterPhysicsModule::ForceAutoKickOn),
        CodeAddress(&Skater::SkaterPhysicsModule::ForceAutoKickOff),
        CodeAddress(&Skater::SkaterPhysicsModule::AutoKickIsOff),
        CodeAddress(&Skater::SkaterPhysicsModule::RestoreAutoKick),
        CodeAddress(&Skater::SkaterPhysicsModule::SkaterVehicleOn),
        CodeAddress(&Skater::SkaterPhysicsModule::SkaterVehicleOff),
        CodeAddress(&Skater::SkaterPhysicsModule::IsSkaterOnVehicle),
        CodeAddress(&Skater::SkaterPhysicsModule::SetSkaterVehicle),
        CodeAddress(&Skater::SkaterPhysicsModule::DoCarPlantBoost),
        CodeAddress(&Skater::SkaterPhysicsModule::LipPushOff),
        CodeAddress(&Skater::SkaterPhysicsModule::OrientToNormal),
        CodeAddress(&Skater::SkaterPhysicsModule::SetSpeed),
        CodeAddress(&Skater::SkaterPhysicsModule::NoSpin),
        CodeAddress(&Skater::SkaterPhysicsModule::CanSpin),
        CodeAddress(&Skater::SkaterPhysicsModule::InBail),
        CodeAddress(&Skater::SkaterPhysicsModule::NotInBail),
        CodeAddress(&Skater::SkaterPhysicsModule::IsInBail),
        CodeAddress(&Skater::SkaterPhysicsModule::BailOn),
        CodeAddress(&Skater::SkaterPhysicsModule::BailOff),
        CodeAddress(&Skater::SkaterPhysicsModule::BailIsOn),
        CodeAddress(&Skater::SkaterPhysicsModule::ChangePhase),
        CodeAddress(&Skater::SkaterPhysicsModule::SetRailSound),
        CodeAddress(&Skater::SkaterPhysicsModule::LockVelocityDirection),
        CodeAddress(&Skater::SkaterPhysicsModule::ChangeWheelDrag),
        CodeAddress(&Skater::SkaterPhysicsModule::ChangeRailAdjust),
        CodeAddress(&Skater::SkaterPhysicsModule::BadLedge),
        CodeAddress(&Skater::SkaterPhysicsModule::SkateInAble),
        CodeAddress(&Skater::SkaterPhysicsModule::NearGround),
        CodeAddress(&Skater::SkaterPhysicsModule::TriggerGrindOff),
        CodeAddress(&Skater::SkaterPhysicsModule::LastSpinWas),
        CodeAddress(&Skater::SkaterPhysicsModule::CameDownOffSpine),
        CodeAddress(&Skater::SkaterPhysicsModule::LandedFromVert),
        CodeAddress(&Skater::SkaterPhysicsModule::SetLandedFromVert),
        CodeAddress(&Skater::SkaterPhysicsModule::ResetLandedFromVert),
        CodeAddress(&Skater::SkaterPhysicsModule::InVertAir),
        CodeAddress(&Skater::SkaterPhysicsModule::AllowLipNoGrind),
        CodeAddress(&Skater::SkaterPhysicsModule::ClearAllowLipNoGrind),
        CodeAddress(&Skater::SkaterPhysicsModule::NoRailTricks),
        CodeAddress(&Skater::SkaterPhysicsModule::AllowRailTricks),
        CodeAddress(&Skater::SkaterPhysicsModule::TurnToFaceVelocity),
        CodeAddress(&Skater::SkaterPhysicsModule::CancelRotateDisplayandUpdateMatrix),
        CodeAddress(&Skater::SkaterPhysicsModule::OverrideLimits),
        CodeAddress(&Skater::SkaterPhysicsModule::SetSpecialFriction),
        CodeAddress(&Skater::SkaterPhysicsModule::OverrideCancelGround),
        CodeAddress(&Skater::SkaterPhysicsModule::SetExtraPush),
        CodeAddress(&Skater::SkaterPhysicsModule::GetMatrixNormal),
        CodeAddress(&Skater::SkaterPhysicsModule::GetLastGroundPos),
        CodeAddress(&Skater::SkaterPhysicsModule::GetLastGroundHeading),
        CodeAddress(&Skater::SkaterPhysicsModule::SetVertAir),
        CodeAddress(&Skater::SkaterPhysicsModule::IsFacingWall),
        CodeAddress(&Skater::SkaterPhysicsModule::TurnFromWall),
        CodeAddress(&Skater::SkaterPhysicsModule::IsFacingVert),
        CodeAddress(&Skater::SkaterPhysicsModule::SetAmmo),
        CodeAddress(&Skater::SkaterPhysicsModule::GetAmmo),
        CodeAddress(&Skater::SkaterPhysicsModule::SetAmmoType),
        CodeAddress(&Skater::SkaterPhysicsModule::GetAmmoType),
        CodeAddress(&Skater::SkaterPhysicsModule::GetPitchBailDir),
        CodeAddress(&Skater::SkaterPhysicsModule::ForceBreakVert),
        CodeAddress(&Skater::SkaterPhysicsModule::InRevert),
        CodeAddress(&Skater::SkaterPhysicsModule::OutRevert),
        CodeAddress(&Skater::SkaterPhysicsModule::IsInRevert),
        CodeAddress(&Skater::SkaterPhysicsModule::ProjectilesOn),
        CodeAddress(&Skater::SkaterPhysicsModule::ProjectilesOff),
        CodeAddress(&Skater::SkaterPhysicsModule::FocusOn),
        CodeAddress(&Skater::SkaterPhysicsModule::FocusOff),
        CodeAddress(&Skater::SkaterPhysicsModule::IsSkaterFocused),
        CodeAddress(&Skater::SkaterPhysicsModule::ResetProjectileInput),
        CodeAddress(&Skater::SkaterPhysicsModule::EnterPowerslide),
        CodeAddress(&Skater::SkaterPhysicsModule::ExitPowerslide),
        CodeAddress(&Skater::SkaterPhysicsModule::WalkingWallplant),
        CodeAddress(&Skater::SkaterPhysicsModule::MotoSkateboardOn),
        CodeAddress(&Skater::SkaterPhysicsModule::MotoSkateboardOff),
        CodeAddress(&Skater::SkaterPhysicsModule::IsOnMotoskateboard),
        CodeAddress(&Skater::SkaterPhysicsModule::SkaterIsFlipping),
        CodeAddress(&Skater::SkaterPhysicsModule::ResetIsFlipping),
        CodeAddress(&Skater::SkaterPhysicsModule::SetThrowingProjectile),
        CodeAddress(&Skater::SkaterPhysicsModule::UnsetThrowingProjectile),
        CodeAddress(&Skater::SkaterPhysicsModule::IsThrowingProjectile),
        CodeAddress(&Skater::SkaterPhysicsModule::CheckNatasExitDirection),
        CodeAddress(&Skater::SkaterPhysicsModule::EndManualTurnaround),
        CodeAddress(&Skater::SkaterPhysicsModule::GetFocusTimeLeft),
        CodeAddress(&Skater::SkaterPhysicsModule::GetFocusTimesAllowed),
        CodeAddress(&Skater::SkaterPhysicsModule::SetFocusTimesAllowed),
        CodeAddress(&Skater::SkaterPhysicsModule::EnterTantrum),
        CodeAddress(&Skater::SkaterPhysicsModule::ExitTantrum),
        CodeAddress(&Skater::SkaterPhysicsModule::InTantrum),
        CodeAddress(&Skater::SkaterPhysicsModule::DoingFlip),
        CodeAddress(&Skater::SkaterPhysicsModule::EnterTagging),
        CodeAddress(&Skater::SkaterPhysicsModule::ExitTagging),
        CodeAddress(&Skater::SkaterPhysicsModule::IsTagging),
    };
    FillMemberFunctionCode(Skater::g_coreMemberFunctions, code, 109);
}

// 0x006423F0: GameObjects::g_soundComponentMemberFunctions (0x00683118), 1 functions.
void FillSoundComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CAudioEmitterModule::Obj_PlaySound),
    };
    FillMemberFunctionCode(GameObjects::g_soundComponentMemberFunctions, code, 1);
}

// 0x00642440: GameObjects::g_trickComponentMemberFunctions (0x00683148), 3 functions.
void FillTrickComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CMoveExecutor::Squeezed),
        CodeAddress(&GameObjects::CMoveExecutor::GetCurrentBailEndpose),
        CodeAddress(&GameObjects::CMoveExecutor::SetCurrentBailEndpose),
    };
    FillMemberFunctionCode(GameObjects::g_trickComponentMemberFunctions, code, 3);
}

// 0x006424C0: GameObjects::g_pagerMessageMemberFunctions (0x006831A8), 5 functions.
void FillPagerMessageMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CPagerMessageComponent::SendPagerMessage),
        CodeAddress(&GameObjects::CPagerMessageComponent::GetPagerMessage),
        CodeAddress(&GameObjects::CPagerMessageComponent::GetPagerMessageCount),
        CodeAddress(&GameObjects::CPagerMessageComponent::GetFirstUnreadMessageNum),
        CodeAddress(&GameObjects::CPagerMessageComponent::ClearPagerMessages),
    };
    FillMemberFunctionCode(GameObjects::g_pagerMessageMemberFunctions, code, 5);
}

// 0x00642580: GameObjects::g_balanceTrickMemberFunctions (0x00683238), 1 functions.
void FillBalanceTrickMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CRiderTeeterMoves::InBalanceMove),
    };
    FillMemberFunctionCode(GameObjects::g_balanceTrickMemberFunctions, code, 1);
}

// 0x006425D0: GameObjects::g_compositeObjectMemberFunctions (0x00683268), 30 functions.
void FillCompositeObjectMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CGameEntity::Obj_GetPosition),
        CodeAddress(&GameObjects::CGameEntity::Obj_PrintDetails),
        CodeAddress(&GameObjects::CGameEntity::Obj_CreateComponent),
        CodeAddress(&GameObjects::CGameEntity::Obj_ComponentExists),
        CodeAddress(&GameObjects::CGameEntity::Obj_SetPosition),
        CodeAddress(&GameObjects::CGameEntity::Obj_SetOrientation),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetQuat),
        CodeAddress(&GameObjects::CGameEntity::Obj_ForceUpdate),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetVelocity),
        CodeAddress(&GameObjects::CGameEntity::GetSpeed),
        CodeAddress(&GameObjects::CGameEntity::Obj_Hide),
        CodeAddress(&GameObjects::CGameEntity::Obj_Unhide),
        CodeAddress(&GameObjects::CGameEntity::Obj_IsHidden),
        CodeAddress(&GameObjects::CGameEntity::Obj_MoveLocal),
        CodeAddress(&GameObjects::CGameEntity::Obj_Suspend),
        CodeAddress(&GameObjects::CGameEntity::Obj_Unsuspend),
        CodeAddress(&GameObjects::CGameEntity::Obj_Pause),
        CodeAddress(&GameObjects::CGameEntity::Obj_Unpause),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetCollision),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetOrientation),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetDistToNode),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetDistanceToObject),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetOrientationToObject),
        CodeAddress(&GameObjects::CGameEntity::Obj_MovingBackward),
        CodeAddress(&GameObjects::CGameEntity::Obj_SpeedNear),
        CodeAddress(&GameObjects::CGameEntity::Obj_SpeedAbove),
        CodeAddress(&GameObjects::CGameEntity::Obj_SpeedBelow),
        CodeAddress(&GameObjects::CGameEntity::Obj_NearestOutsideAngle),
        CodeAddress(&GameObjects::CGameEntity::Obj_GetType),
        CodeAddress(&GameObjects::CGameEntity::Obj_HasComponent),
    };
    FillMemberFunctionCode(GameObjects::g_compositeObjectMemberFunctions, code, 30);
}

// 0x00642940: GameObjects::g_objectMemberFunctions (0x00683550), 16 functions.
void FillObjectMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CEntity::Obj_GetTags),
        CodeAddress(&GameObjects::CEntity::Obj_GetSingleTag),
        CodeAddress(&GameObjects::CEntity::Obj_GetNamedTags),
        CodeAddress(&GameObjects::CEntity::Obj_SetTags),
        CodeAddress(&GameObjects::CEntity::Obj_RemoveTags),
        CodeAddress(&GameObjects::CEntity::Obj_LeaveScript),
        CodeAddress(&GameObjects::CEntity::Obj_GetId),
        CodeAddress(&GameObjects::CEntity::Obj_FlagSet),
        CodeAddress(&GameObjects::CEntity::Obj_FlagNotSet),
        CodeAddress(&GameObjects::CEntity::Obj_ClearFlag),
        CodeAddress(&GameObjects::CEntity::Obj_SetFlag),
        CodeAddress(&GameObjects::CEntity::Obj_KillSpawnedScript),
        CodeAddress(&GameObjects::CEntity::Obj_SpawnScript),
        CodeAddress(&GameObjects::CEntity::Obj_SwitchScript),
        CodeAddress(&GameObjects::CEntity::Obj_Visible),
        CodeAddress(&GameObjects::CEntity::Obj_Invisible),
    };
    FillMemberFunctionCode(GameObjects::g_objectMemberFunctions, code, 16);
}

// 0x00642B30: GameObjects::g_famMemberFunctions (0x006836E8), 2 functions.
void FillFamMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CFamComponent::Obj_PlayFam),
        CodeAddress(&GameObjects::CFamComponent::Obj_StopLastFam),
    };
    FillMemberFunctionCode(GameObjects::g_famMemberFunctions, code, 2);
}

// 0x00642B90: GameObjects::g_pedLogicMemberFunctions (0x00683730), 28 functions.
void FillPedLogicMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_SetLogicState),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetLogicState),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_InitPath),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_StartMoving),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_StopMoving),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_IsMoving),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_SetStickDistances),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_SetIsGrinding),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetCurrentVelocity),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_StoreMaxVelocity),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetOriginalMaxVelocity),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_ApplySpeedBoost),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_RemoveSpeedBoost),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_Bail),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_SetIsBailing),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetJumpToNextNode),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_HitWaypoint),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_SetIsSkater),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_NearVertNode),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_PlayJumpSound),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_PlayLandSound),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetCurrentNodeNames),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_TurnToCurrentWaypointOrientation),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetGender),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_ActivateAuxScript),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_DeactivateAuxScript),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_HideBoard),
        CodeAddress(&GameObjects::CPedestrianBrainModule::Ped_GetVoicePrefix),
    };
    FillMemberFunctionCode(GameObjects::g_pedLogicMemberFunctions, code, 28);
}

// 0x00642ED0: GameObjects::g_avoidMemberFunctions (0x006839E8), 8 functions.
void FillAvoidMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CObstacleDodgeModule::Jump),
        CodeAddress(&GameObjects::CObstacleDodgeModule::DebugBox),
        CodeAddress(&GameObjects::CObstacleDodgeModule::StepJump),
        CodeAddress(&GameObjects::CObstacleDodgeModule::JumpFinished),
        CodeAddress(&GameObjects::CObstacleDodgeModule::StorePosition),
        CodeAddress(&GameObjects::CObstacleDodgeModule::StoreTarget),
        CodeAddress(&GameObjects::CObstacleDodgeModule::HoldMotion),
        CodeAddress(&GameObjects::CObstacleDodgeModule::RestoreMotion),
    };
    FillMemberFunctionCode(GameObjects::g_avoidMemberFunctions, code, 8);
}

// 0x00642FE0: GameObjects::g_flexibleParticleMemberFunctions (0x00683AC0), 25 functions.
void FillFlexibleParticleMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::PassToSystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::DestroyParticles),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::EmptySystem),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::SetScripts),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::SetScripts),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::ShouldEmit),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::EmitOn),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::EmitOff),
        CodeAddress(&GameObjects::CFlexibleParticleComponent::DestroySystem),
    };
    FillMemberFunctionCode(GameObjects::g_flexibleParticleMemberFunctions, code, 25);
}

// 0x006432D0: GameObjects::g_particleComponentMemberFunctions (0x00683D30), 20 functions.
void FillParticleComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetStartPos),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetMidPos),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEndPos),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetStartBox),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetMidBox),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEndBox),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEmitTarget),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEmitRate),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetLifespan),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetFloat19AF74D1),
        CodeAddress(&GameObjects::CEffectsEmitterModule::ChangeMidpointFraction),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetStartRadius),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetMidRadius),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEndRadius),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetStartColor),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetMidColor),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetEndColor),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetFlag5446AF14),
        CodeAddress(&GameObjects::CEffectsEmitterModule::SetColor55835CF2),
        CodeAddress(&GameObjects::CEffectsEmitterModule::GetParticleParams),
    };
    FillMemberFunctionCode(GameObjects::g_particleComponentMemberFunctions, code, 20);
}

// 0x00643530: GameObjects::g_rigidBodyMemberFunctions (0x00683F28), 12 functions.
void FillRigidBodyMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_IgnoreSkater),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_Wake),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_Sleep),
        CodeAddress(&GameObjects::CBodyDynamicsModule::Rigidbody_Kick),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_SetVelocities),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_Disable),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_Enable),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_Reset),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_DisablePlayerCollision),
        CodeAddress(&GameObjects::CBodyDynamicsModule::RigidBody_EnablePlayerCollision),
        CodeAddress(&GameObjects::CBodyDynamicsModule::GetCenterOfMassPos),
        CodeAddress(&GameObjects::CBodyDynamicsModule::Rigidbody_MatchVelocityTo),
    };
    FillMemberFunctionCode(GameObjects::g_rigidBodyMemberFunctions, code, 12);
}

// 0x006436B0: GameObjects::g_vehicleWheelMemberFunctions (0x00684060), 9 functions.
void FillVehicleWheelMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CDrivableModule::Kick),
        CodeAddress(&GameObjects::CDrivableModule::Wake),
        CodeAddress(&GameObjects::CDrivableModule::TeleportToSpawn),
        CodeAddress(&GameObjects::CDrivableModule::PlaceBeforeCamera),
        CodeAddress(&GameObjects::CDrivableModule::AdjustGravity),
        CodeAddress(&GameObjects::CDrivableModule::HandbrakeActive),
        CodeAddress(&GameObjects::CDrivableModule::AllWheelsAreInContact),
        CodeAddress(&GameObjects::CDrivableModule::KickRandomly),
        CodeAddress(&GameObjects::CDrivableModule::IsRiderShown),
    };
    FillMemberFunctionCode(GameObjects::g_vehicleWheelMemberFunctions, code, 9);
}

// 0x006437E0: GameObjects::g_vehicleCameraMemberFunctions (0x00684150), 1 functions.
void FillVehicleCameraMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CCarView::VehicleCamera_Reset),
    };
    FillMemberFunctionCode(GameObjects::g_vehicleCameraMemberFunctions, code, 1);
}

// 0x00643830: GameObjects::g_statsManagerMemberFunctions (0x00684180), 9 functions.
void FillStatsManagerMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CAttributeTracker::ReInit),
        CodeAddress(&GameObjects::CAttributeTracker::ResetGoals),
        CodeAddress(&GameObjects::CAttributeTracker::ActivateGoals),
        CodeAddress(&GameObjects::CAttributeTracker::DeactivateGoals),
        CodeAddress(&GameObjects::CAttributeTracker::GetGoalStatusMember),
        CodeAddress(&GameObjects::CAttributeTracker::UnlockAmGoals),
        CodeAddress(&GameObjects::CAttributeTracker::UnlockProGoals),
        CodeAddress(&GameObjects::CAttributeTracker::GetLastAirComboScore),
        CodeAddress(&GameObjects::CAttributeTracker::ClearLastAirComboScore),
    };
    FillMemberFunctionCode(GameObjects::g_statsManagerMemberFunctions, code, 9);
}

// 0x00643960: ParkEditing::g_goalEditorMemberFunctions (0x00684270), 35 functions.
void FillGoalEditorMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&ParkEditing::CObjectiveAuthoring::EditedGoalAddGap),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::EditedGoalRemoveGap),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::EditedGoalGotGap),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::AddKeyComboSet),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::RemoveKeyComboSet),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetKeyComboSet),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::RemoveGoalSpecificFlag),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetGoalSpecificParams),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::FlagGoalAsWon),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::FindUnfinishedGoal),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GoalExists),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::NewEditorGoal),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::RemovedCreatedGoal),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GoalHasAllPositionsSet),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetCurrentEditorGoal),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetEditorGoalType),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetEditorGoalDescription),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetEditorGoalWinMessage),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetCurrentEditedGoalInfo),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetEditedGoalsInfo),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetNumEditedGoals),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetCurrentEditedGoalId),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::WriteEditedGoalNodePositions),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::AddEditedGoalToGoalManager),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetEditorGoalName),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetEditorPedName),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::MaxEditedGoalsReached),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::EditGoal),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::NukeAllGoals),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetMaxGoalsPerLevel),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetGoalScore),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetGoalTimeLimit),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::SetGoalControlType),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::RefreshGoalCursorPosition),
        CodeAddress(&ParkEditing::CObjectiveAuthoring::GetCreatedGoals),
    };
    FillMemberFunctionCode(ParkEditing::g_goalEditorMemberFunctions, code, 35);
}

// 0x00643D60: GameObjects::g_colorCycleMemberFunctions (0x006845D0), 4 functions.
void FillColorCycleMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CColorCycleComponent::On),
        CodeAddress(&GameObjects::CColorCycleComponent::Disengaged),
        CodeAddress(&GameObjects::CColorCycleComponent::SetPeriodFromScript),
        CodeAddress(&GameObjects::CColorCycleComponent::SetColorsFromScript),
    };
    FillMemberFunctionCode(GameObjects::g_colorCycleMemberFunctions, code, 4);
}

// 0x00643E00: GameObjects::g_vehicleSoundMemberFunctions (0x00684648), 3 functions.
void FillVehicleSoundMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CSkaterVehicleSoundComponent::SetSkaterVehicleSounds),
        CodeAddress(&GameObjects::CSkaterVehicleSoundComponent::NoSkaterVehicleSounds),
        CodeAddress(&GameObjects::CSkaterVehicleSoundComponent::StopSkaterVehicleSounds),
    };
    FillMemberFunctionCode(GameObjects::g_vehicleSoundMemberFunctions, code, 3);
}

// 0x00643E80: GameObjects::g_carPhysicsMemberFunctions (0x006846A8), 2 functions.
void FillCarPhysicsMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CAutomobileDynamicsModule::ShowBody),
        CodeAddress(&GameObjects::CAutomobileDynamicsModule::DoNothing),
    };
    FillMemberFunctionCode(GameObjects::g_carPhysicsMemberFunctions, code, 2);
}

// 0x00643EE0: GameObjects::g_physicsControlMemberFunctions (0x006846F0), 10 functions.
void FillPhysicsControlMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CRiderBoardPhysics::PausePhysics),
        CodeAddress(&GameObjects::CRiderBoardPhysics::UnPausePhysics),
        CodeAddress(&GameObjects::CRiderBoardPhysics::GetTimeSincePhysicsSwitch),
        CodeAddress(&GameObjects::CRiderBoardPhysics::GetPreviousPhysicsStateDuration),
        CodeAddress(&GameObjects::CRiderBoardPhysics::SetBoardMissing),
        CodeAddress(&GameObjects::CRiderBoardPhysics::UnsetBoardMissing),
        CodeAddress(&GameObjects::CRiderBoardPhysics::IsBoardMissing),
        CodeAddress(&GameObjects::CRiderBoardPhysics::SetForceWalk),
        CodeAddress(&GameObjects::CRiderBoardPhysics::UnSetForceWalk),
        CodeAddress(&GameObjects::CRiderBoardPhysics::IsForceWalkOn),
    };
    FillMemberFunctionCode(GameObjects::g_physicsControlMemberFunctions, code, 10);
}

// 0x00644020: GameObjects::g_walkMemberFunctions (0x006847F8), 18 functions.
void FillWalkMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CFootMovement::Walk_Ground),
        CodeAddress(&GameObjects::CRiderPoseState::InAir),
        CodeAddress(&GameObjects::CRiderPoseState::OnWall),
        CodeAddress(&GameObjects::CRiderPoseState::OnLip),
        CodeAddress(&GameObjects::CRiderPoseState::OnRail),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetStateTime),
        CodeAddress(&GameObjects::CFootMovement::ReleaseHang),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetSpeedScale),
        CodeAddress(&GameObjects::CFootMovement::Walk_ScaleAnimSpeed),
        CodeAddress(&GameObjects::CFootMovement::ClipFinishedWaiting),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetHangInitAnimType),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetStateDuration),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetPreviousState),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetState),
        CodeAddress(&GameObjects::CFootMovement::Walk_GetHangAngle),
        CodeAddress(&GameObjects::CFootMovement::Walk_ReleaseHang),
        CodeAddress(&GameObjects::CFootMovement::Walk_SetForwardControlLock),
        CodeAddress(&GameObjects::CFootMovement::ForceToAirFromHang),
    };
    FillMemberFunctionCode(GameObjects::g_walkMemberFunctions, code, 18);
}

// 0x00644240: GameObjects::g_runTimerMemberFunctions (0x006849C0), 6 functions.
void FillRunTimerMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CRiderRunClock::Freeze),
        CodeAddress(&GameObjects::CRiderRunClock::Unpause),
        CodeAddress(&GameObjects::CRiderRunClock::GetFactorComplete),
        CodeAddress(&GameObjects::CRiderRunClock::FetchRunClockPadId),
        CodeAddress(&GameObjects::CRiderRunClock::GetRunTimerId),
        CodeAddress(&GameObjects::CRiderRunClock::ChainBegan),
    };
    FillMemberFunctionCode(GameObjects::g_runTimerMemberFunctions, code, 6);
}

// 0x00644310: GameObjects::g_loopingSoundMemberFunctions (0x00684A68), 0 functions.
void FillLoopingSoundMemberFunctions()
{
    FillMemberFunctionCode(GameObjects::g_loopingSoundMemberFunctions, nullptr, 0);
}

// 0x00644340: GameObjects::g_skaterStateMemberFunctions (0x00684A80), 18 functions.
void FillSkaterStateMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CFootMovement::Walk_Ground),
        CodeAddress(&GameObjects::CRiderPoseState::InAir),
        CodeAddress(&GameObjects::CRiderPoseState::OnWall),
        CodeAddress(&GameObjects::CRiderPoseState::OnLip),
        CodeAddress(&GameObjects::CRiderPoseState::OnRail),
        CodeAddress(&GameObjects::CRiderPoseState::InWallplant),
        CodeAddress(&GameObjects::CRiderPoseState::InBailState),
        CodeAddress(&GameObjects::CRiderPoseState::OnFoot),
        CodeAddress(&GameObjects::CRiderPoseState::Skating),
        CodeAddress(&GameObjects::CRiderPoseState::Driving),
        CodeAddress(&GameObjects::CRiderPoseState::SetDriving),
        CodeAddress(&GameObjects::CRiderPoseState::UnsetDriving),
        CodeAddress(&GameObjects::CRiderPoseState::InAirMove),
        CodeAddress(&GameObjects::CRiderPoseState::HasPowerup),
        CodeAddress(&GameObjects::CRiderPoseState::PickedUpPowerup),
        CodeAddress(&GameObjects::CRiderPoseState::GetSurfaceKind),
        CodeAddress(&GameObjects::CRiderPoseState::GetSparksPos),
        CodeAddress(&GameObjects::CRiderPoseState::SetSparksPos),
    };
    FillMemberFunctionCode(GameObjects::g_skaterStateMemberFunctions, code, 18);
}

// 0x00644560: GameObjects::g_suspendComponentMemberFunctions (0x00684C48), 1 functions.
void FillSuspendComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CDormancyModule::Obj_ResetNoSuspendCount),
    };
    FillMemberFunctionCode(GameObjects::g_suspendComponentMemberFunctions, code, 1);
}

// 0x006445B0: GameObjects::g_inputComponentMemberFunctions (0x00684C78), 14 functions.
void FillInputComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CPadInputModule::LockButtonFromScript),
        CodeAddress(&GameObjects::CPadInputModule::DisablePlayerInput),
        CodeAddress(&GameObjects::CPadInputModule::EnablePlayerInput),
        CodeAddress(&GameObjects::CPadInputModule::NetDisablePlayerInput),
        CodeAddress(&GameObjects::CPadInputModule::NetEnablePlayerInput),
        CodeAddress(&GameObjects::CPadInputModule::PlayerInputIsDisabled),
        CodeAddress(&GameObjects::CPadInputModule::LeftPressed),
        CodeAddress(&GameObjects::CPadInputModule::RightPressed),
        CodeAddress(&GameObjects::CPadInputModule::UpPressed),
        CodeAddress(&GameObjects::CPadInputModule::DownPressed),
        CodeAddress(&GameObjects::CPadInputModule::GetSticks),
        CodeAddress(&GameObjects::CPadInputModule::HeldLongerThan),
        CodeAddress(&GameObjects::CPadInputModule::EnableInputEvents),
        CodeAddress(&GameObjects::CPadInputModule::DisableInputEvents),
    };
    FillMemberFunctionCode(GameObjects::g_inputComponentMemberFunctions, code, 14);
}

// 0x00644760: GameObjects::g_skeletonComponentMemberFunctions (0x00684DE0), 5 functions.
void FillSkeletonComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CBoneRigModule::Obj_SetBoneActive),
        CodeAddress(&GameObjects::CBoneRigModule::Obj_SetBoneScale),
        CodeAddress(&GameObjects::CBoneRigModule::Obj_GetBonePosition),
        CodeAddress(&GameObjects::CBoneRigModule::TextureSplat),
        CodeAddress(&GameObjects::CBoneRigModule::Skeleton_SpawnCompositeObject),
    };
    FillMemberFunctionCode(GameObjects::g_skeletonComponentMemberFunctions, code, 5);
}

// 0x00644820: GameObjects::g_streamComponentMemberFunctions (0x00684E70), 4 functions.
void FillStreamComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CAudioStreamModule::Obj_PlayStream),
        CodeAddress(&GameObjects::CAudioStreamModule::Obj_AddStream),
        CodeAddress(&GameObjects::CAudioStreamModule::Obj_RemoveStream),
        CodeAddress(&GameObjects::CAudioStreamModule::Obj_StopStream),
    };
    FillMemberFunctionCode(GameObjects::g_streamComponentMemberFunctions, code, 4);
}

// 0x006448C0: GameObjects::g_vibrationComponentMemberFunctions (0x00684EE8), 1 functions.
void FillVibrationComponentMemberFunctions()
{
    void* const code[] = {
        CodeAddress(&GameObjects::CPadRumbleModule::Vibrate),
    };
    FillMemberFunctionCode(GameObjects::g_vibrationComponentMemberFunctions, code, 1);
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
