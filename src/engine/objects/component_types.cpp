// The component types every composite object can be made of, registered with the composite object manager.

#include "engine/objects/component_types.h"

#include <cstdint>

#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/objects/avoid.h"
#include "engine/objects/bbox_component.h"
#include "engine/objects/camera_component.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/chain_component.h"
#include "engine/objects/collide_and_die.h"
#include "engine/objects/collision_component.h"
#include "engine/objects/color_cycle.h"
#include "engine/objects/display_matrix_component.h"
#include "engine/objects/dynamic_light.h"
#include "engine/objects/editor_camera.h"
#include "engine/objects/fam.h"
#include "engine/objects/flexible_particle.h"
#include "engine/objects/floating_text.h"
#include "engine/objects/input_component.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/matrix_component.h"
#include "engine/objects/model_component.h"
#include "engine/objects/model_light_update.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/node_components.h"
#include "engine/objects/object_proximity.h"
#include "engine/objects/pager_message.h"
#include "engine/objects/particle_component.h"
#include "engine/objects/ped_logic.h"
#include "engine/objects/projectile_component.h"
#include "engine/objects/projectile_components.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/static_vehicle.h"
#include "engine/objects/stream_component.h"
#include "engine/objects/suspend_component.h"
#include "engine/objects/texture_projection_component.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/vehicle_component.h"
#include "engine/objects/vehicle_sound.h"
#include "engine/objects/velocity_component.h"
#include "engine/objects/vibration_component.h"
#include "game/rider/adjust_physics.h"
#include "game/rider/balance_trick.h"
#include "game/rider/core_physics.h"
#include "game/rider/end_run.h"
#include "game/rider/finalize_physics.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/floating_name.h"
#include "game/rider/gap_entry.h"
#include "game/rider/local_net_logic.h"
#include "game/rider/looping_sound.h"
#include "game/rider/matrix_queries.h"
#include "game/rider/nonlocal_net_logic.h"
#include "game/rider/physics_control.h"
#include "game/rider/rotate_component.h"
#include "game/rider/run_timer.h"
#include "game/rider/skater_camera_component.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_sound.h"
#include "game/rider/skater_state.h"
#include "game/rider/skitch.h"
#include "game/rider/special_item.h"
#include "game/rider/stance_panel.h"
#include "game/rider/state_history.h"
#include "game/rider/stats_manager.h"
#include "game/rider/tagging.h"
#include "game/rider/trick_component.h"
#include "game/rider/vehicle_camera.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/walk_camera.h"
#include "game/rider/walk_physics.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/vehicle_wheel.h"
#include "engine/objects/walk_hang.h"
#include "engine/objects/compositemgr.h"
#include "platform/code_address.h"

