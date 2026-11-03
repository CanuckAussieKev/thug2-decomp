#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/anim_blend.h"
#include <gsl/pointers>

namespace objects {
class GameEntity;
}
namespace script {
class Script;
class Struct;
} // namespace script

// The animation controllers an object's animation blender stacks up (made by the factory 0x00481FB0 from a
// structure's Type, with its Priority): each fills in a pose (gfx::Stance: 64 rotations at +0, 64 translations at
// +0x400, more at +0x800) for the bones of the object's skeleton and answers script member functions. They all
// derive from the base controller (AnimDriver: vtable 0x0064877C, constructed by 0x00481710; e.g. its
// configure_from_data 0x00481740 reads `id`).
//
// Their tables' slot 2 is the shared 0x005BAB90 (ignores its argument) in every one: left out.
namespace gfx {

class Rig;

// The animation timeline a controller plays on (0x00481650 constructs one): an animation between two times, the
// current time stepping by speed * 60 per second in a direction, with a loop type. It's a node of the engine's
// intrusive lists itself (its list_link_ at +0). The original's table (0x00648768: Destroy, Reset, Update, PlayAnim
// and an empty slot) only ever dispatched on timelines of a known type - a plain timeline here (the partial
// animation's, the movies'), an AnimTrack in the blend channels' lists - so the slots are plain methods (AnimTrack
// has its own get_reset / tick_frame / teardown).
struct AnimTimeline {
    using RepeatMode = uint8_t;
    static constexpr int kOnce = 0;
    static constexpr int kCycle = 1;
    static constexpr int kPingPong = 2;
    static constexpr int kHold = 3;

    // 0x00481650: an empty timeline, unlinked (once, forwards, no animation: 0xF60C9090).
    AnimTimeline();
    // 0x00481690: the list node's destructor body - unlinked (the channel's list nodes and the blend timeline use
    // it too). 0x004816E0 (slot 0, also the channel list head's): that, then freed.
    ~AnimTimeline();
    AnimTimeline(const AnimTimeline&) = delete;
    AnimTimeline& operator=(const AnimTimeline&) = delete;
    AnimTimeline* teardown(uint8_t flags);
    // 0x00481380 (slot 1): back to the constructor's times, speed, direction, loop type and animation.
    void get_reset();
    // 0x00481640 (slot 2): the current time stepped (next_time).
    void tick_frame();
    // 0x00481430 (slot 3): `anim` from `from` to `to` (backwards when `to` isn't above it) at `speed` (per 1/60 s);
    // `blendPeriod` isn't used here.
    void play_anim(uint32_t anim, float from, float to, int loop_type, float blend_period, float speed);
    // 0x004813B0: the start, current and end times in the direction it plays (start and end swapped backwards).
    void get_times(float* start, float* current, float* end) const;
    // 0x004813F0: whether a once / ping-pong / hold timeline (not a cycling one) is at its end.
    [[nodiscard]] bool is_accomplished() const;
    // 0x004814B0: the time a frame on (the frame's slow-motion seconds x 60 x speed), wrapped, held or turned
    // round at the ends by the loop type, within the start and end; sets the complete flag. Unrounded (ST0).
    double next_time();
    // 0x004816B0: `seconds` (in frames) added to the current time, then wrapped / held as a step of no time does.
    void advance_clock(float frames);

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] float outset() const { return outset_; }
    void set_outset(float value) { outset_ = value; }
    [[nodiscard]] float ongoing() const { return ongoing_; }
    void set_ongoing(float value) { ongoing_ = value; }
    [[nodiscard]] float finish() const { return finish_; }
    void set_finish(float value) { finish_ = value; }
    [[nodiscard]] float speed() const { return speed_; }
    void set_speed(float value) { speed_ = value; }
    [[nodiscard]] int direction() const { return direction_; }
    void set_direction(int value) { direction_ = value; }
    [[nodiscard]] int repeat_mode() const { return repeat_mode_; }
    void set_repeat_mode(int value) { repeat_mode_ = value; }
    void set_complete(bool value) { complete_ = value; }
    [[nodiscard]] uint32_t anim() const { return anim_; }
    void set_anim(uint32_t value) { anim_ = value; }

private:
    memory::ListNode list_link_{}; // +0x00 (data = this; the original's node size: the fields keep their offsets)
    float outset_{};               // +0x14
    float ongoing_{};              // +0x18
    float finish_{};               // +0x1C
    float speed_{};                // +0x20 (per 1/60 s)
    int direction_{};              // +0x24 0 forwards, -1 backwards
    int repeat_mode_{};            // +0x28
    bool complete_{};              // +0x2C
    std::array<uint8_t, 3> pad2d_{};
    uint32_t anim_{}; // +0x30 the animation's name
};

