// The animation controllers (standard, weighted, wobble, pose, partial animation): see anim_controller.h.

#include "engine/gfx/rig.h"
#include "engine/gfx/anim_controller.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/scene_load.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/names.h"
#include "platform/game_clock.h"
#include "platform/run_log.h"
#include "game/rider/vehicle_sound.h"
#include "frontend/screen_constants.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/skeleton_component.h"

namespace gfx {

memory::PoolPair g_standard_pools; // (CBonedAnimController; script_startup.h)

namespace {

using script::Struct;

// Member function and parameter names (checksums; those not recovered are only numbers).
constexpr uint32_t kPlayAnim = 0xAF2FAE19, kNewSource = 0xC68365EB, kInvalidateCache = 0x5F495AE0;
constexpr uint32_t kGlobalFrame = 0x92267217, kSetWeightedAnim = 0x5E3F0F17;
constexpr uint32_t kSetTarget = 0xD0209498, kSetWobbleDetails = 0xEA6D0EFD;
constexpr uint32_t kStep = 0x6AAEB76F, kSetSpeed = 0xBD4EDD44, kGetPartialAnimParams = 0xD63A1B81;
constexpr uint32_t kReverse = 0xF5E2B871;
constexpr uint32_t kAnimName = 0x6C2BFB7F, kStartTime = 0xD16B61E6, kEndTime = 0xAB81BB3D, kRepeatMode = 0xCD8D7C1B;
constexpr uint32_t kBlendPeriod = 0x8F0D24ED, kSpeed = 0xF0D90109, kAnim = 0x98549BA4, kCurrent = 0x230CCBF4;
constexpr uint32_t kWeightedAnim = 0xD697A799, kWeight = 0xC6AE16AC, kFraction = 0x4D747FA0;
constexpr uint32_t kWobbleAmpA = 0xFD266A26, kSwayAmpSecond = 0x642F3B9C, kWobbleK1 = 0x0F43FD49;
constexpr uint32_t kWobbleK2 = 0x964AACF3, kSpazFactor = 0xF90B0824, kFrames = 0x06C9F278;
constexpr uint32_t kPartialAnimTime = 0xAAECB346, kPartialAnim = 0x659BF355, kPartialAnimComplete = 0x4966CC11;
// Component types.
constexpr uint32_t kAnimBlender = 0x7149B753, kSkeleton = 0x222756D5;

enum : int { kNotMine = 2 };

float frames_per_second() { return frontend::g_frames_per_second; }              // 60
float seconds_per_frame() { return ::objects::g_anim_movies_seconds_per_frame; } // 1/60
float two_pi() { return frontend::g_two_pi; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

double x87_sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// The base controller.
void base_init(AnimDriver* c, Struct* params) { c->AnimDriver::configure_from_data(params); }
// The object (the blend timeline's +0x64) and its skeleton (the Skeleton component's +0x18, or null).
objects::GameEntity* object_of(AnimDriver* c) { return c->AnimDriver::fetch_subject(); }
uint8_t* skeleton_of(AnimDriver* c) { return c->AnimDriver::fetch_rig(); }
// The object's anim set (the animation blender component's 0x00419AB0) and an animation's length in it.
uint32_t current_anim_set(objects::GameEntity* object)
{
    objects::ObjectModule* blender = object->fetch_module(kAnimBlender);
    return static_cast<objects::AnimBlenderComponent*>(blender)->get_anim_set();
}

// The timeline's pieces (thiscall, not rewritten yet).
float get_step(AnimTimeline* t) { return static_cast<float>(t->next_time()); }
void passed_events(AnimTimeline* t, float from, float to) { static_cast<AnimTrack*>(t)->fire_events(from, to); }
void play_anim(AnimTimeline* t, uint32_t anim, float start, float end, int loop_type, float blend, float speed)
{
    t->play_anim(anim, start, end, loop_type, blend, speed);
}
bool is_accomplished(AnimTimeline* t) { return t->is_accomplished(); }

// Frame sources (0x0048BFC0 makes one, 0x0048BFD0 frees it) and their frames.
void* new_source() { return render::create_frame_source(); }
void free_source(void* source) { delete static_cast<render::FrameSource*>(source); }
void* source_frame(void* source, bool done) { return static_cast<render::FrameSource*>(source)->set_done(done); }
void* source_frame_of(void* source, uint32_t anim)
{
    return static_cast<render::FrameSource*>(source)->play_clip(anim);
}
int frame_index(void* frame) { return static_cast<JointClipFrames*>(frame)->acquire_cache_index(); }
// NOT ORIGINAL - the frame of an animation the source's set hasn't is null, and FrameIndex reads through it (a crash at
// 0x00482BA2, as in the original: e.g. peds whose animations weren't loaded, or a converted level's script playing one
// the skeleton hasn't): logged first, with the animation's name.
void* logged_source_frame_of(void* source, uint32_t set, uint32_t anim)
{
    void* frame = source_frame_of(source, set + anim);
    if (!frame) {
        const char* name = script::look_up_hash_name(anim);
        get_log("anim: no animation 0x%08X%s%s%s in the set 0x%08X - the next step (FrameIndex 0x00482BA0) crashes",
                anim, name ? " (" : "", name ? name : "", name ? ")" : "", set);
    }
    return frame;
}
void release_frame_index(void* frame, int index) { static_cast<JointClipFrames*>(frame)->release_cache_index(index); }
void frame_pose(void* frame, uint8_t* pose, float time, int index, void* source)
{
    static_cast<JointClipFrames*>(frame)->get_frames(reinterpret_cast<maths::QuatRotation*>(pose),
                                                     reinterpret_cast<maths::Vec4*>(pose + 0x400), pose + 0x800, time,
                                                     index, source);
}
// The frame source given the skeleton's bone masks (its bones' LOD masks) and its level of detail.
void set_bones(void* source, const uint8_t* skeleton)
{
    RigData* bones = reinterpret_cast<const Rig*>(skeleton)->payload_;
    static_cast<render::FrameSource*>(source)->bone_masks_ = bones ? bones->lod_masks_ : nullptr;
}
void set_bone_count(void* source, const uint8_t* skeleton)
{
    static_cast<render::FrameSource*>(source)->mask_bit_ = static_cast<int>(reinterpret_cast<const Rig*>(skeleton)->lod_);
}

// The game clock (ms) plus its offset.
int get_clock() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }

// The play-anim member function's parameters (the original leaves a missing one uninitialised: 0 here).
struct PlayParams {
    uint32_t anim;
    float start = 0.0f, end = 0.0f, blend = 0.0f, speed = 0.0f;
    int loop_type_ = 0;
};
PlayParams read_play_params(Struct* params)
{
    // (the anim name goes in the argument's own slot: the params' address if missing)
    PlayParams p;
    p.anim = reinterpret_cast<uint32_t>(params);
    params->read_hash_value(kAnimName, &p.anim, 1);
    params->fetch_float(kStartTime, &p.start, 1);
    params->fetch_float(kEndTime, &p.end, 1);
    params->fetch_int_value(kRepeatMode, &p.loop_type_, 1);
    params->fetch_float(kBlendPeriod, &p.blend, 1);
    params->fetch_float(kSpeed, &p.speed, 1);
    return p;
}

memory::PoolPair& standard_pools() { return g_standard_pools; }

} // namespace

// --- the standard controller ---------------------------------------------------------------------------------

// 0x004A4F30
void* StandardAnimController::operator new(size_t size, int) noexcept
{
    return standard_pools().carve(static_cast<unsigned int>(size));
}

// 0x004A5600
void StandardAnimController::operator delete(void* item) { standard_pools().dealloc(item); }

// 0x004A4500
StandardAnimController::StandardAnimController(AnimTimeline* timeline) : AnimDriver(timeline)
{
    blend_source_ = new_source();
    frame_source_ = source_frame(blend_source_, false);
    anim_set_ = current_anim_set(object_of(this));
    frame_source_ = nullptr;
    frame_index_ = -1;
}

// 0x004A4590
StandardAnimController::~StandardAnimController()
{
    if (frame_source_)
        release_frame_index(frame_source_, frame_index_);
    if (blend_source_) {
        free_source(blend_source_);
        blend_source_ = nullptr;
    }
    frame_source_ = nullptr;
}

// 0x004A4F70
bool StandardAnimController::fetch_pose(uint8_t* pose)
{
    if (frame_index_ == -1) {
        if (*reinterpret_cast<int*>(static_cast<uint8_t*>(frame_source_) + 0x24))
            frame_index_ = frame_index(frame_source_);
        else
            frame_source_ = source_frame(blend_source_, false);
    }
    set_bones(blend_source_, skeleton_of(this));
    set_bone_count(blend_source_, skeleton_of(this));
    frame_pose(frame_source_, pose, timeline_ptr_->ongoing_, frame_index_, blend_source_);
    source_frame(blend_source_, true);
    return true;
}

// 0x004A4670
int StandardAnimController::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = read_play_params(params);
        play_anim(timeline_ptr_, p.anim, p.start, p.end, p.loop_type_, p.blend, p.speed);
        frame_source_ = logged_source_frame_of(blend_source_, anim_set_, p.anim);
        frame_index_ = frame_index(frame_source_);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot, as above)
        params->read_hash_value(kAnim, &anim, 1);
        if (frame_source_)
            release_frame_index(frame_source_, frame_index_);
        if (blend_source_) {
            free_source(blend_source_);
            blend_source_ = nullptr;
        }
        blend_source_ = new_source();
        source_frame(blend_source_, false);
        frame_source_ = logged_source_frame_of(blend_source_, anim_set_, anim);
        frame_index_ = frame_index(frame_source_);
        return 1;
    }
    case kGlobalFrame: {
        int value = ClipStore::get_pull()->fetch_kind();
        static_cast<JointClipFrames*>(frame_source_)->build_cache(value);
        return 1;
    }
    case kInvalidateCache:
        frame_source_ = source_frame(blend_source_, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// 0x004A4620
void StandardAnimController::tick_frame()
{
    float before = timeline_ptr_->ongoing_;
    timeline_ptr_->ongoing_ = get_step(timeline_ptr_);
    passed_events(timeline_ptr_, before, timeline_ptr_->ongoing_);
    if (timeline_ptr_->ongoing_ < before)
        frame_source_ = source_frame(blend_source_, false);
}

// --- the weighted controller ---------------------------------------------------------------------------------

// 0x004A5010
WeightedAnimController::WeightedAnimController(AnimTimeline* timeline, objects::GameEntity* object)
    : AnimDriver(timeline)
{
    owner_object_ = timeline ? object_of(this) : object;
    blend_source_ = new_source();
    frame_source_ = source_frame(blend_source_, false);
    anim_set_ = current_anim_set(owner_object_);
    frame_source_ = nullptr;
    weight_ = 0.0f;
}

// 0x004A4820
WeightedAnimController::~WeightedAnimController()
{
    if (blend_source_) {
        free_source(blend_source_);
        blend_source_ = nullptr;
    }
    frame_source_ = nullptr;
}

// 0x004A5660
bool WeightedAnimController::fetch_pose(uint8_t* pose)
{
    if (!frame_source_ || !blend_source_ || weight_ <= 0.0f)
        return false;
    frame_source_ = source_frame(blend_source_, false);
    // (the skeleton component's skeleton, +0x18 - read through null without one, as the original)
    auto skeleton = [this] {
        objects::ObjectModule* component = owner_object_->fetch_module(kSkeleton);
        return component ? static_cast<objects::BoneRigModule*>(component)->bone_rig_ : nullptr;
    };
    set_bones(blend_source_, skeleton());
    set_bone_count(blend_source_, skeleton());
    static_cast<JointClipFrames*>(frame_source_)
        ->apply_additive_if_compressed(reinterpret_cast<maths::QuatRotation*>(pose),
                                       reinterpret_cast<maths::Vec4*>(pose + 0x400), pose + 0x800, weight_,
                                       static_cast<const render::FrameSource*>(blend_source_));
    source_frame(blend_source_, true);
    return true;
}

// 0x004A4890
int WeightedAnimController::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kPlayAnim: {
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's slot: params' address if missing)
        if (!params->read_hash_value(kWeightedAnim, &anim, 0))
            params->read_hash_value(kAnimName, &anim, 1);
        params->fetch_float(kWeight, &weight_, 0);
        frame_source_ = source_frame_of(blend_source_, anim_set_ + anim);
        return 1;
    }
    case kSetWeightedAnim: {
        uint32_t anim = 0;
        if (!params->read_hash_value(kWeightedAnim, &anim, 0))
            params->read_hash_value(kAnimName, &anim, 0);
        if (anim)
            frame_source_ = source_frame_of(blend_source_, anim_set_ + anim);
        params->fetch_float(kWeight, &weight_, 1);
        return 1;
    }
    case kNewSource: {
        uint32_t anim = reinterpret_cast<uint32_t>(params);
        params->read_hash_value(kAnim, &anim, 1);
        if (blend_source_) {
            free_source(blend_source_);
            blend_source_ = nullptr;
        }
        blend_source_ = new_source();
        source_frame(blend_source_, false);
        frame_source_ = source_frame_of(blend_source_, anim_set_ + anim);
        return 1;
    }
    case kInvalidateCache:
        frame_source_ = source_frame(blend_source_, false);
        return kNotMine;
    default:
        return kNotMine;
    }
}

