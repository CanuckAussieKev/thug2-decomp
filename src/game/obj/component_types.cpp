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

// The address of one of our functions (the original's address in the hooked build).
#define CODE(address, function) reinterpret_cast<uint32_t>(GAME_CODE(address, function))

namespace Obj {

namespace {

// A component type: its checksum, the function that makes one and the one that registers its script member
// functions (if any). Names are given where the checksum's name is known.
struct ComponentType {
    uint32_t type;
    uint32_t create;
    uint32_t registerFunctions;
};

// In the original's order (CreateComponent finds the first of a type).
const ComponentType kComponentTypes[] = {
    {0x21FC3301, CODE(0x005193B0, &Obj::CSkaterStancePanelComponent::Create), 0x00000000},
    {0xB5F1E9D2, CODE(0x00513AD0, &Obj::CSkaterLoopingSoundComponent::Create), CODE(0x00513860, &Obj::CSkaterLoopingSoundComponent::RegisterMemberFunctions)}, // SkaterLoopingSound
    {0x187574FA, CODE(0x00518E20, &Obj::CSkaterSoundComponent::Create), 0x00000000}, // SkaterSound
    {0x11B4C67F, CODE(0x00512C60, &Obj::CSkaterGapComponent::Create), 0x00000000}, // SkaterGap
    {0x52D61A0D, CODE(0x00517D40, &Obj::CSkaterRotateComponent::Create), 0x00000000}, // SkaterRotate
    {0xD9151D4D, CODE(0x00517830, &Obj::CSkaterPhysicsControlComponent::Create), CODE(0x005176A0, &Obj::CSkaterPhysicsControlComponent::RegisterMemberFunctions)}, // SkaterPhysicsControl
    {0x7CD3E6D5, CODE(0x00513630, &Obj::CSkaterLocalNetLogicComponent::Create), 0x00000000}, // SkaterLocalNetLogic
    {0x850EEF66, CODE(0x005147F0, &Obj::CSkaterNonLocalNetLogicComponent::Create), 0x00000000}, // SkaterNonLocalNetLogic
    {0xD262802F, CODE(0x00518B60, &Obj::CSkaterScoreComponent::Create), 0x00000000}, // SkaterScore
    {0xBF518E27, CODE(0x00514430, &Obj::CSkaterMatrixQueriesComponent::Create), 0x00000000}, // SkaterMatrixQueries
    {0x125044E2, CODE(0x00511810, &Obj::CSkaterFloatingNameComponent::Create), 0x00000000}, // SkaterFloatingName
    {0x18223FD6, CODE(0x0051AD30, &Obj::CSkaterStateHistoryComponent::Create), 0x00000000}, // SkaterStateHistory
    {0x5BD63B29, CODE(0x00505440, &Skater::CorePhysicsComponent::Create), CODE(0x004FBE80, &Skater::CorePhysicsComponent::RegisterMemberFunctions)}, // SkaterCorePhysics
    {0x9F9CC949, CODE(0x004F80E0, &Obj::CSkaterAdjustPhysicsComponent::Create), 0x00000000}, // SkaterAdjustPhysics
    {0x9B373E58, CODE(0x00511280, &Obj::CSkaterFinalizePhysicsComponent::Create), 0x00000000}, // SkaterFinalizePhysics
    {0xD8CC3428, CODE(0x004FBE20, &CSkaterCleanupStateComponent::Create), 0x00000000}, // SkaterCleanupState
    {0xB1D5A3E1, CODE(0x00510F90, &Obj::CSkaterEndRunComponent::Create), 0x00000000}, // SkaterEndRun
    {0x589E5FB3, CODE(0x004F9170, &Obj::CSkaterBalanceTrickComponent::Create), CODE(0x004F8CD0, &Obj::CSkaterBalanceTrickComponent::RegisterMemberFunctions)}, // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, CODE(0x00511470, &Obj::CSkaterFlipAndRotateComponent::Create), 0x00000000}, // SkaterFlipAndRotate
    {0x43686585, CODE(0x00519740, &Obj::CSkaterStateComponent::Create), CODE(0x00519430, &Obj::CSkaterStateComponent::RegisterMemberFunctions)}, // SkaterState
    {0x81F01058, CODE(0x004EB7F0, &Ed::CGoalEditorComponent::Create), CODE(0x004E6FB0, &Ed::CGoalEditorComponent::RegisterMemberFunctions)},
    {0x5B509AD3, CODE(0x004F7FB0, &Ed::CRailEditorComponent::Create), 0x00000000},
    {0x1D5B1AE6, CODE(0x004EC420, &Obj::CObjectProximityComponent::Create), CODE(0x004EC220, &Obj::CObjectProximityComponent::RegisterMemberFunctions)},
    {0xAC8946E2, CODE(0x004E5940, &Obj::CEditorCameraComponent::Create), 0x00000000},
    {0x8BCE7E66, CODE(0x005185D0, &Obj::CSkaterRunTimerComponent::Create), CODE(0x00518240, &Obj::CSkaterRunTimerComponent::RegisterMemberFunctions)}, // SkaterRunTimer
    {0x1859F772, CODE(0x0051B880, &Obj::CSkaterVehicleSoundComponent::Create), CODE(0x0051AD90, &Obj::CSkaterVehicleSoundComponent::RegisterMemberFunctions)},
    {0x380D909A, CODE(0x0051E280, &Obj::CTaggingControlComponent::Create), 0x00000000},
    {0x5B512EDC, CODE(0x0051DA80, &Obj::CTaggingComponent::Create), 0x00000000}, // Tagging
    {0xD1515392, CODE(0x004ECF20, &Obj::CPagerMessageComponent::Create), CODE(0x004EC960, &Obj::CPagerMessageComponent::RegisterMemberFunctions)},
    {0x286A8D26, CODE(0x00421490, &Obj::CModelComponent::Create), 0x00000000}, // Model
    {0xE3FCA939, CODE(0x00422DC0, &Obj::CModelLightUpdateComponent::Create), 0x00000000}, // ModelLightUpdate
    {0x222756D5, CODE(0x00431F60, &Obj::CSkeletonComponent::Create), CODE(0x00431BE0, &Obj::CSkeletonComponent::RegisterMemberFunctions)}, // the skeleton
    {0x7149B753, 0x0041A5B0,
     0x00419690}, // the animation blender
    {0x07713C7B, CODE(0x00433100, &Obj::CSoundComponent::Create), CODE(0x00432C80, &Obj::CSoundComponent::RegisterMemberFunctions)}, // Sound
    {0xA432DC41, CODE(0x0041C920, &Obj::CCollisionComponent::Create), 0x00000000}, // Collision
    {0x5CB5CB7E, CODE(0x0041FF20, &Obj::CLockObjComponent::Create), 0x00000000},
    {0xC695136D, CODE(0x0041BD90, &Obj::CCarPhysicsComponent::Create), CODE(0x0041B910, &Obj::CCarPhysicsComponent::RegisterMemberFunctions)}, // CarPhysics
    {0x0A015E17, CODE(0x00426650, &Obj::CMotionComponent::Create), CODE(0x00423130, &Obj::CMotionPathMembers::RegisterMemberFunctions)}, // Motion
    {0xCE0CA665, CODE(0x00434290, &Obj::CSuspendComponent::Create), CODE(0x00434610, &Obj::CSuspendComponent::RegisterMemberFunctions)}, // Suspend
    {0x270F56E1, CODE(0x00523490, &Obj::CTrickComponent::Create), CODE(0x0051EA90, &Obj::CTrickComponent::RegisterMemberFunctions)}, // Trick
    {0x6EB050ED, CODE(0x004E39E0, &Obj::CAvoidComponent::Create), CODE(0x004E3310, &Obj::CAvoidComponent::RegisterMemberFunctions)}, // Avoid
    {0x6EFDDFB8, CODE(0x004F0B10, &Obj::CPedLogicComponent::Create), CODE(0x004ECFB0, &Obj::CPedLogicComponent::RegisterMemberFunctions)}, // PedLogic
    {0xFFB39248, CODE(0x0042EF00, &Obj::CRigidBodyComponent::Create), CODE(0x0042BB00, &CRigidBodyComponent::RegisterMemberFunctions)}, // Rigidbody
    {0x6DCFEC46, CODE(0x004298B0, &CChainComponent::Create), 0x00000000},
    {0x8A897DD2, CODE(0x00431800, &Obj::CShadowComponent::Create), 0x00000000}, // Shadow
    {0x0F1641E3, CODE(0x00433D90, &Obj::CStreamComponent::Create), CODE(0x00433870, &Obj::CStreamComponent::RegisterMemberFunctions)}, // Stream
    {0xF27A294D, CODE(0x0051BD80, &Obj::CSpecialItemComponent::Create), 0x00000000}, // SpecialItem
    {0xE47F1B79, CODE(0x0043AA30, &Obj::CVehicleComponent::Create), CODE(0x00434F00, &Obj::RegisterVehicleMemberFunctions)}, // Vehicle
    {0x2C747D8A, CODE(0x005248C0, &Obj::CVehicleCameraComponent::Create), CODE(0x00524420, &Obj::CVehicleCameraComponent::RegisterMemberFunctions)}, // VehicleCamera
    {0xC472ECC5, CODE(0x00428010, &CNodeArrayComponent::Create), 0x00000000},
    {0x1CDFB2A3, CODE(0x0042B920, &CRailManagerComponent::Create), 0x00000000},
    {0x27443708, CODE(0x00428130, &CObjectHookManagerComponent::Create), 0x00000000},
    {0x3506CE64, CODE(0x0051BA10, &Obj::CSkitchComponent::Create), 0x00000000}, // Skitch
    {0xC4E311FA, CODE(0x0041B840, &Obj::CCameraComponent::Create), 0x00000000}, // Camera
    {0x5E43A604, CODE(0x004FA800, &CSkaterCameraComponent::Create), 0x00000000}, // SkaterCamera
    {0x404EB936, CODE(0x0043D670, &Obj::CVibrationComponent::Create), CODE(0x0043D490, &Obj::CVibrationComponent::RegisterMemberFunctions)},
    {0xE594F0A2, CODE(0x00434B90, &Obj::CTriggerComponent::Create), 0x00000000}, // Trigger
    {0xEADA1621, CODE(0x0041E3C0, &Obj::CFloatingTextComponent::Create), 0x00000000},
    {0x27D7CD28, CODE(0x0041EEC0, &Obj::CInputComponent::Create), CODE(0x0041E6F0, &Obj::CInputComponent::RegisterMemberFunctions)}, // Input
    {0xA9DB601E, CODE(0x0042A000, &CParticleComponent::Create), CODE(0x00429940, &CParticleComponent::RegisterMemberFunctions)}, // Particle
    {0xC5BCEB9E, CODE(0x0041E1E0, &Obj::CFlexibleParticleComponent::Create), CODE(0x0041D6E0, &Obj::CFlexibleParticleComponent::RegisterMemberFunctions)},
    {0x726E85AA, CODE(0x0052A5C0, &CWalkComponent::Create), CODE(0x00525E40, &Obj::CWalkComponent::RegisterMemberFunctions)}, // Walk
    {0xC7D37EC2, CODE(0x00525DC0, &Obj::CWalkCameraComponent::Create), 0x00000000}, // WalkCamera
    {0x7505D7CC, CODE(0x00434870, &Obj::CTextureProjectionComponent::Create), 0x00000000},
    {0x81C7122D, CODE(0x004E42E0, &Obj::CCameraLookAroundComponent::Create), 0x00000000}, // CameraLookAround
    {0x408D2AA9, CODE(0x00427AB0, &CMovableContactComponent::Create), 0x00000000}, // MovableContact
    {0x4FF8DAC6, CODE(0x0051CB90, &Obj::CStatsManagerComponent::Create), CODE(0x0051C440, &Obj::CStatsManagerComponent::RegisterMemberFunctions)},
    {0x28C97F3C, CODE(0x0043CFC0, &Obj::CVehicleSoundComponent::Create), 0x00000000}, // VehicleSound
    {0x41272956, CODE(0x0043D430, &Obj::CVelocityComponent::Create), 0x00000000}, // Velocity
    {0x6259B52B, CODE(0x0041C370, &Obj::CCollideAndDieComponent::Create), 0x00000000},
    {0x7767D6D7, CODE(0x004F3010, &Obj::CProjectileCollisionComponent::Create), 0x00000000},
    {0x1BBF844D, CODE(0x004312A0, &Obj::CDisplayMatrixComponent::Create), 0x00000000},
    {0x9649D4F9, CODE(0x00433690, &Obj::CStaticVehicleComponent::Create), 0x00000000}, // StaticVehicle
    {0xCDC76A22, CODE(0x0041B3C0, &Obj::CColorCycleComponent::Create), CODE(0x0041B280, &Obj::CColorCycleComponent::RegisterMemberFunctions)},
    {0xB87C8E43, CODE(0x0041ADC0, &Obj::CBBoxComponent::Create), 0x00000000},
    {0x3618B878, CODE(0x004F3550, &Obj::CProjectileProximityComponent::Create), 0x00000000},
    {0x7E747547, CODE(0x004F3890, &Obj::CProjectileComponent::Create), 0x00000000}, // (the projectile component)
    {0x03AC8065, CODE(0x0043D930, &Obj::CreateWaitAndDieComponent), 0x00000000},
    {0xD8BFC9F7, CODE(0x0041D0F0, &Obj::CDynamicLightComponent::Create), 0x00000000},
    {0x208353AC, CODE(0x0041CB20, &Obj::CMatrixComponent::Create), 0x00000000},
    {0x0301D67C, CODE(0x0041D200, &Obj::CFamComponent::Create), CODE(0x0041D170, &Obj::CFamComponent::RegisterMemberFunctions)},
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
