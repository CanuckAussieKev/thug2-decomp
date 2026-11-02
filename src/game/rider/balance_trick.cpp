// The skater's balance trick component (GameObjects::RiderTeeterMoves): see balance_trick.h.

#include "game/rider/balance_trick.h"

#include <cstdint>
#include <new>
#include <gsl/pointers>
#include <array>
#include <span>

#include "engine/core/slab_bank.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/point_tally.h"
#include "game/rider/rider.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/skater_score.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

std::array<script::MemberFunctionEntry, 2>& balance_trick_member_functions()
{
    static std::array<script::MemberFunctionEntry, 2> balance_trick_member_functions{{
        {.callsign_ = 0x6DF82E70, .owner_kind_ = 0x589E5FB3}, {.callsign_ = 0, .owner_kind_ = 0x589E5FB3}}};
    return balance_trick_member_functions;
}

namespace {

constexpr uint32_t kAnimBlender = 0x7149B753, kSkaterScore = 0xD262802F;

// Member functions
constexpr uint32_t kSwitchOffBalanceMeter = 0x78E669AB, kSwitchOnBalanceMeter = 0x73727701,
                   kStartBalanceTrick = 0x6CC475B7, kAdjustBalance = 0x7A0DCD4B, kSetWobbleDetails = 0xEA6D0EFD,
                   kStopBalanceTrick = 0xE553A5B8, kDoBalanceTrick = 0x7E92670C;
// Parameters
constexpr uint32_t kTimeAdd = 0x3A4E9413, kLeanAdd = 0x0B4CECC1, kPaceBoost = 0xEB9ECABB, kDurationScale = 0x4C074698,
                   kLeanMult = 0x24EBF718, kSpeedMult = 0x94DBBD1C, kWobble = 0x6D941203,
                   kDefaultWobbleParams = 0x5ADB96D4, kWobbleParams = 0xD095379C, kEquilibriumArgs = 0x647FACC2,
                   kTweak = 0x2BFD3D35, kButtonA = 0x4F1A0B01, kButtonB = 0xD6135ABB, kTypeName = 0x7321A8D6,
                   kIsATap = 0x2335E1E1, kDoFlipCheck = 0x1304E677, kPlayRangeAnimBackwards = 0x08FE31ED,
                   kClearCheese = 0xBBDB0F78;
// The wobble's values (each a structure for get_scaled_value).
constexpr std::array<uint32_t, 5> kWobbleNames = {{0xFD266A26 /* WobbleAmpA */, 0x642F3B9C /* WobbleAmpB */,
                                      0x0F43FD49 /* WobbleK1 */, 0x964AACF3 /* WobbleK2 */,
                                      0xF90B0824 /* SpazFactor */}};
// The physics structures of the types.
constexpr uint32_t kGrindParams = 0x6B7FDADD, kManualParams = 0xB8CC8633, kSkitchParams = 0xFC87CC01,
                   kLipParams = 0xBE615D0A;

Rider* skater_of(GameEntity* object) { return core::down_cast<Rider>(object); }

} // namespace

// 0x004F9170
gsl::owner<ObjectModule*> RiderTeeterMoves::fabricate() { return new RiderTeeterMoves; }

// 0x004F8CD0
void RiderTeeterMoves::publish_method_table()
{
    script::register_member_function_table(balance_trick_member_functions());
}

// 0x004F8F80
RiderTeeterMoves::RiderTeeterMoves()
{
    // (the four balances - manual_, grind_, lip_, skitch_ - are constructed first, as members)
    kind_tag_ = kType;
}

// 0x004F8CF0 (0x004F9010 the deleting one)
// (the balances' destructors do nothing)
RiderTeeterMoves::~RiderTeeterMoves() = default;

// 0x004F9040
void RiderTeeterMoves::configure_from_data(script::Struct* /*params*/)
{
    manual_.prepare_all(entity_);
    grind_.prepare_all(entity_);
    lip_.prepare_all(entity_);
    skitch_.prepare_all(entity_);
}

// 0x004F9080
void RiderTeeterMoves::seal()
{
    anim_blender_ptr_ = core::down_cast<AnimBlenderComponent>(entity_->fetch_module(kAnimBlender));
}

