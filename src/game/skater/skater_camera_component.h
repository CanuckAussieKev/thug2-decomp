#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Mth {
class SlerpInterpolator;
} // namespace Mth

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CCameraLookAroundComponent;
struct WalkCameraState;

// The skater camera component (type 0x5E43A604 "skatercamera", vtable 0x0064BA74, 0x17C bytes; made by 0x004FA800):
// the camera that follows a skating skater - behind and above it (its distances eased with a zoom), swung round
// after the skater (its own orientation, slerped through an interpolator), looking up or down in vert air or on a
// lip, with a shake, kept clear of the world. Its camera mode (Skater_Camera_Array) sets the distances, rates, zooms
// and tilts.
//
// Vtable: 0 the scalar deleting destructor (0x004FA590), 1 Update (0x004FA860), 2 InitFromStructure (0x004FA5C0),
// 3 Finalize (0x004F9A20), 12 CallMemberFunction (0x004FA680).
class CSkaterCameraComponent : public CBaseComponent {
public:
    // 0x004FA800: a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004FA4B0: the base's constructor, its vtable and type, the interpolator (0x98 bytes), the zooms and rates at
    // their defaults, no skater. Returns this.
    CSkaterCameraComponent* Construct();
    // 0x004F99F0: the destructor body: the interpolator freed, the base's.
    void DestroyBody();
    // 0x004FA590 (slot 0): the destructor body, then freed with `flags` bit 0. Returns this.
    CSkaterCameraComponent* Destroy(unsigned int flags);
    // 0x004FA5C0 (slot 2): the camera object's matrix made the identity and its orientation the same; the skater
    // from 0x4C48DA58 (an object id, asserted).
    void InitFromStructure(Script::CStruct* params);
    // 0x004F9A20 (slot 3): the camera lookaround component (0x81C7122D).
    void Finalize();
    // 0x004FA680 (slot 12): 0x75A516D3 - a shake from duration, vert_amp, horiz_amp, vert_vel, horiz_vel;
    // 0xD7867FFE - the camera mode from 0x6835B854 (asserted). 1, or 2 for anything else.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004FA860 (slot 1): one frame (skater_camera_update.cpp).
    void Update();

    // 0x004FA3C0: the skater followed, its SkaterState and SkaterPhysicsControl components (none: all null); 3
    // instant frames.
    void SetSkater(CCompositeObject* skater);
    // 0x004FA420: its orientation, target, turn cosine and zoom kept in `state` (as the walk camera's).
    void SaveState(WalkCameraState* state);
    // 0x004F9A40: taken over from another camera: its orientation's first three rows, target, turn cosine and zoom;
    // the settling time and the instant frames cleared, the lookaround's heading and lock too.
    void SetState(const WalkCameraState* state);
    // 0x004F9970 (not a Ghidra function): one frame (1/60 s) of the behind / above distances' lerps.
    void UpdateLerps();
    // 0x004F9AE0: camera mode `mode` (Skater_Camera_Array, or the 2P split screen's arrays): its values read; the
    // behind / above distances (feet) lerped over `time` seconds (none: at once); the field of view (half with a
    // vertical split); the lookaround's heading by the mode (5/6 behind, 7/8 a quarter turn, 11/12 the other, else 0).
    void SetMode(int mode, float time);
    // 0x004FA7C0 / 0x004FA7D0: the current camera mode set again / the next one (1 to 4, round again; nothing from 5
    // up) - SetMode with a blend of 1.
    void ResetMode();
    void NextMode();
    // 0x004F9EB0: whether to look straight down: a skater whose state's +0x1C is clear, on a lip (state 3), in vert air
    // or with the state's +0xF4 set.
    bool LookDown();
    // 0x004F9F00 (RET 8): the shake applied to `position` along the orientation's first two rows; its time run down
    // by the frame.
    void ApplyShake(Mth::Vector* position, const Mth::Matrix* orientation);
    // 0x004FA020 (RET 8): the point followed: the skater's position (at once with `instant` or when the state's +0xA0
    // says so), else eased toward it - across at m_lerpXZ (in vert air m_vertAirLerpXZ), up at m_lerpY
    // (m_vertAirLerpY; at once when the state's +0x98), both scaled by m_rateScale (rising back to 1 over m_rateTime
    // seconds at m_rateRise). Into `out`.
    void GetTarget(Mth::Vector* out, bool instant);
    // 0x004FA230 (RET 8): the height above the skater (m_above, on a lip m_lipTrickAbove, from 3 while zoomed in) and
    // the distance behind (m_behind x the zoom, which eases at m_zoomLerp toward 1 - in vert air m_bigAirTrickZoom, on
    // a rail m_grindZoom, on a lip m_lipTrickZoom - times the look-around's zoom; nearer as the look-around tilts
    // down). Nothing without a skater.
    void GetZoomedOffsets(float* height, float* distance);

