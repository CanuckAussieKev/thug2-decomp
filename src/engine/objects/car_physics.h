#pragma once

#include <cstddef>
#include <cstdint>

#include "base/maths/mat4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class KinematicsModule;
class DormancyModule;

constexpr std::size_t kAutomobileDynamicsModuleWheelSpinOffset = 0x18;
constexpr std::size_t kAutomobileDynamicsModuleLastSpeedOffset = 0x30;
constexpr std::size_t kAutomobileDynamicsModuleTiltDivisorOffset = 0x40;
constexpr std::size_t kAutomobileDynamicsModuleBodyShownOffset = 0x44;
constexpr std::size_t kAutomobileDynamicsModuleSuspendOffset = 0x54;
constexpr std::size_t kAutomobileDynamicsModuleBytes = 0x58;
// The car physics component (type 0xC695136D "carphysics", vtable 0x00645E6C, 0x58 bytes; made by 0x0041BD90, and by
// spawn_automobile through 0x0041BAD0): a car's look - its wheels spinning with its speed, the steering wheels turned
// towards its path, the body tilting with acceleration - written into its skeleton's bone matrices after each update.
// Angles in degrees. The tilt's limits and rates are script globals (re-read every update while the global 0x4CF13D8F
// is set) that `params` may override.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize; the rest the base's / shared (4
// / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20, 12 0x0051ADC0).
class AutomobileDynamicsModule final : public ObjectModule {
public:
    AutomobileDynamicsModule(const AutomobileDynamicsModule&) = delete;
    AutomobileDynamicsModule& operator=(const AutomobileDynamicsModule&) = delete;
    static constexpr uint32_t kType = 0xC695136D;

    // 0x0041BD90 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined; 0
    // if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x0041B910 (not a Ghidra function; registered with Create): its member functions' table (0x006846A8; the
    // functions are put in by the static initialiser at 0x00643E80).
    static void publish_method_table();
    // 0x0041BAD0: its type, no spin or steering, the body shown.
    AutomobileDynamicsModule();
    // 0x0041B930: the base's body.
    ~AutomobileDynamicsModule() override;
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x0041B940 (vtable slot 2): the tilt's minimum (0x877DB8DA), maximum (0xFB3464DF), rate (0xFD8BC8D6) and
    // acceleration divisor (0xB2C7447B): the script globals of those names, overridden by `params`.
    void configure_from_data(script::Struct* params) override;
    // 0x0041BE10 (vtable slot 3): the object's motion, suspend, skeleton and model components; with a skeleton, its
    // skeleton and the model, and the distance between the positions of bones 2 and 4 (each times bone 0's matrix).
    void seal() override;
    // 0x0041C1B0 (vtable slot 1): unless suspended (the suspend component's +0x30, or it says not to update), dead or
    // without a skeleton: update_angles, then update_bones.
    void tick_frame() override;

    // 0x0041BB30: the wheels' spin (the motion's speed; kept within 0-360), the steering (towards
    // -asin(wheel_base_ / the distance to the path follower's point) - negated for a negative turn, 0 without one or
    // without a turn -, by at most 60 degrees a second) and the tilt (towards -acceleration / divisor, within the
    // limits, by at most rate * 60 a second).
    void update_angles();
    // 0x0041C0B0: refresh_bone_rig; with the body hidden, bone 0's matrix scaled to nothing.
    void update_bones();
    // The car's body tilt, its wheels' spin and their steering angle.
    struct WheelPose {
        float tilt{};
        float spin{};
        float steer{};
    };

    // The skeleton and the model the car's bones are written to.
    struct BoneRigTarget {
        void* skeleton{};
        void* model{};
    };

    // 0x0041BF00 (cdecl): every bone's matrix from the model's description: the two front (0x80212262, 0x1E45B7C1)
    // and two back (0x69428757, 0xF04BD6ED) wheels turned about X by the spin, the steering wheels (0x6E2F434E,
    // 0xF72612F4) about Z by the steering then about X by the spin, the body (0x88C21962 "car") about X by the tilt;
    // each but the first then times its parent's matrix.
    static void refresh_bone_rig(BoneRigTarget rig_target, WheelPose wheel_pose);

    // --- the member functions scripts call (the table at 0x006846A8) ---
    // 0x0041B9E0 [0xA2331896]: the body shown when the unnamed integer isn't 0 (none: hidden). 1.
    int show_body(script::Struct* params, script::Script* script);
    // 0x0041BA20 [0xC9F0DB4B]: nothing. 1.
    static int do_nothing(script::Struct* params, script::Script* script);

private:
    float wheel_spin_{0.0F};   // +0x18 degrees, 0-360
    float steer_{0.0F};        // +0x1C
    float target_steer_{0.0F}; // +0x20
    float lean_angle_{}; // +0x24
    float target_tilt_{};  // +0x28
    float wheel_base_{};   // +0x2C
    float last_speed_{};   // +0x30
    float min_tilt_{};     // +0x34 0x877DB8DA
    float max_tilt_{};     // +0x38 0xFB3464DF
    float tilt_rate_{};    // +0x3C 0xFD8BC8D6
    float tilt_divisor_{}; // +0x40 0xB2C7447B
    bool body_shown_{true}; // +0x44
    std::array<uint8_t, 3> pad45{};
    void* mesh_instance_{}; // +0x48 the model component's model (+0x208)
    void* bone_rig_{};      // +0x4C the skeleton component's skeleton (+0x18)
    KinematicsModule* motion_{}; // +0x50 the motion component
    DormancyModule* suspend_{};  // +0x54 the suspend component
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(AutomobileDynamicsModule, wheel_spin_) == kAutomobileDynamicsModuleWheelSpinOffset &&
                          offsetof(AutomobileDynamicsModule, last_speed_) == kAutomobileDynamicsModuleLastSpeedOffset &&
                          offsetof(AutomobileDynamicsModule, tilt_divisor_) == kAutomobileDynamicsModuleTiltDivisorOffset &&
                          offsetof(AutomobileDynamicsModule, body_shown_) == kAutomobileDynamicsModuleBodyShownOffset &&
                          offsetof(AutomobileDynamicsModule, suspend_) == kAutomobileDynamicsModuleSuspendOffset && sizeof(AutomobileDynamicsModule) == kAutomobileDynamicsModuleBytes,
                      "layout must match the original");
    }
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by game/commands/member_function_code.cpp).
std::array<script::MemberFunctionEntry, 3>& car_physics_member_functions();
} // namespace objects