// The base controller (vtable 0x0064877C). Zeroed memory from the current heap (the standard controller has pools
// of its own).
class AnimDriver {
public:
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* memory);

    // 0x00481710: on `timeline`, no object, priority 0, id 0.
    explicit AnimDriver(AnimTimeline* timeline);
    // 0x00481730 / 0x004817A0: nothing to do.
    virtual ~AnimDriver() = default;
    AnimDriver(const AnimDriver&) = delete;
    AnimDriver& operator=(const AnimDriver&) = delete;

    // Slot 1: the controller's part of the pose (64 rotations at +0, 64 translations at +0x400). False in the base.
    virtual bool fetch_pose(Stance* /*pose*/) { return false; }
    // Slot 3: a script member function: 0 false / 1 handled / 2 not its own (the base's: 0x0051ADC0, every name not
    // its own).
    virtual int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x00481740 (slot 4; 0x004A4610 in most controllers' tables is a jump to it): `id` (0x40C698AF) from the
    // parameters.
    virtual void configure_from_data(script::Struct* params);
    // Slot 5: once a frame (nothing in the base).
    virtual void tick_frame() {}

    // 0x00481760: the object its timeline (a blend timeline) belongs to.
    objects::GameEntity* fetch_subject();
    // 0x00481770: the object's skeleton component (0x222756D5)'s skeleton (0x005480F0: its +0x18), or null.
    Rig* fetch_rig();

    // The fields other code reads and sets.
    [[nodiscard]] AnimTimeline* timeline_ptr() const { return timeline_ptr_; }
    [[nodiscard]] objects::GameEntity* owner_object() const { return owner_object_; }
    void set_owner_object(objects::GameEntity* value) { owner_object_ = value; }
    [[nodiscard]] int rank() const { return rank_; }
    void set_rank(int value) { rank_ = value; }
    [[nodiscard]] uint32_t ident() const { return ident_; }

private:
    AnimTimeline* timeline_ptr_;        // +0x04 (the timeline of the blender channel it belongs to)
    objects::GameEntity* owner_object_{nullptr}; // +0x08 (set by some controllers only; fetch_subject asks the channel)
    int rank_{0};                                // +0x0C
    uint32_t ident_{0};                          // +0x10
};

// The standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes from its own item pools 0x00701430):
// plays an animation of the object's anim set on the channel's timeline.
class StandardAnimController final : public AnimDriver {
public:
    StandardAnimController(const StandardAnimController&) = delete;
    StandardAnimController& operator=(const StandardAnimController&) = delete;
    // 0x004A4500: on `timeline`; a frame source of its own (0x0048BFC0), the object's anim set (the blender's
    // 0x00419AB0); no frame, no frame index (-1).
    explicit StandardAnimController(AnimTimeline* timeline);
    // 0x004A4590: the frame's index let go, the frame source freed, then the base's.
    ~StandardAnimController() override;
    // 0x004A4F70 (slot 1)
    bool fetch_pose(Stance* pose) override;
    // 0x004A4670 (slot 3): PlayAnim (0xAF2FAE19: AnimName, StartTime, EndTime, RepeatMode, BlendPeriod, Speed),
    // 0xC68365EB (a new frame source for Anim), 0x92267217 (0x00485270 with the global 0x006F92B0's +0xD4),
    // stale_cache (refetches, then 2).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004A4620 (slot 5): the timeline steps; events between the two times; looping back refetches the frame.
    void tick_frame() override;

    // 0x004A4F30 / 0x004A5600: the class's operator new / delete (its item pools).
    static void* operator new(size_t size, int unused) noexcept;
    static void operator delete(void* item);
    static void operator delete(void* item, int /*unused*/) { operator delete(item); }

private:
    void* cur_frame_data_{}; // +0x14 (unused)
    void* frame_source_;   // +0x18 the current frame (0x0048CFA0 / 0x0048CFD0)
    gsl::owner<void*> blend_source_; // +0x1C its frame source
    uint32_t anim_set_;    // +0x20 added to an animation's name
    int frame_index_{-1};  // +0x24 (-1 none)
};

