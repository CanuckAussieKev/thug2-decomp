// An animation playing on a blend channel - its controllers, events and fade: see anim_track.h.

#include "engine/gfx/anim_track.h"
#include "engine/gfx/bone_look_at.h"
#include "engine/gfx/custom_anim_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "engine/core/game_vtables.h"
#include "engine/core/stubs.h"

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
enum : int { kNotMine = 2 };

void** const& g_event_vtable = vt::kVtblAnimCue;
void** const& g_event_base_vtable = vt::kAnimEventBase;
void** const& g_track_vtable = vt::kAnimTrack;

template <typename R, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// --- calls into code not rewritten here ---
// An event's set-up, arming, firing and destructor body.
void set_up_event(TimedClipEvent* e, int frame, uint32_t type, Struct* params)
{
    reinterpret_cast<AnimScriptKey*>(e)->assign_value(frame, type, params);
}
void arm_event(TimedClipEvent* e, bool armed) { reinterpret_cast<UserClipKey*>(e)->change_live(armed); }
bool fire_event(TimedClipEvent* e, objects::GameEntity* object)
{
    return reinterpret_cast<UserClipKey*>(e)->process_if_active(object);
}
void destroy_event_body(TimedClipEvent* e) { reinterpret_cast<AnimScriptKey*>(e)->destroy_body(); }
// The 0x26205DB6 controller (0x1A4 bytes).
AnimDriver* construct_procedural_controller(void* memory, AnimTimeline* timeline)
{
    return reinterpret_cast<AnimDriver*>(static_cast<BoneLookAt*>(memory)->make_new(timeline, nullptr));
}

