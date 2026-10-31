#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GameObjects {

// CPinnedObjectModule::m_lockFlags (+0x8C)
constexpr uint8_t LOCK_FLAG_LAG = 0x1;    // LagRange or lagwobble given: the position follows with a lag
constexpr uint8_t LOCK_FLAG_ANGLES = 0x2; // Angles given: the orientation turned by m_orientationTurn after the target's
constexpr uint8_t LOCK_FLAG_LOCKED = 0x4; // locked (Obj_LockToObject without `off`)
// NOT ORIGINAL - THAW's no_rotate (THUG2 has no such flag; its lock always takes the target's orientation): the object
// goes where the lock puts it but keeps its own orientation. THAW's Santa Monica ferris wheel locks its cars so
// (SM_FerrisWheelRotation): they hang upright as the wheel turns. The bit is free in the original (only 1, 2 and 4
// are ever used); set and cleared by ConfigureFromData, read by Update.
constexpr uint8_t LOCK_FLAG_NO_ROTATE = 0x8;

// The lockobj component (type 0x5CB5CB7E "lockobj", vtable 0x006460E4, 0x90 bytes; the motion components each moving
// object gets, AddMotionComponents): keeps its object locked to another object (Obj_LockToObject) - at the target's
// position plus the offset turned by the target's orientation (or a bone of the target's skeleton), with the
// target's orientation, optionally following with a lag. Vtable: 0 the scalar deleting destructor (0x0041F440), 1
// Update, 2 ConfigureFromData, 3 / 4 / 9 the empty 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 / 8 / 10 0x005BAB90, 11
// the base's 0x0045D720, 12 InvokeMethodByName. Made by 0x0041FF20 (the constructor 0x0041F500 inlined); the
// destructor body is 0x0041F400. Construction, destruction and the lock's update: lock_obj_update.cpp.
class CPinnedObjectModule : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x5CB5CB7E;

    // 0x0041FF20 (registered with the composite object manager): a new one (Construct on zeroed memory; 0 if out of
    // memory).
    static CObjectModule* __cdecl Create();
    // 0x0041F500: the base's constructor, not locked, its vtable and type. Returns this.
    CPinnedObjectModule* Construct();
    // 0x0041F400: its vtable back, then the base's body.
    void DestroyBody();
    // 0x0041F440 (vtable slot 0)
    CPinnedObjectModule* Destroy(unsigned int flags);
    // 0x0041F760: the lock's update - finds the target by id, else by name (none: nothing), takes its orientation
    // (its model's matrix when the model is followed - a model with +0x234 set, or a skater -, else its display
    // matrix) or, on a bone, the bone's turned by it with "at" pointing to the bone's parent; puts the object at its
    // position plus the offset turned by it (with a lag: part of the way, m_lag, then the next lag from the wobble),
    // turns it by m_orientationTurn when LOCK_FLAG_ANGLES, copies the target's velocity and sets the display matrix.
    void UpdateLock();

    // 0x0041F520 (vtable slot 2): `off` unlocks (nothing else read). Otherwise locked to ObjectName (or id) at the
    // unnamed vector (else BoneOffset; default (0, 0, 0)), on `bone` of the target's skeleton if given; the object
    // gets the target's update priority less one (updated after it). LagRange (min, max; default 1, 1) and lagwobble
    // (two frequencies; default 0, 0) make the position follow with a lag: m_lag = min + (max - min) * (1 + sin(t * f2)
    // * sin(t * f1)) / 2, t = (milliseconds + a random phase) mod 1024 in steps of 2 pi / 1024. Angles turn the
    // orientation by that rotation after taking the target's. NOT ORIGINAL: no_rotate (LOCK_FLAG_NO_ROTATE).
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x0041FF90 (vtable slot 1): when locked, the lock's update (UpdateLock). NOT ORIGINAL: with LOCK_FLAG_NO_ROTATE the object's own
    // orientation (matrix and display matrix) is put back after it - the position stays the lock's.
    void Update();
    // 0x0041F410 (vtable slot 12): Obj_LockToObject (0x785E57C7) - ConfigureFromData(params) through the vtable,
    // 1; any other member: 2 (not its own).
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    uint32_t m_entityLabel;    // +0x18 ObjectName (0 none)
    uint32_t m_targetId;      // +0x1C id (default the "no id" value at 0x0064780C)
    uint32_t m_bone;          // +0x20 bone (0 none)
    Maths::Vector m_shift;     // +0x24 (w 1)
    Maths::Matrix m_orientationTurn;   // +0x34 the rotation of Angles
    float m_lag;              // +0x74
    float m_lagMin;           // +0x78 LagRange
    float m_lagMax;           // +0x7C
    float m_wobble[2];        // +0x80 lagwobble
    int m_wobbleSeed;              // +0x88 Rnd(1024)
    uint8_t m_lockFlags;      // +0x8C LOCK_FLAG_*
    uint8_t pad8D[3];
};
static_assert(offsetof(CPinnedObjectModule, m_entityLabel) == 0x18 && offsetof(CPinnedObjectModule, m_shift) == 0x24 &&
                  offsetof(CPinnedObjectModule, m_orientationTurn) == 0x34 && offsetof(CPinnedObjectModule, m_lag) == 0x74 &&
                  offsetof(CPinnedObjectModule, m_wobble) == 0x80 && offsetof(CPinnedObjectModule, m_wobbleSeed) == 0x88 &&
                  offsetof(CPinnedObjectModule, m_lockFlags) == 0x8C && sizeof(CPinnedObjectModule) == 0x90,
              "layout must match the original (0x0041FF20 allocates 0x90 bytes)");

} // namespace Obj
