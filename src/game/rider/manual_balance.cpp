// The balance of a balance trick, 0x00579850-0x0057A177. See manual_balance.h.

#include "game/rider/manual_balance.h"

#include <cmath>
#include <cstdint>
#include <new>

#include "engine/maths/maths.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/stats_manager.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/skate/point_tally.h"
#include "game/rider/balance_trick.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_score.h"
#include "engine/objects/input_component.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"
#include "engine/core/checksums.h"

namespace objects {

namespace {
constexpr float kRobotRunTime = 0.5f;
constexpr double kHalf = 0.5;
constexpr float kBailFraction = 0.9f;
constexpr float kCheeseFloor = 0.01f;
constexpr int kShakeRange = 50;
constexpr float kShakeScale = 0.01f;
constexpr float kPositionToUnits = 0.0001220703125f; // 1 / 8192
constexpr float kLeanToUnits = 0.000244140625f;      // 1 / 4096

// Names.
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;
constexpr uint32_t kSkaterScore = 0xD262802F;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kStatsManager = 0x4FF8DAC6;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kResetBalanceIndicator = 0x18F77EA1; // reset_balance_indicator (a script)
constexpr uint32_t kShowBalanceIndicator = 0x5D07E2BE;  // (a script integer)
constexpr uint32_t kGrind = 0x255ED86F;
constexpr uint32_t kSlide = 0x8D10119D;
constexpr uint32_t kNosemanual = 0x0AC90769;
constexpr uint32_t kManual = 0xEF24413B;
constexpr uint32_t kUp = 0xBC6B118F;
constexpr uint32_t kDown = 0xE3006FC4;
constexpr uint32_t kOffMeterTop = 0xD77B6FF9;
constexpr uint32_t kOffMeterBottom = 0x47D44B84;
// The physics values (the balance trick component's, 0x004F90A0) and script globals.
constexpr uint32_t kCheese = 0x1231AE3C;
constexpr uint32_t kCheeseFrames = 0xA4362662;
constexpr uint32_t kSameGrindAddTime = 0xFBEC5B6C;
constexpr uint32_t kNewGrindSubTime = 0xC5E1C8EB;
constexpr uint32_t kRobotRailAddTime = 0xF54CA12F;
constexpr uint32_t kRobotRailNudge = 0x30990680;
constexpr uint32_t kRepeatMin = 0x8F4DB5D3;
constexpr uint32_t kRepeatMultiplier = 0x93DD6A10;
constexpr uint32_t kLeanRepeatMultiplier = 0x6CE10EA4;
constexpr uint32_t kLeanBailAngle = 0xF81AB0E9;
constexpr uint32_t kLeanGravityStat = 0x06851748;
constexpr uint32_t kInstableRate = 0xA9FEF5AB;
constexpr uint32_t kInstableBase = 0xB6A634F3;
constexpr uint32_t kLeanAcc = 0x61AD5596;
constexpr uint32_t kLeanMinSpeed = 0xEF27E8FF;
constexpr uint32_t kLeanRndSpeed = 0x32EB3308;
constexpr uint32_t kBalanceSafeButtonPeriod = 0x8CC8F176;
constexpr uint32_t kBalanceIgnoreButtonPeriod = 0x07DA1621;

constexpr double kFrames = 60.0; // 0x00645EA0 (frames a second)

uint32_t get_clock() { return static_cast<uint32_t>(frame_clock::game_clock_elapsed() + clock_offset()); }
// A physics value (0x004F90A0 on the balance trick component).
double get_physics(RiderTeeterMoves* balance_trick, uint32_t name) { return balance_trick->get_physics_value(name); }
uint32_t& get_trick_type(RiderTeeterMoves* balance_trick) { return balance_trick->trick(); }
modules::PointTally* score_display(RiderPointKeeper* score) { return score->points_held(); }
// The score's two balance displays (0x00563370 / 0x00563390): on or off, and the lean shown.
void show_lean(void* score, int on, float lean)
{
    static_cast<modules::PointTally*>(score)->set_teeter_gauge(on != 0, lean);
}
void show_lean2(void* score, int on, float lean)
{
    static_cast<modules::PointTally*>(score)->set_wheelie_gauge(on != 0, lean);
}
// A button of the input component's control data held (its first byte).
bool get_held(PadInputModule* input, uint32_t button)
{
    return input->pad_primary().read_button(button)->held_down() != 0;
}

void reset_state(Balance* b)
{
    b->set_cheese(0.0f);
    b->set_clock(0.0f);
    b->set_on_rail_seconds(0.0f);
    b->set_lean(0.0f);
    b->set_lean_speed(0.0f);
    b->set_pushed(false);
    b->set_show_meter(false);
    b->set_rail(-1);
}

} // namespace

// 0x00579860
void Balance::prepare_all(GameEntity* skater)
{
    skater_ = skater;
    balance_trick_ = core::down_cast<RiderTeeterMoves>(skater->fetch_module(kSkaterBalanceTrick));
    points_held_ = core::down_cast<RiderPointKeeper>(skater_->fetch_module(kSkaterScore));
    input_ = core::down_cast<PadInputModule>(skater_->fetch_module(kInput));
}

// 0x005798A0
void Balance::get_reset()
{
    if (script::fetch_int_value(kShowBalanceIndicator))
        script::launch_script(kResetBalanceIndicator, nullptr, nullptr, false, nullptr);
    reset_state(this);
}

// 0x005798F0
void Balance::clear_best_time() { best_time_ = 0.0f; }
// 0x00579900
void Balance::hide_display() { hide_display_ = true; }
// 0x00579910
void Balance::show_display() { hide_display_ = false; }

// 0x00579920
void Balance::keep_best_time()
{
    if (clock_ > best_time_)
        best_time_ = clock_;
    clock_ = 0.0f;
}

// 0x00579940
void Balance::show_meter() { show_meter_ = true; }

// 0x00579950
Balance::Balance()
{
    if (script::fetch_int_value(kShowBalanceIndicator))
        script::launch_script(kResetBalanceIndicator, nullptr, nullptr, false, nullptr);
    reset_state(this);
}

// 0x005799D0
void Balance::halt()
{
    if (score_display(points_held_)) {
        show_lean(score_display(points_held_), 0, 0.0f);
        show_lean2(score_display(points_held_), 0, 0.0f);
    }
    left_button_ = 0;
    right_press_ = 0;
}

// 0x00579A00
void Balance::kickoff(BalanceKickoff balance_kickoff, bool flipped, bool flip)
{
    const auto left_button = balance_kickoff.left_button_;
    const auto right_button = balance_kickoff.right_button_;
    const auto trick = balance_kickoff.trick;
    left_button_ = left_button;
    right_press_ = right_button;
    trick_ = trick;
    clock_ = 0.0f;
    pushed_ = false;
    mirrored_ = flipped;
    flip_ = flip;
    const uint32_t type = get_trick_type(balance_trick_);
    if (type == kGrind || type == kSlide) {
        auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater_->fetch_module(kSkaterCorePhysics));
        void* rail = core->rail_node();
        if (rail_ != -1) {
            if (static_cast<GrindPoint*>(rail)->is_named(static_cast<uint32_t>(rail_))) { // the same rail
                on_rail_seconds_ =
                    static_cast<float>(get_physics(balance_trick_, kSameGrindAddTime) + on_rail_seconds_);
            } else {
                const double time = on_rail_seconds_ - get_physics(balance_trick_, kNewGrindSubTime);
                on_rail_seconds_ = static_cast<float>(time);
                if (time < 0.0)
                    on_rail_seconds_ = 0.0f;
                cheese_ = 0.0f;
            }
        }
        rail_ = static_cast<int>(static_cast<GrindPoint*>(rail)->label());
        // A robot rail: a short run (under half a second) gets time and a nudge.
        const float run_time = score_display(points_held_)->robot_factor();
        if (run_time < kRobotRunTime) {
            auto inverse = static_cast<float>(1.0 / run_time);
            on_rail_seconds_ = static_cast<float>(
                script::fetch_float(kRobotRailAddTime) * static_cast<double>(inverse) * kHalf + on_rail_seconds_);
            float side = -1.0f;
            if (!(lean_speed_ < 0.0f))
                side = 1.0f;
            lean_ = static_cast<float>(
                script::fetch_float(kRobotRailNudge) * static_cast<double>(inverse) * side * kHalf + lean_);
        }
    }
    auto repeat_min = static_cast<float>(get_physics(balance_trick_, kRepeatMin));
    if (std::fabs(lean_speed_) != 0.0f) {
        const double speed = get_physics(balance_trick_, kRepeatMultiplier) * lean_speed_;
        lean_speed_ = static_cast<float>(speed);
        if (std::fabs(speed) < repeat_min)
            lean_speed_ = static_cast<float>((speed < 0.0 ? -1.0 : 1.0) * repeat_min);
    } else {
        lean_speed_ = repeat_min;
        const uint32_t balance = get_trick_type(balance_trick_);
        if (balance == kNosemanual)
            lean_speed_ = -repeat_min;
        else if (balance != kManual && maths::rnd(2))
            lean_speed_ = -lean_speed_;
    }
    const double lean = get_physics(balance_trick_, kLeanRepeatMultiplier) * lean_;
    lean_ = static_cast<float>(lean);
    lean_ = static_cast<float>((lean < 0.0 ? -1.0 : 1.0) * cheese_ + lean);
    const double bail = get_physics(balance_trick_, kLeanBailAngle);
    if (std::fabs(lean_) > bail) {
        // Kept just inside the bail angle, leaning the way it speeds.
        auto limit = static_cast<float>(kBailFraction * bail);
        lean_ = limit;
        double side = lean_speed_ < 0.0f ? -1.0 : 1.0;
        lean_ = static_cast<float>(std::fabs(limit) * side);
        side = lean_speed_ < 0.0f ? -1.0 : 1.0;
        lean_speed_ = static_cast<float>(side * bail);
    }
    cheese_ = static_cast<float>(get_physics(balance_trick_, kCheese));
    show_meter_ = false;
    start_time_ = get_clock();
}