namespace objects {

namespace {

// A component type: its checksum, the function that makes one and the one that registers its script member
// functions (if any). Names are given where the checksum's name is known.
struct ComponentType {
    uint32_t type;
    void* create;
    void* register_functions_;
};

// In the original's order (spawn_module finds the first of a type).
const ComponentType kComponentTypes[] = {
    {0x21FC3301, code_address(&objects::RiderStanceDisplay::fabricate), nullptr},
    {0xB5F1E9D2, code_address(&objects::RiderRollingAudio::fabricate),
     code_address(&objects::RiderRollingAudio::publish_method_table)},         // SkaterLoopingSound
    {0x187574FA, code_address(&objects::RiderSfxPlayer::fabricate), nullptr},  // SkaterSound
    {0x11B4C67F, code_address(&objects::RiderGapTracker::fabricate), nullptr}, // SkaterGap
    {0x52D61A0D, code_address(&objects::RiderTurning::fabricate), nullptr},    // SkaterRotate
    {0xD9151D4D, code_address(&objects::RiderBoardPhysics::fabricate),
     code_address(&objects::RiderBoardPhysics::publish_method_table)},           // SkaterPhysicsControl
    {0x7CD3E6D5, code_address(&objects::RiderHostSync::fabricate), nullptr},     // SkaterLocalNetLogic
    {0x850EEF66, code_address(&objects::RiderRemoteSync::fabricate), nullptr},   // SkaterNonLocalNetLogic
    {0xD262802F, code_address(&objects::RiderPointKeeper::fabricate), nullptr},  // SkaterScore
    {0xBF518E27, code_address(&objects::RiderFrameLookups::fabricate), nullptr}, // SkaterMatrixQueries
    {0x125044E2, code_address(&objects::RiderOverheadTag::fabricate), nullptr},  // SkaterFloatingName
    {0x18223FD6, code_address(&objects::RiderFrameLog::fabricate), nullptr},     // SkaterStateHistory
    {0x5BD63B29, code_address(&skater::SkaterPhysicsModule::fabricate),
     code_address(&skater::SkaterPhysicsModule::publish_method_table)},           // SkaterCorePhysics
    {0x9F9CC949, code_address(&objects::RiderPhysicsTweaks::fabricate), nullptr}, // SkaterAdjustPhysics
    {0x9B373E58, code_address(&objects::RiderPhysicsCommit::fabricate), nullptr}, // SkaterFinalizePhysics
    {0xD8CC3428, code_address(&RiderStateReset::fabricate), nullptr},             // SkaterCleanupState
    {0xB1D5A3E1, code_address(&objects::RiderSessionEnd::fabricate), nullptr},    // SkaterEndRun
    {0x589E5FB3, code_address(&objects::RiderTeeterMoves::fabricate),
     code_address(&objects::RiderTeeterMoves::publish_method_table)},           // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, code_address(&objects::RiderSpinAndFlip::fabricate), nullptr}, // SkaterFlipAndRotate
    {0x43686585, code_address(&objects::RiderPoseState::fabricate),
     code_address(&objects::RiderPoseState::publish_method_table)}, // SkaterState
    {0x81F01058, code_address(&park_editing::ObjectiveAuthoring::fabricate),
     code_address(&park_editing::ObjectiveAuthoring::publish_method_table)},
    {0x5B509AD3, code_address(&park_editing::RailAuthoring::fabricate), nullptr},
    {0x1D5B1AE6, code_address(&objects::ObjectProximityComponent::fabricate),
     code_address(&objects::ObjectProximityComponent::publish_method_table)},
    {0xAC8946E2, code_address(&objects::EditorViewModule::fabricate), nullptr},
    {0x8BCE7E66, code_address(&objects::RiderRunClock::fabricate),
     code_address(&objects::RiderRunClock::publish_method_table)}, // SkaterRunTimer
    {0x1859F772, code_address(&objects::SkaterVehicleSoundComponent::fabricate),
     code_address(&objects::SkaterVehicleSoundComponent::publish_method_table)},
    {0x380D909A, code_address(&objects::TaggingControlComponent::fabricate), nullptr},
    {0x5B512EDC, code_address(&objects::TaggingComponent::fabricate), nullptr}, // Tagging
    {0xD1515392, code_address(&objects::PagerMessageComponent::fabricate),
     code_address(&objects::PagerMessageComponent::publish_method_table)},
    {0x286A8D26, code_address(&objects::MeshInstanceModule::fabricate), nullptr}, // Model
    {0xE3FCA939, code_address(&objects::MeshLightingModule::fabricate), nullptr}, // ModelLightUpdate
    {0x222756D5, code_address(&objects::BoneRigModule::fabricate),
     code_address(&objects::BoneRigModule::publish_method_table)}, // the skeleton
    {0x7149B753, code_address(&objects::AnimBlenderComponent::fabricate),
     code_address(&objects::AnimBlenderComponent::publish_method_table)}, // the animation blender
    {0x07713C7B, code_address(&objects::AudioEmitterModule::fabricate),
     code_address(&objects::AudioEmitterModule::publish_method_table)},           // Sound
    {0xA432DC41, code_address(&objects::ContactShapeModule::fabricate), nullptr}, // Collision
    {0x5CB5CB7E, code_address(&objects::PinnedObjectModule::fabricate), nullptr},
    {0xC695136D, code_address(&objects::AutomobileDynamicsModule::fabricate),
     code_address(&objects::AutomobileDynamicsModule::publish_method_table)}, // CarPhysics
    {0x0A015E17, code_address(&objects::KinematicsModule::fabricate),
     code_address(&objects::MotionPathMembers::publish_method_table)}, // Motion
    {0xCE0CA665, code_address(&objects::DormancyModule::fabricate),
     code_address(&objects::DormancyModule::publish_method_table)}, // Suspend
    {0x270F56E1, code_address(&objects::MoveExecutor::fabricate),
     code_address(&objects::MoveExecutor::publish_method_table)}, // Trick
    {0x6EB050ED, code_address(&objects::ObstacleDodgeModule::fabricate),
     code_address(&objects::ObstacleDodgeModule::publish_method_table)}, // Avoid
    {0x6EFDDFB8, code_address(&objects::PedestrianBrainModule::fabricate),
     code_address(&objects::PedestrianBrainModule::publish_method_table)}, // PedLogic
    {0xFFB39248, code_address(&objects::BodyDynamicsModule::fabricate),
     code_address(&BodyDynamicsModule::publish_method_table)}, // Rigidbody
    {0x6DCFEC46, code_address(&ChainComponent::fabricate), nullptr},
    {0x8A897DD2, code_address(&objects::GroundShadowModule::fabricate), nullptr}, // Shadow
    {0x0F1641E3, code_address(&objects::AudioStreamModule::fabricate),
     code_address(&objects::AudioStreamModule::publish_method_table)},           // Stream
    {0xF27A294D, code_address(&objects::PickupItemHandler::fabricate), nullptr}, // SpecialItem
    {0xE47F1B79, code_address(&objects::DrivableModule::fabricate),
     code_address(&objects::register_vehicle_member_functions)}, // Vehicle
    {0x2C747D8A, code_address(&objects::CarView::fabricate),
     code_address(&objects::CarView::publish_method_table)}, // VehicleCamera
    {0xC472ECC5, code_address(&WaypointListModule::fabricate), nullptr},
    {0x1CDFB2A3, code_address(&GrindPathLinkModule::fabricate), nullptr},
    {0x27443708, code_address(&HookPointRegistryModule::fabricate), nullptr},
    {0x3506CE64, code_address(&objects::TowRideHandler::fabricate), nullptr},  // Skitch
    {0xC4E311FA, code_address(&objects::ViewpointModule::fabricate), nullptr}, // Camera
    {0x5E43A604, code_address(&RiderChaseView::fabricate), nullptr},           // SkaterCamera
    {0x404EB936, code_address(&objects::PadRumbleModule::fabricate),
     code_address(&objects::PadRumbleModule::publish_method_table)},
    {0xE594F0A2, code_address(&objects::ActivationZoneModule::fabricate), nullptr}, // Trigger
    {0xEADA1621, code_address(&objects::FloatingTextComponent::fabricate), nullptr},
    {0x27D7CD28, code_address(&objects::PadInputModule::fabricate),
     code_address(&objects::PadInputModule::publish_method_table)}, // Input
    {0xA9DB601E, code_address(&EffectsEmitterModule::fabricate),
     code_address(&EffectsEmitterModule::publish_method_table)}, // Particle
    {0xC5BCEB9E, code_address(&objects::FlexibleParticleComponent::fabricate),
     code_address(&objects::FlexibleParticleComponent::publish_method_table)},
    {0x726E85AA, code_address(&FootMovement::fabricate),
     code_address(&objects::FootMovement::publish_method_table)},       // Walk
    {0xC7D37EC2, code_address(&objects::FootView::fabricate), nullptr}, // WalkCamera
    {0x7505D7CC, code_address(&objects::TextureProjectionComponent::fabricate), nullptr},
    {0x81C7122D, code_address(&objects::FreeLookModule::fabricate), nullptr}, // CameraLookAround
    {0x408D2AA9, code_address(&MovingSurfaceModule::fabricate), nullptr},     // MovableContact
    {0x4FF8DAC6, code_address(&objects::AttributeTracker::fabricate),
     code_address(&objects::AttributeTracker::publish_method_table)},
    {0x28C97F3C, code_address(&objects::EngineAudioModule::fabricate), nullptr},     // VehicleSound
    {0x41272956, code_address(&objects::VelocityTrackerModule::fabricate), nullptr}, // Velocity
    {0x6259B52B, code_address(&objects::ImpactDestructModule::fabricate), nullptr},
    {0x7767D6D7, code_address(&objects::ShotImpactModule::fabricate), nullptr},
    {0x1BBF844D, code_address(&objects::FaceHeadingModule::fabricate), nullptr},
    {0x9649D4F9, code_address(&objects::ParkedVehicleModule::fabricate), nullptr}, // StaticVehicle
    {0xCDC76A22, code_address(&objects::ColorCycleComponent::fabricate),
     code_address(&objects::ColorCycleComponent::publish_method_table)},
    {0xB87C8E43, code_address(&objects::CbBoxComponent::fabricate), nullptr},
    {0x3618B878, code_address(&objects::ProjectileProximityComponent::fabricate), nullptr},
    {0x7E747547, code_address(&objects::ProjectileComponent::fabricate), nullptr}, // (the projectile component)
    {0x03AC8065, code_address(&objects::create_wait_and_die_component), nullptr},
    {0xD8BFC9F7, code_address(&objects::DynamicLightComponent::fabricate), nullptr},
    {0x208353AC, code_address(&objects::MatrixComponent::fabricate), nullptr},
    {0x0301D67C, code_address(&objects::FamComponent::fabricate),
     code_address(&objects::FamComponent::publish_method_table)},
};

} // namespace

// 0x004E4860
void register_component_types()
{
    EntityHub* manager = object_hub_slot();
    for (const ComponentType& t : kComponentTypes) {
        manager->enrol_module(t.type, function_at<ComponentCreateFunction>(t.create),
                              function_at<ComponentRegisterFunction>(t.register_functions_));
    }
}

} // namespace objects