// --- the wobble controller -----------------------------------------------------------------------------------

// 0x004A49F0
WobbleDriver::WobbleDriver(AnimTimeline* timeline) : AnimDriver(timeline)
{
    aim_point_ = 0.0f;
    amp_a_ = amp_b_ = k1_ = k2_ = spaz_factor_ = 0.0f;
    blend_source_ = new_source();
    frame_source_ = source_frame(blend_source_, false);
    anim_set_ = current_anim_set(object_of(this));
}

// 0x004A4A80
WobbleDriver::~WobbleDriver()
{
    if (blend_source_) {
        free_source(blend_source_);
        blend_source_ = nullptr;
    }
    if (frame_source_)
        frame_source_ = nullptr;
}

// 0x004A5110
bool WobbleDriver::fetch_pose(uint8_t* pose)
{
    set_bones(blend_source_, skeleton_of(this));
    set_bone_count(blend_source_, skeleton_of(this));
    frame_pose(frame_source_, pose, timeline_ptr_->ongoing_, -1, blend_source_);
    source_frame(blend_source_, true);
    return true;
}

// 0x004A4AF0
int WobbleDriver::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kPlayAnim: {
        PlayParams p = read_play_params(params);
        play_anim(timeline_ptr_, p.anim, p.start, p.end, p.loop_type_, p.blend, p.speed);
        frame_source_ = source_frame_of(blend_source_, anim_set_ + p.anim);
        return 1;
    }
    case kInvalidateCache:
        frame_source_ = source_frame(blend_source_, false);
        return kNotMine;
    case kSetTarget: {
        float fraction;
        if (params->fetch_float(kFraction, &fraction, 0)) {
            if (fraction < 0.0f)
                fraction = 0.0f;
            else if (fraction > 1.0f)
                fraction = 1.0f;
            const AnimTimeline* t = timeline_ptr_;
            aim_point_ = static_cast<float>((static_cast<double>(t->finish_) - t->outset_) * fraction + t->outset_);
        }
        return 1;
    }
    case kSetWobbleDetails: {
        float amp_a, amp_b, k1, k2, spaz;
        if (params->fetch_float(kWobbleAmpA, &amp_a, 0) && params->fetch_float(kSwayAmpSecond, &amp_b, 0) &&
            params->fetch_float(kWobbleK1, &k1, 0) && params->fetch_float(kWobbleK2, &k2, 0) &&
            params->fetch_float(kSpazFactor, &spaz, 0)) {
            amp_a_ = amp_a;
            amp_b_ = amp_b;
            k1_ = k1;
            k2_ = k2;
            spaz_factor_ = spaz;
        }
        return 1;
    }
    default:
        return kNotMine;
    }
}

