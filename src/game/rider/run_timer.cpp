// The skater's run timer component (GameObjects::RiderRunClock): see run_timer.h.

#include "game/rider/run_timer.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/skater_camera_component.h"
#include "game/rider/walk_physics.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace objects {

namespace {
uint32_t g_run_timer_state_names[4]{0x742CF11F, 0x5392CA8E, 0x26776873, 0xDEF417FA};
} // namespace
uint32_t g_run_timer_member_functions[42]{0x82445966, 0x8BCE7E66, 0, 0, 0, 0, 0x956236B1, 0x8BCE7E66, 0, 0, 0, 0,
                                                 0xAB04CBF9, 0x8BCE7E66, 0, 0, 0, 0, 0x96D81D8E, 0x8BCE7E66, 0, 0, 0, 0,
                                                 0x81AAA6DD, 0x8BCE7E66, 0, 0, 0, 0, 0x37CC7691, 0x8BCE7E66, 0, 0, 0, 0,
                                                 0,          0x8BCE7E66, 0, 0, 0, 0};

namespace {

constexpr uint32_t kSkaterRunTimer = 0x8BCE7E66, kWalk = 0x726E85AA;
constexpr uint32_t kMState = 0x109B9260, kMTimer = 0xD9D89E81, kHangSpeedAdjustment = 0x92FB4D09,
                   kRunTimerUp = 0x9E22A8E9, kHideRunTimer = 0x813CC576, kShowRunTimer = 0x47EEC244,
                   kDuration = 0x0B84F532 /* Physics_RunTimer_Duration */, kFactorComplete = 0x41F56511,
                   kClockCtlKey = 0x39778122, kRunTimerId = 0x628F21C9;
// The run timer script's spawn id and the ids the scripts get: from the skater's index (the object's +0x2BC).
constexpr uint32_t kRunTimerScriptId = 0x83321DEA, kRunTimerIdBase = 0x7D42821C;

constexpr int kMaxResumes = 8;
constexpr float kResumeShare = 0.125f; // 0x0064B960 (1 / kMaxResumes)

// The state names cur_phase_ shows as (0x0064C080).
const uint32_t* mode_label_keys() { return g_run_timer_state_names; }

uint32_t get_skater_index(GameEntity* skater) { return static_cast<uint32_t>(static_cast<Rider*>(skater)->view_slot_); }
uint32_t timer_script_id(GameEntity* skater) { return get_skater_index(skater) + kRunTimerScriptId; }

// The skater's physics value `name`.
double physics_value(GameEntity* skater, uint32_t name)
{
    return static_cast<Rider*>(skater)->get_scaled_physics(name);
}

// An event of `type` to the run timer's script (no data, not broadcast).
void tell_timer_script(GameEntity* skater, uint32_t type)
{
    get_tracker()->launch_event(type, timer_script_id(skater), skater->ident_, nullptr, false, -1.0f);
}

} // namespace

// 0x005185D0
memory::owner<ObjectModule*> RiderRunClock::fabricate() { return new RiderRunClock; }

// (the constructor: inlined in fabricate in the original)
RiderRunClock::RiderRunClock() { kind_tag_ = kSkaterRunTimer; }

// 0x00518240
void RiderRunClock::publish_method_table()
{
    script::register_member_function_table(cell::pointer<void>(kSkaterRunTimer), g_run_timer_member_functions,
                                           0x18);
}

// 0x00518260 (0x005182B0 the deleting one)
RiderRunClock::~RiderRunClock() = default;

// 0x005182E0
void RiderRunClock::seal() { walk_ = raw::bytes(entity_->fetch_module(kWalk)); }

// 0x00518270
void RiderRunClock::fetch_diagnostics(script::Struct* params)
{
    params->put_name_key(kMState, mode_label_keys()[cur_phase_]);
    params->push_float(kMTimer, seconds_remaining_);
    // (then the base's fetch_diagnostics, which is empty: 0x005BAB90)
}

// 0x00518300
void RiderRunClock::tick_frame()
{
    if (cur_phase_ == kClockTicking) {
        const int walk_state = raw::as<const objects::FootMovement>(walk_)->state_;
        double elapsed = 0.0;
        if (walk_state >= 2 && walk_state <= 4) {
            const float adjustment = script::fetch_float(kHangSpeedAdjustment);
            elapsed = frame_clock::get_slomo_frame_seconds() * adjustment;
        } else {
            elapsed = frame_clock::get_slomo_frame_seconds();
        }
        seconds_remaining_ = static_cast<float>(seconds_remaining_ - elapsed);
        if (seconds_remaining_ < 0.0f) {
            cur_phase_ = kActiveTimeUp;
            entity_->post_own_event(kRunTimerUp, nullptr);
        }
    } else if (cur_phase_ == kActiveTimeUp) {
        entity_->post_own_event(kRunTimerUp, nullptr);
    }
}

// 0x00518390
void RiderRunClock::halt()
{
    if (script::look_up_spawned_script(timer_script_id(entity_)))
        tell_timer_script(entity_, kHideRunTimer);
    cur_phase_ = kClockIdle;
}

// 0x005183F0
void RiderRunClock::get_resume()
{
    if (cur_phase_ == kClockIdle)
        return;
    cur_phase_ = seconds_remaining_ > 0.0f ? kClockTicking : kActiveTimeUp;
    const int16_t resumes = resumes_;
    if (resumes < kMaxResumes) {
        const double cap = physics_value(entity_, kDuration) * (kMaxResumes - resumes) * kResumeShare;
        if (!(seconds_remaining_ < cap))
            seconds_remaining_ = static_cast<float>(cap);
    }
    resumes_ = static_cast<int16_t>(resumes + 1);
    tell_timer_script(entity_, kShowRunTimer);
}

// 0x005184A0
bool RiderRunClock::get_freeze(script::Struct*, script::Script*)
{
    if (cur_phase_ != kClockIdle) {
        cur_phase_ = kActivePaused;
        tell_timer_script(entity_, kHideRunTimer);
    }
    return true;
}
// 0x005184F0
bool RiderRunClock::unpause(script::Struct*, script::Script*)
{
    get_resume();
    return true;
}
// 0x00518500
bool RiderRunClock::get_factor_complete(script::Struct*, script::Script* script)
{
    script->args_->push_float(kFactorComplete,
                              static_cast<float>(seconds_remaining_ / physics_value(entity_, kDuration)));
    return true;
}
// 0x00518540
bool RiderRunClock::fetch_run_clock_pad_id(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(kClockCtlKey, timer_script_id(entity_));
    return true;
}
// 0x00518570
bool RiderRunClock::get_run_timer_id(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(kRunTimerId, get_skater_index(entity_) + kRunTimerIdBase);
    return true;
}
// 0x005185A0
bool RiderRunClock::chain_began(script::Struct*, script::Script*)
{
    seconds_remaining_ = static_cast<float>(physics_value(entity_, kDuration));
    resumes_ = 0;
    cur_phase_ = kActivePaused;
    return true;
}

} // namespace objects
