// The component types every composite object can be made of, registered with the composite object manager.

#include "game/obj/component_types.h"

#include <cstdint>

#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "game/obj/avoid.h"
#include "game/obj/bbox_component.h"
#include "game/obj/camera_component.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/car_physics.h"
#include "game/obj/chain_component.h"
#include "game/obj/collide_and_die.h"
#include "game/obj/collision_component.h"
#include "game/obj/color_cycle.h"
#include "game/obj/display_matrix_component.h"
#include "game/obj/dynamic_light.h"
#include "game/obj/editor_camera.h"
#include "game/obj/fam.h"
#include "game/obj/flexible_particle.h"
#include "game/obj/floating_text.h"
#include "game/obj/input_component.h"
#include "game/obj/lock_obj.h"
#include "game/obj/matrix_component.h"
#include "game/obj/model_component.h"
#include "game/obj/model_light_update.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/movable_contact.h"
#include "game/obj/node_components.h"
#include "game/obj/object_proximity.h"
#include "game/obj/pager_message.h"
#include "game/obj/particle_component.h"
#include "game/obj/ped_logic.h"
#include "game/obj/projectile_component.h"
#include "game/obj/projectile_components.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/shadow_component.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/sound_component.h"
#include "game/obj/static_vehicle.h"
#include "game/obj/stream_component.h"
#include "game/obj/suspend_component.h"
#include "game/obj/texture_projection_component.h"
#include "game/obj/trigger_component.h"
#include "game/obj/vehicle_component.h"
#include "game/obj/vehicle_sound.h"
#include "game/obj/velocity_component.h"
#include "game/obj/vibration_component.h"
#include "game/skater/adjust_physics.h"
#include "game/skater/balance_trick.h"
#include "game/skater/core_physics.h"
#include "game/skater/end_run.h"
#include "game/skater/finalize_physics.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/floating_name.h"
#include "game/skater/gap_entry.h"
#include "game/skater/local_net_logic.h"
#include "game/skater/looping_sound.h"
#include "game/skater/matrix_queries.h"
#include "game/skater/nonlocal_net_logic.h"
#include "game/skater/physics_control.h"
#include "game/skater/rotate_component.h"
#include "game/skater/run_timer.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/skater_score.h"
#include "game/skater/skater_sound.h"
#include "game/skater/skater_state.h"
#include "game/skater/skitch.h"
#include "game/skater/special_item.h"
#include "game/skater/stance_panel.h"
#include "game/skater/state_history.h"
#include "game/skater/stats_manager.h"
#include "game/skater/tagging.h"
#include "game/skater/trick_component.h"
#include "game/skater/vehicle_camera.h"
#include "game/skater/vehicle_sound.h"
#include "game/skater/walk_camera.h"
#include "game/skater/walk_physics.h"
#include "game/obj/anim_blender.h"
#include "game/obj/vehicle_wheel.h"
#include "game/obj/walk_hang.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"

