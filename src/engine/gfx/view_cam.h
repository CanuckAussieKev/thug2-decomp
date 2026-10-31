#pragma once

#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// A camera (Gfx2D::ViewCam, vtable 0x006487E0, an GameObjects::CEntity, 0xC4 bytes): where a viewport looks from. Camera
// components copy their object's position and orientation into theirs every frame (CViewpointModule 0x0041B810 /
// 0x0041B8A0, through ChangeLocation / ChangeMat); the renderer and the 3D screen elements read it back through FetchLocation /
// FetchMat / FetchEffectiveHFov.
namespace Gfx2D {

class ViewCam : public GameObjects::CEntity {
public:
    // 0x00485BA0: the constructor (returns this): at the origin (w 1, the old position too), the identity
    // orientation, the cameras' default angle (Engine3D::GetScreenAngle), clip distances 12 / 12000 (the far one
    // 30 x the distance-clipping setting when that's on).
    ViewCam* MakeNew();
    // 0x004859F0: the destructor's body (CEntity's after).
    void DestroyBody();
    // 0x00485A20 (vtable slot 0): the destructor, and the memory freed with flags & 1. Returns this.
    ViewCam* ScalarDelete(uint8_t flags);
    // 0x00485B40 (vtable slot 3): the member function 0x44EA2B6D (its name unknown) sets the horizontal angle from
    // radians (the unnamed float parameter); anything else goes to CEntity's.
    bool InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // 0x00485A00 / 0x00485A10 (4 bytes each: source only).
    Maths::Vec4* FetchLocation() { return &m_atPoint; }
    Maths::Mat4* FetchMat() { return &m_basisFrame; }
    // 0x00485A50: the position kept as the previous frame's.
    void RememberPrevPos();
    // 0x00485A70
    void ChangeLocation(const Maths::Vec4& pos);
    // 0x00485A90 (returns the camera's matrix: the copy's result).
    Maths::Mat4& ChangeMat(const Maths::Mat4& matrix);
    // 0x00485AA0: m_correctedHorizFov (m_hFov widened by Engine3D::FetchFovRatio, over m_unknownC0) and m_unknownB4.
    void RefreshEffectiveHFov();
    // 0x00485B10: the horizontal field of view in degrees as drawn (updated first).
    float FetchEffectiveHFov();
    // 0x00485B20: the horizontal field of view in degrees (at 4:3).
    void ChangeHFov(float degrees);

    Maths::Vec4 m_oldPos;       // +0x4C
    Maths::Vec4 m_atPoint;          // +0x5C
    Maths::Mat4 m_basisFrame;       // +0x6C: row 2 points backward (the view looks along -row 2), row 1 up
    float m_hFov;               // +0xAC: degrees, at 4:3
    float m_correctedHorizFov;       // +0xB0: degrees, as drawn
    float m_unknownB4;          // +0xB4: tan(half m_correctedHorizFov) / tan(half the default angle)
    float m_nearDist;           // +0xB8: 12 (by its value)
    float m_farClip;            // +0xBC: 12000 (by its value)
    float m_unknownC0;          // +0xC0: 1 (divides the widened angle's tangent)
};

extern int g_drawDistanceSetting;
} // namespace Gfx
