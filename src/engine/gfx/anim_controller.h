#pragma once

#include <cstddef>
#include <cstdint>

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
// Vtable slots: 0 the scalar deleting destructor, 1 fetch_pose(pose), 2 (0x005BAB90: ignores its argument),
// 3 invoke_method_by_name(name, params, script) -> 0 false / 1 handled / 2 not its own, 4 configure_from_data(params),
// 5 Update (once a frame).
namespace gfx {

// The animation timeline a controller plays on (vtable 0x00648768; 0x00481650 constructs one): an animation
// between two times, the current time stepping by speed * 60 per second in a direction, with a loop type. It's a
// node of the engine's intrusive lists (memory::ListNode: data, priority, next, prev at +4..+0x10).
// Vtable slots: 0 Destroy, 1 Reset, 2 Update, 3 PlayAnim, 4 (0x005BAB90: ignores its argument).
struct AnimTimeline {
    enum RepeatMode : int { kOnce = 0, kCycle = 1, kPingPong = 2, kHold = 3 };

    // 0x00481650: an empty timeline, unlinked (once, forwards, no animation: 0xF60C9090).
    AnimTimeline* make_new();
    // 0x00481690: the list node's destructor body - its vtable 0x006486CC, unlinked (the channel's list nodes and
    // the blend timeline use it too). 0x004816E0 (slot 0, also the channel list head's): that, then freed.
    void destroy_body();
    AnimTimeline* teardown(uint8_t flags);
    // 0x00481380 (slot 1): back to the constructor's times, speed, direction, loop type and animation.
    void get_reset();
    // 0x00481640 (slot 2): the current time stepped (next_time).
    void tick_frame();
    // 0x00481430 (slot 3): `anim` from `from` to `to` (backwards when `to` isn't above it) at `speed` (per 1/60 s);
    // `blendPeriod` isn't used here.
    void play_anim(uint32_t anim, float from, float to, int loop_type, float blend_period, float speed);
    // 0x004813B0: the start, current and end times in the direction it plays (start and end swapped backwards).
    void get_times(float* start, float* current, float* end);
    // 0x004813F0: whether a once / ping-pong / hold timeline (not a cycling one) is at its end.
    bool is_accomplished();
    // 0x004814B0: the time a frame on (the frame's slow-motion seconds x 60 x speed), wrapped, held or turned
    // round at the ends by the loop type, within the start and end; sets the complete flag. Unrounded (ST0).
    double next_time();
    // 0x004816B0: `seconds` (in frames) added to the current time, then wrapped / held as a step of no time does.
    void advance_clock(float frames);

    void** vtable_;
    uint32_t unknown04_[4];
    float outset_;    // +0x14
    float ongoing_;   // +0x18
    float finish_;    // +0x1C
    float speed_;     // +0x20 (per 1/60 s)
    int direction_;   // +0x24 0 forwards, -1 backwards
    int repeat_mode_; // +0x28
    bool complete_;   // +0x2C
    uint8_t pad2d_[3];
    uint32_t anim_; // +0x30 the animation's name
};
static_assert(offsetof(AnimTimeline, outset_) == 0x14 && offsetof(AnimTimeline, anim_) == 0x30 &&
                  sizeof(AnimTimeline) == 0x34,
              "layout must match the original");

// The base controller's fields.
struct AnimDriver {
    void** vtable_;
    AnimTimeline* timeline_ptr_;        // +0x04 (the timeline of the blender channel it belongs to)
    objects::GameEntity* owner_object_; // +0x08 (set by some controllers only; fetch_subject asks the channel)
    int rank_;                          // +0x0C
    uint32_t ident_;                    // +0x10

    // 0x00481710: on `timeline`, no object, priority 0, id 0 (vtable 0x0064877C). Returns this.
    AnimDriver* make_new(AnimTimeline* timeline);
    // 0x00481730: the base's vtable back (its destructor body). 0x004817A0 (slot 0): that, then freed.
    void destroy_body();
    AnimDriver* teardown(uint8_t flags);
    // 0x00481740 (slot 4): `id` (0x40C698AF) from the parameters (read_hash_value's result).
    bool configure_from_data(script::Struct* params);
    // 0x00481760: the object its timeline (a blend timeline) belongs to.
    objects::GameEntity* fetch_subject();
    // 0x00481770: the object's skeleton component (0x222756D5)'s skeleton (0x005480F0: its +0x18), or null.
    uint8_t* fetch_rig();

    // 0x004A4610 (slot 4 of the controllers without one of their own): a jump to the base's configure_from_data
    // (0x00481740).
    void base_init_from_structure(script::Struct* params);
};
static_assert(sizeof(AnimDriver) == 0x14, "layout must match the original");

// The standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes from its own item pools 0x00701430):
// plays an animation of the object's anim set on the channel's timeline.
class StandardAnimController : public AnimDriver {
public:
    // 0x004A4500: on `timeline`; a frame source of its own (0x0048BFC0), the object's anim set (the blender's
    // 0x00419AB0); no frame, no frame index (-1). Returns this.
    StandardAnimController* make_new(AnimTimeline* timeline);
    // 0x004A4590: the frame's index let go, the frame source freed, then the base's.
    void destroy_body();
    // 0x004A55E0 (slot 0): back to the item pools.
    StandardAnimController* teardown(uint8_t flags);
    // 0x004A4F70 (slot 1)
    bool fetch_pose(uint8_t* pose);
    // 0x004A4670 (slot 3): PlayAnim (0xAF2FAE19: AnimName, StartTime, EndTime, RepeatMode, BlendPeriod, Speed),
    // 0xC68365EB (a new frame source for Anim), 0x92267217 (0x00485270 with the global 0x006F92B0's +0xD4),
    // stale_cache (refetches, then 2).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x004A4620 (slot 5): the timeline steps; events between the two times; looping back refetches the frame.
    void tick_frame();

