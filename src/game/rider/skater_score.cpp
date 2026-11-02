// The skater's score component (GameObjects::RiderPointKeeper): see skater_score.h.

#include "game/rider/skater_score.h"

#include <cstdint>
#include <gsl/pointers>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/rider.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterScore = 0xD262802F;
constexpr uint32_t kCheatAlwaysSpecial = 0xEEFECF1B;
// Member functions
constexpr uint32_t kLastScoreLandedLessThan = 0x3C445A82, kGetSpecialValue = 0x3AF0AEDC, kIsLatestTrick = 0x2877B61A,
                   kGotSpecial = 0x5589B902, kCurrentScorePotLessThan = 0x56645FC6, kForceSpecial = 0x5E373400,
                   kTotalScoreGreaterThan = 0x8BF43831, kGetTotalScore = 0x8F2FE3D3,
                   kGetNumberOfNonGapTricks = 0xC068EA59, kTweakTrick = 0xBE3E5463, kIncrementScore = 0xBC30CA4E,
                   kCurrentScorePotGreaterThan = 0xD19BDB50, kSetSpecialValue = 0xF4EE54F0,
                   kLastScoreLandedGreaterThan = 0xE0112AAB, kTotalScoreLessThan = 0xDF14BD98;
// Parameters and results
constexpr uint32_t kValue = 0xE288A7CB, kKeycombo = 0x95E16467, kSpin = 0xEDF5DB70, kNumTaps = 0xC82CF71B,
                   kTrickText = 0x3EAFA520, kTotalScore = 0xEE5B2B48, kNumberOfNonGapTricks = 0xFFE7E02C;

// The score (modules::PointTally).
modules::PointTally* get_score_of(modules::PointTally* score) { return score; }
int total_score(modules::PointTally* score) { return score->get_total_score(); }
// The score so far times the multiplier, truncated as the original's _ftol.
int score_pot(modules::PointTally* score)
{
    const double pot = static_cast<double>(score->pot()) * score->multiplier();
    return static_cast<int>(static_cast<int64_t>(pot));
}
int last_score_landed(modules::PointTally* score) { return score->pot_score(); }

// The career (the skate module 0x007CE478's +0x20; not rewritten here).
RiderStoryProgress* get_career() { return modules::Skate::sole()->career().get(); }

// The unnamed integer (asserted). Without one the original's variable keeps what was in its stack slot - the
// `params` argument it reuses - so that's the value then.
int number_parameter(script::Struct* params)
{
    int value = 0; // (missing: 0 - the original read an argument's stack slot, an address)
    params->fetch_int_value(uint32_t{0}, &value, 1);
    return value;
}

} // namespace

// 0x00518B60
gsl::owner<ObjectModule*> RiderPointKeeper::fabricate() { return new RiderPointKeeper; }

// (the constructor: inlined in fabricate in the original)
RiderPointKeeper::RiderPointKeeper() { kind_tag_ = kSkaterScore; }

// 0x00518BE0 (0x00518C50 the deleting one)
RiderPointKeeper::~RiderPointKeeper()
{
    if (modules::PointTally* score = points_held_) {
        get_score_of(score)->~PointTally(); // (its destructor)
        memory::Hub::get_pull()->dealloc(score);
    }
}

// 0x00518660
void RiderPointKeeper::configure_from_data(script::Struct* /*params*/)
{
    if (points_held_)
        return;
    void* memory = memory::alloc_zeroed(0x1550);
    points_held_ = memory ? memory::construct_in<modules::PointTally>(memory) : nullptr; // (its constructor)
    get_score_of(points_held_)->set_rider_number(static_cast<int16_t>(entity_->ident()));
}

// 0x005186E0
void RiderPointKeeper::tick_frame()
{
    if (!entity_->is_local_skater()) {
        hold_off(true);
        return;
    }
    get_score_of(points_held_)->tick_frame();
    const bool always_special = core::down_cast<Rider>(entity_)->always_special() ||
                                get_career()->is_code_on(kCheatAlwaysSpecial);
    if (always_special) {
        get_career()->set_global_flag(script::fetch_int_value(kCheatAlwaysSpecial));
        get_score_of(points_held_)->fill_special();
        online::net_game_state().set_cheat_flag(true);
    }
}

// 0x00518630
void RiderPointKeeper::reset_score()
{
    get_score_of(points_held_)->get_reset();
    get_score_of(points_held_)->set_teeter_gauge(false, 0.0f);
    get_score_of(points_held_)->set_wheelie_gauge(false, 0.0f);
}

// 0x00518760
int RiderPointKeeper::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kLastScoreLandedLessThan: {
        const int value = number_parameter(params);
        return last_score_landed(points_held_) < value ? 1 : 0;
    }
    case kLastScoreLandedGreaterThan: {
        const int value = number_parameter(params);
        return last_score_landed(points_held_) > value ? 1 : 0;
    }
    case kTotalScoreLessThan: {
        const int value = number_parameter(params);
        return total_score(points_held_) < value ? 1 : 0;
    }
    case kTotalScoreGreaterThan: {
        const int value = number_parameter(params);
        return total_score(points_held_) > value ? 1 : 0;
    }
    case kCurrentScorePotLessThan: {
        const int value = number_parameter(params);
        return score_pot(points_held_) < value ? 1 : 0;
    }
    case kCurrentScorePotGreaterThan: {
        const int value = number_parameter(params);
        return score_pot(points_held_) > value ? 1 : 0;
    }
    case kGetSpecialValue:
        script->args()->put_int(kValue, points_held_->special());
        return 1;
    case kSetSpecialValue: {
        // (no value: the params pointer, as number_parameter's)
        int value = 0; // (missing: 0 - the original read an argument's stack slot, an address)
        params->fetch_int_value(kValue, &value, 0);
        points_held_->set_special(value);
        return 1;
    }
    case kGotSpecial:
        return points_held_->special_active() ? 1 : 0;
    case kForceSpecial:
        get_score_of(points_held_)->fill_special();
        return 1;
    case kGetTotalScore:
        script->args()->put_int(kTotalScore, total_score(points_held_));
        return 1;
    case kGetNumberOfNonGapTricks:
        script->args()->put_int(kNumberOfNonGapTricks, get_score_of(points_held_)->count_non_gap_tricks());
        return 1;
    case kTweakTrick: {
        int tweak = 0;
        params->fetch_int_value(uint32_t{0}, &tweak, 0);
        get_score_of(points_held_)->tweak_trick(tweak);
        return 1;
    }
    case kIncrementScore:
        get_score_of(points_held_)->set_grand_total(total_score(points_held_) + 1);
        return 1;
    case kIsLatestTrick: {
        // A trick by key combo or by name, with its spin (half turns) and taps (1 by default).
        uint32_t key_combo = 0;
        const char* text = nullptr;
        const bool by_key_combo = params->read_hash_value(kKeycombo, &key_combo, 0);
        if (!by_key_combo && !params->fetch_text(kTrickText, &text, 0))
            return 2;
        int spin = 0;
        if (params->fetch_int_value(kSpin, &spin, 0))
            spin /= 180;
        int taps = 1;
        params->fetch_int_value(kNumTaps, &taps, 0);
        const bool latest =
            by_key_combo
                ? get_score_of(points_held_)->last_trick_is_key_combo(key_combo, spin, false, taps)
                : get_score_of(points_held_)->last_trick_is(checksums::hash_from_text(text), spin, false, taps);
        return latest ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace objects
