// An animation playing on a blend channel - its controllers, events and fade: see anim_track.h.

#include "engine/gfx/anim_track.h"
#include "engine/gfx/bone_look_at.h"
#include "engine/gfx/custom_anim_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/flip_rotate.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/script/script_startup.h"

namespace gfx {

namespace {

using script::Struct;

// Member function and parameter names (checksums; those not recovered are only numbers).
constexpr uint32_t kAddAnimController = 0x83654874, kRemoveAnimController = 0x986D274E, kNewAnim = 0xC68365EB;
constexpr uint32_t kPlayAnim = 0xAF2FAE19, kSetTarget = 0xD0209498;
constexpr uint32_t kType = 0x7321A8D6, kPriority = 0x9D5923D8, kId = 0x40C698AF, kAnim = 0x98549BA4;
constexpr uint32_t kDuration = 0x79A07F3F, kSync = 0xD3D40A85, kFrame = 0x4A07C332, kTime = 0x906B67BA;
constexpr uint32_t kEvent = 0xC451F558, kParams = 0x7031F10C, kFlipped = 0x0C7A712C;
constexpr uint32_t kAnimName = 0x6C2BFB7F, kStartTime = 0xD16B61E6, kEndTime = 0xAB81BB3D, kRepeatMode = 0xCD8D7C1B;
constexpr uint32_t kBlendPeriod = 0x8F0D24ED, kSpeed = 0xF0D90109, kWeightedAnim = 0xD697A799;
constexpr uint32_t kWeight = 0xC6AE16AC, kFraction = 0x4D747FA0, kWeightedFlag = 0x1B92121C;
constexpr uint32_t kControllerParams = 0xAEEEC12E;
// Controller types.
constexpr uint32_t kWeightedController = 0x7E1940A1, kProceduralController = 0x26205DB6;
constexpr uint32_t kPartialController = 0x659BF355, kShakeController = 0x6D941203;
constexpr uint32_t kStandardController = 0xCB4533FF, kStaticPoseCtl = 0xCFC5B380;
constexpr uint32_t kTumbleController = 0xDF5C091A;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr int kLoopWobble = 3;
constexpr int kNotMine = 2;

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// --- calls into code not rewritten here ---
// An event's set-up, arming and firing.
void set_up_event(TimedClipEvent* e, int frame, uint32_t type, Struct* params) { e->assign_value(frame, type, params); }
void arm_event(TimedClipEvent* e, bool armed) { e->change_live(armed); }
bool fire_event(TimedClipEvent* e, objects::GameEntity* object) { return e->process_if_active(object); }

// A script structure from the structures' pools, and back.
Struct* new_params()
{
    Struct* params = script::spawn_param_bag();
    return params ? new (params) script::Struct : nullptr;
}
void delete_params(Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// --- the track ----------------------------------------------------------------------------------------------

memory::PoolPair g_anim_track_pools;

memory::PoolPair& AnimTrack::get_pools() { return g_anim_track_pools; }

// 0x00481EE0
void* AnimTrack::operator new(size_t size) { return get_pools().carve(static_cast<unsigned int>(size)); }

// 0x00482360
void AnimTrack::operator delete(void* item) { get_pools().dealloc(item); }

// 0x00481F20
AnimTrack::~AnimTrack()
{
    delete_controllers();
    delete_events();
    // (then the timeline's destructor, as its base)
}

// 0x00482340
AnimTrack* AnimTrack::teardown(uint8_t flags)
{
    this->~AnimTrack();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x00481A30
void AnimTrack::insert_controller(AnimDriver* controller, int priority)
{
    // (no room check: six at most by the callers)
    const int count = controller_count_;
    int at = 0;
    if (count > 0) {
        while (at < controller_count_ && priority <= controller_list_[at]->rank_)
            ++at;
        if (at < controller_count_) {
            for (int i = count; i > at; --i)
                controller_list_[i] = controller_list_[i - 1];
        }
    }
    controller_list_[at] = controller;
    ++controller_count_;
}

// 0x00481A80
AnimDriver* AnimTrack::find_controller(uint32_t id)
{
    for (int i = 0; i < controller_count_; ++i) {
        if (controller_list_[i]->ident_ == id)
            return controller_list_[i];
    }
    return nullptr;
}

// 0x00481AC0
void AnimTrack::tick_frame()
{
    complete_ = false;
    if (cur_phase_ == kFading) {
        // (x87 at 53 bits: the frame's share of the rate, taken off the weight left; the comparisons use the
        // unrounded result)
        double step = frame_clock::get_slomo_frame_seconds() * 60.0;
        step *= fade_rate_;
        const double left = static_cast<double>(fade_left_) - step;
        fade_left_ = static_cast<float>(left);
        if (left <= 0.0 || left * fade_scale_ < 0.0)
            cur_phase_ = kOff;
    }
    if (cur_phase_ == kPlaying || cur_phase_ == kFading) {
        for (int i = 0; i < controller_count_; ++i)
            controller_list_[i]->tick_frame();
    }
}

// 0x00481B40
bool AnimTrack::start_fade(float period)
{
    if (period == 0.0f)
        cur_phase_ = kOff;
    if (cur_phase_ != kPlaying)
        return false;
    cur_phase_ = kFading;
    fade_left_ = period;
    return true;
}

// 0x00481B80
double AnimTrack::get_weight()
{
    switch (cur_phase_) {
    case kFading:
        return static_cast<double>(fade_scale_) * fade_left_;
    case kPlaying:
        return 1.0;
    default:
        return 0.0;
    }
}

// 0x00481BB0
void AnimTrack::delete_events()
{
    if (!event_count_ || !events_ptr_)
        return;
    event_count_ = 0;
    events_ptr_.reset(); // (0x00481DB0, the vector deleting destructor: last to first, then freed)
}

// 0x00481C00
void AnimTrack::load_events(uint32_t anim)
{
    objects::ObjectModule* component = owner_object_->fetch_module(kAnimBlender);
    if (!component)
        return;
    const uint32_t table_name = static_cast<objects::AnimBlenderComponent*>(component)->get_first_channel58();
    if (!table_name)
        return;
    Struct* table = script::read_sub_struct(table_name);
    if (!table)
        return;
    script::Array* list = nullptr;
    if (!table->fetch_list(anim, &list, 0))
        return;
    const uint32_t count = list->extent_;
    event_count_ = count;
    if (!count)
        return;
    events_ptr_ = std::make_unique<TimedClipEvent[]>(
        count); // (default-initialised; the count before the array, as the original's)
    for (uint32_t i = 0; i < event_count_; ++i) {
        auto* entry = reinterpret_cast<Struct*>(list->fetch_part(i));
        int frame = 0;
        if (!entry->fetch_int_value(kFrame, &frame, 0)) {
            float seconds = 0.0f;
            entry->fetch_float(kTime, &seconds, 1);
            frame = get_truncated(static_cast<double>(seconds) * 60.0);
        }
        uint32_t type = 0; // (the original's is left as it was found on the stack when missing - an assert)
        entry->read_hash_value(kEvent, &type, 1);
        Struct* params = nullptr;
        entry->read_sub_struct(kParams, &params, 0);
        set_up_event(&events_ptr_[i], frame, type, params);
        arm_event(&events_ptr_[i], true);
    }
}

// 0x00481E10
void AnimTrack::fire_events(float from, float to)
{
    if (!events_on_ || cur_phase_ != kPlaying)
        return;
    // (frames: compared unrounded, passed on as floats)
    const double first = static_cast<double>(from) * 60.0;
    const double last = static_cast<double>(to) * 60.0;
    auto first_frame = static_cast<float>(first);
    auto last_frame = static_cast<float>(last);
    const bool looped = direction_ == 0 ? first > last : last > first;
    if (looped) {
        arm_events();
        first_frame = outset_; // (as the original: the start itself, not in frames)
    }
    for (uint32_t i = 0; i < event_count_; ++i) {
        arm_event(&events_ptr_[i], true);
        if (events_ptr_[i].inside_range(first_frame, last_frame, false))
            fire_event(&events_ptr_[i], owner_object_);
    }
}

// 0x00481FB0
AnimDriver* AnimTrack::attach_controller(Struct* params)
{
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 1);
    int priority = 500;
    AnimDriver* controller = nullptr;
    switch (type) {
    case kWeightedController:
        controller = new WeightedAnimController(this, nullptr);
        break;
    case kProceduralController:
        controller = new BoneLookAt(this, nullptr);
        break;
    case kPartialController:
        controller = new PartialClipDriver(this);
        break;
    case kShakeController:
        controller = new WobbleDriver(this);
        break;
    case kStandardController:
        controller = new (0) StandardAnimController(this);
        break;
    case kStaticPoseCtl:
        controller = new StanceDriver(this);
        break;
    case kTumbleController:
        controller = new objects::FlipRotateComponent(this);
        priority = 100;
        break;
    default:
        return nullptr;
    }
    params->fetch_int_value(kPriority, &priority, 0);
    // (a failed allocation goes on with null: the call below crashes, as in the original)
    controller->configure_from_data(params);
    controller->rank_ = priority;
    insert_controller(controller, priority);
    return controller;
}

// 0x00482200
int AnimTrack::invoke_method_by_name(uint32_t name, Struct* params, script::Script* script)
{
    if (name == kAddAnimController) {
        attach_controller(params);
        return 1;
    }
    if (name == kRemoveAnimController) {
        auto id = reinterpret_cast<uint32_t>(params); // (the argument's own slot: the params' address if missing)
        params->read_hash_value(kId, &id, 1);
        if (AnimDriver* controller = find_controller(id))
            detach_anim_driver(controller);
    } else if (name == kNewAnim) {
        uint32_t anim = 0; // (the original's is left as it was found on the stack when missing - an assert)
        params->read_hash_value(kAnim, &anim, 1);
        float duration = 0.0f;
        std::memcpy(&duration, &params, sizeof(duration)); // (the argument's own slot, as above)
        params->fetch_float(kDuration, &duration, 1);
        const bool sync = params->has_bare_flag(kSync);
        anim_ = anim;
        delete_events();
        load_events(anim_);
        float current = 0.0f;
        if (sync) {
            // (x87 at 53 bits: the place on the old timeline in proportion, rounded once)
            auto place = static_cast<double>(ongoing_);
            ongoing_ = 0.0f;
            current = static_cast<float>(place / finish_ * duration);
        }
        finish_ = duration;
        outset_ = 0.0f;
        advance_clock(current);
    }
    for (int i = 0; i < controller_count_; ++i) {
        const int result = controller_list_[i]->invoke_method_by_name(name, params, script);
        if (result != kNotMine)
            return result;
    }
    return kNotMine;
}

// 0x004823C0
void AnimTrack::play_anim(uint32_t anim, float start, float end, int loop_type, float blend_period, float speed,
                          bool flipped, Struct* params)
{
    delete_events();
    load_events(anim);
    cur_phase_ = kPlaying;
    fade_rate_ = 1.0f / 60.0f; // (0x3C888889)

    Struct* settings = new_params();
    settings->put_name_key(kType, loop_type == kLoopWobble ? kShakeController : kStandardController);
    attach_controller(settings);
    delete_params(settings);

    uint32_t weighted_anim = anim;
    float weight = 0.0f;
    if (params) {
        if (params->read_hash_value(kWeightedAnim, &weighted_anim, 0) || params->has_bare_flag(kWeightedFlag)) {
            params->fetch_float(kWeight, &weight, 0);
            settings = new_params();
            settings->put_name_key(kType, kWeightedController);
            attach_controller(settings);
            delete_params(settings);
        }
        Struct* controller_params = nullptr;
        if (params->read_sub_struct(kControllerParams, &controller_params, 0)) {
            settings = new_params();
            settings->put_name_key(kType, kProceduralController);
            settings->put_sub_struct(0u, controller_params); // (no name)
            attach_controller(settings);
            delete_params(settings);
        }
    }
    settings = new_params();
    settings->put_name_key(kType, kTumbleController);
    settings->put_int(kFlipped, flipped);
    attach_controller(settings);
    delete_params(settings);

    settings = new_params();
    settings->push_float(kFraction, 0.0f);
    for (int i = 0; i < controller_count_; ++i)
        controller_list_[i]->invoke_method_by_name(kSetTarget, settings, nullptr);
    delete_params(settings);

    settings = new_params();
    settings->put_name_key(kAnimName, anim);
    settings->push_float(kStartTime, start);
    settings->push_float(kEndTime, end);
    settings->put_int(kRepeatMode, loop_type);
    settings->push_float(kBlendPeriod, blend_period);
    settings->push_float(kSpeed, speed);
    settings->put_name_key(kWeightedAnim, weighted_anim);
    settings->push_float(kWeight, weight);
    for (int i = 0; i < controller_count_; ++i)
        controller_list_[i]->invoke_method_by_name(kPlayAnim, settings, nullptr);
    delete_params(settings);
}

} // namespace gfx