// The 0x7E1940A1 controller (vtable 0x00649F68, 0x24 bytes): an animation of the object's anim set blended in
// with a weight, without a timeline of its own.
class WeightedAnimController final : public AnimDriver {
public:
    WeightedAnimController(const WeightedAnimController&) = delete;
    WeightedAnimController& operator=(const WeightedAnimController&) = delete;
    // 0x004A5010: on `timeline`; the object is the channel's (or `object` without a timeline).
    WeightedAnimController(AnimTimeline* timeline, objects::GameEntity* object);
    // 0x004A4820
    ~WeightedAnimController() override;
    // 0x004A5660 (slot 1): nothing (false) without a frame or with a weight of 0 or less.
    bool fetch_pose(Stance* pose) override;
    // 0x004A4890 (slot 3): 0xAF2FAE19 / 0x5E3F0F17 (the animation 0xD697A799 or AnimName, the weight
    // 0xC6AE16AC), 0xC68365EB, stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

private:
    void* frame_source_; // +0x14
    gsl::owner<void*> blend_source_; // +0x18
    uint32_t anim_set_;  // +0x1C
    float weight_{0.0f}; // +0x20
};

// The wobble controller (type "wobble", vtable 0x00649F80, 0x38 bytes): plays the timeline's animation around a
// target time, swinging by two sines of the game clock (SetWobbleDetails).
class WobbleDriver final : public AnimDriver {
public:
    WobbleDriver(const WobbleDriver&) = delete;
    WobbleDriver& operator=(const WobbleDriver&) = delete;
    // 0x004A49F0
    explicit WobbleDriver(AnimTimeline* timeline);
    // 0x004A4A80
    ~WobbleDriver() override;
    // 0x004A5110 (slot 1)
    bool fetch_pose(Stance* pose) override;
    // 0x004A4AF0 (slot 3): PlayAnim, 0xD0209498 (the target as a fraction 0xD747FA0 of the timeline, 0..1),
    // SetWobbleDetails (all five or nothing), stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004A52E0 (slot 5)
    void tick_frame() override;
    // 0x004A5180: the time to show now (ST0).
    float wobble_time();

private:
    float amp_a_;        // +0x14 wobbleAmpA
    float amp_b_;        // +0x18 wobbleAmpB
    float k1_;           // +0x1C wobbleK1 (cycles per ms of the first sine)
    float k2_;           // +0x20 wobbleK2
    float spaz_factor_;  // +0x24 spazFactor
    float aim_point_{0.0f}; // +0x28 the time it wobbles around
    gsl::owner<void*> blend_source_; // +0x2C
    void* frame_source_; // +0x30
    uint32_t anim_set_;  // +0x34
};

// The pose controller (type "pose", vtable 0x00649F98, 0x854 bytes): a fixed pose (identity rotations).
class StanceDriver final : public AnimDriver {
public:
    StanceDriver(const StanceDriver&) = delete;
    StanceDriver& operator=(const StanceDriver&) = delete;
    // 0x004A5730
    explicit StanceDriver(AnimTimeline* timeline);
    // 0x004A4D20
    ~StanceDriver() override;
    // 0x004A4D30 (slot 1): its pose copied out.
    bool fetch_pose(Stance* pose) override;

private:
    Stance stance_{}; // +0x14
};

// The partial animation controller (type "partialAnim", vtable 0x00649FB0, 0x24 bytes): a second animation on a
// timeline of its own.
class PartialClipDriver final : public AnimDriver {
public:
    PartialClipDriver(const PartialClipDriver&) = delete;
    PartialClipDriver& operator=(const PartialClipDriver&) = delete;
    // 0x004A5350
    explicit PartialClipDriver(AnimTimeline* timeline);
    // 0x004A4D50
    ~PartialClipDriver() override;
    // 0x004A5430 (slot 1)
    bool fetch_pose(Stance* pose) override;
    // 0x004A54A0 (slot 3): 0x6AAEB76F (a step of 0x06C9F278 frames), 0xBD4EDD44 (Speed), GetPartialAnimParams,
    // 0xF5E2B871 (reverses), stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004A4DD0 (slot 4): the base's, then AnimName's times, loop type, Speed and Current on its timeline.
    void configure_from_data(script::Struct* params) override;
    // 0x004A4EF0 (slot 5)
    void tick_frame() override;

private:
    AnimTimeline* partial_; // +0x14 its own timeline
    gsl::owner<void*> blend_source_; // +0x18
    void* frame_source_{nullptr};    // +0x1C
    uint32_t anim_set_;     // +0x20
};

} // namespace gfx
