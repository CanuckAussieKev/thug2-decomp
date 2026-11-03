#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The suspend component (type 0xCE0CA665, "suspend"; vtable 0x006465C8, 0x4C bytes; made by 0x00434290): suspends its
// object while the camera is further than SuspendDistance from it (unless NeverSuspend - the default until a distance
// is given), and decides how often its model animates and whether it's drawn (its levels of detail, lod_dist1-4). The
// component suspends itself at its first update: a task (made by publish_method_table) runs every suspend component
// each frame instead. Its script member function: Obj_ResetNoSuspendCount (the table at 0x00684C48, filled by
// 0x00644560).
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 configure_from_data; 3 Finalize; 4 / 9 the empty 0x005480A0; 5
// the shared 0x004E3F90; 6 0x0048DCD0; 7 / 8 / 10 the empty 0x005BAB90 (it can't be suspended by its object); 11
// 0x0042BB20 (shared); 12 invoke_method_by_name 0x0051ADC0 (none by name).
class DormancyModule final : public ObjectModule {
public:
    static constexpr int kCheckFrames = 10; // frames between the LOD / suspension checks

    DormancyModule(const DormancyModule&) = delete;
    DormancyModule& operator=(const DormancyModule&) = delete;
    static constexpr uint32_t kType = 0xCE0CA665;

    // 0x00434290 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x00434610 (not a Ghidra function; registered with Create): its script member functions registered, then the
    // suspend task made (create_suspend_task).
    static void publish_method_table();
    // 0x00433F50: its type; NeverSuspend; the suspend distance
    // DefaultMovingObjectSuspendDistance feet (and its square); 10 frames before the first check, 3 before the LOD
    // checks.
    DormancyModule();
    // 0x00433E10: the base's body.
    ~DormancyModule() override;
    void hold_off(bool /*suspend*/) override {} // (slot 7: never suspended)
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00433E50 (vtable slot 1): suspends itself (the base's hold_off(true)) - the task does its work.
    void tick_frame() override;
    // 0x00434000 (vtable slot 2): SuspendDistance (feet; 0: lod_dist1, else the default) - which turns NeverSuspend
    // off -, NeverSuspend; the LOD distances (feet): none with NoLOD (the script integer), else lod_dist1 (50),
    // lod_dist2 (lod_dist1 + 75), lod_dist3 / lod_dist4 (1000001 / 1000002), the first two scaled by the script
    // floats LOD0DistanceMultiplier / LOD1DistanceMultiplier (out-of-order distances only reported, compiled out).
    void configure_from_data(script::Struct* params) override;
    // 0x004341C0 (vtable slot 3): the model component found and kept.
    void seal() override;

    // 0x00433E60: 10 frames before the next suspend check and the LOD checks.
    void reset_counts();
    // 0x00433E70: the LOD distances out of reach (no LOD).
    void disable_lod();
    // 0x00433E90: whether it's too far away (suspended).
    bool is_far();
    // 0x00433EA0: whether the model animates this frame: always within the LOD frames; within lod_dist1, every frame
    // within AnimLODInterleave2, every second one within AnimLODInterleave4, every fourth beyond (the frame counters
    // 0x006951B4 / 0x006951B8); never beyond lod_dist1. The distance squared through `distance_squared` if given.
    bool wants_animating(float* distance_squared);
    // 0x004342F0: whether the model is drawn: not when the object is invisible; always without LOD, in more than one
    // viewport (0x00701378 > 1) or when the model asks (+0x234); otherwise within lod_dist2 and its bounding sphere in
    // view (0x0048C490). Told to the model (0x00493C60); a model just made visible is updated at once.
    void refresh_shown_state();

    // --- script member functions (the table at 0x00684C48) ---
    // 0x004341E0 (not a Ghidra function) Obj_ResetNoSuspendCount: reset_counts and the object unsuspended. True.
    bool obj_reset_no_suspend_count(script::Struct* params, script::Script* script);

    // The fields other code reads and sets.
    [[nodiscard]] bool never_suspend() const { return never_suspend_; }
    [[nodiscard]] float suspend_distance_squared() const { return suspend_distance_squared_; }
    void set_distance_squared(float value) { distance_squared_ = value; }
    int& frames_before_check() { return frames_before_check_; }
    [[nodiscard]] int frames_before_check() const { return frames_before_check_; }
    [[nodiscard]] ObjectModule* mesh_instance() const { return mesh_instance_; }
    [[nodiscard]] bool far() const { return far_; }
    void set_far(bool value) { far_ = value; }
    std::array<float, 4>& lod_distances() { return lod_distances_; }
    [[nodiscard]] const std::array<float, 4>& lod_distances() const { return lod_distances_; }
    int& frames_before_lod() { return frames_before_lod_; }
    [[nodiscard]] int frames_before_lod() const { return frames_before_lod_; }

private:
    bool never_suspend_{true}; // +0x18
    std::array<uint8_t, 3> pad19{};
    float suspend_distance_;         // +0x1C (inches)
    float suspend_distance_squared_; // +0x20
    float distance_squared_{};       // +0x24 to the camera, at the last check (0 when not checked)
    int frames_before_check_{kCheckFrames}; // +0x28
    ObjectModule* mesh_instance_{};  // +0x2C
    bool far_{};                     // +0x30 suspended by distance
    std::array<uint8_t, 3> pad31{};
    std::array<float, 4> lod_distances_{}; // +0x34 (inches)
    int frames_before_lod_{3}; // +0x44
    uint32_t unused48_{};    // +0x48
};

// 0x00433E20: a new frame for the animation LOD counters (0x006951B0 counts frames; 0x006951B4 starts each frame at
// the count, 0x006951B8 at the count with the low bits changed by its second bit - spreading the interleaved objects
// over the frames).
void next_suspend_frame();
// 0x004345B0: the suspend task's holder made (0x006951BC; 4 bytes - the task - by 0x00434520).
void create_suspend_task();
struct SuspendTaskHolder {
    // 0x00434520: the task (tasks::Task, 0x20 bytes, priority -1100, callback suspend_task, its data this holder) made
    // and added to the main loop's logic tasks (none if out of memory: null added).
    SuspendTaskHolder();

private:
    tasks::SchedulerTask* job_handle_;
};
// 0x00434420 (not a Ghidra function; the task's callback): every suspend component: its check frames counted down
// (all of them at once: the original doesn't move on to the next component while counting), else - unless the camera
// data (the skate module's +0x3DC) says otherwise or it never suspends - after its LOD frames, its distance squared to
// the camera against the suspend distance; the object suspended or woken to match; then the model updated (during the
// LOD frames: shown) or its visibility decided (refresh_shown_state).
void suspend_task(tasks::Task<SuspendTaskHolder>* task);

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
std::array<script::MemberFunctionEntry, 2>& suspend_component_member_functions();
} // namespace objects
