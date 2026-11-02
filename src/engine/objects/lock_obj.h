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

// PinnedObjectModule::lock_flags_ (+0x8C)
constexpr uint8_t kLockFlagLag = 0x1;    // LagRange or lagwobble given: the position follows with a lag
constexpr uint8_t kLockFlagAngles = 0x2; // Angles given: the orientation turned by orientation_turn_ after the target's
constexpr uint8_t kLockFlagLocked = 0x4; // locked (Obj_LockToObject without `off`)
// NOT ORIGINAL - THAW's no_rotate (THUG2 has no such flag; its lock always takes the target's orientation): the object
// goes where the lock puts it but keeps its own orientation. THAW's Santa Monica ferris wheel locks its cars so
// (SM_FerrisWheelRotation): they hang upright as the wheel turns. The bit is free in the original (only 1, 2 and 4
// are ever used); set and cleared by configure_from_data, read by Update.
constexpr uint8_t kLockFlagNoRotate = 0x8;

// The lockobj component (type 0x5CB5CB7E "lockobj", vtable 0x006460E4, 0x90 bytes; the motion components each moving
// object gets, add_motion_components): keeps its object locked to another object (Obj_LockToObject) - at the target's
// position plus the offset turned by the target's orientation (or a bone of the target's skeleton), with the
// target's orientation, optionally following with a lag. Vtable: 0 the scalar deleting destructor (0x0041F440), 1
// Update, 2 configure_from_data, 3 / 4 / 9 the empty 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 / 8 / 10 0x005BAB90, 11
// the base's 0x0045D720, 12 invoke_method_by_name. Made by 0x0041FF20 (the constructor 0x0041F500 inlined); the
// destructor body is 0x0041F400. Construction, destruction and the lock's update: lock_obj_update.cpp.
class PinnedObjectModule final : public ObjectModule {
public:
    PinnedObjectModule(const PinnedObjectModule&) = delete;
    PinnedObjectModule& operator=(const PinnedObjectModule&) = delete;
    static constexpr uint32_t kType = 0x5CB5CB7E;

    // 0x0041FF20 (registered with the composite object manager): a new one (Construct on zeroed memory; 0 if out of
    // memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x0041F500: not locked, its type.
    PinnedObjectModule();
    // 0x0041F400: the base's.
    ~PinnedObjectModule() override;
    void hold_off(bool /*suspend*/) override {} // (slot 7: never suspended)
    // 0x0041F760: the lock's update - finds the target by id, else by name (none: nothing), takes its orientation
    // (its model's matrix when the model is followed - a model with +0x234 set, or a skater -, else its display
    // matrix) or, on a bone, the bone's turned by it with "at" pointing to the bone's parent; puts the object at its
    // position plus the offset turned by it (with a lag: part of the way, m_lag, then the next lag from the wobble),
    // turns it by orientation_turn_ when kLockFlagAngles, copies the target's velocity and sets the display matrix.
    void update_lock();

    // 0x0041F520 (vtable slot 2): `off` unlocks (nothing else read). Otherwise locked to ObjectName (or id) at the
    // unnamed vector (else BoneOffset; default (0, 0, 0)), on `bone` of the target's skeleton if given; the object
    // gets the target's update priority less one (updated after it). LagRange (min, max; default 1, 1) and lagwobble
    // (two frequencies; default 0, 0) make the position follow with a lag: m_lag = min + (max - min) * (1 + sin(t * f2)
    // * sin(t * f1)) / 2, t = (milliseconds + a random phase) mod 1024 in steps of 2 pi / 1024. Angles turn the
    // orientation by that rotation after taking the target's. NOT ORIGINAL: no_rotate (kLockFlagNoRotate).
    void configure_from_data(script::Struct* params) override;
    // 0x0041FF90 (vtable slot 1): when locked, the lock's update (update_lock). NOT ORIGINAL: with kLockFlagNoRotate the object's own
    // orientation (matrix and display matrix) is put back after it - the position stays the lock's.
    void tick_frame() override;
    // 0x0041F410 (vtable slot 12): Obj_LockToObject (0x785E57C7) - configure_from_data(params) through the vtable,
    // 1; any other member: 2 (not its own).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // The fields other code reads and sets.
    uint8_t& lock_flags() { return lock_flags_; }
    [[nodiscard]] uint8_t lock_flags() const { return lock_flags_; }

private:
    uint32_t entity_label_{};        // +0x18 ObjectName (0 none)
    uint32_t target_id_{};           // +0x1C id (default the "no id" value at 0x0064780C)
    uint32_t bone_{};                // +0x20 bone (0 none)
    maths::Vec4 shift_{};            // +0x24 (w 1)
    maths::Mat4 orientation_turn_{}; // +0x34 the rotation of Angles
    float lag_{};                    // +0x74
    float lag_min_{};                // +0x78 LagRange
    float lag_max_{};                // +0x7C
    std::array<float, 2> wobble_{};              // +0x80 lagwobble
    int wobble_seed_{};              // +0x88 Rnd(1024)
    uint8_t lock_flags_{};           // +0x8C LOCK_FLAG_*
    std::array<uint8_t, 3> pad8d_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PinnedObjectModule, entity_label_) == 0x18 && offsetof(PinnedObjectModule, shift_) == 0x24 &&
                          offsetof(PinnedObjectModule, orientation_turn_) == 0x34 &&
                          offsetof(PinnedObjectModule, lag_) == 0x74 && offsetof(PinnedObjectModule, wobble_) == 0x80 &&
                          offsetof(PinnedObjectModule, wobble_seed_) == 0x88 &&
                          offsetof(PinnedObjectModule, lock_flags_) == 0x8C && sizeof(PinnedObjectModule) == 0x90,
                      "layout must match the original (0x0041FF20 allocates 0x90 bytes)");
    }
};

} // namespace objects
