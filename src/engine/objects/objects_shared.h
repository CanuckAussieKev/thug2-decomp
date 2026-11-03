#pragma once

#include <array>
#include <cstdint>
#include "engine/objects/cutscene.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/rigidbody_component.h"

// NOT ORIGINAL - the objects' state their files share: the cutscene's camera slot, rotations, translations, fixed frame
// and playing flag, the event tracker and its references, and the rigid bodies' pushers, contacts, collision cache and
// debug drawing switches (the original's globals).
namespace objects {

// The objects' state their files share: the cutscene's camera slot, rotations, translations, fixed frame and playing
// flag, the event tracker and its references, and the rigid bodies' pushers, contacts, collision cache and debug
// drawing switches. Made on first use, never destroyed.
class ObjectsShared {
public:
    std::array<uint8_t, 64>& cutscene_camera_slot() { return cutscene_camera_slot_; }
    std::array<float, 512>& cutscene_rotations() { return cutscene_rotations_; }
    std::array<float, 512>& cutscene_translations() { return cutscene_translations_; }
    int& cutscene_fixed_frame() { return cutscene_fixed_frame_; }
    [[nodiscard]] bool cutscene_playing() const { return cutscene_playing_; }
    void set_cutscene_playing(bool value) { cutscene_playing_ = value; }
    uint32_t& tracker_references() { return tracker_references_; }
    std::unique_ptr<objects::EventWaitBoard>& tracker() { return tracker_; }
    std::array<float, 8>& rigid_body_pusher_reach() { return rigid_body_pusher_reach_; }
    std::optional<render::HitMeshCache>& rigid_body_cache() { return rigid_body_cache_; }
    std::array<BodyDynamicsModule::Contact, 24>& rigid_body_contacts() { return rigid_body_contacts_; }
    std::array<maths::Vec4, 24>& rigid_body_world_contact_points() { return rigid_body_world_contact_points_; }
    std::array<maths::Vec4, 24>& rigid_body_world_sphere_centers() { return rigid_body_world_sphere_centers_; }
    std::array<maths::Vec4, 2>& rigid_body_world_spring_points() { return rigid_body_world_spring_points_; }
    std::array<SharedRef<GameEntity>, 8>& rigid_body_pushers() { return rigid_body_pushers_; }
    [[nodiscard]] float rigid_body_pusher_height() const { return rigid_body_pusher_height_; }
    void set_rigid_body_pusher_height(float value) { rigid_body_pusher_height_ = value; }
    bool& rigid_body_draw_shape() { return rigid_body_draw_shape_; }
    bool& rigid_body_draw_details() { return rigid_body_draw_details_; }

private:
    // (cutscene_data.cpp)
    std::array<uint8_t, 64> cutscene_camera_slot_{}; // (5 bytes in the original, which wrote a pose's per-bone flags past them)
    std::array<float, 512> cutscene_rotations_{};
    std::array<float, 512> cutscene_translations_{};

    // (cutscene_play.cpp)
    int cutscene_fixed_frame_{};
    bool cutscene_playing_{};

    // (entity_hub.cpp)
    uint32_t tracker_references_{};
    std::unique_ptr<objects::EventWaitBoard> tracker_{};

    // (rigidbody_component.cpp)
    std::array<float, 8> rigid_body_pusher_reach_{};
    std::optional<render::HitMeshCache> rigid_body_cache_{};
    std::array<BodyDynamicsModule::Contact, 24> rigid_body_contacts_{};
    std::array<maths::Vec4, 24> rigid_body_world_contact_points_{};
    std::array<maths::Vec4, 24> rigid_body_world_sphere_centers_{};
    std::array<maths::Vec4, 2> rigid_body_world_spring_points_{};
    std::array<SharedRef<GameEntity>, 8> rigid_body_pushers_{};
    float rigid_body_pusher_height_{};

    // (rigidbody_physics.cpp)
    bool rigid_body_draw_shape_{};
    bool rigid_body_draw_details_{};
};

// The state (made on first use, never destroyed).
ObjectsShared& objects_shared();

} // namespace objects