// A script structure from the structures' pools, and back.
Struct* new_params()
{
    Struct* params = script::spawn_param_bag();
    return params ? params->prepare_all() : nullptr;
}
void delete_params(Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// --- the events ---------------------------------------------------------------------------------------------

// 0x004819F0
AnimEventBase* AnimEventBase::teardown(uint8_t flags)
{
    vtable_ = g_event_base_vtable;
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00481A20
TimedClipEvent* TimedClipEvent::make_new()
{
    vtable_ = g_event_vtable;
    call_args_ = nullptr;
    return this;
}

// 0x00481DB0
TimedClipEvent* TimedClipEvent::teardown(uint8_t flags)
{
    if (flags & 2) {
        // (the compiler's array-destructor helper, 0x0062382B: last to first)
        uint32_t* block = reinterpret_cast<uint32_t*>(this) - 1;
        for (uint32_t i = *block; i > 0; --i)
            destroy_event_body(this + (i - 1));
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(block);
        return reinterpret_cast<TimedClipEvent*>(block);
    }
    destroy_event_body(this);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// --- the track ----------------------------------------------------------------------------------------------

memory::PoolPair g_anim_track_pools;

memory::PoolPair& AnimTrack::get_pools() { return g_anim_track_pools; }

// 0x00481EE0
void* AnimTrack::operator new(size_t size) { return get_pools().carve(static_cast<unsigned int>(size)); }

// 0x00482360
void AnimTrack::operator delete(void* item) { get_pools().dealloc(item); }

// 0x00481F20
void AnimTrack::destroy_body()
{
    vtable_ = g_track_vtable;
    delete_controllers();
    delete_events();
    AnimTimeline::destroy_body();
}

// 0x00482340
AnimTrack* AnimTrack::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        operator delete(this);
    return this;
}

// 0x00481A30
void AnimTrack::insert_controller(AnimDriver* controller, int priority)
{
    // (no room check: six at most by the callers)
    int count = controller_count_;
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
        double left = static_cast<double>(fade_left_) - step;
        fade_left_ = static_cast<float>(left);
        if (left <= 0.0 || left * fade_scale_ < 0.0)
            cur_phase_ = kOff;
    }
    if (cur_phase_ == kPlaying || cur_phase_ == kFading) {
        for (int i = 0; i < controller_count_; ++i)
            Virtual<void>(controller_list_[i], 5);
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
    uint32_t* block = reinterpret_cast<uint32_t*>(events_ptr_) - 1;
    if (*block)
        Virtual<TimedClipEvent*>(events_ptr_, 0, 3); // (the vector deleting destructor)
    else
        memory::Hub::get_pull()->dealloc(block);
    events_ptr_ = nullptr;
}

// 0x00481C00
void AnimTrack::load_events(uint32_t anim)
{
    objects::ObjectModule* component = owner_object_->fetch_module(kAnimBlender);
    if (!component)
        return;
    uint32_t table_name = static_cast<objects::AnimBlenderComponent*>(component)->get_first_channel58();
    if (!table_name)
        return;
    Struct* table = script::read_sub_struct(table_name);
    if (!table)
        return;
    script::Array* list;
    if (!table->fetch_list(anim, &list, 0))
        return;
    uint32_t count = list->extent_;
    event_count_ = count;
    if (!count)
        return;
    auto* block = static_cast<uint32_t*>(memory::alloc(count * sizeof(TimedClipEvent) + 4));
    TimedClipEvent* events = nullptr;
    if (block) {
        *block = count;
        events = reinterpret_cast<TimedClipEvent*>(block + 1);
        // (the compiler's array-constructor helper, 0x0062388B)
        for (uint32_t i = 0; i < count; ++i)
            events[i].make_new();
    }
    events_ptr_ = events;
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
    double first = static_cast<double>(from) * 60.0;
    double last = static_cast<double>(to) * 60.0;
    float first_frame = static_cast<float>(first);
    float last_frame = static_cast<float>(last);
    bool looped = direction_ == 0 ? first > last : last > first;
    if (looped) {
        arm_events();
        first_frame = outset_; // (as the original: the start itself, not in frames)
    }
    for (uint32_t i = 0; i < event_count_; ++i) {
        arm_event(&events_ptr_[i], true);
        if (Virtual<bool>(&events_ptr_[i], 1, first_frame, last_frame, 0))
            fire_event(&events_ptr_[i], owner_object_);
    }
}

// 0x00481FB0
AnimDriver* AnimTrack::attach_controller(Struct* params)
{
    uint32_t type;
    params->read_hash_value(kType, &type, 1);
    int priority = 500;
    AnimDriver* controller;
    void* memory;
    switch (type) {
    case kWeightedController:
        memory = memory::alloc_zeroed(sizeof(WeightedAnimController));
        controller = memory ? static_cast<WeightedAnimController*>(memory)->make_new(this, nullptr) : nullptr;
        break;
    case kProceduralController:
        memory = memory::alloc_zeroed(0x1A4);
        controller = memory ? construct_procedural_controller(memory, this) : nullptr;
        break;
    case kPartialController:
        memory = memory::alloc_zeroed(sizeof(PartialClipDriver));
        controller = memory ? static_cast<PartialClipDriver*>(memory)->make_new(this) : nullptr;
        break;
    case kShakeController:
        memory = memory::alloc_zeroed(sizeof(WobbleDriver));
        controller = memory ? static_cast<WobbleDriver*>(memory)->make_new(this) : nullptr;
        break;
    case kStandardController:
        memory = StandardAnimController::operator new(sizeof(StandardAnimController), 0);
        controller = memory ? static_cast<StandardAnimController*>(memory)->make_new(this) : nullptr;
        break;
    case kStaticPoseCtl:
        memory = memory::alloc_zeroed(sizeof(StanceDriver));
        controller = memory ? static_cast<StanceDriver*>(memory)->make_new(this) : nullptr;
        break;
    case kTumbleController:
        memory = memory::alloc_zeroed(sizeof(objects::FlipRotateComponent));
        controller =
            memory ? reinterpret_cast<AnimDriver*>(static_cast<objects::FlipRotateComponent*>(memory)->make_new(this))
                   : nullptr;
        priority = 100;
        break;
    default:
        return nullptr;
    }
    params->fetch_int_value(kPriority, &priority, 0);
    // (a failed allocation goes on with null: the call below crashes, as in the original)
    Virtual<void>(controller, 4, params);
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
        uint32_t id = reinterpret_cast<uint32_t>(params); // (the argument's own slot: the params' address if missing)
        params->read_hash_value(kId, &id, 1);
        if (AnimDriver* controller = find_controller(id))
            detach_anim_driver(controller);
    } else if (name == kNewAnim) {
        uint32_t anim = 0; // (the original's is left as it was found on the stack when missing - an assert)
        params->read_hash_value(kAnim, &anim, 1);
        float duration;
        std::memcpy(&duration, &params, sizeof(duration)); // (the argument's own slot, as above)
        params->fetch_float(kDuration, &duration, 1);
        bool sync = params->has_bare_flag(kSync);
        anim_ = anim;
        delete_events();
        load_events(anim_);
        float current = 0.0f;
        if (sync) {
            // (x87 at 53 bits: the place on the old timeline in proportion, rounded once)
            double place = static_cast<double>(ongoing_);
            ongoing_ = 0.0f;
            current = static_cast<float>(place / finish_ * duration);
        }
        finish_ = duration;
        outset_ = 0.0f;
        advance_clock(current);
    }
    for (int i = 0; i < controller_count_; ++i) {
        int result = Virtual<int>(controller_list_[i], 3, name, params, script);
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
        Virtual<int>(controller_list_[i], 3, kSetTarget, settings, static_cast<script::Script*>(nullptr));
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
        Virtual<int>(controller_list_[i], 3, kPlayAnim, settings, static_cast<script::Script*>(nullptr));
    delete_params(settings);
}

} // namespace gfx

namespace vt {
static VtableSlot g_anim_track_slots[] = {&gfx::AnimTrack::teardown,   &gfx::AnimTrack::get_reset,
                                          &gfx::AnimTrack::tick_frame, &gfx::AnimTimeline::play_anim,
                                          stubs::ignore_argument,      &gfx::AnimTrack::invoke_method_by_name,
                                          &gfx::AnimTrack::play_anim};
void** const kAnimTrack = reinterpret_cast<void**>(g_anim_track_slots);
static VtableSlot g_anim_event_base_slots[] = {&gfx::AnimEventBase::teardown, &gfx::UserClipKey::inside_range,
                                               &_purecall};
void** const kAnimEventBase = reinterpret_cast<void**>(g_anim_event_base_slots);
static VtableSlot g_anim_event_slots[] = {&gfx::TimedClipEvent::teardown, &gfx::UserClipKey::inside_range,
                                          &gfx::AnimScriptKey::work_on};
void** const kVtblAnimCue = reinterpret_cast<void**>(g_anim_event_slots);
} // namespace vt
