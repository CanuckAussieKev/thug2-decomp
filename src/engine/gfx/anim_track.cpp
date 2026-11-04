// An animation playing on a blend channel - its controllers, events and fade: see anim_track.h.

#include "engine/gfx/anim_track.h"
#include "engine/gfx/bone_look_at.h"
#include "engine/gfx/custom_anim_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "engine/core/slab_bank.h"
#include "engine/gfx/anim_controller.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/flip_rotate.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/script/script_startup.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <gsl/pointers>

namespace gfx {

constexpr double kFramesPerSecond = 60.0;
constexpr float kFramesPerSecondFloat = 60.0F;
constexpr int kDefaultPriority = 500;
constexpr int kTumblePriority = 100;

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
void set_up_event(TimedClipEvent* e, int frame, uint32_t type, Struct* params) { e->assign_value(frame, checksums::Name{type}, params); }
void arm_event(TimedClipEvent* e, bool armed) { e->change_live(armed); }
bool fire_event(TimedClipEvent* e, objects::GameEntity* object) { return e->process_if_active(object); }

// A script structure from the structures' pools, and back.

} // namespace

// --- the track ----------------------------------------------------------------------------------------------

memory::PoolPair& anim_track_pools()
{
    static core::NoDestroy<memory::PoolPair> anim_track_pools;
    return anim_track_pools.get();
}

memory::PoolPair& AnimTrack::get_pools() { return anim_track_pools(); }

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
AnimTrack* AnimTrack::teardown_track(uint8_t flags)
{
    this->~AnimTrack();
    if ((flags & 1) != 0) {
        operator delete(this);
    }
    return this;
}

// 0x00481A30
void AnimTrack::insert_controller(AnimDriver* controller, int priority)
{
    // (no room check: six at most by the callers)
    const int count = controller_count_;
    int at = 0;
    if (count > 0) {
        while (at < controller_count_ && priority <= core::at(controller_list_, at)->rank()) {
            ++at;
        }
        if (at < controller_count_) {
            for (int i = count; i > at; --i) {
                core::at(controller_list_, i) = core::at(controller_list_, i - 1);
            }
        }
    }
    core::at(controller_list_, at) = controller;
    ++controller_count_;
}

// 0x00481A80
AnimDriver* AnimTrack::find_controller(uint32_t id)
{
    for (int i = 0; i < controller_count_; ++i) {
        if (core::at(controller_list_, i)->ident() == id) {
            return core::at(controller_list_, i);
        }
    }
    return nullptr;
}

// 0x00481AC0
void AnimTrack::tick_track()
{
    set_complete(false);
    if (cur_phase_ == AnimTrack::State::kFading) {
        // (x87 at 53 bits: the frame's share of the rate, taken off the weight left; the comparisons use the
        // unrounded result)
        double step = frame_clock::get_slomo_frame_seconds() * kFramesPerSecond;
        step *= fade_rate_;
        const double left = static_cast<double>(fade_left_) - step;
        fade_left_ = static_cast<float>(left);
        if (left <= 0.0 || left * fade_scale_ < 0.0) {
            cur_phase_ = AnimTrack::State::kOff;
        }
    }
    if (cur_phase_ == AnimTrack::State::kPlaying || cur_phase_ == AnimTrack::State::kFading) {
        for (int i = 0; i < controller_count_; ++i) {
            core::at(controller_list_, i)->tick_frame();
        }
    }
}

// 0x00481B40
bool AnimTrack::start_fade(float period)
{
    if (period == 0.0F) {
        cur_phase_ = AnimTrack::State::kOff;
    }
    if (cur_phase_ != AnimTrack::State::kPlaying) {
        return false;
    }
    cur_phase_ = AnimTrack::State::kFading;
    fade_left_ = period;
    return true;
}

// 0x00481B80
double AnimTrack::get_weight()
{
    switch (cur_phase_) {
    case AnimTrack::State::kFading:
        return static_cast<double>(fade_scale_) * fade_left_;
    case AnimTrack::State::kPlaying:
        return 1.0;
    case AnimTrack::State::kOff:
        break;
    }
    return 0.0;
}

// 0x00481BB0
void AnimTrack::delete_events()
{
    events_.clear(); // (0x00481DB0, the vector deleting destructor)
}

// 0x00481C00
void AnimTrack::load_events(uint32_t anim)
{
    objects::ObjectModule* component = owner_object_->fetch_module(kAnimBlender);
    if (!component) {
        return;
    }
    const uint32_t table_name = core::down_cast<objects::AnimBlenderComponent>(component)->get_first_channel58();
    if (table_name == 0U) {
        return;
    }
    Struct* table = script::read_sub_struct(table_name);
    if (!table) {
        return;
    }
    script::Array* list = nullptr;
    if (!table->fetch_list(anim, &list, 0)) {
        return;
    }
    const uint32_t count = list->extent();
    events_ = std::vector<TimedClipEvent>(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* entry = list->struct_at(i);
        int frame = 0;
        if (!entry->fetch_int_value(kFrame, &frame, 0)) {
            float seconds = 0.0F;
            entry->fetch_float(kTime, &seconds, 1);
            frame = get_truncated(static_cast<double>(seconds) * kFramesPerSecond);
        }
        uint32_t type = 0; // (the original's is left as it was found on the stack when missing - an assert)
        entry->read_hash_value(kEvent, &type, 1);
        Struct* params = nullptr;
        entry->read_sub_struct(kParams, &params, 0);
        set_up_event(&core::at(events_, i), frame, type, params);
        arm_event(&core::at(events_, i), true);
    }
}