// 0x004A5180: the amplitudes grow (spazFactor at the timeline's ends, 1 in its middle) with the target's distance
// from the middle, the target is kept far enough from the ends for them, then the first sine (K1) swings the
// second's amplitude around amp_a, and the second (K2) the time around the target; within the timeline.
float WobbleDriver::wobble_time()
{
    const AnimTimeline* t = timeline_ptr_;
    float target = aim_point_;
    float k1 = k1_, k2 = k2_;
    double scale =
        std::fabs(target - (static_cast<double>(t->outset_) + t->finish_) * 0.5) *
            ((static_cast<double>(spaz_factor_) - 1.0) / ((static_cast<double>(t->finish_) - t->outset_) * 0.5)) +
        1.0;
    float a = static_cast<float>(scale * amp_a_);
    float b = static_cast<float>(scale * amp_b_);
    if (static_cast<double>(target) - a - b < t->outset_)
        target = static_cast<float>(static_cast<double>(b) + a + t->outset_);
    if (static_cast<double>(target) + b + a > t->finish_)
        target = static_cast<float>(static_cast<double>(t->finish_) - a - b);

    double cycles = static_cast<double>(get_clock()) * k1;
    double phase = (cycles - static_cast<double>(get_truncated(cycles)) * 0.0625 * 16.0) * two_pi();
    float amplitude = static_cast<float>(x87_sin(phase) * b + a);
    cycles = static_cast<double>(get_clock()) * k2;
    phase = (cycles - static_cast<double>(get_truncated(cycles)) * 0.0625 * 16.0) * two_pi();
    double time = x87_sin(phase) * amplitude + target;
    if (time < t->outset_)
        time = t->outset_;
    if (time > t->finish_)
        time = t->finish_;
    return static_cast<float>(time);
}

