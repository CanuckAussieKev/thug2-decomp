#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class FreeLookModule;

// The VehicleCamera component (type 0x2C747D8A, vtable 0x0064C5A0, 0xA4 bytes; made by 0x005248C0, its member
// function table 0x00684150 registered by 0x00524420): the camera behind a driven vehicle (its subject). Each
// frame the direction it looks along turns toward the vehicle's heading (its velocity when it moves fast enough
// with two wheels or more down), at most by the angle between them, at `lerp` a second; it sits `distance` behind
// and `height` above the vehicle, tilted by `angle` degrees, turned further by the look-around component's
// heading, and kept clear of the world.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize; the base's others.
class CarView final : public ObjectModule {
public:
    CarView(const CarView&) = delete;
    CarView& operator=(const CarView&) = delete;
    // 0x005248C0: a new one (0 if out of memory): the base's constructor, its vtable and type, height 60,
    // distance 144, lerp 3.
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    CarView();
    // 0x00524420: its member function table (0x00684150) registered.
    static void publish_method_table();
    // 0x00524440: the destructor body (the base's). 0x00524470 (slot 0): the scalar deleting destructor.
    ~CarView() override;
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x005244A0 (slot 2): subject= (an object id: the object and its Vehicle component), then 0x9213625F (lerp),
    // 0x14849B6D (height), 0xBD3D3CA9 (distance) and angle= - each kept when not given.
    void configure_from_data(script::Struct* params) override;
    // 0x00524C00 (slot 3): the object's CameraLookAround component, then Reset.
    void seal() override;
    // 0x00524B40: the look-around component back to straight ahead; looking along the vehicle's heading; the
    // orientation and position built and given to the camera object; the Camera component's Update.
    void get_reset();
    // 0x00524940 (slot 1): the camera's old position kept; the direction turned toward the target (see the
    // class), the orientation and position built, the position kept clear of the world, given to the camera.
    void tick_frame() override;
    // 0x00524450 (VehicleCamera_Reset, by its member function record): its structure read again (vtable +0x14)
    // and Finalize (vtable +0x0C). True.
    bool vehicle_camera_reset(script::Struct* params, script::Script* script);

    // 0x00524550: the target direction: the vehicle's heading (the subject's at-row negated, flat, normalised),
    // turned toward its velocity by the speed (x 0.0037879, at most 1) along the heading when it has two wheels or
    // more down; with fewer, its velocity's flat direction (when the speed is at least 88000000 - never).
    void update_target_direction();
    // 0x00524780: the orientation from the direction and the look-around heading, the position from the
    // subject's, the tilt (angle) applied.
    void update_orientation();
    // 0x00524720: the position and the orientation given to the camera object (its matrix and display matrix).
    void apply_to_camera();

    GameEntity* subject_{};          // +0x18 the vehicle
    uint8_t* vehicle_{};             // +0x1C its Vehicle component (+0xA4 its velocity, +0x114 wheels down)
    maths::Vec4 direction_{};        // +0x20 where the camera looks (flat, normalised)
    maths::Vec4 target_direction_{}; // +0x30
    maths::Vec4 anchor_point_{};     // +0x40
    maths::Mat4 orientation_{};      // +0x50
    float level_y_{60.0f};           // +0x90 (0x14849B6D)
    float reach_{144.0f};            // +0x94 (0xBD3D3CA9)
    float angle_{};                // +0x98 degrees (angle)
    float lerp_{3.0f};             // +0x9C (0x9213625F) a second
    FreeLookModule* free_look_{};  // +0xA0
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 2> g_vehicle_camera_member_functions;
} // namespace objects
