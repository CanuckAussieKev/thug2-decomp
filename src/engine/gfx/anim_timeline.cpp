// The animation timeline (gfx::AnimTimeline, anim_controller.h), the blend channel's timeline (blend_timeline.h),
// the base animation controller's members and the base animation event's destructor body
// (0x00481380-0x004819E6, but 0x004817D0). From the disassembly.

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "engine/gfx/anim_track.h"
#include "engine/gfx/custom_anim_key.h"

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_controller.h"
#include "engine/objects/entity.h"
#include "engine/objects/skeleton_component.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/rider/vehicle_sound.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace gfx {

namespace {

constexpr uint32_t kNoAnim = 0xF60C9090;         // Unassigned
constexpr uint32_t kId = 0x40C698AF;             // id
constexpr uint32_t kSkeleton = 0x222756D5;       // skeleton
constexpr float kFramesPerSecond = 60.0f;        // 0x00645EA0
constexpr float kSecondsPerFrame = 1.0f / 60.0f; // 0x006463B8

// The timeline's empty state (Reset and the constructor).
void get_clear(AnimTimeline* t)
{
    t->set_outset(0.0f);
    t->set_ongoing(0.0f);
    t->set_finish(0.0f);
    t->set_speed(0.0f);
    t->set_direction(0);
    t->set_repeat_mode(AnimTimeline::kCycle);
    t->set_complete(false);
    t->set_anim(kNoAnim);
}

} // namespace

// --- the timeline ---

// 0x00481650
AnimTimeline::AnimTimeline()
{
    list_link_.make_node(this);
    get_clear(this);
}

// 0x00481690
AnimTimeline::~AnimTimeline() { list_link_.uncouple(); }