// 0x004A52E0
void WobbleDriver::tick_frame()
{
    float before = timeline_ptr_->ongoing_;
    timeline_ptr_->ongoing_ = wobble_time();
    if (timeline_ptr_->ongoing_ < before)
        frame_source_ = source_frame(blend_source_, false);
}

// --- the pose controller -------------------------------------------------------------------------------------

// 0x004A5730
StanceDriver::StanceDriver(AnimTimeline* timeline) : AnimDriver(timeline)
{
    auto* rotations = reinterpret_cast<float*>(stance_);
    for (int i = 0; i < 64; ++i) {
        rotations[i * 4 + 0] = 0.0f;
        rotations[i * 4 + 1] = 0.0f;
        rotations[i * 4 + 2] = 0.0f;
        rotations[i * 4 + 3] = 1.0f;
    }
}

// 0x004A4D20
StanceDriver::~StanceDriver() {}

// 0x004A4D30
bool StanceDriver::fetch_pose(uint8_t* pose)
{
    std::memcpy(pose, stance_, sizeof(stance_));
    return true;
}

// --- the partial animation controller ------------------------------------------------------------------------

// 0x004A5350
PartialClipDriver::PartialClipDriver(AnimTimeline* timeline) : AnimDriver(timeline)
{
    blend_source_ = new_source();
    frame_source_ = source_frame(blend_source_, false);
    anim_set_ = current_anim_set(object_of(this));
    void* memory = memory::alloc_zeroed(sizeof(AnimTimeline));
    partial_ = memory ? static_cast<AnimTimeline*>(memory)->make_new() : nullptr;
    frame_source_ = nullptr;
}