// 0x004F8D70
int RiderTeeterMoves::in_balance_move(script::Struct* /*params*/, script::Script* /*script*/)
{
    return started_ ? 1 : 0;
}

// 0x004F8D80
void RiderTeeterMoves::clear_best_times()
{
    manual_.clear_best_time();
    grind_.clear_best_time();
    lip_.clear_best_time();
    skitch_.clear_best_time();
}

// 0x004F8DB0
void RiderTeeterMoves::keep_best_times()
{
    manual_.keep_best_time();
    grind_.keep_best_time();
    lip_.keep_best_time();
    skitch_.keep_best_time();
}

// 0x004F8DE0
void RiderTeeterMoves::get_reset()
{
    manual_.get_reset();
    grind_.get_reset();
    skitch_.get_reset();
    lip_.get_reset();
    trick_ = 0;
    started_ = false;
}

// 0x004F8EE0
void RiderTeeterMoves::halt()
{
    manual_.halt();
    grind_.halt();
    lip_.halt();
    skitch_.halt();
    trick_ = 0;
    started_ = false;
}

template <typename Self>
auto* RiderTeeterMoves::balance_of(Self* self)
{
    using Result = decltype(&self->manual_);
    switch (self->trick_) {
    case kManual:
    case kNoseManual:
        return &self->manual_;
    case kGrind:
    case kSlide:
        return &self->grind_;
    case kSkitch:
        return &self->skitch_;
    case kLip:
        return &self->lip_;
    default:
        return static_cast<Result>(nullptr);
    }
}
Balance* RiderTeeterMoves::at_hand() { return balance_of(this); }
const Balance* RiderTeeterMoves::at_hand() const { return balance_of(this); }

// 0x004F8E20
void RiderTeeterMoves::get_buttons(int* count, std::span<uint32_t> buttons) const
{
    const Balance* balance = at_hand();
    if (!balance) {
        *count = 0;
        return;
    }
    *count = 2;
    core::at(buttons, 0) = balance->left_button();
    core::at(buttons, 1) = balance->right_press();
}

// 0x004F8F20
float RiderTeeterMoves::get_lean() const
{
    const Balance* balance = at_hand();
    return balance ? balance->lean() : 0.0f;
}

// 0x004F90A0
float RiderTeeterMoves::get_physics_value(uint32_t name)
{
    if (call_args_)
        return skater_of(entity_)->get_scaled_param(name, call_args_);
    uint32_t structure = 0;
    switch (trick_) {
    case kGrind:
    case kSlide:
        structure = kGrindParams;
        break;
    case kManual:
    case kNoseManual:
        structure = kManualParams;
        break;
    case kSkitch:
        structure = kSkitchParams;
        break;
    case kLip:
        structure = kLipParams;
        break;
    default:
        return 0.0f;
    }
    Rider* skater = skater_of(entity_);
    return skater->get_scaled_param(name, script::read_sub_struct(structure));
}

// 0x004F91D0
void RiderTeeterMoves::clear_params()
{
    if (script::Struct* params = call_args_) {
        params->get_clear();
        script::delete_struct(params);
        call_args_ = nullptr;
    }
}

// 0x004F9370
void RiderTeeterMoves::change_args(script::Struct* params)
{
    if (!call_args_)
        call_args_ = script::new_struct();
    call_args_->get_clear();
    call_args_->append_structure_if_any(params);
}

// 0x004F9200
void RiderTeeterMoves::set_wobble(script::Struct* wobble)
{
    if (!entity_->own_script()) {
        void* memory = script::script_pools().carve(0xF4);
        entity_->set_own_script(memory ? static_cast<script::Script*>(memory)->make_new() : nullptr);
    }
    std::array<float, 5> values{};
    script::Struct* value = nullptr; // (not cleared again: a missing one leaves the last found)
    for (int i = 0; i < 5; ++i) {
        wobble->read_sub_struct(core::at(kWobbleNames, i), &value, 1);
        core::at(values, i) = skater_of(entity_)->get_scaled_value(value);
    }
    anim_blender_ptr_->first_channel_47e_450(values, 1);
}