    uint8_t m_unknown18[0x24 - 0x18];
    int m_mode;                      // +0x24 the camera mode (an index of Skater_Camera_Array)
    int m_instantFrames;             // +0x28 frames that jump straight to the target
    float m_turnCosine;              // +0x2C the last turn's cosine (the new facing against the old)
    Mth::Vector m_target;            // +0x30 the point followed
    Mth::Vector m_orientation[3];    // +0x40 (the view's first three rows)
    int m_wallRideFrames;            // +0x70
    Mth::Matrix m_skaterMatrix;      // +0x74 the skater's orientation last frame
    Mth::Vector m_wallFacing;        // +0xB4 the wall's facing when the wall ride began (flat)
    float m_behind;                  // +0xC4 behind (inches)
    float m_behindTime;              // +0xC8 left to lerp
    float m_behindStep;              // +0xCC a frame's
    float m_above;                   // +0xD0 above (inches)
    float m_aboveTime;               // +0xD4
    float m_aboveStep;               // +0xD8
    float m_settleTime;              // +0xDC the slower slerp after vert air, left (seconds)
    float m_horizFov;                // +0xE0 horiz_fov
    float m_lerpXZ;                  // +0xE4 lerp_xz
    float m_lerpY;                   // +0xE8 lerp_y
    float m_vertAirLerpXZ;           // +0xEC vert_air_lerp_xz
    float m_vertAirLerpY;            // +0xF0 vert_air_lerp_y
    float m_lookRate;                // +0xF4 the roll's ease (the mode's 0xBEF0D195; name not recovered)
    float m_rateTime;                // +0xF8 seconds left of the rates' rise back to 1
    float m_rateRise;                // +0xFC
    float m_rateScale;               // +0x100
    uint8_t m_vertZoomOn;            // +0x104 the vert-air zoom on (0 / 1)
    uint8_t pad105[3];
    float m_zoom;                    // +0x108 the zoom now
    float m_zoomTarget;              // +0x10C
    float m_zoomLerp;                // +0x110 zoom_lerp
    float m_bigAirTrickZoom;         // +0x114 big_air_trick_zoom (in vert air)
    float m_grindZoom;               // +0x118 grind_zoom
    float m_lipTrickZoom;            // +0x11C lip_trick_zoom
    int m_lipState;                  // +0x120 0 none, 1 looking along the lip, 2 blocked: the other way
    float m_wallrideOffsetAngle;     // +0x124 wallride_offset_angle
    float m_roll;                    // +0x128
    float m_tilt;                    // +0x12C tilt
    float m_airTilt;                 // +0x130
    float m_lipTrickTilt;            // +0x134 lip_trick_tilt
    float m_lipTrickAbove;           // +0x138 lip_trick_above
    uint8_t m_unknown13C[4];
    float m_slerp;                   // +0x140 slerp
    float m_vertAirSlerp;            // +0x144 vert_air_slerp
    float m_vertAirLandedSlerp;      // +0x148 vert_air_landed_slerp (while settling)
    float m_originOffset;            // +0x14C origin_offset (inches)
    float m_shakeLeft;               // +0x150 the shake's time left
    float m_shakeLength;             // +0x154 its length
    float m_shakeAmpX;               // +0x158 amplitudes
    float m_shakeAmpY;               // +0x15C
    float m_shakeRateX;              // +0x160 rates
    float m_shakeRateY;              // +0x164
    CCameraLookAroundComponent* mp_lookAround; // +0x168
    Mth::SlerpInterpolator* mp_slerp; // +0x16C
    CCompositeObject* mp_skater;     // +0x170
    uint8_t* mp_state;               // +0x174 the skater's SkaterState (+0x18 the state, flags as bytes)
    uint8_t* mp_control;             // +0x178 the skater's SkaterPhysicsControl (+0x18 paused, +0x2C its state)
};
static_assert(offsetof(CSkaterCameraComponent, m_mode) == 0x24 && offsetof(CSkaterCameraComponent, m_target) == 0x30 &&
                  offsetof(CSkaterCameraComponent, m_orientation) == 0x40 &&
                  offsetof(CSkaterCameraComponent, m_skaterMatrix) == 0x74 &&
                  offsetof(CSkaterCameraComponent, m_wallFacing) == 0xB4 &&
                  offsetof(CSkaterCameraComponent, m_behind) == 0xC4 &&
                  offsetof(CSkaterCameraComponent, m_horizFov) == 0xE0 &&
                  offsetof(CSkaterCameraComponent, m_vertZoomOn) == 0x104 &&
                  offsetof(CSkaterCameraComponent, m_zoom) == 0x108 &&
                  offsetof(CSkaterCameraComponent, m_lipState) == 0x120 &&
                  offsetof(CSkaterCameraComponent, m_tilt) == 0x12C &&
                  offsetof(CSkaterCameraComponent, m_slerp) == 0x140 &&
                  offsetof(CSkaterCameraComponent, m_shakeLeft) == 0x150 &&
                  offsetof(CSkaterCameraComponent, mp_lookAround) == 0x168 &&
                  offsetof(CSkaterCameraComponent, mp_slerp) == 0x16C &&
                  offsetof(CSkaterCameraComponent, mp_control) == 0x178 && sizeof(CSkaterCameraComponent) == 0x17C,
              "layout must match the original");

