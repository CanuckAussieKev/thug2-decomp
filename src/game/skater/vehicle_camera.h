#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CCameraLookAroundComponent;

// The VehicleCamera component (type 0x2C747D8A, vtable 0x0064C5A0, 0xA4 bytes; made by 0x005248C0, its member
// function table 0x00684150 registered by 0x00524420): the camera behind a driven vehicle (its subject). Each
// frame the direction it looks along turns toward the vehicle's heading (its velocity when it moves fast enough
// with two wheels or more down), at most by the angle between them, at `lerp` a second; it sits `distance` behind
// and `height` above the vehicle, tilted by `angle` degrees, turned further by the look-around component's
// heading, and kept clear of the world.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize; the base's others.
class CVehicleCameraComponent : public CBaseComponent {
public:
    // 0x005248C0: a new one (0 if out of memory): the base's constructor, its vtable and type, height 60,
    // distance 144, lerp 3.
    static CBaseComponent* __cdecl Create();
    // 0x00524420: its member function table (0x00684150) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x00524440: the destructor body (the base's). 0x00524470 (slot 0): the scalar deleting destructor.
    void DestroyBody();
    CVehicleCameraComponent* Destroy(unsigned int flags);
    // 0x005244A0 (slot 2): subject= (an object id: the object and its Vehicle component), then 0x9213625F (lerp),
    // 0x14849B6D (height), 0xBD3D3CA9 (distance) and angle= - each kept when not given.
    void InitFromStructure(Script::CStruct* params);
    // 0x00524C00 (slot 3): the object's CameraLookAround component, then Reset.
    void Finalize();
    // 0x00524B40: the look-around component back to straight ahead; looking along the vehicle's heading; the
    // orientation and position built and given to the camera object; the Camera component's Update.
    void Reset();
    // 0x00524940 (slot 1): the camera's old position kept; the direction turned toward the target (see the
    // class), the orientation and position built, the position kept clear of the world, given to the camera.
    void Update();
    // 0x00524450 (VehicleCamera_Reset, by its member function record): its structure read again (vtable +0x14)
    // and Finalize (vtable +0x0C). True.
    bool VehicleCamera_Reset(Script::CStruct* params, Script::CScript* script);

    // 0x00524550: the target direction: the vehicle's heading (the subject's at-row negated, flat, normalised),
    // turned toward its velocity by the speed (x 0.0037879, at most 1) along the heading when it has two wheels or
    // more down; with fewer, its velocity's flat direction (when the speed is at least 88000000 - never).
    void UpdateTargetDirection();
    // 0x00524780: the orientation from the direction and the look-around heading, the position from the
    // subject's, the tilt (angle) applied.
    void UpdateOrientation();
    // 0x00524720: the position and the orientation given to the camera object (its matrix and display matrix).
    void ApplyToCamera();

    CCompositeObject* m_subject;          // +0x18 the vehicle
    uint8_t* m_vehicle;                   // +0x1C its Vehicle component (+0xA4 its velocity, +0x114 wheels down)
    Mth::Vector m_direction;              // +0x20 where the camera looks (flat, normalised)
    Mth::Vector m_targetDirection;        // +0x30
    Mth::Vector m_position;               // +0x40
    Mth::Matrix m_orientation;            // +0x50
    float m_height;                       // +0x90 (0x14849B6D)
    float m_distance;                     // +0x94 (0xBD3D3CA9)
    float m_angle;                        // +0x98 degrees (angle)
    float m_lerp;                         // +0x9C (0x9213625F) a second
    CCameraLookAroundComponent* m_lookAround; // +0xA0
};
static_assert(offsetof(CVehicleCameraComponent, m_subject) == 0x18 &&
                  offsetof(CVehicleCameraComponent, m_direction) == 0x20 &&
                  offsetof(CVehicleCameraComponent, m_orientation) == 0x50 &&
                  offsetof(CVehicleCameraComponent, m_lerp) == 0x9C &&
                  sizeof(CVehicleCameraComponent) == 0xA4,
              "layout must match the original");

} // namespace Obj
