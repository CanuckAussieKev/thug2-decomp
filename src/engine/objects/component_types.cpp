// The component types every composite object can be made of, registered with the composite object manager.

#include "engine/objects/component_types.h"

#include <cstdint>
#include <array>

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
    ComponentCreateFunction create;
    ComponentRegisterFunction register_functions_;
};

// A component maker as the manager calls it (some makers return their own class).
template <auto Make>
ObjectModule* create_with()
{
    return static_cast<ObjectModule*>(Make());
}

// In the original's order (spawn_module finds the first of a type).
constexpr auto kComponentTypes = std::to_array<ComponentType>({
    {0x21FC3301, &create_with<&objects::RiderStanceDisplay::fabricate>, nullptr},
    {0xB5F1E9D2, &create_with<&objects::RiderRollingAudio::fabricate>,
     &objects::RiderRollingAudio::publish_method_table},         // SkaterLoopingSound
    {0x187574FA, &create_with<&objects::RiderSfxPlayer::fabricate>, nullptr},  // SkaterSound
    {0x11B4C67F, &create_with<&objects::RiderGapTracker::fabricate>, nullptr}, // SkaterGap
    {0x52D61A0D, &create_with<&objects::RiderTurning::fabricate>, nullptr},    // SkaterRotate
    {0xD9151D4D, &create_with<&objects::RiderBoardPhysics::fabricate>,
     &objects::RiderBoardPhysics::publish_method_table},           // SkaterPhysicsControl
    {0x7CD3E6D5, &create_with<&objects::RiderHostSync::fabricate>, nullptr},     // SkaterLocalNetLogic
    {0x850EEF66, &create_with<&objects::RiderRemoteSync::fabricate>, nullptr},   // SkaterNonLocalNetLogic
    {0xD262802F, &create_with<&objects::RiderPointKeeper::fabricate>, nullptr},  // SkaterScore
    {0xBF518E27, &create_with<&objects::RiderFrameLookups::fabricate>, nullptr}, // SkaterMatrixQueries
    {0x125044E2, &create_with<&objects::RiderOverheadTag::fabricate>, nullptr},  // SkaterFloatingName
    {0x18223FD6, &create_with<&objects::RiderFrameLog::fabricate>, nullptr},     // SkaterStateHistory
    {0x5BD63B29, &create_with<&skater::SkaterPhysicsModule::fabricate>,
     &skater::SkaterPhysicsModule::publish_method_table},           // SkaterCorePhysics
    {0x9F9CC949, &create_with<&objects::RiderPhysicsTweaks::fabricate>, nullptr}, // SkaterAdjustPhysics
    {0x9B373E58, &create_with<&objects::RiderPhysicsCommit::fabricate>, nullptr}, // SkaterFinalizePhysics
    {0xD8CC3428, &create_with<&RiderStateReset::fabricate>, nullptr},             // SkaterCleanupState
    {0xB1D5A3E1, &create_with<&objects::RiderSessionEnd::fabricate>, nullptr},    // SkaterEndRun
    {0x589E5FB3, &create_with<&objects::RiderTeeterMoves::fabricate>,
     &objects::RiderTeeterMoves::publish_method_table},           // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, &create_with<&objects::RiderSpinAndFlip::fabricate>, nullptr}, // SkaterFlipAndRotate
    {0x43686585, &create_with<&objects::RiderPoseState::fabricate>,
     &objects::RiderPoseState::publish_method_table}, // SkaterState
    {0x81F01058, &create_with<&park_editing::ObjectiveAuthoring::fabricate>,
     &park_editing::ObjectiveAuthoring::publish_method_table},
    {0x5B509AD3, &create_with<&park_editing::RailAuthoring::fabricate>, nullptr},
    {0x1D5B1AE6, &create_with<&objects::ObjectProximityComponent::fabricate>,
     &objects::ObjectProximityComponent::publish_method_table},
    {0xAC8946E2, &create_with<&objects::EditorViewModule::fabricate>, nullptr},
    {0x8BCE7E66, &create_with<&objects::RiderRunClock::fabricate>,
     &objects::RiderRunClock::publish_method_table}, // SkaterRunTimer
    {0x1859F772, &create_with<&objects::SkaterVehicleSoundComponent::fabricate>,
     &objects::SkaterVehicleSoundComponent::publish_method_table},
    {0x380D909A, &create_with<&objects::TaggingControlComponent::fabricate>, nullptr},
    {0x5B512EDC, &create_with<&objects::TaggingComponent::fabricate>, nullptr}, // Tagging
    {0xD1515392, &create_with<&objects::PagerMessageComponent::fabricate>,
     &objects::PagerMessageComponent::publish_method_table},
    {0x286A8D26, &create_with<&objects::MeshInstanceModule::fabricate>, nullptr}, // Model
    {0xE3FCA939, &create_with<&objects::MeshLightingModule::fabricate>, nullptr}, // ModelLightUpdate
    {0x222756D5, &create_with<&objects::BoneRigModule::fabricate>,
     &objects::BoneRigModule::publish_method_table}, // the skeleton
    {0x7149B753, &create_with<&objects::AnimBlenderComponent::fabricate>,
     &objects::AnimBlenderComponent::publish_method_table}, // the animation blender
    {0x07713C7B, &create_with<&objects::AudioEmitterModule::fabricate>,
     &objects::AudioEmitterModule::publish_method_table},           // Sound
    {0xA432DC41, &create_with<&objects::ContactShapeModule::fabricate>, nullptr}, // Collision
    {0x5CB5CB7E, &create_with<&objects::PinnedObjectModule::fabricate>, nullptr},
    {0xC695136D, &create_with<&objects::AutomobileDynamicsModule::fabricate>,
     &objects::AutomobileDynamicsModule::publish_method_table}, // CarPhysics
    {0x0A015E17, &create_with<&objects::KinematicsModule::fabricate>,
     &objects::KinematicsModule::publish_method_table}, // Motion
    {0xCE0CA665, &create_with<&objects::DormancyModule::fabricate>,
     &objects::DormancyModule::publish_method_table}, // Suspend
    {0x270F56E1, &create_with<&objects::MoveExecutor::fabricate>,
     &objects::MoveExecutor::publish_method_table}, // Trick
    {0x6EB050ED, &create_with<&objects::ObstacleDodgeModule::fabricate>,
     &objects::ObstacleDodgeModule::publish_method_table}, // Avoid
    {0x6EFDDFB8, &create_with<&objects::PedestrianBrainModule::fabricate>,
     &objects::PedestrianBrainModule::publish_method_table}, // PedLogic
    {0xFFB39248, &create_with<&objects::BodyDynamicsModule::fabricate>,
     &BodyDynamicsModule::publish_method_table}, // Rigidbody
    {0x6DCFEC46, &create_with<&ChainComponent::fabricate>, nullptr},
    {0x8A897DD2, &create_with<&objects::GroundShadowModule::fabricate>, nullptr}, // Shadow
    {0x0F1641E3, &create_with<&objects::AudioStreamModule::fabricate>,
     &objects::AudioStreamModule::publish_method_table},           // Stream
    {0xF27A294D, &create_with<&objects::PickupItemHandler::fabricate>, nullptr}, // SpecialItem
    {0xE47F1B79, &create_with<&objects::DrivableModule::fabricate>,
     &objects::register_vehicle_member_functions}, // Vehicle
    {0x2C747D8A, &create_with<&objects::CarView::fabricate>,
     &objects::CarView::publish_method_table}, // VehicleCamera
    {0xC472ECC5, &create_with<&WaypointListModule::fabricate>, nullptr},
    {0x1CDFB2A3, &create_with<&GrindPathLinkModule::fabricate>, nullptr},
    {0x27443708, &create_with<&HookPointRegistryModule::fabricate>, nullptr},
    {0x3506CE64, &create_with<&objects::TowRideHandler::fabricate>, nullptr},  // Skitch
    {0xC4E311FA, &create_with<&objects::ViewpointModule::fabricate>, nullptr}, // Camera
    {0x5E43A604, &create_with<&RiderChaseView::fabricate>, nullptr},           // SkaterCamera
    {0x404EB936, &create_with<&objects::PadRumbleModule::fabricate>,
     &objects::PadRumbleModule::publish_method_table},
    {0xE594F0A2, &create_with<&objects::ActivationZoneModule::fabricate>, nullptr}, // Trigger
    {0xEADA1621, &create_with<&objects::FloatingTextComponent::fabricate>, nullptr},
    {0x27D7CD28, &create_with<&objects::PadInputModule::fabricate>,
     &objects::PadInputModule::publish_method_table}, // Input
    {0xA9DB601E, &create_with<&EffectsEmitterModule::fabricate>,
     &EffectsEmitterModule::publish_method_table}, // Particle
    {0xC5BCEB9E, &create_with<&objects::FlexibleParticleComponent::fabricate>,
     &objects::FlexibleParticleComponent::publish_method_table},
    {0x726E85AA, &create_with<&FootMovement::fabricate>,
     &objects::FootMovement::publish_method_table},       // Walk
    {0xC7D37EC2, &create_with<&objects::FootView::fabricate>, nullptr}, // WalkCamera
    {0x7505D7CC, &create_with<&objects::TextureProjectionComponent::fabricate>, nullptr},
    {0x81C7122D, &create_with<&objects::FreeLookModule::fabricate>, nullptr}, // CameraLookAround
    {0x408D2AA9, &create_with<&MovingSurfaceModule::fabricate>, nullptr},     // MovableContact
    {0x4FF8DAC6, &create_with<&objects::AttributeTracker::fabricate>,
     &objects::AttributeTracker::publish_method_table},
    {0x28C97F3C, &create_with<&objects::EngineAudioModule::fabricate>, nullptr},     // VehicleSound
    {0x41272956, &create_with<&objects::VelocityTrackerModule::fabricate>, nullptr}, // Velocity
    {0x6259B52B, &create_with<&objects::ImpactDestructModule::fabricate>, nullptr},
    {0x7767D6D7, &create_with<&objects::ShotImpactModule::fabricate>, nullptr},
    {0x1BBF844D, &create_with<&objects::FaceHeadingModule::fabricate>, nullptr},
    {0x9649D4F9, &create_with<&objects::ParkedVehicleModule::fabricate>, nullptr}, // StaticVehicle
    {0xCDC76A22, &create_with<&objects::ColorCycleComponent::fabricate>,
     &objects::ColorCycleComponent::publish_method_table},
    {0xB87C8E43, &create_with<&objects::CbBoxComponent::fabricate>, nullptr},
    {0x3618B878, &create_with<&objects::ProjectileProximityComponent::fabricate>, nullptr},
    {0x7E747547, &create_with<&objects::ProjectileComponent::fabricate>, nullptr}, // (the projectile component)
    {0x03AC8065, &objects::create_wait_and_die_component, nullptr},
    {0xD8BFC9F7, &create_with<&objects::DynamicLightComponent::fabricate>, nullptr},
    {0x208353AC, &create_with<&objects::MatrixComponent::fabricate>, nullptr},
    {0x0301D67C, &create_with<&objects::FamComponent::fabricate>,
     &objects::FamComponent::publish_method_table},
});

} // namespace

// 0x004E4860
void register_component_types()
{
    EntityHub* manager = object_hub_slot().get();
    for (const ComponentType& t : kComponentTypes) {
        manager->enrol_module(t.type, t.create,
                              t.register_functions_);
    }
}

} // namespace objects