namespace GameObjects {

namespace {

// A component type: its checksum, the function that makes one and the one that registers its script member
// functions (if any). Names are given where the checksum's name is known.
struct ComponentType {
    uint32_t type;
    void* create;
    void* registerFunctions;
};

// In the original's order (SpawnModule finds the first of a type).
const ComponentType kComponentTypes[] = {
    {0x21FC3301, CodeAddress(&GameObjects::CRiderStanceDisplay::Fabricate), nullptr},
    {0xB5F1E9D2, CodeAddress(&GameObjects::CRiderRollingAudio::Fabricate), CodeAddress(&GameObjects::CRiderRollingAudio::PublishMethodTable)}, // SkaterLoopingSound
    {0x187574FA, CodeAddress(&GameObjects::CRiderSfxPlayer::Fabricate), nullptr}, // SkaterSound
    {0x11B4C67F, CodeAddress(&GameObjects::CRiderGapTracker::Fabricate), nullptr}, // SkaterGap
    {0x52D61A0D, CodeAddress(&GameObjects::CRiderTurning::Fabricate), nullptr}, // SkaterRotate
    {0xD9151D4D, CodeAddress(&GameObjects::CRiderBoardPhysics::Fabricate), CodeAddress(&GameObjects::CRiderBoardPhysics::PublishMethodTable)}, // SkaterPhysicsControl
    {0x7CD3E6D5, CodeAddress(&GameObjects::CRiderHostSync::Fabricate), nullptr}, // SkaterLocalNetLogic
    {0x850EEF66, CodeAddress(&GameObjects::CRiderRemoteSync::Fabricate), nullptr}, // SkaterNonLocalNetLogic
    {0xD262802F, CodeAddress(&GameObjects::CRiderPointKeeper::Fabricate), nullptr}, // SkaterScore
    {0xBF518E27, CodeAddress(&GameObjects::CRiderFrameLookups::Fabricate), nullptr}, // SkaterMatrixQueries
    {0x125044E2, CodeAddress(&GameObjects::CRiderOverheadTag::Fabricate), nullptr}, // SkaterFloatingName
    {0x18223FD6, CodeAddress(&GameObjects::CRiderFrameLog::Fabricate), nullptr}, // SkaterStateHistory
    {0x5BD63B29, CodeAddress(&Skater::SkaterPhysicsModule::Fabricate), CodeAddress(&Skater::SkaterPhysicsModule::PublishMethodTable)}, // SkaterCorePhysics
    {0x9F9CC949, CodeAddress(&GameObjects::CRiderPhysicsTweaks::Fabricate), nullptr}, // SkaterAdjustPhysics
    {0x9B373E58, CodeAddress(&GameObjects::CRiderPhysicsCommit::Fabricate), nullptr}, // SkaterFinalizePhysics
    {0xD8CC3428, CodeAddress(&CRiderStateReset::Fabricate), nullptr}, // SkaterCleanupState
    {0xB1D5A3E1, CodeAddress(&GameObjects::CRiderSessionEnd::Fabricate), nullptr}, // SkaterEndRun
    {0x589E5FB3, CodeAddress(&GameObjects::CRiderTeeterMoves::Fabricate), CodeAddress(&GameObjects::CRiderTeeterMoves::PublishMethodTable)}, // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, CodeAddress(&GameObjects::CRiderSpinAndFlip::Fabricate), nullptr}, // SkaterFlipAndRotate
    {0x43686585, CodeAddress(&GameObjects::CRiderPoseState::Fabricate), CodeAddress(&GameObjects::CRiderPoseState::PublishMethodTable)}, // SkaterState
    {0x81F01058, CodeAddress(&ParkEditing::CObjectiveAuthoring::Fabricate), CodeAddress(&ParkEditing::CObjectiveAuthoring::PublishMethodTable)},
    {0x5B509AD3, CodeAddress(&ParkEditing::CRailAuthoring::Fabricate), nullptr},
    {0x1D5B1AE6, CodeAddress(&GameObjects::CObjectProximityComponent::Fabricate), CodeAddress(&GameObjects::CObjectProximityComponent::PublishMethodTable)},
    {0xAC8946E2, CodeAddress(&GameObjects::CEditorViewModule::Fabricate), nullptr},
    {0x8BCE7E66, CodeAddress(&GameObjects::CRiderRunClock::Fabricate), CodeAddress(&GameObjects::CRiderRunClock::PublishMethodTable)}, // SkaterRunTimer
    {0x1859F772, CodeAddress(&GameObjects::CSkaterVehicleSoundComponent::Fabricate), CodeAddress(&GameObjects::CSkaterVehicleSoundComponent::PublishMethodTable)},
    {0x380D909A, CodeAddress(&GameObjects::CTaggingControlComponent::Fabricate), nullptr},
    {0x5B512EDC, CodeAddress(&GameObjects::CTaggingComponent::Fabricate), nullptr}, // Tagging
    {0xD1515392, CodeAddress(&GameObjects::CPagerMessageComponent::Fabricate), CodeAddress(&GameObjects::CPagerMessageComponent::PublishMethodTable)},
    {0x286A8D26, CodeAddress(&GameObjects::CMeshInstanceModule::Fabricate), nullptr}, // Model
    {0xE3FCA939, CodeAddress(&GameObjects::CMeshLightingModule::Fabricate), nullptr}, // ModelLightUpdate
    {0x222756D5, CodeAddress(&GameObjects::CBoneRigModule::Fabricate), CodeAddress(&GameObjects::CBoneRigModule::PublishMethodTable)}, // the skeleton
    {0x7149B753, CodeAddress(&GameObjects::CAnimBlenderComponent::Fabricate),
     CodeAddress(&GameObjects::CAnimBlenderComponent::PublishMethodTable)}, // the animation blender
    {0x07713C7B, CodeAddress(&GameObjects::CAudioEmitterModule::Fabricate), CodeAddress(&GameObjects::CAudioEmitterModule::PublishMethodTable)}, // Sound
    {0xA432DC41, CodeAddress(&GameObjects::CContactShapeModule::Fabricate), nullptr}, // Collision
    {0x5CB5CB7E, CodeAddress(&GameObjects::CPinnedObjectModule::Fabricate), nullptr},
    {0xC695136D, CodeAddress(&GameObjects::CAutomobileDynamicsModule::Fabricate), CodeAddress(&GameObjects::CAutomobileDynamicsModule::PublishMethodTable)}, // CarPhysics
    {0x0A015E17, CodeAddress(&GameObjects::CKinematicsModule::Fabricate), CodeAddress(&GameObjects::CMotionPathMembers::PublishMethodTable)}, // Motion
    {0xCE0CA665, CodeAddress(&GameObjects::CDormancyModule::Fabricate), CodeAddress(&GameObjects::CDormancyModule::PublishMethodTable)}, // Suspend
    {0x270F56E1, CodeAddress(&GameObjects::CMoveExecutor::Fabricate), CodeAddress(&GameObjects::CMoveExecutor::PublishMethodTable)}, // Trick
    {0x6EB050ED, CodeAddress(&GameObjects::CObstacleDodgeModule::Fabricate), CodeAddress(&GameObjects::CObstacleDodgeModule::PublishMethodTable)}, // Avoid
    {0x6EFDDFB8, CodeAddress(&GameObjects::CPedestrianBrainModule::Fabricate), CodeAddress(&GameObjects::CPedestrianBrainModule::PublishMethodTable)}, // PedLogic
    {0xFFB39248, CodeAddress(&GameObjects::CBodyDynamicsModule::Fabricate), CodeAddress(&CBodyDynamicsModule::PublishMethodTable)}, // Rigidbody
    {0x6DCFEC46, CodeAddress(&CChainComponent::Fabricate), nullptr},
    {0x8A897DD2, CodeAddress(&GameObjects::CGroundShadowModule::Fabricate), nullptr}, // Shadow
    {0x0F1641E3, CodeAddress(&GameObjects::CAudioStreamModule::Fabricate), CodeAddress(&GameObjects::CAudioStreamModule::PublishMethodTable)}, // Stream
    {0xF27A294D, CodeAddress(&GameObjects::CPickupItemHandler::Fabricate), nullptr}, // SpecialItem
    {0xE47F1B79, CodeAddress(&GameObjects::CDrivableModule::Fabricate), CodeAddress(&GameObjects::RegisterVehicleMemberFunctions)}, // Vehicle
    {0x2C747D8A, CodeAddress(&GameObjects::CCarView::Fabricate), CodeAddress(&GameObjects::CCarView::PublishMethodTable)}, // VehicleCamera
    {0xC472ECC5, CodeAddress(&CWaypointListModule::Fabricate), nullptr},
    {0x1CDFB2A3, CodeAddress(&CGrindPathLinkModule::Fabricate), nullptr},
    {0x27443708, CodeAddress(&CHookPointRegistryModule::Fabricate), nullptr},
    {0x3506CE64, CodeAddress(&GameObjects::CTowRideHandler::Fabricate), nullptr}, // Skitch
    {0xC4E311FA, CodeAddress(&GameObjects::CViewpointModule::Fabricate), nullptr}, // Camera
    {0x5E43A604, CodeAddress(&CRiderChaseView::Fabricate), nullptr}, // SkaterCamera
    {0x404EB936, CodeAddress(&GameObjects::CPadRumbleModule::Fabricate), CodeAddress(&GameObjects::CPadRumbleModule::PublishMethodTable)},
    {0xE594F0A2, CodeAddress(&GameObjects::CActivationZoneModule::Fabricate), nullptr}, // Trigger
    {0xEADA1621, CodeAddress(&GameObjects::CFloatingTextComponent::Fabricate), nullptr},
    {0x27D7CD28, CodeAddress(&GameObjects::CPadInputModule::Fabricate), CodeAddress(&GameObjects::CPadInputModule::PublishMethodTable)}, // Input
    {0xA9DB601E, CodeAddress(&CEffectsEmitterModule::Fabricate), CodeAddress(&CEffectsEmitterModule::PublishMethodTable)}, // Particle
    {0xC5BCEB9E, CodeAddress(&GameObjects::CFlexibleParticleComponent::Fabricate), CodeAddress(&GameObjects::CFlexibleParticleComponent::PublishMethodTable)},
    {0x726E85AA, CodeAddress(&CFootMovement::Fabricate), CodeAddress(&GameObjects::CFootMovement::PublishMethodTable)}, // Walk
    {0xC7D37EC2, CodeAddress(&GameObjects::CFootView::Fabricate), nullptr}, // WalkCamera
    {0x7505D7CC, CodeAddress(&GameObjects::CTextureProjectionComponent::Fabricate), nullptr},
    {0x81C7122D, CodeAddress(&GameObjects::CFreeLookModule::Fabricate), nullptr}, // CameraLookAround
    {0x408D2AA9, CodeAddress(&CMovingSurfaceModule::Fabricate), nullptr}, // MovableContact
    {0x4FF8DAC6, CodeAddress(&GameObjects::CAttributeTracker::Fabricate), CodeAddress(&GameObjects::CAttributeTracker::PublishMethodTable)},
    {0x28C97F3C, CodeAddress(&GameObjects::CEngineAudioModule::Fabricate), nullptr}, // VehicleSound
    {0x41272956, CodeAddress(&GameObjects::CVelocityTrackerModule::Fabricate), nullptr}, // Velocity
    {0x6259B52B, CodeAddress(&GameObjects::CImpactDestructModule::Fabricate), nullptr},
    {0x7767D6D7, CodeAddress(&GameObjects::CShotImpactModule::Fabricate), nullptr},
    {0x1BBF844D, CodeAddress(&GameObjects::CFaceHeadingModule::Fabricate), nullptr},
    {0x9649D4F9, CodeAddress(&GameObjects::CParkedVehicleModule::Fabricate), nullptr}, // StaticVehicle
    {0xCDC76A22, CodeAddress(&GameObjects::CColorCycleComponent::Fabricate), CodeAddress(&GameObjects::CColorCycleComponent::PublishMethodTable)},
    {0xB87C8E43, CodeAddress(&GameObjects::CBBoxComponent::Fabricate), nullptr},
    {0x3618B878, CodeAddress(&GameObjects::CProjectileProximityComponent::Fabricate), nullptr},
    {0x7E747547, CodeAddress(&GameObjects::CProjectileComponent::Fabricate), nullptr}, // (the projectile component)
    {0x03AC8065, CodeAddress(&GameObjects::CreateWaitAndDieComponent), nullptr},
    {0xD8BFC9F7, CodeAddress(&GameObjects::CDynamicLightComponent::Fabricate), nullptr},
    {0x208353AC, CodeAddress(&GameObjects::CMatrixComponent::Fabricate), nullptr},
    {0x0301D67C, CodeAddress(&GameObjects::CFamComponent::Fabricate), CodeAddress(&GameObjects::CFamComponent::PublishMethodTable)},
};

} // namespace

// 0x004E4860
void __cdecl RegisterComponentTypes()
{
    CEntityHub* manager = ObjectHubSlot();
    for (const ComponentType& t : kComponentTypes) {
        manager->EnrolModule(t.type, reinterpret_cast<ComponentCreateFunction>(t.create),
                                   reinterpret_cast<ComponentRegisterFunction>(t.registerFunctions));
    }
}

} // namespace Obj
