#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "engine/objects/cutscene.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/rigidbody_component.h"

namespace objects {

// NOT ORIGINAL grouping - cutscene_data.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (cutscene_data_shared()).
struct CutsceneDataShared {
    std::array<uint8_t, 64> cutscene_camera_slot_{}; // (5 bytes in the original, which wrote a pose's per-bone flags past them)
    std::array<float, 512> cutscene_rotations_{};
    std::array<float, 512> cutscene_translations_{};
};
CutsceneDataShared& cutscene_data_shared();

} // namespace objects

namespace objects {

// NOT ORIGINAL grouping - cutscene_play.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (cutscene_play_shared()).
struct CutscenePlayShared {
    int cutscene_fixed_frame_{};
    bool cutscene_playing_{};
};
CutscenePlayShared& cutscene_play_shared();

} // namespace objects

namespace objects {

// NOT ORIGINAL grouping - entity_hub.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (entity_hub_shared()).
struct EntityHubShared {
    uint32_t tracker_references_{};
};
EntityHubShared& entity_hub_shared();

} // namespace objects

namespace objects {

// NOT ORIGINAL grouping - rigidbody_component.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (rigidbody_component_shared()).
struct RigidbodyComponentShared {
    std::array<float, 8> rigid_body_pusher_reach_{};
    std::optional<render::HitMeshCache> rigid_body_cache_{};
    std::array<BodyDynamicsModule::Contact, 24> rigid_body_contacts_{};
    std::array<maths::Vec4, 24> rigid_body_world_contact_points_{};
    std::array<maths::Vec4, 24> rigid_body_world_sphere_centers_{};
    std::array<maths::Vec4, 2> rigid_body_world_spring_points_{};
    std::array<SharedRef<GameEntity>, 8> rigid_body_pushers_{};
    float rigid_body_pusher_height_{};
};
RigidbodyComponentShared& rigidbody_component_shared();

} // namespace objects

namespace objects {

// NOT ORIGINAL grouping - rigidbody_physics.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (rigidbody_physics_shared()).
struct RigidbodyPhysicsShared {
    bool rigid_body_draw_shape_{};
    bool rigid_body_draw_details_{};
};
RigidbodyPhysicsShared& rigidbody_physics_shared();

} // namespace objects
