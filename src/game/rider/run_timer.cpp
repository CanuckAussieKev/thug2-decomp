// The skater's run timer component (GameObjects::RiderRunClock): see run_timer.h.

#include "engine/core/checked_index.h"
#include "game/rider/run_timer.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/objects/entity.h"
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
#include "engine/core/down_cast.h"

namespace objects {
namespace {
constexpr uint32_t kRunTimerPause = 0x82445966; // RunTimer_Pause
constexpr uint32_t kSkaterRunTimer = 0x8BCE7E66; // SkaterRunTimer
constexpr uint32_t kRunTimerUnpause = 0x956236B1; // RunTimer_Unpause
constexpr uint32_t kRunTimerGetFactorComplete = 0xAB04CBF9; // RunTimer_GetFactorComplete
constexpr uint32_t kRunTimerGetRunTimerControllerId = 0x96D81D8E; // RunTimer_GetRunTimerControllerId
constexpr uint32_t kRunTimerGetRunTimerId = 0x81AAA6DD; // RunTimer_GetRunTimerId
constexpr uint32_t kRunTimerComboStarted = 0x37CC7691; // RunTimer_ComboStarted
} // namespace

std::array<script::MemberFunctionEntry, kRunTimerMemberFunctionCount>& run_timer_member_functions()
{
    static std::array<script::MemberFunctionEntry, kRunTimerMemberFunctionCount> run_timer_member_functions{{
        {.callsign_ = kRunTimerPause, .owner_kind_ = kSkaterRunTimer}, {.callsign_ = kRunTimerUnpause, .owner_kind_ = kSkaterRunTimer}, {.callsign_ = kRunTimerGetFactorComplete, .owner_kind_ = kSkaterRunTimer}, {.callsign_ = kRunTimerGetRunTimerControllerId, .owner_kind_ = kSkaterRunTimer},
        {.callsign_ = kRunTimerGetRunTimerId, .owner_kind_ = kSkaterRunTimer}, {.callsign_ = kRunTimerComboStarted, .owner_kind_ = kSkaterRunTimer}, {.callsign_ = 0, .owner_kind_ = kSkaterRunTimer}}};
    return run_timer_member_functions;
}

namespace {

constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kMState = 0x109B9260, kMTimer = 0xD9D89E81, kHangSpeedAdjustment = 0x92FB4D09,
                   kRunTimerUp = 0x9E22A8E9, kHideRunTimer = 0x813CC576, kShowRunTimer = 0x47EEC244,
                   kDuration = 0x0B84F532 /* Physics_RunTimer_Duration */, kFactorComplete = 0x41F56511,
                   kClockCtlKey = 0x39778122, kRunTimerId = 0x628F21C9;
// The run timer script's spawn id and the ids the scripts get: from the skater's index (the object's +0x2BC).
constexpr uint32_t kRunTimerScriptId = 0x83321DEA, kRunTimerIdBase = 0x7D42821C;

constexpr int kMaxResumes = 8;
constexpr float kResumeShare = 0.125f; // 0x0064B960 (1 / kMaxResumes)

// The state names cur_phase_ shows as (0x0064C080).
const std::array<uint32_t, 4>& mode_label_keys()
{
    static const std::array<uint32_t, 4> kRunTimerStateNames{{0x742CF11F, 0x5392CA8E, 0x26776873, 0xDEF417FA}};
    return kRunTimerStateNames;
}

uint32_t get_skater_index(GameEntity* skater) { return static_cast<uint32_t>(core::down_cast<Rider>(skater)->view_slot()); }
uint32_t timer_script_id(GameEntity* skater) { return get_skater_index(skater) + kRunTimerScriptId; }

// The skater's physics value `name`.
double physics_value(GameEntity* skater, uint32_t name)
{
    return core::down_cast<Rider>(skater)->get_scaled_physics(name);
}

// An event of `type` to the run timer's script (no data, not broadcast).
void tell_timer_script(GameEntity* skater, uint32_t type)
{
    get_tracker()->launch_event(type, timer_script_id(skater), skater->ident(), nullptr, false, -1.0f);
}

} // namespace

// 0x005185D0
gsl::owner<ObjectModule*> RiderRunClock::fabricate() { return new RiderRunClock; }

// (the constructor: inlined in fabricate in the original)
RiderRunClock::RiderRunClock() { set_kind_tag(kSkaterRunTimer); }

// 0x00518240
void RiderRunClock::publish_method_table()
{
    script::register_member_function_table(run_timer_member_functions());
}

// 0x00518260 (0x005182B0 the deleting one)
RiderRunClock::~RiderRunClock() = default;

// 0x005182E0
void RiderRunClock::seal() { walk_ = core::down_cast<FootMovement>(entity()->fetch_module(kWalk)); }

// 0x00518270
void RiderRunClock::fetch_diagnostics(script::Struct* params)
{
    params->put_name_key(checksums::Name{kMState}, core::at(mode_label_keys(), cur_phase_));
    params->push_float(checksums::Name{kMTimer}, seconds_remaining_);
    // (then the base's fetch_diagnostics, which is empty: 0x005BAB90)
}

// 0x00518300
void RiderRunClock::tick_frame()
{
    if (cur_phase_ == kClockTicking) {
        const int walk_state = walk_->get_state();
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
            entity()->post_own_event(kRunTimerUp, nullptr);
        }
    } else if (cur_phase_ == kActiveTimeUp) {
        entity()->post_own_event(kRunTimerUp, nullptr);
    }
}

// 0x00518390
void RiderRunClock::halt()
{
    if (script::look_up_spawned_script(timer_script_id(entity())))
        tell_timer_script(entity(), kHideRunTimer);
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
        const double cap = physics_value(entity(), kDuration) * (kMaxResumes - resumes) * kResumeShare;
        if (!(seconds_remaining_ < cap))
            seconds_remaining_ = static_cast<float>(cap);
    }
    resumes_ = static_cast<int16_t>(resumes + 1);
    tell_timer_script(entity(), kShowRunTimer);
}

// 0x005184A0
bool RiderRunClock::get_freeze(script::Struct*, script::Script*)
{
    if (cur_phase_ != kClockIdle) {
        cur_phase_ = kActivePaused;
        tell_timer_script(entity(), kHideRunTimer);
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
    script->args()->push_float(checksums::Name{kFactorComplete},
                              static_cast<float>(seconds_remaining_ / physics_value(entity(), kDuration)));
    return true;
}
// 0x00518540
bool RiderRunClock::fetch_run_clock_pad_id(script::Struct*, script::Script* script)
{
    script->args()->put_name_key(checksums::Name{kClockCtlKey}, timer_script_id(entity()));
    return true;
}
// 0x00518570
bool RiderRunClock::get_run_timer_id(script::Struct*, script::Script* script)
{
    script->args()->put_name_key(checksums::Name{kRunTimerId}, get_skater_index(entity()) + kRunTimerIdBase);
    return true;
}
// 0x005185A0
bool RiderRunClock::chain_began(script::Struct*, script::Script*)
{
    seconds_remaining_ = static_cast<float>(physics_value(entity(), kDuration));
    resumes_ = 0;
    cur_phase_ = kActivePaused;
    return true;
}

} // namespace objects