// The skater's cleanup-state component (type 0xD8CC3428 "SkaterCleanupState", vtable 0x0064BB48, 0x1C bytes; made
// by 0x004FBE20): each frame the skater's shadow and light follow it (CSkater::UpdateShadowLight).
class CSkaterCleanupStateComponent : public CBaseComponent {
public:
    // 0x004FBE20: a new one (the constructor inlined: the base's, its vtable and type; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004FBD90: the destructor body (its vtable, then the base's). 0x004FBDB0 (slot 0): the scalar deleting
    // destructor.
    void DestroyBody();
    CSkaterCleanupStateComponent* Destroy(unsigned int flags);
    // 0x004FBDA0 (slot 2; `params` isn't used): the state cleared. (Two other components' vtables, 0x0064BEF8 and
    // 0x0064C054, share this function.)
    void InitFromStructure(Script::CStruct* params);
    // 0x004FBDE0 (slot 3): the object's SkaterState component.
    void Finalize();
    // 0x004FBE00 (slot 1): CSkater::UpdateShadowLight with the object's position and matrix.
    void Update();

    CBaseComponent* mp_state;        // +0x18 the object's SkaterState
};
static_assert(sizeof(CSkaterCleanupStateComponent) == 0x1C, "layout must match the original");

// 0x0064BA74: skaterCameraVtable (defined where the class's functions are).
extern void** const g_skaterCameraVtable;

} // namespace Obj
