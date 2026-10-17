#pragma once

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// A camera (Gfx::Camera, vtable 0x006487E0, an Obj::CObject, 0xC4 bytes): where a viewport looks from. Camera
// components copy their object's position and orientation into theirs every frame (CCameraComponent 0x0041B810 /
// 0x0041B8A0, through SetPos / SetMatrix); the renderer and the 3D screen elements read it back through GetPos /
// GetMatrix / GetAdjustedHFOV.
namespace Gfx {

class Camera : public Obj::CObject {
public:
    // 0x00485BA0: the constructor (returns this): at the origin (w 1, the old position too), the identity
    // orientation, the cameras' default angle (Nx::GetScreenAngle), clip distances 12 / 12000 (the far one
    // 30 x the distance-clipping setting when that's on).
    Camera* Construct();
    // 0x004859F0: the destructor's body (CObject's after).
    void DestroyBody();
    // 0x00485A20 (vtable slot 0): the destructor, and the memory freed with flags & 1. Returns this.
    Camera* ScalarDelete(uint8_t flags);
    // 0x00485B40 (vtable slot 3): the member function 0x44EA2B6D (its name unknown) sets the horizontal angle from
    // radians (the unnamed float parameter); anything else goes to CObject's.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00485A00 / 0x00485A10 (4 bytes each: source only).
    Mth::Vector* GetPos() { return &m_pos; }
    Mth::Matrix* GetMatrix() { return &m_matrix; }
    // 0x00485A50: the position kept as the previous frame's.
    void StoreOldPos();
    // 0x00485A70
    void SetPos(const Mth::Vector& pos);
    // 0x00485A90 (returns the camera's matrix: the copy's result).
    Mth::Matrix& SetMatrix(const Mth::Matrix& matrix);
    // 0x00485AA0: m_adjustedHFov (m_hFov widened by Nx::GetScreenAngleFactor, over m_unknownC0) and m_unknownB4.
    void UpdateAdjustedHFOV();
    // 0x00485B10: the horizontal field of view in degrees as drawn (updated first).
    float GetAdjustedHFOV();
    // 0x00485B20: the horizontal field of view in degrees (at 4:3).
    void SetHFOV(float degrees);

    Mth::Vector m_oldPos;       // +0x4C
    Mth::Vector m_pos;          // +0x5C
    Mth::Matrix m_matrix;       // +0x6C: row 2 points backward (the view looks along -row 2), row 1 up
    float m_hFov;               // +0xAC: degrees, at 4:3
    float m_adjustedHFov;       // +0xB0: degrees, as drawn
    float m_unknownB4;          // +0xB4: tan(half m_adjustedHFov) / tan(half the default angle)
    float m_nearClip;           // +0xB8: 12 (by its value)
    float m_farClip;            // +0xBC: 12000 (by its value)
    float m_unknownC0;          // +0xC0: 1 (divides the widened angle's tangent)
};

} // namespace Gfx