// 0x004A4D50
PartialClipDriver::~PartialClipDriver()
{
    if (blend_source_) {
        free_source(blend_source_);
        blend_source_ = nullptr;
    }
    if (partial_) {
        partial_->teardown(1);
        partial_ = nullptr;
    }
    if (frame_source_)
        frame_source_ = nullptr;
}

// 0x004A5430
bool PartialClipDriver::fetch_pose(uint8_t* pose)
{
    set_bones(blend_source_, skeleton_of(this));
    set_bone_count(blend_source_, skeleton_of(this));
    frame_pose(frame_source_, pose, partial_->ongoing_, -1, blend_source_);
    source_frame(blend_source_, true);
    return true;
}

// 0x004A4DD0
void PartialClipDriver::configure_from_data(script::Struct* params)
{
    base_init(this, params);
    uint32_t anim = 0;
    params->read_hash_value(kAnimName, &anim, 1);
    float start = 0.0f, end = 0.0f, current = 0.0f;
    objects::ObjectModule* blender = object_of(this)->fetch_module(kAnimBlender);
    // (the blender's fetch_value: the anim set's entry for the animation - its length)
    float duration = static_cast<objects::AnimBlenderComponent*>(blender)->fetch_value(anim);
    // the times and loop type given (0x004877E0, 0x00487770; cdecl)
    fetch_anim_durations(&start, &end, current, duration, params);
    int loop_type = 0; // (the original leaves it to 0x00487770)
    get_anim_loop_type(&loop_type, params);
    float speed = 1.0f;
    params->fetch_float(kSpeed, &speed, 0);
    params->fetch_float(kCurrent, &current, 0);
    // the timeline's PlayAnim (the original's slot 3)
    partial_->play_anim(anim, start, end, loop_type, 0.0f, speed);
    if (current != 0.0f)
        partial_->advance_clock(current);
    frame_source_ = source_frame_of(blend_source_, anim_set_ + anim);
}

