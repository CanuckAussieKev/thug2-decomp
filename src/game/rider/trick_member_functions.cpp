// The Trick component's script member functions (GameObjects::MoveExecutor::invoke_method_by_name, 0x00523560, vtable
// +0x30): see trick_component.h.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "platform/stopwatch.h"
#include "game/goals/goal_manager.h"
#include "game/skate/point_tally.h"
#include "game/rider/balance_trick.h"
#include "game/rider/core_physics.h"
#include "game/rider/gap_entry.h"
#include "game/rider/stats_manager.h"
#include "game/rider/skater_state.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {


// Member function names.
constexpr uint32_t kReleased = 0x04BA9EE9, kGetSpazzFactor = 0x00271B04, kUseEventsX = 0x070E934D,
                   kClearPanelLanded = 0x11CA5C42, kClearQuickGetup = 0x190B85BC, kGetActualSpin = 0x1CA450FB,
                   kSetManualTricks = 0x1FCF080A, kBashOff = 0x290F6010, kPassToTrickObjects = 0x36B2BE57,
                   kNoQuickGetup = 0x444C0E5A, kSetTrickName = 0x4B2EE2BC, kGetSpin = 0x4BA7719F,
                   kClearTrickQueue = 0x54C33B20, kRestoreEvents = 0x54D43789, kSetExtraGrindTricks = 0x6BA35AB4,
                   kFunction72c926cA = 0x72C926CA, kSetExtraTricks = 0x7627DC71, kDoNextManualTrick = 0x788F9A4E,
                   kClearEventBuffer = 0x86928082, kClearManualTrick = 0x8E3D3BEC, kClearExtraGrindTrick = 0xAB6B8DD9,
                   kReplaceBindings = 0xABFF7889, kSetTrickSlot = 0xB42A1230, kClearPanelBailed = 0xB8045433,
                   kClearTricksFrom = 0xCB30572D, kSetTrickScore = 0xCB3A8FD2, kUseGrindEvents = 0xD4099D35,
                   kKillExtraTricks = 0xD5813251, kBashOn = 0xD872002D, kSetQueueTricks = 0xD8C79BB0,
                   kFunctionDe98dE7a = 0xDE98DE7A, kResetSpin = 0xE50D471E, kHeld = 0xEDA2772E, kDisplay = 0xF32E8D5C;
// Parameter and other names.
constexpr uint32_t kSpazzFactor = 0x679B2500, kX = 0x7323E97C, kSpinCountSlop = 0x50C5CC2F, kSkaterId = 0x5B24FAAA,
                   kSkaterExitCombo = 0x4B3CE1FE, kSkaterGap = 0x11B4C67F, kSkaterLanded = 0xC98BA111,
                   kSkaterBailed = 0x6045A960, kSkaterTrickDisplayed = 0x11D8BC9E, kSpin = 0xEDF5DB70,
                   kSpecial = 0xB394C01C, kDuration = 0x79A07F3F, kUsedBy = 0x9694FC41, kRegular = 0xB58EFC2B,
                   kExtra = 0xB2C0F29A, kManual = 0xEF24413B, kUsedByEc066b6a = 0xEC066B6A, kButtonsName = 0xBCA37A49,
                   kOlderThan = 0xCDCFEE8A, kSlot = 0x53F1DF98, kTrick = 0x270F56E1, kNoQueue = 0x9E22D268,
                   kDeferred = 0x5C9B1765, kAddSpin = 0x83CB0082, kBlockspin = 0xC2150BB0, kNoDegrade = 0x3C24AC46,
                   kNoSpecial = 0x559BE3ED, kCat = 0x61A1BC57, kNatas = 0x7A57EB44, kNoMult = 0x62F7C401,
                   kNoSwitch = 0xFC6944A8;

constexpr float kFullTurn = 360.0f; // 0x00645EA8
constexpr float kHalfTurn = 180.0f; // 0x0064B278
constexpr float kSpinMargin = 0.1f; // 0x00645D74 (0.100000001f)

