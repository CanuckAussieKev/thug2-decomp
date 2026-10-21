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
class CWalkComponent;

// What the camera keeps for a later restore (WalkCamera's SaveState / RestoreState: 0x58 bytes).
struct WalkCameraState {
    Mth::Matrix orientation;  // +0x00
    Mth::Vector target;       // +0x40
    float turnCosine;         // +0x50
    float zoom;               // +0x54
};

// 0x00524D70 (cdecl): `a` toward `b` by where `t` lies from `low` to `high` (x through a float step, y with its
// step unrounded, z and w as floats). Returns `out`.
Mth::Vector* __cdecl LerpByRange(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t, float low,
                                 float high);

// The WalkCamera component (type 0xC7D37EC2, vtable 0x0064C5E0, 0xA8 bytes; made by 0x00525DC0): the camera
// behind a walking skater. Each frame it swings round toward the skater's facing (when the right stick isn't
// turning it, the skater moves within lock_angle of where it looks and fast enough - min_slerp_speed to
// full_slerp_speed, or the d-pad's pair), turns with the right stick (facing_control), slerps its orientation
// toward that at matrix_slerp_rate (scaled for running, hanging and the stick; flushed at once on request),
// follows the skater's position (eased on two rates of the skater camera's), sits behind and above it (the
// skater camera's distances, zoomed by the look-around), looks at a point above the skater and keeps clear of the
// world. The parameters are the global structure walk_camera_parameters.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize, 12 CallMemberFunction.
class CWalkCameraComponent : public CBaseComponent {
public:
    // 0x00525DC0: a new one (0 if out of memory): the base's constructor, its vtable and type, the target (0, 0,
    // 0, 1).
    static CBaseComponent* __cdecl Create();
    // 0x00524C20: the destructor body (the base's). 0x00524F20 (slot 0): the scalar deleting destructor.
    void DestroyBody();
    CWalkCameraComponent* Destroy(unsigned int flags);
    // 0x00524F50 (slot 2): its skater from 0x4C48DA58 (an object id, asserted; name not recovered), the zoom and
    // the turn cosine 1, its orientation from the camera object's matrix.
    void InitFromStructure(Script::CStruct* params);
    // 0x00524FB0 (slot 3): the object's CameraLookAround, Camera and skatercamera components.
    void Finalize();
    // 0x00525D40 (slot 12): WalkCamera_FlushRequest (FlushRequest's work) and WalkCamera_Reset (ResetNow); 2
    // for anything else.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x005251D0 (slot 1): one frame (see the class).
    void Update();

    // 0x00524C50: the skater followed, its Walk and SkaterPhysicsControl components (none: all null).
    void SetSkater(CCompositeObject* skater);
    // 0x00524C30: an Update (through the vtable) that jumps straight to where it should be.
    void ResetNow();
    // 0x005250A0: the next updates turn straight to the skater's facing (the Walk component told too), the
    // look-around back to straight ahead, unlocked, its factor 1.
    void FlushRequest();
    // 0x00525F70: look along `facing` (turned by the look-around heading) at `rate`, until RestoreState.
    void SetOverrideFacing(const Mth::Vector* facing, float rate);
    // 0x00525060 / 0x00524FF0: its orientation, target, turn cosine and zoom saved / restored (restoring also
    // ends a flush, the instant frames, an override and the look-around's heading and lock).
    void SaveState(WalkCameraState* state);
    void RestoreState(const WalkCameraState* state);
    // 0x00524EF0 (stdcall; `this` isn't used): the float `name` of walk_camera_parameters (asserted).
    static float __stdcall Param(uint32_t name);
    // 0x005250E0: the point followed: the skater's position (at once when `instant`), else eased toward it at the
    // skater camera's rates (+0xE4 across, +0xE8 up) over the frame. Written to `out`.
    void GetTarget(Mth::Vector* out, bool instant);
    // 0x00524CB0: the height above the skater (the skater camera's +0xD0, from 3 when zoomed in) and the
    // distance behind (+0xC4 x the zoom, nearer when the look-around tilts up). The zoom eases toward the
    // look-around's factor while it turns (else 1) at the skater camera's +0x110.
    void GetZoomedOffsets(float* height, float* distance);

    int m_instantFrames;            // +0x18 frames left that jump straight to the target (3 after a teleport)
    bool m_flush;                   // +0x1C a flush requested
    bool m_reset;                   // +0x1D (ResetNow)
    uint8_t pad1E[2];
    float m_zoom;                   // +0x20
    Mth::Vector m_target;           // +0x24
    Mth::Matrix m_orientation;      // +0x34
    float m_turnCosine;             // +0x74 the last turn's cosine (the new at-row against the old)
    bool m_override;                // +0x78 (SetOverrideFacing)
    uint8_t pad79[3];
    Mth::Vector m_overrideFacing;   // +0x7C
    float m_overrideRate;           // +0x8C
    CCameraLookAroundComponent* m_lookAround; // +0x90
    CBaseComponent* m_camera;       // +0x94 (+0x18 its camera)
    uint8_t* m_skaterCamera;        // +0x98 (the parameters it shares: +0x74 a matrix, +0xC4, +0xD0, +0xE4, +0xE8,
                                    // +0x110, +0x12C)
    CCompositeObject* m_skater;     // +0x9C
    CWalkComponent* m_walk;         // +0xA0
    uint8_t* m_physicsControl;      // +0xA4
};
static_assert(offsetof(CWalkCameraComponent, m_instantFrames) == 0x18 && offsetof(CWalkCameraComponent, m_zoom) == 0x20 &&
                  offsetof(CWalkCameraComponent, m_target) == 0x24 &&
                  offsetof(CWalkCameraComponent, m_orientation) == 0x34 &&
                  offsetof(CWalkCameraComponent, m_override) == 0x78 &&
                  offsetof(CWalkCameraComponent, m_lookAround) == 0x90 &&
                  offsetof(CWalkCameraComponent, m_physicsControl) == 0xA4 && sizeof(CWalkCameraComponent) == 0xA8,
              "layout must match the original");
static_assert(sizeof(WalkCameraState) == 0x58, "layout must match the original");

} // namespace Obj
