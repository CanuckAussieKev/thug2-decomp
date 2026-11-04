// The component types every composite object can be made of, registered with the composite object manager.

#include "app/component_types.h"

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
#include "engine/objects/wait_and_die.h"
#include "engine/objects/compositemgr.h"
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
    {.type = 0x21FC3301, .create = &create_with<&objects::RiderStanceDisplay::fabricate>, .register_functions_ = nullptr},
    {.type = 0xB5F1E9D2, .create = &create_with<&objects::RiderRollingAudio::fabricate>,
     .register_functions_ = &objects::RiderRollingAudio::publish_method_table},         // SkaterLoopingSound
    {.type = 0x187574FA, .create = &create_with<&objects::RiderSfxPlayer::fabricate>, .register_functions_ = nullptr},  // SkaterSound
    {.type = 0x11B4C67F, .create = &create_with<&objects::RiderGapTracker::fabricate>, .register_functions_ = nullptr}, // SkaterGap
    {.type = 0x52D61A0D, .create = &create_with<&objects::RiderTurning::fabricate>, .register_functions_ = nullptr},    // SkaterRotate
    {.type = 0xD9151D4D, .create = &create_with<&objects::RiderBoardPhysics::fabricate>,
     .register_functions_ = &objects::RiderBoardPhysics::publish_method_table},           // SkaterPhysicsControl
    {.type = 0x7CD3E6D5, .create = &create_with<&objects::RiderHostSync::fabricate>, .register_functions_ = nullptr},     // SkaterLocalNetLogic
    {.type = 0x850EEF66, .create = &create_with<&objects::RiderRemoteSync::fabricate>, .register_functions_ = nullptr},   // SkaterNonLocalNetLogic
    {.type = 0xD262802F, .create = &create_with<&objects::RiderPointKeeper::fabricate>, .register_functions_ = nullptr},  // SkaterScore
    {.type = 0xBF518E27, .create = &create_with<&objects::RiderFrameLookups::fabricate>, .register_functions_ = nullptr}, // SkaterMatrixQueries
    {.type = 0x125044E2, .create = &create_with<&objects::RiderOverheadTag::fabricate>, .register_functions_ = nullptr},  // SkaterFloatingName
    {.type = 0x18223FD6, .create = &create_with<&objects::RiderFrameLog::fabricate>, .register_functions_ = nullptr},     // SkaterStateHistory
    {.type = 0x5BD63B29, .create = &create_with<&skater::SkaterPhysicsModule::fabricate>,
     .register_functions_ = &skater::SkaterPhysicsModule::publish_method_table},           // SkaterCorePhysics
    {.type = 0x9F9CC949, .create = &create_with<&objects::RiderPhysicsTweaks::fabricate>, .register_functions_ = nullptr}, // SkaterAdjustPhysics
    {.type = 0x9B373E58, .create = &create_with<&objects::RiderPhysicsCommit::fabricate>, .register_functions_ = nullptr}, // SkaterFinalizePhysics
    {.type = 0xD8CC3428, .create = &create_with<&RiderStateReset::fabricate>, .register_functions_ = nullptr},             // SkaterCleanupState
    {.type = 0xB1D5A3E1, .create = &create_with<&objects::RiderSessionEnd::fabricate>, .register_functions_ = nullptr},    // SkaterEndRun
    {.type = 0x589E5FB3, .create = &create_with<&objects::RiderTeeterMoves::fabricate>,
     .register_functions_ = &objects::RiderTeeterMoves::publish_method_table},           // SkaterBalanceTrick (balance_trick.h)
    {.type = 0x488AF07A, .create = &create_with<&objects::RiderSpinAndFlip::fabricate>, .register_functions_ = nullptr}, // SkaterFlipAndRotate
    {.type = 0x43686585, .create = &create_with<&objects::RiderPoseState::fabricate>,
     .register_functions_ = &objects::RiderPoseState::publish_method_table}, // SkaterState
    {.type = 0x81F01058, .create = &create_with<&park_editing::ObjectiveAuthoring::fabricate>,
     .register_functions_ = &park_editing::ObjectiveAuthoring::publish_method_table},
    {.type = 0x5B509AD3, .create = &create_with<&park_editing::RailAuthoring::fabricate>, .register_functions_ = nullptr},
    {.type = 0x1D5B1AE6, .create = &create_with<&objects::ObjectProximityComponent::fabricate>,
     .register_functions_ = &objects::ObjectProximityComponent::publish_method_table},
    {.type = 0xAC8946E2, .create = &create_with<&objects::EditorViewModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x8BCE7E66, .create = &create_with<&objects::RiderRunClock::fabricate>,
     .register_functions_ = &objects::RiderRunClock::publish_method_table}, // SkaterRunTimer
    {.type = 0x1859F772, .create = &create_with<&objects::SkaterVehicleSoundComponent::fabricate>,
     .register_functions_ = &objects::SkaterVehicleSoundComponent::publish_method_table},
    {.type = 0x380D909A, .create = &create_with<&objects::TaggingControlComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x5B512EDC, .create = &create_with<&objects::TaggingComponent::fabricate>, .register_functions_ = nullptr}, // Tagging
    {.type = 0xD1515392, .create = &create_with<&objects::PagerMessageComponent::fabricate>,
     .register_functions_ = &objects::PagerMessageComponent::publish_method_table},
    {.type = 0x286A8D26, .create = &create_with<&objects::MeshInstanceModule::fabricate>, .register_functions_ = nullptr}, // Model
    {.type = 0xE3FCA939, .create = &create_with<&objects::MeshLightingModule::fabricate>, .register_functions_ = nullptr}, // ModelLightUpdate
    {.type = 0x222756D5, .create = &create_with<&objects::BoneRigModule::fabricate>,
     .register_functions_ = &objects::BoneRigModule::publish_method_table}, // the skeleton
    {.type = 0x7149B753, .create = &create_with<&objects::AnimBlenderComponent::fabricate>,
     .register_functions_ = &objects::AnimBlenderComponent::publish_method_table}, // the animation blender
    {.type = 0x07713C7B, .create = &create_with<&objects::AudioEmitterModule::fabricate>,
     .register_functions_ = &objects::AudioEmitterModule::publish_method_table},           // Sound
    {.type = 0xA432DC41, .create = &create_with<&objects::ContactShapeModule::fabricate>, .register_functions_ = nullptr}, // Collision
    {.type = 0x5CB5CB7E, .create = &create_with<&objects::PinnedObjectModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0xC695136D, .create = &create_with<&objects::AutomobileDynamicsModule::fabricate>,
     .register_functions_ = &objects::AutomobileDynamicsModule::publish_method_table}, // CarPhysics
    {.type = 0x0A015E17, .create = &create_with<&objects::KinematicsModule::fabricate>,
     .register_functions_ = &objects::KinematicsModule::publish_method_table}, // Motion
    {.type = 0xCE0CA665, .create = &create_with<&objects::DormancyModule::fabricate>,
     .register_functions_ = &objects::DormancyModule::publish_method_table}, // Suspend
    {.type = 0x270F56E1, .create = &create_with<&objects::MoveExecutor::fabricate>,
     .register_functions_ = &objects::MoveExecutor::publish_method_table}, // Trick
    {.type = 0x6EB050ED, .create = &create_with<&objects::ObstacleDodgeModule::fabricate>,
     .register_functions_ = &objects::ObstacleDodgeModule::publish_method_table}, // Avoid
    {.type = 0x6EFDDFB8, .create = &create_with<&objects::PedestrianBrainModule::fabricate>,
     .register_functions_ = &objects::PedestrianBrainModule::publish_method_table}, // PedLogic
    {.type = 0xFFB39248, .create = &create_with<&objects::BodyDynamicsModule::fabricate>,
     .register_functions_ = &BodyDynamicsModule::publish_method_table}, // Rigidbody
    {.type = 0x6DCFEC46, .create = &create_with<&ChainComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x8A897DD2, .create = &create_with<&objects::GroundShadowModule::fabricate>, .register_functions_ = nullptr}, // Shadow
    {.type = 0x0F1641E3, .create = &create_with<&objects::AudioStreamModule::fabricate>,
     .register_functions_ = &objects::AudioStreamModule::publish_method_table},           // Stream
    {.type = 0xF27A294D, .create = &create_with<&objects::PickupItemHandler::fabricate>, .register_functions_ = nullptr}, // SpecialItem
    {.type = 0xE47F1B79, .create = &create_with<&objects::DrivableModule::fabricate>,
     .register_functions_ = &objects::register_vehicle_member_functions}, // Vehicle
    {.type = 0x2C747D8A, .create = &create_with<&objects::CarView::fabricate>,
     .register_functions_ = &objects::CarView::publish_method_table}, // VehicleCamera
    {.type = 0xC472ECC5, .create = &create_with<&WaypointListModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x1CDFB2A3, .create = &create_with<&GrindPathLinkModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x27443708, .create = &create_with<&HookPointRegistryModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x3506CE64, .create = &create_with<&objects::TowRideHandler::fabricate>, .register_functions_ = nullptr},  // Skitch
    {.type = 0xC4E311FA, .create = &create_with<&objects::ViewpointModule::fabricate>, .register_functions_ = nullptr}, // Camera
    {.type = 0x5E43A604, .create = &create_with<&RiderChaseView::fabricate>, .register_functions_ = nullptr},           // SkaterCamera
    {.type = 0x404EB936, .create = &create_with<&objects::PadRumbleModule::fabricate>,
     .register_functions_ = &objects::PadRumbleModule::publish_method_table},
    {.type = 0xE594F0A2, .create = &create_with<&objects::ActivationZoneModule::fabricate>, .register_functions_ = nullptr}, // Trigger
    {.type = 0xEADA1621, .create = &create_with<&objects::FloatingTextComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x27D7CD28, .create = &create_with<&objects::PadInputModule::fabricate>,
     .register_functions_ = &objects::PadInputModule::publish_method_table}, // Input
    {.type = 0xA9DB601E, .create = &create_with<&EffectsEmitterModule::fabricate>,
     .register_functions_ = &EffectsEmitterModule::publish_method_table}, // Particle
    {.type = 0xC5BCEB9E, .create = &create_with<&objects::FlexibleParticleComponent::fabricate>,
     .register_functions_ = &objects::FlexibleParticleComponent::publish_method_table},
    {.type = 0x726E85AA, .create = &create_with<&FootMovement::fabricate>,
     .register_functions_ = &objects::FootMovement::publish_method_table},       // Walk
    {.type = 0xC7D37EC2, .create = &create_with<&objects::FootView::fabricate>, .register_functions_ = nullptr}, // WalkCamera
    {.type = 0x7505D7CC, .create = &create_with<&objects::TextureProjectionComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x81C7122D, .create = &create_with<&objects::FreeLookModule::fabricate>, .register_functions_ = nullptr}, // CameraLookAround
    {.type = 0x408D2AA9, .create = &create_with<&MovingSurfaceModule::fabricate>, .register_functions_ = nullptr},     // MovableContact
    {.type = 0x4FF8DAC6, .create = &create_with<&objects::AttributeTracker::fabricate>,
     .register_functions_ = &objects::AttributeTracker::publish_method_table},
    {.type = 0x28C97F3C, .create = &create_with<&objects::EngineAudioModule::fabricate>, .register_functions_ = nullptr},     // VehicleSound
    {.type = 0x41272956, .create = &create_with<&objects::VelocityTrackerModule::fabricate>, .register_functions_ = nullptr}, // Velocity
    {.type = 0x6259B52B, .create = &create_with<&objects::ImpactDestructModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x7767D6D7, .create = &create_with<&objects::ShotImpactModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x1BBF844D, .create = &create_with<&objects::FaceHeadingModule::fabricate>, .register_functions_ = nullptr},
    {.type = 0x9649D4F9, .create = &create_with<&objects::ParkedVehicleModule::fabricate>, .register_functions_ = nullptr}, // StaticVehicle
    {.type = 0xCDC76A22, .create = &create_with<&objects::ColorCycleComponent::fabricate>,
     .register_functions_ = &objects::ColorCycleComponent::publish_method_table},
    {.type = 0xB87C8E43, .create = &create_with<&objects::CbBoxComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x3618B878, .create = &create_with<&objects::ProjectileProximityComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x7E747547, .create = &create_with<&objects::ProjectileComponent::fabricate>, .register_functions_ = nullptr}, // (the projectile component)
    {.type = 0x03AC8065, .create = &objects::create_wait_and_die_component, .register_functions_ = nullptr},
    {.type = 0xD8BFC9F7, .create = &create_with<&objects::DynamicLightComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x208353AC, .create = &create_with<&objects::MatrixComponent::fabricate>, .register_functions_ = nullptr},
    {.type = 0x0301D67C, .create = &create_with<&objects::FamComponent::fabricate>,
     .register_functions_ = &objects::FamComponent::publish_method_table},
});

} // namespace

// 0x004E4860
void register_component_types()
{
    EntityHub* manager = object_hub_slot().get();
    for (const ComponentType& type_entry : kComponentTypes) {
        manager->enrol_module(type_entry.type, type_entry.create, type_entry.register_functions_);
    }
}

} // namespace objects