// 0x00579CC0
void Balance::tick_frame()
{
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    if (!left_button_ || !right_press_)
        return;
    auto* net = online::net_game_state().online_hub();
    if (net->host())                                                           // (the folded getter 0x005480C0)
        net->find_player_slot_by_obj(static_cast<uint16_t>(skater_->ident())); // (its result unused)
    // The cheese wears off.
    if (cheese_ > kCheeseFloor) {
        auto cheese = static_cast<float>(get_physics(balance_trick_, kCheese));
        const double wear = cheese / get_physics(balance_trick_, kCheeseFrames) * frame * kFrames;
        const double left = cheese_ - wear;
        cheese_ = static_cast<float>(left);
        if (left < 0.0)
            cheese_ = 0.0f;
    }
    static_cast<modules::PointTally*>(score_display(points_held_))->tweak_trick(static_cast<int>(trick_));
    clock_ = static_cast<float>(static_cast<double>(frame) + clock_);
    on_rail_seconds_ = static_cast<float>(static_cast<double>(frame) + on_rail_seconds_);
    ObjectModule* stats = skater_->fetch_module(kStatsManager);
    core::down_cast<AttributeTracker>(stats)->check_balance_time(checksums::Name{get_trick_type(balance_trick_)}, clock_, 0);
    // The lean speeds up.
    auto gravity = static_cast<float>(get_physics(balance_trick_, kLeanGravityStat));
    auto instable_rate = static_cast<float>(get_physics(balance_trick_, kInstableRate));
    const double instable =
        get_physics(balance_trick_, kInstableBase) + instable_rate * static_cast<double>(on_rail_seconds_);
    const double grown = (gravity * instable * frame * kFrames + 1.0) * lean_;
    lean_ = static_cast<float>(grown + instable * lean_speed_ * frame * kFrames);
    const bool left = get_held(input_, left_button_);
    const bool right = get_held(input_, right_press_);
    if (!left && !right)
        pushed_ = true;
    auto safe_period = static_cast<uint32_t>(script::fetch_int_value(kBalanceSafeButtonPeriod));
    const uint32_t elapsed = get_clock() - start_time_;
    if (!pushed_ && elapsed > ints::as_unsigned(script::fetch_int_value(kBalanceIgnoreButtonPeriod)))
        pushed_ = true;
    if (left && pushed_) {
        // Pushing back - not at all before the safe period while leaning the other way (the original's ratio is an
        // integer division: 0 then).
        float scale = 1.0f;
        if (lean_ < 0.0f && elapsed < safe_period)
            scale = static_cast<float>(elapsed / safe_period); // NOLINT(bugprone-integer-division): the original's
        lean_speed_ = static_cast<float>(lean_speed_ - get_physics(balance_trick_, kLeanAcc) * scale * frame * kFrames);
    } else if (right && pushed_) {
        float scale = 1.0f;
        if (lean_ > 0.0f && elapsed < safe_period)
            scale = static_cast<float>(elapsed / safe_period); // NOLINT(bugprone-integer-division): the original's
        lean_speed_ = static_cast<float>(get_physics(balance_trick_, kLeanAcc) * scale * frame * kFrames + lean_speed_);
    } else {
        // No button: a random shake.
        const float speed = std::fabs(lean_speed_);
        if (get_physics(balance_trick_, kLeanMinSpeed) > speed) {
            const int range = static_cast<int>(get_physics(balance_trick_, kLeanRndSpeed)) | 1;
            auto shake = static_cast<float>(maths::rnd(range));
            lean_speed_ = static_cast<float>(maths::get_sign(lean_speed_) * shake);
        } else {
            auto shake = static_cast<float>(maths::rnd(kShakeRange));
            lean_speed_ = static_cast<float>(maths::get_sign(lean_speed_) * shake * kShakeScale + lean_speed_);
        }
    }
    if (!skater_->own_script()) {
        skater_->set_own_script(script::Script::create());
    }
    // Past the bail angle: the skater falls.
    const float lean = std::fabs(lean_);
    if (get_physics(balance_trick_, kLeanBailAngle) < lean) {
        skater_->post_own_event(lean_ > 0.0f ? kOffMeterTop : kOffMeterBottom, nullptr);
        lean_ = 0.0f;
        get_trick_type(balance_trick_) = 0;
    }
    // The balance meter (the anim blender's).
    if (show_meter_) {
        bool flip = flip_;
        if (mirrored_) {
            auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater_->fetch_module(kSkaterCorePhysics));
            if (core->state()->flag(PoseFlag::kFlipped).on)
                flip = !flip;
        }
        if (ObjectModule* blender = skater_->fetch_module(kAnimBlender)) {
            const double position = flip ? 4096.0 - lean_ : lean_ + 4096.0;
            core::down_cast<AnimBlenderComponent>(blender)->first_channel_47e_280(
                static_cast<float>(position * kPositionToUnits), 1);
        }
    }
    auto shown = static_cast<float>(lean_ * -kLeanToUnits);
    if (hide_display_ || !get_trick_type(balance_trick_))
        return;
    const bool up_down =
        (left_button_ == kUp || left_button_ == kDown) && (right_press_ == kUp || right_press_ == kDown);
    void* score = score_display(points_held_);
    if (!score)
        return;
    if (up_down)
        show_lean2(score, 1, shown);
    else
        show_lean(score, 1, shown);
}

} // namespace objects
