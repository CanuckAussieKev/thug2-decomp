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
#include "hook/game.h"
#include "hook/game_code.h"
#include "hook/hook.h"

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
    {0x21FC3301, GAME_CODE(0x005193B0, &Obj::CSkaterStancePanelComponent::Create), nullptr},
    {0xB5F1E9D2, GAME_CODE(0x00513AD0, &Obj::CSkaterLoopingSoundComponent::Create), GAME_CODE(0x00513860, &Obj::CSkaterLoopingSoundComponent::RegisterMemberFunctions)}, // SkaterLoopingSound
    {0x187574FA, GAME_CODE(0x00518E20, &Obj::CSkaterSoundComponent::Create), nullptr}, // SkaterSound
    {0x11B4C67F, GAME_CODE(0x00512C60, &Obj::CSkaterGapComponent::Create), nullptr}, // SkaterGap
    {0x52D61A0D, GAME_CODE(0x00517D40, &Obj::CSkaterRotateComponent::Create), nullptr}, // SkaterRotate
    {0xD9151D4D, GAME_CODE(0x00517830, &Obj::CSkaterPhysicsControlComponent::Create), GAME_CODE(0x005176A0, &Obj::CSkaterPhysicsControlComponent::RegisterMemberFunctions)}, // SkaterPhysicsControl
    {0x7CD3E6D5, GAME_CODE(0x00513630, &Obj::CSkaterLocalNetLogicComponent::Create), nullptr}, // SkaterLocalNetLogic
    {0x850EEF66, GAME_CODE(0x005147F0, &Obj::CSkaterNonLocalNetLogicComponent::Create), nullptr}, // SkaterNonLocalNetLogic
    {0xD262802F, GAME_CODE(0x00518B60, &Obj::CSkaterScoreComponent::Create), nullptr}, // SkaterScore
    {0xBF518E27, GAME_CODE(0x00514430, &Obj::CSkaterMatrixQueriesComponent::Create), nullptr}, // SkaterMatrixQueries
    {0x125044E2, GAME_CODE(0x00511810, &Obj::CSkaterFloatingNameComponent::Create), nullptr}, // SkaterFloatingName
    {0x18223FD6, GAME_CODE(0x0051AD30, &Obj::CSkaterStateHistoryComponent::Create), nullptr}, // SkaterStateHistory
    {0x5BD63B29, GAME_CODE(0x00505440, &Skater::CorePhysicsComponent::Create), GAME_CODE(0x004FBE80, &Skater::CorePhysicsComponent::RegisterMemberFunctions)}, // SkaterCorePhysics
    {0x9F9CC949, GAME_CODE(0x004F80E0, &Obj::CSkaterAdjustPhysicsComponent::Create), nullptr}, // SkaterAdjustPhysics
    {0x9B373E58, GAME_CODE(0x00511280, &Obj::CSkaterFinalizePhysicsComponent::Create), nullptr}, // SkaterFinalizePhysics
    {0xD8CC3428, GAME_CODE(0x004FBE20, &CSkaterCleanupStateComponent::Create), nullptr}, // SkaterCleanupState
    {0xB1D5A3E1, GAME_CODE(0x00510F90, &Obj::CSkaterEndRunComponent::Create), nullptr}, // SkaterEndRun
    {0x589E5FB3, GAME_CODE(0x004F9170, &Obj::CSkaterBalanceTrickComponent::Create), GAME_CODE(0x004F8CD0, &Obj::CSkaterBalanceTrickComponent::RegisterMemberFunctions)}, // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, GAME_CODE(0x00511470, &Obj::CSkaterFlipAndRotateComponent::Create), nullptr}, // SkaterFlipAndRotate
    {0x43686585, GAME_CODE(0x00519740, &Obj::CSkaterStateComponent::Create), GAME_CODE(0x00519430, &Obj::CSkaterStateComponent::RegisterMemberFunctions)}, // SkaterState
    {0x81F01058, GAME_CODE(0x004EB7F0, &Ed::CGoalEditorComponent::Create), GAME_CODE(0x004E6FB0, &Ed::CGoalEditorComponent::RegisterMemberFunctions)},
    {0x5B509AD3, GAME_CODE(0x004F7FB0, &Ed::CRailEditorComponent::Create), nullptr},
    {0x1D5B1AE6, GAME_CODE(0x004EC420, &Obj::CObjectProximityComponent::Create), GAME_CODE(0x004EC220, &Obj::CObjectProximityComponent::RegisterMemberFunctions)},
    {0xAC8946E2, GAME_CODE(0x004E5940, &Obj::CEditorCameraComponent::Create), nullptr},
    {0x8BCE7E66, GAME_CODE(0x005185D0, &Obj::CSkaterRunTimerComponent::Create), GAME_CODE(0x00518240, &Obj::CSkaterRunTimerComponent::RegisterMemberFunctions)}, // SkaterRunTimer
    {0x1859F772, GAME_CODE(0x0051B880, &Obj::CSkaterVehicleSoundComponent::Create), GAME_CODE(0x0051AD90, &Obj::CSkaterVehicleSoundComponent::RegisterMemberFunctions)},
    {0x380D909A, GAME_CODE(0x0051E280, &Obj::CTaggingControlComponent::Create), nullptr},
    {0x5B512EDC, GAME_CODE(0x0051DA80, &Obj::CTaggingComponent::Create), nullptr}, // Tagging
    {0xD1515392, GAME_CODE(0x004ECF20, &Obj::CPagerMessageComponent::Create), GAME_CODE(0x004EC960, &Obj::CPagerMessageComponent::RegisterMemberFunctions)},
    {0x286A8D26, GAME_CODE(0x00421490, &Obj::CModelComponent::Create), nullptr}, // Model
    {0xE3FCA939, GAME_CODE(0x00422DC0, &Obj::CModelLightUpdateComponent::Create), nullptr}, // ModelLightUpdate
    {0x222756D5, GAME_CODE(0x00431F60, &Obj::CSkeletonComponent::Create), GAME_CODE(0x00431BE0, &Obj::CSkeletonComponent::RegisterMemberFunctions)}, // the skeleton
    {0x7149B753, GAME_CODE(0x0041A5B0, &Obj::CAnimBlenderComponent::Create),
     GAME_CODE(0x00419690, &Obj::CAnimBlenderComponent::RegisterMemberFunctions)}, // the animation blender
    {0x07713C7B, GAME_CODE(0x00433100, &Obj::CSoundComponent::Create), GAME_CODE(0x00432C80, &Obj::CSoundComponent::RegisterMemberFunctions)}, // Sound
    {0xA432DC41, GAME_CODE(0x0041C920, &Obj::CCollisionComponent::Create), nullptr}, // Collision
    {0x5CB5CB7E, GAME_CODE(0x0041FF20, &Obj::CLockObjComponent::Create), nullptr},
    {0xC695136D, GAME_CODE(0x0041BD90, &Obj::CCarPhysicsComponent::Create), GAME_CODE(0x0041B910, &Obj::CCarPhysicsComponent::RegisterMemberFunctions)}, // CarPhysics
    {0x0A015E17, GAME_CODE(0x00426650, &Obj::CMotionComponent::Create), GAME_CODE(0x00423130, &Obj::CMotionPathMembers::RegisterMemberFunctions)}, // Motion
    {0xCE0CA665, GAME_CODE(0x00434290, &Obj::CSuspendComponent::Create), GAME_CODE(0x00434610, &Obj::CSuspendComponent::RegisterMemberFunctions)}, // Suspend
    {0x270F56E1, GAME_CODE(0x00523490, &Obj::CTrickComponent::Create), GAME_CODE(0x0051EA90, &Obj::CTrickComponent::RegisterMemberFunctions)}, // Trick
    {0x6EB050ED, GAME_CODE(0x004E39E0, &Obj::CAvoidComponent::Create), GAME_CODE(0x004E3310, &Obj::CAvoidComponent::RegisterMemberFunctions)}, // Avoid
    {0x6EFDDFB8, GAME_CODE(0x004F0B10, &Obj::CPedLogicComponent::Create), GAME_CODE(0x004ECFB0, &Obj::CPedLogicComponent::RegisterMemberFunctions)}, // PedLogic
    {0xFFB39248, GAME_CODE(0x0042EF00, &Obj::CRigidBodyComponent::Create), GAME_CODE(0x0042BB00, &CRigidBodyComponent::RegisterMemberFunctions)}, // Rigidbody
    {0x6DCFEC46, GAME_CODE(0x004298B0, &CChainComponent::Create), nullptr},
    {0x8A897DD2, GAME_CODE(0x00431800, &Obj::CShadowComponent::Create), nullptr}, // Shadow
    {0x0F1641E3, GAME_CODE(0x00433D90, &Obj::CStreamComponent::Create), GAME_CODE(0x00433870, &Obj::CStreamComponent::RegisterMemberFunctions)}, // Stream
    {0xF27A294D, GAME_CODE(0x0051BD80, &Obj::CSpecialItemComponent::Create), nullptr}, // SpecialItem
    {0xE47F1B79, GAME_CODE(0x0043AA30, &Obj::CVehicleComponent::Create), GAME_CODE(0x00434F00, &Obj::RegisterVehicleMemberFunctions)}, // Vehicle
    {0x2C747D8A, GAME_CODE(0x005248C0, &Obj::CVehicleCameraComponent::Create), GAME_CODE(0x00524420, &Obj::CVehicleCameraComponent::RegisterMemberFunctions)}, // VehicleCamera
    {0xC472ECC5, GAME_CODE(0x00428010, &CNodeArrayComponent::Create), nullptr},
    {0x1CDFB2A3, GAME_CODE(0x0042B920, &CRailManagerComponent::Create), nullptr},
    {0x27443708, GAME_CODE(0x00428130, &CObjectHookManagerComponent::Create), nullptr},
    {0x3506CE64, GAME_CODE(0x0051BA10, &Obj::CSkitchComponent::Create), nullptr}, // Skitch
    {0xC4E311FA, GAME_CODE(0x0041B840, &Obj::CCameraComponent::Create), nullptr}, // Camera
    {0x5E43A604, GAME_CODE(0x004FA800, &CSkaterCameraComponent::Create), nullptr}, // SkaterCamera
    {0x404EB936, GAME_CODE(0x0043D670, &Obj::CVibrationComponent::Create), GAME_CODE(0x0043D490, &Obj::CVibrationComponent::RegisterMemberFunctions)},
    {0xE594F0A2, GAME_CODE(0x00434B90, &Obj::CTriggerComponent::Create), nullptr}, // Trigger
    {0xEADA1621, GAME_CODE(0x0041E3C0, &Obj::CFloatingTextComponent::Create), nullptr},
    {0x27D7CD28, GAME_CODE(0x0041EEC0, &Obj::CInputComponent::Create), GAME_CODE(0x0041E6F0, &Obj::CInputComponent::RegisterMemberFunctions)}, // Input
    {0xA9DB601E, GAME_CODE(0x0042A000, &CParticleComponent::Create), GAME_CODE(0x00429940, &CParticleComponent::RegisterMemberFunctions)}, // Particle
    {0xC5BCEB9E, GAME_CODE(0x0041E1E0, &Obj::CFlexibleParticleComponent::Create), GAME_CODE(0x0041D6E0, &Obj::CFlexibleParticleComponent::RegisterMemberFunctions)},
    {0x726E85AA, GAME_CODE(0x0052A5C0, &CWalkComponent::Create), GAME_CODE(0x00525E40, &Obj::CWalkComponent::RegisterMemberFunctions)}, // Walk
    {0xC7D37EC2, GAME_CODE(0x00525DC0, &Obj::CWalkCameraComponent::Create), nullptr}, // WalkCamera
    {0x7505D7CC, GAME_CODE(0x00434870, &Obj::CTextureProjectionComponent::Create), nullptr},
    {0x81C7122D, GAME_CODE(0x004E42E0, &Obj::CCameraLookAroundComponent::Create), nullptr}, // CameraLookAround
    {0x408D2AA9, GAME_CODE(0x00427AB0, &CMovableContactComponent::Create), nullptr}, // MovableContact
    {0x4FF8DAC6, GAME_CODE(0x0051CB90, &Obj::CStatsManagerComponent::Create), GAME_CODE(0x0051C440, &Obj::CStatsManagerComponent::RegisterMemberFunctions)},
    {0x28C97F3C, GAME_CODE(0x0043CFC0, &Obj::CVehicleSoundComponent::Create), nullptr}, // VehicleSound
    {0x41272956, GAME_CODE(0x0043D430, &Obj::CVelocityComponent::Create), nullptr}, // Velocity
    {0x6259B52B, GAME_CODE(0x0041C370, &Obj::CCollideAndDieComponent::Create), nullptr},
    {0x7767D6D7, GAME_CODE(0x004F3010, &Obj::CProjectileCollisionComponent::Create), nullptr},
    {0x1BBF844D, GAME_CODE(0x004312A0, &Obj::CDisplayMatrixComponent::Create), nullptr},
    {0x9649D4F9, GAME_CODE(0x00433690, &Obj::CStaticVehicleComponent::Create), nullptr}, // StaticVehicle
    {0xCDC76A22, GAME_CODE(0x0041B3C0, &Obj::CColorCycleComponent::Create), GAME_CODE(0x0041B280, &Obj::CColorCycleComponent::RegisterMemberFunctions)},
    {0xB87C8E43, GAME_CODE(0x0041ADC0, &Obj::CBBoxComponent::Create), nullptr},
    {0x3618B878, GAME_CODE(0x004F3550, &Obj::CProjectileProximityComponent::Create), nullptr},
    {0x7E747547, GAME_CODE(0x004F3890, &Obj::CProjectileComponent::Create), nullptr}, // (the projectile component)
    {0x03AC8065, GAME_CODE(0x0043D930, &Obj::CreateWaitAndDieComponent), nullptr},
    {0xD8BFC9F7, GAME_CODE(0x0041D0F0, &Obj::CDynamicLightComponent::Create), nullptr},
    {0x208353AC, GAME_CODE(0x0041CB20, &Obj::CMatrixComponent::Create), nullptr},
    {0x0301D67C, GAME_CODE(0x0041D200, &Obj::CFamComponent::Create), GAME_CODE(0x0041D170, &Obj::CFamComponent::RegisterMemberFunctions)},
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

REPLACE(0x004E4860, Obj::RegisterComponentTypes);
