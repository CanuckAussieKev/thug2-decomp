#pragma once

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CBaseComponent;
class CCompositeObject;

// The skater camera component (type 0x5E43A604 "skatercamera", vtable 0x0064BA74, 0x17C bytes; made by 0x004FA800):
// the camera that follows a skating skater - behind and above it (its distances eased with a zoom), swung round
// after the skater (its own orientation, +0x40, slerped through an interpolator at +0x16C), looking up or down in
// vert air or on a lip, with a shake, kept clear of the world. Its layout is only partly known: fields are reached
// by offset (skater_camera_component.cpp names them).
//
// Vtable: 0 the scalar deleting destructor (0x004FA590), 1 Update (0x004FA860), 2 InitFromStructure (0x004FA5C0),
// 12 CallMemberFunction (0x004FA680).
class CSkaterCameraComponent {
public:
    // 0x004FA800: a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004FA4B0: the base's constructor, its vtable and type, the interpolator (+0x16C, 0x98 bytes), the zooms and
    // rates at their defaults, no skater. Returns this.
    CSkaterCameraComponent* Construct();
    // 0x004FA590 (slot 0): the destructor body (0x004F99F0), then freed with `flags` bit 0. Returns this.
    CSkaterCameraComponent* Destroy(unsigned int flags);
    // 0x004FA5C0 (slot 2): the camera object's matrix made the identity and its orientation (+0x40) the same; the
    // skater from 0x4C48DA58 (an object id, asserted).
    void InitFromStructure(Script::CStruct* params);
    // 0x004FA680 (slot 12): 0x75A516D3 - a shake from duration, vert_amp, horiz_amp, vert_vel, horiz_vel;
    // 0xD7867FFE - the camera mode from 0x6835B854 (asserted; 0x004F9AE0). 1, or 2 for anything else.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004FA860 (slot 1): one frame (skater_camera_update.cpp).
    void Update();

    // 0x004FA3C0: the skater followed, its SkaterState and SkaterPhysicsControl components (none: all null); 3
    // instant frames.
    void SetSkater(CCompositeObject* skater);
    // 0x004FA420: its orientation, target, turn cosine and zoom kept in `state` (0x58 bytes, as the walk camera's).
    void SaveState(void* state);
    // 0x004FA7C0 / 0x004FA7D0: the current camera mode (+0x24) set again / the next one (1 to 4, round again;
    // nothing from 5 up) - 0x004F9AE0 with a blend of 1.
    void ResetMode();
    void NextMode();
    // 0x004F9EB0: whether to look straight down: a skater whose state's +0x1C is clear, on a lip (state 3), in vert air
    // or with the state's +0xF4 set.
    bool LookDown();
    // 0x004F9F00 (RET 8): the shake (+0x150 its time left, +0x154 its length, +0x158 / +0x15C amplitudes, +0x160 /
    // +0x164 rates) applied to `position` along the orientation's first two rows; the time run down by the frame.
    void ApplyShake(Mth::Vector* position, const Mth::Matrix* orientation);
    // 0x004FA020 (RET 8): the point followed: the skater's position (at once with `instant` or when the state's +0xA0
    // says so), else eased toward it - across at +0xE4 (in vert air +0xEC), up at +0xE8 (+0xF0; at once when the
    // state's +0x98), both scaled by +0x100 (rising back to 1 over +0xF8 seconds at +0xFC). Into `out`.
    void GetTarget(Mth::Vector* out, bool instant);
    // 0x004FA230 (RET 8): the height above the skater (+0xD0, on a lip +0x138, from 3 while zoomed in) and the
    // distance behind (+0xC4 x the zoom +0x108, which eases at +0x110 toward 1 - in vert air +0x114, on a rail
    // +0x118, on a lip +0x11C - times the look-around's zoom; nearer as the look-around tilts down). Nothing
    // without a skater.
    void GetZoomedOffsets(float* height, float* distance);
};

// The skater's cleanup-state component (type 0xD8CC3428 "SkaterCleanupState", vtable 0x0064BB48, 0x1C bytes; made
// by 0x004FBE20): each frame the skater's shadow and light follow it (CSkater::UpdateShadowLight).
class CSkaterCleanupStateComponent {
public:
    // 0x004FBE20: a new one (the constructor inlined: the base's, its vtable and type; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004FBD90: the destructor body (its vtable, then the base's). 0x004FBDB0 (slot 0): the scalar deleting
    // destructor.
    void DestroyBody();
    CSkaterCleanupStateComponent* Destroy(unsigned int flags);
    // 0x004FBDA0 (slot 2; `params` isn't used): +0x18 cleared. (Two other components' vtables, 0x0064BEF8 and
    // 0x0064C054, share this function.)
    void InitFromStructure(Script::CStruct* params);
    // 0x004FBDE0 (slot 3): +0x18 the object's SkaterState component.
    void Finalize();
    // 0x004FBE00 (slot 1): CSkater::UpdateShadowLight with the object's position and matrix.
    void Update();
};

} // namespace Obj