// 0x004816E0
AnimTimeline* AnimTimeline::teardown(uint8_t flags)
{
    this->~AnimTimeline();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x00481380
void AnimTimeline::get_reset() { get_clear(this); }

// 0x00481640
void AnimTimeline::tick_frame() { ongoing_ = static_cast<float>(next_time()); }

// 0x00481430 (start: the smaller of the two - `to` when they're equal or NaN; end: the other)
void AnimTimeline::play_anim(uint32_t anim, float from, float to, int loop_type, float /*blendPeriod*/, float speed)
{
    const bool forwards = from < to;
    outset_ = forwards ? from : to;
    ongoing_ = from;
    finish_ = forwards ? to : from;
    speed_ = speed * kSecondsPerFrame;
    direction_ = forwards ? 0 : -1;
    repeat_mode_ = loop_type;
    complete_ = false;
    anim_ = anim;
}

// 0x004813B0
void AnimTimeline::get_times(float* start, float* current, float* end) const
{
    *current = ongoing_;
    if (direction_ == 0) {
        *start = outset_;
        *end = finish_;
    } else {
        *start = finish_;
        *end = outset_;
    }
}

// 0x004813F0
bool AnimTimeline::is_accomplished() const
{
    if (repeat_mode_ != kOnce)
        return false;
    return direction_ == 0 ? ongoing_ == finish_ : ongoing_ == outset_;
}

// 0x004814B0 (x87: the step and the wrapped time unrounded; the stepped time itself kept as a float)
double AnimTimeline::next_time()
{
    float stepped = 0.0f; // the time a frame on
    float bound = 0.0f;   // the end it reaches
    double time = 0.0;
    complete_ = false;
    if (repeat_mode_ == kHold)
        return 0.0;
    if (direction_ == 0) {
        const double next = frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * speed_ + ongoing_;
        stepped = static_cast<float>(next);
        complete_ = next > finish_;
        if (outset_ == finish_) {
            bound = finish_;
            time = finish_;
        } else {
            // (wrapped round the end, as many times as it takes - forever on a degenerate range, as the original)
            time = static_cast<double>(stepped) - finish_ + outset_;
            while (time > finish_)
                time = time - finish_ + outset_;
            bound = finish_;
        }
    } else if (direction_ == -1) {
        const double next = ongoing_ - frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * speed_;
        stepped = static_cast<float>(next);
        complete_ = next < outset_;
        if (outset_ == finish_) {
            time = finish_;
        } else {
            time = static_cast<double>(stepped) - outset_ + finish_;
            while (time < outset_)
                time = time - outset_ + finish_;
        }
        bound = outset_;
    }
    if (complete_ && repeat_mode_ == kOnce) {
        time = bound;
    } else if (complete_ && repeat_mode_ == kPingPong) {
        time = bound;
        direction_ = direction_ != 0 ? 0 : -1;
    } else if (!complete_ || repeat_mode_ != kCycle) {
        time = stepped;
    }
    time = std::max<double>(time, outset_);
    time = std::min<double>(time, finish_);
    return time;
}

// 0x004816B0
void AnimTimeline::advance_clock(float frames)
{
    const float speed = speed_;
    speed_ = 0.0f;
    ongoing_ = static_cast<float>(static_cast<double>(frames) + ongoing_);
    ongoing_ = static_cast<float>(next_time());
    speed_ = speed;
}

// --- the blend timeline ---

// 0x00481830
// (the timeline's constructor first, as its base)
AnimTrack::AnimTrack(objects::GameEntity* object) : owner_object_(object) {}

// 0x00481960
void AnimTrack::reset_track()
{
    AnimTimeline::get_reset();
    cur_phase_ = AnimTrack::State::kOff;
    fade_left_ = 0.0f;
    fade_scale_ = 0.0f;
}

// 0x00481870
void AnimTrack::delete_controllers()
{
    for (int i = 0; i < controller_count_; ++i) {
        gsl::owner<AnimDriver*> controller = memory::owned_element(core::at(controller_list_, i)); // (the track's)
        delete controller;
        core::at(controller_list_, i) = nullptr;
    }
    controller_count_ = 0;
}

// 0x004818B0
void AnimTrack::detach_anim_driver(AnimDriver* controller)
{
    int i = 0;
    for (;; ++i) {
        if (i >= controller_count_)
            return;
        if (core::at(controller_list_, i) == controller)
            break;
    }
    gsl::owner<AnimDriver*> owned = memory::owned_element(core::at(controller_list_, i)); // (the track's)
    delete owned;
    core::at(controller_list_, i) = nullptr;
    for (; i < controller_count_ - 1; ++i)
        core::at(controller_list_, i) = core::at(controller_list_, i + 1);
    --controller_count_;
}

// 0x00481920
bool AnimTrack::get_poses(Stance* pose)
{
    for (int i = 0; i < controller_count_; ++i) {
        AnimDriver* controller = core::at(controller_list_, i);
        controller->fetch_pose(pose);
    }
    return true;
}

// 0x00481980
bool AnimTrack::is_operative() { return cur_phase_ != AnimTrack::State::kOff; }

// 0x00481990
bool AnimTrack::is_fading() { return cur_phase_ == AnimTrack::State::kFading; }

// 0x004819A0
void AnimTrack::arm_events()
{
    for (TimedClipEvent& event : events_)
        event.change_live(true);
}

// 0x004819D0
objects::GameEntity* AnimTrack::fetch_subject() { return owner_object_; }

// --- the base controller ---

// 0x00481710
AnimDriver::AnimDriver(AnimTimeline* timeline) : timeline_ptr_(timeline) {}

void* AnimDriver::operator new(std::size_t size) noexcept
{
    return memory::alloc_zeroed(static_cast<unsigned int>(size));
}
void AnimDriver::operator delete(void* memory) { memory::dealloc(memory); }

// 0x0051ADC0 (the base's slot 3, shared with components without member functions): not its own.
int AnimDriver::invoke_method_by_name(uint32_t /*unused*/, script::Struct* /*params*/, script::Script* /*script*/)
{
    return 2;
}

// 0x00481740
void AnimDriver::configure_from_data(script::Struct* params) { params->read_hash_value(kId, &ident_, 0); }

// 0x00481760
objects::GameEntity* AnimDriver::fetch_subject() { return core::down_cast<AnimTrack>(timeline_ptr_)->fetch_subject(); }

// 0x00481770
Rig* AnimDriver::fetch_rig()
{
    objects::ObjectModule* component = core::down_cast<AnimTrack>(timeline_ptr_)->fetch_subject()->fetch_module(kSkeleton);
    return component ? core::down_cast<objects::BoneRigModule>(component)->bone_rig() : nullptr;
}

} // namespace gfx