    // 0x004A4F30 / 0x004A5600: the class's operator new / delete (its item pools).
    static void* operator new(size_t size, int unused);
    static void operator delete(void* item);
    static void operator delete(void* item, int) { operator delete(item); }

    void* cur_frame_data_; // +0x14 (unused)
    void* frame_source_;   // +0x18 the current frame (0x0048CFA0 / 0x0048CFD0)
    void* blend_source_;   // +0x1C its frame source
    uint32_t anim_set_;    // +0x20 added to an animation's name
    int frame_index_;      // +0x24 (-1 none)
};
static_assert(sizeof(StandardAnimController) == 0x28, "layout must match the original");

// The 0x7E1940A1 controller (vtable 0x00649F68, 0x24 bytes): an animation of the object's anim set blended in
// with a weight, without a timeline of its own.
class WeightedAnimController : public AnimDriver {
public:
    // 0x004A5010: on `timeline`; the object is the channel's (or `object` without a timeline). Returns this.
    WeightedAnimController* make_new(AnimTimeline* timeline, objects::GameEntity* object);
    // 0x004A4820
    void destroy_body();
    // 0x004A50B0 (slot 0)
    WeightedAnimController* teardown(uint8_t flags);
    // 0x004A5660 (slot 1): nothing (false) without a frame or with a weight of 0 or less.
    bool fetch_pose(uint8_t* pose);
    // 0x004A4890 (slot 3): 0xAF2FAE19 / 0x5E3F0F17 (the animation 0xD697A799 or AnimName, the weight
    // 0xC6AE16AC), 0xC68365EB, stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);

    void* frame_source_; // +0x14
    void* blend_source_; // +0x18
    uint32_t anim_set_;  // +0x1C
    float weight_;       // +0x20
};
static_assert(sizeof(WeightedAnimController) == 0x24, "layout must match the original");

// The wobble controller (type "wobble", vtable 0x00649F80, 0x38 bytes): plays the timeline's animation around a
// target time, swinging by two sines of the game clock (SetWobbleDetails).
class WobbleDriver : public AnimDriver {
public:
    // 0x004A49F0
    WobbleDriver* make_new(AnimTimeline* timeline);
    // 0x004A4A80
    void destroy_body();
    // 0x004A50E0 (slot 0)
    WobbleDriver* teardown(uint8_t flags);
    // 0x004A5110 (slot 1)
    bool fetch_pose(uint8_t* pose);
    // 0x004A4AF0 (slot 3): PlayAnim, 0xD0209498 (the target as a fraction 0xD747FA0 of the timeline, 0..1),
    // SetWobbleDetails (all five or nothing), stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x004A52E0 (slot 5)
    void tick_frame();
    // 0x004A5180: the time to show now (ST0).
    float wobble_time();

    float amp_a_;        // +0x14 wobbleAmpA
    float amp_b_;        // +0x18 wobbleAmpB
    float k1_;           // +0x1C wobbleK1 (cycles per ms of the first sine)
    float k2_;           // +0x20 wobbleK2
    float spaz_factor_;  // +0x24 spazFactor
    float aim_point_;    // +0x28 the time it wobbles around
    void* blend_source_; // +0x2C
    void* frame_source_; // +0x30
    uint32_t anim_set_;  // +0x34
};
static_assert(sizeof(WobbleDriver) == 0x38, "layout must match the original");

// The pose controller (type "pose", vtable 0x00649F98, 0x854 bytes): a fixed pose (identity rotations).
class StanceDriver : public AnimDriver {
public:
    // 0x004A5730
    StanceDriver* make_new(AnimTimeline* timeline);
    // 0x004A4D20
    void destroy_body();
    // 0x004A5320 (slot 0)
    StanceDriver* teardown(uint8_t flags);
    // 0x004A4D30 (slot 1): its pose copied out.
    bool fetch_pose(uint8_t* pose);

    uint8_t stance_[0x840]; // +0x14 (64 rotations, then the rest)
};
static_assert(sizeof(StanceDriver) == 0x854, "layout must match the original");

// The partial animation controller (type "partialAnim", vtable 0x00649FB0, 0x24 bytes): a second animation on a
// timeline of its own.
class PartialClipDriver : public AnimDriver {
public:
    // 0x004A5350
    PartialClipDriver* make_new(AnimTimeline* timeline);
    // 0x004A4D50
    void destroy_body();
    // 0x004A5400 (slot 0)
    PartialClipDriver* teardown(uint8_t flags);
    // 0x004A5430 (slot 1)
    bool fetch_pose(uint8_t* pose);
    // 0x004A54A0 (slot 3): 0x6AAEB76F (a step of 0x06C9F278 frames), 0xBD4EDD44 (Speed), GetPartialAnimParams,
    // 0xF5E2B871 (reverses), stale_cache.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x004A4DD0 (slot 4): the base's, then AnimName's times, loop type, Speed and Current on its timeline.
    void configure_from_data(script::Struct* params);
    // 0x004A4EF0 (slot 5)
    void tick_frame();

    AnimTimeline* partial_; // +0x14 its own timeline
    void* blend_source_;    // +0x18
    void* frame_source_;    // +0x1C
    uint32_t anim_set_;     // +0x20
};
static_assert(sizeof(PartialClipDriver) == 0x24, "layout must match the original");

} // namespace gfx
