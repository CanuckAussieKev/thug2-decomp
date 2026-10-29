#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/quat.h"
#include "game/mth/vector.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// An object's bones turned to look at something (Gfx::CBoneLookAt, 0x1A4 bytes, vtable 0x006488D8 - its base is the
// pose modifier at 0x0064877C, 0x14 bytes: +4 its owner, +8 the object, +0x10 its id; the base's code is at
// 0x00481710-0x00481770): up to four bones, each turned each frame towards a target - an object (followed, with an
// offset) or a fixed point - within angle limits, eased from the last frame's turn. From the disassembly of
// 0x00487D40-0x00488DA1 (with 0x00487D60 / 0x00487D90 / 0x00487DC0, code Ghidra never made functions of).
namespace Gfx {

// Euler angles <-> quaternions, the angles (x, y, z) in radians.
// 0x00487F40 (cdecl): the rotation of these angles. Returns out.
Mth::Quat* __cdecl EulerToQuat(Mth::Quat* out, const Mth::Vector* angles);
// 0x00488020 (cdecl): the angles of a rotation (w 1). Returns out.
Mth::Vector* __cdecl QuatToEuler(Mth::Vector* out, const Mth::Quat* rotation);
// 0x00487EE0 (cdecl): `v` clamped (SClampVector::Clamp) into `out`. Returns out.
Mth::Vector* __cdecl ClampedVector(Mth::Vector* out, const Mth::Vector* v, const Mth::Vector* min,
                                   const Mth::Vector* max);
// A vector clamped.
struct SClampVector : Mth::Vector {
    // 0x00487E00: each of the four components kept to min..max (above max: max; then below min: min - a NaN stays).
    // Returns this.
    Mth::Vector* Clamp(const Mth::Vector* min, const Mth::Vector* max);
};

class CBoneLookAt {
public:
    // One bone's look-at (0x64 bytes).
    struct SBone {
        uint32_t m_bone;       // +0x00 its name (-1: none)
        uint32_t m_target;     // +0x04 the object looked at (its id)
        float m_weight;        // +0x08 the clamped angles' scale (1)
        int m_axis;            // +0x0C which way the bone looks: +-1 / +-2 x / z turned a quarter, +-3 as it is
        Mth::Vector m_min;     // +0x10 the angle limits (radians; w 1)
        Mth::Vector m_max;     // +0x20
        Mth::Vector m_targetPos; // +0x30 the point looked at
        Mth::Vector m_offset;  // +0x40 from the target object's position (w 1)
        Mth::Quat m_rotation;  // +0x50 the turn last applied
        bool m_active;         // +0x60
        bool m_fixedPos;       // +0x61: m_targetPos given (the target object isn't followed)
        bool m_clamp;          // +0x62: the angles clamped and scaled (else each one past its limits zeroed)
        uint8_t pad63;
    };
    static_assert(sizeof(SBone) == 0x64, "layout must match the original");

    // 0x00488300: the base's constructor (0x00481710 with `owner`), no bones, the object: the owner's (0x00481760)
    // or `object` without an owner. Returns this.
    CBoneLookAt* Construct(void* owner, void* object);
    // 0x00487D40: the destructor's body (the base's, 0x00481730, after).
    void DestroyBody();
    // 0x004880D0 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    CBoneLookAt* Destroy(unsigned int flags);
    // 0x00488410 (vtable slot 1): updated (slot 5), then each active bone with a name the skeleton has turned towards
    // its target in `rotations` (the pose's quaternions, one a bone) - and the turn kept. True.
    bool Apply(Mth::Quat* rotations);
    // 0x00488B40 (vtable slot 3): the script member functions 0x20D24E9A (on), 0x027B2B1C (whether on), 0x4599FB80
    // (off) and 0xB040E943 (settings) on the bone index=n, or id=<bone>, or all - else bone 0. 2: not one of these.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00488B20 (vtable slot 4): the base's settings (its id: 0x00481740), then bone 0's (Configure).
    void InitFromStructure(Script::CStruct* params);
    // 0x00488100 (vtable slot 5): each bone following an object (not m_fixedPos) gets its target position: the
    // object's position plus the offset (none: left as it was).
    void Update();
    // 0x00488950: settings of bone `index` (with `all`: on / off and the angle limits for every bone with a name).
    void Configure(bool all, int index, Script::CStruct* params);
    // 0x00487D60: the index of the bone named `bone` (-1: none).
    int FindBone(uint32_t bone);
    // 0x00487D90: whether every bone with a name is active.
    bool AllActive();
    // 0x00487DC0 / 0x00487DE0: every bone with a name made active / inactive.
    void ActivateAll();
    void DeactivateAll();
    // 0x00488170 / 0x00488220: every bone with a name given these limits.
    void SetAllMin(const Mth::Vector* min);
    void SetAllMax(const Mth::Vector* max);

    void** vtable;
    void* mp_owner;       // +0x04
    uint8_t* mp_object;   // +0x08 (a composite object)
    uint32_t m_unknownC;  // +0x0C
    uint32_t m_id;        // +0x10
    SBone m_bones[4];     // +0x14
};
static_assert(offsetof(CBoneLookAt, m_bones) == 0x14 && sizeof(CBoneLookAt) == 0x1A4, "layout must match the original");

} // namespace Gfx
