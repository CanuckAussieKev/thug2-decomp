#pragma once

// The skater's adjust-physics component (type 0x9F9CC949 "SkaterAdjustPhysics", vtable 0x0064B964, 0x34 bytes; made by
// 0x004F80E0): run after the core physics each frame, it puts right what the move left wrong - the "uber frig" (no
// ground at all under the skater: back where the frame began, turned round), the skater kept just above the ground it
// stands on, leaving a moving object it no longer stands on, the shadow placed, the model lit by the ground, and the
// "under the floor" check (a floor right above: onto it; further up: back where the frame began, half speed the other
// way); first, the skater found inside a moving object rides it (adjust_physics_component.cpp).

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace render {
class ProbeRay;
}

namespace objects {

class MeshInstanceModule;
class MovingSurfaceModule;
class GroundShadowModule;
class RiderPoseState;

class RiderPhysicsTweaks final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x9F9CC949;

    // 0x004F80E0 (registered by register_component_types): a new one (0 if out of memory): no components, not frigged.
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderPhysicsTweaks();
    // 0x004F8040: the destructor body (the base's). (0x004F8050 the deleting one)
    ~RiderPhysicsTweaks() override;
    // 0x004F8080 (vtable slot 3): the core physics, model, shadow, movable contact and state components.
    void seal() override;
    // 0x004F8C40 (vtable slot 1): the component's frame: on the ground or falling (state 0, or moving down), the
    // state component's "landed" flag (+0x60) set with the game clock's time (+0x64) the first time; unless skitching
    // (+0x88) check_inside_moving_object; keep_above_ground; the object's last position = its position.
    void tick_frame() override;
    // 0x004F8420 (called by tick_frame; the uber frig: kept above the ground)
    void keep_above_ground();
    // 0x004F8AA0: inside a moving object (the movable contact finds one along the frame's move): a debug message, a
    // feeler 30 up and down (movable objects too) - riding the object it hits; in the lip state (4) out into the air
    // with the under-the-floor event; else, not moved this frame: the velocity cleared.
    void check_inside_moving_object();
    // 0x004F82F0: the model lit by the ground: the feeler `ground` asked again (no movables, nearest), a copy of it
    // from 12 higher down to just under its hit (or nowhere: no hit) asked for faces flagged 0x20 - the model's
    // brightness from that hit, else from the ground's (apply_brightness, lerped at rate 1).
    void get_update_brightness(render::ProbeRay* ground);

    int uber_frig_tries_;  // +0x18 the uber frig's tries in a row (0-5: its offsets from the frame's start)
    uint8_t uber_frigged_; // +0x1C set while the uber frig put the skater back this frame
    uint8_t pad1d_[3];
    skater::SkaterPhysicsModule* core_ptr_; // +0x20 the skater's core physics component
    MeshInstanceModule* mesh_instance_;     // +0x24 the model component (UpdateBrightness)
    GroundShadowModule* shadow_handle_;     // +0x28
    MovingSurfaceModule* ride_;             // +0x2C
    RiderPoseState* state_ref_;             // +0x30
};

} // namespace objects