// 0x004A54A0
int PartialClipDriver::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kStep: {
        // the timeline stepped once by `frames` (its speed held at 0 meanwhile)
        float frames = 0.0f; // (missing: the original's is whatever its argument slot held)
        params->fetch_float(kFrames, &frames, 1);
        float speed = static_cast<float>(static_cast<double>(partial_->speed_) * frames_per_second());
        partial_->speed_ = 0.0f;
        partial_->ongoing_ = static_cast<float>(static_cast<double>(frames) + partial_->ongoing_);
        partial_->tick_frame();
        partial_->speed_ = static_cast<float>(static_cast<double>(speed) * seconds_per_frame());
        return 1;
    }
    case kSetSpeed: {
        float speed = 0.0f;
        params->fetch_float(kSpeed, &speed, 1);
        partial_->speed_ = static_cast<float>(static_cast<double>(speed) * seconds_per_frame());
        return 1;
    }
    case kInvalidateCache:
        frame_source_ = source_frame(blend_source_, false);
        return kNotMine;
    case kGetPartialAnimParams: {
        script->args_->push_float(kPartialAnimTime, partial_->ongoing_);
        script->args_->put_name_key(kPartialAnim, partial_->anim_);
        Struct* out = script->args_;
        out->put_int(kPartialAnimComplete, is_accomplished(partial_));
        return 1;
    }
    case kReverse:
        partial_->direction_ = partial_->direction_ ? 0 : -1;
        return 1;
    default:
        return kNotMine;
    }
}

// 0x004A4EF0
void PartialClipDriver::tick_frame()
{
    float before = partial_->ongoing_;
    partial_->ongoing_ = get_step(partial_);
    if (partial_->ongoing_ < before)
        frame_source_ = source_frame(blend_source_, false);
}

} // namespace gfx

using namespace gfx;
