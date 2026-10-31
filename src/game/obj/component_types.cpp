// The component types every composite object can be made of, registered with the composite object manager.

#include "game/obj/component_types.h"

#include <cstdint>

#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
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
#include "game/skater/gap.h"
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

namespace Obj {

namespace {

// A component type: its checksum, the function that makes one and the one that registers its script member
// functions (if any). Names are given where the checksum's name is known.
struct ComponentType {
    uint32_t type;
    void* create;
    void* registerFunctions;
};

// In the original's order (CreateComponent finds the first of a type).
const ComponentType kComponentTypes[] = {
    {0x21FC3301, CodeAddress(&Obj::CSkaterStancePanelComponent::Create), nullptr},
    {0xB5F1E9D2, CodeAddress(&Obj::CSkaterLoopingSoundComponent::Create), CodeAddress(&Obj::CSkaterLoopingSoundComponent::RegisterMemberFunctions)}, // SkaterLoopingSound
    {0x187574FA, CodeAddress(&Obj::CSkaterSoundComponent::Create), nullptr}, // SkaterSound
    {0x11B4C67F, CodeAddress(&Obj::CSkaterGapComponent::Create), nullptr}, // SkaterGap
    {0x52D61A0D, CodeAddress(&Obj::CSkaterRotateComponent::Create), nullptr}, // SkaterRotate
    {0xD9151D4D, CodeAddress(&Obj::CSkaterPhysicsControlComponent::Create), CodeAddress(&Obj::CSkaterPhysicsControlComponent::RegisterMemberFunctions)}, // SkaterPhysicsControl
    {0x7CD3E6D5, CodeAddress(&Obj::CSkaterLocalNetLogicComponent::Create), nullptr}, // SkaterLocalNetLogic
    {0x850EEF66, CodeAddress(&Obj::CSkaterNonLocalNetLogicComponent::Create), nullptr}, // SkaterNonLocalNetLogic
    {0xD262802F, CodeAddress(&Obj::CSkaterScoreComponent::Create), nullptr}, // SkaterScore
    {0xBF518E27, CodeAddress(&Obj::CSkaterMatrixQueriesComponent::Create), nullptr}, // SkaterMatrixQueries
    {0x125044E2, CodeAddress(&Obj::CSkaterFloatingNameComponent::Create), nullptr}, // SkaterFloatingName
    {0x18223FD6, CodeAddress(&Obj::CSkaterStateHistoryComponent::Create), nullptr}, // SkaterStateHistory
    {0x5BD63B29, CodeAddress(&Skater::CorePhysicsComponent::Create), CodeAddress(&Skater::CorePhysicsComponent::RegisterMemberFunctions)}, // SkaterCorePhysics
    {0x9F9CC949, CodeAddress(&Obj::CSkaterAdjustPhysicsComponent::Create), nullptr}, // SkaterAdjustPhysics
    {0x9B373E58, CodeAddress(&Obj::CSkaterFinalizePhysicsComponent::Create), nullptr}, // SkaterFinalizePhysics
    {0xD8CC3428, CodeAddress(&CSkaterCleanupStateComponent::Create), nullptr}, // SkaterCleanupState
    {0xB1D5A3E1, CodeAddress(&Obj::CSkaterEndRunComponent::Create), nullptr}, // SkaterEndRun
    {0x589E5FB3, CodeAddress(&Obj::CSkaterBalanceTrickComponent::Create), CodeAddress(&Obj::CSkaterBalanceTrickComponent::RegisterMemberFunctions)}, // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, CodeAddress(&Obj::CSkaterFlipAndRotateComponent::Create), nullptr}, // SkaterFlipAndRotate
    {0x43686585, CodeAddress(&Obj::CSkaterStateComponent::Create), CodeAddress(&Obj::CSkaterStateComponent::RegisterMemberFunctions)}, // SkaterState
    {0x81F01058, CodeAddress(&Ed::CGoalEditorComponent::Create), CodeAddress(&Ed::CGoalEditorComponent::RegisterMemberFunctions)},
    {0x5B509AD3, CodeAddress(&Ed::CRailEditorComponent::Create), nullptr},
    {0x1D5B1AE6, CodeAddress(&Obj::CObjectProximityComponent::Create), CodeAddress(&Obj::CObjectProximityComponent::RegisterMemberFunctions)},
    {0xAC8946E2, CodeAddress(&Obj::CEditorCameraComponent::Create), nullptr},
    {0x8BCE7E66, CodeAddress(&Obj::CSkaterRunTimerComponent::Create), CodeAddress(&Obj::CSkaterRunTimerComponent::RegisterMemberFunctions)}, // SkaterRunTimer
    {0x1859F772, CodeAddress(&Obj::CSkaterVehicleSoundComponent::Create), CodeAddress(&Obj::CSkaterVehicleSoundComponent::RegisterMemberFunctions)},
    {0x380D909A, CodeAddress(&Obj::CTaggingControlComponent::Create), nullptr},
    {0x5B512EDC, CodeAddress(&Obj::CTaggingComponent::Create), nullptr}, // Tagging
    {0xD1515392, CodeAddress(&Obj::CPagerMessageComponent::Create), CodeAddress(&Obj::CPagerMessageComponent::RegisterMemberFunctions)},
    {0x286A8D26, CodeAddress(&Obj::CModelComponent::Create), nullptr}, // Model
    {0xE3FCA939, CodeAddress(&Obj::CModelLightUpdateComponent::Create), nullptr}, // ModelLightUpdate
    {0x222756D5, CodeAddress(&Obj::CSkeletonComponent::Create), CodeAddress(&Obj::CSkeletonComponent::RegisterMemberFunctions)}, // the skeleton
    {0x7149B753, CodeAddress(&Obj::CAnimBlenderComponent::Create),
     CodeAddress(&Obj::CAnimBlenderComponent::RegisterMemberFunctions)}, // the animation blender
    {0x07713C7B, CodeAddress(&Obj::CSoundComponent::Create), CodeAddress(&Obj::CSoundComponent::RegisterMemberFunctions)}, // Sound
    {0xA432DC41, CodeAddress(&Obj::CCollisionComponent::Create), nullptr}, // Collision
    {0x5CB5CB7E, CodeAddress(&Obj::CLockObjComponent::Create), nullptr},
    {0xC695136D, CodeAddress(&Obj::CCarPhysicsComponent::Create), CodeAddress(&Obj::CCarPhysicsComponent::RegisterMemberFunctions)}, // CarPhysics
    {0x0A015E17, CodeAddress(&Obj::CMotionComponent::Create), CodeAddress(&Obj::CMotionPathMembers::RegisterMemberFunctions)}, // Motion
    {0xCE0CA665, CodeAddress(&Obj::CSuspendComponent::Create), CodeAddress(&Obj::CSuspendComponent::RegisterMemberFunctions)}, // Suspend
    {0x270F56E1, CodeAddress(&Obj::CTrickComponent::Create), CodeAddress(&Obj::CTrickComponent::RegisterMemberFunctions)}, // Trick
    {0x6EB050ED, CodeAddress(&Obj::CAvoidComponent::Create), CodeAddress(&Obj::CAvoidComponent::RegisterMemberFunctions)}, // Avoid
    {0x6EFDDFB8, CodeAddress(&Obj::CPedLogicComponent::Create), CodeAddress(&Obj::CPedLogicComponent::RegisterMemberFunctions)}, // PedLogic
    {0xFFB39248, CodeAddress(&Obj::CRigidBodyComponent::Create), CodeAddress(&CRigidBodyComponent::RegisterMemberFunctions)}, // Rigidbody
    {0x6DCFEC46, CodeAddress(&CChainComponent::Create), nullptr},
    {0x8A897DD2, CodeAddress(&Obj::CShadowComponent::Create), nullptr}, // Shadow
    {0x0F1641E3, CodeAddress(&Obj::CStreamComponent::Create), CodeAddress(&Obj::CStreamComponent::RegisterMemberFunctions)}, // Stream
    {0xF27A294D, CodeAddress(&Obj::CSpecialItemComponent::Create), nullptr}, // SpecialItem
    {0xE47F1B79, CodeAddress(&Obj::CVehicleComponent::Create), CodeAddress(&Obj::RegisterVehicleMemberFunctions)}, // Vehicle
    {0x2C747D8A, CodeAddress(&Obj::CVehicleCameraComponent::Create), CodeAddress(&Obj::CVehicleCameraComponent::RegisterMemberFunctions)}, // VehicleCamera
    {0xC472ECC5, CodeAddress(&CNodeArrayComponent::Create), nullptr},
    {0x1CDFB2A3, CodeAddress(&CRailManagerComponent::Create), nullptr},
    {0x27443708, CodeAddress(&CObjectHookManagerComponent::Create), nullptr},
    {0x3506CE64, CodeAddress(&Obj::CSkitchComponent::Create), nullptr}, // Skitch
    {0xC4E311FA, CodeAddress(&Obj::CCameraComponent::Create), nullptr}, // Camera
    {0x5E43A604, CodeAddress(&CSkaterCameraComponent::Create), nullptr}, // SkaterCamera
    {0x404EB936, CodeAddress(&Obj::CVibrationComponent::Create), CodeAddress(&Obj::CVibrationComponent::RegisterMemberFunctions)},
    {0xE594F0A2, CodeAddress(&Obj::CTriggerComponent::Create), nullptr}, // Trigger
    {0xEADA1621, CodeAddress(&Obj::CFloatingTextComponent::Create), nullptr},
    {0x27D7CD28, CodeAddress(&Obj::CInputComponent::Create), CodeAddress(&Obj::CInputComponent::RegisterMemberFunctions)}, // Input
    {0xA9DB601E, CodeAddress(&CParticleComponent::Create), CodeAddress(&CParticleComponent::RegisterMemberFunctions)}, // Particle
    {0xC5BCEB9E, CodeAddress(&Obj::CFlexibleParticleComponent::Create), CodeAddress(&Obj::CFlexibleParticleComponent::RegisterMemberFunctions)},
    {0x726E85AA, CodeAddress(&CWalkComponent::Create), CodeAddress(&Obj::CWalkComponent::RegisterMemberFunctions)}, // Walk
    {0xC7D37EC2, CodeAddress(&Obj::CWalkCameraComponent::Create), nullptr}, // WalkCamera
    {0x7505D7CC, CodeAddress(&Obj::CTextureProjectionComponent::Create), nullptr},
    {0x81C7122D, CodeAddress(&Obj::CCameraLookAroundComponent::Create), nullptr}, // CameraLookAround
    {0x408D2AA9, CodeAddress(&CMovableContactComponent::Create), nullptr}, // MovableContact
    {0x4FF8DAC6, CodeAddress(&Obj::CStatsManagerComponent::Create), CodeAddress(&Obj::CStatsManagerComponent::RegisterMemberFunctions)},
    {0x28C97F3C, CodeAddress(&Obj::CVehicleSoundComponent::Create), nullptr}, // VehicleSound
    {0x41272956, CodeAddress(&Obj::CVelocityComponent::Create), nullptr}, // Velocity
    {0x6259B52B, CodeAddress(&Obj::CCollideAndDieComponent::Create), nullptr},
    {0x7767D6D7, CodeAddress(&Obj::CProjectileCollisionComponent::Create), nullptr},
    {0x1BBF844D, CodeAddress(&Obj::CDisplayMatrixComponent::Create), nullptr},
    {0x9649D4F9, CodeAddress(&Obj::CStaticVehicleComponent::Create), nullptr}, // StaticVehicle
    {0xCDC76A22, CodeAddress(&Obj::CColorCycleComponent::Create), CodeAddress(&Obj::CColorCycleComponent::RegisterMemberFunctions)},
    {0xB87C8E43, CodeAddress(&Obj::CBBoxComponent::Create), nullptr},
    {0x3618B878, CodeAddress(&Obj::CProjectileProximityComponent::Create), nullptr},
    {0x7E747547, CodeAddress(&Obj::CProjectileComponent::Create), nullptr}, // (the projectile component)
    {0x03AC8065, CodeAddress(&Obj::CreateWaitAndDieComponent), nullptr},
    {0xD8BFC9F7, CodeAddress(&Obj::CDynamicLightComponent::Create), nullptr},
    {0x208353AC, CodeAddress(&Obj::CMatrixComponent::Create), nullptr},
    {0x0301D67C, CodeAddress(&Obj::CFamComponent::Create), CodeAddress(&Obj::CFamComponent::RegisterMemberFunctions)},
};

} // namespace

// 0x004E4860
void __cdecl RegisterComponentTypes()
{
    CCompositeObjectManager* manager = CompositeObjectManager();
    for (const ComponentType& t : kComponentTypes) {
        manager->RegisterComponent(t.type, reinterpret_cast<ComponentCreateFunction>(t.create),
                                   reinterpret_cast<ComponentRegisterFunction>(t.registerFunctions));
    }
}

} // namespace Obj