// 0x00481E10
void AnimTrack::fire_events(float from, float to)
{
    if (!events_on_ || cur_phase_ != AnimTrack::State::kPlaying) {
        return;
    }
    // (frames: compared unrounded, passed on as floats)
    const double first = static_cast<double>(from) * 60.0;
    const double last = static_cast<double>(to) * 60.0;
    auto first_frame = static_cast<float>(first);
    auto last_frame = static_cast<float>(last);
    const bool looped = direction() == 0 ? first > last : last > first;
    if (looped) {
        arm_events();
        first_frame = outset(); // (as the original: the start itself, not in frames)
    }
    for (TimedClipEvent& event : events_) {
        arm_event(&event, true);
        if (event.inside_range(first_frame, last_frame, false)) {
            fire_event(&event, owner_object_);
        }
    }
}

// 0x00481FB0
AnimDriver* AnimTrack::attach_controller(Struct* params)
{
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 1);
    int priority = kDefaultPriority;
    gsl::owner<AnimDriver*> controller = nullptr;
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
        priority = kTumblePriority;
        break;
    default:
        return nullptr;
    }
    params->fetch_int_value(kPriority, &priority, 0);
    // (a failed allocation goes on with null: the call below crashes, as in the original)
    controller->configure_from_data(params);
    controller->set_rank(priority);
    insert_controller(controller, priority);
    AnimDriver* const inserted = controller; // (the track's now)
    return inserted;
}

// 0x00482200
int AnimTrack::invoke_method_by_name(uint32_t name, Struct* params, script::Script* script)
{
    if (name == kAddAnimController) {
        attach_controller(params);
        return 1;
    }
    if (name == kRemoveAnimController) {
        uint32_t id = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
        params->read_hash_value(kId, &id, 1);
        if (AnimDriver* controller = find_controller(id)) {
            detach_anim_driver(controller);
        }
    } else if (name == kNewAnim) {
        uint32_t anim = 0; // (the original's is left as it was found on the stack when missing - an assert)
        params->read_hash_value(kAnim, &anim, 1);
        float duration = 0.0F;
        std::memcpy(&duration, static_cast<const void*>(&params),
                    sizeof(duration)); // (the argument's own slot, as above)
        params->fetch_float(kDuration, &duration, 1);
        const bool sync = params->has_bare_flag(kSync);
        set_anim(anim);
        delete_events();
        load_events(this->anim());
        float current = 0.0F;
        if (sync) {
            // (x87 at 53 bits: the place on the old timeline in proportion, rounded once)
            auto place = static_cast<double>(ongoing());
            set_ongoing(0.0F);
            current = static_cast<float>(place / finish() * duration);
        }
        set_finish(duration);
        set_outset(0.0F);
        advance_clock(current);
    }
    for (int i = 0; i < controller_count_; ++i) {
        const int result = core::at(controller_list_, i)->invoke_method_by_name(name, params, script);
        if (result != kNotMine) {
            return result;
        }
    }
    return kNotMine;
}

// 0x004823C0
void AnimTrack::play_anim(uint32_t anim, AnimPlay anim_play,
                          bool flipped, Struct* params)
{
    const auto start = anim_play.start;
    const auto end = anim_play.end;
    const auto loop_type = anim_play.loop_type_;
    const auto blend_period = anim_play.blend_period_;
    const auto speed = anim_play.speed;
    delete_events();
    load_events(anim);
    cur_phase_ = AnimTrack::State::kPlaying;
    fade_rate_ = 1.0F / kFramesPerSecondFloat; // (0x3C888889)

    gsl::owner<Struct*> settings = script::new_struct();
    settings->put_name_key(checksums::Name{kType}, loop_type == kLoopWobble ? kShakeController : kStandardController);
    attach_controller(settings);
    script::free_struct(settings);

    uint32_t weighted_anim = anim;
    float weight = 0.0F;
    if (params) {
        if (params->read_hash_value(kWeightedAnim, &weighted_anim, 0) || params->has_bare_flag(kWeightedFlag)) {
            params->fetch_float(kWeight, &weight, 0);
            settings = script::new_struct();
            settings->put_name_key(checksums::Name{kType}, kWeightedController);
            attach_controller(settings);
            script::free_struct(settings);
        }
        Struct* controller_params = nullptr;
        if (params->read_sub_struct(kControllerParams, &controller_params, 0)) {
            settings = script::new_struct();
            settings->put_name_key(checksums::Name{kType}, kProceduralController);
            settings->put_sub_struct(0U, controller_params); // (no name)
            attach_controller(settings);
            script::free_struct(settings);
        }
    }
    settings = script::new_struct();
    settings->put_name_key(checksums::Name{kType}, kTumbleController);
    settings->put_int(checksums::Name{kFlipped}, static_cast<int>(flipped));
    attach_controller(settings);
    script::free_struct(settings);

    settings = script::new_struct();
    settings->push_float(checksums::Name{kFraction}, 0.0F);
    for (int i = 0; i < controller_count_; ++i) {
        core::at(controller_list_, i)->invoke_method_by_name(kSetTarget, settings, nullptr);
    }
    script::free_struct(settings);

    settings = script::new_struct();
    settings->put_name_key(checksums::Name{kAnimName}, anim);
    settings->push_float(checksums::Name{kStartTime}, start);
    settings->push_float(checksums::Name{kEndTime}, end);
    settings->put_int(checksums::Name{kRepeatMode}, loop_type);
    settings->push_float(checksums::Name{kBlendPeriod}, blend_period);
    settings->push_float(checksums::Name{kSpeed}, speed);
    settings->put_name_key(checksums::Name{kWeightedAnim}, weighted_anim);
    settings->push_float(checksums::Name{kWeight}, weight);
    for (int i = 0; i < controller_count_; ++i) {
        core::at(controller_list_, i)->invoke_method_by_name(kPlayAnim, settings, nullptr);
    }
    script::free_struct(settings);
}

} // namespace gfx