// 0x004F9420
int RiderTeeterMoves::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    switch (name) {
    case kSwitchOffBalanceMeter: {
        auto* score = entity_->fetch_module(kSkaterScore);
        modules::PointTally* display = core::down_cast<RiderPointKeeper>(score)->points_held_;
        display->set_teeter_gauge(false, 0.0f);
        display->set_wheelie_gauge(false, 0.0f);
        manual_.hide_display();
        grind_.hide_display();
        lip_.hide_display();
        skitch_.hide_display();
        return 1;
    }
    case kSwitchOnBalanceMeter:
        manual_.show_display();
        grind_.show_display();
        lip_.show_display();
        skitch_.show_display();
        return 1;
    case kStartBalanceTrick:
        started_ = true;
        return 1;
    case kAdjustBalance: {
        if (!trick_)
            return 1;
        float time_add = 0.0f, lean_add = 0.0f, speed_add = 0.0f, time_mult = 1.0f, lean_mult = 1.0f, speed_mult = 1.0f;
        params->fetch_float(kTimeAdd, &time_add, 0);
        params->fetch_float(kLeanAdd, &lean_add, 0);
        params->fetch_float(kPaceBoost, &speed_add, 0);
        params->fetch_float(kDurationScale, &time_mult, 0);
        params->fetch_float(kLeanMult, &lean_mult, 0);
        params->fetch_float(kSpeedMult, &speed_mult, 0);
        // (an unknown type has no balance: the original crashes here)
        Balance* balance = at_hand();
        const double time = static_cast<double>(time_mult) * balance->on_rail_seconds() + time_add;
        balance->set_on_rail_seconds(time < 0.0 ? 0.0f : static_cast<float>(time));
        // The lean and its speed scaled, then pushed further the way they lean.
        const double lean = static_cast<double>(lean_mult) * balance->lean();
        balance->set_lean(static_cast<float>(lean));
        balance->set_lean(static_cast<float>((lean < 0.0 ? -1.0f : 1.0f) * static_cast<double>(lean_add) + lean));
        const double speed = static_cast<double>(speed_mult) * balance->lean_speed();
        balance->set_lean_speed(static_cast<float>(speed));
        balance->set_lean_speed(static_cast<float>((speed < 0.0 ? -1.0f : 1.0f) * static_cast<double>(speed_add) + speed));
        return 1;
    }
    case kSetWobbleDetails: {
        if (params->has_bare_flag(kWobble)) {
            manual_.show_meter();
            grind_.show_meter();
            lip_.show_meter();
            skitch_.show_meter();
        }
        script::Struct* wobble = script::read_sub_struct(kDefaultWobbleParams); // (asserted)
        params->read_sub_struct(kWobbleParams, &wobble, 0);
        set_wobble(wobble);
        return 1;
    }
    case kStopBalanceTrick:
        halt();
        return 1;
    case kDoBalanceTrick: {
        clear_params();
        script::Struct* balance_params = nullptr;
        if (params->read_sub_struct(kEquilibriumArgs, &balance_params, 0))
            change_args(balance_params);
        int tweak = 0;
        params->fetch_int_value(kTweak, &tweak, 0);
        uint32_t button_a = 0, button_b = 0, type = 0;
        params->read_hash_value(kButtonA, &button_a, 1);
        params->read_hash_value(kButtonB, &button_b, 1);
        params->read_hash_value(kTypeName, &type, 0);
        // The same trick (a manual and a nose manual count as one) or a tap: carried on.
        if (trick_ == type || (trick_ == kManual && type == kNoseManual) || (trick_ == kNoseManual && type == kManual))
            return 1;
        int tap = 0;
        if (params->fetch_int_value(kIsATap, &tap, 0))
            return 1;
        trick_ = type;
        const bool do_flip = params->has_bare_flag(kDoFlipCheck);
        const bool backwards = params->has_bare_flag(kPlayRangeAnimBackwards);
        const bool clear_cheese = params->has_bare_flag(kClearCheese);
        Balance* balance = at_hand();
        if (!balance)
            return 1;
        if (clear_cheese)
            balance->get_reset();
        balance->keep_best_time();
        balance->kickoff(button_a, button_b, static_cast<uint32_t>(tweak), do_flip, backwards);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
