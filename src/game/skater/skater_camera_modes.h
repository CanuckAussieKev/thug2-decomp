#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"


// Part of the skater camera component (SkaterCamera, type 0x5E43A604, vtable 0x0064BA74; made by 0x004FA800): the
// functions at 0x004F9970-0x004F9E7E - its destructor body, Finalize, the state taken over from the walk camera, the
// per-frame lerp of its distances and the mode set from Skater_Camera_Array. The rest of the class (0x004FA590-) is
// written with the code after it; to be folded into that class. The fields are named here as these functions use
// them.
namespace Obj {

class CSkaterCameraModes : public CBaseComponent {
public:
    // 0x004F99F0: the destructor body: its +0x16C block freed, the base's.
    void DestroyBody();
    // 0x004F9A20 (vtable slot 3): the camera lookaround component (0x81C7122D).
    void Finalize();
    // 0x004F9A40: taken over from another camera: its orientation's first three rows, target, turn cosine and zoom;
    // the zoom's lerp, +0x28 and the lookaround's +0x24 / +0x18 cleared.
    void SetState(const struct WalkCameraStateView* state);
    // 0x004F9970 (not a Ghidra function): one frame (1/60 s) of the behind / above distances' lerps.
    void UpdateLerps();
    // 0x004F9AE0: camera mode `mode` (Skater_Camera_Array, or the 2P split screen's arrays): its values read; the
    // behind / above distances (feet) lerped over `time` seconds (none: at once); the field of view (half with a
    // vertical split); the lookaround's heading by the mode (5/6 behind, 7/8 a quarter turn, 11/12 the other, else 0).
    void SetMode(int mode, float time);

    uint8_t m_unknown18[0x24 - 0x18];
    int m_mode;                     // +0x24
    float m_unknown28;              // +0x28
    float m_turnCosine;             // +0x2C
    float m_target[4];              // +0x30
    float m_orientation[12];        // +0x40 (three rows)
    uint8_t m_unknown70[0xC4 - 0x70];
    float m_behind;                 // +0xC4 (feet)
    float m_behindTime;             // +0xC8 left to lerp
    float m_behindStep;             // +0xCC a frame's
    float m_above;                  // +0xD0
    float m_aboveTime;              // +0xD4
    float m_aboveStep;              // +0xD8
    float m_unknownDC;              // +0xDC
    float m_horizFov;               // +0xE0 horiz_fov
    float m_lerpXZ;                 // +0xE4 lerp_xz
    float m_lerpY;                  // +0xE8 lerp_y
    float m_vertAirLerpXZ;          // +0xEC vert_air_lerp_xz
    float m_vertAirLerpY;           // +0xF0 vert_air_lerp_y
    float m_unknownF4;              // +0xF4 (0xBEF0D195, name unknown)
    uint8_t m_unknownF8[0x108 - 0xF8];
    float m_zoom;                   // +0x108
    uint8_t m_unknown10C[4];
    float m_zoomLerp;               // +0x110 zoom_lerp
    float m_bigAirTrickZoom;        // +0x114 big_air_trick_zoom
    float m_grindZoom;              // +0x118 grind_zoom
    float m_lipTrickZoom;           // +0x11C lip_trick_zoom
    uint8_t m_unknown120[4];
    float m_wallrideOffsetAngle;    // +0x124 wallride_offset_angle
    uint8_t m_unknown128[4];
    float m_tilt;                   // +0x12C tilt
    uint8_t m_unknown130[4];
    float m_lipTrickTilt;           // +0x134 lip_trick_tilt
    float m_lipTrickAbove;          // +0x138 lip_trick_above
    uint8_t m_unknown13C[4];
    float m_slerp;                  // +0x140 slerp
    float m_vertAirSlerp;           // +0x144 vert_air_slerp
    float m_vertAirLandedSlerp;     // +0x148 vert_air_landed_slerp
    float m_originOffset;           // +0x14C origin_offset (inches)
    uint8_t m_unknown150[0x168 - 0x150];
    uint8_t* mp_lookaround;         // +0x168 the camera lookaround component
    void* mp_block;                 // +0x16C freed by the destructor
};
static_assert(offsetof(CSkaterCameraModes, m_mode) == 0x24 && offsetof(CSkaterCameraModes, m_target) == 0x30 &&
                  offsetof(CSkaterCameraModes, m_behind) == 0xC4 && offsetof(CSkaterCameraModes, m_horizFov) == 0xE0 &&
                  offsetof(CSkaterCameraModes, m_zoom) == 0x108 && offsetof(CSkaterCameraModes, m_zoomLerp) == 0x110 &&
                  offsetof(CSkaterCameraModes, m_tilt) == 0x12C && offsetof(CSkaterCameraModes, m_originOffset) == 0x14C &&
                  offsetof(CSkaterCameraModes, mp_lookaround) == 0x168 && offsetof(CSkaterCameraModes, mp_block) == 0x16C,
              "layout must match the original");

// The state another camera hands over (as Skater::WalkCameraState: the orientation +0x00, the target +0x40, the
// turn cosine +0x50, the zoom +0x54).
struct WalkCameraStateView {
    float orientation[16];
    float target[4];
    float turnCosine;
    float zoom;
};

} // namespace Obj