// The fields (see trick_queue.cpp).
constexpr uint32_t kScore = 0x1C, kBalanceTrick = 0x24, kCorePhysics = 0x28, kComponent34 = 0x34;
constexpr uint32_t kIsSpecial = 0xA24, kTrickBindingsSlot = 0xA28, kQueueHead = 0xA2C, kQueueTail = 0xA30;
constexpr uint32_t kQueueCount = 0x1204, kQueueArrays = 0x1208, kQueueSpecial = 0x1230, kEventsUse = 0x1234;
constexpr uint32_t kManualCount = 0x1238, kManualArrays = 0x123C, kManualSpecial = 0x1264;
constexpr uint32_t kGrindCount = 0x1280, kGrindArrays = 0x1284, kGrindSpecial = 0x12AC;
constexpr uint32_t kExtraOn = 0x12C8, kBashing = 0x1330;
constexpr uint32_t kDeferredScore = 0x139C, kDeferredFlags = 0x13A0;
constexpr uint32_t kBailed = 0x13A8, kBailedPanel = 0x13A9, kTrickCount = 0x13AC, kTrickObjectsOn = 0x13B0,
                   kTrickObjects = 0x13B4, kQuickGetupDone = 0x1BB8;

// strcpy (inline in the original: no length limit).
void copy_text(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
// The original's _ftol (0x006236FC): truncated toward zero.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rest of the component (0x0051EA90-0x00521B8F) and its neighbours, not rewritten here.
bool is_pad_held(MoveExecutor* self, uint32_t button) { return self->is_key_held(button); }
bool is_switch(MoveExecutor* self) { return (self->core_physics_->is_sides_swapped() & 0xFF) != 0; }
// The score component (+0x1C): the trick's points (0x00563990), the spin's (0x00563F30).
void score_trick(MoveExecutor* self, const char* name, int score, uint32_t flags)
{
    self->score_->fire_off(name, score, flags);
}
void score_spin(MoveExecutor* self, int degrees) { self->score_->change_spin_degrees(degrees); }

// The trick names of unnamed name components (SetQueueTricks and the others): into `arrays`, counted at `count`.
void collect_names(script::Struct* params, int& count, uint32_t* arrays)
{
    count = 0;
    for (script::Component* c = params->step_to_entry(nullptr); c; c = params->step_to_entry(c)) {
        if (c->name == 0 && (c->type_and_flag_ & 0xFE) == script::kQbvalName << 1)
            arrays[count++] = c->value.word();
    }
}

// The combo the score holds (its +0x14 times its +0x60) handed to SkaterExitCombo listeners with the object's id.
void exit_combo(MoveExecutor* self)
{
    const modules::PointTally* score = self->score_;
    if (!truncate(static_cast<double>(score->pot_) * score->multiplier_))
        return;
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->put_name_key(kSkaterId, self->entity_->ident_);
    self->entity_->announce(kSkaterExitCombo, params, 0.0f);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// Whether the spin (in degrees, unsigned) is a whole turn - 360 on a rail or in the score's special case, else 180 -
// less the skater's spin_count_slop, with 0.1 to spare.
bool spin_counts(MoveExecutor* self, bool full_turn)
{
    const float spin = std::fabs(self->spin_total_);
    const double slop = read_tuning_float(kSpinCountSlop, 1, static_cast<GameEntity*>(self->entity_));
    const double threshold = (static_cast<double>(full_turn ? kFullTurn : kHalfTurn) - slop) + kSpinMargin;
    return threshold <= spin;
}

// Whether the score shows the trick named (its 0x00561640 against the name's checksum, taken first).
bool same_trick_shown(MoveExecutor* self, void* score)
{
    const uint32_t name = checksums::hash_from_text(&core::at(self->trick_name_, 0));
    return static_cast<modules::PointTally*>(score)->fetch_newest_trick_key() == name;
}

} // namespace

// 0x00523560
int MoveExecutor::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kReleased: {
        uint32_t button = 0;
        params->read_hash_value(0u, &button, 0);
        if (!button)
            return 0;
        return is_pad_held(this, button) ? 0 : 1;
    }
    case kGetSpazzFactor:
        script->args_->put_int(kSpazzFactor, truncate(count_triangle_bashes()));
        return 1;
    case kUseEventsX:
        // Every event of the button x marked used (+0x14), in the buffer's order.
        for (int i = 0; std::cmp_less(i, event_count_); ++i) {
            TrickEvent* event = &core::at(events_, i);
            if (event->button == kX)
                event->flags = 1;
        }
        return 1;
    case kClearPanelLanded: {
        modules::PointTally* score = score_;
        if (entity_->is_local_skater_) {
            char* deferred = &core::at(deferred_name_, 0);
            if (*deferred) {
                const bool on_rail = core_physics_->landed_from_air_vert_ != 0;
                if (spin_counts(this, on_rail)) {
                    score_trick(this, deferred, deferred_score_, deferred_flags_);
                    score_spin(this, truncate(spin_total_));
                }
                *deferred = 0;
            }
            goals::get_objective_board()->check_trick_goals(); // the goal manager
            exit_combo(this);
            static_cast<void>(entity_->fetch_module(kSkaterGap)); // (its only use was ECX of the call below)
            pay_out_queued_gaps();
            static_cast<modules::PointTally*>(score)->land();
            spin_total_ = 0.0f;
            skater_state_->in_trick_move_ = false;
            stats_manager_->announce_met_goals();
        }
        entity_->announce(kSkaterLanded, nullptr, 0.0f);
        if (bailed_)
            bailed_panel_ = true;
        trick_count_ = 0;
        balance_trick_->keep_best_times();
        balance_trick_->get_reset();
        core_physics_->special_friction_index_ = 0;
        return 1;
    }
    case kClearQuickGetup:
        quick_getup_done_ = false;
        return 1;
    case kGetActualSpin:
        script->args_->push_float(kSpin, static_cast<float>(truncate(std::fabs(spin_total_))));
        return 1;
    case kSetManualTricks:
        collect_names(params, manual_.count_, &core::at(manual_.arrays_, 0));
        manual_.special_ = 0;
        params->read_hash_value(kSpecial, &manual_.special_, 0);
        return 1;
    case kBashOff:
        bashing_ = false;
        return 1;
    case kPassToTrickObjects: {
        uint32_t value = 0;
        params->read_hash_value(0u, &value, 1);
        if (trick_objects_on_)
            trick_objects_.append_one(value);
        return 1;
    }
    case kNoQuickGetup:
        quick_getup_done_ = true;
        return 1;
    case kSetTrickName: {
        const char* text = nullptr;
        params->fetch_text(0u, &text, 0);
        if (text)
            copy_text(&core::at(trick_name_, 0), text);
        else
            core::at(trick_name_, 0) = 0;
        return 1;
    }
    case kGetSpin: {
        const float spin = spin_total_;
        const double slop = read_tuning_float(kSpinCountSlop, 1, static_cast<GameEntity*>(entity_));
        const int turns = truncate(slop + std::fabs(spin)) / 180;
        script->args_->push_float(kSpin, static_cast<float>(turns * static_cast<double>(kHalfTurn)));
        return 1;
    }
    case kClearTrickQueue:
        queue_last_ = -1;
        queue_first_ = -1;
        return 1;
    case kRestoreEvents: {
        // The events used by UsedBy= (regular / extra / manual / 0xEC066B6A) within duration= (200) ms made
        // usable again.
        int duration = 200;
        params->fetch_int_value(kDuration, &duration, 0);
        uint32_t used_by = 0;
        params->read_hash_value(kUsedBy, &used_by, 0);
        uint32_t mask = 0;
        switch (used_by) {
        case kRegular:
            mask = 1;
            break;
        case kExtra:
            mask = 2;
            break;
        case kManual:
            mask = 4;
            break;
        case kUsedByEc066b6a:
            mask = 8;
            break;
        default:
            break;
        }
        int index = event_head_;
        for (int i = 0; std::cmp_less(i, event_count_); ++i) {
            TrickEvent* event = &core::at(events_, index);
            if (frame_clock::get_time() - event->time >= static_cast<uint32_t>(duration))
                break;
            if (event->flags & mask)
                event->flags &= ~mask;
            if (--index < 0)
                index += 100;
        }
        return 1;
    }
    case kSetExtraGrindTricks:
        collect_names(params, grind_.count_, &core::at(grind_.arrays_, 0));
        grind_.special_ = 0;
        params->read_hash_value(kSpecial, &grind_.special_, 0);
        return 1;
    case kFunction72c926cA:
        dump_event_buffer();
        return 1;
    case kSetExtraTricks:
        set_extra_tricks(params, script);
        return 1;
    case kDoNextManualTrick:
        do_next_manual_trick(params);
        return 1;
    case kClearEventBuffer: {
        // buttons= with OlderThan=: each button's events (0x0051F610); else the whole buffer (0x0051EB00).
        script::Array* buttons = nullptr;
        if (params->fetch_list(kButtonsName, &buttons, 0)) {
            int older_than = 0;
            params->fetch_int_value(kOlderThan, &older_than, 0);
            const int count = static_cast<int>(buttons->extent_);
            for (int i = 0; i < count; ++i)
                expire_button(buttons->fetch_part(i), static_cast<uint32_t>(older_than));
            return 1;
        }
        clear_event_buffer();
        return 1;
    }
    case kClearManualTrick:
        clear_block1238();
        return 1;
    case kClearExtraGrindTrick:
        clear_block1280();
        return 1;
    case kReplaceBindings: {
        // The trick mappings replaced by the global structure the first (unnamed name) component names.
        const script::Component* c = params->step_to_entry(nullptr);
        if (!c || c->name != 0 || (c->type_and_flag_ & 0xFE) != script::kQbvalName << 1)
            return 1;
        script::Struct* mappings = script::read_sub_struct(c->value.word());
        if (!mappings)
            return 1;
        trick_lists_->get_clear();
        trick_lists_->append_structure_if_any(mappings);
        return 1;
    }
    case kSetTrickSlot: {
        // slot= mapped to the created trick trick= (a pair: 13, the trick's index).
        uint32_t slot = 0;
        params->read_hash_value(kSlot, &slot, 0);
        uint32_t trick = 0;
        params->read_hash_value(kTrick, &trick, 0);
        trick_lists_->insert_pair(slot, 13.0f, static_cast<float>(static_cast<double>(trick)));
        return 1;
    }
    case kClearPanelBailed: {
        if (entity_->is_local_skater_) {
            exit_combo(this);
            score_->get_bail(false);
        }
        entity_->announce(kSkaterBailed, nullptr, 0.0f);
        bailed_ = true;
        bailed_panel_ = true;
        trick_objects_on_ = false;
        HitClusterLog& list = trick_objects_;
        list.get_clear();
        trick_count_ = 0;
        balance_trick_->get_reset();
        core_physics_->end_skitch();
        spin_total_ = 0.0f;
        core_physics_->special_friction_index_ = 0;
        list.get_clear();
        static_cast<void>(entity_->fetch_module(kSkaterGap)); // (its only use was ECX of the call below)
        discard_queued_gaps();
        stats_manager_->get_reset();
        return 1;
    }
    case kClearTricksFrom:
        for (script::Component* c = params->step_to_entry(nullptr); c; c = params->step_to_entry(c)) {
            if (c->name == 0 && (c->type_and_flag_ & 0xFE) == script::kQbvalName << 1)
                remove_trick_list(c->value.word());
        }
        return 1;
    case kSetTrickScore:
        params->fetch_int_value(0u, &trick_score_, 0);
        return 1;
    case kUseGrindEvents:
        list_used_ = 8;
        return 1;
    case kKillExtraTricks:
        extra_.on_ = false;
        return 1;
    case kBashOn:
        bashing_ = true;
        clear_event_buffer();
        return 1;
    case kSetQueueTricks:
        collect_names(params, extra_list_count_, &core::at(extra_lists_, 0));
        if (extra_list_count_ == 1 && !script::fetch_list(core::at(extra_lists_, 0)))
            core::at(extra_lists_, 0) = kNoQueue;
        manual_list_ = 0;
        params->read_hash_value(kSpecial, &manual_list_, 0);
        return 1;
    case kFunctionDe98dE7a:
        queue_all_done_tricks();
        return 1;
    case kResetSpin:
        spin_total_ = 0.0f;
        return 1;
    case kHeld: {
        // buttons=: whether any is held; else the unnamed button's state (0x0051ECF0).
        script::Array* buttons = nullptr;
        if (params->fetch_list(kButtonsName, &buttons, 0)) {
            const int count = static_cast<int>(buttons->extent_);
            for (int i = 0; i < count; ++i) {
                // NOT ORIGINAL - THAW's BMX (the feature on, on the bike): THAW's stick directions too (its trick
                // lists' buttons=[rightright, RightUpRight, RightDownRight]). Original behaviour: THUG2's only.
                if (skater::thaw_bmx::is_switched_on()) {
                    const int held = skater::thaw_bmx::trick_stick_held(this, buttons->fetch_part(i));
                    if (held >= 0) {
                        if (held)
                            return 1;
                        continue;
                    }
                }
                if (core::at(btn_down_, pad_input::glyph_for_button_tag(buttons->fetch_part(i))))
                    return 1;
            }
            return 0;
        }
        uint32_t button = 0;
        params->read_hash_value(0u, &button, 0);
        if (!button)
            return 0;
        return is_pad_held(this, button) ? 1 : 0;
    }
    case kDisplay: {
        // The trick displayed (SetTrickName's): deferred (until ClearPanel_Landed) with Deferred; AddSpin=: spin
        // added while the same trick shows; else scored (on a rail its score times the score's rail multiplier),
        // SkaterTrickDisplayed, its spin scored when it's a whole turn (always off a rail).
        modules::PointTally* score = score_;
        int add_spin = 0;
        if (params->has_bare_flag(kDeferred)) {
            copy_text(&core::at(deferred_name_, 0), &core::at(trick_name_, 0));
            deferred_score_ = trick_score_;
            if (is_switch(this))
                deferred_flags_ |= 4;
        } else if (params->fetch_int_value(kAddSpin, &add_spin, 0) && same_trick_shown(this, score)) {
            const double spin = static_cast<double>(add_spin) + spin_total_;
            spin_total_ = static_cast<float>(spin);
            if (entity_->is_local_skater_)
                static_cast<modules::PointTally*>(score)->advance_turn(truncate(spin));
        } else {
            uint32_t flags = 0;
            if (params->has_bare_flag(kBlockspin))
                flags = 2;
            if (params->has_bare_flag(kNoDegrade))
                flags |= 0x40;
            if (is_switch(this))
                flags |= 4;
            if (starting_special_) {
                if (params->has_bare_flag(kNoSpecial))
                    starting_special_ = false;
                else
                    flags |= 0x20;
            }
            if (params->has_bare_flag(kCat))
                flags |= 0x80;
            if (params->has_bare_flag(kNatas))
                flags |= 0x100;
            if (params->has_bare_flag(kNoMult))
                flags |= 0x200;
            if (params->has_bare_flag(kNoSwitch))
                flags |= 0x400;
            if (entity_->is_local_skater_) {
                auto* core = core_physics_;
                if (core->state_->cur_phase_ == 4) {
                    const int trick_score = trick_score_;
                    if (trick_score) {
                        const float multiplier = score->robot_factor_;
                        debug::printf(
                            "RAIL score %d * %2.3f = %d\n", trick_score, static_cast<double>(multiplier),
                            truncate(static_cast<double>(trick_score) * multiplier)); // "RAIL score %d * %2.3f = %d\n"
                        trick_score_ = truncate(static_cast<double>(trick_score_) * multiplier);
                    }
                }
                const char* trick_name = &core::at(trick_name_, 0);
                score_trick(this, trick_name, trick_score_, flags);
                if (*trick_name) {
                    entity_->announce(kSkaterTrickDisplayed, nullptr, 0.0f);
                    const bool special = core->state_->flag(PoseFlag::kVertAir).on;
                    if ((!special && !static_cast<bool>(core->landed_from_air_vert_)) || spin_counts(this, true))
                        score_spin(this, truncate(spin_total_));
                }
            }
            if (params->has_bare_flag(kBlockspin))
                spin_total_ = 0.0f;
            core::at(deferred_name_, 0) = 0;
        }
        if (core::at(trick_name_, 0))
            starting_special_ = false;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
